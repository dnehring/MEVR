// SPDX-License-Identifier: MIT
// Our side of the seams in Halcyon's ME2 renderer (src/halcyon/le2/mevr_bridge.h): the OpenXR loader, the session's
// functions for the test loop, our controller actions, their vibration and the floor on Halcyon's session (plugin/XrHost,
// shared with LE1's bridge), the view each frame for the mod (core/vr/HostView), and the motion controllers as Xbox pad 0.
// As LE1's (plugin/HalcyonBridge.cpp). The game-thread queue and the runtime test are in plugin/HalcyonLe2.cpp. Separate
// from that file: the OpenXR SDK's headers and Halcyon's xr_types.h define the same names.

#include "halcyon/le2/mevr_bridge.h"

#include "core/Log.hpp"
#include "core/vr/Colour.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "game/GamepadMapping.hpp"
#include "plugin/Colour.hpp"
#include "plugin/EyeCopy.hpp"
#include "plugin/HandReticle.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/SightLayers.hpp"
#include "plugin/Vignette.hpp"
#include "plugin/XrHost.hpp"

#include <openxr/openxr.h>

#include <d3d11.h>
#include <dxgi1_6.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace ME2VR::MevrBridge {
namespace {

bool gToldFirstSync = false;  // Present thread
bool gLoggedView = false;     // Present thread

std::mutex gPadMutex;  // the latches and packet numbering below (XInput is polled from any thread)
mevr::game::gamepad::Latches gLatches;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;

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
    mevr::host::testloop::OnSessionCreated(static_cast<XrSession>(session));
    mevr::host::swapchains::OnSessionCreated(static_cast<XrSession>(session));  // ours went with the old session
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
    gToldFirstSync = false;
}

void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept {
    if (session == nullptr) {
        return;
    }
    mevr::vr::controllers::State state;
    mevr::host::xr_host::OnFrameBegun(static_cast<XrSession>(session), static_cast<XrSpace>(space),
        static_cast<XrTime>(predictedDisplayTime), state);
    bool const inputReady = mevr::host::xr_host::InputReady();
    // The test loop records the live sample, or replaces it with a recording's (plugin/TestLoop), as on LE1.
    mevr::host::testloop::OnFrameBegun(static_cast<XrSession>(session), static_cast<XrSpace>(space),
        static_cast<XrTime>(predictedDisplayTime), mevr::host::xr_host::InputFocused(), state);
    if (!inputReady && !state.active) {
        return;  // no controllers, and no replay standing in for them
    }
    mevr::vr::controllers::Publish(state);
    if (inputReady && !gToldFirstSync) {
        gToldFirstSync = true;
        mevr::log::Info("halcyon le2 bridge: first controller sync (session {}focused)",
            mevr::host::xr_host::InputFocused() ? "" : "not ");
    }
}

void OnViewBuilt(bool worldView, bool headLook, bool headAim, int lookYawUU, int lookPitchUU, float const headQ[4],
    float const headP[3], float offsetRightUU, float offsetUpUU, float offsetForwardUU, float unitsPerMeter,
    bool floorWorld) noexcept {
    (void)floorWorld;  // the floor lock (plugin/FloorLock) isn't in the LE2 host yet: the view goes out uncorrected
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
    mevr::vr::host_view::Publish(view);
    if (worldView && !gLoggedView) {
        gLoggedView = true;
        mevr::log::Info("halcyon le2 bridge: first world view: head look yaw {} pitch {} (head aim {}), view offset "
                        "fwd {:.1f} right {:.1f} up {:.1f}, {:.1f} units per meter, head at ({:.2f}, {:.2f}, {:.2f}) m",
            view.lookYaw, view.lookPitch, headAim ? "on" : "off", offsetForwardUU, offsetRightUU, offsetUpUU, unitsPerMeter,
            headP[0], headP[1], headP[2]);
    }
}

void OnRecentered() noexcept {
    mevr::host::xr_host::NoteRecentered();
    mevr::host::hud_panel::Recentered();  // the HUD panel straight ahead again
    mevr::log::Info("halcyon le2 bridge: recentered");
}

