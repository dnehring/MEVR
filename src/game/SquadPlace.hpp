// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadPlace.hpp"
#include "game/GamepadMapping.hpp"

#include <string>

// Squad placement by an arc, like a teleport (core/vr/SquadPlace; the user, 2026-11-05). On foot in VR, hold the right B: an
// arc leaves the right hand's aim and is traced through the world (Actor.Trace, level geometry only, segment by segment), a
// ring marks where it lands on floor, and a selector of tiles stands above the hand ([first squadmate | Squad | second], ME1's
// d-pad order; it starts on the squad), stepped by a sideways flick of the right stick (one pulse for the first, two for the
// second, a long one for the squad; the stick doesn't turn meanwhile). Let go: the selected squadmates go to the ring and hold
// it until called back (squad_orders::GiveAt plus each one's AI walked there). B double-tapped calls the whole squad back to
// Shepard. Landing within followWithinM of Shepard is a follow; a wall, a steep slope or nothing in reach calls it off. Off while
// B is the game's melee button (meleeButton = 1), in the squad's or the power wheel, or anywhere but on foot. `[SquadOrders]
// place`, `placeSpeedMS`, `placeRangeM`, `placeMinHoldS`. The host draws it (HostServices v32 setSquadPlace,
// plugin/SquadPlaceDraw). `mevr place aim <pitchDeg> [yawDeg] | select left|right | release | double` without controllers.
namespace mevr::game::squad_place {

struct Link {
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
    void (*publish)(vr::squad_place::View const* view) = nullptr;  // null: no arc drawn (an older host)
};

// Game thread, each tick after the squad orders'.
void Tick(double deltaSeconds, Link const& link);
// B is held: the arc and the selector are up (game/comfort stops turning from the right stick).
bool Holding();
// The right stick is the selector's while B is held (gamepad::Options::squadPlace: kept from the game).
void AdjustPad(gamepad::Options& options);
// The console's `mevr place ...` (game/ConsoleRequest): what happened, in words.
std::string Console(std::string_view args);

std::string StatusJson();
void LogStats();
void Shutdown();

}  // namespace mevr::game::squad_place
