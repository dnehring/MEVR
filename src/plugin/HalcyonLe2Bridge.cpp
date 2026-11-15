// SPDX-License-Identifier: MIT
// Our side of the seams in Halcyon's ME2 renderer (src/halcyon/le2/mevr_bridge.h): the OpenXR loader, the session's
// functions for the test loop, our controller actions, their vibration and the floor on Halcyon's session (plugin/XrHost,
// shared with LE1's bridge), the view each frame for the mod (core/vr/HostView), and the motion controllers as Xbox pad 0.
// As LE1's (plugin/HalcyonBridge.cpp). The game-thread queue and the runtime test are in plugin/HalcyonLe2.cpp. Separate
// from that file: the OpenXR SDK's headers and Halcyon's xr_types.h define the same names.

#include "halcyon/le2/mevr_bridge.h"

#include "core/Breadcrumbs.hpp"
#include "core/DevChannels.hpp"
#include "core/Log.hpp"
#include "core/Ini.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/CineScreen.hpp"
#include "core/vr/Colour.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "game/GamepadMapping.hpp"
#include "engine/GameAdapter.hpp"
#include "plugin/CinemaBackdrop.hpp"
#include "plugin/CineScreen.hpp"
#include "plugin/SequelCinema.hpp"
#include "plugin/SequelWeaponLayers.hpp"
#include "plugin/SequelSquadLayers.hpp"
#include "plugin/Colour.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "plugin/EyeCopy.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/GamePointer.hpp"
#include "plugin/HandReticle.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/HudPieceLayers.hpp"
#include "plugin/MenusLe2.hpp"
#include "plugin/PadMerge.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/Voice.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/SightLayers.hpp"
#include "plugin/SquadPanel.hpp"
#include "plugin/TransitionFade.hpp"
#include "plugin/TraversalCue.hpp"
#include "plugin/Vignette.hpp"
#include "plugin/PowerWheel.hpp"
#include "core/vr/PowerWheel.hpp"
#include "core/vr/SquadMenu.hpp"
#include "plugin/SquadPlaceDraw.hpp"
#include "plugin/WorldPanels.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/Thespian.hpp"
#endif
#include "plugin/XrHost.hpp"

#include <openxr/openxr.h>

#include <d3d11.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>

namespace ME2VR::MevrBridge {
namespace {

bool gToldFirstSync = false;  // Present thread
bool gLoggedView = false;     // Present thread
mevr::vr::galaxy_map::NativeTablePanel gGalaxyTable;
void* gGalaxySpace = nullptr;
std::optional<mevr::vr::galaxy_map::Screen> gGalaxyPanel;
bool gGalaxyPointerShown = false;
bool gGalaxyTableEnabled = false;
mevr::vr::galaxy_holo::TableSettings gGalaxySettings;
std::uint64_t gGalaxySettingsRead = 0;
// The floor lock's last pass (FloorViewTranslation, game thread): the view is published there while it runs.
std::atomic<std::uint64_t> gFloorTranslatedMs{0};
constexpr std::uint64_t kFloorTranslateQuietMs = 100;

std::mutex gPadMutex;  // the packet numbering below (XInput is polled from any thread; the latches are plugin/PadMerge's)
// ME2's conversation game mode (SFXGameModeManager.CurrentMode, me2_xr.cpp's [VRCINE] 5 = Conversation): its triggers are the
// interrupts' (BIOInput.ini's conversation bindings: LT InterruptParagon, RT InterruptRenegade; research/le2-interrupt-acquisition.md).
constexpr int kConversationMode = 5;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;
int gGalaxyInputMode = -1; // guarded by gPadMutex, like gLastPad
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

// ME1's 3D cinema screen (research/le2-cinema.md): this frame's screen as CineScreenLayers placed it, for OverlayLayers' UI
// layer in front of it (Present thread; cleared once OverlayLayers used it), and the headset's eye distance for the plan.
using ScreenNow = mevr::host::sequel_cinema::State;
ScreenNow gScreen;

} // namespace

void* StaticXrGetInstanceProcAddr() noexcept {
    return reinterpret_cast<void*>(&xrGetInstanceProcAddr);
}

void WrapSessionFunctions(void** acquireSwapchainImage, void** waitSwapchainImage, void** releaseSwapchainImage,
    void** waitFrame, void** beginFrame, void** endFrame, void** locateViews) noexcept {
    (void)waitSwapchainImage;  // the frame-breakdown probe's (plugin/PerfProbe): LE1's host only so far
    (void)waitFrame;
    (void)beginFrame;
    mevr::host::testloop::WrapSessionFunctions(acquireSwapchainImage, releaseSwapchainImage, endFrame, locateViews);
    mevr::log::Info("test loop: wrapped Halcyon's ME2 xrAcquireSwapchainImage, xrReleaseSwapchainImage, xrEndFrame and "
                    "xrLocateViews (snapshots, recording, replay)");
}

void OnSessionCreated(void* instance, void* session) noexcept {
    mevr::host::sequel_squad_layers::ForgetSession();
    gGalaxyTable.Reset();
    gGalaxyPanel.reset();
    gGalaxySpace = nullptr;
    if (gGalaxyPointerShown) mevr::host::game_pointer::ForgetGameUiQuad();
    gGalaxyPointerShown = false;
    {
        std::lock_guard lock(gPadMutex);
        gGalaxyInputMode = -1;
        gGalaxyTriggerLatch = {};
    }
    mevr::host::testloop::OnSessionCreated(static_cast<XrSession>(session));
    mevr::host::swapchains::OnSessionCreated(static_cast<XrSession>(session));  // ours went with the old session
    mevr::host::hud_piece_layers::ForgetSession();
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::thespian::ForgetSession();
#endif
    mevr::host::eye_copy::Reset();
    if (mevr::host::xr_host::OnSessionCreated(static_cast<XrInstance>(instance), static_cast<XrSession>(session))) {
        mevr::log::Info("halcyon le2 bridge: our controller actions are attached to Halcyon's session");
    }
    // The colour correction's per-headset settings and automatic profile (plugin/Colour, research/27), as LE1's bridge.
    {
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId system = XR_NULL_SYSTEM_ID;
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
        XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(xrGetSystem(static_cast<XrInstance>(instance), &systemInfo, &system)) &&
            XR_SUCCEEDED(xrGetSystemProperties(static_cast<XrInstance>(instance), system, &properties)) &&
            XR_SUCCEEDED(xrGetInstanceProperties(static_cast<XrInstance>(instance), &runtime))) {
            mevr::host::colour::SetHeadset(runtime.runtimeName, properties.systemName, properties.vendorId);
        }
    }
    mevr::host::menus_le2::OnSessionCreated(static_cast<XrInstance>(instance), static_cast<XrSession>(session));  // M4
    gToldFirstSync = false;
}

