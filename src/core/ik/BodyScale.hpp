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
// A standing eye height in metres that can be (2026-11-06, the owner's QA: a floor read 50 m below Shepard took the scale to
// kMin and kept it there; "when I do the recenter, I'm still much taller than the in-game avatar").
inline constexpr double kMinEyeM = 0.5;
inline constexpr double kMaxEyeM = 2.5;

struct Scale {
    double s = 1.0;
    Vec3 pivot;  // component space: under the view, at the floor
    bool Active() const;
};

// p after the scale (the pose's points).
Vec3 Apply(Scale const& scale, Vec3 p);
// A point of a body standing on the floor at floorZ (world or component units), scaled by s about that floor directly under
// it: its place kept, its height above that floor times s. Where a scaled body standing there would have it. Not Apply's
// point: Apply's pivot is under the view on the pawn's own floor, and a head metres from the view and above that floor
// (Shepard's mark on the council's podium as the hearing opens) moved 18.5% of both at 1.185, up and away: the player's view
// 0.4-1.2 m too high (2026-11-13; research/69 "The view that floated").
Vec3 StandingAt(double s, Vec3 p, double floorZ);
// The goal a solve must aim at so that after the scale it lands on `p`.
Vec3 Unscale(Scale const& scale, Vec3 p);
// A displacement, or a length, likewise.
Vec3 UnscaleVector(Scale const& scale, Vec3 v);
double UnscaleLength(Scale const& scale, double length);
// Every transform scaled (its translation about the pivot, its own scale multiplied).
Transform Apply(Scale const& scale, Transform t);

// The factor that puts Shepard's eyes where the view is: the view's standing height above the floor over Shepard's eye height
// (both measured standing, unscaled, the same units), clamped to kMin..kMax; nullopt for heights that can't be (outside
// kMinEyeM..kMaxEyeM or not finite: no measurement yet, or a bad floor reading).
// True for an eye height (component units) a standing person can have: kMinEyeM..kMaxEyeM.
bool Plausible(double eyeHeight, double unitsPerMeter);
std::optional<double> Factor(double viewEyeHeight, double shepardEyeHeight, double unitsPerMeter);

// Moves `current` toward `target` by at most `ratePerSecond` x dt (a change of size never pops).
double Approach(double current, double target, double ratePerSecond, double dt);

} // namespace mevr::ik::body_scale
