// The hot-reloadable part of MEVR. Everything that changes often lives behind these entry
// points; the host ASI only owns the loader hookup, the engine tick and the Present hook.

#include "mod/ModApi.hpp"

#include "core/Hotkey.hpp"
#include "core/Log.hpp"
#include "core/vr/Output.hpp"
#include "core/vr/ViewSync.hpp"
#include "engine/CallFunction.hpp"
#include "engine/CodeDump.hpp"
#include "engine/Engine.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/SceneView.hpp"
#include "game/GamepadBridge.hpp"
#include "game/HandAim.hpp"
#include "game/HeadSim.hpp"
#include "game/HeadTracking.hpp"
#include "game/Reflections.hpp"
#include "game/SkeletonProbe.hpp"

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
bool gHooked = false;  // ProcessEvent hook live (after the script packages loaded)
bool gSceneViewHooked = false;
bool gGamepadHooked = false;  // XInput hooks (once the game has loaded an XInput DLL)
bool gCallFunctionHooked = false;

mevr::Hotkey gVrToggleKey{VK_OEM_6};    // ]  VR on / off
mevr::Hotkey gModeToggleKey{VK_OEM_4};  // [  head-tracked / screen
mevr::Hotkey gHeadPathKey{VK_OEM_5};    // \  head path: render / camera / off (A/B)
mevr::Hotkey gHeadSimKey{VK_OEM_7};     // '  desktop head simulator: sweep / nod / off
mevr::Hotkey gStereoKey{VK_OEM_1};      // ;  stereo: mono / alternate-eye (render path, headset)
mevr::Hotkey gPositionKey{VK_OEM_2};    // /  6DOF: head position moves the view (render path)
mevr::Hotkey gHandAimKey{VK_OEM_COMMA}; // ,  aim weapons with the right controller (on foot)

double gRetryTimer = kStartupRetrySeconds;  // first attempt on the first tick
double gHeartbeatElapsed = 0.0;
unsigned gHeartbeatFrames = 0;
unsigned gHeartbeats = 0;

// Lockstep: after each engine tick, wait until that frame is presented, so the head pose read
// for the next frame is predicted for the display time it will actually be shown at (vrframework
// guide 07). Pairing images with views doesn't depend on it (view_sync consumes one view per
// Present). The timeout only guards against a frame that never presents: it's long enough that
// an ordinary hitch never trips it, since a timed-out wait lets the game run a frame ahead.
constexpr std::uint32_t kLockstepTimeoutMs = 100;
// Several timeouts in a row mean a view whose frame never presented is stuck at the front of
// the queue (every image would get the previous frame's view): drop it.
constexpr unsigned kDriftTimeouts = 3;
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
    if (!vr::output::WantsHeadTracking() || !vr::output::ShowsFrames() || !vr::view_sync::HasPendingFrame()) {
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
    if (!gSceneViewHooked) {
        gSceneViewHooked = engine::scene_view::Install(*gInit, &game::head_tracking::OnBeforePrimaryView);
    }
    if (!game::head_tracking::TryStartup(gSceneViewHooked)) {
        return;
    }
    if (!game::reflections::TryStartup()) {
        log::Warn("reflections: SceneCaptureComponent.SetFrameRate not found; reflection fix off");
    }
    if (!game::skeleton_probe::TryStartup()) {
        log::Warn("skeleton probe: bone query functions not found");
    }
    bool const handAim = game::hand_aim::TryStartup();
    if (!handAim) {
        log::Warn("hand aim: BioPlayerController.GetAdjustedAimFor not found; hand aim off");
    }
    if (!engine::process_event::Install(*gInit)) {
        log::Error("ProcessEvent hook failed; mod inactive");
        gActive = false;
        return;
    }
    game::head_tracking::SetReady();
    gHooked = true;
    // Script-to-script calls (hand aim). Optional: everything else works without it.
    if (handAim) {
        gCallFunctionHooked = engine::call_function::Install(*gInit);
    }
}

void PollHotkeys() {
    using namespace mevr;
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
    if (gHandAimKey.Pressed()) {
        log::Info(",: hand aim {}", game::hand_aim::Toggle() ? "on" : "off");
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
    if (host != nullptr && host->version == kHostServicesVersion && host->log != nullptr) {
        log::SetSink(host->log);
    }
    log::Info("mod startup (built {} {})", __DATE__, __TIME__);

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
        game::reflections::Tick(deltaSeconds);
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
    game::hand_aim::Shutdown();
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