void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept {
    if (session == nullptr) {
        return;
    }
    mevr::host::world_panels::NoteFrame();  // every frame: a panel (the 3D cinema screen) knows whether it was shown before
    mevr::vr::controllers::State state;
    mevr::host::xr_host::OnFrameBegun(static_cast<XrSession>(session), static_cast<XrSpace>(space),
        static_cast<XrTime>(predictedDisplayTime), state);
    bool const inputReady = mevr::host::xr_host::InputReady();
    mevr::vr::controllers::State const live = state;  // the player's own, before a replay's takes its place
    // The test loop records the live sample, or replaces it with a recording's (plugin/TestLoop), as on LE1.
    mevr::host::testloop::OnFrameBegun(static_cast<XrSession>(session), static_cast<XrSpace>(space),
        static_cast<XrTime>(predictedDisplayTime), mevr::host::xr_host::InputFocused(), state);
    if (!inputReady && !state.active) {
        return;  // no controllers, and no replay standing in for them
    }
    // Our main menu and the guided calibration take the controllers while they're up (plugin/MenusLe2, M4).
    mevr::vr::controllers::Publish(mevr::host::menus_le2::OnFrameBegun(static_cast<XrSession>(session),
        static_cast<XrSpace>(space), static_cast<XrTime>(predictedDisplayTime), live, state,
        mevr::host::xr_host::InputFocused()));
    if (inputReady && !gToldFirstSync) {
        gToldFirstSync = true;
        mevr::log::Info("halcyon le2 bridge: first controller sync (session {}focused)",
            mevr::host::xr_host::InputFocused() ? "" : "not ");
    }
}

