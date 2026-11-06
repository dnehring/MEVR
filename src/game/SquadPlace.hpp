// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadPlace.hpp"

#include <string>

// Squad placement by an arc, like a teleport (core/vr/SquadPlace; the user, 2026-11-05). On foot in VR, hold the right B: an
// arc leaves the right hand's aim and is traced through the world (Actor.Trace, level geometry only, segment by segment), a
// ring marks where it lands on floor; let go and the squad moves there through the one dispatcher
// (squad_orders::GiveAt: a move to the point, with the acknowledgement and standing bookkeeping). The right hand's straight
// ray on a squadmate when B goes down orders that one alone; landing within followWithinM of Shepard is a follow; a wall,
// a steep slope or nothing in reach calls it off (two buzzes). Off while B is the game's melee button (meleeButton = 1), in the
// squad's or the power wheel, or anywhere but on foot. `[SquadOrders] place`, `placeSpeedMS`, `placeRangeM`, `placeMinHoldS`.
// The host draws it (HostServices v32 setSquadPlace, plugin/SquadPlaceDraw). `mevr place [aim <pitchDeg> | release]` holds B
// from the console for tests without controllers.
namespace mevr::game::squad_place {

struct Link {
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
    void (*publish)(vr::squad_place::View const* view) = nullptr;  // null: no arc drawn (an older host)
};

// Game thread, each tick after the squad orders'.
void Tick(double deltaSeconds, Link const& link);
// The console's `mevr place ...` (game/ConsoleRequest): what happened, in words.
std::string Console(std::string_view args);

std::string StatusJson();
void LogStats();
void Shutdown();

}  // namespace mevr::game::squad_place
