// SPDX-License-Identifier: MIT
// ME2's part of the sequels' bridge (plugin/HalcyonSequelBridge.cpp has the common seams, plugin/SequelOverlays.cpp the
// layer composition): the adapter for what ME2 alone has (the pull that picks a planet kept from the orbital scanner, its UI
// draws' order for the log), its PlacePanel, the eye-image seams (lane EY's)
// and ME2's own seams (src/halcyon/le2/mevr_bridge.h: the floor view's journal, the swap chain's Present). Separate from
// plugin/HalcyonLe2.cpp: the OpenXR SDK's headers and Halcyon's xr_types.h define the same names.

#include "halcyon/le2/mevr_bridge.h"

#include "core/Breadcrumbs.hpp"
#include "core/Log.hpp"
#include "core/vr/HudPieces.hpp"
#include "engine/GameAdapter.hpp"
#include "game/GamepadMapping.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "plugin/EyeCopy.hpp"
#include "plugin/GamePointer.hpp"
#include "plugin/HalcyonSequelBridge.hpp"
#include "plugin/HdrDisplayPolicy.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/MenusLe2.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Voice.hpp"

#include <openxr/openxr.h>

#include <d3d11.h>
#include <dxgi1_6.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>

namespace {

// ME2's galaxy map is ME1's projector flow (game/GalaxyProjectorFlow, plugin/SequelOverlays) since 2026-11-17 (lane GX2): the
// opt-in flat table that stood here ([Galaxy] nativeTable, NativeTablePanel) is retired (research/le2-galaxy-table.md "Retired").
// The pull that picked a planet must end before it can operate the orbital scanner: under sequel_bridge::PadMutex().
int gGalaxyInputMode = -1;
mevr::game::gamepad::CloseLatch gGalaxyTriggerLatch;

// The order of a present's UI draws, for the log (render thread = the Present thread on ME2): runs of "e" (before the
// renderer captured its left eye) and "l" (after), upper case when the bound target is the swap chain's backbuffer.
struct UiTrace {
    char text[160] = {};
    int length = 0;
    char last = 0;
    unsigned run = 0;
    void Flush() {
        if (run > 0 && length < static_cast<int>(sizeof text) - 12) {
            length += std::snprintf(text + length, sizeof text - length, "%c%u ", last, run);
        }
        run = 0;
    }
    void Add(char c) {
        if (c != last) {
            Flush();
            last = c;
        }
        ++run;
    }
};
UiTrace gUiTrace;
void* gBackbuffer = nullptr;   // identity only: this present's backbuffer
unsigned gUiTracePresents = 0;

} // namespace

// ---- ME2's adapter (plugin/HalcyonSequelBridge.hpp) ----------------------------------------------------------------------
namespace mevr::host::sequel_bridge::game {

Facts const& Get() noexcept {
    static Facts const facts{
        "halcyon le2 bridge",
        false,  // invalidViewAtOnce: an invalid view waits for the floor lock's quiet like any other
        true,   // panelColour
        static_cast<int>(vr::hud_pieces::Layout::Me2),  // ME2's own HUD: its bands (core/vr/HudPieces)
        // galaxyUiPointer: none, as ME3's. ME2's map keeps its own controls (the stick moves the Normandy, A selects, X exits, Y
        // buys probes, LB/RB the tabs): a ray click on the map's Exit prompt through the game screens' GUI events
        // (SFXGameModeGalaxy.GuiKey) changed nothing (run galaxy-final-201028, research/le2-galaxy-table.md "The projector flow").
        0,
    };
    return facts;
}

void SessionCreated(void* /*instance*/, void* /*session*/) noexcept {
    std::lock_guard lock(PadMutex());
    gGalaxyInputMode = -1;
    gGalaxyTriggerLatch = {};
}

void SessionStopped() noexcept {}
void FrameBegun(void* /*session*/, void* /*space*/, bool /*focused*/, bool /*tracked*/) noexcept {}
void FramePublished() noexcept {}

void Recentered() noexcept {}

bool PadKeeps(int /*gameMode*/) noexcept {
    return false;
}

bool PadOrbitalTriggers(int gameMode) noexcept {
    return engine::adapter::IsOrbitalMode(gameMode);  // ME2's mode 10: the scanner's LT and RT
}

void PadMerged(int gameMode, bool /*controllersActive*/, XINPUT_GAMEPAD& pad) noexcept {
    if (engine::adapter::IsGalaxyMode(gGalaxyInputMode) && engine::adapter::IsOrbitalMode(gameMode)) {
        // The pull selecting a planet must end before it can operate the orbital scanner.
        gGalaxyTriggerLatch.leftTrigger = gGalaxyTriggerLatch.rightTrigger = true;
    }
    gGalaxyInputMode = gameMode;
    mevr::game::gamepad::ApplyCloseLatch(gGalaxyTriggerLatch, pad);
}

void PadFiltered(int /*gameMode*/, bool /*ownMenuOpen*/, XINPUT_GAMEPAD& /*pad*/) noexcept {}

// The order of a present's UI draws, for the log (OnSwapChainPresent prints it).
void UiDraw(ID3D11DeviceContext* context, bool afterLeftEye) noexcept {
    if (gUiTracePresents < 12 && context != nullptr) {
        ID3D11RenderTargetView* rtv = nullptr;
        context->OMGetRenderTargets(1, &rtv, nullptr);
        bool backbuffer = false;
        if (rtv != nullptr) {
            ID3D11Resource* res = nullptr;
            rtv->GetResource(&res);
            backbuffer = res != nullptr && res == gBackbuffer;
            if (res != nullptr) res->Release();
            rtv->Release();
        }
        char c = afterLeftEye ? (backbuffer ? 'L' : 'l') : (backbuffer ? 'E' : 'e');
        if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
            c = afterLeftEye ? 'D' : 'd';  // a deferred context's
        }
        gUiTrace.Add(c);
    }
}

