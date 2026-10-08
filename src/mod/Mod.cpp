// The hot-reloadable part of MEVR. Everything that changes often lives behind these entry
// points; the host ASI only owns the loader hookup, the engine tick and the Present hook.

#include "mod/ModApi.hpp"

#include "core/Hotkey.hpp"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/Output.hpp"
#include "core/vr/ViewSync.hpp"
#include "engine/CallFunction.hpp"
#include "engine/CodeDump.hpp"
#include "engine/Engine.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/SceneView.hpp"
#include "game/ArmIk.hpp"
#include "game/GamepadBridge.hpp"
#include "game/HandAim.hpp"
#include "game/HeadSim.hpp"
#include "game/HeadTracking.hpp"
#include "game/Reflections.hpp"
#include "game/SkeletonProbe.hpp"
#include "game/ViewAnchor.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

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
// Host renderer (HostServices v3): it publishes its view (game/view_anchor maps the controllers
// into the world with it) and takes our word on hand aim (its head aim stops while we aim).
bool gHostView = false;
// Host renderer: hand aim comes on by itself the first time a motion controller aims, unless `,`
// has decided it this session.
bool gHandAimChosen = false;
bool gHandAimReported = false;  // what the host was last told
bool gHooked = false;  // ProcessEvent hook live (after the script packages loaded)
bool gSceneViewHooked = false;
bool gGamepadHooked = false;  // XInput hooks (once the game has loaded an XInput DLL)
bool gCallFunctionHooked = false;  // installed the first time hand aim is turned on
bool gHandAimAvailable = false;
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
mevr::Hotkey gArmIkKey{VK_OEM_PERIOD};  // .  VRIK arms: the engine's limb IK follows the controllers
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

void SetVrFeatures(bool on) {
    using namespace mevr;
    gVrFeatures = on;
    log::Info("VR features {}", on ? "on: stereo, 6DOF, hand aim, arm IK (; / , . switch each)" : "off");
    game::head_tracking::SetStereo(on ? game::head_tracking::Stereo::AlternateEye : game::head_tracking::Stereo::Mono);
    game::head_tracking::SetPositional(on);
    game::arm_ik::SetEnabled(on);
    if (gHandAimAvailable) {
        if (on) {
            EnsureCallFunctionHook();
        }
        game::hand_aim::SetEnabled(on);
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
    // Tell the host only what actually works: without the CallFunction hook, shots fired from
    // script keep the game's aim, so the host's head aim must stay.
    bool const aiming = game::hand_aim::Enabled() && gCallFunctionHooked;
    if (aiming != gHandAimReported) {
        gHandAimReported = aiming;
        gHost->setHandAim(aiming);
    }
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
        for (Hotkey* key : {&gVrToggleKey, &gModeToggleKey, &gHeadPathKey, &gHeadSimKey, &gStereoKey, &gPositionKey,
                 &gArmIkKey, &gCinemaKey, &gLockstepKey}) {
            if (key->Pressed() && !gToldHostOwnsVr) {
                gToldHostOwnsVr = true;
                log::Info("Halcyon's renderer is active: our VR keys are off except , (hand aim); use its Insert menu");
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
        log::Info(".: arm IK {}", game::arm_ik::Toggle() ? "on" : "off");
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
        game::head_tracking::LogStats();
        game::reflections::LogStats();
        game::hand_aim::LogStats();
        game::view_anchor::LogStats();
        game::arm_ik::LogStats();
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
    // Version 2 and up: fields are only appended, so read what both sides know.
    if (host != nullptr && host->version >= 2 && host->log != nullptr) {
        log::SetSink(host->log);
        gHost = host;
        gHostOwnsVr = host->controllers != nullptr;
        gHostView = gHostOwnsVr && host->version >= 3 && host->view != nullptr && host->setHandAim != nullptr;
    }
    log::Info("mod startup (built {} {}){}", __DATE__, __TIME__, gHostOwnsVr ? "; VR renderer: the host's (Halcyon)" : "");
    if (gHostOwnsVr && !gHostView) {
        log::Warn("the host ASI is older than this mod (services v{}): no view to aim with, so hand aim stays off; "
                  "close the game and redeploy the host", host->version);
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
    PollHotkeys();
    if (gHostOwnsVr) {
        vr::controllers::State controllers;
        gHost->controllers(&controllers);
        vr::controllers::Mirror(controllers);  // keeps the host's timestamp, so it still ages
        gGamepadHooked = true;  // the host's XInput hook feeds the controllers to the game
        if (gHostView) {
            vr::host_view::State view;
            gHost->view(&view);
            vr::host_view::Mirror(view);
        }
    }
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
        game::arm_ik::Tick(deltaSeconds);  // uses this tick's view anchor, so before OnTickEnd
        game::head_tracking::OnTickEnd();
    }
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
    vr::output::SetScreenOverride(false);
    gScreenWanted = gScreenShown = false;
    gScreenWantedFor = 0.0;
    // The host has already detached the present callback, so the render thread is out.
    vr::output::Shutdown();
    if (gInit && gHooked) {
        engine::process_event::Uninstall(*gInit);
    }
    if (gInit && gSceneViewHooked) {
        engine::scene_view::Uninstall(*gInit);
    }
    if (gInit && gGamepadHooked) {
        game::gamepad_bridge::Uninstall(*gInit);
    }
    if (gInit && gCallFunctionHooked) {
        engine::call_function::Uninstall(*gInit);
    }
    engine::call_function::ClearHandlers();
    gCallFunctionHooked = false;
    gHandAimAvailable = false;
    gVrFeatures = false;
    if (gHostView && gHandAimReported) {
        gHost->setHandAim(false);  // the host's head aim comes back (it also forgets on unload)
    }
    gHost = nullptr;
    gHostOwnsVr = false;
    gToldHostOwnsVr = false;
    gHostView = false;
    gHandAimChosen = false;
    gHandAimReported = false;
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
