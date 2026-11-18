// SPDX-License-Identifier: MIT
// Our side of the common seams (src/halcyon/MevrSeams.h) for Halcyon's ME2 and ME3 renderers, written once: the OpenXR
// loader, the session's functions for the test loop and the probe, our controller actions, their vibration and the floor on
// Halcyon's session (plugin/XrHost, shared with LE1's bridge), the view each frame for the mod (core/vr/HostView), the
// motion controllers as Xbox pad 0, the 3D cinema screen, the dip from black and the HUD's draws. As LE1's
// (plugin/HalcyonBridge.cpp), whose behaviour is the reference where the two sequels differed by accident.
//
// What a game alone has is behind its adapter (plugin/HalcyonSequelBridge.hpp's `game`), implemented in
// plugin/HalcyonLe2Bridge.cpp and plugin/HalcyonLe3Bridge.cpp. The layer composition is plugin/SequelOverlays.cpp. The
// seams with one answer for all three games are plugin/Seams.cpp's. Separate from plugin/HalcyonLe2.cpp and HalcyonLe3.cpp:
// the OpenXR SDK's headers and Halcyon's xr_types.h define the same names.

#include "plugin/HalcyonSequelBridge.hpp"

#if defined(MEVR_WITH_HALCYON_LE2)
#include "halcyon/le2/mevr_bridge.h"
#elif defined(MEVR_WITH_HALCYON_LE3)
#include "halcyon/le3/mevr_bridge.h"
#endif

#include "core/DevChannels.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/CombatStance.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/InteractLabel.hpp"
#include "engine/GameAdapter.hpp"
#include "game/GamepadMapping.hpp"
#include "plugin/Colour.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "plugin/EyeCopy.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/HudPieceLayers.hpp"
#include "plugin/MenusLe2.hpp"
#include "plugin/PadMerge.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/RecenterTilt.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/SequelSquadLayers.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/TransitionFade.hpp"
#include "plugin/TraversalCue.hpp"
#include "plugin/Vignette.hpp"
#include "plugin/WorldPanels.hpp"
#include "plugin/XrHost.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/Thespian.hpp"
#endif

#include <openxr/openxr.h>

