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

// The hand bone's rotation that points a held object's barrel along `aim` and keeps the object's
// up as close to `up` as the barrel allows (a gun follows a controller's aim ray, upright with
// the controller). `barrelInHand` and `upInHand` are the object's barrel and up directions in the
// hand bone's own axes (fixed while it's held); `aim` and `up` say where they should point, in
// the space the result is in (e.g. component space). Any lengths. Nullopt if a direction is zero
// or its up is within a few degrees of its barrel.
std::optional<Quat> HandForBarrel(Vec3 barrelInHand, Vec3 upInHand, Vec3 aim, Vec3 up);

// Which way is "up" for a gun held in a hand, in the hand bone's own axes: the hand's thumb side
// (anatomical +Y, from AnatomyInHand) made perpendicular to the barrel, then rolled about the
// barrel by `rollDegrees` (positive rolls the top to the right, seen from behind the gun). A hand
// holds every gun with the top of its grip toward the thumb and index finger, so this doesn't
// depend on how a weapon model's axes happen to be authored (they disagree between LE1's weapons).
// With HandForBarrel, the thumb side then follows the controller's up: the gun's roll follows the
// wrist. Nullopt if the barrel is (nearly) along the thumb side or zero.
std::optional<Vec3> GunUpInHand(Vec3 barrelInHand, Quat anatomyInHand, double rollDegrees = 0.0);

} // namespace mevr::ik
