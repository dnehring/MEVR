// SPDX-License-Identifier: MIT
// Offline tests for VR cover (core/vr/VrCover, research/23 section 6): when a VR player's head is hidden behind cover from
// a shooter, and the cover's facing the games' own damage code then tests its cone against.

#include "core/vr/VrCover.hpp"

#include <cmath>
#include <cstdio>
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

} // namespace

int RunVrCoverTests() {
    gFailures = 0;
    TestVerdicts();
    TestGameCone();
    std::printf("vr cover: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
