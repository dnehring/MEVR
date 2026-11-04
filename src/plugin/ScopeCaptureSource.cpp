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
// The vtable of a texture the host made on the game's device (NoteDevice): a candidate with exactly it is a texture of that
// device's class, whatever module its vtable lives in (the capture's texture was missed by the module check, research/39
// section 11 and research/51 section 8), and is asked nothing it can't answer.
std::atomic<std::uintptr_t> gKnownTextureVtable{0};

ID3D11Texture2D* ProbeRaw(std::uint64_t candidate, Modules const* modules, UINT width, UINT height, D3D11_TEXTURE2D_DESC* desc) noexcept {
    __try {
        auto const object = static_cast<std::uintptr_t>(candidate);
        if (modules->Holds(object)) {
            return nullptr;  // a pointer into the module itself, not an object
        }
        auto const vtable = *reinterpret_cast<std::uintptr_t const volatile*>(object);
        std::uintptr_t const known = gKnownTextureVtable.load(std::memory_order_relaxed);
        if (vtable != known && !modules->Holds(vtable)) {
            return nullptr;  // only d3d11's own objects (or ones of a known texture's class) are asked anything
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
    if (GetModuleHandleW(L"d3d11.dll") == nullptr) {
        gResolved.gaveUp = true;
        log::Error("scope capture source: d3d11.dll isn't loaded?; the scope keeps the eye copy");
        return false;
    }
    // The capture's texture sits behind an unaligned pointer (+0xA0 -> +0x8, research/39 section 11, research/51 section 8),
    // which the aligned walk skips: FindTargetTexture falls back to a walk for objects of the class of a texture of our own
    // on the game's device, so that class must be known first.
    NoteDevice(device);
    D3D11_TEXTURE2D_DESC desc{};
    sc::TexturePath hint = gHint;
    ID3D11Texture2D* const found = FindTargetTexture(capture.resource, capture.width, capture.height, hint, &desc);
    if (found == nullptr) {
        if (gResolved.tries >= kResolveTries) {
            gResolved.gaveUp = true;
            log::Warn("scope capture source: no {}x{} D3D11 texture found behind the render target's resource 0x{:X} in {} frames (its "
                      "RHI references within 0x100 bytes, theirs within 0x80; last search: {}; the known class's textures there:{}); "
                      "the scope keeps the eye copy",
                capture.width, capture.height, capture.resource, gResolved.tries, LastSearch(), DescribeTextures(capture.resource));
        }
        return false;
    }
    sc::TexturePath const path = hint;
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

namespace {
ID3D11Texture2D* FindKnownTexture(std::uint64_t resource, unsigned width, unsigned height, sc::TexturePath& hint, D3D11_TEXTURE2D_DESC* desc);
std::string gLastSearch;  // what the last FindTargetTexture saw (Present thread)
} // namespace

std::string LastSearch() {
    return gLastSearch;
}

ID3D11Texture2D* FindTargetTexture(std::uint64_t resource, unsigned width, unsigned height, sc::TexturePath& hint, D3D11_TEXTURE2D_DESC* desc) {
    Modules const modules{ImageOf(L"d3d11.dll"), ImageOf(L"d3d11_3SDKLayers.dll"), ImageOf(L"D3D11SDKLayers.dll")};
    if (modules.d3d11.begin == 0 || resource == 0 || desc == nullptr) {
        return nullptr;
    }
    ID3D11Texture2D* found = nullptr;
    auto const read = [](std::uint64_t address) -> std::optional<std::uint64_t> {
        std::uint64_t value = 0;
        return ReadRaw(address, &value) ? std::optional<std::uint64_t>(value) : std::nullopt;
    };
    auto const isTexture = [&](std::uint64_t candidate) {
        if (ID3D11Texture2D* const t = ProbeRaw(candidate, &modules, width, height, desc)) {
            if (found != nullptr) {
                found->Release();
            }
            found = t;
            return true;
        }
        return false;
    };
    sc::TexturePath const path = sc::FindTexture(resource, read, isTexture, hint);
    gLastSearch = std::format("probe path {} found {}", path.Valid() ? std::format("+0x{:X} -> {}", path.first, path.second) : std::string("none"),
        found != nullptr ? "a texture" : "nothing");
    if (!path.Valid() || found == nullptr) {
        if (found != nullptr) {
            found->Release();
        }
        // The QueryInterface probe turned down the capture targets' textures although they fit (research/39 section 11,
        // research/51 section 8: +0xA0 -> +0x8, the right size, one sample, shader-resource binding): the same window walked
        // for objects of the known texture class (NoteDevice), asked nothing but GetDesc and AddRef.
        return FindKnownTexture(resource, width, height, hint, desc);
    }
    hint = path;
    return found;
}

namespace {
// One known texture's description, when `object` has the known texture vtable (only then is it asked anything).
bool KnownTextureDesc(std::uint64_t object, D3D11_TEXTURE2D_DESC* desc) noexcept {
    __try {
        std::uintptr_t const known = gKnownTextureVtable.load(std::memory_order_relaxed);
        if (known == 0 || object < 0x10000) {
            return false;
        }
        if (*reinterpret_cast<std::uintptr_t const volatile*>(static_cast<std::uintptr_t>(object)) != known) {
            return false;
        }
        reinterpret_cast<ID3D11Texture2D*>(static_cast<std::uintptr_t>(object))->GetDesc(desc);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A reference of our own on a texture of the known class (SEH-guarded).
ID3D11Texture2D* AddRefKnown(std::uint64_t object) noexcept {
    __try {
        auto* const texture = reinterpret_cast<ID3D11Texture2D*>(static_cast<std::uintptr_t>(object));
        texture->AddRef();
        return texture;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

ID3D11Texture2D* FindKnownTexture(std::uint64_t resource, unsigned width, unsigned height, sc::TexturePath& hint, D3D11_TEXTURE2D_DESC* desc) {
    if (gKnownTextureVtable.load(std::memory_order_relaxed) == 0 || resource == 0 || desc == nullptr) {
        return nullptr;
    }
    int known = 0;
    auto const fits = [&](std::uint64_t object) {
        D3D11_TEXTURE2D_DESC d{};
        if (!KnownTextureDesc(object, &d)) {
            return false;
        }
        ++known;
        if (d.Width != width || d.Height != height || d.SampleDesc.Count != 1 || d.ArraySize != 1 ||
            (d.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
            gLastSearch += std::format("; known {}x{} s{} a{} b0x{:X} vs {}x{}", d.Width, d.Height, d.SampleDesc.Count, d.ArraySize, d.BindFlags,
                width, height);
            return false;
        }
        *desc = d;
        return true;
    };
    for (std::int32_t first = 0; first < 0x100; first += 8) {
        std::uint64_t a = 0;
        // No alignment asked of the pointers (the walk in sc::FindTexture wants them 8-aligned, and the capture targets' path
        // runs through one that isn't): the object must carry the known texture vtable, and every read is guarded.
        if (!ReadRaw(resource + static_cast<std::uint64_t>(first), &a) || a < 0x10000 || a >= 0x00007FFFFFFFFFFFull) {
            continue;
        }
        if (fits(a)) {
            hint = {first, -1, a};
            return AddRefKnown(a);
        }
        for (std::int32_t second = 0; second < 0x80; second += 8) {
            std::uint64_t b = 0;
            if (ReadRaw(a + static_cast<std::uint64_t>(second), &b) && b >= 0x10000 && b < 0x00007FFFFFFFFFFFull && fits(b)) {
                hint = {first, second, b};
                ID3D11Texture2D* const t = AddRefKnown(b);
                gLastSearch += std::format("; the known class's texture at +0x{:X} -> +0x{:X}{}", first, second, t != nullptr ? "" : " (AddRef failed)");
                return t;
            }
        }
    }
    gLastSearch += std::format("; {} known-class textures, none fits", known);
    return nullptr;
}
} // namespace

std::string DescribeTextures(std::uint64_t resource) {
    std::string out;
    for (std::int32_t first = 0; first < 0x100; first += 8) {
        std::uint64_t a = 0;
        if (!ReadRaw(resource + static_cast<std::uint64_t>(first), &a) || a < 0x10000) {
            continue;
        }
        D3D11_TEXTURE2D_DESC d{};
        if (KnownTextureDesc(a, &d)) {
            out += std::format(" +0x{:X}: {}x{} format {} samples {} array {} mips {} bind 0x{:X};", first, d.Width, d.Height,
                static_cast<int>(d.Format), d.SampleDesc.Count, d.ArraySize, d.MipLevels, d.BindFlags);
        }
        for (std::int32_t second = 0; second < 0x80; second += 8) {
            std::uint64_t b = 0;
            if (!ReadRaw(a + static_cast<std::uint64_t>(second), &b) || b < 0x10000) {
                continue;
            }
            if (KnownTextureDesc(b, &d)) {
                out += std::format(" +0x{:X} -> +0x{:X}: {}x{} format {} samples {} array {} mips {} bind 0x{:X};", first, second, d.Width,
                    d.Height, static_cast<int>(d.Format), d.SampleDesc.Count, d.ArraySize, d.MipLevels, d.BindFlags);
            }
        }
    }
    return out.empty() ? std::string(" none") : out;
}

void NoteDevice(ID3D11Device* device) {
    if (device == nullptr || gKnownTextureVtable.load(std::memory_order_relaxed) != 0) {
        return;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 4;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&td, nullptr, texture.GetAddressOf()))) {
        return;
    }
    std::uintptr_t const vtable = *reinterpret_cast<std::uintptr_t const*>(texture.Get());
    gKnownTextureVtable.store(vtable, std::memory_order_relaxed);
    HMODULE module = nullptr;
    wchar_t name[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(vtable), &module) && module != nullptr) {
        GetModuleFileNameW(module, name, MAX_PATH);
    }
    std::string narrow;
    for (wchar_t const* c = name; *c != 0; ++c) {
        narrow.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    }
    log::Info("scope capture source: a texture of the game's device has its vtable at 0x{:X} ({}): such candidates are textures",
        vtable, narrow);
}

bool CaptureViewFormat(DXGI_FORMAT format, DXGI_FORMAT& view, float& decode) {
    return ViewFormat(format, view, decode);
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