void OnViewBuilt(bool worldView, bool headLook, bool headAim, int lookYawUU, int lookPitchUU, float const headQ[4],
    float const headP[3], float offsetRightUU, float offsetUpUU, float offsetForwardUU, float unitsPerMeter,
    bool floorWorld) noexcept {
    mevr::vr::host_view::State view;
    view.valid = worldView;
    view.headLook = headLook;
    view.headAim = headAim;
    view.head.orientation = {headQ[0], headQ[1], headQ[2], headQ[3]};
    view.head.position = {headP[0], headP[1], headP[2]};
    // As LE1's: Halcyon's head look yaw is + when the view turns left (its DriveHeadLook takes yawSign -1 of the head's
    // left-positive yaw); ours (UE rotators) + right. Pitch is + up in both. [verify] on ME2 with a replayed turn.
    view.lookYaw = -lookYawUU;
    view.lookPitch = lookPitchUU;
    view.offsetForward = offsetForwardUU;
    view.offsetRight = offsetRightUU;
    view.offsetUp = offsetUpUU;
    view.unitsPerMeter = unitsPerMeter;
    // The floor lock (plugin/FloorLock, as LE1's): the view goes out from the game thread's ApplyHeadPosition with the
    // pair it rendered (FloorViewTranslation). Where that didn't run lately (ME2's lean off, a VR cine's tracking lock) the
    // view goes out from here, uncorrected, so the mod keeps its view.
    mevr::vr::floor::State floor;
    mevr::host::xr_host::Floor(&floor);
    mevr::host::floor_lock::Queue(view, floor, floorWorld);
    std::uint64_t const now = GetTickCount64();
    if (now - gFloorTranslatedMs.load(std::memory_order_relaxed) > kFloorTranslateQuietMs) {
        mevr::vr::host_view::Publish(view, now);
    }
    if (worldView && !gLoggedView) {
        gLoggedView = true;
        mevr::log::Info("halcyon le2 bridge: first world view: head look yaw {} pitch {} (head aim {}), view offset "
                        "fwd {:.1f} right {:.1f} up {:.1f}, {:.1f} units per meter, head at ({:.2f}, {:.2f}, {:.2f}) m",
            view.lookYaw, view.lookPitch, headAim ? "on" : "off", offsetForwardUU, offsetRightUU, offsetUpUU, unitsPerMeter,
            headP[0], headP[1], headP[2]);
    }
}

float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ, float* right, float* up,
    float* forward) noexcept {
    gFloorTranslatedMs.store(GetTickCount64(), std::memory_order_relaxed);
    return mevr::host::floor_lock::Translate(replay, rawCameraZ, rightZ, upZ, forwardZ, right, up, forward);
}

void JournalFloorViewWrite(void const* origin, void const* preTranslation) noexcept {
    mevr::host::crash::RecordWrite("FloorLock.viewOrigin", origin, 12, mevr::breadcrumbs::FirstBytes(origin, 12));
    mevr::host::crash::RecordWrite("FloorLock.preViewTranslation", preTranslation, 12,
        mevr::breadcrumbs::FirstBytes(preTranslation, 12));
}

void OnRecentered() noexcept {
    gGalaxyTable.Reset();
    gGalaxyPanel.reset();
    mevr::host::xr_host::NoteRecentered();
    mevr::host::sequel_squad_layers::Recentered();
    mevr::host::hud_panel::Recentered();  // the HUD panel straight ahead again
    mevr::host::world_panels::Recenter();  // the 3D cinema screen placed afresh
    mevr::host::menus_le2::OnRecentered();  // our menu and the calibration placed again (M4)
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::thespian::Recentered();
#endif
    mevr::log::Info("halcyon le2 bridge: recentered");
}

void OnSessionStopped() noexcept {
    mevr::host::sequel_squad_layers::ForgetSession();
    mevr::host::floor_lock::Reset();  // the queued and published views (host_view::Reset)
    mevr::host::hud_panel::SetArmed(false);  // no OverlayLayers runs now to disarm it: the UI back to the frame
    mevr::host::eye_copy::Reset();
    mevr::log::Info("halcyon le2 bridge: the session stopped");
}

namespace {
D3D11_TEXTURE2D_DESC gSourceDesc[2]{};  // Present thread: the last eye source's, for the copy's log line
void* gPendingSource = nullptr;          // Present thread: OnEyeSource's, for the copy right after it (its eye)
int gPendingEye = -1;
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
    gPendingSource = source;
    gPendingEye = eye;
    mevr::host::testloop::OnEyeSource(eye, static_cast<ID3D11Texture2D*>(source));
}

