// SPDX-License-Identifier: GPL-3.0-only
// src/halcyon/MevrMetricView.h (life-size stereo's render side on ME2 and ME3; research/30 section 9.5): what pass 0
// latches (the newest plan made for stereo, the last one when the newest is upstream's mode 0, the eyes' centre after a
// flat present or before any plan, the cinema screen's routing), what the submit reads back (LatchedPlan), and mode 2's
// containment: a view whose CPU window can't hold the eyes' frusta renders that pair on the symmetric window. Over a
// synthetic FSceneView (the fields at LE1's offsets, as in both sequels).

#include "halcyon/MevrMetricView.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/MetricViewTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace mv = ::MevrMetricView;

std::vector<std::string> gLog;
void Log(char const* line) { gLog.emplace_back(line); }

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// The PS VR2's eyes (research/37: left -61.5, 43.4, 53.0, -53.0 degrees; the right eye mirrored), as tangents.
constexpr float kDeg = 3.14159265358979f / 180.0f;
mv::Plan Mode2Plan() {
    mv::Plan p;
    p.mode = 2;
    p.pass0Eye = 0;
    float const l = std::tan(-61.5f * kDeg), r = std::tan(43.4f * kDeg), u = std::tan(53.0f * kDeg), d = std::tan(-53.0f * kDeg);
    float const tans[2][4] = {{l, r, u, d}, {-r, -l, u, d}};
    for (int pass = 0; pass < 2; ++pass) {
        mv::Pass& q = p.passes[pass];
        for (int k = 0; k < 4; ++k) q.tan[k] = tans[pass][k];
        q.p0 = 2.0f / (q.tan[1] - q.tan[0]);
        q.p5 = 2.0f / (q.tan[2] - q.tan[3]);
        q.p8 = -(q.tan[1] + q.tan[0]) / (q.tan[1] - q.tan[0]);
        q.p9 = -(q.tan[2] + q.tan[3]) / (q.tan[2] - q.tan[3]);
        q.offsetUU[0] = pass == 0 ? -3.15f : 3.15f;   // 63 mm at 100 uu per metre
    }
    return p;
}
mv::Plan Mode0Plan() {
    mv::Plan p;
    p.mode = 0;
    return p;
}

void TestChooseLatch() {
    mv::LatchInput in;
    in.next = Mode2Plan();
    mv::LatchChoice c = mv::ChooseLatch(in);
    CHECK(c.source == mv::Source::Published && c.plan.mode == 2 && c.plan.passes[1].offsetUU[0] == 3.15f);

    // The newest is upstream's mode 0 (planned while the cinema screen was up, or the flags flipped before the publish).
    in.next = Mode0Plan();
    in.lastStereo = Mode2Plan();
    in.lastStereoAny = true;
    c = mv::ChooseLatch(in);
    CHECK(c.source == mv::Source::LastStereo && c.plan.mode == 2);
    // ...and nothing planned for stereo yet: the eyes' centre, life-size, no offset: never upstream's half eye.
    in.lastStereoAny = false;
    c = mv::ChooseLatch(in);
    CHECK(c.source == mv::Source::Centre && c.plan.mode == 1 && c.plan.pass0Eye == 0);
    CHECK(c.plan.passes[0].offsetUU[0] == 0.0f && c.plan.passes[1].offsetUU[0] == 0.0f);

    // A flat present since: the eyes' centre whatever was published.
    in.next = Mode2Plan();
    in.expired = true;
    c = mv::ChooseLatch(in);
    CHECK(c.source == mv::Source::Expired && c.plan.mode == 1 && c.plan.passes[0].offsetUU[0] == 0.0f);

    // The cinema screen's pair: pass 0 the left camera, routed with no swap or shift, whatever life-size says.
    in.frame = mv::Frame::CinemaScreen;
    c = mv::ChooseLatch(in);
    CHECK(c.source == mv::Source::CinemaScreen && c.plan.mode == 1 && c.plan.pass0Eye == 0);
    in.lifeSize = false;
    c = mv::ChooseLatch(in);
    CHECK(c.source == mv::Source::CinemaScreen && c.plan.mode == 1);

    // Life-size off ([Debug] upstreamStereo = 1, a measurement): upstream's plan exactly, expired or not.
    in.frame = mv::Frame::World;
    in.next = Mode0Plan();
    in.next.pass0Eye = 1;   // upstream's SwapEyes
    c = mv::ChooseLatch(in);
    CHECK(c.source == mv::Source::Published && c.plan.mode == 0 && c.plan.pass0Eye == 1);
}

