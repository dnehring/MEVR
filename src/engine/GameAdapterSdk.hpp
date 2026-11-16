// SPDX-License-Identifier: MIT
#pragma once

// The LExSDKv2-typed per-game accessors, for the LE2 diagnostic host's observers (engine/Le2DiagnosticObservation: the
// only code left that reads the engine through LExSDKv2's types; T04, docs/trilogy-plan.md). Each LExSDKv2 game target
// generates slightly different names for identical fields; our layer looks names up case-insensitively, so they
// disappear there. Never include this from a module a players' build compiles.

#include "engine/GameAdapter.hpp"

#include <LESDK/Headers.hpp>

namespace mevr::engine::adapter {

// GetPlayerViewPoint's rotation out-parameter (LE3's generator lower-cased it).
inline FRotator& ViewPointRotation(AController_eventGetPlayerViewPoint_Parms& parms) {
#if defined(SDK_TARGET_LE3)
    return parms.out_rotation;
#else
    return parms.out_Rotation;
#endif
}

// An actor's world location (LE3's generator lower-cased it).
inline FVector const& Location(AActor const* actor) {
#if defined(SDK_TARGET_LE3)
    return actor->location;
#else
    return actor->Location;
#endif
}

// A camera point of view's location (LE3's generator lower-cased it).
inline FVector const& Location(FTPOV const& pov) {
#if defined(SDK_TARGET_LE3)
    return pov.location;
#else
    return pov.Location;
#endif
}

// The game-mode manager (EGameModes: 0 = on-foot gameplay in all three games).
inline USFXGameModeManager* GameModes(ABioPlayerController* controller) {
#if defined(SDK_TARGET_LE1)
    return controller->GameModeManager;
#else
    return controller->GameModeManager2;
#endif
}

} // namespace mevr::engine::adapter
