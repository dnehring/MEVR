// SPDX-License-Identifier: MIT
// Life-size stereo on the sequels (research/30 "The sequels"): the shared core's containment rule, and what Halcyon's ME2
// and ME3 upstream stereo puts in front of the eyes, with the same pair model research/30 measured ME1's with.

#include "core/vr/MetricStereo.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {

int gFails = 0;

void Check(bool ok, char const* what) {
    if (!ok) {
        std::printf("FAIL metric stereo (sequels): %s\n", what);
        ++gFails;
    }
}

constexpr float kDeg = 3.14159265f / 180.0f;

} // namespace

int RunMetricStereoSequelTests() {
    namespace ms = mevr::vr::metric_stereo;
    gFails = 0;

    // The containment rule: a PS VR2-like pair (each eye wider outward than inward) against ME2's aspect-derived window
    // (the vertical half angle of the left eye, the horizontal from a 2048 x 2304 eye: tan 1.18, 49.7 degrees) and against
    // the eyes' union (ME1's window).
    ms::Fov const left{-61.5f * kDeg, 40.0f * kDeg, 53.0f * kDeg, -53.0f * kDeg};
    ms::Fov const right{-40.0f * kDeg, 61.5f * kDeg, 53.0f * kDeg, -53.0f * kDeg};
    float const aspectWindowH = std::atan(std::tan(53.0f * kDeg) * (2048.0f / 2304.0f));
    Check(!ms::WindowHolds(aspectWindowH, 53.0f * kDeg, left, 1.0f, 1.0f), "the aspect window misses the left eye's outer edge");
    Check(!ms::WindowHolds(aspectWindowH, 53.0f * kDeg, right, 1.0f, 1.0f), "the aspect window misses the right eye's outer edge");
    ms::Fov const window = ms::UnionWindow(left, right, 1.0f, 1.0f);
    Check(ms::WindowHolds(window.right, window.up, left, 1.0f, 1.0f), "the union window holds the left eye");
    Check(ms::WindowHolds(window.right, window.up, right, 1.0f, 1.0f), "the union window holds the right eye");
    // A fill wider than the window's is not held; one narrower is.
    Check(!ms::WindowHolds(window.right, window.up, left, 1.1f, 1.0f), "a wider fill is not held");
    Check(ms::WindowHolds(window.right, window.up, left, 0.9f, 0.9f), "a narrower fill is held");
    // A symmetric headset (the null driver's) in a window of its own size: held (the tolerance takes rounding).
    ms::Fov const sym{-50.0f * kDeg, 50.0f * kDeg, 45.0f * kDeg, -45.0f * kDeg};
    Check(ms::WindowHolds(50.0f * kDeg, 45.0f * kDeg, sym, 1.0f, 1.0f), "a symmetric eye in its own window");
    Check(!ms::WindowHolds(49.0f * kDeg, 45.0f * kDeg, sym, 1.0f, 1.0f), "a window a degree short");
    Check(!ms::WindowHolds(0.0f, 0.0f, sym, 1.0f, 1.0f), "no window holds nothing");

    // Upstream's pairs, a point straight ahead (the viewer's eyes 6.3 cm apart, both sequels at 100 units per metre: their
    // lean's 50 times PosScale 2).
    double const ipd = 0.063;
    float const tanH = std::tan(aspectWindowH);
    // ME2 (MELE2VR.ini as deployed: HalfEyeUU 3.35, Convergence 0.0197, SwapEyes 1): the shift's sign stays with the
    // pass, so with swap on it pushes the eyes apart: far scenery needs them to diverge.
    ms::RenderedPair const me2 = ms::HalcyonPair(3.35f, 100.0f, 0.0197f, std::atan(tanH), true, 0.0f);
    Check(ms::PerceivedDepth(me2, 1.0e6, ipd) < 0.0, "ME2 upstream: far scenery beyond infinity (divergent)");
    Check(ms::PerceivedDepth(me2, 2.0, ipd) < 0.0, "ME2 upstream: 2 m already beyond infinity");
    double const me2Near = ms::PerceivedDepth(me2, 0.5, ipd);
    Check(me2Near > 0.55 && me2Near < 2.0, "ME2 upstream: a pistol at 0.5 m reads farther");
    // ME3 (its code defaults: HalfEyeUU 1.6, SfrConvergence 0.03, SwapEyes 0): the shift crosses the eyes, so infinity
    // lands near; the half-size eye distance flattens everything onto it.
    ms::RenderedPair const me3 = ms::HalcyonPair(1.6f, 100.0f, 0.03f, std::atan(tanH), false, 0.0f);
    double const me3Far = ms::PerceivedDepth(me3, 1.0e6, ipd);
    Check(me3Far > 0.5 && me3Far < 1.5, "ME3 upstream: far scenery at about a metre");
    double const me3Mid = ms::PerceivedDepth(me3, 10.0, ipd);
    Check(me3Mid > 0.5 && me3Mid < me3Far, "ME3 upstream: 10 m nearer still");
    // Life-size: the real eyes, no shift: every depth reads true.
    ms::RenderedPair const metric = ms::MetricPair(static_cast<float>(ipd));
    for (double const d : {0.3, 0.5, 1.0, 2.0, 10.0}) {
        Check(std::fabs(ms::PerceivedDepth(metric, d, ipd) - d) < 1e-3 * d, "life-size reads the true depth");
        Check(std::fabs(ms::DisparityErrorArcmin(metric, d, ipd)) < 0.01, "life-size: no disparity error");
    }
    Check(std::isinf(ms::PerceivedDepth(metric, 1.0e9, ipd)) || ms::PerceivedDepth(metric, 1.0e9, ipd) > 1.0e6,
          "life-size: far scenery at infinity");

    // BodyOffsets with head look off (the sequels' vehicles, a locked cine): the eyes' head-relative offsets along the
    // camera's own axes, a horizontal baseline whatever the head's yaw.
    ms::EyeInputs in;
    float const yaw = 30.0f * kDeg;   // the head turned 30 degrees left about +y
    float const s = std::sin(0.5f * yaw), c = std::cos(0.5f * yaw);
    in.orientation[0] = in.orientation[1] = {0.0f, s, 0.0f, c};
    in.viewOrientation = {0.0f, s, 0.0f, c};
    // The eyes 6.3 cm apart along the turned head's right.
    float const rx = std::cos(yaw), rz = std::sin(yaw);   // the head's right in app space after a left turn
    in.position[0] = {-0.0315f * rx, 1.6f, 0.0315f * rz};
    in.position[1] = {0.0315f * rx, 1.6f, -0.0315f * rz};
    in.unitsPerMeter = 100.0f;
    in.viewFollowsHead = false;
    ms::EyeOffsets const off = ms::BodyOffsets(in);
    Check(std::fabs(off.body[0].x + 3.15f) < 0.02f && std::fabs(off.body[1].x - 3.15f) < 0.02f,
          "head look off: the eyes along the camera's right");
    Check(std::fabs(off.body[0].z) < 0.02f && std::fabs(off.body[1].z) < 0.02f, "head look off: no fore-and-aft offset");
    in.viewFollowsHead = true;
    ms::EyeOffsets const on = ms::BodyOffsets(in);
    Check(std::fabs(on.body[1].z - on.body[0].z) > 2.0f, "head look on: the eyes' real places, turned with the head");
    Check(std::fabs(on.ipdUU - 6.3f) < 0.01f, "the eye distance in units at 100 per metre");

    return gFails;
}
