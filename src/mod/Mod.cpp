// The hot-reloadable part of MEVR. Everything that changes often lives behind these entry
// points; the host ASI only owns the loader hookup, the engine tick and the Present hook.

#include "mod/ModApi.hpp"

#include "core/Hotkey.hpp"
#include "core/Log.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/Output.hpp"
#include "core/vr/TestRequest.hpp"
#include "core/vr/ViewSync.hpp"
#include "engine/BonePose.hpp"
#include "engine/CallFunction.hpp"
#include "engine/CodeDump.hpp"
#include "engine/Engine.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/NativesDump.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/SceneView.hpp"
#include "game/ArmIk.hpp"
#include "game/GamepadBridge.hpp"
#include "game/GamepadMapping.hpp"
#include "game/HandAim.hpp"
#include "game/HandTargets.hpp"
#include "game/HeadSim.hpp"
#include "game/HeadTracking.hpp"
#include "game/Reflections.hpp"
#include "game/SkeletonProbe.hpp"
#include "game/SupportGrip.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Weapon.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>

namespace {

constexpr double kStartupRetrySeconds = 1.0;
constexpr double kHeartbeatSeconds = 5.0;

std::unique_ptr<::LESDK::Initializer> gInit;
std::atomic<bool> gActive{false};  // read on the render thread too
// LE1: the host runs Halcyon's renderer (HostServices::controllers set). Then the mod does no VR
// output, render-path head tracking, reflection fix or XInput hooking of its own (the renderer
// does all of those), and gets the motion controllers from the host.
mevr::HostServices const* gHost = nullptr;
bool gHostOwnsVr = false;
bool gToldHostOwnsVr = false;
bool gToldStateMismatch = false;
// Host renderer (HostServices v4): it publishes its view (game/view_anchor maps the controllers
// into the world with it) and takes our word on hand aim (its head aim stops while we aim).
bool gHostView = false;
// Host renderer: this tick's controllers and view came from the host (state sizes match; always
// in the monolithic build, which shares them).
bool gHostDataOk = false;
// Host renderer: a headset session has shown up (the host published a view or a controller
// sample) since the host started. Until then, arm IK's desktop test stands in for it.
bool gHeadsetSeen = false;
// Host renderer: hand aim comes on by itself the first time a motion controller aims, unless `,`
// has decided it this session.
bool gHandAimChosen = false;
bool gHandAimReported = false;  // what the host was last told
// Host renderer: arm IK comes on by itself the first time tracked controllers have a view to be
// placed in, unless `.` has decided it this session.
bool gArmIkChosen = false;
// Host renderer (HostServices v6): it draws the reticle along the gun's aim ray we publish.
bool gToldNoAimRay = false;
bool gAimRaySent = false;  // a ray was published (cleared again at shutdown)
// Host renderer (HostServices v7): it runs the two-handed grip's pad mapping.
bool gToldNoSupportGrip = false;
bool gHooked = false;  // ProcessEvent hook live (after the script packages loaded)
bool gSceneViewHooked = false;
bool gGamepadHooked = false;  // XInput hooks (once the game has loaded an XInput DLL)
bool gCallFunctionHooked = false;  // installed the first time hand aim is turned on
bool gHandAimAvailable = false;
bool gBonePoseTried = false;  // the arm writeback hook goes in the first time arm IK is turned on
bool gBonePoseHooked = false;
// `]` is the one switch a player needs: once a VR session runs, every VR feature comes on with
// it (stereo, 6DOF, hand aim, arm IK), and turning VR off (or quitting from the headset) turns
// them off. The headset sleeping doesn't count as off. Within a session the feature keys switch
// each one individually (A/B tests, isolating a problem); the next `]` on resets them.
bool gVrFeatures = false;

mevr::Hotkey gVrToggleKey{VK_OEM_6};    // ]  VR on / off, with all VR features
mevr::Hotkey gModeToggleKey{VK_OEM_4};  // [  head-tracked / screen
mevr::Hotkey gHeadPathKey{VK_OEM_5};    // \  head path: render / camera / off (A/B)
mevr::Hotkey gHeadSimKey{VK_OEM_7};     // '  desktop head simulator: sweep / nod / off
mevr::Hotkey gStereoKey{VK_OEM_1};      // ;  stereo: mono / alternate-eye (render path, headset)
mevr::Hotkey gPositionKey{VK_OEM_2};    // /  6DOF: head position moves the view (render path)
mevr::Hotkey gHandAimKey{VK_OEM_COMMA}; // ,  aim weapons with the right controller (on foot)
mevr::Hotkey gArmIkKey{VK_OEM_PERIOD};  // .  VRIK arms follow the controllers (no headset: desktop test pose)
mevr::Hotkey gCinemaKey{VK_OEM_PLUS};   // =  conversations, cutscenes, menus on the virtual screen
mevr::Hotkey gLockstepKey{VK_OEM_MINUS}; // -  lockstep on / off (frame pacing A/B)
bool gAutoCinema = true;
// The game mode can flicker (e.g. between conversation lines); switch the screen only once a
// state has held this long, so every flicker isn't a layer change.
constexpr double kScreenSwitchSeconds = 0.3;
bool gScreenWanted = false;
bool gScreenShown = false;
double gScreenWantedFor = 0.0;

double gRetryTimer = kStartupRetrySeconds;  // first attempt on the first tick
double gHeartbeatElapsed = 0.0;
unsigned gHeartbeatFrames = 0;
unsigned gHeartbeats = 0;

// Lockstep: after each engine tick, wait until that frame is presented, so the head pose read
// for the next frame is predicted for the display time it will actually be shown at (vrframework
// guide 07). Off by default: in game it serialized the game and render threads, dropping 88 fps
// to 50 (2026-10-08). The runtime reprojects each image from the pose it carries, so a pose
// predicted a frame ahead (vr::output::SetLockstep) costs little. Pairing images with views doesn't depend on it (view_sync consumes one view per
// Present). The timeout only guards against a frame that never presents: it's long enough that
// an ordinary hitch never trips it, since a timed-out wait lets the game run a frame ahead.
constexpr std::uint32_t kLockstepTimeoutMs = 100;
// Several timeouts in a row mean a view whose frame never presented is stuck at the front of
// the queue (every image would get the previous frame's view): drop it.
constexpr unsigned kDriftTimeouts = 3;
bool gLockstep = false;
unsigned gLockstepWaits = 0;
unsigned gLockstepTimeouts = 0;
unsigned gConsecutiveTimeouts = 0;
// Presents seen when the last wait timed out: if none came since, presenting has stopped
// (e.g. minimized), so waiting would only slow the game to the timeout rate.
std::optional<std::uint64_t> gPresentsAtTimeout;
unsigned gDriftRecoveries = 0;
double gLockstepWaitMs = 0.0;

void WaitForThisFrameToPresent() {
    using namespace mevr;
    if (!gLockstep || !vr::output::WantsHeadTracking() || !vr::output::ShowsFrames() || !vr::view_sync::HasPendingFrame()) {
        gConsecutiveTimeouts = 0;
        return;
    }
    if (gPresentsAtTimeout) {
        if (vr::view_sync::PresentCount() == *gPresentsAtTimeout) {
            return;  // still no Present since the last timeout
        }
        gPresentsAtTimeout.reset();
    }
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&start);
    bool const presented = vr::view_sync::WaitForPresent(kLockstepTimeoutMs);
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    ++gLockstepWaits;
    gLockstepWaitMs += static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
    if (presented) {
        gConsecutiveTimeouts = 0;
        return;
    }
    ++gLockstepTimeouts;
    gPresentsAtTimeout = vr::view_sync::PresentCount();
    if (++gConsecutiveTimeouts >= kDriftTimeouts) {
        vr::view_sync::DropOldest();
        gConsecutiveTimeouts = 0;
        ++gDriftRecoveries;
    }
}

