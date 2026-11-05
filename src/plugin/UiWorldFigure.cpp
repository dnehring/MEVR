// SPDX-License-Identifier: MIT
// The squad member behind the equipment, XMods and squad screens: the host's side (plugin/UiWorldFigure.hpp, research/57 section 7).
#include "plugin/UiWorldFigure.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "core/vr/WorldUi.hpp"
#include "plugin/ScopeCaptureSource.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstring>
#include <format>
#include <string>

namespace mevr::host::ui_world_figure {

namespace {

using Microsoft::WRL::ComPtr;
namespace wu = vr::world_ui;
namespace sc = vr::scope_capture;

constexpr std::uint64_t kFreshMs = 500;           // the mod's state this fresh
constexpr std::uint64_t kRenderedWithinMs = 500;  // our capture rendered the UI world this recently: its picture is shown
constexpr std::uint64_t kProbeTargetOffset = 0x1C;  // FSceneCaptureProbe's UTextureRenderTarget* (packed; read at 0x4EE4B0 + 0x63)
// FSceneCaptureProbe's count of Umbra camera queries kicked for it (0x4FD550 adds one, 0x4EE4B0 consumes one through Umbra). While
// it's 0 the probe renders without Umbra whatever the eye's family says (0x4EE4B0's second branch), which the UI world's scene needs.
constexpr std::uint64_t kProbeUmbraPendingOffset = 0x6C;
// The per-capture Umbra camera thunk BeginRenderingViewFamily calls on the game thread (`mov rcx, [rcx]`, the probe's slot 8).
constexpr std::uintptr_t kCaptureUmbraCameraRva = 0x7526C0;
constexpr std::uint64_t kSceneSearchFrom = 0x40;  // where a UWorld's FScene* is looked for: past UObject's header
constexpr std::uint64_t kSceneSearchTo = 0x1000;
constexpr int kResolveTries = 120;

bool ReadU64(std::uint64_t address, std::uint64_t& out) noexcept {
    if (address < 0x10000) {
        return false;
    }
    __try {
        std::memcpy(&out, reinterpret_cast<void const*>(address), sizeof out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadI32(std::uint64_t address, std::int32_t& out) noexcept {
    if (address < 0x10000) {
        return false;
    }
    __try {
        std::memcpy(&out, reinterpret_cast<void const*>(address), sizeof out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The render target our capture's probe holds (0: none or unreadable).
std::uint64_t ProbeTarget(void* captureInfo, std::uint64_t* probeOut = nullptr) {
    std::uint64_t probe = 0;
    std::uint64_t target = 0;
    if (!ReadU64(reinterpret_cast<std::uint64_t>(captureInfo), probe) || !ReadU64(probe + kProbeTargetOffset, target)) {
        return 0;
    }
    if (probeOut != nullptr) {
        *probeOut = probe;
    }
    return target;
}

// The render target of ours, from the mod's latest state (0: no capture of ours).
std::uint64_t OurTarget() {
    std::optional<wu::State> const state = wu::Latest(kFreshMs);
    return state ? state->figureTarget : 0;
}

using tCaptureUmbraCamera = void(void* captureInfo, void* umbra, void* family);
tCaptureUmbraCamera* gCaptureUmbraCameraOrig = nullptr;
std::atomic<std::uint64_t> gUmbraSkips{0};
std::atomic<bool> gLoggedUmbraSkip{false};

void CaptureUmbraCameraHook(void* captureInfo, void* umbra, void* family) {
    std::uint64_t const ours = OurTarget();
    if (ours != 0 && ProbeTarget(captureInfo) == ours) {
        gUmbraSkips.fetch_add(1, std::memory_order_relaxed);
        if (!gLoggedUmbraSkip.exchange(true)) {
            log::Info("ui world figure: our capture gets no Umbra camera query (it renders the UI world, which Umbra doesn't know)");
        }
        return;
    }
    gCaptureUmbraCameraOrig(captureInfo, umbra, family);
}

bool WriteU64(std::uint64_t address, std::uint64_t value) noexcept {
    if (address < 0x10000) {
        return false;
    }
    __try {
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool gInstalled = false;  // the Umbra camera thunk is hooked (attach)
std::atomic<std::uint64_t> gPendingRenders{0};  // our capture had an Umbra query pending: rendered as the level once
// Render thread.
std::int64_t gSceneOffset = -1;  // UWorld::Scene (+0x70, as FViewportClient::Draw reads GWorld's): found as the eye renderer's scene in the level's UWorld
std::int64_t gLoggedOffset = -1;
std::string gLoggedWhy;
bool gLoggedFirstSwap = false;
// Any thread.
std::atomic<std::uint64_t> gLastSwapMs{0};
std::atomic<std::uint64_t> gSeen{0};   // our capture came through the capture loop
std::atomic<std::uint64_t> gSwaps{0};  // ... and rendered the UI world's scene
std::atomic<std::uint64_t> gSkips{0};  // ... and didn't render (no UI world scene)
std::atomic<std::uint64_t> gUiScene{0};
std::atomic<std::uint64_t> gLevelScene{0};
std::atomic<std::int64_t> gOffsetShown{-1};
// Present thread.
struct Texture {
    std::uint64_t resource = 0;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    float decode = 1.0f;
    int tries = 0;
    bool gaveUp = false;
};
Texture gTexture;
sc::TexturePath gHint;
std::atomic<std::uint64_t> gShown{0};
std::atomic<bool> gTextureFound{false};  // for the status (any thread)

// The UI world's scene: the level's UWorld holds the eye renderer's scene at UWorld::Scene's offset, the UI world's UWorld its own
// at the same place; it must be another FScene (the same vtable). 0 and `why` if not.
std::uint64_t FindUiScene(std::uint64_t levelWorld, std::uint64_t uiWorld, std::uint64_t mainScene, std::string& why) {
    std::uint64_t value = 0;
    if (gSceneOffset < 0 || !ReadU64(levelWorld + static_cast<std::uint64_t>(gSceneOffset), value) || value != mainScene) {
        gSceneOffset = -1;
        for (std::uint64_t k = kSceneSearchFrom; k < kSceneSearchTo; k += 8) {
            if (ReadU64(levelWorld + k, value) && value == mainScene) {
                gSceneOffset = static_cast<std::int64_t>(k);
                break;
            }
        }
        if (gSceneOffset < 0) {
            why = std::format("the eye render's scene {:#x} isn't in the level's UWorld {:#x} (+0x{:X}..0x{:X})", mainScene, levelWorld,
                kSceneSearchFrom, kSceneSearchTo);
            return 0;
        }
        gOffsetShown.store(gSceneOffset, std::memory_order_relaxed);
        if (gSceneOffset != gLoggedOffset) {
            gLoggedOffset = gSceneOffset;
            log::Info("ui world figure: a UWorld's scene is at +0x{:X} (the eye render's scene {:#x} in the level's UWorld {:#x})",
                gSceneOffset, mainScene, levelWorld);
        }
    }
    std::uint64_t ui = 0;
    std::uint64_t uiVtable = 0;
    std::uint64_t mainVtable = 0;
    if (!ReadU64(uiWorld + static_cast<std::uint64_t>(gSceneOffset), ui) || ui == 0) {
        why = std::format("the UI world's UWorld {:#x} has no scene at +0x{:X}", uiWorld, gSceneOffset);
        return 0;
    }
    if (ui == mainScene) {
        why = "the UI world's scene is the level's";
        return 0;
    }
    if (!ReadU64(ui, uiVtable) || !ReadU64(mainScene, mainVtable) || uiVtable != mainVtable) {
        why = std::format("the UI world's +0x{:X} ({:#x}) isn't an FScene like the level's (vtable {:#x} against {:#x})", gSceneOffset, ui,
            uiVtable, mainVtable);
        return 0;
    }
    return ui;
}

void LogWhy(std::string const& why) {
    if (why != gLoggedWhy) {
        gLoggedWhy = why;
        log::Warn("ui world figure: our capture doesn't render (it would show the level from the UI world's camera): {}", why);
    }
}

} // namespace

void Install(hook::Context& init) {
    auto* const thunk = init.ResolveTyped<tCaptureUmbraCamera>(kCaptureUmbraCameraRva);
    gInstalled = thunk != nullptr &&
        hook::Install(init, "scene capture's Umbra camera", thunk, &CaptureUmbraCameraHook, gCaptureUmbraCameraOrig);
    if (!gInstalled) {
        log::Warn("ui world figure: the capture's Umbra camera thunk isn't hooked: no squad member behind the equipment and squad screens");
    }
}

CaptureSwap BeginCapture(void* captureInfo, void* mainRenderer) {
    CaptureSwap swap;
    std::optional<wu::State> const state = wu::Latest(kFreshMs);
    if (!state || state->figureTarget == 0) {
        return swap;
    }
    std::uint64_t probe = 0;
    if (ProbeTarget(captureInfo, &probe) != state->figureTarget) {
        return swap;  // another capture (the scope's, a reflection)
    }
    gSeen.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t mainScene = 0;
    std::int32_t pending = 0;
    std::string why;
    std::uint64_t ui = 0;
    if (!gInstalled) {
        why = "the capture's Umbra camera thunk isn't hooked";
    } else if (!ReadI32(probe + kProbeUmbraPendingOffset, pending) || pending != 0) {
        // An Umbra query kicked for it before the thunk skipped it (the screen's first frame): this render consumes it through Umbra,
        // as the level, and the next one renders the UI world.
        gPendingRenders.fetch_add(1, std::memory_order_relaxed);
        return swap;
    } else if (!ReadU64(reinterpret_cast<std::uint64_t>(mainRenderer), mainScene) || mainScene == 0) {
        why = "the eye renderer has no scene";
    } else if (state->levelWorld == 0 || state->uiWorld == 0) {
        why = "the mod sent no worlds";
    } else {
        ui = FindUiScene(state->levelWorld, state->uiWorld, mainScene, why);
    }
    std::uint64_t const renderer = reinterpret_cast<std::uint64_t>(mainRenderer);
    if (ui == 0 || !WriteU64(renderer, ui)) {
        swap.skip = true;
        gSkips.fetch_add(1, std::memory_order_relaxed);
        LogWhy(why.empty() ? std::string("the eye renderer's scene couldn't be swapped") : why);
        return swap;
    }
    swap.original = reinterpret_cast<void*>(mainScene);
    gSwaps.fetch_add(1, std::memory_order_relaxed);
    gLastSwapMs.store(GetTickCount64(), std::memory_order_relaxed);
    gUiScene.store(ui, std::memory_order_relaxed);
    gLevelScene.store(mainScene, std::memory_order_relaxed);
    gLoggedWhy.clear();
    if (!gLoggedFirstSwap) {
        gLoggedFirstSwap = true;
        log::Info("ui world figure: our capture renders the UI world's scene {:#x} instead of the level's {:#x} (its render target {:#x}), "
                  "without Umbra (no camera query was kicked for it)", ui, mainScene, state->figureTarget);
    }
    return swap;
}

void EndCapture(void* mainRenderer, CaptureSwap const& swap) {
    if (swap.original != nullptr) {
        WriteU64(reinterpret_cast<std::uint64_t>(mainRenderer), reinterpret_cast<std::uint64_t>(swap.original));
    }
}

std::optional<Picture> ForFrame(ID3D11Device* device, float backdrop) {
    std::optional<wu::State> const state = wu::Latest(kFreshMs);
    if (device == nullptr || !state || state->figure == 0 || state->figureResource == 0 || state->figureWidth == 0 ||
        state->figureHeight == 0) {
        return std::nullopt;
    }
    if (GetTickCount64() - gLastSwapMs.load(std::memory_order_relaxed) > kRenderedWithinMs) {
        return std::nullopt;  // not rendered with the UI world's scene lately: its texture holds something else (or nothing)
    }
    if (gTexture.resource != state->figureResource) {
        gTexture = Texture{};
        gTexture.resource = state->figureResource;
        gTextureFound.store(false, std::memory_order_relaxed);
    }
    if (gTexture.srv == nullptr) {
        if (gTexture.gaveUp) {
            return std::nullopt;
        }
        ++gTexture.tries;
        // The target's texture sits behind an unaligned pointer, which the search finds by the class of a texture of our own on the
        // game's device (research/39 section 11): that class must be known first.
        scope_capture_source::NoteDevice(device);
        D3D11_TEXTURE2D_DESC desc{};
        ID3D11Texture2D* const found =
            scope_capture_source::FindTargetTexture(state->figureResource, state->figureWidth, state->figureHeight, gHint, &desc);
        if (found == nullptr) {
            if (gTexture.tries >= kResolveTries) {
                gTexture.gaveUp = true;
                log::Warn("ui world figure: no {}x{} D3D11 texture found behind the render target's resource {:#x} in {} frames (last "
                          "search: {}; the known class's textures there:{}): no squad member behind the screen", state->figureWidth,
                    state->figureHeight, state->figureResource, gTexture.tries, scope_capture_source::LastSearch(),
                    scope_capture_source::DescribeTextures(state->figureResource));
            }
            return std::nullopt;
        }
        gTexture.texture.Attach(found);
        DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
        if (!scope_capture_source::CaptureViewFormat(desc.Format, view, gTexture.decode)) {
            gTexture.gaveUp = true;
            gTexture.texture.Reset();
            log::Warn("ui world figure: the capture's texture has format {}, which isn't read", static_cast<int>(desc.Format));
            return std::nullopt;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = view;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(gTexture.texture.Get(), &sd, gTexture.srv.GetAddressOf()))) {
            gTexture.gaveUp = true;
            gTexture.texture.Reset();
            log::Warn("ui world figure: no view of format {} on the capture's texture (format {})", static_cast<int>(view),
                static_cast<int>(desc.Format));
            return std::nullopt;
        }
        gTextureFound.store(true, std::memory_order_relaxed);
        log::Info("ui world figure: the capture's D3D11 texture: {}x{} format {}, read as format {}{} (found in {} frames)", desc.Width,
            desc.Height, static_cast<int>(desc.Format), static_cast<int>(view),
            gTexture.decode != 1.0f ? std::format(", decoded with gamma {:.1f}", gTexture.decode) : std::string(), gTexture.tries);
    }
    gShown.fetch_add(1, std::memory_order_relaxed);
    Picture picture;
    picture.srv = gTexture.srv.Get();
    picture.decodeGamma = gTexture.decode;
    picture.opacity = state->figureOpacity > 0.0f ? state->figureOpacity : backdrop;
    return picture;
}

void Forget() {
    gTexture = Texture{};
    gTextureFound.store(false, std::memory_order_relaxed);
}

std::string StatusJson() {
    std::uint64_t const last = gLastSwapMs.load(std::memory_order_relaxed);
    return std::format("{{\"hooked\": {}, \"umbraSkips\": {}, \"pendingRenders\": {}, \"seen\": {}, \"swaps\": {}, \"skips\": {}, "
                       "\"sceneOffset\": {}, \"uiScene\": \"{:#x}\", \"levelScene\": \"{:#x}\", \"lastSwapAgoMs\": {}, \"shown\": {}, \"texture\": {}}}",
        gInstalled ? "true" : "false", gUmbraSkips.load(std::memory_order_relaxed), gPendingRenders.load(std::memory_order_relaxed),
        gSeen.load(std::memory_order_relaxed), gSwaps.load(std::memory_order_relaxed), gSkips.load(std::memory_order_relaxed),
        gOffsetShown.load(std::memory_order_relaxed), gUiScene.load(std::memory_order_relaxed), gLevelScene.load(std::memory_order_relaxed),
        last != 0 ? static_cast<long long>(GetTickCount64() - last) : -1LL, gShown.load(std::memory_order_relaxed),
        gTextureFound.load(std::memory_order_relaxed) ? "true" : "false");
}

} // namespace mevr::host::ui_world_figure
