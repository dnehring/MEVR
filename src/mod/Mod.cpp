// The hot-reloadable part of MEVR. Everything that changes often lives behind these three
// entry points; the host ASI only owns the loader hookup and the engine tick.

#include "mod/ModApi.hpp"

#include "core/Log.hpp"
#include "engine/Engine.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/CameraTest.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <memory>

namespace {

constexpr double kStartupRetrySeconds = 1.0;
constexpr double kHeartbeatSeconds = 5.0;

std::unique_ptr<::LESDK::Initializer> gInit;
bool gActive = false;
bool gHooked = false;  // ProcessEvent hook live (after the script packages loaded)

double gRetryTimer = kStartupRetrySeconds;  // first attempt on the first tick
double gHeartbeatElapsed = 0.0;
unsigned gHeartbeatFrames = 0;
unsigned gHeartbeats = 0;

// Script-level hooks need Engine.u/SFXGame.u loaded, which happens after attach. Handlers are
// registered before the ProcessEvent hook goes live so the table never changes under it.
void TryInstallScriptHooks() {
    using namespace mevr;
    if (!game::camera_test::TryStartup()) {
        return;
    }
    if (!engine::process_event::Install(*gInit)) {
        log::Error("ProcessEvent hook failed; mod inactive");
        gActive = false;
        return;
    }
    gHooked = true;
}

void Heartbeat() {
    using namespace mevr;
    UWorld* const world = (GWorld != nullptr) ? *GWorld : nullptr;
    log::Info("heartbeat #{}: {:.1f} fps, GWorld={}, objects={}, script hooks {}",
        ++gHeartbeats, gHeartbeatFrames / gHeartbeatElapsed, static_cast<void*>(world),
        UObject::GObjObjects->Count(), gHooked ? "live" : "waiting for script packages");
    if (gHooked) {
        game::camera_test::LogStats();
    }
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
    if (!gHooked) {
        gRetryTimer += deltaSeconds;
        if (gRetryTimer >= kStartupRetrySeconds) {
            gRetryTimer = 0.0;
            TryInstallScriptHooks();
        }
    } else {
        game::camera_test::Tick(deltaSeconds);
    }

    gHeartbeatElapsed += deltaSeconds;
    ++gHeartbeatFrames;
    if (gHeartbeatElapsed >= kHeartbeatSeconds) {
        Heartbeat();
        gHeartbeatElapsed = 0.0;
        gHeartbeatFrames = 0;
    }
}

MEVR_MOD_API void MevrModShutdown() {
    using namespace mevr;
    gActive = false;
    if (gInit && gHooked) {
        engine::process_event::Uninstall(*gInit);
    }
    gHooked = false;
    engine::process_event::ClearHandlers();
    game::camera_test::Shutdown();
    gInit.reset();
    log::Info("mod shut down");
    log::SetSink(nullptr);
}