// Script-level hooks need Engine.u/SFXGame.u loaded, which happens after attach. Handlers are
// registered before the ProcessEvent hook goes live so the table never changes under it.
void TryInstallScriptHooks() {
    using namespace mevr;
    // CalcSceneView is native and always there, but the handlers it serves need the scripts.
    if (!gSceneViewHooked && !gHostOwnsVr) {
        gSceneViewHooked = engine::scene_view::Install(*gInit, &game::head_tracking::OnBeforePrimaryView);
    }
    // Without the scene view hook (or a head source) head tracking stays inert; it still reads
    // the game mode, which the probes and features use.
    if (!game::head_tracking::TryStartup(gSceneViewHooked)) {
        return;
    }
    if (!gHostOwnsVr && !game::reflections::TryStartup()) {
        log::Warn("reflections: SceneCaptureComponent.SetFrameRate not found; reflection fix off");
    }
    if (!game::skeleton_probe::TryStartup()) {  // also starts engine::skeleton (arm IK uses it)
        log::Warn("skeleton probe: bone query functions not found");
    }
    if (!game::weapon::TryStartup()) {
        log::Warn("weapon: BioWeaponRanged.GetMuzzleLoc not found (or not LE1): shots and hands keep the controller's aim");
    }
    gHandAimAvailable = game::hand_aim::TryStartup();
    if (!gHandAimAvailable) {
        log::Warn("hand aim: BioPlayerController.GetAdjustedAimFor not found; hand aim unavailable");
    }
    if (!engine::process_event::Install(*gInit)) {
        log::Error("ProcessEvent hook failed; mod inactive");
        gActive = false;
        return;
    }
    game::head_tracking::SetReady();
    gHooked = true;
}

