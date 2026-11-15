// SPDX-License-Identifier: MIT
// Offline tests for the traversal rules (core/vr/Traversal, research/le2-traversal.md): the double A, the commit's judgement, the
// record of a move, the level view, the comfort mask and the cue.

#include "core/vr/Traversal.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/TraversalTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

bool Near(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) <= eps;
}

namespace tv = mevr::vr::traversal;

// The player standing next to an obstacle the game admits, on foot, focused, nothing selected.
tv::PressInput At(double t, bool down, bool eligible = true) {
    tv::PressInput in;
    in.nowS = t;
    in.down = down;
    in.eligible = eligible;
    return in;
}

// A press: down at `t` for `hold` seconds (ticks at 90 Hz). Returns the tap of the tick it went down.
tv::Tap Tap(tv::PressTracker& p, double& t, double hold, tv::PressInput base = At(0.0, false)) {
    tv::Tap first = tv::Tap::None;
    base.down = true;
    base.nowS = t;
    first = p.Step(base);
    for (double h = 1.0 / 90.0; h < hold; h += 1.0 / 90.0) {
        base.nowS = t + h;
        p.Step(base);
    }
    t += hold;
    base.down = false;
    base.nowS = t;
    p.Step(base);
    return first;
}

void Idle(tv::PressTracker& p, double& t, double seconds, tv::PressInput base = At(0.0, false)) {
    base.down = false;
    for (double h = 0.0; h < seconds; h += 1.0 / 90.0) {
        base.nowS = t + h;
        p.Step(base);
    }
    t += seconds;
}

void TestNames() {
    CHECK(std::string(tv::Label(tv::Kind::Vault)) == "Vault");
    CHECK(std::string(tv::Label(tv::Kind::ClimbUp)) == "Climb up");
    CHECK(std::string(tv::Label(tv::Kind::None)).empty());
    CHECK(tv::ParseView("Level") == tv::View::Level);
    CHECK(tv::ParseView("game") == tv::View::Game);
    CHECK(!tv::ParseView("sideways"));
    CHECK(tv::ParseMask(" blink ") == tv::Mask::Blink);
    CHECK(tv::ParseMask("vignette") == tv::Mask::Vignette);
    CHECK(tv::ParseMask("off") == tv::Mask::None);
    // The defaults keep today's behaviour: no cue, the game's camera, no mask.
    tv::Settings const s;
    CHECK(!s.cue && s.view == tv::View::Game && s.mask == tv::Mask::None);
    CHECK(Near(s.doubleTapS, 0.45f));
}

