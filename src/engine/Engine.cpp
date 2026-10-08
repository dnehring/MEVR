#include "engine/Engine.hpp"

#include "core/Log.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

namespace mevr::engine {
namespace {

using tGameEngineTick = void(UGameEngine* self, float deltaSeconds);
tGameEngineTick* gGameEngineTickOrig = nullptr;

// M0 heartbeat: proves the tick hook runs, and that GWorld/GObjects read sanely.
constexpr double kHeartbeatSeconds = 5.0;
double gElapsed = 0.0;
unsigned gFrames = 0;
unsigned gHeartbeats = 0;

void GameEngineTickHook(UGameEngine* self, float deltaSeconds) {
    gGameEngineTickOrig(self, deltaSeconds);

    gElapsed += deltaSeconds;
    ++gFrames;
    if (gElapsed < kHeartbeatSeconds) {
        return;
    }
    UWorld* const world = (GWorld != nullptr) ? *GWorld : nullptr;
    log::Info("heartbeat #{}: {} frames in {:.2f}s ({:.1f} fps), GWorld={}, objects={}",
        ++gHeartbeats, gFrames, gElapsed, gFrames / gElapsed,
        static_cast<void*>(world), UObject::GObjObjects->Count());
    gElapsed = 0.0;
    gFrames = 0;
}

template <typename T>
bool Resolve(::LESDK::Initializer& init, ::LESDK::Address address, T*& out, char const* name) {
    out = init.ResolveTyped<T>(address);
    if (out == nullptr) {
        log::Error("failed to resolve {}", name);
        return false;
    }
    log::Info("resolved {} at {}", name, reinterpret_cast<void const*>(out));
    return true;
}

} // namespace

bool Initialize(::LESDK::Initializer& init) {
    bool ok = true;
    ok &= Resolve(init, BUILTIN_GMALLOC_RVA, GMalloc, "GMalloc");
    ok &= Resolve(init, BUILTIN_GOBJECTS_RVA, UObject::GObjObjects, "GObjObjects");
    ok &= Resolve(init, BUILTIN_SFXNAMEPOOLS_RVA, SFXName::GBioNamePools, "SFXName pools");
    ok &= Resolve(init, BUILTIN_SFXNAMEINIT_RVA, SFXName::GInitMethod, "SFXName init");
    ok &= Resolve(init, BUILTIN_GENGINE_RVA, GEngine, "GEngine");
    ok &= Resolve(init, BUILTIN_GWORLD_RVA, GWorld, "GWorld");

    tGameEngineTick* tickTarget = nullptr;
    ok &= Resolve(init, BUILTIN_GAMEENGINETICK_RVA, tickTarget, "UGameEngine::Tick");
    if (!ok) {
        return false;
    }

    gGameEngineTickOrig = reinterpret_cast<tGameEngineTick*>(init.InstallHook("UGameEngine::Tick",
        reinterpret_cast<void*>(tickTarget), reinterpret_cast<void*>(&GameEngineTickHook)));
    if (gGameEngineTickOrig == nullptr) {
        log::Error("failed to hook UGameEngine::Tick");
        return false;
    }
    log::Info("hooked UGameEngine::Tick");
    return true;
}

} // namespace mevr::engine