// Every script call passes through the CallFunction hook, so it only goes in once hand aim is
// first turned on (game thread, outside any script call; handlers already registered).
void EnsureCallFunctionHook() {
    using namespace mevr;
    if (!gCallFunctionHooked) {
        gCallFunctionHooked = engine::call_function::Install(*gInit);
        if (!gCallFunctionHooked) {
            log::Warn("hand aim: script hook failed; only native aim queries are redirected");
        }
    }
}

// UpdateSkelPose runs for every skeletal mesh, so its hook only goes in once arm IK is first
// turned on (game thread, outside the engine tick).
void EnsureBonePoseHook() {
    using namespace mevr;
    if (!gBonePoseTried) {
        gBonePoseTried = true;
        gBonePoseHooked = engine::bone_pose::Install(*gInit);
    }
}

void SetVrFeatures(bool on) {
    using namespace mevr;
    gVrFeatures = on;
    log::Info("VR features {}", on ? "on: stereo, 6DOF, hand aim, arm IK (; / , . switch each)" : "off");
    game::head_tracking::SetStereo(on ? game::head_tracking::Stereo::AlternateEye : game::head_tracking::Stereo::Mono);
    game::head_tracking::SetPositional(on);
    if (on) {
        EnsureBonePoseHook();
    }
    game::arm_ik::SetEnabled(on);
    if (gHandAimAvailable) {
        if (on) {
            EnsureCallFunctionHook();
        }
        game::hand_aim::SetEnabled(on);
    }
}

// Arm IK's desktop test (the right hand held in front of the chest) stands in for the controllers
// only where no headset can be followed: our renderer with no VR session, or the host's renderer
// before any headset session has shown up. Decided every tick (before arm IK's), never latched: a
// view lost for a moment (the Insert menu, a flat conversation, a tracking blip, the first tick
// after a hot reload) leaves the arms to the animation instead of switching to the test.
bool NoHeadset() {
    using namespace mevr;
    if (!gHostOwnsVr) {
        return !vr::output::Running();
    }
    if (!gHostDataOk) {
        return false;  // the host's state is unreadable: assume a headset rather than risk the test in one
    }
    if (!gHeadsetSeen) {
        // Any view or controller sample means an XR session ran. The host keeps the sequences
        // across hot reloads, and the mod mirrors them each tick (MirrorHostState).
        gHeadsetSeen = vr::host_view::Latest(UINT64_MAX).sequence != 0 || vr::controllers::Latest(UINT64_MAX).sequence != 0;
    }
    return !gHeadsetSeen;
}

// `.`: arm IK on or off. With no headset to follow, it's the desktop test (NoHeadset).
void ToggleArmIk() {
    using namespace mevr;
    bool const on = game::arm_ik::Toggle();
    if (on) {
        EnsureBonePoseHook();
    }
    log::Info(".: arm IK {}{}", on ? "on" : "off",
        on && NoHeadset() ? " (no headset to follow: desktop test, right hand held in front of the chest)" : "");
}

// Host renderer: arm IK, like hand aim, comes on by itself once tracked controllers have a view.
void UpdateHostArmIk() {
    using namespace mevr;
    if (!gHostView || !gHooked || gArmIkChosen || game::arm_ik::Enabled()) {
        return;
    }
    if (vr::controllers::Latest().active && game::view_anchor::Current()) {
        EnsureBonePoseHook();
        game::arm_ik::SetEnabled(true);
        log::Info("arm IK on: tracked controllers in a head-tracked view (. toggles)");
    }
}

