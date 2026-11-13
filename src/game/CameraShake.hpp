// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The game's camera shakes in VR (research/71 sections 2-3, 2026-11-12, players' reports: "the camera shakes when I fire",
// "sprinting puts the run's jog on my view"). LE1 shakes the view two ways, and in VR both move the world around the player:
//
//  1. Screen shakes (SFXCameraModifier_ScreenShake on the player camera): sine oscillations of the POV's rotation, place and
//     FOV. Each gun's stats object carries two (SFXWeaponStatsBase.FiringShake, TightAimFiringShake, played on every shot);
//     the Mako has its own (BioVehicleWheeled.JumpShake, BoostShake). `[Comfort] screenShake` (0..1, default 0) scales their
//     amplitudes (TheShake.RotAmplitude, LocAmplitude, FOVAmplitude) at the source while VR is on: the held weapon's two
//     and the Mako's two, each original kept and put back when the setting goes to 1, VR goes off or the mod unloads.
//  2. Camera shakes from the body (BioCameraShake: types Run, Rifle, Pistol, Sniper, Shotgun, FireLoop, Grenade, Biotics;
//     driven by the player's AnimTree BioAnimNodeCameraShake nodes CameraShake1-3), which a camera mode applies only while
//     its SFXCameraMode.bIsCameraShakeEnabled is set. `[Comfort] viewShake = 0` (default) clears it on the first-person
//     on-foot modes (as game/CameraCollision clears their collision), journalled, put back with `viewShake = 1`, a view that
//     isn't head-tracked, or the unload. [verify] which of the two carries the run's bob and how much each shot's shake is.
// LE1 only. Log prefix `camera shake:`; snapshot `"mod"."cameraShake"`; console `mevr shake [screen <0..1> | view on|off]`.
namespace mevr::game::camera_shake {

bool TryStartup();
void Tick();
// `mevr shake [screen <0..1> | view on|off | status]` (game/ConsoleRequest): this session only.
std::string Command(std::string const& args);
// Snapshot JSON ("cameraShake").
std::string StatusJson();
void Shutdown();

} // namespace mevr::game::camera_shake