#include <d3d11.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace mevr::host::sequel_bridge {
namespace {

bool gToldFirstSync = false;  // Present thread
bool gLoggedView = false;     // Present thread
// The floor lock's last pass (FloorViewTranslation, game thread): the view is published there while it runs.
std::atomic<std::uint64_t> gFloorTranslatedMs{0};
constexpr std::uint64_t kFloorTranslateQuietMs = 100;

std::mutex gPadMutex;  // the packet numbering below and the adapters' pad state
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;

sequel_cinema::State gScreen;

GalaxyTable gGalaxy;
std::uint64_t gGalaxyRead = 0;

// LE1's ControllerAimActive (plugin/HalcyonBridge.cpp): when the right controller's aim pose was last tracked.
constexpr std::uint64_t kControllerAimHoldMs = 1000;
std::atomic<std::uint64_t> gRightAimTrackedMs{0};
bool gControllerAim = false;  // Present thread: the last answer, for its log line

// The recenter's one log line (research/trilogy-seams.md "One recenter rule"), Present thread: a recenter replaces the app
// space after the frame's controllers were sampled in the old one (OnFrameBegun comes first in every Halcyon), so that
// frame's layers go in the new space with poses of the old; the next OnFrameBegun samples them in the new one. The line
// says how old the sample was and how many engine ticks (the mod's) began in between: what a drop of the controllers at the
// recenter (ME3's rule until 2026-11-16) would have shown the mod as no controllers.
using Clock = std::chrono::steady_clock;
Clock::time_point gSampledAt{};       // the last OnFrameBegun's sample
void* gSampledSpace = nullptr;        // the app space it was located in
bool gRecenterPending = false;
double gRecenterSampleAgeMs = 0.0;    // the sample's age at the recenter
void* gRecenterOldSpace = nullptr;
Clock::time_point gRecenteredAt{};
std::uint64_t gRecenterTicks = 0;     // crash::gGameTicks at the recenter
bool gRecenterHadSample = false;

// The constant eye (MevrSeams::ConstantFpEye; LE1's in plugin/HalcyonBridge.cpp): the settings, read on the game thread
// (RefreshConstantEye), and the explore mode's HookOffset, remembered on the Present thread. (0, 0, 120) until an Explore
// mode is seen: ME1's package default, ME3's Explore (read 2026-11-18 on the Normandy, lane AU5) and ME2's (its measured eye
// with the renderer's explore Offset).
std::atomic<bool> gConstantEyeOn{true};
std::atomic<bool> gCrouchDropOn{false};
std::mutex gConstantEyeMutex;  // the renderer's writers (the active mode, the blend's endpoints, ME2's prewarm)
mevr::vr::combat_stance::Vec gExploreHook{0.0, 0.0, 120.0};
// What was last logged per mode class (the renderer writes every frame, ME2's prewarm every mode): a line only on a change.
struct ConstantEyeLogged {
    char cls[64] = {};
    float hook[3] = {};
    float offset[3] = {};
    bool crouch = false;
};
ConstantEyeLogged gConstantEyeLogged[24];
int gConstantEyeLoggedCount = 0;

// True when this class's values differ from what was last logged for it (and remembers them).
bool ConstantEyeChanged(char const* cls, float const hook[3], float const offset[3], bool crouch) noexcept {
    ConstantEyeLogged* entry = nullptr;
    for (int i = 0; i < gConstantEyeLoggedCount; ++i) {
        if (std::strcmp(gConstantEyeLogged[i].cls, cls) == 0) {
            entry = &gConstantEyeLogged[i];
            break;
        }
    }
    if (entry == nullptr) {
        if (gConstantEyeLoggedCount >= static_cast<int>(std::size(gConstantEyeLogged))) {
            return false;  // more classes than the table holds: the rest go unlogged
        }
        entry = &gConstantEyeLogged[gConstantEyeLoggedCount++];
        strncpy_s(entry->cls, cls, _TRUNCATE);
    } else {
        bool same = entry->crouch == crouch;
        for (int i = 0; i < 3 && same; ++i) {
            same = std::fabs(entry->hook[i] - hook[i]) <= 0.05f && std::fabs(entry->offset[i] - offset[i]) <= 0.05f;
        }
        if (same) {
            return false;
        }
    }
    std::copy(hook, hook + 3, entry->hook);
    std::copy(offset, offset + 3, entry->offset);
    entry->crouch = crouch;
    return true;
}

} // namespace

bool ControllerAimActive() noexcept {
    std::uint64_t const tracked = gRightAimTrackedMs.load(std::memory_order_relaxed);
    bool const on = renderer::ModHandAim() && tracked != 0 && GetTickCount64() - tracked < kControllerAimHoldMs;
    if (on != gControllerAim) {
        gControllerAim = on;
        mevr::log::Info("{}: {}", game::Get().log, on ? "the right controller aims weapons: the hand reticle is on its aim"
                                                : "controller aim stopped: no hand reticle");
    }
    return on;
}

sequel_cinema::State& Screen() noexcept {
    return gScreen;
}

std::mutex& PadMutex() noexcept {
    return gPadMutex;
}

GalaxyTable const& GalaxyTableSettings() noexcept {
    std::uint64_t const now = GetTickCount64();
    if (gGalaxyRead != 0 && now - gGalaxyRead < 1000) {
        return gGalaxy;
    }
    gGalaxyRead = now;
    auto const file = ini::Read(SettingsPath()).file;
    gGalaxy.enabled = ini::GetNumber(file, "Galaxy", "nativeTable").value_or(0) != 0;
    auto number = [&](char const* key, float fallback, float lo, float hi) {
        auto const value = static_cast<float>(ini::GetNumber(file, "Galaxy", key).value_or(fallback));
        return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
    };
    gGalaxy.settings.distanceM = number("tableDistanceM", 0.70f, 0.3f, 3.0f);
    gGalaxy.settings.belowM = number("tableBelowM", 0.45f, 0.1f, 1.5f);
    gGalaxy.settings.radiusM = number("tableRadiusM", 0.48f, 0.2f, 1.5f);
    gGalaxy.settings.tiltDeg = number("worldTiltDeg", 35.0f, 0.0f, 80.0f);
    return gGalaxy;
}