// Host renderer: hand aim needs the host's view, and the CallFunction hook (the weapon asks for
// its aim from script). Comes on the first time the right controller aims, unless `,` decided.
void UpdateHostHandAim() {
    using namespace mevr;
    if (!gHostView || !gHooked || !gHandAimAvailable) {
        return;
    }
    if (!gHandAimChosen && !game::hand_aim::Enabled()) {
        vr::controllers::State const controllers = vr::controllers::Latest();
        if (controllers.active && controllers.right.aimValid) {
            EnsureCallFunctionHook();
            game::hand_aim::SetEnabled(true);
            log::Info("hand aim on: motion controllers are active, so the right controller aims (, toggles)");
        }
    }
    // Tell the host only what actually works, or its head aim stops with nothing in its place:
    // without the CallFunction hook, shots fired from script keep the game's aim; without the
    // host's controllers and view (state sizes differ: an older host), hand aim has nothing to
    // aim with.
    bool const aiming = game::hand_aim::Enabled() && gCallFunctionHooked && gHostDataOk;
    if (aiming != gHandAimReported) {
        gHandAimReported = aiming;
        gHost->setHandAim(aiming);
    }
}

// The host can take the gun's aim ray: v6 services, built with the same Ray (hot reload replaces
// only the mod). The monolithic build passes its own services, so this holds there too.
bool HostTakesAimRay() {
    using namespace mevr;
    return gHostView && gHost->version >= 6 && gHost->setAimRay != nullptr &&
        gHost->aimRaySize == sizeof(vr::aim_ray::Ray);
}

// Host renderer: the gun's aim ray (muzzle and barrel, tracking space) for its reticle, every
// tick; `valid` false whenever shots don't follow the barrel.
void PublishAimRay() {
    using namespace mevr;
    if (!gHostView || !gHostDataOk) {
        return;
    }
    if (!HostTakesAimRay()) {
        if (!gToldNoAimRay) {
            gToldNoAimRay = true;
            log::Warn("aim ray: the host (services v{}, ray size {} vs {}) can't take it: no reticle along the gun; close the "
                      "game and redeploy the host", gHost->version, gHost->version >= 6 ? gHost->aimRaySize : 0u,
                sizeof(vr::aim_ray::Ray));
        }
        return;
    }
    vr::aim_ray::Ray const ray = game::hand_aim::AimRay();
    gHost->setAimRay(&ray);
    gAimRaySent = true;
}

// Host renderer (HostServices v7): its XInput hook builds the pad, so it runs the two-handed grip's
// mapping with our distance and settings, and tells us whether it holds.
bool HostTakesSupportGrip() {
    using namespace mevr;
    return gHostOwnsVr && gHost->version >= 7 && gHost->setSupportGrip != nullptr && gHost->supportGripHeld != nullptr;
}

void PublishSupportGrip() {
    using namespace mevr;
    if (!gHostOwnsVr) {
        return;  // our renderer: game/gamepad_bridge reads support_grip itself
    }
    if (!HostTakesSupportGrip()) {
        if (!gToldNoSupportGrip) {
            gToldNoSupportGrip = true;
            log::Warn("support grip: the host (services v{}) can't take it: no two-handed grip, and [Controls] leftTrigger "
                      "isn't applied; close the game and redeploy the host", gHost->version);
        }
        game::support_grip::SetHeld(false);
        return;
    }
    game::gamepad::Options const options = game::support_grip::PadOptions();
    gHost->setSupportGrip(options.support.distance, options.support.acquire, options.support.release,
        game::gamepad::ToFlags(options));
    game::support_grip::SetHeld(gHost->supportGripHeld());
}

