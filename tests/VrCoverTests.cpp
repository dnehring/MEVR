// SPDX-License-Identifier: MIT
// Offline tests for VR cover (core/vr/VrCover, research/23 section 6): when a VR player's head is hidden behind cover from
// a shooter, and the cover's facing the games' own damage code then tests its cone against.

#include "core/vr/VrCover.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string_view>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/VrCoverTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace vc = mevr::vr::cover;

bool Near(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance;
}

// A crouched player at the origin's side of a crate: the eyes at (0, 0, 100), an enemy 15 m away along +X at eye height.
vc::Vec3 const kEyes{0.0, 0.0, 100.0};
vc::Vec3 const kShooter{1500.0, 0.0, 160.0};

void TestSlotLease() {
    vc::SlotState native{0, -1};
    vc::SlotLease outer, inner;
    CHECK(outer.Acquire(native, {100, 2}) && native == vc::SlotState(100, 2));
    CHECK(!outer.Acquire(native, {300, 4}));
    // A rejected/early-return nested hit owns no fields and cannot restore the outer invocation.
    CHECK(!inner.Release(native, true) && native == vc::SlotState(100, 2));
    CHECK(inner.Acquire(native, {200, 3}));
    CHECK(inner.Release(native, true) && native == vc::SlotState(100, 2));
    CHECK(outer.Release(native, true) && native == vc::SlotState(0, -1));
    CHECK(!outer.Release(native, true));
    CHECK(outer.Acquire(native, {100, 2}));
    native.index = 7; // native overwrite of only one member transfers ownership of the pair
    CHECK(!outer.Release(native, true) && native == vc::SlotState(100, 7));
    CHECK(outer.Acquire(native, {200, 3}));
    native.link = 300;
    CHECK(!outer.Release(native, true) && native == vc::SlotState(300, 3));
    CHECK(outer.Acquire(native, {100, 2}));
    CHECK(!outer.Release(native, false) && native == vc::SlotState(100, 2)); // replaced pawn/world
    CHECK(!outer.Acquire(native, {0, 1}) && !outer.Acquire(native, {100, -1}));
    native = {0, -1};
    CHECK(outer.Acquire(native, {100, 2}));
    CHECK(outer.Suspend(native, true) && native == vc::SlotState(0, -1));
    CHECK(!outer.Acquire(native, {400, 8}));
    CHECK(inner.Acquire(native, {200, 3}));
    CHECK(inner.Release(native, true) && native == vc::SlotState(0, -1));
    CHECK(outer.Resume(native, true) && native == vc::SlotState(100, 2));
    CHECK(outer.Suspend(native, true));
    native = {300, 9}; // native mutation during nested call: do not resume an older borrowed slot
    CHECK(!outer.Resume(native, true) && native == vc::SlotState(300, 9));
    CHECK(!outer.Release(native, true));
    CHECK(outer.Acquire(native, {100, 2}) && outer.Suspend(native, true));
    CHECK(!outer.Resume(native, false)); // destroyed/replaced owner during the nested invocation

}