void RefreshConstantEye() noexcept {
    std::wstring const& path = SettingsPath();
    bool const on = path.empty() || GetPrivateProfileIntW(L"Hands", L"constantEye", 1, path.c_str()) != 0;
    bool const drop = !path.empty() && GetPrivateProfileIntW(L"Hands", L"crouchDrop", 0, path.c_str()) != 0;
    static bool logged = false;
    if (!logged || on != gConstantEyeOn.load(std::memory_order_relaxed) || drop != gCrouchDropOn.load(std::memory_order_relaxed)) {
        logged = true;
        mevr::log::Info("constant eye: {} ([Hands] constantEye = {}, research/38 B); a physical crouch {} ([Hands] crouchDrop = {})",
            on ? "on: every standing on-foot camera mode puts the eye where exploring does" : "off: the renderer's per-mode offsets",
            on ? 1 : 0, drop ? "also gets the game's lower crouch camera" : "lowers the view by the real head only", drop ? 1 : 0);
    }
    gConstantEyeOn.store(on, std::memory_order_relaxed);
    gCrouchDropOn.store(drop, std::memory_order_relaxed);
}

} // namespace mevr::host::sequel_bridge

namespace MevrSeams {

namespace sb = mevr::host::sequel_bridge;

void* StaticXrGetInstanceProcAddr() noexcept {
    return reinterpret_cast<void*>(&xrGetInstanceProcAddr);
}

// The constant eye (research/38 B; LE1's MevrBridge::ConstantFpEye, the same rule in core/vr/CombatStance): each standing
// on-foot mode's Offset is its own HookOffset minus the explore eye, so a draw, aimed fire or a sprint leaves the eye in place.
bool ConstantFpEye(char const* modeClass, float const hookOffset[3], float const exploreOffset[3], float offset[3]) noexcept {
    namespace cs = mevr::vr::combat_stance;
    if (modeClass == nullptr || hookOffset == nullptr || exploreOffset == nullptr || offset == nullptr ||
        !sb::gConstantEyeOn.load(std::memory_order_relaxed)) {
        return false;
    }
    cs::Vec const hook{hookOffset[0], hookOffset[1], hookOffset[2]};
    if (!cs::PlausibleHook(hook)) {
        return false;
    }
    // The mod's physical crouch (kFlagPhysicalCrouch, with the pad options each tick): the crouch modes keep the eye too.
    bool const physicalCrouch = !sb::gCrouchDropOn.load(std::memory_order_relaxed) &&
        (mevr::host::pad_merge::FreshFlags(GetTickCount64()) & mevr::game::gamepad::kFlagPhysicalCrouch) != 0;
    bool const applies = cs::ConstantEyeApplies(modeClass, "", physicalCrouch);
    std::lock_guard lock(sb::gConstantEyeMutex);
    if (cs::IsExploreMode(modeClass, "")) {
        sb::gExploreHook = hook;
    }
    cs::Vec const eye = cs::ExploreEye(sb::gExploreHook, {exploreOffset[0], exploreOffset[1], exploreOffset[2]});
    cs::Vec const given{offset[0], offset[1], offset[2]};
    cs::Vec const want = applies ? cs::ConstantEyeOffset(hook, eye) : given;
    float const out[3] = {static_cast<float>(want.x), static_cast<float>(want.y), static_cast<float>(want.z)};
    // Once per class and values: the `camera: mode` lines then read the same eye.
    if (sb::ConstantEyeChanged(modeClass, hookOffset, out, physicalCrouch)) {
        auto const v = [](cs::Vec x) { return std::format("({:.1f}, {:.1f}, {:.1f})", x.x, x.y, x.z); };
        mevr::log::Info("constant eye: {}: HookOffset {} -> Offset {}{}: the eye {} from the hook bone, as exploring (the renderer's "
                        "Offset {}){}", modeClass, v(hook), v(want), applies ? "" : " kept (not a standing mode)", v(eye), v(given),
            physicalCrouch ? "; you are physically crouched: only your real head lowers the view" : "");
    }
    if (!applies) {
        return false;
    }
    std::copy(out, out + 3, offset);
    return true;
}

void WrapSessionFunctions(void** acquireSwapchainImage, void** waitSwapchainImage, void** releaseSwapchainImage,
    void** waitFrame, void** beginFrame, void** endFrame, void** locateViews) noexcept {
    mevr::host::testloop::WrapSessionFunctions(acquireSwapchainImage, releaseSwapchainImage, endFrame, locateViews);
    // The probe's wrappers go outside ours (its xrEndFrame timing includes a snapshot's work), as LE1's.
    mevr::host::perf::WrapFrameFunctions(waitSwapchainImage, waitFrame, beginFrame, endFrame);
    mevr::log::Info("test loop: wrapped {}'s xrAcquireSwapchainImage, xrReleaseSwapchainImage, xrEndFrame and "
                    "xrLocateViews (snapshots, recording, replay)", mevr::host::renderer::Capabilities().name);
}

// The frame-breakdown probe's steps (plugin/PerfProbe): MevrSeams' optional perf group.
void PerfPresentBegin(ID3D11DeviceContext* context) noexcept { mevr::host::perf::SeamPresentBegin(context); }
void PerfPresentEnd(ID3D11DeviceContext* context) noexcept { mevr::host::perf::SeamPresentEnd(context); }
void PerfFirstEyeCaptured(ID3D11DeviceContext* context) noexcept { mevr::host::perf::SeamFirstEyeCaptured(context); }
void PerfDraw() noexcept { mevr::host::perf::NoteDraws(); }

void OnSessionCreated(void* instance, void* session) noexcept {
    auto const xrInstance = static_cast<XrInstance>(instance);
    auto const xrSession = static_cast<XrSession>(session);
    sb::game::SessionCreated(instance, session);
    // Everything of ours that belonged to the old session: its swapchains, its poses, its latches. (ME3's bridge had the
    // whole list, ME2's a part of it; a new session is rare enough that the list was never missed.)
    mevr::host::sequel_squad_layers::ForgetSession();
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::thespian::ForgetSession();
#endif
    mevr::host::hud_piece_layers::ForgetSession();
    mevr::vr::interact::Reset();
    mevr::host::world_panels::Recenter();
    mevr::host::testloop::OnSessionCreated(xrSession);
    mevr::host::swapchains::OnSessionCreated(xrSession);  // ours went with the old session
    mevr::host::eye_copy::Reset();
    mevr::host::floor_lock::Reset();
    mevr::vr::controllers::Publish({});
    mevr::host::pad_merge::Reset();
    sb::gFloorTranslatedMs.store(0, std::memory_order_relaxed);  // the next OnViewBuilt publishes its view itself
    bool const attached = mevr::host::xr_host::OnSessionCreated(xrInstance, xrSession);
    mevr::log::Info("{}: our controller actions are {} Halcyon's session", sb::game::Get().log,
        attached ? "attached to" : "not available on");
    // The colour correction's per-headset settings and automatic profile (plugin/Colour, research/27), as LE1's bridge.
    {
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId system = XR_NULL_SYSTEM_ID;
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
        XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(xrGetSystem(xrInstance, &systemInfo, &system)) &&
            XR_SUCCEEDED(xrGetSystemProperties(xrInstance, system, &properties)) &&
            XR_SUCCEEDED(xrGetInstanceProperties(xrInstance, &runtime))) {
            mevr::host::colour::SetHeadset(runtime.runtimeName, properties.systemName, properties.vendorId);
        }
    }
    mevr::host::menus_le2::OnSessionCreated(xrInstance, xrSession);  // our menu and the calibration (M4)
    sb::gToldFirstSync = false;
}