void OverlayBegin(MevrSeams::OverlayFrame const& /*frame*/, bool /*usable*/) noexcept {}

bool OverlayLabel(OverlayContext const& context) noexcept {
    return !context.worldUiOwns;  // never while a screen of ours has the panel
}

int OverlayReticleMode(OverlayContext const& context) noexcept {
    return context.lastGameMode;  // ME2's numbers are the hand reticle's: 0 on foot
}

// Nothing of ME2's own: the galaxy map's stereo window on the CIC's projector, its UI on the panel there and the pointer's
// surface are the shared composition's (plugin/SequelOverlays).
unsigned OverlayGameLayers(OverlayContext const& /*context*/, hud_panel::Frame const& /*panel*/, void const** /*layersOut*/,
    unsigned count, unsigned /*capacity*/) noexcept {
    return count;
}

bool OverlaySpokenBlocked(OverlayContext const& /*context*/) noexcept {
    return false;
}
void OverlaySpokenContext(bool /*eligible*/) noexcept {}
void OverlayEnd(OverlayContext const& /*context*/) noexcept {}

} // namespace mevr::host::sequel_bridge::game

// ---- ME2's own seams (src/halcyon/le2/mevr_bridge.h) ---------------------------------------------------------------------
namespace ME2VR::MevrBridge {
using namespace MevrSeams;
void JournalFloorViewWrite(void const* origin, void const* preTranslation) noexcept {
    mevr::host::crash::RecordWrite("FloorLock.viewOrigin", origin, 12, mevr::breadcrumbs::FirstBytes(origin, 12));
    mevr::host::crash::RecordWrite("FloorLock.preViewTranslation", preTranslation, 12,
        mevr::breadcrumbs::FirstBytes(preTranslation, 12));
}
} // namespace ME2VR::MevrBridge

