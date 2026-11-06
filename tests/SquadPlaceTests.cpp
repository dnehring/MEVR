// SPDX-License-Identifier: MIT
// Offline tests for squad placement by an arc (core/vr/SquadPlace): the flight, its segments and reach, the floor test, the
// right B's hold (a blip, a floor, a wall, "on me", held too long, a press held through where it couldn't start), the host's
// latest view.

#include "core/vr/SquadPlace.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

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

sp::Step Tick(sp::Machine& m, bool button, sp::Landing landing = sp::Landing::Floor, float fromPlayerM = 8.0f, float stickX = 0.0f,
    bool usable = true, float dt = 0.05f, int members = 2) {
    sp::Input in;
    in.dt = dt;
    in.usable = usable;
    in.button = button;
    in.stickX = stickX;
    in.members = members;
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
    {  // The selector: starts on the squad; a flick left is the first squadmate, held over (no repeat) and centred it stays;
       // right twice from there is the second; past the end it stays.
        sp::Machine m;
        sp::Step s = Tick(m, true);
        CHECK(s.started && m.Tile() == 1 && m.Member() == sp::kSquadTile);
        s = Tick(m, true, sp::Landing::Floor, 8.0f, -0.9f);
        CHECK(s.selectionChanged && m.Tile() == 0 && m.Member() == 0);
        for (int i = 0; i < 5; ++i) CHECK(!Tick(m, true, sp::Landing::Floor, 8.0f, -0.9f).selectionChanged);  // held: no repeat
        Tick(m, true, sp::Landing::Floor, 8.0f, 0.0f);
        CHECK(m.Member() == 0);
        CHECK(Tick(m, true, sp::Landing::Floor, 8.0f, 0.8f).selectionChanged && m.Member() == sp::kSquadTile);
        Tick(m, true, sp::Landing::Floor, 8.0f, 0.1f);
        CHECK(Tick(m, true, sp::Landing::Floor, 8.0f, 0.8f).selectionChanged && m.Member() == 1);
        Tick(m, true, sp::Landing::Floor, 8.0f, 0.0f);
        CHECK(!Tick(m, true, sp::Landing::Floor, 8.0f, 0.8f).selectionChanged && m.Member() == 1);  // the last tile
        Tick(m, true, sp::Landing::Floor, 8.0f, 0.45f);  // between the thresholds: not re-armed, not a flick
        s = Tick(m, false);
        CHECK(s.given && s.member == 1);
        // The next hold starts on the squad again.
        CHECK(Tick(m, true).started && m.Member() == sp::kSquadTile);
    }
    {  // A stick already pushed when B goes down doesn't step until it comes back.
        sp::Machine m;
        Tick(m, true, sp::Landing::Floor, 8.0f, -0.9f);
        CHECK(m.Member() == sp::kSquadTile);
        CHECK(!Tick(m, true, sp::Landing::Floor, 8.0f, -0.9f).selectionChanged);
        Tick(m, true, sp::Landing::Floor, 8.0f, 0.0f);
        CHECK(Tick(m, true, sp::Landing::Floor, 8.0f, -0.9f).selectionChanged && m.Member() == 0);
    }
    {  // One squadmate: one tile, the squad; flicks change nothing.
        sp::Machine m;
        Tick(m, true, sp::Landing::Floor, 8.0f, 0.0f, true, 0.05f, 1);
        CHECK(!Tick(m, true, sp::Landing::Floor, 8.0f, 0.9f, true, 0.05f, 1).selectionChanged);
        CHECK(m.Member() == sp::kSquadTile);
        CHECK(sp::TileCount(1) == 1 && sp::TileCount(2) == 3 && sp::TileCount(0) == 0);
        CHECK(sp::MemberOfTile(0, 2) == 0 && sp::MemberOfTile(1, 2) == sp::kSquadTile && sp::MemberOfTile(2, 2) == 1);
        CHECK(sp::MemberOfTile(0, 1) == sp::kSquadTile);
    }
    {  // Double tap: two quick presses bring the whole squad back; a slow second press is a new hold instead.
        sp::Machine m;
        Tick(m, true);
        CHECK(Tick(m, false).cancelled);         // the first tap, a blip
        Tick(m, false);
        sp::Step s = Tick(m, true);
        CHECK(s.regroup && s.order == sp::Order::Follow && s.member == sp::kSquadTile && !m.Holding());
        CHECK(!Tick(m, true).started);           // the second press, held on, never becomes a hold
        Tick(m, false);
        sp::Machine n;
        Tick(n, true);
        Tick(n, false);
        for (int i = 0; i < 10; ++i) Tick(n, false);  // 0.5 s later
        s = Tick(n, true);
        CHECK(s.started && !s.regroup);
        // A held placement let go, then a quick press: not a double tap (only a blip arms it).
        sp::Machine o;
        Tick(o, true);
        for (int i = 0; i < 10; ++i) Tick(o, true);
        CHECK(Tick(o, false).given);
        CHECK(Tick(o, true).started);
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
        CHECK(Tick(m, true, sp::Landing::Floor, 8.0f, 0.0f, false).cancelled);
        CHECK(!Tick(m, true).started);
        sp::Machine n;
        CHECK(!Tick(n, true, sp::Landing::Floor, 8.0f, 0.0f, false).started);
        CHECK(!Tick(n, true).started);  // usable now, but B was already down
        Tick(n, false);
        CHECK(Tick(n, true).started);
    }
}