void OnSessionStopped() noexcept {
    sb::game::SessionStopped();
    mevr::host::sequel_squad_layers::ForgetSession();
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::thespian::ForgetSession();
#endif
    mevr::host::hud_piece_layers::ForgetSession();
    mevr::vr::interact::Reset();
    sb::gScreen = {};
    mevr::host::hud_panel::SetArmed(false);  // no OverlayLayers runs now to disarm it: the UI back to the frame
    mevr::host::vignette::Forget();
    mevr::host::traversal_cue::Forget();
    mevr::host::menus_le2::OnSessionStopped();  // our menu and the calibration close: nobody can see them
    mevr::host::world_panels::Recenter();
    mevr::host::floor_lock::Reset();  // the queued and published views (host_view::Reset)
    mevr::host::eye_copy::Reset();
    mevr::host::xr_host::ForgetHaptics();
    mevr::vr::controllers::Publish({});
    mevr::host::pad_merge::Reset();
    mevr::log::Info("{}: the session stopped", sb::game::Get().log);
}

void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept {
    if (session == nullptr) {
        return;
    }
    auto const xrSession = static_cast<XrSession>(session);
    auto const xrSpace = static_cast<XrSpace>(space);
    auto const time = static_cast<XrTime>(predictedDisplayTime);
    mevr::host::world_panels::NoteFrame();  // every frame: a panel (the 3D cinema screen) knows whether it was shown before
    sb::gScreen = {};                       // this frame's CineScreenLayers places it again
    mevr::vr::controllers::State state;
    mevr::host::xr_host::OnFrameBegun(xrSession, xrSpace, time, state);
    bool const focused = mevr::host::xr_host::InputFocused();
    sb::game::FrameBegun(session, space, focused, state.active);
    mevr::vr::controllers::State const live = state;  // the player's own, before a replay's takes its place
    // The test loop records the live sample, or replaces it with a recording's (plugin/TestLoop), as on LE1.
    mevr::host::testloop::OnFrameBegun(xrSession, xrSpace, time, focused, state);
    if (state.active && state.right.aimValid) {
        sb::gRightAimTrackedMs.store(GetTickCount64(), std::memory_order_relaxed);  // LE1's, after the test loop's sample
    }
    // Our main menu and the guided calibration take the controllers while they're up (plugin/MenusLe2, M4). An inactive
    // sample is published too: it must replace the last active one (focus lost, a controller unplugged, a replay's end).
    mevr::vr::controllers::Publish(mevr::host::menus_le2::OnFrameBegun(xrSession, xrSpace, time, live, state, focused));
    if (mevr::host::xr_host::InputReady() && !sb::gToldFirstSync) {
        sb::gToldFirstSync = true;
        mevr::log::Info("{}: first controller sync (session {}focused)", sb::game::Get().log, focused ? "" : "not ");
    }
    sb::game::FramePublished();
    sb::Clock::time_point const sampledAt = sb::Clock::now();
    if (sb::gRecenterPending) {
        sb::gRecenterPending = false;
        std::uint64_t const ticks = mevr::host::crash::gGameTicks.load(std::memory_order_relaxed) - sb::gRecenterTicks;
        double const sinceMs = std::chrono::duration<double, std::milli>(sampledAt - sb::gRecenteredAt).count();
        if (sb::gRecenterHadSample) {
            mevr::log::Info("{}: recenter: that frame's controllers had been sampled {:.1f} ms before it, in the old app space "
                            "({}), and its layers went in the new one ({}); the first sample in the new space came {:.1f} ms after "
                            "it, {} engine tick{} later (ticks that would have seen no controllers had the recenter dropped them)",
                sb::game::Get().log, sb::gRecenterSampleAgeMs, sb::gRecenterOldSpace, space, sinceMs, ticks, ticks == 1 ? "" : "s");
        }
    }
    sb::gSampledAt = sampledAt;
    sb::gSampledSpace = space;
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
    // left-positive yaw); ours (UE rotators) + right. Pitch is + up in both.
    view.lookYaw = -lookYawUU;
    view.lookPitch = lookPitchUU;
    view.offsetForward = offsetForwardUU;
    view.offsetRight = offsetRightUU;
    view.offsetUp = offsetUpUU;
    view.unitsPerMeter = unitsPerMeter;
    // The floor lock (plugin/FloorLock, as LE1's): the view goes out from the game thread's ApplyHeadPosition with the
    // pair it rendered (FloorViewTranslation). Where that didn't run lately (the lean off, a VR cine's tracking lock) the
    // view goes out from here, uncorrected, so the mod keeps its view.
    mevr::vr::floor::State floor;
    mevr::host::xr_host::Floor(&floor);
    mevr::host::floor_lock::Queue(view, floor, floorWorld);
    std::uint64_t const now = GetTickCount64();
    if ((!worldView && sb::game::Get().invalidViewAtOnce) ||
        now - sb::gFloorTranslatedMs.load(std::memory_order_relaxed) > sb::kFloorTranslateQuietMs) {
        mevr::vr::host_view::Publish(view, now);
    }
    if (worldView && !sb::gLoggedView) {
        sb::gLoggedView = true;
        mevr::log::Info("{}: first world view: head look yaw {} pitch {} (head aim {}), view offset "
                        "fwd {:.1f} right {:.1f} up {:.1f}, {:.1f} units per meter, head at ({:.2f}, {:.2f}, {:.2f}) m",
            sb::game::Get().log, view.lookYaw, view.lookPitch, headAim ? "on" : "off", offsetForwardUU, offsetRightUU,
            offsetUpUU, unitsPerMeter, headP[0], headP[1], headP[2]);
    }
}

