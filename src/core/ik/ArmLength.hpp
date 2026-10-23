// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"

#include <optional>

// The player's arm length from one T-pose sample (both arms straight out to the sides), for
// scaling reach so the player's full extension is Shepard's full extension. Tracking space
// (OpenXR, meters); the arm is measured from the shoulder to the controller's grip point.
// Engine-agnostic; tested offline.
namespace mevr::ik {

struct ArmLengthSample {
    Vec3 head;   // the headset
    Vec3 left;   // the left grip
    Vec3 right;  // the right grip
};

struct ArmLengths {
    double left = 0.0;
    double right = 0.0;
    double mean = 0.0;
};

// The shoulders sit on the line between the grips, `shoulderHalfWidth` either side of the point
// on it nearest the head (in a T pose the arms run along the shoulder line, so the head's height
// above it and its distance ahead of it don't matter). Each arm is its grip's distance from its
// shoulder. Nullopt for a sample that isn't a T pose: grips closer than the shoulders, the head
// beside the shoulder line rather than between the hands, or a result outside 0.3-1.2 m.
std::optional<ArmLengths> ArmLengthFromTPose(ArmLengthSample const& sample, double shoulderHalfWidth = 0.18);

// How much of the arm-length calibration's reach scaling a hand gets (0..1): none within `nearMeters`
// of the eyes (the hand sits exactly on its controller there: a gun at the cheek lines up with the eye
// the player looks through), all of it from `nearMeters + fadeMeters` out, smoothly between. A
// `nearMeters` of 0 (or not finite) turns it off: always 1.
double ReachWeight(double meters, double nearMeters, double fadeMeters = 0.15);
// The reach scale to use: 1 + (full - 1) * weight.
double ScaledReach(double full, double weight);

} // namespace mevr::ik