sp::Step TickW(sp::Machine& m, bool button, float stickX, float stickY, std::uint8_t mask = 0b1011, int current = 3,
    sp::Landing landing = sp::Landing::Floor) {
    sp::Input in;
    in.dt = 0.05f;
    in.usable = true;
    in.button = button;
    in.stickX = stickX;
    in.stickY = stickY;
    in.members = 2;
    in.weaponMask = mask;
    in.currentWeapon = current;
    in.landing = landing;
    in.landingFromPlayerM = 8.0f;
    return m.Advance(in, sp::Settings{});
}

void TestWeapons() {
    CHECK(sp::WeaponTypes(0b1011) == (std::vector<int>{0, 1, 3}));
    CHECK(sp::WeaponTypes(0).empty());
    {  // Down opens the row on the weapon in hand; sideways picks; let go equips (no move).
        sp::Machine m;
        TickW(m, true, 0.0f, 0.0f);
        TickW(m, true, -0.9f, 0.0f);  // the first squadmate
        TickW(m, true, 0.0f, 0.0f);
        sp::Step s = TickW(m, true, 0.0f, -0.9f);
        CHECK(s.rowChanged && m.CurrentRow() == sp::Row::Weapon && m.Weapon() == 3 && m.WeaponTile() == 2);
        TickW(m, true, 0.0f, 0.0f);
        s = TickW(m, true, -0.9f, 0.0f);
        CHECK(s.weaponChanged && m.Weapon() == 1 && m.Member() == 0);  // sideways steps weapons now, not who
        TickW(m, true, 0.0f, 0.0f);
        s = TickW(m, false, 0.0f, 0.0f);
        CHECK(s.equip && s.weapon == 1 && s.member == 0 && !s.given && !s.cancelled);
    }
    {  // Up goes back to the names: let go places them as before.
        sp::Machine m;
        TickW(m, true, 0.0f, 0.0f);
        TickW(m, true, 0.0f, -0.9f);
        CHECK(m.CurrentRow() == sp::Row::Weapon);
        TickW(m, true, 0.0f, 0.0f);
        CHECK(TickW(m, true, 0.0f, 0.9f).rowChanged && m.CurrentRow() == sp::Row::Who && m.Weapon() == -1);
        TickW(m, true, 0.0f, 0.0f);
        sp::Step const s = TickW(m, false, 0.0f, 0.0f);
        CHECK(s.given && !s.equip && s.member == sp::kSquadTile);
    }
    {  // No weapons for the lit tile, or the row turned off: down does nothing; a diagonal goes by the stronger axis.
        sp::Machine m;
        TickW(m, true, 0.0f, 0.0f, 0);
        CHECK(!TickW(m, true, 0.0f, -0.9f, 0).rowChanged && m.CurrentRow() == sp::Row::Who);
        sp::Machine n;
        sp::Settings off;
        off.weapons = false;
        sp::Input in;
        in.dt = 0.05f;
        in.usable = true;
        in.button = true;
        in.members = 2;
        in.weaponMask = 0b1111;
        n.Advance(in, off);
        in.stickY = -0.9f;
        CHECK(!n.Advance(in, off).rowChanged);
        sp::Machine d;
        TickW(d, true, 0.0f, 0.0f);
        sp::Step const s = TickW(d, true, -0.7f, -0.8f);  // more down than left
        CHECK(s.rowChanged && !s.selectionChanged);
    }
    {  // The next hold starts on the names again.
        sp::Machine m;
        TickW(m, true, 0.0f, 0.0f);
        TickW(m, true, 0.0f, -0.9f);
        TickW(m, true, 0.0f, 0.0f);
        TickW(m, false, 0.0f, 0.0f);
        CHECK(TickW(m, true, 0.0f, 0.0f).started && m.CurrentRow() == sp::Row::Who);
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
    TestWeapons();
    TestLatest();
    return gFailures;
}