void TestVerdicts() {
    vc::Settings const s;
    // Nothing between the eyes and the shooter: in the open, whatever stands farther along.
    CHECK(vc::Judge(kEyes, kShooter, std::nullopt, s).why == vc::Why::Exposed);
    // The crate's near face 0.5 m ahead, facing the player: covered, and the facing is that face's (toward the player, -X).
    vc::Block const crate{{50.0, 0.0, 104.0}, {-1.0, 0.0, 0.0}};
    vc::Verdict const covered = vc::Judge(kEyes, kShooter, crate, s);
    CHECK(covered.Covered());
    CHECK(Near(covered.coverNormal.x, -1.0) && Near(covered.coverNormal.y, 0.0) && Near(covered.coverNormal.z, 0.0));
    CHECK(Near(covered.coverM, std::hypot(50.0, 4.0) / 100.0));
    // A wall standing at an angle: its own (flattened) facing, so the game's cone tests the real wall.
    vc::Block const angled{{60.0, 10.0, 100.0}, {-0.8, -0.6, 0.0}};
    vc::Verdict const a = vc::Judge(kEyes, kShooter, angled, s);
    CHECK(a.Covered() && Near(a.coverNormal.x, -0.8) && Near(a.coverNormal.y, -0.6));
    // A surface reporting its back (normal away from the player) is turned toward the player.
    vc::Block const back{{50.0, 0.0, 100.0}, {1.0, 0.0, 0.0}};
    CHECK(Near(vc::Judge(kEyes, kShooter, back, s).coverNormal.x, -1.0));
    // A slightly tilted wall still counts as a wall (its z under topNormalZ), flattened.
    vc::Block const tilted{{50.0, 0.0, 100.0}, {-0.8, 0.0, 0.6}};
    vc::Verdict const t = vc::Judge(kEyes, kShooter, tilted, s);
    CHECK(t.Covered() && Near(t.coverNormal.x, -1.0) && Near(t.coverNormal.z, 0.0));
    // Blocked far from the eyes (the shooter's own cover, a pillar halfway): not the player's cover.
    vc::Block const far{{700.0, 0.0, 130.0}, {-1.0, 0.0, 0.0}};
    vc::Verdict const f = vc::Judge(kEyes, kShooter, far, s);
    CHECK(f.why == vc::Why::CoverTooFar && f.coverM > s.reachM);
    // Just inside and just outside the reach.
    CHECK(vc::Judge(kEyes, kShooter, vc::Block{{119.0, 0.0, 100.0}, {-1.0, 0.0, 0.0}}, s).Covered());
    CHECK(vc::Judge(kEyes, kShooter, vc::Block{{121.0, 0.0, 100.0}, {-1.0, 0.0, 0.0}}, s).why == vc::Why::CoverTooFar);
    // The crate's top hit (a shooter above, the head just under the top): the facing comes from the shot, flattened.
    vc::Vec3 const above{900.0, 300.0, 600.0};
    vc::Verdict const top = vc::Judge(kEyes, above, vc::Block{{40.0, 13.0, 115.0}, {0.0, 0.0, 1.0}}, s);
    CHECK(top.Covered());
    double const len = std::hypot(-900.0, -300.0);
    CHECK(Near(top.coverNormal.x, -900.0 / len) && Near(top.coverNormal.y, -300.0 / len) && Near(top.coverNormal.z, 0.0));
    // A shooter straight above: no cover's facing.
    CHECK(vc::Judge(kEyes, vc::Vec3{0.0, 0.0, 900.0}, vc::Block{{0.0, 0.0, 150.0}, {0.0, 0.0, -1.0}}, s).why == vc::Why::Overhead);
    // No shooter, or the setting off.
    CHECK(vc::Judge(kEyes, std::nullopt, crate, s).why == vc::Why::NoShooter);
    vc::Settings off;
    off.on = false;
    CHECK(vc::Judge(kEyes, kShooter, crate, off).why == vc::Why::Off);
    // Other world scales: the reach is in metres.
    vc::Settings half;
    half.unitsPerMeter = 50.0;
    CHECK(vc::Judge(kEyes, kShooter, vc::Block{{55.0, 0.0, 100.0}, {-1.0, 0.0, 0.0}}, half).Covered());   // 1.1 m
    CHECK(vc::Judge(kEyes, kShooter, vc::Block{{65.0, 0.0, 100.0}, {-1.0, 0.0, 0.0}}, half).why == vc::Why::CoverTooFar);  // 1.3 m
    CHECK(std::string_view(vc::Name(vc::Why::Covered)) == "covered" && std::string_view(vc::Name(vc::Why::Exposed)) == "exposed");
}

void TestGameCone() {
    vc::Vec3 const normal{-1.0, 0.0, 0.0};  // the cover faces the player at -X; the enemy is beyond it, at +X
    vc::Vec3 const hit{0.0, 0.0, 100.0};
    // Straight from the front, 15 m: cut.
    CHECK(vc::GameCone(normal, hit, kShooter));
    // From the front but 60 degrees off the normal: still inside (cos 60 = 0.5 > 0.3).
    CHECK(vc::GameCone(normal, hit, vc::Vec3{1500.0 * 0.5, 1500.0 * std::sqrt(3.0) / 2.0, 100.0}));
    // 80 degrees off: outside the cone (cos 80 = 0.17 < 0.3).
    double const r80 = 80.0 * 3.14159265358979 / 180.0;
    CHECK(!vc::GameCone(normal, hit, vc::Vec3{1500.0 * std::cos(r80), 1500.0 * std::sin(r80), 100.0}));
    // From the side and from behind: not cut.
    CHECK(!vc::GameCone(normal, hit, vc::Vec3{0.0, 1500.0, 100.0}));
    CHECK(!vc::GameCone(normal, hit, vc::Vec3{-1500.0, 0.0, 100.0}));
    // From the front but within 3 m: not cut (the games' 300 units).
    CHECK(!vc::GameCone(normal, hit, vc::Vec3{290.0, 0.0, 100.0}));
    CHECK(vc::GameCone(normal, hit, vc::Vec3{310.0, 0.0, 100.0}));
    // The verdict's facing passes the game's cone for the shooter it was judged against.
    vc::Verdict const v = vc::Judge(kEyes, kShooter, vc::Block{{50.0, 0.0, 104.0}, {-1.0, 0.0, 0.0}}, vc::Settings{});
    CHECK(vc::GameCone(v.coverNormal, kEyes, kShooter));
}