float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ, float* right, float* up,
    float* forward) noexcept {
    sb::gFloorTranslatedMs.store(GetTickCount64(), std::memory_order_relaxed);
    return mevr::host::floor_lock::Translate(replay, rawCameraZ, rightZ, upZ, forwardZ, right, up, forward);
}

void OnRecentered() noexcept {
    // The one recenter rule (research/trilogy-seams.md "One recenter rule", as LE1's HalcyonOverlays): what is placed in the
    // room is placed again and the floor's recenter count goes up (xr_host::NoteRecentered: the mod's body follow and Mako
    // controls re-anchor on it); nothing published is dropped. The next frame's sample is in the new space.
    {
        sb::Clock::time_point const now = sb::Clock::now();
        sb::gRecenterHadSample = sb::gSampledSpace != nullptr;
        sb::gRecenterSampleAgeMs = std::chrono::duration<double, std::milli>(now - sb::gSampledAt).count();
        sb::gRecenterOldSpace = sb::gSampledSpace;
        sb::gRecenteredAt = now;
        sb::gRecenterTicks = mevr::host::crash::gGameTicks.load(std::memory_order_relaxed);
        sb::gRecenterPending = true;
    }
    sb::game::Recentered();
    mevr::host::recenter_tilt::Committed();  // the orientation RecenterOrientation chose is the app space's now
    mevr::host::xr_host::NoteRecentered();
    mevr::host::sequel_squad_layers::Recentered();
    mevr::host::hud_panel::Recentered();   // the HUD panel straight ahead again, as LE1's
    mevr::host::world_panels::Recenter();  // the 3D cinema screen placed afresh
    mevr::host::menus_le2::OnRecentered(); // our menu and the calibration placed again (M4)
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::thespian::Recentered();
#endif
    mevr::log::Info("{}: recentered", sb::game::Get().log);
}

