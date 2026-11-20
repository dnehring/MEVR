// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

// Whether the world is being drawn just now (lane FPC-ME3, 2026-11-20): Shepard's body drawn within the last moments of wall
// time, read from its mesh's PrimitiveComponent.LastRenderTime moving. During a loading screen no scene is drawn and the
// world's own clock may stand still, so the moves are timed on the wall clock, never on WorldInfo.TimeSeconds. ME3's rapid
// transit loads in game mode 0 behind its loading picture; a console `scale set` then (the depth of field back after a
// conversation) left the load spinning for good (gotchas/me2-scale-set-during-load, ME3 rows). Callers that send `scale set`
// lines ask Now() first. Every game; game thread.
namespace mevr::game::world_drawn {

// Once a game tick (Mod.cpp, every game): notes whether the player's body was drawn since the last tick.
void Tick();
// The world drawn just now: the player's body drawn within `withinMs` of wall time (false before the first move is seen).
bool Now(std::uint64_t withinMs = 500);

} // namespace mevr::game::world_drawn