bool CopyIntoImage(void* context, void* image, void* source, int shiftX) noexcept {
    // The eye whose source OnEyeSource just named (CopyTextureToEyeFullFrame), else the flat panel (-1): the colour
    // correction's dither pattern per eye (plugin/Colour).
    int const eye = source != nullptr && source == gPendingSource ? gPendingEye : -1;
    gPendingSource = nullptr;
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

// ---- Our layers (LE1's HalcyonOverlays::OverlayLayers) ------------------------------------------------------------------
// LE2 composes the shared comfort vignette, the HUD panel, the weapon sights and the hand reticle over Halcyon's projection,
// each the same module LE1's host composes (no copies), in LE1's order.
unsigned OverlayLayers(const OverlayFrame& frame, const void** layersOut, unsigned capacity) noexcept {
    // Release the old table before another screen publishes its pointer surface this frame.
    if (gGalaxyPointerShown && (!gGalaxyPanel || frame.gameMode != 9)) {
        mevr::host::game_pointer::ForgetGameUiQuad();
        gGalaxyPointerShown = false;
    }
    if (layersOut == nullptr || capacity == 0 || frame.session == nullptr || frame.context == nullptr) {
        gScreen = ScreenNow{};
        return 0;
    }
    unsigned count = 0;
    ID3D11Device* device = nullptr;
    frame.context->GetDevice(&device);
    static int lastGameMode = -1;  // Present thread: the last readable game mode
    if (frame.gameMode >= 0) {
        lastGameMode = frame.gameMode;
    }
    // The comfort vignette (research/31): head-locked, under all of ours.
    {
        mevr::host::vignette::Frame vignette;
        vignette.session = static_cast<XrSession>(frame.session);
        vignette.context = frame.context;
        vignette.device = device;
        vignette.worldFrame = frame.worldFrame;
        vignette.menuOpen = frame.menuOpen;
        vignette.fovValid = frame.projectionValid;
        std::memcpy(vignette.fov, frame.projectionFov, sizeof vignette.fov);
        if (XrCompositionLayerQuad const* layer = mevr::host::vignette::Layer(vignette)) {
            layersOut[count++] = layer;
        }
    }
    // The body-locked HUD panel (plugin/HudPanel, LE1's way): the game's UI, which d3d_capture.cpp's draw hooks sent there
    // instead of the eyes (MevrBridge::HudPanelDraw), on a quad in front of the body. Also clears the panel and arms the
    // next frame's redirect.
    {
        mevr::host::renderer::HudSettings const cfg = mevr::host::renderer::Hud();
        // The game's UI gets the eyes' colour correction (plugin/Colour, research/27, research/le2-picture.md), as LE1's
        // HalcyonOverlays hands it to the panel's blit: set before the panel's first blit.
        static bool const colourHooked = [] {
            mevr::host::hud_panel::SetColour({mevr::vr::colour::ShaderPrelude(), &mevr::host::colour::BeginPanel,
                &mevr::host::colour::EndPanel});
            return true;
        }();
        (void)colourHooked;
        mevr::host::hud_panel::Frame panel;
        panel.session = static_cast<XrSession>(frame.session);
        panel.appSpace = static_cast<XrSpace>(frame.appSpace);
        panel.context = frame.context;
        panel.device = device;
        panel.worldFrame = frame.worldFrame && !frame.menuOpen;
        panel.gameplay = frame.gameMode >= 0 && frame.gameMode <= 4;  // as LE1's: not conversations (5), cutscenes, menus
        // ...but the game's screens of ours (plugin/MenusLe2, M4.3): their UI on the panel, shown at the screen's place.
        mevr::host::menus_le2::WorldUiPanel const worldUi = mevr::host::menus_le2::BeforePanel(frame, mevr::host::hud_panel::Width(),
            mevr::host::hud_panel::Height());
        panel.gameplay = panel.gameplay || worldUi.owns;
        panel.backdrop = worldUi.backdrop;
        panel.stereo = frame.stereo;
        panel.headValid = frame.headValid;
        panel.head.orientation = {frame.headOrientation[0], frame.headOrientation[1], frame.headOrientation[2],
            frame.headOrientation[3]};
        panel.head.position = {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]};
        panel.enabled = cfg.hudPanel;
        panel.distanceM = cfg.hudPanelDistanceM;
        panel.dropM = cfg.hudPanelDropM;
        panel.followDeg = cfg.hudPanelFollowDeg;
        panel.widthM = cfg.hudPanelWidthM;
        // ME1's 3D cinema screen is up (CineScreenLayers, this frame): the game's UI (the dialogue wheel, subtitles, the
        // interrupt prompt) on a layer of its own just in front of it ([Cinematics] uiLayer, uiForwardM; research/52
        // "Cross-eyed"), the part of the window the screen shows, as large to the eye as on the screen.
        mevr::host::sequel_cinema::FillUi(gScreen, frame.headPosition, panel);
        // The HUD keeps the movie's own layout on the panel (Halcyon's per-element HUD moves stand down), as LE1's.
        mevr::host::renderer::SetAuthoredHudLayout(panel.enabled && panel.stereo && mevr::host::hud_panel::Usable());
        // The HUD pieces on the body (plugin/HudPieceLayers, LE1's, research/41): on foot, the squad's block on the left
        // wrist and the weapon panel on the gun where the mod says (game/HudPieces), cut out of the panel meanwhile.
        namespace hpl = mevr::host::hud_piece_layers;
        hpl::Frame pieceFrame;
        pieceFrame.gameplayOnFoot = lastGameMode == 0;
        pieceFrame.worldFrame = panel.worldFrame;
        pieceFrame.headValid = frame.headValid;
        pieceFrame.panelReady = panel.enabled && panel.stereo && mevr::host::hud_panel::Ready() && device != nullptr;
        pieceFrame.panelWidth = mevr::host::hud_panel::Width();
        pieceFrame.panelHeight = mevr::host::hud_panel::Height();
        pieceFrame.layout = mevr::vr::hud_pieces::Layout::Me2;  // ME2's own HUD: its bands (core/vr/HudPieces)
        // The pieces are blitted from this frame's UI after the panel is drawn and before it's cleared (hud_panel's
        // beforeClear), and go on top of the panel's quad, as LE1's.
        struct PiecesPass {
            hpl::Common common;
            hpl::Plan plan;
            void const* layers[2] = {};
            unsigned count = 0;
        } pass;
        pass.common.session = static_cast<XrSession>(frame.session);
        pass.common.appSpace = static_cast<XrSpace>(frame.appSpace);
        pass.common.context = frame.context;
        pass.common.device = device;
        pass.common.head = {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]};
        pass.common.nowMs = GetTickCount64();
        pass.common.options.colourSeed = 3;  // a quad of ours, not the panel (LE1's overlays' options for a piece)
        pass.plan = hpl::Decide(pieceFrame, pass.common.nowMs, panel.masks);
        // Reserve room for panel, wrist, gun and label before removing the native prompt's band. A failed copy leaves
        // that band on the original panel. Never relocate dialogue, screen UI or a wheel's captured HUD.
        bool const interactionWanted = pieceFrame.panelReady && pieceFrame.worldFrame && frame.gameMode == 0 &&
            frame.headValid && !worldUi.owns && !frame.menuOpen && !mevr::host::renderer::OwnMenuOpen() &&
            !mevr::host::menus_le2::PadWanted() &&
            capacity - count >= 4 && !mevr::vr::power_wheel::Latest().open && !mevr::vr::squad_menu::Latest().open;
        XrCompositionLayerQuad const* const interactionLayer = hpl::Interaction(pass.common, interactionWanted, panel.masks);
        panel.user = &pass;
        panel.beforeClear = [](void* user, bool shown) {
            auto& p = *static_cast<PiecesPass*>(user);
            p.count = hpl::Layers(p.common, p.plan, shown, p.layers, 0, 2);
        };
        XrCompositionLayerQuad const* const panelLayer = mevr::host::hud_panel::Layer(panel);
        count = mevr::host::menus_le2::PanelLayers(frame, device, panelLayer, layersOut, count, capacity);  // or a screen's place
        if (interactionLayer != nullptr && panelLayer != nullptr && count < capacity) layersOut[count++] = interactionLayer;
        for (unsigned i = 0; i < pass.count && count < capacity; ++i) {
            layersOut[count++] = pass.layers[i];
        }
    }
    count = mevr::host::sequel_weapon_layers::Layers(frame, device, lastGameMode, layersOut, count, capacity);
    // Native hiding remains LE2's verified movie adapter, separate from the shared hand-reticle drawing.
    mevr::host::renderer::SetScreenReticleHidden(mevr::host::renderer::Hud().handReticle && lastGameMode >= 0 && lastGameMode <= 4);
    // Traversal's cue at the obstacle and its blink (plugin/TraversalCue, research/le2-traversal.md): the blink over the world's
    // layers, under the menus.
    if (count < capacity) {
        mevr::host::traversal_cue::Frame cue;
        cue.session = static_cast<XrSession>(frame.session);
        cue.appSpace = static_cast<XrSpace>(frame.appSpace);
        cue.context = frame.context;
        cue.device = device;
        cue.worldFrame = frame.worldFrame;
        cue.menuOpen = frame.menuOpen;
        cue.headValid = frame.headValid;
        cue.head = {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]};
        count += mevr::host::traversal_cue::Layers(cue, layersOut + count, capacity - count);
    }
    if (count < capacity && frame.gameMode == 0) {
        mevr::host::power_wheel::Frame wheel;
        wheel.session = static_cast<XrSession>(frame.session);
        wheel.appSpace = static_cast<XrSpace>(frame.appSpace);
        wheel.context = frame.context;
        wheel.device = device;
        wheel.worldFrame = frame.worldFrame;
        wheel.menuOpen = frame.menuOpen || mevr::host::renderer::OwnMenuOpen();
        wheel.head = {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]};
        wheel.headValid = frame.headValid;
        XrCompositionLayerQuad const* quads[3]{};
        int const n = mevr::host::power_wheel::Layers(wheel, quads, static_cast<int>(std::min(3u, capacity - count)));
        for (int i = 0; i < n; ++i) layersOut[count++] = quads[i];
    }
    OverlayFrame squadFrame = frame;
    squadFrame.worldFrame = frame.worldFrame && frame.gameMode == 0;
    squadFrame.menuOpen = frame.menuOpen || mevr::host::renderer::OwnMenuOpen() || mevr::host::menus_le2::PadWanted();
    count = mevr::host::sequel_squad_layers::Place(squadFrame, device, layersOut, count, capacity);
    // Use the submitted image for picking. Orbit keeps its native scan and probe controls.
    if (gGalaxyPanel && frame.gameMode == 9 && !frame.menuOpen && !mevr::host::renderer::OwnMenuOpen() &&
        !mevr::host::menus_le2::PadWanted()) {
        mevr::host::game_pointer::NoteGameUiQuad(gGalaxyPanel->pose, gGalaxyPanel->width, gGalaxyPanel->height);
        gGalaxyPointerShown = true;
    } else if (gGalaxyPointerShown) {
        mevr::host::game_pointer::ForgetGameUiQuad();
        gGalaxyPointerShown = false;
    }
    count = mevr::host::sequel_squad_layers::Panel(squadFrame, device, layersOut, count, capacity);
    // Our main menu and the guided calibration (plugin/MenusLe2, M4): over the rest, alone while they're up.