void TestDoubleTap() {
    // Two quick taps next to the obstacle: the second is the double tap.
    {
        tv::PressTracker p;
        double t = 0.0;
        Idle(p, t, 0.2);
        CHECK(Tap(p, t, 0.08) == tv::Tap::First);
        Idle(p, t, 0.15);
        CHECK(Tap(p, t, 0.08) == tv::Tap::Double);
        CHECK(p.Last().why == tv::PressWhy::Fresh && p.Last().serial == 2);
        // A third tap right after starts again (one move per double tap).
        Idle(p, t, 0.1);
        CHECK(Tap(p, t, 0.08) == tv::Tap::First);
    }
    // Too slow: two single presses.
    {
        tv::PressTracker p;
        double t = 0.0;
        Idle(p, t, 0.2);
        CHECK(Tap(p, t, 0.08) == tv::Tap::First);
        Idle(p, t, 0.5);
        CHECK(Tap(p, t, 0.08) == tv::Tap::First);
        // The double tap's timing is the setting's.
        tv::PressTracker q;
        double u = 0.0;
        Idle(q, u, 0.2);
        tv::PressInput base = At(0.0, false);
        base.nowS = u;
        base.down = true;
        CHECK(q.Step(base, 0.8) == tv::Tap::First);
        base.down = false;
        base.nowS = u + 0.1;
        q.Step(base, 0.8);
        base.down = true;
        base.nowS = u + 0.7;
        CHECK(q.Step(base, 0.8) == tv::Tap::Double);
    }
    // A held A never counts again: one press, however long.
    {
        tv::PressTracker p;
        double t = 0.0;
        Idle(p, t, 0.2);
        CHECK(Tap(p, t, 2.0) == tv::Tap::First);
        CHECK(p.Last().serial == 1);
    }
    // A press begun in a menu (and let go after it closed) can't begin a double tap; nor a press without input focus.
    {
        tv::PressTracker p;
        double t = 0.0;
        tv::PressInput menu = At(0.0, false);
        menu.menuOpen = true;
        Idle(p, t, 0.2, menu);
        CHECK(Tap(p, t, 0.08, menu) == tv::Tap::Other);
        CHECK(p.Last().why == tv::PressWhy::FromMenu);
        Idle(p, t, 0.1);
        CHECK(Tap(p, t, 0.08) == tv::Tap::First);  // not a double tap: the first was the menu's
        tv::PressInput unfocused = At(0.0, false);
        unfocused.focused = false;
        tv::PressTracker q;
        double u = 0.0;
        Idle(q, u, 0.2, unfocused);
        CHECK(Tap(q, u, 0.08, unfocused) == tv::Tap::Other);
        Idle(q, u, 0.1);
        CHECK(Tap(q, u, 0.08) == tv::Tap::First);
    }
    // A coming back from a menu: down while the menu was open, still held when it closes: never a press of its own.
    {
        tv::PressTracker p;
        double t = 0.0;
        tv::PressInput menu = At(0.0, false);
        menu.menuOpen = true;
        Idle(p, t, 0.2, menu);
        menu.down = true;
        menu.nowS = t;
        p.Step(menu);  // went down in the menu
        tv::PressInput open = At(t + 0.05, true);
        CHECK(p.Step(open) == tv::Tap::None);  // the menu closed, A still held: nothing
        open.down = false;
        open.nowS = t + 0.10;
        p.Step(open);
        CHECK(p.Last().serial == 1 && p.Last().why == tv::PressWhy::FromMenu);
    }
    // Something selected for Use: A uses it first; no double tap (the game's Use, its binding's first command, takes the press).
    {
        tv::PressTracker p;
        double t = 0.0;
        tv::PressInput use = At(0.0, false);
        use.useFirst = true;
        Idle(p, t, 0.2, use);
        CHECK(Tap(p, t, 0.08, use) == tv::Tap::Other);
        CHECK(p.Last().why == tv::PressWhy::UseFirst);
        Idle(p, t, 0.1, use);
        CHECK(Tap(p, t, 0.08, use) == tv::Tap::Other);
        // Selected only at the second press: no vault either.
        tv::PressTracker q;
        double u = 0.0;
        Idle(q, u, 0.2);
        CHECK(Tap(q, u, 0.08) == tv::Tap::First);
        Idle(q, u, 0.1, use);
        CHECK(Tap(q, u, 0.08, use) == tv::Tap::Other);
    }
    // Away from any obstacle: a single A does nothing of ours; a first tap a step away still pairs with a second next to it.
    {
        tv::PressTracker p;
        double t = 0.0;
        Idle(p, t, 0.2, At(0.0, false, false));
        CHECK(Tap(p, t, 0.08, At(0.0, false, false)) == tv::Tap::First);
        CHECK(p.Last().why == tv::PressWhy::NotEligible);
        Idle(p, t, 0.1, At(0.0, false, false));
        CHECK(Tap(p, t, 0.08, At(0.0, false, false)) == tv::Tap::First);  // still nothing to vault: no double tap
        Idle(p, t, 0.6);
        CHECK(Tap(p, t, 0.08, At(0.0, false, false)) == tv::Tap::First);
        Idle(p, t, 0.1);  // eligible now
        CHECK(Tap(p, t, 0.08) == tv::Tap::Double);
    }
    // Pressed while a move runs, or not on foot: never a double tap.
    {
        tv::PressTracker p;
        double t = 0.0;
        tv::PressInput moving = At(0.0, false);
        moving.traversing = true;
        Idle(p, t, 0.2, moving);
        CHECK(Tap(p, t, 0.08, moving) == tv::Tap::Other);
        CHECK(p.Last().why == tv::PressWhy::DuringTraversal);
        Idle(p, t, 0.1);
        CHECK(Tap(p, t, 0.08) == tv::Tap::First);
        tv::PressInput conversation = At(0.0, false);
        conversation.onFoot = false;
        tv::PressTracker q;
        double u = 0.0;
        Idle(q, u, 0.2, conversation);
        CHECK(Tap(q, u, 0.08, conversation) == tv::Tap::Other);
        CHECK(q.Last().why == tv::PressWhy::NotOnFoot);
    }
    // The verdict is the state before the tick: the game may already have reacted (begun a move) on the tick the press is seen.
    {
        tv::PressTracker p;
        double t = 0.0;
        Idle(p, t, 0.2);
        CHECK(Tap(p, t, 0.08) == tv::Tap::First);
        Idle(p, t, 0.1);
        tv::PressInput now = At(t, true);
        now.traversing = true;  // the move began this very tick
        CHECK(p.Step(now) == tv::Tap::Double);
    }
}

