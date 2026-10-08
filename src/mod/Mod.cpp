// The hot-reloadable part of MEVR. Everything that changes often lives behind these entry
// points; the host ASI only owns the loader hookup, the engine tick and the Present hook.

#include "mod/ModApi.hpp"

#include "core/Hotkey.hpp"
#include "core/Log.hpp"
#include "core/vr/Output.hpp"
#include "core/vr/ViewSync.hpp"
#include "engine/Engine.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/HeadTracking.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace {

constexpr double kStartupRetrySeconds = 1.0;
constexpr double kHeartbeatSeconds = 5.0;

std::unique_ptr<::LESDK::Initializer> gInit;
std::atomic<bool> gActive{false};  // read on the render thread too
bool gHooked = false;  // ProcessEvent hook live (after the script packages loaded)

mevr::Hotkey gVrToggleKey{VK_OEM_6};    // ]  VR on / off
mevr::Hotkey gModeToggleKey{VK_OEM_4};  // [  head-tracked / screen
mevr::Hotkey gCameraAbKey{VK_OEM_5};    // \  head rotation applied to the game camera (A/B)

double gRetryTimer = kStartupRetrySeconds;  // first attempt on the first tick
double gHeartbeatElapsed = 0.0;
unsigned gHeartbeatFrames = 0;
unsigned gHeartbeats = 0;

// Lockstep: after each engine tick, wait until that frame is presented so the next frame's
// camera pairs with exactly one image (vrframework guide 07). The timeout keeps a frame that
// never presents (loading, minimized) from stalling the game.
constexpr std::uint32_t kLockstepTimeoutMs = 30;
unsigned gLockstepWaits = 0;
unsigned gLockstepTimeouts = 0;
double gLockstepWaitMs = 0.0;

void WaitForThisFrameToPresent() {
    using namespace mevr;
    if (!vr::output::WantsHeadTracking() || !vr::view_sync::HasPendingFrame()) {
        return;
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
    if (!presented) {
        ++gLockstepTimeouts;
    }
}

// Script-level hooks need Engine.u/SFXGame.u loaded, which happens after attach. Handlers are
// registered before the ProcessEvent hook goes live so the table never changes under it.
void TryInstallScriptHooks() {
    using namespace mevr;
    if (!game::head_tracking::TryStartup()) {
        return;
    }
    if (!engine::process_event::Install(*gInit)) {
        log::Error("ProcessEvent hook failed; mod inactive");
        gActive = false;
        return;
    }
    gHooked = true;
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
    if (gCameraAbKey.Pressed()) {
        bool const apply = !game::head_tracking::AppliesToCamera();
        game::head_tracking::SetApplyToCamera(apply);
        log::Info("\\: head rotation on game camera {}", apply ? "ON" : "OFF");
    }
}

void Heartbeat() {
    using namespace mevr;
    UWorld* const world = (GWorld != nullptr) ? *GWorld : nullptr;
    log::Info("heartbeat #{}: {:.1f} fps, GWorld={}, objects={}, script hooks {}",
        ++gHeartbeats, gHeartbeatFrames / gHeartbeatElapsed, static_cast<void*>(world),
        UObject::GObjObjects->Count(), gHooked ? "live" : "waiting for script packages");
    if (gHooked) {
        game::head_tracking::LogStats();
    }
    vr::output::LogStats();
    if (gLockstepWaits > 0) {
        log::Info("    lockstep: {} waits (game thread {}), avg {:.2f} ms, {} timeouts",
            gLockstepWaits, GetCurrentThreadId(), gLockstepWaitMs / gLockstepWaits, gLockstepTimeouts);
    }
    gLockstepWaits = gLockstepTimeouts = 0;
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
    if (!gHooked) {
        gRetryTimer += deltaSeconds;
        if (gRetryTimer >= kStartupRetrySeconds) {
            gRetryTimer = 0.0;
            TryInstallScriptHooks();
        }
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
    gHooked = false;
    engine::process_event::ClearHandlers();
    game::head_tracking::Shutdown();
    gInit.reset();
    log::Info("mod shut down");
    log::SetSink(nullptr);
}