#if defined(MEVR_WITH_MAIN_MENU)
    {
        mevr::host::thespian::Frame spoken;
        spoken.session = static_cast<XrSession>(frame.session);
        spoken.appSpace = static_cast<XrSpace>(frame.appSpace);
        spoken.context = frame.context;
        spoken.device = device;
        spoken.head = {{frame.headOrientation[0], frame.headOrientation[1], frame.headOrientation[2], frame.headOrientation[3]},
            {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]}};
        spoken.othersOwnInput = frame.gameMode != kConversationMode || !frame.headValid || count >= capacity || frame.menuOpen || mevr::host::renderer::OwnMenuOpen() || mevr::host::menus_le2::PadWanted();
        if (auto const* layer = mevr::host::thespian::Layer(spoken)) layersOut[count++] = layer;
    }
#endif
    count = mevr::host::menus_le2::Layers(frame, device, layersOut, count, capacity);
    if (device != nullptr) {
        device->Release();
    }
    gScreen = ScreenNow{};  // this frame's 3D screen used; the next submit places it again
    return count;
}

// ---- ME1's 3D cinema screen (plugin/CineScreen, CinemaBackdrop, WorldPanels; research/52, research/le2-cinema.md) ----------
bool CineScreenWanted(bool conversation) noexcept {
    return mevr::host::cine_screen::Wanted(conversation);
}

