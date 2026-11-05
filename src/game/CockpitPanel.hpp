// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/CockpitPanel.hpp"
#include "game/GamepadMapping.hpp"

#include <string>

namespace mevr::ue3 {
struct Object;
}

// The Mako cockpit's repair button and extraction lever (research/48 section 27; LE1). Riding with the cockpit up and its
// `panel` layout (tools/cockpit/place.py writes it from the hull's manifest), the hands work the models: a fingertip pushes
// the button's cap, holding it fills the plate's squares and then calls the game's own repair; a grip pulls the overhead
// handle out, and held at the end it calls the game's own extraction (SFXGameModeVehicle.RepairVehicle and .LeaveWorld, on
// the vehicle mode the player controller holds, as Y and X do on a pad). The ring's lamps show the hull's health. The rules
// are core/vr/CockpitPanel's (unit-tested); game/CockpitProbe moves the models and swaps the lamps' instances. Log `cockpit
// panel:`; snapshot "mod"."cockpitPanel".
namespace mevr::game::cockpit_panel {

struct Link {
    bool mainMenuOpen = false;  // our menu or the calibration: the hands are theirs
    void (*hapticPulse)(int hand, float seconds, float amplitude) = nullptr;
};

// Each mod tick from game/mako, in the Mako or not (out of it everything lets go). Game thread.
void Tick(double deltaSeconds, Link const& link);
// A squeeze at the handle is the handle's: never the cannon's RB or the boost's LB.
void AdjustPad(gamepad::Options& options);
// Where the models are (game/CockpitProbe moves the cap and the handle by it).
vr::cockpit_panel::Output const& Current();
// Snapshot JSON.
std::string StatusJson();
// `mevr cockpit panel [repair|extract|ask]`: the state, or (for tests) one of the two calls as the controls would make it, or
// what a press would show now (the VehicleRepair power's answer: no omni-gel, cooling down, or none).
std::string Command(std::string const& rest);

}  // namespace mevr::game::cockpit_panel
