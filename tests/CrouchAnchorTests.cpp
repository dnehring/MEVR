// SPDX-License-Identifier: MIT
// The crouch that went third person (research/29, 2026-11-18). The owner, ME2 in the headset: "almost like clicks and moves
// Shepard to be behind cover, where it's almost magnetic". Each physical crouch behind a barrier crouched Shepard by the game;
// his animated head went 45-60 units under the view (the floor lock keeps the view at the player's real head, and the player
// crouched less than the game, or was already standing up), the view came 75 units from it, arm IK's anchor fell to third
// person and the body stopped sliding under the view: the drawn body jumped 0.5-0.76 m back to the pawn, against the barrier.
// ik::FirstPersonAnchor (core/ik/BodyFollow) measures only the horizontal distance while the view's height is the player's
// own; a third-person camera is still a metre or more behind him. Numbers from the owner's LE2MEVR.log (`arm IK: first
// person camera vs Shepard's head bone`, `body anchor`).

#include "core/ik/BodyFollow.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/CrouchAnchorTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

using namespace mevr::ik;

AnchorInput At(double forward, double right, double up, bool playerHeight) {
    AnchorInput in;
    in.haveHead = true;
    in.viewFromHead = {forward, right, up};
    in.playerHeight = playerHeight;
    return in;
}

// A run of ticks through the rule; how many ended in first person, and whether the last one did.
struct Ticks {
    int firstPerson = 0;
    bool last = false;
};
Ticks Through(std::vector<AnchorInput> const& ticks, bool start) {
    Ticks out;
    bool first = start;
    for (AnchorInput const& t : ticks) {
        first = FirstPersonAnchor(first, t);
        out.firstPerson += first ? 1 : 0;
    }
    out.last = first;
    return out;
}

} // namespace

int RunCrouchAnchorTests() {
    // Standing in first person: Halcyon's eye 20-40 units ahead of the head bone (the log: forward 24.0, right 8.2, up -1.2).
    CHECK(FirstPersonAnchor(false, At(24.0, 8.2, -1.2, false)));
    CHECK(FirstPersonAnchor(false, At(24.0, 8.2, -1.2, true)));
    CHECK(std::abs(AnchorGap(At(24.0, 8.2, -1.2, false)) - std::sqrt(24.0 * 24.0 + 8.2 * 8.2 + 1.2 * 1.2)) < 1e-9);
    CHECK(std::abs(AnchorGap(At(24.0, 8.2, -1.2, true)) - std::hypot(24.0, 8.2)) < 1e-9);

    // The owner's crouch behind a barrier: the game crouched Shepard (the log while crouched: forward 35.2, right 20.5,
    // up 34.9), then the player stood up while the game still had him crouched (the eyes 1.52 -> 1.77 m before the 0.2 s
    // stand dwell): the view 75-76 units from his head. Before: third person until the game stood him (about a second, the
    // body jumping back to the pawn and out again). Now: first person throughout, with the floor lock's height.
    std::vector<AnchorInput> crouch;
    for (bool const height : {false, true}) {
        crouch = {At(24.0, 8.2, -1.2, height), At(30.0, 15.0, 20.0, height), At(35.2, 20.5, 34.9, height),
            At(35.2, 20.5, 55.0, height), At(35.2, 20.5, 63.0, height), At(35.2, 20.5, 66.0, height),
            At(35.2, 20.5, 66.0, height), At(30.0, 12.0, 40.0, height), At(24.8, 4.9, 35.8, height)};
        Ticks const t = Through(crouch, true);
        if (height) {
            CHECK(t.firstPerson == static_cast<int>(crouch.size()) && t.last);
        } else {
            CHECK(t.firstPerson < static_cast<int>(crouch.size()));  // the old rule: third person while rising
        }
    }
    // Once in the third person the old rule needed the view back within 45: 44 units once the game stood him up, a second
    // later (the log's re-entry); measured across, the same view is 25 units off.
    CHECK(FirstPersonAnchor(false, At(24.8, 4.9, 35.8, false)) && !FirstPersonAnchor(false, At(30.0, 12.0, 40.0, false)));
    CHECK(FirstPersonAnchor(false, At(30.0, 12.0, 40.0, true)));

    // A squat with the game standing (the physical crouch off, or above its threshold): the view 75 units under his head.
    CHECK(!FirstPersonAnchor(true, At(24.0, 8.0, -75.0, false)));
    CHECK(FirstPersonAnchor(true, At(24.0, 8.0, -75.0, true)));

    // Real third person stays third person, the view's height the player's or not: Halcyon's third-person camera a metre and
    // more behind and beside him.
    for (bool const height : {false, true}) {
        CHECK(!FirstPersonAnchor(true, At(-150.0, 40.0, 30.0, height)));
        CHECK(!FirstPersonAnchor(false, At(-110.0, 35.0, 10.0, height)));
        CHECK(!FirstPersonAnchor(true, At(-80.0, 0.0, 0.0, height)));
    }
    // The hysteresis is the same horizontally: a step of the view 0.8 m from his head leaves, back within 45 enters, between
    // keeps what it was.
    CHECK(!FirstPersonAnchor(true, At(76.0, 0.0, 0.0, true)));
    CHECK(FirstPersonAnchor(true, At(74.0, 0.0, 0.0, true)));
    CHECK(!FirstPersonAnchor(false, At(50.0, 0.0, 0.0, true)));
    CHECK(FirstPersonAnchor(false, At(44.0, 0.0, 0.0, true)));
    AnchorSettings tight;
    tight.enterUnits = 10.0;
    tight.leaveUnits = 20.0;
    CHECK(!FirstPersonAnchor(false, At(15.0, 0.0, 0.0, true), tight) && FirstPersonAnchor(true, At(15.0, 0.0, 0.0, true), tight));

    // No head bone, or a broken one: never first person by this rule (the caller's `inView` decides then).
    AnchorInput none;
    none.playerHeight = true;
    CHECK(AnchorGap(none) < 0.0 && !FirstPersonAnchor(true, none));
    double const nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(!FirstPersonAnchor(true, At(nan, 0.0, 0.0, true)) && !FirstPersonAnchor(true, At(0.0, 0.0, nan, false)));
    // The vertical part ignored only with the player's height: a broken height then doesn't matter.
    CHECK(FirstPersonAnchor(true, At(10.0, 0.0, nan, true)));
    return gFailures;
}
