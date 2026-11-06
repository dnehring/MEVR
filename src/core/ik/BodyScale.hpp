// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"
#include "core/ik/Rigid.hpp"

#include <optional>

// Shepard drawn at the player's size (research/58): every bone but the camera's scaled by `s` about a point on the floor
// under the view, after the solves; the solves' goals mapped through the inverse first, so they land where they were meant
// to. Engine-agnostic; unit-tested.
namespace mevr::ik::body_scale {

// Off within this of 1 (no write for nothing).
inline constexpr double kOffWithin = 0.002;
inline constexpr double kMin = 0.85;
inline constexpr double kMax = 1.25;

struct Scale {
    double s = 1.0;
    Vec3 pivot;  // component space: under the view, at the floor
    bool Active() const;
};

// p after the scale (the pose's points).
Vec3 Apply(Scale const& scale, Vec3 p);
// The goal a solve must aim at so that after the scale it lands on `p`.
Vec3 Unscale(Scale const& scale, Vec3 p);
// A displacement, or a length, likewise.
Vec3 UnscaleVector(Scale const& scale, Vec3 v);
double UnscaleLength(Scale const& scale, double length);
// Every transform scaled (its translation about the pivot, its own scale multiplied).
Transform Apply(Scale const& scale, Transform t);

// The factor that puts Shepard's eyes where the view is: the view's standing height above the floor over Shepard's eye height
// (both measured standing, unscaled, the same units), clamped to kMin..kMax; nullopt for heights that can't be (below 0.5 m
// or not finite: no measurement yet).
std::optional<double> Factor(double viewEyeHeight, double shepardEyeHeight, double unitsPerMeter);

// Moves `current` toward `target` by at most `ratePerSecond` x dt (a change of size never pops).
double Approach(double current, double target, double ratePerSecond, double dt);

} // namespace mevr::ik::body_scale