void OnSessionStopped() noexcept {
    mevr::vr::host_view::Reset();
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
            mevr::log::Info("halcyon le2 bridge: eye copies in 60 s: left {} (indices {:#x}; acquire failed {} last {}, wait "
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
    if (layersOut == nullptr || capacity == 0 || frame.session == nullptr || frame.context == nullptr) {
        return 0;
    }
    unsigned count = 0;
    ID3D11Device* device = nullptr;
    frame.context->GetDevice(&device);
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
        // The HUD keeps the movie's own layout on the panel (Halcyon's per-element HUD moves stand down), as LE1's.
        mevr::host::renderer::SetAuthoredHudLayout(panel.enabled && panel.stereo && mevr::host::hud_panel::Usable());
        if (XrCompositionLayerQuad const* layer = mevr::host::hud_panel::Layer(panel); layer != nullptr && count < capacity) {
            layersOut[count++] = layer;
        }
    }
    // The gun's sights (plugin/SightLayers, LE1's): worked out before the ring, which hides while an eye looks through the
    // sight (held 0.3 s, as LE1's HalcyonOverlays); their quads go on top of the ring's.
    mevr::host::sight_layers::Frame sights;
    sights.session = static_cast<XrSession>(frame.session);
    sights.appSpace = static_cast<XrSpace>(frame.appSpace);
    sights.context = frame.context;
    sights.device = device;
    sights.worldFrame = frame.worldFrame;
    sights.menuOpen = frame.menuOpen;
    sights.onFoot = frame.gameMode == 0;
    sights.eyesValid = frame.eyesValid;
    sights.projectionValid = frame.projectionValid;
    for (int e = 0; e < 2; ++e) {
        auto const& eye = frame.eyePosition[e];
        sights.eyes[e].position = {eye[0], eye[1], eye[2]};
        if (frame.projectionValid) {
            auto const& rect = frame.projectionRect[e];
            auto const& orientation = frame.projectionOrientation[e];
            auto const& position = frame.projectionPosition[e];
            auto const& fov = frame.projectionFov[e];
            sights.projectionSwapchain[e] = static_cast<XrSwapchain>(frame.projectionSwapchain[e]);
            sights.projectionRect[e] = {{rect[0], rect[1]}, {rect[2], rect[3]}};
            sights.projectionPose[e] = {{orientation[0], orientation[1], orientation[2], orientation[3]},
                                       {position[0], position[1], position[2]}};
            sights.projectionFov[e] = {fov[0], fov[1], fov[2], fov[3]};
        }
    }
    auto const sightLayers = mevr::host::sight_layers::Layers(sights);
    static std::uint64_t sightSeenMs = 0;  // Present thread
    std::uint64_t const nowMs = GetTickCount64();
    if (sightLayers.looking && sightLayers.replaceRing) {
        sightSeenMs = nowMs;
    }
    // The hand reticle (plugin/HandReticle, LE1's): a ring on the mod's aim ray (game/HandAim's setAimRay) while the mod
    // aims with the right controller, on foot (game mode 0; the last readable one).
    {
        mevr::host::renderer::HudSettings const cfg = mevr::host::renderer::Hud();
        static int lastGameMode = -1;  // Present thread
        if (frame.gameMode >= 0) {
            lastGameMode = frame.gameMode;
        }
        mevr::host::hand_reticle::Frame reticle;
        reticle.session = static_cast<XrSession>(frame.session);
        reticle.appSpace = static_cast<XrSpace>(frame.appSpace);
        reticle.context = frame.context;
        reticle.device = device;
        reticle.worldFrame = frame.worldFrame;
        reticle.menuOpen = frame.menuOpen;
        reticle.headValid = frame.headValid;
        reticle.headPosition = {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]};
        reticle.gameMode = lastGameMode;
        reticle.handAim = mevr::host::renderer::ModHandAim();
        reticle.sightHidesRing = sightSeenMs != 0 && nowMs - sightSeenMs < 300;
        reticle.enabled = cfg.handReticle;
        reticle.distanceM = cfg.handReticleDistanceM;
        reticle.sizeDeg = cfg.handReticleSizeDeg;
        if (XrCompositionLayerQuad const* ring = mevr::host::hand_reticle::Layer(reticle); ring != nullptr && count < capacity) {
            layersOut[count++] = ring;
        }
    }
    for (unsigned i = 0; i < sightLayers.count && count < capacity; ++i) {
        layersOut[count++] = sightLayers.layers[i];
    }
    if (device != nullptr) {
        device->Release();
    }
    return count;
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
    if (!controllers.active) {
        return result;
    }
    bool const realPad = result == ERROR_SUCCESS;
    std::lock_guard lock(gPadMutex);
    XINPUT_GAMEPAD const pad = mevr::game::gamepad::Merge(realPad ? state->Gamepad : XINPUT_GAMEPAD{},
        mevr::game::gamepad::FromControllers(controllers, gLatches));
    if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) {
        gLastPad = pad;
        ++gPacket;  // the game only re-reads a pad whose packet number changed
    }
    state->dwPacketNumber = gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
}

} // namespace ME2VR::MevrBridge
