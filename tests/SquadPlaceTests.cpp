// SPDX-License-Identifier: MIT
// Offline tests for squad placement by an arc (core/vr/SquadPlace): the flight, its segments and reach, the floor test, the
// right B's hold (a blip, a floor, a wall, "on me", held too long, a press held through where it couldn't start), the host's
// latest view.

#include "core/vr/SquadPlace.hpp"

#include <cmath>
#include <cstdio>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/SquadPlaceTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace sp = mevr::vr::squad_place;

bool Close(float a, float b, float tolerance = 1e-3f) {
    return std::abs(a - b) <= tolerance;
}

void TestArc() {
    sp::ArcSettings const s;
    sp::Vec3 const origin{0.0f, 0.0f, 1.3f};
    // Level: it falls 1.3 m in sqrt(2 * 1.3 / g) and has gone speed times that.
    sp::Vec3 const level{1.0f, 0.0f, 0.0f};
    float const fall = std::sqrt(2.0f * 1.3f / s.gravityMS2);
    sp::Vec3 const at = sp::ArcAt(origin, level, fall, s);
    CHECK(Close(at.z, 0.0f));
    CHECK(Close(at.x, s.speedMS * fall));
    CHECK(Close(at.y, 0.0f));
    // The segments start at the origin, are stepS apart in time and stop at maxS.
    sp::ArcSettings wide = s;
    wide.rangeM = 1000.0f;  // the range doesn't cut it short
    std::vector<sp::Vec3> const segments = sp::ArcSegments(origin, level, wide);
    CHECK(Close(segments.front().x, 0.0f) && Close(segments.front().z, 1.3f));
    CHECK(static_cast<int>(segments.size()) == static_cast<int>(std::ceil(s.maxS / s.stepS)) + 1);
    CHECK(Close(segments[1].x, s.speedMS * s.stepS));
    // Aimed up at 45 degrees it flies farther than the range: the segments end just past it.
    float const r = std::sqrt(0.5f);
    sp::ArcSettings shortReach = s;
    shortReach.rangeM = 10.0f;
    std::vector<sp::Vec3> const far = sp::ArcSegments(origin, {r, 0.0f, r}, shortReach);
    float const lastFlat = std::hypot(far.back().x, far.back().y);
    float const beforeFlat = std::hypot(far[far.size() - 2].x, far[far.size() - 2].y);
    CHECK(lastFlat > 10.0f && beforeFlat <= 10.0f);
    // Samples for drawing: the ends are the origin and the time asked for.
    std::vector<sp::Vec3> const samples = sp::ArcSamples(origin, level, fall, 9, s);
    CHECK(samples.size() == 9);
    CHECK(Close(samples.back().x, at.x) && Close(samples.back().z, at.z));
    CHECK(sp::ArcSamples(origin, level, fall, 1, s).size() == 2);
    // Floor: level ground and a gentle slope are floor; a wall, a steep slope and a ceiling aren't.
    CHECK(sp::IsFloor({0.0f, 0.0f, 1.0f}, s));
    CHECK(sp::IsFloor({0.3f, 0.0f, 0.95f}, s));
    CHECK(!sp::IsFloor({1.0f, 0.0f, 0.0f}, s));
    CHECK(!sp::IsFloor({0.8f, 0.0f, 0.6f}, s));
    CHECK(!sp::IsFloor({0.0f, 0.0f, -1.0f}, s));
    CHECK(!sp::IsFloor({0.0f, 0.0f, 0.0f}, s));
    CHECK(sp::IsFloor({0.0f, 0.0f, 5.0f}, s));  // not unit: still floor
}

sp::Step Tick(sp::Machine& m, bool button, sp::Landing landing = sp::Landing::Floor, float fromPlayerM = 8.0f, int member = -1,
    bool usable = true, float dt = 0.05f) {
    sp::Input in;
    in.dt = dt;
    in.usable = usable;
    in.button = button;
    in.pointedMember = member;
    in.landing = landing;
    in.landingFromPlayerM = fromPlayerM;
    return m.Advance(in, sp::Settings{});
}

