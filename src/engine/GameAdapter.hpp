#pragma once

// Per-game spellings of the same engine data (docs/trilogy-design.md: one system, small
// adapters). Each LExSDKv2 game target generates slightly different names for identical fields;
// everything game-specific in field access goes through here, so the rest of the mod compiles
// unchanged for LE1, LE2 and LE3.

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

// The game-mode manager (EGameModes: 0 = on-foot gameplay in all three games).
inline USFXGameModeManager* GameModes(ABioPlayerController* controller) {
#if defined(SDK_TARGET_LE1)
    return controller->GameModeManager;
#else
    return controller->GameModeManager2;
#endif
}

// Game modes better shown on the flat virtual screen than head-tracked: conversations,
// cinematics, full-screen GUI, movies, the galaxy map and similar (the cameras cut, or there's no
// world to look around in). On-foot play, vehicles and the power/weapon/command wheels stay
// head-tracked. SFXGameModeManager.EGameModes differs per game.
inline bool IsFlatScreenMode(unsigned char mode) {
#if defined(SDK_TARGET_LE1)
    return mode >= 5 && mode <= 10;  // Conversation, Cinematic, GUI, Movie, Galaxy, Minigame
#elif defined(SDK_TARGET_LE2)
    return mode >= 5 && mode <= 10;  // Conversation, Cinematic, GUI, Movie, Galaxy, Orbital
#else
    return mode >= 7 && mode <= 12;  // Conversation, Cinematic, GUI, Movie, Galaxy, Orbital
#endif
}

} // namespace mevr::engine::adapter