// ---- ME1's 3D cinema screen (plugin/CineScreen, CinemaBackdrop, WorldPanels; research/52, research/le2-cinema.md) ----------
// (CineScreenWanted and CineScreenFullRate are plugin/Seams.cpp's, the same for every game.)
unsigned CineScreenLayers(const CineScreenFrame& frame, const void** layersOut, unsigned capacity) noexcept {
    return mevr::host::sequel_cinema::Layers(frame, layersOut, capacity, sb::gScreen);
}

const void* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept {
    return mevr::host::transition_fade::Layer(session, viewSpace, context, view);
}

bool HudPanelDraw(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool textBatch, bool afterLeftEye) noexcept {
    sb::game::UiDraw(context, afterLeftEye);
    return mevr::host::hud_panel::Redirect(context, draw, user, textBatch, afterLeftEye);
}

DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept {
    if (index != 0 || state == nullptr) {
        return result;
    }
    mevr::vr::controllers::State const controllers = mevr::vr::controllers::Latest();
    if (!controllers.active) {
        mevr::host::pad_merge::NoControllers();
    }
    std::lock_guard lock(sb::gPadMutex);
    int const mode = mevr::host::renderer::GameMode();
    bool const kept = sb::game::PadKeeps(mode);
    if (!controllers.active && !kept && !mevr::host::menus_le2::PadWanted()) {  // our menu or the calibration has the pad (M4)
        return result;
    }
    bool const realPad = result == ERROR_SUCCESS;
    XINPUT_GAMEPAD pad = realPad ? state->Gamepad : XINPUT_GAMEPAD{};
    mevr::host::pad_merge::Renderer renderer;
    renderer.ownMenuOpen = mevr::host::renderer::OwnMenuOpen();  // Halcyon's own menu
    renderer.headLookYawUU = &mevr::host::renderer::MoveStickHeadLookYawUU;  // its "run where you look", after the merge
    renderer.gateRightClick = true;
    // The right stick click's hold is the power wheel's on foot (the mod's, ME1's rule): that press never reaches the game as
    // R3, which is ME2's Console_ShowMap (the area map, the objective radar) and ME3's NavAssistanceOrCoverTurn (a point of
    // interest, the area map). The owner, 2026-11-17 (docs/trilogy-plan.md question 14); the map stays on the omni-tool's
    // Map entry, and Halcyon's sequel recenter is a double Back, not R3.
    renderer.keepPowerClick = true;
    // The interrupts (M5.2, research/le2-cinema.md): in the conversation mode the triggers are the game's LT (Paragon) and
    // RT (Renegade), as on a pad; nothing of ours keeps them (pad_merge::ConversationTriggers).
    renderer.conversationTriggers = mevr::engine::adapter::IsConversationMode(mode);
    renderer.orbitalTriggers = sb::game::PadOrbitalTriggers(mode);
    if (controllers.active) {
        // The motion controllers with the mod's flags, merged over a real pad (plugin/PadMerge, LE1's mapping): the support
        // grip, the holsters' and the squad's grips, comfort's turn and walk. Both stick clicks are the chord (our menu,
        // MenusLe2's OnFrameBegun), kept from the game; R3 waits for L3 as L3 for R3 (the owner's L3 + R3 priority).
        pad = mevr::host::pad_merge::Merge(pad, controllers, renderer, GetTickCount64());
    }
    sb::game::PadMerged(mode, controllers.active, pad);
    // Neutral while our menu or the calibration has it; after, what was held then is kept from the game until let go.
    mevr::host::menus_le2::FilterPad(realPad ? &state->Gamepad : nullptr, pad);
    sb::game::PadFiltered(mode, renderer.ownMenuOpen, pad);
    if (std::memcmp(&pad, &sb::gLastPad, sizeof pad) != 0) {
        if (controllers.active && renderer.conversationTriggers && !renderer.ownMenuOpen) {
            auto const down = [](BYTE value) { return value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD; };
            if ((down(pad.bLeftTrigger) != down(sb::gLastPad.bLeftTrigger) ||
                    down(pad.bRightTrigger) != down(sb::gLastPad.bRightTrigger)) &&
                mevr::dev_channels::Allowed(std::filesystem::path(mevr::host::SettingsPath()))) {
                mevr::log::Info("conversation pad 0: LT {}, RT {}", pad.bLeftTrigger, pad.bRightTrigger);
            }
        }
        sb::gLastPad = pad;
        ++sb::gPacket;  // the game only re-reads a pad whose packet number changed
    }
    state->dwPacketNumber = sb::gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
}

} // namespace MevrSeams

// Each sequel tree's own seam with the same answer (declared in both le2/ and le3/mevr_bridge.h).
namespace ME2VR::MevrBridge {
float CineScreenHalfSeparationUU(float halfH) noexcept {
    return mevr::host::sequel_cinema::HalfSeparationUU(halfH);
}
} // namespace ME2VR::MevrBridge