// Host renderer (HostServices v8): while the test loop takes a snapshot, our state for its JSON.
void PublishStatus() {
    using namespace mevr;
    if (!gHostOwnsVr || gHost->version < 8 || gHost->statusWanted == nullptr || gHost->setStatus == nullptr ||
        !gHost->statusWanted()) {
        return;
    }
    namespace json = vr::test_request;
    auto const mode = gHooked ? game::head_tracking::GameMode() : std::nullopt;
    char const* type = "none";
    switch (game::weapon::Type()) {
    case game::weapon::RangedType::Pistol: type = "pistol"; break;
    case game::weapon::RangedType::Shotgun: type = "shotgun"; break;
    case game::weapon::RangedType::AssaultRifle: type = "assault rifle"; break;
    case game::weapon::RangedType::Sniper: type = "sniper rifle"; break;
    case game::weapon::RangedType::Unknown: type = game::weapon::Key() != 0 ? "unknown" : "none"; break;
    }
    std::string const status = std::format(
        "{{\"built\": {}, \"scriptHooks\": {}, \"gameMode\": {}, \"noHeadset\": {}, "
        "\"armIk\": {{\"enabled\": {}, \"status\": {}, \"handSource\": {}, \"rightHandAimsGun\": {}, \"supportRequested\": {}}}, "
        "\"handAim\": {{\"enabled\": {}, \"toldHost\": {}}}, "
        "\"weapon\": {{\"type\": {}, \"key\": \"0x{:X}\", \"status\": {}}}, "
        "\"supportGrip\": {{\"held\": {}, \"blend\": {}}}}}",
        json::JsonString(__DATE__ " " __TIME__), gHooked ? "true" : "false", mode ? std::to_string(*mode) : std::string("null"),
        NoHeadset() ? "true" : "false", game::arm_ik::Enabled() ? "true" : "false", json::JsonString(game::arm_ik::Status()),
        json::JsonString(game::arm_ik::HandSource()), game::arm_ik::RightHandAimsGun() ? "true" : "false",
        game::arm_ik::SupportRequested() ? "true" : "false", game::hand_aim::Enabled() ? "true" : "false",
        gHandAimReported ? "true" : "false", json::JsonString(type), static_cast<unsigned long long>(game::weapon::Key()),
        json::JsonString(game::weapon::Status()), game::support_grip::Held() ? "true" : "false",
        json::JsonNumber(game::support_grip::Blend()));
    gHost->setStatus(status.c_str());
}

// Host renderer: copies the host's latest controllers and view into the mod (first thing each
// tick, so everything after sees this tick's).
void MirrorHostState() {
    using namespace mevr;
#if defined(MEVR_MOD_DLL)
    // A v4+ host says how big its structs are; older hosts can't, so trust them.
    bool const sizesMatch = gHost->version < 4 || (gHost->controllerStateSize == sizeof(vr::controllers::State) &&
        gHost->viewStateSize == sizeof(vr::host_view::State));
    if (sizesMatch) {
        vr::controllers::State controllers;
        gHost->controllers(&controllers);
        vr::controllers::Mirror(controllers);  // keeps the host's timestamp, so it still ages
        if (gHostView) {
            vr::host_view::State view;
            gHost->view(&view);
            vr::host_view::Mirror(view);
        }
    } else if (!gToldStateMismatch) {
        gToldStateMismatch = true;
        log::Error("host state sizes differ from the mod's (controllers {} vs {}, view {} vs {}): close the game and "
            "redeploy the host; controllers and the view ignored, hand aim stays with the host", gHost->controllerStateSize,
            sizeof(vr::controllers::State), gHost->viewStateSize, sizeof(vr::host_view::State));
    }
    gHostDataOk = sizesMatch;
#else
    gHostDataOk = true;  // monolithic: host and mod already share this state
#endif
}