void TestPublishAndLatch() {
    mv::ResetForTests();
    mv::Plan out;
    CHECK(!mv::LatchedPlan(&out));   // nothing rendered yet

    // A stereo present publishes; pass 0 latches it, pass 1 keeps it even if a newer plan arrives in between.
    mv::Publish(Mode2Plan());
    mv::Plan const& pass0 = mv::Latch(false, mv::Frame::World, true, Log);
    CHECK(pass0.mode == 2);
    mv::Publish(Mode0Plan());
    mv::Plan const& pass1 = mv::Latch(true, mv::Frame::World, true, Log);
    CHECK(pass1.mode == 2);
    CHECK(mv::LatchedPlan(&out) && out.mode == 2);

    // The newest is mode 0 (the screen's plan): the next Draw renders the last stereo plan, not upstream.
    CHECK(mv::Latch(false, mv::Frame::World, true, Log).mode == 2);
    CHECK(mv::LatchedPlan(&out) && out.mode == 2);

    // A flat present expires the plan: the next Draw is from the eyes' centre, until a stereo present publishes.
    mv::Expire();
    mv::Plan const& flat = mv::Latch(false, mv::Frame::World, true, Log);
    CHECK(flat.mode == 1 && flat.passes[0].offsetUU[0] == 0.0f);
    CHECK(mv::LatchedPlan(&out) && out.mode == 1);
    mv::Publish(Mode2Plan());
    CHECK(mv::Latch(false, mv::Frame::World, true, Log).mode == 2);

    // The cinema screen: the submit after it goes down routes that pair as rendered (mode 1: no swap, no shift).
    CHECK(mv::Latch(false, mv::Frame::CinemaScreen, true, Log).mode == 1);
    CHECK(mv::LatchedPlan(&out) && out.mode == 1 && out.pass0Eye == 0);

    // Each change of source was logged once.
    bool expiredLogged = false, screenLogged = false;
    for (std::string const& line : gLog) {
        expiredLogged = expiredLogged || line.find("the plan expired") != std::string::npos;
        screenLogged = screenLogged || line.find("cinema screen's routing") != std::string::npos;
    }
    CHECK(expiredLogged && screenLogged);

    // Before any plan made for stereo (a process's first gameplay frame): the eyes' centre, never upstream's mode 0.
    mv::ResetForTests();
    mv::Publish(Mode0Plan());
    mv::Plan const& first = mv::Latch(false, mv::Frame::World, true, Log);
    CHECK(first.mode == 1 && first.passes[1].offsetUU[0] == 0.0f);
    mv::ResetForTests();
}

// A synthetic FSceneView: identity view matrices at LE1's offsets, a perspective projection with the given symmetric
// window (UE3's: P[0] = 1/tanH, P[5] = 1/tanV, P[11] = 1 (w = z), P[15] = 0).
struct FakeView {
    float storage[0x100] = {};   // 0x400 bytes, float-aligned
    unsigned char* const bytes = reinterpret_cast<unsigned char*>(storage);
    float* At(std::uintptr_t off) { return reinterpret_cast<float*>(bytes + off); }
    std::uintptr_t Address() { return reinterpret_cast<std::uintptr_t>(bytes); }
    explicit FakeView(float tanH, float tanV) {
        mv::ViewFields const f;
        for (std::uintptr_t m : {f.viewMatrix, f.translatedView}) {
            float* v = At(m);
            v[0] = v[5] = v[10] = v[15] = 1.0f;
        }
        float* p = At(f.projection);
        p[0] = 1.0f / tanH;
        p[5] = 1.0f / tanV;
        p[10] = 0.0f;
        p[11] = 1.0f;
        p[14] = 10.0f;   // UE3's reversed-Z near plane
        mv::Mul4x4(At(f.viewProjection), At(f.viewMatrix), p);
        mv::Mul4x4(At(f.translatedViewProj), At(f.translatedView), p);
    }
};

