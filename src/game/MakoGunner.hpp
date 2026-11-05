// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Controllers.hpp"
#include "core/vr/MakoGunner.hpp"
#include "engine/XrToUnreal.hpp"

#include <optional>
#include <string>

namespace mevr::ue3 {
struct Object;
}

// The Mako's gunner (research/47 "The gunner"; rules in core/vr/MakoGunner): riding in the cockpit, a squadmate aims the
// turret at the nearest hostile it can see and fires: the machine gun in bursts (RT's FireWeapon on the vehicle's game mode),
// the cannon at big targets (RB's FireCannon). The player's thumbstick on the stick, or the free right hand aiming (finger on or
// pulling the trigger, pointing out the front), takes the gun at once; the gunner's again [Mako] gunnerHandbackS later.
// Candidates: WorldInfo.PawnList's BioPawns the driver's IsHostile names, not dead, within gunnerRangeM and seen from the
// muzzle (Actor.FastTrace), re-read 4 times a second.
// Game thread; every call through our reflection layer. Logs `mako gunner:`; snapshot "mod"."makoGunner".
namespace mevr::game::mako_gunner {

struct Inputs {
    ue3::Object* vehicle = nullptr;      // the driven BioVehicleBase
    ue3::Object* behavior = nullptr;     // its m_oBehavior
    ue3::Object* controller = nullptr;   // the player's controller
    bool riding = false;                 // the cockpit rides (the gunner works only there)
    vr::controllers::State const* controllers = nullptr;
    bool stickHeld = false;              // the right hand holds the cockpit's stick
    std::optional<engine::TrackingFrame> frame;  // to tell where the free hand points
};

struct Result {
    vr::mako_gunner::Control control = vr::mako_gunner::Control::Hand;
    bool gunner = false;                 // the gunner aims this tick
    bool holdHand = false;               // the hand's aim ended: hold where it left the turret until the gunner's back
    std::optional<engine::UnrealVector> target;  // the gunner's target (its middle), if it has one
    std::string what;
};

Result Tick(double deltaSeconds, Inputs const& inputs);
// `mevr gunner wake <n>` (tests): a dormant pawn shown and given collision (SetHidden false, SetCollision), so it can be hit.
std::string Wake(ue3::Object* pawn);
// Out of the Mako, or the mod stops: the triggers let go, the state forgotten.
void Stop(char const* why);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::mako_gunner
