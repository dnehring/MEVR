// SPDX-License-Identifier: MIT
// The scope capture's render target for the scope picture (plugin/ScopeCaptureSource.hpp, research/39). Present thread.

#include "plugin/ScopeCaptureSource.hpp"

#include "core/Log.hpp"
#include "core/vr/ScopeCapture.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace mevr::host::scope_capture_source {
namespace {

using Microsoft::WRL::ComPtr;
namespace sc = vr::scope_capture;

constexpr unsigned kResolveTries = 240;  // frames per render target before giving up on finding its texture
constexpr std::uint32_t kWarmupTicks = 3;  // without a pair arm: ticks after enabling before the texture counts

struct Range {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    bool Holds(std::uintptr_t p) const { return p >= begin && p < end; }
};

// A loaded module's image (from its PE header), or an empty range.
Range ImageOf(wchar_t const* name) {
    HMODULE const module = GetModuleHandleW(name);
    if (module == nullptr) {
        return {};
    }
    auto const base = reinterpret_cast<std::uintptr_t>(module);
    auto const* dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    auto const* nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
    return {base, base + nt->OptionalHeader.SizeOfImage};
}

struct Modules {
    Range d3d11;
    Range layers;  // the debug layer, if loaded
    Range layersOld;
    bool Holds(std::uintptr_t p) const { return d3d11.Holds(p) || layers.Holds(p) || layersOld.Holds(p); }
};

// ---- SEH-guarded probes (no C++ objects with destructors in these frames) --------------------------------------------
bool ReadRaw(std::uint64_t address, std::uint64_t* out) noexcept {
    __try {
        *out = *reinterpret_cast<std::uint64_t const volatile*>(static_cast<std::uintptr_t>(address));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The candidate is an ID3D11Texture2D of the render target's size, one sample, readable by shaders: returns it with a
// reference of its own (QueryInterface's), else null.
ID3D11Texture2D* ProbeRaw(std::uint64_t candidate, Modules const* modules, UINT width, UINT height, D3D11_TEXTURE2D_DESC* desc) noexcept {
    __try {
        auto const object = static_cast<std::uintptr_t>(candidate);
        if (modules->Holds(object)) {
            return nullptr;  // a pointer into the module itself, not an object
        }
        auto const vtable = *reinterpret_cast<std::uintptr_t const volatile*>(object);
        if (!modules->Holds(vtable)) {
            return nullptr;  // only d3d11's own objects are asked anything
        }
        auto* const unknown = reinterpret_cast<IUnknown*>(object);
        ID3D11Texture2D* texture = nullptr;
        if (FAILED(unknown->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) || texture == nullptr) {
            return nullptr;
        }
        texture->GetDesc(desc);
        if (desc->Width != width || desc->Height != height || desc->SampleDesc.Count != 1 || desc->ArraySize != 1 ||
            (desc->BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
            texture->Release();
            return nullptr;
        }
        return texture;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// The view a capture is read through, and what the shader then does to a sample: float targets are linear; 8-bit ones
// hold gamma-encoded colour (UE3's copy applies the display gamma), read through an sRGB view when the texture is
// typeless, else decoded in the shader (2.2).
bool ViewFormat(DXGI_FORMAT format, DXGI_FORMAT& view, float& decode) {
    decode = 1.0f;
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: view = DXGI_FORMAT_R16G16B16A16_FLOAT; return true;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT: view = DXGI_FORMAT_R32G32B32A32_FLOAT; return true;
    case DXGI_FORMAT_R11G11B10_FLOAT: view = format; return true;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: view = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; return true;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: view = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: view = format; return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM: view = format; decode = 2.2f; return true;
    default: return false;
    }
}

// ---- State --------------------------------------------------------------------------------------------------------------
struct Resolved {
    std::uint64_t generation = 0;
    std::uint64_t resource = 0;
    unsigned tries = 0;
    bool gaveUp = false;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_TEXTURE2D_DESC desc{};
    float decode = 1.0f;
    sc::TexturePath path;
};
Resolved gResolved;
sc::TexturePath gHint;  // the path found last time: the same engine build lays every render target out alike
std::atomic<std::uint64_t> gActiveResource{0};
std::atomic<std::uint64_t> gActiveTexture{0};
std::uint64_t gSeenResets = 0;  // vr::scope_capture::Resets() when gResolved was last valid for it
unsigned gFramesUsed = 0;
unsigned gFramesFromArm = 0;
unsigned gFramesNotReady = 0;
bool gLoggedFirstUse = false;
bool gLoggedArmMiss = false;
std::string gLastReason = "none";

bool Resolve(ID3D11Device* device, sc::Capture const& capture) {
    if (gResolved.generation != capture.generation || gResolved.resource != capture.resource) {
        gResolved = Resolved{};
        gResolved.generation = capture.generation;
        gResolved.resource = capture.resource;
    }
    if (gResolved.srv != nullptr) {
        return true;
    }
    if (gResolved.gaveUp) {
        return false;
    }
    ++gResolved.tries;
    Modules const modules{ImageOf(L"d3d11.dll"), ImageOf(L"d3d11_3SDKLayers.dll"), ImageOf(L"D3D11SDKLayers.dll")};
    if (modules.d3d11.begin == 0) {
        gResolved.gaveUp = true;
        log::Error("scope capture source: d3d11.dll isn't loaded?; the scope keeps the eye copy");
        return false;
    }
    ID3D11Texture2D* found = nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    auto const read = [](std::uint64_t address) -> std::optional<std::uint64_t> {
        std::uint64_t value = 0;
        return ReadRaw(address, &value) ? std::optional<std::uint64_t>(value) : std::nullopt;
    };
    auto const isTexture = [&](std::uint64_t candidate) {
        if (ID3D11Texture2D* const t = ProbeRaw(candidate, &modules, capture.width, capture.height, &desc)) {
            if (found != nullptr) {
                found->Release();
            }
            found = t;
            return true;
        }
        return false;
    };
    sc::TexturePath const path = sc::FindTexture(capture.resource, read, isTexture, gHint);
    if (!path.Valid() || found == nullptr) {
        if (found != nullptr) {
            found->Release();
        }
        if (gResolved.tries >= kResolveTries) {
            gResolved.gaveUp = true;
            log::Warn("scope capture source: no {}x{} D3D11 texture found behind the render target's resource 0x{:X} in {} frames (its "
                      "RHI references within 0x100 bytes, theirs within 0x80); the scope keeps the eye copy [research/39: the offsets "
                      "need Ghidra]",
                capture.width, capture.height, capture.resource, gResolved.tries);
        }
        return false;
    }
    gResolved.texture.Attach(found);
    gResolved.desc = desc;
    gResolved.path = path;
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    if (!ViewFormat(desc.Format, view, gResolved.decode)) {
        gResolved.gaveUp = true;
        gResolved.texture.Reset();
        log::Warn("scope capture source: the capture's texture has format {}, which the scope's shader doesn't read; the scope keeps the "
                  "eye copy", static_cast<int>(desc.Format));
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = view;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    if (device == nullptr || FAILED(device->CreateShaderResourceView(gResolved.texture.Get(), &sd, gResolved.srv.GetAddressOf()))) {
        gResolved.gaveUp = true;
        gResolved.texture.Reset();
        log::Warn("scope capture source: no view of format {} on the capture's texture (format {}); the scope keeps the eye copy",
            static_cast<int>(view), static_cast<int>(desc.Format));
        return false;
    }
    gHint = path;
    gActiveTexture.store(reinterpret_cast<std::uintptr_t>(gResolved.texture.Get()), std::memory_order_relaxed);
    log::Info("scope capture source: the render target's D3D11 texture: resource 0x{:X} +0x{:X} -> {} = 0x{:X}, {}x{} format {} (bind "
              "0x{:X}, {} mips), read as format {}{} (found in {} frames)",
        capture.resource, path.first, path.second >= 0 ? std::format("+0x{:X}", path.second) : std::string("itself"), path.texture,
        desc.Width, desc.Height, static_cast<int>(desc.Format), desc.BindFlags, desc.MipLevels, static_cast<int>(view),
        gResolved.decode != 1.0f ? std::format(", decoded with gamma {:.1f} in the shader", gResolved.decode) : std::string(),
        gResolved.tries);
    return true;
}

} // namespace

std::optional<Source> ForFrame(ID3D11Device* device, int weapon, std::uint64_t pairArm) {
    // The mod went (hot reload, unload): its next render target may repeat the old generation at the old address.
    if (std::uint64_t const resets = sc::Resets(); resets != gSeenResets) {
        gSeenResets = resets;
        Forget();
    }
    sc::Capture const latest = sc::Latest(200);
    if (latest.state != sc::kCapturing || latest.resource == 0 || latest.width == 0 || latest.height == 0) {
        gActiveResource.store(0, std::memory_order_relaxed);
        gLastReason = latest.state != sc::kCapturing ? "the mod isn't capturing" : "no render target yet";
        return std::nullopt;
    }
    gActiveResource.store(latest.resource, std::memory_order_relaxed);
    if (latest.weapon != weapon) {
        gLastReason = "the capture is for another weapon";
        return std::nullopt;
    }
    if (!Resolve(device, latest)) {
        gLastReason = gResolved.gaveUp ? "its texture wasn't found" : "looking for its texture";
        return std::nullopt;
    }
    Source out;
    out.srv = gResolved.srv.Get();
    out.width = gResolved.desc.Width;
    out.height = gResolved.desc.Height;
    out.decodeGamma = gResolved.decode;
    // The pose the texture was rendered from: the one the presented Draw rendered (its pair arm), else the publish before
    // the latest (the mod's tick normally runs between the Draw and its Present: research/39 section 5).
    std::optional<sc::Capture> rendered;
    if (auto const sequence = sc::SequenceForArm(pairArm)) {
        rendered = sc::BySequence(*sequence);
        if (!rendered || rendered->state != sc::kCapturing || rendered->generation != latest.generation) {
            ++gFramesNotReady;
            gLastReason = "the presented Draw rendered before the capture was on";
            return std::nullopt;  // that Draw had no capture (just enabled): nothing in the texture yet
        }
        out.poseFromArm = true;
    } else {
        if (pairArm != 0 && !gLoggedArmMiss) {
            gLoggedArmMiss = true;
            log::Info("scope capture source: the presented pair's arm {} has no capture note (no [MEVR] NoteDrawArm for it): the pose is the "
                      "publish before the latest", pairArm);
        }
        if (latest.enabledTicks < kWarmupTicks) {
            ++gFramesNotReady;
            gLastReason = "just enabled";
            return std::nullopt;
        }
        rendered = sc::BySequence(latest.sequence - 1);
        if (!rendered || rendered->state != sc::kCapturing || rendered->generation != latest.generation) {
            rendered = latest;
        }
    }
    out.pose = rendered->pose;
    out.tanHalf = rendered->tanHalf;
    ++gFramesUsed;
    gFramesFromArm += out.poseFromArm ? 1u : 0u;
    gLastReason = "in use";
    if (!gLoggedFirstUse) {
        gLoggedFirstUse = true;
        log::Info("scope capture source: first scope picture from the capture ({}x{}, field {:.2f} degrees, pose {})", out.width, out.height,
            2.0 * std::atan(out.tanHalf) * 180.0 / 3.14159265, out.poseFromArm ? "from the presented Draw's arm" : "the publish before the latest");
    }
    return out;
}

std::uint64_t ActiveResource() {
    return gActiveResource.load(std::memory_order_relaxed);
}

std::uint64_t ActiveTexture() {
    return gActiveTexture.load(std::memory_order_relaxed);
}

void Forget() {
    gResolved = Resolved{};
    gActiveTexture.store(0, std::memory_order_relaxed);
    gActiveResource.store(0, std::memory_order_relaxed);
}

std::string StatusJson() {
    return std::format("{{\"state\": \"{}\", \"resource\": \"0x{:X}\", \"texture\": \"0x{:X}\", \"size\": [{}, {}], \"format\": {}, "
                       "\"decodeGamma\": {:.2f}, \"path\": [{}, {}], \"tries\": {}, \"framesUsed\": {}, \"framesFromArm\": {}, "
                       "\"framesNotReady\": {}}}",
        gLastReason, gResolved.resource, ActiveTexture(), gResolved.desc.Width, gResolved.desc.Height, static_cast<int>(gResolved.desc.Format),
        gResolved.decode, gResolved.path.first, gResolved.path.second, gResolved.tries, gFramesUsed, gFramesFromArm, gFramesNotReady);
}

} // namespace mevr::host::scope_capture_source
