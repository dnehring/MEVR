#pragma once

#include "core/ik/Rigid.hpp"

#include <optional>

// Turning a motion controller's grip into a hand bone rotation, the same way every time.
//
// A hand's anatomical frame: X along the fingers (wrist to the middle knuckle), Y toward the thumb
// side (pinky knuckle to index knuckle, made perpendicular to X), Z = X cross Y. A skeleton gives it
// from its finger base bones; a grip gives it from how a hand holds a controller: the fingers point
// where the fist points (OpenXR grip -Z) and the thumb side is up (grip +Y), for both hands.
// Engine-agnostic; tested offline.
namespace mevr::ik {

// The rotation taking the anatomical axes into the hand bone's own axes, from the finger base
// positions in the hand bone's local space. None if the fingers don't span a frame.
std::optional<Quat> AnatomyInHand(Vec3 middleLocal, Vec3 indexLocal, Vec3 pinkLocal);

// A trim in the grip's own axes (X forward, Y right, Z up, as UE axes): yaw about Z, then pitch
// about Y, then roll about X. Degrees; positive pitch tilts the fingers down, positive yaw turns
// them right, positive roll turns the thumb side to the right.
Quat GripTrim(double pitchDegrees, double yawDegrees, double rollDegrees);

// The hand bone's rotation for a grip rotation (its X axis = where the fist points, Z = up),
// expressed in the same space as `grip`.
Quat HandFromGrip(Quat grip, Quat trim, Quat anatomyInHand);

} // namespace mevr::ik