void TestJudge() {
    // The mod's double tap: allowed once; the same press can't commit a second move.
    {
        tv::PressTracker p;
        double t = 0.0;
        Idle(p, t, 0.2);
        Tap(p, t, 0.08);
        Idle(p, t, 0.1);
        double const pressAt = t;
        CHECK(Tap(p, t, 0.08) == tv::Tap::Double);
        tv::Commit const c = tv::Judge(p, pressAt + 0.01);
        CHECK(c.why == tv::CommitWhy::Allowed && c.press == 2);
        CHECK(Near(c.sincePressS, 0.01, 1e-9));
        CHECK(tv::Judge(p, pressAt + 0.02).why == tv::CommitWhy::Repeated);
    }
    // The game's own move on a single press (its in-cover A): judged a single tap, not ours.
    {
        tv::PressTracker p;
        double t = 0.0;
        Idle(p, t, 0.2);
        double const pressAt = t;
        Tap(p, t, 0.08);
        CHECK(tv::Judge(p, pressAt - 0.01).why == tv::CommitWhy::SingleTap);  // began in the engine tick before the mod saw the press
    }
    // No press near it: the game began it by itself (a script, a held button).
    {
        tv::PressTracker p;
        CHECK(tv::Judge(p, 5.0).why == tv::CommitWhy::NoPress);
        double t = 0.0;
        Idle(p, t, 0.2);
        Tap(p, t, 0.08);
        CHECK(tv::Judge(p, t + 1.0).why == tv::CommitWhy::NoPress);
    }
    // A press that couldn't commit (from a menu).
    {
        tv::PressTracker p;
        double t = 0.0;
        tv::PressInput menu = At(0.0, false);
        menu.menuOpen = true;
        Idle(p, t, 0.2, menu);
        double const pressAt = t;
        Tap(p, t, 0.08, menu);
        CHECK(tv::Judge(p, pressAt + 0.02).why == tv::CommitWhy::NotFresh);
    }
}