void PollHotkeys() {
    using namespace mevr;
    if (gHostOwnsVr) {
        // `,` still switches hand aim (A/B against Halcyon's head aim). Everything else is
        // Halcyon's (Insert menu, R recenter, K first person).
        if (gHandAimKey.Pressed()) {
            if (!gHostView || !gHandAimAvailable) {
                log::Info(",: hand aim unavailable{}", gHostView ? "" : " (the host publishes no view: redeploy the host ASI)");
            } else {
                gHandAimChosen = true;
                bool const on = game::hand_aim::Toggle();
                if (on) {
                    EnsureCallFunctionHook();
                }
                log::Info(",: hand aim {}", on ? "on (the right controller aims)" : "off (Halcyon's head aim)");
            }
        }
        if (gArmIkKey.Pressed()) {
            gArmIkChosen = true;
            ToggleArmIk();
        }
        for (Hotkey* key : {&gVrToggleKey, &gModeToggleKey, &gHeadPathKey, &gHeadSimKey, &gStereoKey, &gPositionKey,
                 &gCinemaKey, &gLockstepKey}) {
            if (key->Pressed() && !gToldHostOwnsVr) {
                gToldHostOwnsVr = true;
                log::Info("Halcyon's renderer is active: our VR keys are off except , (hand aim) and . (arm IK); "
                          "its menu is \\ (or both thumbsticks held)");
            }
        }
        return;
    }
    if (gVrToggleKey.Pressed()) {
        bool const enable = !vr::output::IsEnabled();
        vr::output::SetEnabled(enable);
        log::Info("]: VR {}", enable ? "ON" : "OFF");
    }
    if (gModeToggleKey.Pressed()) {
        auto const mode = vr::output::GetMode() == vr::output::Mode::HeadTracked
            ? vr::output::Mode::Screen
            : vr::output::Mode::HeadTracked;
        vr::output::SetMode(mode);
        log::Info("[: {}", mode == vr::output::Mode::HeadTracked ? "head-tracked" : "screen");
    }
    if (gHeadPathKey.Pressed()) {
        log::Info("\\: head path {}", game::head_tracking::Name(game::head_tracking::CyclePath()));
    }
    if (gHeadSimKey.Pressed()) {
        log::Info("': head simulator {}{}", game::head_sim::Name(game::head_sim::Cycle()),
            vr::output::WantsHeadTracking() ? " (ignored while VR is head-tracked)" : "");
    }
    if (gStereoKey.Pressed()) {
        log::Info(";: stereo {}", game::head_tracking::Name(game::head_tracking::CycleStereo()));
    }
    if (gPositionKey.Pressed()) {
        log::Info("/: 6DOF {}", game::head_tracking::TogglePositional() ? "on" : "off");
    }
    if (gLockstepKey.Pressed()) {
        gLockstep = !gLockstep;
        vr::output::SetLockstep(gLockstep);
        log::Info("-: lockstep {}", gLockstep ? "on" : "off");
    }
    if (gCinemaKey.Pressed()) {
        gAutoCinema = !gAutoCinema;
        log::Info("=: conversations and cutscenes on the virtual screen {}", gAutoCinema ? "on" : "off");
    }
    if (gArmIkKey.Pressed()) {
        ToggleArmIk();
    }
    if (gHandAimKey.Pressed()) {
        if (!gHandAimAvailable) {
            log::Info(",: hand aim unavailable");
        } else {
            bool const on = game::hand_aim::Toggle();
            if (on) {
                EnsureCallFunctionHook();
            }
            log::Info(",: hand aim {}", on ? "on" : "off");
        }
    }
}

void Heartbeat() {
    using namespace mevr;
    if (gHeartbeats == 0) {
        engine::code_dump::SaveOnce();  // the game is up, so its code is decrypted by now
    }
    UWorld* const world = (GWorld != nullptr) ? *GWorld : nullptr;
    log::Info("heartbeat #{}: {:.1f} fps, GWorld={}, objects={}, script hooks {}",
        ++gHeartbeats, gHeartbeatFrames / gHeartbeatElapsed, static_cast<void*>(world),
        UObject::GObjObjects->Count(), gHooked ? "live" : "waiting for script packages");
    if (gHooked) {
        engine::natives_dump::SaveIfRequested(*gInit);  // opt-in (MEVR_DUMP_NATIVES), once
        game::head_tracking::LogStats();
        game::reflections::LogStats();
        game::hand_aim::LogStats();
        game::weapon::LogStats();
        game::view_anchor::LogStats();
        game::arm_ik::LogStats();
        game::support_grip::LogStats();
        game::skeleton_probe::Tick();
    }
    engine::scene_view::LogStats();
    game::gamepad_bridge::LogStats();
    vr::output::LogStats();
    if (gLockstepWaits > 0) {
        log::Info("    lockstep: {} waits (game thread {}), avg {:.2f} ms, {} timeouts, {} drift recoveries",
            gLockstepWaits, GetCurrentThreadId(), gLockstepWaitMs / gLockstepWaits, gLockstepTimeouts, gDriftRecoveries);
    }
    gLockstepWaits = gLockstepTimeouts = gDriftRecoveries = 0;
    gLockstepWaitMs = 0.0;
}

} // namespace