void TestEyeFrustum() {
    mv::ViewFields const f;
    mv::Plan const plan = Mode2Plan();

    // The window widened to hold both eyes (LS's union window): pass 0 writes the left eye's off-centre frustum.
    {
        mv::ResetForTests();
        mv::Publish(plan);
        FakeView view(std::tan(61.6f * kDeg), std::tan(53.1f * kDeg));
        mv::Plan const& p0 = mv::Latch(false, mv::Frame::World, true, Log);
        mv::ApplyEyeFrustum(view.Address(), f, p0, false, 1.0f, Log);
        float const* tvp = view.At(f.translatedViewProj);
        CHECK(Near(tvp[0], plan.passes[0].p0) && Near(tvp[5], plan.passes[0].p5) && Near(tvp[8], plan.passes[0].p8));
        mv::Plan out;
        CHECK(mv::LatchedPlan(&out) && out.mode == 2);
        CHECK(Near(view.At(f.projection)[0], 1.0f / std::tan(61.6f * kDeg)));   // the CPU's window untouched
    }
    // A view the renderer left narrow (ME2's narrow view without a weapon: a conversation's first frames): the CPU window
    // can't hold the eyes, so the pair renders on the symmetric window, both passes and the submit.
    {
        mv::ResetForTests();
        mv::Publish(plan);
        float const narrow = std::tan(31.0f * kDeg);
        FakeView view(narrow, std::tan(18.0f * kDeg));
        float before[16];
        std::memcpy(before, view.At(f.translatedViewProj), sizeof(before));
        mv::Plan const& p0 = mv::Latch(false, mv::Frame::World, true, Log);
        mv::ApplyEyeFrustum(view.Address(), f, p0, false, 1.0f, Log);
        CHECK(std::memcmp(before, view.At(f.translatedViewProj), sizeof(before)) == 0);
        mv::Plan out;
        CHECK(mv::LatchedPlan(&out) && out.mode == 1);
        mv::Plan const& p1 = mv::Latch(true, mv::Frame::World, true, Log);
        CHECK(p1.mode == 1);
        FakeView view1(narrow, std::tan(18.0f * kDeg));
        mv::ApplyEyeFrustum(view1.Address(), f, p1, true, 1.0f, Log);
        CHECK(std::memcmp(before, view1.At(f.translatedViewProj), sizeof(before)) == 0);
        bool logged = false;
        for (std::string const& line : gLog) logged = logged || line.find("doesn't hold the eyes' frusta") != std::string::npos;
        CHECK(logged);
    }
    // Zoomed (ADS: the render narrower than the declared window by the same factor): the eyes' frusta scaled by the zoom
    // fit the narrowed CPU window, so the eye's own frustum stays.
    {
        mv::ResetForTests();
        mv::Publish(plan);
        float const zoom = 0.4f;
        FakeView view(std::tan(61.6f * kDeg) * zoom, std::tan(53.1f * kDeg) * zoom);
        mv::Plan const& p0 = mv::Latch(false, mv::Frame::World, true, Log);
        mv::ApplyEyeFrustum(view.Address(), f, p0, false, zoom, Log);
        CHECK(Near(view.At(f.translatedViewProj)[0], plan.passes[0].p0 / zoom, 1e-3f));
        mv::Plan out;
        CHECK(mv::LatchedPlan(&out) && out.mode == 2);
    }
    mv::ResetForTests();
}

void TestWindowHoldsPlan() {
    mv::Plan const plan = Mode2Plan();
    CHECK(mv::WindowHoldsPlan(std::tan(61.5f * kDeg), std::tan(53.0f * kDeg), plan, 1.0f));
    CHECK(!mv::WindowHoldsPlan(std::tan(55.0f * kDeg), std::tan(53.0f * kDeg), plan, 1.0f));   // the left eye's outer edge
    CHECK(!mv::WindowHoldsPlan(std::tan(61.5f * kDeg), std::tan(45.0f * kDeg), plan, 1.0f));
    CHECK(mv::WindowHoldsPlan(std::tan(61.5f * kDeg) * 0.5f, std::tan(53.0f * kDeg) * 0.5f, plan, 0.5f));
}

void TestEyeOffset() {
    mv::ViewFields const f;
    // From the eyes' centre: the view is left exactly as built.
    {
        FakeView view(1.0f, 1.0f);
        unsigned char before[sizeof(view.storage)];
        std::memcpy(before, view.storage, sizeof(before));
        CHECK(mv::ApplyEyeOffset(view.Address(), f, mv::CentrePlan(), false, Log));
        CHECK(std::memcmp(before, view.storage, sizeof(before)) == 0);
    }
    // Mode 0 (life-size off): the caller's own half eye.
    {
        FakeView view(1.0f, 1.0f);
        CHECK(!mv::ApplyEyeOffset(view.Address(), f, Mode0Plan(), false, Log));
    }
    // A pass's offset moves the camera along its right axis (the identity view: world x).
    {
        FakeView view(1.0f, 1.0f);
        CHECK(mv::ApplyEyeOffset(view.Address(), f, Mode2Plan(), true, Log));
        CHECK(Near(view.At(f.viewOrigin)[0], 3.15f) && Near(view.At(f.preViewTranslation)[0], -3.15f));
        CHECK(Near(view.At(f.viewMatrix)[12], -3.15f));
    }
}

} // namespace

int main() {
    TestChooseLatch();
    TestPublishAndLatch();
    TestEyeFrustum();
    TestWindowHoldsPlan();
    TestEyeOffset();
    if (gFailures == 0) {
        std::printf("metric view tests: all passed\n");
        return 0;
    }
    std::printf("metric view tests: %d failures\n", gFailures);
    return 1;
}