bool PlaceGalaxyPanel(OverlayFrame const& frame, bool imageReady, float aspect, GalaxyPanel& out) noexcept {
    std::uint64_t const now = GetTickCount64();
    if (now - gGalaxySettingsRead >= 1000) {
        gGalaxySettingsRead = now;
        auto const file = mevr::ini::Read(mevr::host::SettingsPath()).file;
        // Opt in until the Normandy/orbit presentation and native input are checked in the game.
        gGalaxyTableEnabled = mevr::ini::GetNumber(file, "Galaxy", "nativeTable").value_or(0) != 0;
        auto number = [&](char const* key, float fallback, float lo, float hi) {
            return std::clamp(static_cast<float>(mevr::ini::GetNumber(file, "Galaxy", key).value_or(fallback)), lo, hi);
        };
        gGalaxySettings.distanceM = number("tableDistanceM", 0.70f, 0.3f, 3.0f);
        gGalaxySettings.belowM = number("tableBelowM", 0.45f, 0.1f, 1.5f);
        gGalaxySettings.radiusM = number("tableRadiusM", 0.48f, 0.2f, 1.5f);
        gGalaxySettings.tiltDeg = number("worldTiltDeg", 35.0f, 0.0f, 80.0f);
    }
    if (frame.appSpace != gGalaxySpace) {
        gGalaxySpace = frame.appSpace;
        gGalaxyTable.Reset();
    }
    XrPosef const head{{frame.headOrientation[0], frame.headOrientation[1], frame.headOrientation[2], frame.headOrientation[3]},
        {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]}};
    gGalaxyPanel = gGalaxyTable.Update(mevr::engine::adapter::IsMapMode(frame.gameMode), gGalaxyTableEnabled && frame.appSpace != nullptr,
        imageReady && frame.headValid, head, aspect, gGalaxySettings);
    if (!gGalaxyPanel) return false;
    auto const& p = gGalaxyPanel->pose;
    out = {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
        {p.position.x, p.position.y, p.position.z}, gGalaxyPanel->width, gGalaxyPanel->height};
    return true;
}