MEVR_MOD_API bool MevrModStartup(ISharedProxyInterface* spi, mevr::HostServices const* host) {
    using namespace mevr;
    // Version 2 and up: fields are only appended, so read what both sides know. (The monolithic
    // build passes no logger: host and mod already share one log.)
    if (host != nullptr && host->version >= 2) {
        if (host->log != nullptr) {
            log::SetSink(host->log);  // separate DLL: log through the host's file
        }
        gHost = host;
        gHostOwnsVr = host->controllers != nullptr;
        // Nothing past `controllers` from a v3 host: v3 had two layouts (mod/ModApi.hpp).
        gHostView = gHostOwnsVr && host->version >= 4 && host->view != nullptr && host->setHandAim != nullptr;
        if (host->version >= 4) {
            engine::process_event::UseHost(host->setProcessEventCallback);
        }
    }
    log::Info("mod startup (built {} {}){}", __DATE__, __TIME__, gHostOwnsVr ? "; VR renderer: the host's (Halcyon)" : "");
    if (gHostOwnsVr && !gHostView) {
        log::Warn("the host ASI is older than this mod (services v{}): no view to aim with, so hand aim stays off; "
                  "close the game and redeploy the host", host->version);
    }
    if (gHost != nullptr && gHost->version < 5) {
        log::Warn("the host ASI is older than this mod (services v{}): it unloads the mod even when a hook couldn't be "
                  "removed; close the game and redeploy the host", gHost->version);
    }
    game::view_anchor::UseHostView(gHostOwnsVr);

    // Hook names get this prefix, so the host's and the mod's hooks never collide.
    gInit = std::make_unique<::LESDK::Initializer>(spi, SDK_TARGET_NAME_A "MEVRmod");
    if (!engine::ResolveGlobals(*gInit)) {
        gInit.reset();
        return false;
    }
    gActive = true;
    return true;
}

MEVR_MOD_API void MevrModTick(float deltaSeconds) {
    using namespace mevr;
    if (!gActive) {
        return;
    }
    if (gHostOwnsVr) {
        MirrorHostState();
        gGamepadHooked = true;  // the host's XInput hook feeds the controllers to the game
    }
    PollHotkeys();
    if (!gHooked || !gGamepadHooked) {
        gRetryTimer += deltaSeconds;
        if (gRetryTimer >= kStartupRetrySeconds) {
            gRetryTimer = 0.0;
            if (!gHooked) {
                TryInstallScriptHooks();
            }
            if (!gGamepadHooked && gActive) {
                gGamepadHooked = game::gamepad_bridge::TryInstall(*gInit);
            }
        }
    }
    if (gHooked) {
        game::head_tracking::RefreshGameMode();
    }
    if (gHostOwnsVr) {
        UpdateHostHandAim();
        UpdateHostArmIk();
    }
    if (gHooked && !gHostOwnsVr) {
        if (!gVrFeatures && vr::output::Running()) {
            SetVrFeatures(true);
        } else if (gVrFeatures && !vr::output::IsEnabled()) {
            SetVrFeatures(false);
        }
        // Cutscenes and conversations cut between cameras, and menus have no world to look
        // around in: show them on the virtual screen (the chosen mode returns afterwards).
        auto const mode = game::head_tracking::GameMode();
        bool const wanted = gAutoCinema && mode && engine::adapter::IsFlatScreenMode(*mode);
        gScreenWantedFor = wanted == gScreenWanted ? gScreenWantedFor + deltaSeconds : 0.0;
        gScreenWanted = wanted;
        if (gScreenShown != gScreenWanted && (gScreenWantedFor >= kScreenSwitchSeconds || !gAutoCinema)) {
            gScreenShown = gScreenWanted;
            vr::output::SetScreenOverride(gScreenShown);
        }
        game::reflections::Tick(deltaSeconds);
    }
    if (gHooked) {
        game::hand_targets::SetDesktopTest(NoHeadset());
        // The held gun first (arm IK aims it), then arm IK (uses this tick's view anchor, so before
        // OnTickEnd), then which way shots go and the reticle's ray.
        game::weapon::Tick(game::hand_aim::Enabled() || game::arm_ik::Enabled());
        game::arm_ik::Tick(deltaSeconds);
        game::support_grip::Tick(deltaSeconds, game::arm_ik::SupportRequested());
        PublishSupportGrip();
        game::hand_aim::Tick();
        PublishAimRay();
        game::head_tracking::OnTickEnd();
    }
    PublishStatus();
    WaitForThisFrameToPresent();

    gHeartbeatElapsed += deltaSeconds;
    ++gHeartbeatFrames;
    if (gHeartbeatElapsed >= kHeartbeatSeconds) {
        Heartbeat();
        gHeartbeatElapsed = 0.0;
        gHeartbeatFrames = 0;
    }
}

