// SPDX-License-Identifier: MIT
#include "core/ik/Posture.hpp"
#include <cstdio>

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
    return failures;
}