bool CineScreenFullRate() noexcept {
    return mevr::host::cine_screen::FullRate();
}

float CineScreenHalfSeparationUU(float halfH) noexcept {
    return mevr::host::sequel_cinema::HalfSeparationUU(halfH);
}
unsigned CineScreenLayers(const CineScreenFrame& frame, const void** layersOut, unsigned capacity) noexcept {
    return mevr::host::sequel_cinema::Layers(frame, layersOut, capacity, gScreen);
}

const void* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept {
    return mevr::host::transition_fade::Layer(session, viewSpace, context, view);
}

bool HudPanelDraw(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool textBatch, bool afterLeftEye) noexcept {
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
    return mevr::host::hud_panel::Redirect(context, draw, user, textBatch, afterLeftEye);
}

// ---- The display's HDR answer (IDXGIOutput6::GetDesc1) ---------------------------------------------------------------
// As LE1's [Colour] hdrDisplay (plugin/HalcyonBridge's HdrDisplayDesc, research/27 7c): 1 answers HDR10 for an SDR display,
// 0 (LE1's default) passes the display's own answer through, -1 (ME2's default) answers SDR for an HDR display. With
// Windows HDR on, ME2 makes an R16G16B16A16_FLOAT swap chain in gameplay even with [HDR] DesiredRange = SDR (LE1 stays
// 8-bit), and Halcyon's ME2 renderer is built for an 8-bit one (its SFR left eye and its UI treatment find nothing in a
// float frame; plugin/EyeCopy keeps its eyes from going black). Answered SDR (-1), ME2 renders the 8-bit SDR frame ME1
// does (research/le2-renderer-integration.md "The black eyes"). -1 is an interim default (the owner, 2026-11-14: "a stop
// gap"): when ME2's renderer handles its native HDR frame (the HDR study, research/le2-hdr.md), it goes back to 0.
constexpr int kHdrDisplayDefault = -1;  // the ME2 fact that forces it: the float swap chain, above
namespace {
using GetDesc1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput6*, DXGI_OUTPUT_DESC1*);
GetDesc1Fn gRealGetDesc1 = nullptr;
std::atomic<int> gHdrAnswerLogs{0};

HRESULT STDMETHODCALLTYPE GetDesc1Hook(IDXGIOutput6* self, DXGI_OUTPUT_DESC1* desc) {
    HRESULT const hr = gRealGetDesc1 != nullptr ? gRealGetDesc1(self, desc) : E_FAIL;
    if (FAILED(hr) || desc == nullptr) {
        return hr;
    }
    std::wstring const& path = mevr::host::SettingsPath();
    int const want = path.empty() ? kHdrDisplayDefault
                                  : static_cast<int>(GetPrivateProfileIntW(L"Colour", L"hdrDisplay", kHdrDisplayDefault,
                                        path.c_str()));
    int const was = static_cast<int>(desc->ColorSpace);
    bool const hdr = desc->ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    char const* answered = "its own";
    if (want > 0 && !hdr) {
        desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        desc->BitsPerColor = 10;
        answered = "HDR10";
    } else if (want < 0 && hdr) {
        desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        desc->BitsPerColor = 8;
        answered = "SDR";
    }
    if (gHdrAnswerLogs.fetch_add(1, std::memory_order_relaxed) < 3) {
        mevr::log::Info("hdr display: the display reports colour space {} ({}), {} bits, {:.0f} nits; answered {} "
                        "([Colour] hdrDisplay = {})",
            was, hdr ? "HDR10" : "SDR", desc->BitsPerColor, desc->MaxLuminance, answered, want);
    }
    return hr;
}
} // namespace

