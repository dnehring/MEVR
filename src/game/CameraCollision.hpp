// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The game's camera collision in first person (research/69 "The camera that fell to Shepard's knees", 2026-11-11). Each of
// LE1's camera modes may pull the camera from where it wants it back toward its hook when geometry lies between them
// (SFXCameraMode.bCollisionEnabled; SFXPlayerCamera.UpdateCameraMode then calls DoCameraCollision). Halcyon's first person
// puts the explore and combat modes' camera 0.5 m ahead of the Camera bone at head height, so standing close to a wall or
// under a low obstacle pulled the camera to the bone, 0.26 m below the pawn's centre, and Halcyon drew the eye 0.43 m behind
// that: the view behind Shepard's knees, looking at his legs (measured: 0.62 m above the floor at a spot, 1.79 m 1.5 m away).
// In VR on foot the camera is the headset's eye, which may stand where the player puts it; so while the view is
// head-tracked on foot, the current mode (and an interpolation's target) of the classes Halcyon puts in first person
// (Explore, ExploreStorm, Combat, CombatStorm, TightAim; never Vehicle, Sniper or HipAim) has its collision cleared,
// journalled, and put back when the view stops being head-tracked or the mod unloads. `[Game] cameraCollision = 1` keeps
// the game's collision. LE1 only. Log prefix `camera collision:`.
namespace mevr::game::camera_collision {

bool TryStartup();
void Tick();
// Snapshot JSON ("cameraCollision").
std::string StatusJson();
void Shutdown();

} // namespace mevr::game::camera_collision
