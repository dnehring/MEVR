// SPDX-License-Identifier: MIT
// Offline tests for the body's scale (core/ik/BodyScale, research/58): the factor from the two eye heights, the scale about
// the floor and its inverse (a goal mapped through the inverse lands on itself after the scale), off near 1, the approach.

#include "core/ik/BodyScale.hpp"

#include <cmath>
#include <cstdio>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/BodyScaleTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace bs = mevr::ik::body_scale;
using mevr::ik::Vec3;

bool Near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}
bool Near(Vec3 a, Vec3 b, double tolerance) {
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

} // namespace

int RunBodyScaleTests() {
    // The user measured (2026-11-05): the view 1.84 m above the floor, Shepard's eyes 1.69: 1.089. Units at 100 per metre.
    auto const f = bs::Factor(184.0, 169.0, 100.0);
    CHECK(f && Near(*f, 184.0 / 169.0, 1e-9));
    CHECK(bs::Factor(250.0, 150.0, 100.0) == bs::kMax);  // clamped
    CHECK(bs::Factor(120.0, 169.0, 100.0) == bs::kMin);
    CHECK(!bs::Factor(30.0, 169.0, 100.0) && !bs::Factor(184.0, 0.0, 100.0) && !bs::Factor(NAN, 169.0, 100.0));
    // A bad floor reading (2026-11-06, the owner's QA: Shepard's eyes "50.3 m" and the view "39.5 m" above it took the scale to
    // kMin for good): no factor, and neither height counts.
    CHECK(!bs::Factor(3948.3, 5031.8, 100.0) && !bs::Factor(183.2, 5031.8, 100.0) && !bs::Factor(493.4, 167.5, 100.0));
    CHECK(bs::Plausible(183.2, 100.0) && !bs::Plausible(3948.3, 100.0) && !bs::Plausible(40.0, 100.0));

    // The scale about a floor point under the view: the floor stays, the eyes rise by s, the head stays over the pivot.
    bs::Scale scale{1.1, {10.0, 5.0, 3.7}};
    CHECK(scale.Active());
    Vec3 const toe{20.0, 0.0, 3.7};
    Vec3 const eye{10.0, 5.0, 172.0};
    CHECK(Near(bs::Apply(scale, toe).z, 3.7, 1e-9) && Near(bs::Apply(scale, toe).x, 21.0, 1e-9));
    CHECK(Near(bs::Apply(scale, eye), Vec3{10.0, 5.0, 3.7 + (172.0 - 3.7) * 1.1}, 1e-9));
    // A goal through the inverse lands on itself after the scale (the hands on the controllers).
    Vec3 const goal{45.0, -20.0, 140.0};
    CHECK(Near(bs::Apply(scale, bs::Unscale(scale, goal)), goal, 1e-9));
    CHECK(Near(bs::UnscaleVector(scale, {11.0, 0.0, -2.2}), Vec3{10.0, 0.0, -2.0}, 1e-9));
    CHECK(Near(bs::UnscaleLength(scale, 55.0), 50.0, 1e-9));
    // A bone's own scale multiplies.
    mevr::ik::Transform bone;
    bone.translation = eye;
    bone.scale = 1.0;
    mevr::ik::Transform const scaled = bs::Apply(scale, bone);
    CHECK(Near(scaled.scale, 1.1, 1e-12) && Near(scaled.translation, bs::Apply(scale, eye), 1e-12));
    // Off near 1: nothing moves.
    bs::Scale const off{1.001, {0.0, 0.0, 0.0}};
    CHECK(!off.Active() && Near(bs::Apply(off, goal), goal, 0.0) && Near(bs::Unscale(off, goal), goal, 0.0));
    // A head far from the pivot (Shepard's mark as a scene opens, the view metres away): scaled about the floor under itself it
    // keeps its place and only its height grows; about the pivot it moved off by a share of the distance (the council hearing's
    // floating view, 2026-11-13).
    bs::Scale const tall{1.185, {0.0, 0.0, -90.0}};   // the pivot under the view, on the pawn's floor
    Vec3 const markHead{-330.0, 70.0, 146.0};          // 3.3 m away on a podium 0.7 m up: 1.66 m above its own floor (-20)
    Vec3 const standing = bs::StandingAt(1.185, markHead, -20.0);
    CHECK(Near(standing, Vec3{-330.0, 70.0, -20.0 + 166.0 * 1.185}, 1e-9));
    Vec3 const old = bs::Apply(tall, markHead);       // the old point: 0.6 m further away and 0.13 m higher still
    CHECK(old.x < -390.0 && old.z > standing.z + 10.0);
    CHECK(Near(bs::StandingAt(1.0, markHead, -20.0), markHead, 1e-12));
    // The approach: at most the rate, never past the target.
    CHECK(Near(bs::Approach(1.0, 1.1, 0.05, 1.0), 1.05, 1e-12));
    CHECK(Near(bs::Approach(1.0, 1.02, 0.05, 1.0), 1.02, 1e-12));
    CHECK(Near(bs::Approach(1.1, 1.0, 0.05, 0.5), 1.075, 1e-12));
    return gFailures;
}