void TestMoveRecord() {
    tv::MoveRecord r;
    r.Begin(tv::Kind::Vault);
    CHECK(r.Active());
    // A vault as ME2 plays it (research/le2-traversal.md): the custom action 0.9 s; the pawn moved by its root motion in steps (still,
    // then 1.1 m up at once, then 2 m on at once, then down); the camera smooth, 0.5 m up and back over 1.6 s, with a 2 cm bob at 5 Hz
    // and pitched down 4 degrees at the middle; the pawn blocking no actors for the first 0.4 s; then the blend-out until 1.8 s.
    double const pi = 3.14159265358979;
    for (int i = 0; i <= 180; ++i) {
        double const t = i / 100.0;
        tv::Sample s;
        s.t = t;
        double const along = t < 0.55 ? 0.0 : 200.0;
        double const up = t < 0.36 ? 0.0 : t < 0.80 ? 110.0 : 0.0;
        s.pawn = {along, 0.0, up};
        s.pawnYaw = t < 0.5 ? 0.0 : 10.0;
        double const u = std::min(t / 1.6, 1.0);
        double const k = u * u * (3.0 - 2.0 * u);  // eased in and out, as the game's camera arrives
        s.camera = {230.0 * k, 0.0, 75.0 + 50.0 * std::sin(pi * k) + 1.0 * std::sin(2.0 * pi * 5.0 * t)};
        s.pitch = -4.0 * std::sin(pi * k);
        s.roll = i == 50 ? 2.0 : 0.0;
        s.collision = t >= 0.4;
        s.running = t <= 0.9;
        r.Add(s);
        if (i == 90) {
            r.End(0.9);
        }
    }
    CHECK(!r.Settled());  // the 5 Hz bob goes on: never at rest
    tv::Summary const s = r.Current();
    CHECK(s.kind == tv::Kind::Vault && s.samples == 181);
    CHECK(Near(s.seconds, 0.9));
    CHECK(Near(s.horizontalM, 2.0));
    CHECK(Near(s.riseM, 1.1, 1e-9));
    CHECK(Near(s.dropM, 0.0, 1e-9));
    CHECK(Near(s.yawTurnDeg, 10.0));
    CHECK(Near(s.pawnStepM, 2.0, 1e-9));  // the root motion's step
    CHECK(Near(s.pitchMinDeg, -4.0, 1e-3) && Near(s.pitchMaxDeg, 0.0, 1e-9));
    CHECK(Near(s.rollMaxDeg, 2.0) && Near(s.rollMinDeg, 0.0));
    CHECK(Near(s.collisionOffS, 0.4, 0.011));
    CHECK(Near(s.cameraTravelM, 2.3, 1e-9));
    CHECK(Near(s.cameraRiseM, 0.5, 0.011));
    CHECK(Near(s.cameraDropM, 0.0, 0.011));
    // The 2 cm peak-to-peak bob (left at about 1.2x by the 0.3 s average at 5 Hz, plus the arc's curvature), not the 0.5 m rise.
    CHECK(s.cameraBobM > 0.015 && s.cameraBobM < 0.07);
    CHECK(s.cameraPeakSpeedMS > 1.8 && s.cameraPeakSpeedMS < 2.8);
    CHECK(s.maxStepM < 0.05);
    CHECK(s.settledS < 0.0);  // a 5 Hz bob never lets it rest
    tv::Summary const finished = r.Finish();
    CHECK(!r.Active() && finished.samples == 181);
    // The same arc without the bob (as ME2's camera was measured): the bob measure stays small.
    tv::MoveRecord smooth;
    smooth.Begin(tv::Kind::Vault);
    for (int i = 0; i <= 180; ++i) {
        double const t = i / 100.0;
        double const u = std::min(t / 1.6, 1.0);
        double const k = u * u * (3.0 - 2.0 * u);
        tv::Sample a;
        a.t = t;
        a.camera = {230.0 * k, 0.0, 75.0 + 50.0 * std::sin(pi * k)};
        smooth.Add(a);
    }
    CHECK(smooth.Current().cameraBobM < 0.035);
    // A camera at rest after the end: settled, and when.
    tv::MoveRecord q;
    q.Begin(tv::Kind::ClimbUp);
    for (int i = 0; i <= 150; ++i) {
        double const t = i / 100.0;
        tv::Sample still;
        still.t = t;
        still.camera = {t < 1.0 ? 100.0 * t : 100.0, 0.0, 0.0};
        q.Add(still);
        if (i == 50) {
            q.End(0.5);
        }
    }
    CHECK(q.Settled());
    CHECK(Near(q.Current().settledS, 0.5, 0.011));
}

