// SPDX-License-Identifier: MIT
// Offline tests for lens flares in life-size stereo's mode 2 (core/vr/FlareWindow, research/30 section 9.6).
//
// The model is ME3's reflection pass (0x26B780) as read in Ghidra: the source projected through the CPU's window (the
// view's ViewProjectionMatrix), an element on the screen line through the centre, turned back into a world point through
// the view's InvTranslatedViewProjectionMatrix at clip depth 0.1, then drawn through the eye's own translated
// view-projection. With the PS VR2's left eye (lane FXR's replay, research/30 section 9.4) the bug puts a lamp seen at
// x 1233 of 3072 at x 934, the symmetric window's prediction (FXR measured the flare at 920).

#include "core/vr/FlareWindow.hpp"

#include <cmath>
#include <cstdio>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/FlareWindowTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace fw = mevr::vr::flare_window;
namespace sm = mevr::vr::screen_matrix;
using fw::Mat4;

// A rotation about Z by `yaw`, then about the new Y by `pitch` (row vectors), and UE3's view basis change (world X
// forward, Y right, Z up -> view Z forward, X right, Y up): tests/ScreenMatrixTests.cpp's helpers.
Mat4 Rotation(float yaw, float pitch) {
    float const cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    Mat4 const rz{cy, sy, 0, 0, -sy, cy, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Mat4 const ry{cp, 0, -sp, 0, 0, 1, 0, 0, sp, 0, cp, 0, 0, 0, 0, 1};
    return sm::Multiply(rz, ry);
}
Mat4 ViewBasis() {
    return {0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1};
}

// An off-centre perspective projection with UE3's layout (tangents of the frustum's edges; near plane n, infinite far).
Mat4 Projection(float left, float right, float down, float up, float n) {
    float const a = 2.0f / (right - left), b = 2.0f / (up - down);
    float const c = -(right + left) / (right - left), d = -(up + down) / (up - down);
    return {a, 0, 0, 0, 0, b, 0, 0, c, d, 1, 1, 0, 0, -n, 0};
}

// A translated-space point through a view-projection: normalised device x, y (w divided).
bool Project(Mat4 const& m, float const p[3], float* x, float* y) {
    float o[4] = {};
    float const v[4] = {p[0], p[1], p[2], 1.0f};
    for (int c = 0; c < 4; ++c) {
        for (int k = 0; k < 4; ++k) {
            o[c] += v[k] * m[k * 4 + c];
        }
    }
    if (o[3] <= 0.0f) {
        return false;
    }
    *x = o[0] / o[3];
    *y = o[1] / o[3];
    return true;
}

bool Near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// The PS VR2's eyes as lane FXR replayed them (degrees: left -61.5, right 43.4, up and down 53.0; the right eye mirrored),
// and the window holding both (+-61.5 x +-53.0).
constexpr float kOuter = 1.8418f, kInner = 0.9472f, kVertical = 1.3270f, kNear = 10.0f;

void TestLampOnItsLamp() {
    Mat4 const tv = sm::Multiply(Rotation(0.4f, -0.1f), ViewBasis());   // the translated view: rotation only
    Mat4 const pWindow = Projection(-kOuter, kOuter, -kVertical, kVertical, kNear);
    Mat4 const pLeft = Projection(-kOuter, kInner, -kVertical, kVertical, kNear);
    Mat4 const pRight = Projection(-kInner, kOuter, -kVertical, kVertical, kNear);
    Mat4 const tvpWindow = sm::Multiply(tv, pWindow);
    Mat4 const tvpLeft = sm::Multiply(tv, pLeft);
    Mat4 const tvpRight = sm::Multiply(tv, pRight);

    // The lamp: in the left eye's image at x 1233 of 3072 (tangent -0.722 to the left), a little above the centre, 4 m away.
    float const tanX = -kOuter + (1233.5f / 3072.0f) * (kInner + kOuter);
    float const tanY = 0.20f;
    float const viewPoint[3] = {tanX * 400.0f, tanY * 400.0f, 400.0f};  // view space: x right, y up, z forward
    auto const toTranslated = sm::Inverse(tv);
    CHECK(toTranslated.has_value());
    if (!toTranslated) {
        return;
    }
    float lamp[3] = {};
    for (int c = 0; c < 3; ++c) {
        lamp[c] = viewPoint[0] * (*toTranslated)[c] + viewPoint[1] * (*toTranslated)[4 + c] + viewPoint[2] * (*toTranslated)[8 + c];
    }

    float lampLeftX = 0, lampLeftY = 0, lampRightX = 0, lampRightY = 0, sx = 0, sy = 0;
    CHECK(Project(tvpLeft, lamp, &lampLeftX, &lampLeftY));
    CHECK(Project(tvpRight, lamp, &lampRightX, &lampRightY));
    CHECK(Project(tvpWindow, lamp, &sx, &sy));   // FSceneView::WorldToScreen through the CPU's window
    auto const pixel = [](float ndcX) { return (ndcX + 1.0f) * 0.5f * 3072.0f; };
    CHECK(Near(pixel(lampLeftX), 1233.5f, 0.5f));

    // The engine's inverse in mode 2: Halcyon's [INVMAT] refreshed it from the eye's matrix. The source element (ray
    // distance 0) lands at the window's screen point in the eye's image: x 934 of 3072, not on its lamp.
    auto const invLeft = sm::Inverse(tvpLeft);
    CHECK(invLeft.has_value());
    if (!invLeft) {
        return;
    }
    auto const wrong = fw::Unproject(*invLeft, sx, sy, 0.1f);
    CHECK(wrong.has_value());
    float wx = 0, wy = 0;
    CHECK(wrong && Project(tvpLeft, wrong->data(), &wx, &wy));
    CHECK(Near(wx, sx, 1e-4f) && Near(wy, sy, 1e-4f));
    CHECK(Near(pixel(wx), 934.0f, 1.5f));
    CHECK(std::fabs(pixel(wx) - pixel(lampLeftX)) > 250.0f);

    // The fix: the window's own inverse. The element sits on the lamp in each eye's image.
    auto const window = fw::WindowInverse(tv, pWindow);
    CHECK(window.has_value());
    if (!window) {
        return;
    }
    CHECK(fw::Differs(*window, *invLeft));
    auto const right = fw::Unproject(*window, sx, sy, 0.1f);
    CHECK(right.has_value());
    float fx = 0, fy = 0;
    CHECK(right && Project(tvpLeft, right->data(), &fx, &fy));
    CHECK(Near(fx, lampLeftX, 1e-4f) && Near(fy, lampLeftY, 1e-4f));
    CHECK(right && Project(tvpRight, right->data(), &fx, &fy));
    CHECK(Near(fx, lampRightX, 1e-4f) && Near(fy, lampRightY, 1e-4f));

    // A ghost (ray distance 1: the source mirrored through the screen's centre): with the fix it is placed along one
    // direction for both eyes (it fuses, as in mode 1); with each eye's own inverse the two directions differ.
    auto const direction = [](std::array<float, 3> const& p) {
        float const l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        return std::array<float, 3>{p[0] / l, p[1] / l, p[2] / l};
    };
    auto const invRight = sm::Inverse(tvpRight);
    auto const ghostLeft = fw::Unproject(*invLeft, -sx, -sy, 0.1f);
    auto const ghostRight = invRight ? fw::Unproject(*invRight, -sx, -sy, 0.1f) : std::nullopt;
    auto const ghostFixed = fw::Unproject(*window, -sx, -sy, 0.1f);
    CHECK(ghostLeft && ghostRight && ghostFixed);
    if (ghostLeft && ghostRight && ghostFixed) {
        auto const a = direction(*ghostLeft), b = direction(*ghostRight);
        float const dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        CHECK(dot < 0.95f);   // more than 18 degrees apart: unfusable
        // With the fix both eyes' passes get the same inverse (the same window, the same view rotation).
        float gx = 0, gy = 0;
        CHECK(Project(tvpWindow, ghostFixed->data(), &gx, &gy));
        CHECK(Near(gx, -sx, 1e-4f) && Near(gy, -sy, 1e-4f));
    }
}

void TestModeOneUntouched() {
    // Mode 1: the GPU's matrix is the window's, so the refreshed inverse already is the window's one.
    Mat4 const tv = sm::Multiply(Rotation(-1.1f, 0.3f), ViewBasis());
    Mat4 const p = Projection(-kOuter, kOuter, -2.072f, 2.072f, kNear);
    auto const engine = sm::Inverse(sm::Multiply(tv, p));
    auto const window = fw::WindowInverse(tv, p);
    CHECK(engine && window);
    CHECK(engine && window && !fw::Differs(*engine, *window));
}

void TestDiffers() {
    Mat4 a{};
    for (int i = 0; i < 16; ++i) {
        a[i] = static_cast<float>(i) * 0.37f - 2.0f;
    }
    Mat4 b = a;
    CHECK(!fw::Differs(a, b));
    b[5] += 1e-6f;
    CHECK(!fw::Differs(a, b));
    b[9] += 0.01f;
    CHECK(fw::Differs(a, b));
    Mat4 c = a;
    c[3] = std::nanf("");
    CHECK(fw::Differs(a, c));
    // A singular product has no window inverse.
    Mat4 zero{};
    CHECK(!fw::WindowInverse(zero, zero).has_value());
}

} // namespace

int RunFlareWindowTests() {
    TestLampOnItsLamp();
    TestModeOneUntouched();
    TestDiffers();
    return gFailures;
}
