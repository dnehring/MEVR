// SPDX-License-Identifier: MIT
#include "core/vr/FloorLock.hpp"
#include "core/vr/CombatStance.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/Calibration.hpp"
#include "core/ik/BodyScale.hpp"
#include "engine/XrToUnreal.hpp"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>

namespace {
namespace fl = mevr::vr::floor_lock;
namespace floor = mevr::vr::floor;
int gFailures = 0;
void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/FloorLockTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)
bool Close(double a, double b) { return std::abs(a - b) < 1e-8; }
fl::Context Ordinary() {
    fl::Context c;
    c.world = c.firstPerson = true;
    c.pawn = {true, true, false, true, true, 1234.0, 1000};
    return c;
}
floor::State Sample(float height = 1.845f) {
    floor::State s;
    s.valid = true;
    s.eyeHeight = height;
    s.source = floor::Source::Stage;
    s.timeMs = 1000;
    return s;
}
}

int RunFloorLockTests() {
    namespace cal = mevr::vr::calibration;
    using Position = cal::PlayPosition;
    CHECK(cal::ParsePosition("") == Position::Standing); // old calibration
    CHECK(cal::ParsePosition("future-value") == Position::Standing);
    CHECK(cal::ParsePosition(" SeAtEd ") == Position::Seated);
    CHECK(cal::PhysicalCrouch(Position::Standing, true));
    CHECK(!cal::PhysicalCrouch(Position::Standing, false));
    CHECK(!cal::PhysicalCrouch(Position::Seated, true));
    CHECK(!cal::PhysicalCrouch(Position::Seated, false));
    CHECK(!cal::CrouchFromHeights(1.84, 1.05, 0.5, 0.35, Position::Seated));
    CHECK(cal::CrouchFromHeights(1.84, 1.05).has_value()); // standing thresholds unchanged
    CHECK(cal::StandingEye(Position::Seated, 1.84, 1.67) == 1.84); // earlier standing/entered value wins
    CHECK(cal::StandingEye(Position::Seated, std::nullopt, 1.67) == 1.67); // no shrinking to the chair
    CHECK(cal::StandingEye(Position::Seated, NAN, 1.67) == 1.67);
    CHECK(cal::StandingEye(Position::Seated, -1.0, 1.67) == 1.67);
    CHECK(!cal::StandingEye(Position::Seated, std::nullopt, NAN)); // wait for actual Shepard, no guessed height
    CHECK(!cal::StandingEye(Position::Standing, std::nullopt, 1.67)); // standing still learns its own STAGE ref
    CHECK(cal::StandingEye(Position::Standing, 1.84, 1.67) == 1.84);
    fl::StandingReference seatedReference;
    for (int i = 0; i < 24; ++i) seatedReference.Feed(1, false, false, true, 0.65, 1.67, 0.05, true);
    CHECK(!seatedReference.Player()); // seated-only players never supply a standing STAGE height
    CHECK(seatedReference.Shepard() && Close(*seatedReference.Shepard(), 1.67));
    auto const seatedHeight = cal::StandingEye(Position::Seated, std::nullopt, seatedReference.Shepard());
    CHECK(mevr::ik::body_scale::Factor(*seatedHeight, *seatedReference.Shepard(), 1.0) == 1.0);

    cal::SeatedMotion motion;
    CHECK(motion.Eye(Position::Seated, 1.67, 1.1, 0.0, 1, 0) == 1.67);
    auto const lowered = motion.Eye(Position::Seated, 1.67, 0.9, -0.2, 1, 0);
    CHECK(lowered && Close(*lowered, 1.47)); // live lowering stays 1:1, never a new body scale
    auto seated = Ordinary();
    seated.pawn.seated = true;
    auto const translated = fl::Correct(seated, Sample(0.9f), 1000, 1355, -74.5, 100, lowered);
    CHECK(translated.active && Close(translated.centreZ, seated.pawn.floorZ + 147.0));
    CHECK(!fl::Correct(seated, Sample(0.9f), 1000, 1355, -74.5, 100).active); // never use low floor as standing
    CHECK(motion.Eye(Position::Seated, 1.67, 0.9, 0.0, 1, 1) == 1.67); // recenter seated neutral
    CHECK(motion.Eye(Position::Seated, 1.67, 1.2, 0.0, 2, 1) == 1.67); // new session
    CHECK(motion.Eye(Position::Seated, 1.84, std::nullopt, 0.0, 2, 1) == 1.84); // no-floor fallback
    auto const noFloor = motion.Eye(Position::Seated, 1.84, std::nullopt, 0.1, 2, 1);
    CHECK(noFloor && Close(*noFloor, 1.94));
    CHECK(fl::Correct(seated, {}, 1000, 1400, -4.1, 100, noFloor).active);
    seated.menu = true;
    CHECK(!fl::Correct(seated, {}, 1000, 1400, -4.1, 100, noFloor).active);
    CHECK(!motion.Eye(Position::Standing, 1.84, 1.2, 0.0, 2, 1));
    CHECK(motion.Eye(Position::Seated, 1.67, 1.0, 0.0, 2, 1) == 1.67); // position change resets neutral
    motion.Reset();
    CHECK(!motion.Eye(Position::Seated, std::nullopt, 1.1, 0.0, 1, 0)); // neutral before Shepard reference ready
    CHECK(motion.Eye(Position::Seated, 1.67, 1.0, -0.1, 1, 0) &&
        Close(*motion.Eye(Position::Seated, 1.67, 1.0, -0.1, 1, 0), 1.57));

    // Review context#1: rebuilding a corrected view cannot renew the source XR sample's age.
    namespace hv = mevr::vr::host_view;
    hv::State view;
    view.valid = true;
    std::uint64_t const sampledAt = GetTickCount64() - 1000;
    for (int i = 0; i < 10; ++i) {
        hv::Publish(view, sampledAt);
        CHECK(!hv::Latest().valid);
        CHECK(hv::Latest().timeMs == sampledAt);
    }
    hv::Publish(view);
    CHECK(hv::Latest().valid);
    hv::Reset();
    CHECK(!hv::Latest().valid && hv::Latest().timeMs == 0);
    // Regression: mode 7 renders the held camera without a fresh floor correction. Its base must carry the
    // on-foot centre, while IK still maps the captured frame there. Include pitch/roll and a changed capture rotation.
    for (auto const rotation : {mevr::engine::UnrealRotation{}, {5200, 16000, -2400}, {-7000, -10000, 3300}}) {
        auto const basis = mevr::engine::ToBasis(rotation);
        hv::State foot;
        foot.valid = foot.centreValid = foot.floorLocked = true;
        foot.pawnFloorZ = 1000;
        foot.centreZ = 1187.9;
        foot.offsetForward = -33.7f;
        foot.offsetRight = 1.7f;
        foot.offsetUp = -4.1f;
        double const raw = 1160;
        double const common = basis.right.z * foot.offsetRight + basis.up.z * foot.offsetUp + basis.forward.z * foot.offsetForward;
        double const heldBase = hv::HoldBaseZ(foot, raw, basis.right.z, basis.up.z, basis.forward.z);
        auto menu = Ordinary();
        menu.pawn.onFoot = false; // FloorLockSample in mode 7
        auto const held = fl::Correct(menu, Sample(), 1000, heldBase, common, 100);
        CHECK(!held.active && held.correction == 0);
        CHECK(Close(held.centreZ, foot.centreZ)); // render eye == captured IK eye; no doubled correction
        auto const old = fl::Correct(menu, Sample(), 1000, raw, common, 100);
        CHECK(std::abs(old.centreZ - foot.centreZ) > 1.0); // the reported high arms, relative to the lower camera
        foot.floorLocked = false;
        CHECK(Close(hv::HoldBaseZ(foot, raw, basis.right.z, basis.up.z, basis.forward.z), raw));
        foot.floorLocked = true;
        foot.valid = false;
        CHECK(Close(hv::HoldBaseZ(foot, raw, basis.right.z, basis.up.z, basis.forward.z), raw));
    }
    auto c = Ordinary();
    auto s = Sample();
    // The real camera chain: animated hook bone + mode hook - rotated mode offset, then Halcyon's framing + recentered lean.
    // Shipped framing: up -0.082 * 50 = -4.1 uu, not -8.2. Pitch can project forward framing into vertical motion.
    for (double units : {50.0, 100.0, 120.0}) {
        for (double recenterHeight : {0.8, 1.3, 1.845, 2.05}) {
            for (double boneZ : {c.pawn.floorZ + 55.0, c.pawn.floorZ + 78.0, c.pawn.floorZ + 97.0}) {
                for (double pitch : {-0.7, 0.0, 0.5}) {
                    for (double modeUp : {25.1, 51.0, 96.0}) {
                        double const raw = boneZ + 120.0 - (-50.1 * std::sin(pitch) + modeUp * std::cos(pitch));
                        double const common = (-50.0 * 0.674) * std::sin(pitch) +
                            (-4.1 + units * (s.eyeHeight - recenterHeight)) * std::cos(pitch);
                        auto const result = fl::Correct(c, s, 1000, raw, common, units);
                        CHECK(result.active);
                        CHECK(Close((raw + common + result.correction - c.pawn.floorZ) / units, s.eyeHeight));
                        // A pitched/rolled eye pair's vertical offsets may be nonzero; common correction preserves the full separation.
                        double const eyeL = raw + common - 1.2;
                        double const eyeR = raw + common + 1.2;
                        CHECK(Close((eyeR + result.correction) - (eyeL + result.correction), eyeR - eyeL));
                        CHECK(Close(((eyeL + result.correction + eyeR + result.correction) * 0.5 - c.pawn.floorZ) / units, s.eyeHeight));
                        double const separationBefore = std::hypot(6.4, eyeR - eyeL);
                        CHECK(Close(std::hypot(6.4, (eyeR + result.correction) - (eyeL + result.correction)), separationBefore));
                        // Held world cameras carry the centre minus common framing; adding framing once recovers the same eye.
                        double const heldBase = result.centreZ - common;
                        CHECK(Close(heldBase + common, result.centreZ));
                    }
                }
            }
        }
    }
    auto off = [&](fl::Context const& gated) {
        auto const result = fl::Correct(gated, s, 1000, 1400.0, -4.1, 100.0);
        CHECK(!result.active && result.correction == 0.0 && result.centreZ == 1395.9);
    };
    c.enabled = false; off(c); c = Ordinary(); // setting 0: old behaviour, including framing
    c.pawn.onFoot = false; off(c); c = Ordinary(); // Mako and every non-on-foot mode
    c.pawn.pawn = false; off(c); c = Ordinary();
    c.pawn.cover = true; off(c); c = Ordinary();
    c.firstPerson = false; off(c); c = Ordinary(); // K
    c.cutscene = true; off(c); c = Ordinary();
    c.screen = true; off(c); c = Ordinary(); // 3D screen
    c.frontEnd = true; off(c); c = Ordinary();
    c.galaxyCamera = true; off(c); c = Ordinary(); // galaxy's own scene camera
    c.menu = true; off(c); c = Ordinary();
    c.world = false; off(c); c = Ordinary(); // flat screens, missing tracking
    c.pawn.cameraOwned = false; off(c); c = Ordinary(); // scope, director, unknown mode
    c.pawn.floorValid = false; off(c); c = Ordinary();
    c.pawn.timeMs = 749; off(c); c = Ordinary();
    c.pawn.floorZ = std::numeric_limits<double>::quiet_NaN(); off(c); c = Ordinary();
    for (auto source : {floor::Source::None, static_cast<floor::Source>(99)}) {
        s.source = source;
        CHECK(!fl::Correct(c, s, 1000, 1400, 0, 100).active);
    }
    s = Sample(); s.valid = false; CHECK(!fl::Correct(c, s, 1000, 1400, 0, 100).active);
    s = Sample(); s.timeMs = 749; CHECK(!fl::Correct(c, s, 1000, 1400, 0, 100).active);
    s = Sample(); s.timeMs = 1001; CHECK(!fl::Correct(c, s, 1000, 1400, 0, 100).active);
    for (float bad : {0.0f, -1.0f, 4.0f, std::numeric_limits<float>::quiet_NaN()}) {
        s = Sample(bad); CHECK(!fl::Correct(c, s, 1000, 1400, 0, 100).active);
    }
    s = Sample(); s.source = floor::Source::LocalFloor;
    CHECK(fl::Correct(c, s, 1250, 1400, 0, 100).active);
    CHECK(!fl::Correct(c, s, 1251, 1400, 0, 100).active);
    CHECK(!fl::Correct(c, s, 1000, 1400, 0, 0).active);
    CHECK(!fl::Correct(c, s, 1000, NAN, 0, 100).active);

    // constantEye's horizontal position is unaffected: the lock returns only a world Z correction.
    namespace cs = mevr::vr::combat_stance;
    auto const eye = cs::ExploreEye({0, 0, 120}, {-50.1, 0, 25.1});
    CHECK(Close(eye.x, 50.1) && Close(eye.y, 0.0));
    CHECK(cs::ConstantEyeApplies("SFXCameraMode_CombatCrouch", "Crouch", true));

    fl::StandingReference standing;
    for (int i = 0; i < 20; ++i) {
        standing.Feed(1, true, false, true, 1.1, 1.2, 0.05);
        standing.Feed(1, false, true, true, 1.845, 1.2, 0.05);
        standing.Feed(1, false, false, false, 1.845, 1.5, 0.05);
    }
    CHECK(!standing.Player() && !standing.Shepard());
    for (int i = 0; i < 24; ++i) standing.Feed(1, false, false, true, 1.845, 1.665, 0.05);
    CHECK(standing.Player() && Close(*standing.Player(), 1.845));
    CHECK(standing.Shepard() && Close(*standing.Shepard(), 1.665));
    auto const target = mevr::ik::body_scale::Factor(*standing.Player(), *standing.Shepard(), 1.0);
    CHECK(target && Close(*target, 1.845 / 1.665));
    auto const tall = fl::Correct(c, Sample(), 1000, 1400, -4.1, 100);
    auto const low = fl::Correct(c, Sample(1.1f), 1000, 1355, -74.5, 100); // physical crouch AND synthetic game drop
    CHECK(Close((tall.centreZ - low.centreZ) / 100.0, static_cast<double>(Sample().eyeHeight) - Sample(1.1f).eyeHeight));
    auto const button = fl::Correct(c, Sample(), 1000, 1355, -4.1, 100);
    CHECK(Close(button.centreZ, tall.centreZ)); // button state retained; real head alone controls the view
    for (int i = 0; i < 100; ++i) {
        standing.Feed(1, true, false, true, 1.1, 1.2, 0.05);
        standing.Feed(1, false, true, true, 1.845, 1.2, 0.05);
        standing.Feed(1, false, false, true, 2.0, 1.75, 0.05); // idle animation/recenter never chases a settled target
    }
    CHECK(mevr::ik::body_scale::Factor(*standing.Player(), *standing.Shepard(), 1.0) == target);
    standing.Feed(2, true, true, true, 1.1, 1.2, 0.05);
    CHECK(!standing.Shepard()); // another skeleton needs its own standing sample
    return gFailures;
}