// ---- The eye-image seams (lane EY's: bodies as they were) ----------------------------------------------------------------
namespace MevrSeams {

namespace {
D3D11_TEXTURE2D_DESC gSourceDesc[2]{};  // Present thread: the last eye source's, for the copy's log line
D3D11_TEXTURE2D_DESC gLoggedSource{};
D3D11_TEXTURE2D_DESC gLoggedImage{};
int gCopyDescLogs = 0;

bool SameDesc(D3D11_TEXTURE2D_DESC const& a, D3D11_TEXTURE2D_DESC const& b) {
    return a.Width == b.Width && a.Height == b.Height && a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count &&
        a.MiscFlags == b.MiscFlags && a.BindFlags == b.BindFlags && a.ArraySize == b.ArraySize && a.MipLevels == b.MipLevels;
}
} // namespace

void OnEyeSource(int eye, void* source) noexcept {
    if ((eye == 0 || eye == 1) && source != nullptr) {
        static_cast<ID3D11Texture2D*>(source)->GetDesc(&gSourceDesc[eye]);
    }
    mevr::host::testloop::OnEyeSource(eye, static_cast<ID3D11Texture2D*>(source));
}

bool CopyIntoImage(void* context, void* image, void* source, int shiftX, int eye) noexcept {
    // `eye`: 0 or 1 (CopyTextureToEyeFullFrame), else the flat panel (-1): the colour correction's dither pattern per eye
    // (plugin/Colour).
    return mevr::host::eye_copy::Copy(static_cast<ID3D11DeviceContext*>(context), static_cast<ID3D11Texture2D*>(image),
        static_cast<ID3D11Texture2D*>(source), shiftX, eye);
}

void OnEyeCopied(int eye, void* image, std::uint32_t index, int acquired, int waited) noexcept {
    if (eye != 0 && eye != 1) {
        return;
    }
    // Present thread only: per eye, the copies of the last minute and what refused them.
    struct Counts {
        unsigned copies = 0, acquireFailed = 0, waitFailed = 0, noImage = 0;
        unsigned indices = 0;  // bit per image index seen
        int lastAcquire = 0, lastWait = 0;
    };
    static Counts counts[2];
    static ULONGLONG nextLogMs = 0;
    static bool toldFailure = false;
    Counts& c = counts[eye];
    if (acquired != 0) {
        ++c.acquireFailed;
        c.lastAcquire = acquired;
    } else if (waited != 0) {
        ++c.waitFailed;
        c.lastWait = waited;
    } else if (image == nullptr) {
        ++c.noImage;
    } else {
        ++c.copies;
        c.indices |= index < 32 ? (1u << index) : 0u;
        D3D11_TEXTURE2D_DESC desc{};
        static_cast<ID3D11Texture2D*>(image)->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC const& s = gSourceDesc[eye];
        if (gCopyDescLogs < 8 && (!SameDesc(s, gLoggedSource) || !SameDesc(desc, gLoggedImage))) {
            ++gCopyDescLogs;
            gLoggedSource = s;
            gLoggedImage = desc;
            mevr::log::Info("halcyon le2 bridge: eye copy from {}x{} format {} samples {} mips {} bind {:#x} misc {:#x} into "
                            "{}x{} format {} samples {} mips {} bind {:#x} misc {:#x}",
                s.Width, s.Height, static_cast<int>(s.Format), s.SampleDesc.Count, s.MipLevels, s.BindFlags, s.MiscFlags,
                desc.Width, desc.Height, static_cast<int>(desc.Format), desc.SampleDesc.Count, desc.MipLevels, desc.BindFlags,
                desc.MiscFlags);
        }
        mevr::host::testloop::OnEyeCopied(eye, static_cast<ID3D11Texture2D*>(image));
    }
    if ((acquired != 0 || waited != 0 || (image == nullptr && acquired == 0 && waited == 0)) && !toldFailure) {
        toldFailure = true;
        mevr::log::Warn("halcyon le2 bridge: an eye copy was refused ({} eye: acquire {}, wait {}, image index {}{})",
            eye == 0 ? "left" : "right", acquired, waited, index, image == nullptr ? ", no image" : "");
    }
    ULONGLONG const now = GetTickCount64();
    if (now >= nextLogMs) {
        bool const first = nextLogMs == 0;
        nextLogMs = now + 60000;
        if (!first) {
            mevr::log::Info("halcyon le2 bridge: eye copy commands before release in 60 s: left {} (indices {:#x}; acquire failed {} last {}, wait "
                            "failed {} last {}, no image {}), right {} (indices {:#x}; acquire failed {} last {}, wait failed {} "
                            "last {}, no image {})",
                counts[0].copies, counts[0].indices, counts[0].acquireFailed, counts[0].lastAcquire, counts[0].waitFailed,
                counts[0].lastWait, counts[0].noImage, counts[1].copies, counts[1].indices, counts[1].acquireFailed,
                counts[1].lastAcquire, counts[1].waitFailed, counts[1].lastWait, counts[1].noImage);
        }
        counts[0] = {};
        counts[1] = {};
    }
}

void OnEyeReleased(int eye, bool published) noexcept {
    mevr::host::testloop::OnEyeReleased(eye, published);
}

void OnFactory(void* factory) noexcept {
    mevr::host::hdr_display::OnFactory(factory);
}

} // namespace MevrSeams
namespace ME2VR::MevrBridge {
using namespace MevrSeams;
void OnSwapChainPresent(void* swapChain, void* capturedSwapChain) noexcept {
    // The last present's UI draws in order (HudPanelDraw), six presents with UI once the HUD panel is armed.
    if (swapChain != nullptr && swapChain == capturedSwapChain) {
        static unsigned presentsWithUi = 0;
        if (gUiTrace.run > 0 || gUiTrace.length > 0) {
            gUiTrace.Flush();
            ++presentsWithUi;
            if (mevr::host::hud_panel::Armed() && gUiTracePresents < 12 && (presentsWithUi <= 3 || presentsWithUi % 600 == 0)) {
                ++gUiTracePresents;
                mevr::log::Info("halcyon le2 bridge: this present's UI draws in order (e before the left eye's capture, l after; "
                                "upper case into the backbuffer): {}", gUiTrace.text);
            }
        }
        gUiTrace = UiTrace{};
        ID3D11Texture2D* bb = nullptr;
        if (SUCCEEDED(static_cast<IDXGISwapChain*>(swapChain)->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb))) &&
            bb != nullptr) {
            ID3D11Resource* res = nullptr;
            bb->QueryInterface(__uuidof(ID3D11Resource), reinterpret_cast<void**>(&res));
            gBackbuffer = res;  // identity only
            if (res != nullptr) res->Release();
            bb->Release();
        }
        mevr::host::testloop::OnPresent();
        mevr::host::voice::OnPresent();
        mevr::host::menus_le2::OnPresent();  // our menu closes itself when nobody can see it (M4)
    }
    // Present thread only. The captured swap chain's buffers: each change of size or format logged (ME2's is R8G8B8A8 at
    // the title and R16G16B16A16_FLOAT in gameplay with Windows HDR on: plugin/EyeCopy). Checked every 90th Present.
    static unsigned presents = 0;
    static DXGI_MODE_DESC last{};
    if (swapChain != nullptr && swapChain == capturedSwapChain && presents++ % 90 == 0) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(static_cast<IDXGISwapChain*>(swapChain)->GetDesc(&desc)) &&
            (desc.BufferDesc.Format != last.Format || desc.BufferDesc.Width != last.Width ||
                desc.BufferDesc.Height != last.Height)) {
            last = desc.BufferDesc;
            mevr::log::Info("halcyon le2 bridge: the game's swap chain is {}x{} format {} ({} buffers, swap effect {})",
                desc.BufferDesc.Width, desc.BufferDesc.Height, static_cast<int>(desc.BufferDesc.Format), desc.BufferCount,
                static_cast<int>(desc.SwapEffect));
        }
    }
    // The first few swap chains other than the captured one are named once each.
    static void* seen[8] = {};
    static int count = 0;
    if (swapChain == nullptr || swapChain == capturedSwapChain) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (seen[i] == swapChain) {
            return;
        }
    }
    if (count >= 8) {
        return;
    }
    seen[count++] = swapChain;
    DXGI_SWAP_CHAIN_DESC desc{};
    bool const described = SUCCEEDED(static_cast<IDXGISwapChain*>(swapChain)->GetDesc(&desc));
    mevr::log::Warn("halcyon le2 bridge: a Present on swap chain {} that isn't the one the renderer copies its eyes from "
                    "({}): {}x{} format {} buffers {} swap effect {} window {:#x}{}",
        swapChain, capturedSwapChain, described ? desc.BufferDesc.Width : 0, described ? desc.BufferDesc.Height : 0,
        described ? static_cast<int>(desc.BufferDesc.Format) : 0, described ? desc.BufferCount : 0,
        described ? static_cast<int>(desc.SwapEffect) : -1,
        described ? reinterpret_cast<std::uintptr_t>(desc.OutputWindow) : 0, described ? "" : " (no description)");
}
} // namespace ME2VR::MevrBridge
namespace MevrSeams {

// ME2's PlacePanel: every flat panel keeps the renderer's own placement (`placed` false; capability panelsInRoom is ME3's).
// The galaxy map isn't a flat panel while the mod holds it over the CIC's projector (MevrSeams::GalaxyOwnsPresentation in
// d3d_capture.cpp's GetMenuMode); otherwise (no room, tracking or view) it is the renderer's flat panel, as on ME1.
PanelPose PlacePanel(PanelKind /*kind*/, OverlayFrame const& /*frame*/, float const /*viewPosition*/[3], float /*widthM*/,
    float /*heightM*/, bool /*imageReady*/) noexcept {
    return PanelPose{};
}

} // namespace MevrSeams