// Behind cover outside a shot (the shields' faster recharge on ME2): hidden from every awake hostile, by the player's own
// cover from at least one.
void TestShelter() {
    using W = vc::Why;
    using S = vc::ShelterWhy;
    auto const shelter = [](std::initializer_list<W> verdicts) {
        vc::ShelterCount count;
        for (W w : verdicts) {
            vc::Add(count, w);
        }
        return vc::Shelter(count);
    };
    CHECK(shelter({}) == S::NoHostile);
    CHECK(shelter({W::NoShooter, W::Off}) == S::NoHostile);         // nobody judged
    CHECK(shelter({W::Covered}) == S::Sheltered);
    CHECK(shelter({W::Covered, W::CoverTooFar}) == S::Sheltered);   // a far wall hides the other one: still sheltered
    CHECK(shelter({W::Covered, W::Exposed}) == S::Exposed);         // a second hostile sees the head
    CHECK(shelter({W::Covered, W::Overhead}) == S::Exposed);        // one straight above: no facing, counted as open
    CHECK(shelter({W::CoverTooFar, W::CoverTooFar}) == S::NoCloseCover);  // hidden, but not by the player's own cover
    vc::ShelterCount count;
    vc::Add(count, W::Covered);
    vc::Add(count, W::CoverTooFar);
    vc::Add(count, W::NoShooter);
    CHECK(count.hostiles == 2 && count.covered == 1 && count.blockedFar == 1 && count.exposed == 0);
    CHECK(std::string_view(vc::Name(S::Sheltered)) == "behind cover");
}

// The shields' wait from the game's timer: a hit raises it, it counts down, the recharge begins at 0.
void TestRegenWait() {
    vc::RegenWait w;
    double t = 1.0;
    CHECK(!vc::Step(w, t, 0.0f, false));                 // nothing yet
    t += 0.1;
    CHECK(!vc::Step(w, t, 3.0f, false) && w.waiting);    // a hit: a 3 s wait begins
    // 1.5x as fast while sheltered (ME2's extra 0.5 dt): the timer reaches 0 after 2 s.
    std::optional<vc::RegenWaitDone> done;
    float timer = 3.0f;
    for (int i = 0; i < 60 && !done; ++i) {
        t += 0.05;
        timer = std::max(0.0f, timer - 0.075f);
        done = vc::Step(w, t, timer, true);
    }
    CHECK(done.has_value());
    if (done) {
        CHECK(Near(done->waitS, 2.0, 0.051));
        CHECK(Near(done->shelteredS, 2.0, 0.051));
        CHECK(Near(done->startTimer, 3.0, 1e-6));
    }
    CHECK(!w.waiting);
    // Reported once; a second hit mid-wait starts it again and reports only the second.
    CHECK(!vc::Step(w, t + 0.05, 0.0f, false));
    CHECK(!vc::Step(w, t + 0.1, 3.0f, false));
    CHECK(!vc::Step(w, t + 1.1, 2.0f, false));
    CHECK(!vc::Step(w, t + 1.2, 3.0f, false));  // hit again
    std::optional<vc::RegenWaitDone> second = vc::Step(w, t + 4.2, 0.0f, false);
    CHECK(second.has_value() && Near(second->waitS, 3.0, 1e-6) && Near(second->shelteredS, 0.0, 1e-9));
    // A non-finite timer reads as 0 and ends nothing that didn't start.
    vc::RegenWait fresh;
    CHECK(!vc::Step(fresh, 1.0, std::numeric_limits<float>::quiet_NaN(), true));
}