void TestLevelView() {
    double const pi = 3.14159265358979;
    // The game's camera through a move: a 0.5 m rise and back over 1.6 s, with a 2 cm bob at 5 Hz, pitched and rolled by the body.
    auto const game = [pi](double t) {
        tv::Pose g;
        g.location = {100.0 * t, 5.0, 75.0 + 50.0 * std::sin(pi * t / 1.6) + 1.0 * std::sin(2.0 * pi * 5.0 * t)};
        g.pitch = -6.0 * std::sin(pi * t);
        g.yaw = 10.0 + t;
        g.roll = 3.0 * std::sin(pi * t);
        return g;
    };
    tv::Pose camera;
    camera.location = {0.0, 0.0, 75.0};
    camera.yaw = 10.0;
    // Without the height filter (the default): the game's place, height and yaw; level (the start's pitch, no roll).
    tv::LevelView v;
    v.Begin(camera);
    CHECK(v.Started());
    for (int i = 1; i <= 160; ++i) {
        tv::Pose const g = game(i / 100.0);
        tv::Pose const p = v.During(g, 0.01);
        CHECK(Near(p.location.x, g.location.x) && Near(p.location.y, 5.0) && Near(p.location.z, g.location.z) && Near(p.yaw, g.yaw));
        CHECK(Near(p.pitch, 0.0) && Near(p.roll, 0.0));
    }
    // With the filter (tauS 0.03): the bob halved, the arc followed within 8 units (2 tauS times its 98 units/s at the steepest).
    tv::LevelView f;
    f.Begin(camera);
    double worstOffArc = 0.0;
    for (int i = 1; i <= 160; ++i) {
        double const t = i / 100.0;
        tv::Pose const p = f.During(game(t), 0.01, 0.03);
        if (t > 0.2) {
            worstOffArc = std::max(worstOffArc, std::abs(p.location.z - (75.0 + 50.0 * std::sin(pi * t / 1.6))));
        }
    }
    CHECK(worstOffArc < 8.0);
    tv::LevelView steady;
    steady.Begin(camera);
    double bobLeft = 0.0;
    for (int i = 1; i <= 300; ++i) {
        double const t = i / 100.0;
        tv::Pose g;
        g.location = {0.0, 0.0, 75.0 + std::sin(2.0 * pi * 5.0 * t)};
        tv::Pose const p = steady.During(g, 0.01, 0.03);
        if (t > 1.0) {
            bobLeft = std::max(bobLeft, std::abs(p.location.z - 75.0));
        }
    }
    CHECK(bobLeft < 0.6);  // a 1-unit bob at 5 Hz: about half left
    // The hand-back to the game's camera over settleS (from the last During: x 160, height about 75).
    v.End();
    tv::Pose after;
    after.location = {200.0, 5.0, 95.0};
    after.pitch = 2.0;
    after.yaw = 12.0;
    std::optional<tv::Pose> const start = v.After(after, 0.0, 0.25);
    CHECK(start && Near(start->pitch, 0.0) && std::abs(start->location.z - 75.0) < 3.0);
    std::optional<tv::Pose> const half = v.After(after, 0.125, 0.25);
    CHECK(half && Near(half->pitch, 1.0, 1e-9) && Near(half->location.x, 180.0, 1e-6));
    CHECK(!v.After(after, 0.25, 0.25));  // handed back
    CHECK(!v.Started());
    // Yaw across the seam in the hand-back: from 179 to -179 is 2 degrees, not 358.
    tv::LevelView w;
    tv::Pose seam;
    seam.yaw = 179.0;
    w.Begin(seam);
    w.During(seam, 0.01);
    w.End();
    tv::Pose other;
    other.yaw = -179.0;
    std::optional<tv::Pose> const mid = w.After(other, 0.125, 0.25);
    CHECK(mid && (Near(mid->yaw, 180.0, 1e-6) || Near(mid->yaw, -180.0, 1e-6)));
    CHECK(Near(tv::SignedDegrees(540.0), 180.0) && Near(tv::SignedDegrees(-190.0), 170.0));
}

