#pragma once

#include "core/vr/AimRay.hpp"

#include <string>

// Aim with the right controller, on foot (the Mako turret keeps the game's aim). Two paths, by
// rewriting BioPlayerController.GetAdjustedAimFor's result (the weapon asks it for the aim of
// every shot):
//   - Along the gun barrel (LE1): while arm IK points the held gun along the right controller's
//     aim ray (game/arm_ik, game/weapon), shots go along the gun's barrel, and their start moves
//     to its muzzle (Pawn.GetWeaponStartTraceLocation's answer rewritten). Shots, the gun model and
//     the reticle (the aim ray below) then agree, whatever the hand calibration.
//   - Along the controller's aim ray, otherwise (no weapon or muzzle resolved, arm IK off, the grip
//     not measured yet): the direction only; shots still start where the game starts them (the log
//     reports how far that is from the ray once).
// The controller comes into the world through game/view_anchor, so either works with our renderer
// and with the host's (Halcyon's). Without a view anchor or a tracked controller the game's aim
// stays (fail closed). Our renderer: on with each VR session. Host renderer: on once motion
// controllers are seen. `,` toggles either way.
namespace mevr::game::hand_aim {

// Finds the functions and registers the call_function and process_event post-handlers (script
// calls GetAdjustedAimFor from script and natively). False until the script packages have loaded;
// call before either hook is installed.
bool TryStartup();
bool Toggle();
void SetEnabled(bool on);
bool Enabled();
// Each mod tick, after arm IK: logs which path shots take when it changes.
void Tick();
// The gun's aim ray for the reticle: the muzzle and barrel, in tracking space through
// game/view_anchor's frame (engine::ToTracking). `valid` false unless shots follow the barrel and
// there's a view anchor.
vr::aim_ray::Ray AimRay();
void LogStats();
// For snapshots: the last shot (research/21 section 10): converged on the sight line or along the barrel, its
// angle to the sight line and the barrel, what the sight line hit, its start against the muzzle.
std::string StatusJson();
void Shutdown();

} // namespace mevr::game::hand_aim