// The level look (the sequels, research/23 section 7.7): the games' own cone ignores the shooter's height, so a drone above a
// barrier the ducked head is behind still counts as hidden from, by the flat line toward it.
void TestLevelLook() {
    vc::Settings const s;  // reach 1.2 m at 100 units per metre
    // A drone 15 m away, 2 m above the eyes; a barrier 0.5 m ahead whose top is 5 cm above the eyes.
    vc::Vec3 const drone{1500.0, 0.0, kEyes.z + 200.0};
    std::optional<vc::LevelLine> const line = vc::Level(kEyes, drone, s);
    CHECK(line.has_value());
    if (line) {
        // From the eyes, flat, reachM toward the drone.
        CHECK(Near(line->from.x, kEyes.x) && Near(line->from.y, kEyes.y) && Near(line->from.z, kEyes.z));
        CHECK(Near(line->to.x, 120.0) && Near(line->to.y, 0.0) && Near(line->to.z, kEyes.z));
    }
    // The eye line climbs over the barrier's top (6.7 cm up at 0.5 m): nothing on it, in the open by the eye line.
    vc::Verdict const byLine = vc::Judge(kEyes, drone, std::nullopt, s);
    CHECK(byLine.why == vc::Why::Exposed);
    // The flat line meets the barrier's face 0.5 m ahead: covered, the face's facing for the game's cone.
    vc::Block const face{{50.0, 0.0, kEyes.z}, {-1.0, 0.0, 0.0}};
    vc::Verdict const byLevel = vc::Judge(kEyes, drone, face, s);
    CHECK(byLevel.Covered() && Near(byLevel.coverNormal.x, -1.0) && Near(byLevel.coverM, 0.5));
    // The rules: the eye line alone keeps the old verdict, the level look alone or either take the barrier.
    CHECK(vc::Choose(vc::Rule::Line, byLine, byLevel).why == vc::Why::Exposed);
    CHECK(vc::Choose(vc::Rule::Level, byLine, byLevel).Covered());
    CHECK(vc::Choose(vc::Rule::Either, byLine, byLevel).Covered());
    // Covered by the eye line (a high wall): either keeps the eye line's own verdict and facing.
    vc::Verdict const wall = vc::Judge(kEyes, drone, vc::Block{{40.0, 0.0, kEyes.z + 3.0}, {-0.8, -0.6, 0.0}}, s);
    vc::Verdict const chosen = vc::Choose(vc::Rule::Either, wall, byLevel);
    CHECK(chosen.Covered() && Near(chosen.coverNormal.x, -0.8) && Near(chosen.coverNormal.y, -0.6));
    // The eyes over the barrier's top: the flat line finds nothing either, in the open by both (the game's "leaning out").
    vc::Verdict const nothing = vc::Judge(kEyes, drone, std::nullopt, s);
    CHECK(vc::Choose(vc::Rule::Either, byLine, nothing).why == vc::Why::Exposed);
    // A drop below the eyes: the flat line starts and runs that much lower.
    std::optional<vc::LevelLine> const low = vc::Level(kEyes, drone, s, 10.0);
    CHECK(low && Near(low->from.z, kEyes.z - 10.0) && Near(low->to.z, kEyes.z - 10.0) && Near(low->to.x, 120.0));
    // Straight above or no shooter: no flat line.
    CHECK(!vc::Level(kEyes, vc::Vec3{0.0, 0.0, 900.0}, s));
    CHECK(!vc::Level(kEyes, std::nullopt, s));
    // The world's scale: reachM in metres.
    vc::Settings half;
    half.unitsPerMeter = 50.0;
    std::optional<vc::LevelLine> const scaled = vc::Level(kEyes, vc::Vec3{0.0, -900.0, kEyes.z}, half);
    CHECK(scaled && Near(scaled->to.x, 0.0) && Near(scaled->to.y, -60.0));
    // The setting's words.
    CHECK(vc::ParseRule("either") == vc::Rule::Either && vc::ParseRule("line") == vc::Rule::Line &&
          vc::ParseRule("level") == vc::Rule::Level && !vc::ParseRule("sideways"));
    CHECK(std::string_view(vc::Name(vc::Rule::Either)) == "either");
}

} // namespace

int RunVrCoverTests() {
    gFailures = 0;
    TestSlotLease();
    TestVerdicts();
    TestGameCone();
    TestShelter();
    TestRegenWait();
    TestLevelLook();
    std::printf("vr cover: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