void TestMask() {
    tv::Settings s;
    CHECK(tv::MaskAmount(tv::Mask::None, s, true, 0.3, -1.0) == 0.0f);
    s.vignetteStrength = 0.7f;
    CHECK(Near(tv::MaskAmount(tv::Mask::Vignette, s, true, 0.3, -1.0), 0.7f, 1e-6));
    CHECK(tv::MaskAmount(tv::Mask::Vignette, s, false, 1.3, 0.1) == 0.0f);
    s.blinkInS = 0.1f;
    s.blinkHoldS = 0.3f;
    s.blinkOutS = 0.2f;
    CHECK(Near(tv::MaskAmount(tv::Mask::Blink, s, true, 0.0, -1.0), 0.0f, 1e-6));
    CHECK(Near(tv::MaskAmount(tv::Mask::Blink, s, true, 0.05, -1.0), 0.5f, 1e-5));
    CHECK(Near(tv::MaskAmount(tv::Mask::Blink, s, true, 0.25, -1.0), 1.0f, 1e-6));   // held black
    CHECK(Near(tv::MaskAmount(tv::Mask::Blink, s, true, 0.5, -1.0), 0.5f, 1e-5));    // fading back while the move runs on
    CHECK(Near(tv::MaskAmount(tv::Mask::Blink, s, true, 0.7, -1.0), 0.0f, 1e-6));
    // A move that ended at 0.2 s: black until then, then back over blinkOutS.
    CHECK(Near(tv::MaskAmount(tv::Mask::Blink, s, false, 0.2, 0.0), 1.0f, 1e-6));
    CHECK(Near(tv::MaskAmount(tv::Mask::Blink, s, false, 0.3, 0.1), 0.5f, 1e-5));
    CHECK(Near(tv::MaskAmount(tv::Mask::Blink, s, false, 0.45, 0.25), 0.0f, 1e-6));
    // Ended during the fade in: from where it got to.
    CHECK(Near(tv::MaskAmount(tv::Mask::Blink, s, false, 0.15, 0.1), 0.25f, 1e-5));
}

void TestCue() {
    // The point halfway over the cover, on its top, raised.
    tv::Vec3 const p = tv::CuePoint({0.0, 0.0, 100.0}, tv::Vec3{200.0, 40.0, 100.0}, 110.0, 18.0);
    CHECK(Near(p.x, 100.0) && Near(p.y, 20.0) && Near(p.z, 128.0));
    tv::Vec3 const q = tv::CuePoint({0.0, 0.0, 100.0}, std::nullopt, std::nullopt, 18.0, 60.0);
    CHECK(Near(q.x, 0.0) && Near(q.z, 178.0));
    // The label upright, facing the head, its height the angle at that distance (clamped 4..20 cm).
    tv::Quad quad;
    CHECK(tv::PlaceCue({0.0f, 1.0f, -2.0f}, {0.0f, 1.6f, 0.0f}, 2.6f, 4.0f, quad));
    CHECK(Near(quad.pose.orientation.x, 0.0) && Near(quad.pose.orientation.z, 0.0));
    CHECK(Near(std::abs(quad.pose.orientation.w), 1.0, 1e-6));  // the head straight behind +Z: no turn
    double const distance = std::sqrt(0.36 + 4.0);
    CHECK(Near(quad.height, 2.0 * distance * std::tan(1.3 * 3.14159265358979 / 180.0), 1e-5));
    CHECK(Near(quad.width, quad.height * 4.0, 1e-6));
    CHECK(tv::PlaceCue({2.0f, 1.6f, 0.0f}, {0.0f, 1.6f, 0.0f}, 2.6f, 4.0f, quad));
    // Turned to face -X (the head): +Z rotated -90 degrees about +Y.
    CHECK(Near(quad.pose.orientation.y, -std::sin(3.14159265358979 / 4.0), 1e-6));
    CHECK(tv::PlaceCue({0.0f, 1.0f, -20.0f}, {0.0f, 1.6f, 0.0f}, 2.6f, 4.0f, quad) && Near(quad.height, 0.20f, 1e-6));
    CHECK(tv::PlaceCue({0.0f, 1.6f, -0.3f}, {0.0f, 1.6f, 0.0f}, 2.6f, 4.0f, quad) && Near(quad.height, 0.04f, 1e-6));
    CHECK(!tv::PlaceCue({0.0f, 1.6f, 0.0f}, {0.0f, 1.6f, 0.0f}, 2.6f, 4.0f, quad));  // at the head
    // When it shows.
    tv::CueInput in;
    in.enabled = true;
    in.onFoot = true;
    in.eligible = true;
    in.distanceM = 1.2;
    CHECK(tv::CueShows(in));
    tv::CueInput off = in;
    off.enabled = false;
    CHECK(!tv::CueShows(off));
    tv::CueInput use = in;
    use.useFirst = true;
    CHECK(!tv::CueShows(use));
    tv::CueInput moving = in;
    moving.traversing = true;
    CHECK(!tv::CueShows(moving));
    tv::CueInput far = in;
    far.distanceM = 3.5;
    CHECK(!tv::CueShows(far));
    tv::CueInput menu = in;
    menu.menuOpen = true;
    CHECK(!tv::CueShows(menu));
    tv::CueInput refused = in;
    refused.eligible = false;
    CHECK(!tv::CueShows(refused));
    // Published, then stale.
    tv::Reset();
    CHECK(tv::Latest().size == 0);
    tv::Cue cue;
    cue.visible = true;
    cue.kind = tv::Kind::ClimbUp;
    tv::Publish(cue);
    tv::Cue const got = tv::Latest();
    CHECK(got.size == sizeof(tv::Cue) && got.visible && got.kind == tv::Kind::ClimbUp && got.sequence > 0);
    tv::Reset();
    CHECK(!tv::Latest().visible);
}

} // namespace