void OnFactory(void* factory) noexcept {
    static bool hooked = false;  // under Halcyon's hook mutex
    if (hooked || factory == nullptr) {
        return;
    }
    IDXGIFactory1* f1 = nullptr;
    if (FAILED(static_cast<IUnknown*>(factory)->QueryInterface(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&f1))) ||
        f1 == nullptr) {
        return;
    }
    IDXGIAdapter1* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    IDXGIOutput6* output6 = nullptr;
    if (SUCCEEDED(f1->EnumAdapters1(0, &adapter)) && adapter != nullptr && SUCCEEDED(adapter->EnumOutputs(0, &output)) &&
        output != nullptr &&
        SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&output6))) && output6 != nullptr) {
        void** const slot = *reinterpret_cast<void***>(output6) + 27;  // IDXGIOutput6::GetDesc1
        DWORD old = 0;
        if (*slot != reinterpret_cast<void*>(&GetDesc1Hook) &&
            VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
            gRealGetDesc1 = reinterpret_cast<GetDesc1Fn>(*slot);
            *slot = reinterpret_cast<void*>(&GetDesc1Hook);
            VirtualProtect(slot, sizeof(void*), old, &old);
            hooked = true;
            mevr::log::Info("hdr display: IDXGIOutput6::GetDesc1 hooked (the game's HDR question; [Colour] hdrDisplay)");
        }
    }
    if (output6 != nullptr) {
        output6->Release();
    }
    if (output != nullptr) {
        output->Release();
    }
    if (adapter != nullptr) {
        adapter->Release();
    }
    f1->Release();
}


DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept {
    if (index != 0 || state == nullptr) {
        return result;
    }
    mevr::vr::controllers::State const controllers = mevr::vr::controllers::Latest();
    bool const menus = mevr::host::menus_le2::PadWanted();  // our menu or the calibration has the pad (M4)
    if (!controllers.active) {
        mevr::host::pad_merge::NoControllers();
    }
    if (!controllers.active && !menus) {
        return result;
    }
    bool const realPad = result == ERROR_SUCCESS;
    bool const conversationTriggers = controllers.active && mevr::host::renderer::GameMode() == kConversationMode;
    std::lock_guard lock(gPadMutex);
    XINPUT_GAMEPAD pad = realPad ? state->Gamepad : XINPUT_GAMEPAD{};
    if (controllers.active) {
        // The motion controllers with the mod's flags, merged over a real pad (plugin/PadMerge, LE1's mapping): the support
        // grip, the holsters' and the squad's grips, comfort's turn and walk. Both stick clicks are the chord (our menu,
        // MenusLe2's OnFrameBegun), kept from the game; R3 waits for L3 as L3 for R3 (the owner's L3 + R3 priority).
        mevr::host::pad_merge::Renderer renderer;
        renderer.ownMenuOpen = mevr::host::renderer::OwnMenuOpen();  // Halcyon's ME2 menu
        renderer.headLookYawUU = &mevr::host::renderer::MoveStickHeadLookYawUU;  // its "run where you look", after the merge
        renderer.gateRightClick = true;
        // ME2's interrupts (M5.2, research/le2-cinema.md): in the conversation mode (game mode 5) the triggers are the game's
        // LT (Paragon) and RT (Renegade), as on a pad; nothing of ours keeps them (pad_merge::ConversationTriggers).
        renderer.conversationTriggers = conversationTriggers;
        renderer.orbitalTriggers = mevr::host::renderer::GameMode() == 10;
        pad = mevr::host::pad_merge::Merge(pad, controllers, renderer, GetTickCount64());
    }
    // Neutral while our menu or the calibration has it; after, what was held then is kept from the game until let go.
    int const inputMode = mevr::host::renderer::GameMode();
    if (gGalaxyInputMode == 9 && inputMode == 10) {
        // The pull selecting a planet must end before it can operate the orbital scanner.
        gGalaxyTriggerLatch.leftTrigger = gGalaxyTriggerLatch.rightTrigger = true;
    }
    gGalaxyInputMode = inputMode;
    mevr::game::gamepad::ApplyCloseLatch(gGalaxyTriggerLatch, pad);
    mevr::host::menus_le2::FilterPad(realPad ? &state->Gamepad : nullptr, pad);
    if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) {
        if (conversationTriggers && !mevr::host::renderer::OwnMenuOpen()) {
            auto const down = [](BYTE value) { return value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD; };
            if ((down(pad.bLeftTrigger) != down(gLastPad.bLeftTrigger) ||
                    down(pad.bRightTrigger) != down(gLastPad.bRightTrigger)) &&
                mevr::dev_channels::Allowed(std::filesystem::path(mevr::host::SettingsPath()))) {
                mevr::log::Info("conversation pad 0: LT {}, RT {}", pad.bLeftTrigger, pad.bRightTrigger);
            }
        }
        gLastPad = pad;
        ++gPacket;  // the game only re-reads a pad whose packet number changed
    }
    state->dwPacketNumber = gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
}

} // namespace ME2VR::MevrBridge
