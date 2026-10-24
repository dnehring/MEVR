// SPDX-License-Identifier: MIT
#include "engine/Engine.hpp"

#if defined(MEVR_GAME_LE1)

#include "core/Log.hpp"
#include "ue3/Runtime.hpp"

#include <atomic>
#include <optional>

namespace mevr::engine {
namespace {

// The path from the GameEngine to the world, frozen by ResolveWorld (game thread) before anything reads it.
struct WorldPath {
    ue3::Object* engine = nullptr;
    std::uint32_t gamePlayers = 0;  // Engine.Engine.GamePlayers (TArray<LocalPlayer*>)
    std::uint32_t actor = 0;        // Engine.Player.Actor (the player controller)
    std::uint32_t worldInfo = 0;    // Engine.Actor.WorldInfo
    ue3::Class* world = nullptr;    // Engine.World
};
WorldPath gPath;
std::atomic<bool> gResolved{false};
bool gToldMissing = false;

void const* Find(char const** how) {
    if (!gResolved.load(std::memory_order_acquire) || !ue3::runtime::Ready()) {
        *how = "not resolved";
        return nullptr;
    }
    using ue3::Reflection;
    auto const players = Reflection::Read<ue3::TArray<ue3::Object*>>(gPath.engine, gPath.gamePlayers);
    ue3::Object* const player = players.Valid(0) ? players.data[0] : nullptr;
    ue3::Object* const controller = player != nullptr ? Reflection::Read<ue3::Object*>(player, gPath.actor) : nullptr;
    ue3::Object* const worldInfo = controller != nullptr ? Reflection::Read<ue3::Object*>(controller, gPath.worldInfo) : nullptr;
    if (worldInfo == nullptr) {
        *how = "no player controller";
        return nullptr;
    }
    // An actor's outer is its level, the level's is the world (GWorld).
    if (ue3::Object* const world = ue3::runtime::Get().OuterOfA(worldInfo, gPath.world)) {
        *how = "WorldInfo's outer World";
        return world;
    }
    *how = "WorldInfo (no World outer)";
    return worldInfo;
}

} // namespace

bool ResolveWorld() {
    if (gResolved.load(std::memory_order_acquire)) {
        return true;
    }
    if (!ue3::runtime::Ready()) {
        return false;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const engine = ue3::runtime::GameEngine();
    std::optional<ue3::PropertyInfo> const players =
        engine != nullptr ? r.PropertyOf(r.ClassOf(engine), "GamePlayers") : std::nullopt;
    std::optional<ue3::PropertyInfo> const actor = r.PropertyOf(r.FindClass("Engine.Player"), "Actor");
    std::optional<ue3::PropertyInfo> const worldInfo = r.PropertyOf(r.FindClass("Engine.Actor"), "WorldInfo");
    ue3::Class* const world = r.FindClass("Engine.World");
    ue3::runtime::FlushNotes();
    char const* missing = engine == nullptr                                         ? "the GameEngine"
        : !players || players->kind != ue3::Kind::Array                             ? "Engine.GamePlayers"
        : !actor || actor->elementSize != static_cast<std::int32_t>(sizeof(void*))  ? "Engine.Player.Actor"
        : !worldInfo || worldInfo->elementSize != static_cast<std::int32_t>(sizeof(void*)) ? "Engine.Actor.WorldInfo"
        : world == nullptr                                                          ? "Engine.World"
                                                                                    : nullptr;
    if (missing != nullptr) {
        if (!gToldMissing) {
            gToldMissing = true;
            log::Warn("world: {} not found: no world identity (arm IK's writes and the weapon's caches go by the mesh's "
                      "owner and the request's age only)", missing);
        }
        return false;
    }
    gPath = {engine, static_cast<std::uint32_t>(players->offset), static_cast<std::uint32_t>(actor->offset),
        static_cast<std::uint32_t>(worldInfo->offset), world};
    gResolved.store(true, std::memory_order_release);
    return true;
}

void ForgetWorld() {
    gResolved.store(false, std::memory_order_release);
    gPath = {};
    gToldMissing = false;
}

void const* World() {
    char const* how = nullptr;
    return Find(&how);
}

char const* WorldHow() {
    char const* how = nullptr;
    Find(&how);
    return how;
}

std::int32_t ObjectCount() {
    return ue3::runtime::Ready() ? ue3::runtime::Get().ObjectCount() : 0;
}

} // namespace mevr::engine

#else  // LE2/LE3: no reflection layout yet (research/34, stage 5)

#include "engine/Legacy.hpp"

namespace mevr::engine {

bool ResolveWorld() {
    return false;
}

void ForgetWorld() {}

void const* World() {
    return legacy::World();
}

char const* WorldHow() {
    return "GWorld (LExSDKv2: LE2/LE3)";
}

std::int32_t ObjectCount() {
    return legacy::ObjectCount();
}

} // namespace mevr::engine

#endif