void TestHold() {
    {  // Held and let go on floor far off: a move, for everyone.
        sp::Machine m;
        CHECK(!Tick(m, false).started);
        sp::Step s = Tick(m, true);
        CHECK(s.started && m.Holding() && s.member == -1);
        for (int i = 0; i < 10; ++i) CHECK(!Tick(m, true).given);
        s = Tick(m, false);
        CHECK(s.given && s.order == sp::Order::Move && s.member == -1 && !m.Holding());
    }
    {  // The squadmate pointed at when B went down gets it alone, wherever the ray goes after.
        sp::Machine m;
        CHECK(Tick(m, true, sp::Landing::Floor, 8.0f, 1).member == 1);
        for (int i = 0; i < 10; ++i) Tick(m, true, sp::Landing::Floor, 8.0f, -1);
        sp::Step const s = Tick(m, false, sp::Landing::Floor, 8.0f, 0);
        CHECK(s.given && s.member == 1);
    }
    {  // Near Shepard: on me (a follow).
        sp::Machine m;
        Tick(m, true);
        for (int i = 0; i < 10; ++i) Tick(m, true, sp::Landing::Floor, 1.0f);
        sp::Step const s = Tick(m, false, sp::Landing::Floor, 1.0f);
        CHECK(s.given && s.order == sp::Order::Follow);
    }
    {  // A blip: nothing given.
        sp::Machine m;
        Tick(m, true);
        sp::Step const s = Tick(m, false);
        CHECK(s.cancelled && !s.given);
    }
    {  // Let go on a wall or on nothing: called off; the landing's change ticks.
        sp::Machine m;
        Tick(m, true);
        for (int i = 0; i < 5; ++i) Tick(m, true);
        CHECK(Tick(m, true, sp::Landing::Wall).landingChanged);
        CHECK(!Tick(m, true, sp::Landing::Wall).landingChanged);
        sp::Step s = Tick(m, false, sp::Landing::Wall);
        CHECK(s.cancelled && !s.given);
        Tick(m, true);
        for (int i = 0; i < 5; ++i) Tick(m, true, sp::Landing::None);
        s = Tick(m, false, sp::Landing::None);
        CHECK(s.cancelled && !s.given);
    }
    {  // Held too long: called off, and the same press never starts again.
        sp::Machine m;
        Tick(m, true);
        sp::Step s;
        for (int i = 0; i < 400 && !s.cancelled; ++i) s = Tick(m, true);
        CHECK(s.cancelled && !m.Holding());
        CHECK(!Tick(m, true).started);
        CHECK(!Tick(m, false).given);
        CHECK(Tick(m, true).started);  // a new press does
    }
    {  // No longer usable while holding (a menu opened): called off; pressed where it can't start: that press never does.
        sp::Machine m;
        Tick(m, true);
        CHECK(Tick(m, true, sp::Landing::Floor, 8.0f, -1, false).cancelled);
        CHECK(!Tick(m, true).started);
        sp::Machine n;
        CHECK(!Tick(n, true, sp::Landing::Floor, 8.0f, -1, false).started);
        CHECK(!Tick(n, true).started);  // usable now, but B was already down
        Tick(n, false);
        CHECK(Tick(n, true).started);
    }
}

void TestLatest() {
    sp::View v;
    v.active = 1;
    v.count = 3;
    v.landing = static_cast<std::uint8_t>(sp::Landing::Floor);
    sp::Publish(v);
    sp::View const got = sp::Latest();
    CHECK(got.active == 1 && got.count == 3 && got.size == sizeof(sp::View));
    CHECK(sp::Latest(0).active == 0 || sp::Latest(0).active == 1);  // age 0 may or may not have passed: no crash
}

}  // namespace

int RunSquadPlaceTests() {
    TestArc();
    TestHold();
    TestLatest();
    return gFailures;
}