MEVR_MOD_API void MevrModOnPresent(IDXGISwapChain* swapChain) {
    if (gActive) {
        mevr::vr::output::OnPresent(swapChain);
    }
}

MEVR_MOD_API void MevrModShutdown() {
    using namespace mevr;
    gActive = false;
    // The process is exiting (v5 hosts say so): nothing waits for other threads, and the host
    // leaves the DLL mapped anyway.
    bool const exiting = gHost != nullptr && gHost->version >= 5 && gHost->processExiting != nullptr &&
        gHost->processExiting();
    // The first thing that leaves this DLL's code reachable after shutdown: the host must not
    // unload it then.
    char const* keepLoaded = nullptr;
    auto const removed = [&keepLoaded](bool ok, char const* what) {
        if (!ok && keepLoaded == nullptr) {
            keepLoaded = what;
        }
    };
    vr::output::SetScreenOverride(false);
    gScreenWanted = gScreenShown = false;
    gScreenWantedFor = 0.0;
    // The host has already detached the present callback, so the render thread is out.
    vr::output::Shutdown();
    if (gInit && gHooked) {
        removed(engine::process_event::Uninstall(*gInit), "the ProcessEvent hook couldn't be removed");
    }
    if (gInit && gSceneViewHooked) {
        removed(engine::scene_view::Uninstall(*gInit), "the CalcSceneView hook couldn't be removed");
    }
    if (gInit && gGamepadHooked) {
        removed(game::gamepad_bridge::Uninstall(*gInit), "an XInput hook couldn't be removed");
    }
    if (gInit && gCallFunctionHooked) {
        removed(engine::call_function::Uninstall(*gInit), "the CallFunction hook couldn't be removed");
    }
    game::arm_ik::Shutdown();  // stops the writes before the hook goes
    if (gInit && gBonePoseHooked) {
        removed(engine::bone_pose::Uninstall(*gInit, !exiting),
            "the UpdateSkelPose hook couldn't be removed, or a call was still inside it");
    }
    if (keepLoaded != nullptr && !exiting) {
        if (gHost != nullptr && gHost->version >= 5 && gHost->keepLoaded != nullptr) {
            gHost->keepLoaded(keepLoaded);  // the host pins this DLL instead of unloading it
        } else {
            log::Error("{}, but the host (services v{}) unloads the mod anyway: the game may crash; redeploy the host",
                keepLoaded, gHost != nullptr ? gHost->version : 0);
        }
    }
    gBonePoseTried = gBonePoseHooked = false;
    engine::call_function::ClearHandlers();
    gCallFunctionHooked = false;
    gHandAimAvailable = false;
    gVrFeatures = false;
    if (gHostView && gHandAimReported) {
        gHost->setHandAim(false);  // the host's head aim comes back (it also forgets on unload)
    }
    if (gAimRaySent && HostTakesAimRay()) {
        vr::aim_ray::Ray const none;
        gHost->setAimRay(&none);  // no reticle without the mod (the host also forgets on unload)
    }
    gAimRaySent = false;
    gToldNoAimRay = false;
    if (HostTakesSupportGrip()) {
        // No grip without the mod, LT back on the trigger (the host also forgets on unload).
        gHost->setSupportGrip(-1.0f, 0.12f, 0.22f, game::gamepad::kFlagLeftTrigger | game::gamepad::kFlagSupportAims);
    }
    gToldNoSupportGrip = false;
    game::support_grip::Shutdown();
    game::weapon::Shutdown();
    gHost = nullptr;
    gHostOwnsVr = false;
    gToldHostOwnsVr = false;
    gToldStateMismatch = false;
    engine::process_event::UseHost(nullptr);
    gHostView = false;
    gHostDataOk = false;
    gHeadsetSeen = false;
    game::hand_targets::SetDesktopTest(false);
    gHandAimChosen = false;
    gHandAimReported = false;
    gArmIkChosen = false;
    game::hand_aim::Shutdown();
    game::view_anchor::Shutdown();
    game::arm_ik::Shutdown();
    game::skeleton_probe::Shutdown();
    gHooked = false;
    gSceneViewHooked = false;
    gGamepadHooked = false;
    game::reflections::Shutdown();
    engine::process_event::ClearHandlers();
    game::head_tracking::Shutdown();
    gInit.reset();
    log::Info("mod shut down");
    log::SetSink(nullptr);
}
