// SPDX-License-Identifier: MIT
#pragma once

// The floor marker's Waits need on every game (the owner's yes of 2026-11-19, need (a); lane FPC5): a scene in first person that
// waits for the player at a level trigger. The level's touch events (Engine.SeqEvent_Touch and its games' subclasses, whose
// Originator is the trigger or volume) are gathered near where the scene wants Shepard; one is waited on (the core's
// cw::WaitingTrigger) when it is enabled and one-shot with a touch left, its trigger's collision contains where the scene wants him
// (Actor.PointCheckComponent, ME1's trigger stop's own check), and neither his pawn nor the player's feet stand in it. The caller
// shows the marker there (cw::StepMarker's Waits). Game thread; every engine call through ue3 (kEveryGame handles).

#include "ue3/Runtime.hpp"

#include <optional>
#include <string>

namespace mevr::game::scene_waits {

struct Wait {
    ue3::Vector at{};     // where the scene wants Shepard (his place: the caller puts it on the floor)
    std::string trigger;  // the trigger's name (logs)
};
// `wanted`: where the scene wants Shepard (his pawn's place, withheld); `shepard`: his pawn; `playerAt`: the pawn's place the player
// stands at (the pin, or the held view's feet at a pawn's height). Gathers the touch events within 30 m of `wanted` at most every
// 3 s (a walk over the object table), judges them at most every 0.25 s; nullopt when nothing waits.
std::optional<Wait> Find(ue3::Vector const& wanted, ue3::Object* shepard, ue3::Vector const& playerAt);
// The scene ended (or a level changed): the gathered events are dropped.
void Forget();
// Gathers and judgements so far (status lines).
std::string Status();

} // namespace mevr::game::scene_waits
