// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/AimRay.hpp"
#include "ue3/Types.hpp"

#include <optional>
#include <string>

// The Mako in VR (research/44; LE1, game mode 1, the host's renderer; [Mako] in ASI\LE1MEVR.ini):
// - The turret aims where the right controller points. Each tick the controller's aim ray (or a saved pistol
//   calibration's line) is mapped into the world through the view anchor and traced (Actor.Trace on the vehicle,
//   which it ignores) for the aim point. Shots: game/hand_aim asks ShotRotation inside its
//   BioPlayerController.GetAdjustedAimFor handler (the vehicle's cannon and machine gun are BioWeaponRanged on the
//   Mako and ask the same function), and gets the direction from the shot's own start (the vehicle's
//   GetWeaponStartTraceLocation, seen just before) toward that point. The turret model: the game's own native
//   BioVehicleBehaviorBase.SetDesiredTurretAimPoint(turret, point) through ProcessEvent, for every turret the vehicle's
//   weapons sit on, from the mod's tick (after the world tick) and/or a post-handler of the vehicle's own Tick (inside
//   it); the turret's BioSkelControl_TurretConstrained controls are read back each tick to tell whether the game
//   re-aimed after us (the log and the snapshot say so).
// - The aim ray for the host's hand reticle (the host shows it in the Mako only on a valid ray from the mod).
// - The vehicle's motion for the comfort vignette (core/vr/Comfort VehicleMotion; game/comfort reads Motion()).
// Everything through our reflection layer; a lookup that fails stands the part down with a `mako:` line. Head aim stays
// Halcyon's business (its makoHeadAim is off in our deploy): the head drives the view only. Game thread.
namespace mevr::game::mako {

struct Link {
    bool headTrackedView = false;  // the host shows a head-tracked world view
    bool mainMenuOpen = false;     // our VR main menu (the host hides the controllers then)
};

// Registers the ProcessEvent post-handlers (the vehicle's Tick and its shot-start query); call before the hooks go
// in. False until the script packages are loaded.
bool TryStartup();
// Each mod tick, before game/comfort (which reads Motion()) and before game/hand_aim's tick.
void Tick(double deltaSeconds, Link const& link);

// The vehicle's motion this tick, for the vignette: in the Mako with [Mako] vignette on.
struct Motion {
    bool inVehicle = false;  // the player drives a vehicle (game mode 1 with a BioVehicleBase pawn)
    bool wanted = false;     // ...and [Mako] vignette = 1
    bool moving = false;
    bool turning = false;
};
Motion CurrentMotion();

// For game/hand_aim's GetAdjustedAimFor handler: the shot's rotation (unwound next to `game`) while the controller
// aims the turret; false otherwise (the game's aim stays).
bool ShotRotation(ue3::Rotator const& game, ue3::Rotator& out);
// For game/hand_aim's AimRay(): the controller's ray in tracking space while it aims the turret and [Mako] reticle
// is on; nullopt otherwise.
std::optional<vr::aim_ray::Ray> AimRay();

std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::mako
