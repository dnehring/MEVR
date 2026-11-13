// SPDX-License-Identifier: MIT
#include "core/vr/CalibrationWeapon.hpp"

#include <cstdio>
#include <limits>

namespace {
int failures = 0;
void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/CalibrationWeaponTests.cpp line %d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)
bool Near(float a, float b) { return std::abs(a - b) < 0.00001f; }
}

int RunCalibrationWeaponTests() {
    namespace w = mevr::vr::calibration_weapon;
    namespace c = mevr::vr::calibration;
    w::Model model;
    auto const pistol = w::Build(model);
    CHECK(pistol.size() == 36);
    for (auto const& face : pistol) for (auto p : face.points) {
        CHECK(w::Finite(p));
        CHECK(p.y <= 0.091f); // no front or rear sight above the flat slide
    }
    XrPosef eye{{0, 0, 0, 1}, {0, 0, 0}};
    model.pose = {c::YawRotation(0.4f), {0.08f, -0.1f, -0.7f}};
    auto const world = w::Build(model);
    auto const projected = w::Project(world, eye);
    CHECK(!projected.empty());
    float previous = 1000;
    for (auto const& poly : projected) {
        CHECK(poly.depth <= previous);
        previous = poly.depth;
        CHECK(poly.points.size() >= 3);
        for (auto p : poly.points) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1);
    }
    auto leftEye = eye;
    leftEye.position.x = -0.032f;
    auto rightEye = eye;
    rightEye.position.x = 0.032f;
    auto const l = w::Project(world, leftEye);
    auto const r = w::Project(world, rightEye);
    CHECK(l.size() == r.size() && !l.empty());
    CHECK(l.front().points.front().x > r.front().points.front().x); // correct stereo disparity
    auto plane = w::CanvasPose(rightEye);
    CHECK(Near(plane.position.x, 0.032f) && Near(plane.position.z, -w::kPlaneM));
    // Reproject the canvas back through the same eye: matches the original world ray.
    w::Face face;
    face.points = {{{0.1f, 0.04f, -0.5f}, {0.12f, 0.04f, -0.5f},
        {0.12f, 0.06f, -0.5f}, {0.1f, 0.06f, -0.5f}}};
    face.colour = 0xFFFFFFFFu;
    std::vector<w::Face> patch{face};
    auto pp = w::Project(patch, rightEye);
    CHECK(pp.size() == 1);
    patch[0].colour = 0x40FFFFFFu;
    CHECK((w::Project(patch, rightEye).front().colour >> 24) == 0x40u); // ghost reference opacity survives projection
    auto uv = pp.front().points.front();
    float const onPlaneX = (uv.x - 0.5f) * 2 * w::kHalfSizeM;
    float const onPlaneY = (0.5f - uv.y) * 2 * w::kHalfSizeM;
    CHECK(Near(onPlaneX / w::kPlaneM, (0.1f - rightEye.position.x) / 0.5f));
    CHECK(Near(onPlaneY / w::kPlaneM, 0.04f / 0.5f));
    // A pistol crossing the eye clips rather than projecting through zero or exploding over the display.
    model.pose = {{0, 0, 0, 1}, {0, 0, -0.08f}};
    auto clipped = w::Project(w::Build(model), eye);
    CHECK(!clipped.empty());
    for (auto const& poly : clipped) for (auto p : poly.points) {
        CHECK(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1);
    }
    model.pose.position.z = 2;
    CHECK(w::Project(w::Build(model), eye).empty());
    model.pose.position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(w::Build(model).empty());
    return failures;
}
