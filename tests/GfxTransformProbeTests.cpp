// SPDX-License-Identifier: MIT
#include "core/vr/GfxTransformProbe.hpp"
#include <cstdio>
#include <limits>

int RunGfxTransformProbeTests() {
    namespace p = mevr::vr::gfx_transform_probe;
    int failures = 0;
    auto check = [&](char const* name, p::Result got, p::Result want) {
        if (got != want) { ++failures; std::printf("FAIL GFx transform %s: %s\n", name, p::Name(got)); }
    };
    std::array<float, 16> const identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    check("non-display wrapper", p::Check(false, 0, 0, identity), p::Result::NotDisplayObject);
    auto m = identity;
    m[12] = -621.45f; m[13] = 642.2f; m[0] = m[5] = 1.2316f;
    check("consistent translated scaled clip", p::Check(true, -621.45f, 642.2f, m), p::Result::Consistent);
    m[12] = m[13] = -619.0f;
    check("native duplicate-X observation", p::Check(true, -621.45f, 642.2f, m), p::Result::PositionMismatch);
    m = identity; m[0] = 1.62e32f; m[15] = 0;
    check("native finite garbage observation", p::Check(true, 0, 0, m), p::Result::OutOfRange);
    m = identity; m[15] = 0;
    check("missing homogeneous W", p::Check(true, 0, 0, m), p::Result::NonAffine);
    m = identity; m[0] = std::numeric_limits<float>::quiet_NaN();
    check("NaN", p::Check(true, 0, 0, m), p::Result::NonFinite);
    m = identity; m[14] = 10;
    check("3D parent", p::Check(true, 0, 0, m), p::Result::NonPlanar);
    m = identity; m[0] = 0;
    check("collapsed clip", p::Check(true, 0, 0, m), p::Result::Degenerate);
    m = identity; m[0] = 0; m[1] = 1; m[4] = -1; m[5] = 0;
    check("rotated clip", p::Check(true, 0, 0, m), p::Result::Consistent);
    return failures;
}