int RunTraversalTests() {
    gFailures = 0;
    TestNames();
    {
        tv::ActionLifecycle a;
        CHECK(a.Request(10, 1.0));
        CHECK(!a.Request(10, 1.1)); // one pending request
        a.Observe(10, 0, tv::Kind::None, 1.1);
        CHECK(a.phase == tv::ActionPhase::Requested); // dispatch isn't a start
        a.Observe(10, 42, tv::Kind::LadderUp, 1.2);
        CHECK(a.phase == tv::ActionPhase::Started && a.kind == tv::Kind::LadderUp);
        a.End(10, 43, false, 1.3);
        CHECK(a.phase == tv::ActionPhase::Started); // another action can't finish ours
        a.End(10, 42, false, 2.0);
        CHECK(a.phase == tv::ActionPhase::Completed);
        a.End(10, 42, true, 2.0); // native interrupt wraps common end
        CHECK(a.phase == tv::ActionPhase::Interrupted);
        a.End(10, 42, false, 2.0);
        CHECK(a.phase == tv::ActionPhase::Interrupted); // a later common callback cannot hide interruption
        CHECK(a.Request(10, 3.0) && a.serial == 2);
        a.Observe(10, 0, tv::Kind::None, 3.6);
        CHECK(a.phase == tv::ActionPhase::NoStartObserved);
        CHECK(a.Request(10, 4.0));
        a.Observe(10, 50, tv::Kind::Vault, 4.1);
        a.Observe(10, 0, tv::Kind::None, 4.3);
        CHECK(a.phase == tv::ActionPhase::EndedUnknown); // disappearing doesn't prove completion
        CHECK(a.Request(10, 5.0));
        a.Observe(10, 51, tv::Kind::Evade, 5.1);
        a.Observe(11, 51, tv::Kind::Evade, 5.2); // same pointer after load is a different context
        CHECK(a.phase == tv::ActionPhase::Cancelled && a.action == 0);
        a.End(10, 51, false, 5.3);
        CHECK(a.phase == tv::ActionPhase::Cancelled);
        CHECK(std::string(tv::Label(tv::Kind::Ladder)) == "Use ladder");
    }
    TestDoubleTap();
    // Scene entry on the second press cannot use the previous on-foot sample to authorize it.
    {
        tv::PressTracker p;
        p.Step(At(0.0, false));
        CHECK(p.Step(At(0.05, true)) == tv::Tap::First);
        p.Step(At(0.1, false));
        auto seated = At(0.15, true);
        seated.authoredSeated = true;
        CHECK(p.Step(seated) == tv::Tap::Other);
        CHECK(p.Last().why == tv::PressWhy::AuthoredSeated);
        CHECK(tv::Judge(p, 0.15).why == tv::CommitWhy::NotFresh);
        p.Step(At(0.2, false));
        CHECK(p.Step(At(0.25, true)) == tv::Tap::First);
    }
    // A menu/seat interruption between taps consumes the pending first tap even without a button edge.
    for (bool seat : {false, true}) {
        tv::PressTracker p;
        p.Step(At(0.0, false));
        p.Step(At(0.05, true));
        p.Step(At(0.1, false));
        auto interrupted = At(0.12, false);
        interrupted.authoredSeated = seat;
        interrupted.menuOpen = !seat;
        p.Step(interrupted);
        p.Step(At(0.14, false));
        CHECK(p.Step(At(0.16, true)) == tv::Tap::First);
    }
    {
        tv::PressTracker p;
        p.Step(At(0.0, false));
        p.Step(At(0.05, true));
        p.Step(At(0.1, false));
        CHECK(p.Step(At(0.15, true)) == tv::Tap::Double);
        auto seated = At(0.16, true);
        seated.authoredSeated = true;
        p.Step(seated); // no new button edge, but the pending double cannot survive the scene
        CHECK(tv::Judge(p, 0.16).why == tv::CommitWhy::NotFresh);
    }
    // A native 90-degree turn must not rotate the player's view at action end.
    // The accumulated basis survives recovery; later player turns remain one-for-one.
    {
        tv::AuthoredYaw yaw;
        yaw.Begin(20);
        CHECK(Near(yaw.Update(110, true), 20));
        CHECK(Near(yaw.Offset(), -90));
        CHECK(Near(yaw.Update(110, false), 20));
        CHECK(!yaw.Holding());
        CHECK(Near(yaw.Update(140, false), 50));
        yaw.Begin(140); // another authored action starts from the already corrected basis
        CHECK(Near(yaw.Update(50, true), 50));
        CHECK(Near(yaw.Update(50, false), 50));
        yaw.Reset(); // explicit new scene/reference space
        CHECK(Near(yaw.Update(75, false), 75));
    }
    {
        tv::AuthoredYaw yaw;
        yaw.Begin(170);
        CHECK(Near(yaw.Update(-175, true), 170));
        CHECK(Near(yaw.Offset(), -15));
        CHECK(Near(yaw.Update(-175, false), 170));
        CHECK(Near(yaw.Update(-165, false), 180));
    }
    {
        tv::Presentation p;
        p.flags = tv::kPresentationActive;
        p.sceneGeneration = 5; p.timeMs = 1000; p.sessions = 2; p.recenters = 7; p.yawOffsetUU = -16384;
        CHECK(tv::PresentationFresh(p, 1100, 7, 2));
        CHECK(!tv::PresentationFresh(p, 1101, 7, 2));
        CHECK(!tv::PresentationFresh(p, 999, 7, 2));
        CHECK(!tv::PresentationFresh(p, 1000, 8, 2));
        CHECK(!tv::PresentationFresh(p, 1000, 7, 3));
        p.flags |= 4;
        CHECK(!tv::PresentationFresh(p, 1000, 7, 2));
        p.flags = tv::kPresentationActive; p.yawOffsetUU = 32769;
        CHECK(!tv::PresentationFresh(p, 1000, 7, 2));
        p.yawOffsetUU = 0; p.version = 0;
        CHECK(!tv::PresentationFresh(p, 1000, 7, 2));
        p.version = tv::kPresentationVersion; --p.size;
        CHECK(!tv::PresentationFresh(p, 1000, 7, 2));
    }
    TestJudge();
    TestMoveRecord();
    TestLevelView();
    TestMask();
    TestCue();
    std::printf("traversal tests: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
