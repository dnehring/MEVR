// SPDX-License-Identifier: MIT
// Offline tests for the draw replay's second-eye screen-space matrices (core/vr/ScreenMatrix, research/37 "The right
// eye's shadows") and the shader reflection reader that finds them (core/d3d11/ShaderReflect).
//
// The model: two eyes 6.4 cm apart with their own off-centre projections, a world point, a shadow projection R. The
// first eye's composite M1 = S1 x Inv(TVP1) x T(-pvt1) x R, as UE3 builds ScreenToShadowMatrix; the second eye's
// shader input (its clip x, y, w and 1) through C x M1 must land where the point's own shadow coordinate is.

#include "core/d3d11/ShaderReflect.hpp"
#include "core/vr/ScreenMatrix.hpp"

#include <d3dcompiler.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ScreenMatrixTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace sm = mevr::vr::screen_matrix;
using sm::Mat4;

Mat4 Identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Mat4 Translation(float x, float y, float z) {
    Mat4 m = Identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}

// A rotation about Z by `yaw`, then about the new Y by `pitch`, as a UE3 view matrix's rotation part (row vectors).
Mat4 Rotation(float yaw, float pitch) {
    float const cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    Mat4 const rz{cy, sy, 0, 0, -sy, cy, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Mat4 const ry{cp, 0, -sp, 0, 0, 1, 0, 0, sp, 0, cp, 0, 0, 0, 0, 1};
    return sm::Multiply(rz, ry);
}

// UE3's view basis change: world X forward, Y right, Z up -> view Z forward, X right, Y up.
Mat4 ViewBasis() {
    return {0, 0, 1, 0,
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 0, 1};
}

// An off-centre perspective projection with UE3's layout (near plane `n`, infinite far: P22 = 1, P32 = -n).
Mat4 Projection(float left, float right, float down, float up, float n) {
    float const a = 2.0f / (right - left), b = 2.0f / (up - down);
    float const c = -(right + left) / (right - left), d = -(up + down) / (up - down);
    return {a, 0, 0, 0,
            0, b, 0, 0,
            c, d, 1, 1,
            0, 0, -n, 0};
}

void Row(float const* v, Mat4 const& m, float* out) {
    for (int c = 0; c < 4; ++c) {
        out[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + v[3] * m[12 + c];
    }
}

void TestSecondEyeShadow() {
    Mat4 const rot = sm::Multiply(Rotation(0.7f, -0.25f), ViewBasis());  // the translated view: rotation only
    // Each eye's own frustum (a headset's are mirrored and off-centre).
    Mat4 const p1 = Projection(-1.2f, 0.9f, -1.1f, 1.0f, 10.0f);
    Mat4 const p2 = Projection(-0.9f, 1.2f, -1.1f, 1.0f, 10.0f);
    Mat4 const tvp1 = sm::Multiply(rot, p1);
    Mat4 const tvp2 = sm::Multiply(rot, p2);
    // The eyes: 6.4 cm apart (6.4 units at 100 per metre) along the view's right, far from the origin.
    float const head[3] = {-31000.0f, 145000.0f, 900.0f};
    float const rightX = rot[0], rightY = rot[4], rightZ = rot[8];   // world direction of view X
    float const e1[3] = {head[0] - 3.2f * rightX, head[1] - 3.2f * rightY, head[2] - 3.2f * rightZ};
    float const e2[3] = {head[0] + 3.2f * rightX, head[1] + 3.2f * rightY, head[2] + 3.2f * rightZ};
    float const pvt1[3] = {-e1[0], -e1[1], -e1[2]};
    float const pvt2[3] = {-e2[0], -e2[1], -e2[2]};

    auto const s1 = sm::ScreenToClip(tvp1);
    CHECK(s1.has_value());
    CHECK(s1 && std::fabs((*s1)[10] - 1.0f) < 1e-4f && std::fabs((*s1)[14] + 10.0f) < 1e-3f);

    // A shadow projection R: a light's view-projection around the point, with a scale and bias.
    Mat4 const lightRot = sm::Multiply(Rotation(-1.3f, 0.9f), ViewBasis());
    Mat4 const r = sm::Multiply(sm::Multiply(Translation(31000.0f, -145200.0f, -1000.0f), lightRot),
                                Mat4{0.002f, 0, 0, 0, 0, -0.002f, 0, 0, 0, 0, 0.0001f, 0, 0.5f, 0.5f, 0.01f, 1});
    auto const inv1 = sm::Inverse(tvp1);
    CHECK(inv1.has_value());
    if (!s1 || !inv1) {
        return;
    }
    Mat4 const m1 = sm::Multiply(sm::Multiply(sm::Multiply(*s1, *inv1), Translation(-pvt1[0], -pvt1[1], -pvt1[2])), r);

    auto const c = sm::SecondEyeCorrection(tvp1, tvp2, pvt1, pvt2);
    CHECK(c.has_value());
    if (!c) {
        return;
    }
    Mat4 m2 = m1;
    sm::Apply(*c, m2.data(), true);
    Mat4 m2t{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            m2t[j * 4 + i] = m1[i * 4 + j];
        }
    }
    sm::Apply(*c, m2t.data(), false);

    // Points 1-20 m ahead, spread across the view.
    float worst = 0.0f, worstUncorrected = 0.0f, worstTransposed = 0.0f;
    for (int i = 0; i < 9; ++i) {
        float const ahead = 100.0f + 230.0f * static_cast<float>(i);
        float const side = -120.0f + 30.0f * static_cast<float>(i);
        // A point in front of the head, in view axes, back to the world.
        float const viewPt[4] = {side, -40.0f + 10.0f * static_cast<float>(i), ahead, 1.0f};  // view x right, y up, z forward
        float world[4];
        auto const rotInv = sm::Inverse(rot);
        Row(viewPt, *rotInv, world);
        for (int k = 0; k < 3; ++k) {
            world[k] += head[k];
        }
        world[3] = 1.0f;
        float expect[4];
        Row(world, r, expect);
        // The second eye sees it at (world + pvt2) x TVP2; its shader input is (clip x, clip y, clip w, 1).
        float const translated[4] = {world[0] + pvt2[0], world[1] + pvt2[1], world[2] + pvt2[2], 1.0f};
        float clip[4];
        Row(translated, tvp2, clip);
        float const input[4] = {clip[0], clip[1], clip[3], 1.0f};
        float got[4], wrong[4], gotT[4];
        Row(input, m2, got);
        Row(input, m1, wrong);
        // Transposed storage: the shader reads mem^T, so the product is input x (m2t)^T.
        Mat4 back{};
        for (int a = 0; a < 4; ++a) {
            for (int b = 0; b < 4; ++b) {
                back[b * 4 + a] = m2t[a * 4 + b];
            }
        }
        Row(input, back, gotT);
        for (int k = 0; k < 3; ++k) {
            worst = std::fmax(worst, std::fabs(got[k] / got[3] - expect[k] / expect[3]));
            worstTransposed = std::fmax(worstTransposed, std::fabs(gotT[k] / gotT[3] - expect[k] / expect[3]));
            worstUncorrected = std::fmax(worstUncorrected, std::fabs(wrong[k] / wrong[3] - expect[k] / expect[3]));
        }
    }
    // Shadow-map coordinates (0..1 across a 10 m light frustum here): within a thousandth with the correction; the
    // first eye's matrix lands far off for the second eye.
    CHECK(worst < 1e-3f);
    CHECK(worstTransposed < 1e-3f);
    CHECK(worstUncorrected > 5e-3f);
    std::printf("screen matrix: second-eye shadow coordinate error %.6f corrected, %.6f with the first eye's matrix\n",
                worst, worstUncorrected);

    // The same view for both eyes: C is the identity.
    auto const same = sm::SecondEyeCorrection(tvp1, tvp1, pvt1, pvt1);
    CHECK(same.has_value());
    if (same) {
        float dev = 0.0f;
        Mat4 const id = Identity();
        for (int i = 0; i < 16; ++i) {
            dev = std::fmax(dev, std::fabs((*same)[i] - id[i]));
        }
        CHECK(dev < 1e-4f);
    }
    // Not a projection of UE3's shape: refused.
    CHECK(!sm::ScreenToClip(Identity()).has_value());
}

bool WantScreen(std::string_view name) {
    return name.rfind("Screen", 0) == 0;
}

void TestReflection() {
    char const* const source =
        "float4 ScreenPositionScaleBias;\n"
        "row_major float4x4 ScreenToShadowMatrix;\n"
        "float4x4 ScreenToWorld;\n"
        "float4 ShadowBufferSize;\n"
        "cbuffer Extra : register(b3) { row_major float4x4 ScreenToView; float4 Unused; }\n"
        "float4 main(float4 p : SV_Position) : SV_Target {\n"
        "  float4 a = mul(p, ScreenToShadowMatrix) + mul(p, ScreenToWorld) + ShadowBufferSize + ScreenPositionScaleBias;\n"
        "  return a + mul(p, ScreenToView);\n"
        "}\n";
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT const hr = D3DCompile(source, std::strlen(source), "reflect", nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors);
    if (errors != nullptr) {
        errors->Release();
    }
    CHECK(SUCCEEDED(hr) && code != nullptr);
    if (FAILED(hr) || code == nullptr) {
        return;
    }
    namespace rf = mevr::d3d11::reflect;
    auto const all = rf::Variables(code->GetBufferPointer(), code->GetBufferSize());
    auto const screen = rf::Variables(code->GetBufferPointer(), code->GetBufferSize(), &WantScreen);
    auto find = [&](std::string const& n) -> rf::Variable const* {
        for (rf::Variable const& v : screen) {
            if (v.name == n) {
                return &v;
            }
        }
        return nullptr;
    };
    CHECK(all.size() >= 6);
    CHECK(screen.size() == 4);
    rf::Variable const* const shadow = find("ScreenToShadowMatrix");
    rf::Variable const* const world = find("ScreenToWorld");
    rf::Variable const* const view = find("ScreenToView");
    CHECK(shadow != nullptr && shadow->slot == 0 && shadow->offset == 16 && shadow->size == 64 && shadow->varClass == 2 &&
          shadow->rows == 4 && shadow->columns == 4);
    CHECK(world != nullptr && world->slot == 0 && world->offset == 80 && world->varClass == 3);
    CHECK(view != nullptr && view->slot == 3 && view->offset == 0 && view->varClass == 2);
    // Garbage and truncation are refused, not read past.
    CHECK(rf::Variables(code->GetBufferPointer(), 40).empty());
    std::string junk(200, 'x');
    CHECK(rf::Variables(junk.data(), junk.size()).empty());
    code->Release();
}

} // namespace

int RunScreenMatrixTests() {
    TestSecondEyeShadow();
    TestReflection();
    return gFailures;
}
