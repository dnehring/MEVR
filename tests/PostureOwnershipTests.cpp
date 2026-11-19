// SPDX-License-Identifier: MIT
#include "core/ik/Posture.hpp"
#include <cmath>
#include <cstdio>
#include <optional>

int RunPostureOwnershipTests() {
    using namespace mevr::ik::posture;
    int failures = 0;
    auto check = [&](bool ok, char const* what) {
        if (!ok) { ++failures; std::printf("posture ownership: %s\n", what); }
    };
    CrouchArbiter arbiter;
    CrouchDetector detector;
    arbiter.Update({CrouchEvent::Crouch, false, true});
    arbiter.Applied(true);
    // Each ownership/recenter transition resets pending physical input, but the same
    // pawn remains crouched. Standing after reopening must still request a release.
    for (int transition = 0; transition != 4; ++transition) {
        arbiter.Update({CrouchEvent::Stand, true, false});
        arbiter.ResetTracking(detector, true);
        check(arbiter.Owns() && detector.Crouched() && !arbiter.Pending(), "menu/recenter lost native ownership");
    }
    arbiter.ResetTracking(detector, std::nullopt);
    check(arbiter.Owns() && detector.Crouched(), "missing native sample is not an external stand");
    auto event = detector.Update(1.0, 0.25, CrouchParams{});
    check(event == CrouchEvent::Stand, "standing after tracking reset must create a stand edge");
    check(arbiter.Update({event, true, true}).action == CrouchAction::Stand, "stand edge must reach native adapter");
    // Native admission may refuse during an action. Neither refusal nor an unreadable
    // field completes unload; a later accepted/native-observed release does.
    check(!arbiter.FinishRelease(true) && arbiter.Owns(), "refused release must retain ownership");
    check(!arbiter.FinishRelease(std::nullopt) && arbiter.Owns(), "unknown release must retain ownership");
    check(arbiter.FinishRelease(false) && !arbiter.Owns(), "observed stand should complete release");
    arbiter.Update({CrouchEvent::Crouch, false, true});
    arbiter.Applied(true);
    arbiter.ResetTracking(detector, false); // script stood the same pawn up
    check(!arbiter.Owns() && !detector.Crouched(), "external stand must end our ownership");
    arbiter.Update({CrouchEvent::Crouch, false, true});
    arbiter.Applied(true);
    arbiter.Forget(); // actual world/pawn replacement
    check(arbiter.FinishRelease(std::nullopt), "new pawn must never inherit a restoration");

    // No cover snapping, one rule for the three games: refused whenever VR is on foot with [Hands] noCover. The rule takes
    // nothing else, so a hand wheel, a wrist screen, a held view, a roll's or a cast's end or a late tick can't lift it.
    CoverSnapInput onFoot{true, true, true, false};
    check(RefuseCoverSnap(onFoot), "VR on foot with noCover must refuse the snap");
    CoverSnapInput in = onFoot;
    in.noCover = false;
    check(!RefuseCoverSnap(in), "noCover off (or `mevr cover snap on`) lets the game snap");
    in = onFoot;
    in.onFoot = false;
    check(!RefuseCoverSnap(in), "outside the on-foot mode the refusal stands down");
    in = onFoot;
    in.headTracked = false;
    check(!RefuseCoverSnap(in), "VR off (no head-tracked view) leaves the game its cover");
    in = onFoot;
    in.calibrating = true;
    check(!RefuseCoverSnap(in), "the calibration (a neutral pad) stands the refusal down, as ME1's");

    // The crouch's margin ([Hands] crouchMinDropM; research/23 "A lean is not a crouch", lane M2W). The owner's calibration
    // (eyes 1.87 m standing, 1.56 m crouched) put the threshold half way, at 0.92 of standing (CrouchFromHeights' cap), and
    // the live standing height was 1.86 m: a reach toward someone (eyes 1.69 m) crouched Shepard.
    {
        CrouchParams calibrated;
        calibrated.crouchBelow = 0.92;
        calibrated.standAbove = 0.942;
        double const standing = 1.86;
        double const crouched = 1.56 / 1.87;
        CrouchParams const off = WithMinimumDrop(calibrated, standing, 0.0, crouched);
        check(off.crouchBelow == calibrated.crouchBelow && off.standAbove == calibrated.standAbove, "minimum drop 0 must change nothing");
        check(1.69 / standing < off.crouchBelow, "today's rule: the owner's lean (1.69 m) is below the threshold");
        CrouchParams const margin = WithMinimumDrop(calibrated, standing, 0.25, crouched);
        check(std::abs(margin.crouchBelow - (1.0 - 0.25 / standing)) < 1e-9, "a 0.25 m margin puts the crouch 0.25 m under standing");
        check(1.69 / standing > margin.crouchBelow, "with the margin the owner's lean (1.69 m) is no crouch");
        check(1.56 / standing < margin.crouchBelow, "with the margin the calibrated crouch (1.56 m) still crouches");
        check(std::abs((margin.standAbove - margin.crouchBelow) - (calibrated.standAbove - calibrated.crouchBelow)) < 1e-9,
            "the stand keeps the hysteresis gap it had");
        CrouchParams const deep = WithMinimumDrop(calibrated, standing, 0.40, crouched);
        check(deep.crouchBelow > crouched && 1.56 / standing < deep.crouchBelow,
            "a margin deeper than the calibrated crouch stops just above it: the calibrated crouch still crouches");
        CrouchParams uncalibrated;  // 0.75 / 0.85: already 0.46 m under 1.86 m
        CrouchParams const same = WithMinimumDrop(uncalibrated, standing, 0.25);
        check(same.crouchBelow == uncalibrated.crouchBelow && same.standAbove == uncalibrated.standAbove,
            "a threshold already deeper than the margin is kept");
        check(WithMinimumDrop(calibrated, -1.0, 0.25).crouchBelow == calibrated.crouchBelow, "no standing height: unchanged");
    }

    // A lean is not a crouch ([Hands] crouchLeanRatio).
    {
        CrouchParams params;
        params.crouchBelow = 0.92;
        params.standAbove = 0.942;
        double const standing = 1.86;
        LeanGuard guard;
        auto sample = [&](double x, double z, double eye) { return LeanSample{x, z, eye / standing, standing}; };
        check(!guard.Update(sample(0.0, 0.0, 1.78), params, 1.0), "standing anchors, no lean");
        // A reach: the head 0.30 m forward and 0.09 m down (1.78 -> 1.69 m).
        check(guard.Update(sample(0.0, -0.30, 1.69), params, 1.0), "0.30 m forward for 0.09 m down is a lean at ratio 1");
        check(std::abs(guard.Horizontal() - 0.30) < 1e-9 && std::abs(guard.Drop() - 0.09) < 1e-9, "the lean's measures");
        check(!guard.Update(sample(0.0, -0.30, 1.69), params, 0.0), "ratio 0: never a lean");
        // Back to standing re-anchors; then a squat straight down (0.05 m forward, 0.40 m down) is a crouch.
        guard.Update(sample(0.10, 0.10, 1.80), params, 1.0);
        check(!guard.Update(sample(0.10, 0.05, 1.40), params, 1.0), "a squat (0.05 m forward, 0.40 m down) is no lean");
        // The detector, fed as game/Posture feeds it: a lean's sample is the threshold itself, so no crouch comes.
        CrouchDetector d;
        LeanGuard g;
        g.Update(sample(0.0, 0.0, 1.80), params, 1.0);
        bool crouched = false;
        for (int i = 0; i < 60; ++i) {
            std::optional<double> f = 1.69 / standing;
            if (g.Update(sample(0.0, -0.30, 1.69), params, 1.0) && *f < params.crouchBelow) f = params.crouchBelow;
            crouched = crouched || d.Update(f, 0.016, params) == CrouchEvent::Crouch;
        }
        check(!crouched, "a held lean never crouches Shepard");
        for (int i = 0; i < 60; ++i) {
            crouched = crouched || d.Update(1.69 / standing, 0.016, params) == CrouchEvent::Crouch;
        }
        check(crouched, "without the guard the same drop crouches (today's rule)");
        LeanGuard r;
        r.Update(sample(0.0, 0.0, 1.80), params, 1.0);
        r.Reset();
        check(!r.Update(sample(0.0, -0.30, 1.69), params, 1.0), "after a reset the first sample anchors (a recenter)");
    }

    // The crouch follows the player ([Hands] crouchFollow; the owner, 2026-11-18). ME2 stood a physically crouched Shepard up
    // to walk with the weapon away (20:49:11: ExploreCrouched -> ExploreRun, the player still crouched for 18 s).
    {
        ArbiterInput base;
        base.follow = true;
        base.canApply = true;
        auto tick = [&](CrouchArbiter& a, bool gameWants, bool physicalCrouched, double now, bool canRestore = true,
                        bool explained = false, bool button = false, CrouchEvent event = CrouchEvent::None) {
            ArbiterInput in = base;
            in.physical = event;
            in.gameWants = gameWants;
            in.physicalCrouched = physicalCrouched;
            in.nowSeconds = now;
            in.canRestore = canRestore;
            in.explained = explained;
            in.button = button;
            return a.Update(in);
        };
        CrouchArbiter a;
        tick(a, false, true, 0.0, true, false, false, CrouchEvent::Crouch);
        a.Applied(true);
        check(a.Owns(), "the physical crouch is ours");
        // The game stands him up while he walks (explained, and no put-back while moving).
        ArbiterStep s = tick(a, false, true, 1.0, false, true);
        check(s.external && s.restoring && !a.Owns() && a.Restoring(), "a stand against the posture is put back");
        s = tick(a, false, true, 1.1, false, true);
        check(s.deferred && s.action == CrouchAction::None, "the put-back waits while he walks");
        s = tick(a, false, true, 2.0, true, false);
        check(s.action == CrouchAction::Crouch, "standing still again: crouched again");
        a.Applied(true);
        check(a.Owns() && !a.Restoring(), "the put-back crouch is ours, as a physical one");
        // Walking again soon stands him up again: explained, so it never counts as the game insisting.
        s = tick(a, false, true, 2.5, false, true);
        check(s.restoring && !s.insisted, "an explained undo is no insistence");
        tick(a, false, true, 3.0, true, false);
        a.Applied(true);
        // A script stands him twice, each soon after our put-back, with nothing seen: it wins until the next physical change.
        s = tick(a, false, true, 3.5);
        check(s.restoring && !s.insisted && s.action == CrouchAction::Crouch, "the first unexplained quick stand is put back");
        a.Applied(true);
        s = tick(a, false, true, 4.0);
        check(s.insisted && !s.restoring && !a.Pending(), "the second within 1.5 s of a put-back: the game insists, it wins");
        s = tick(a, true, true, 5.0);
        check(s.external && !s.restoring, "given up: the game's changes are kept until the next physical change");
        s = tick(a, false, true, 5.5);
        check(!s.restoring && !a.Pending(), "still given up");
        // The next physical change follows afresh.
        s = tick(a, false, false, 6.0, true, false, false, CrouchEvent::Stand);
        check(s.alreadyThere, "standing up for real: Shepard already stands");
        s = tick(a, true, false, 9.0);
        check(s.restoring && s.action == CrouchAction::Stand, "a crouch the game makes against a standing player is put back too");
        a.Applied(false);
        // The crouch button (ME1's L3: a change just after a click) is kept, as ME1 keeps it.
        s = tick(a, true, false, 20.0, true, false, true);
        check(s.external && !s.restoring && !a.Pending(), "L3's crouch is kept");
        // The sequels' L3 (the mod's toggle) is the button's: no external change seen, nothing put back.
        CrouchArbiter b;
        tick(b, false, false, 0.0);
        b.Pressed(true);
        s = tick(b, true, false, 0.1);
        check(!s.external && !s.restoring && !b.Pending() && !b.Owns(), "the mod's L3 crouch is kept, not ours");
        // Follow off: ME1's rule as before.
        CrouchArbiter c;
        base.follow = false;
        tick(c, false, true, 0.0, true, false, false, CrouchEvent::Crouch);
        c.Applied(true);
        s = tick(c, false, true, 1.0);
        check(s.external && !s.restoring && !c.Pending(), "crouchFollow 0: the game's stand is kept");
    }
    return failures;
}
