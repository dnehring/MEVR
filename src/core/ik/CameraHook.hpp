// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// LE1's player camera hangs off a bone of the player's own body mesh (research/18): the camera
// mode's GetCameraLocation (0xCB2240) reads the bone named by its HookName from SpaceBases, as
// the pose currently sits there. Exploration, combat, crouch and tight aim hook the bone named
// `Camera` (a child of the root, `God`); a sniper crouch mode hooks `God`. A pose write that moves
// either moves the view, the loop commit 489ef51 caused. Engine-agnostic; tested offline.
namespace mevr::ik {

// The bones a pose write must leave as the animation had them: those named `Camera` or `God`
// (case-insensitive), in skeleton order.
std::vector<int> FindCameraHookBones(std::span<std::string const> names);

// Of `bones`, those that are one of `roots` or hang below one (a write of those subtrees may move
// them). Out-of-range indices and broken parent chains count as outside.
std::vector<int> InSubtrees(std::span<int const> parents, std::span<int const> roots, std::span<int const> bones);

// Where GetCameraLocation puts the camera for a BioPawn target: the hook point (the hook bone,
// world) plus HookOffset turned by the view's yaw alone (CalculateOffsetHook, distance 1), minus
// Offset turned by the view's pitch and yaw (roll 0). UE3 axes (FRotationMatrix rows); radians.
Vec3 CameraFromHook(Vec3 hookBone, Vec3 hookOffset, Vec3 offset, double pitch, double yaw);

// A world vector in a view's level axes for that yaw (radians): (forward, right, up).
Vec3 InViewAxes(Vec3 v, double yaw);

// What the camera-mode log compares between ticks (game/camera_probe): the active mode and the
// fields that place the camera.
struct CameraModeKey {
    std::uintptr_t mode = 0;  // the mode object's address (identity only)
    std::uint64_t hookName = 0;
    Vec3 hookOffset;
    Vec3 offset;
};
// Another mode or hook name, or HookOffset or Offset moved more than `tolerance` (units).
bool CameraModeChanged(CameraModeKey const& a, CameraModeKey const& b, double tolerance = 0.5);

} // namespace mevr::ik
