// SPDX-License-Identifier: MIT
// Offline tests for the Mako cockpit's two controls (core/vr/CockpitPanel, research/48 section 27): the repair button pressed
// by a fingertip and held, the extraction lever grabbed and pulled, and the lamps' counts and colours.

#include "core/vr/CockpitPanel.hpp"
#include "core/vr/MakoGunner.hpp"

#include <cmath>
#include <cstdio>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/CockpitPanelTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace cp = mevr::vr::cockpit_panel;

bool Close(float a, float b, float tolerance = 1e-3f) {
    return std::abs(a - b) <= tolerance;
}

// The button's cap at the origin, pressed downward (-y); the handle hinged along x at (0, 1, 0), its grip 0.06 m below the hinge,
// swinging out toward -z.
cp::Input Base() {
    cp::Input in;
    in.deltaSeconds = 0.05;
    in.active = true;
    in.layout.buttonTop = {0.0f, 0.0f, 0.0f};
    in.layout.buttonIn = {0.0f, -1.0f, 0.0f};
    in.layout.travelM = 0.003f;
    in.layout.hinge = {0.0f, 1.0f, 0.0f};
    in.layout.hingeAxis = {1.0f, 0.0f, 0.0f};  // + turns -y toward -z: the grip below the hinge swings out to -z
    in.layout.grip = {0.0f, 0.94f, 0.0f};
    in.layout.pullDeg = 30.0f;
    for (cp::Hand& hand : in.hands) {
        hand.tracked = true;
        hand.tip = {0.5f, 0.5f, 0.5f};
        hand.grip = {0.5f, 0.5f, 0.5f};
    }
    return in;
}

cp::Output Tip(cp::Panel& panel, cp::Settings const& s, float y, float x = 0.0f, double dt = 0.05) {
    cp::Input in = Base();
    in.deltaSeconds = dt;
    in.hands[1].tip = {x, y, 0.0f};
    return panel.Step(in, s);
}

void TestButton() {
    cp::Settings s;
    cp::Panel panel;
    // a tip that starts inside the cap (came through it) never presses
    cp::Output out = Tip(panel, s, -0.002f);
    CHECK(!out.pressed && out.pressHand == -1);
    out = Tip(panel, s, -0.003f);
    CHECK(!out.pressed);
    // from in front: the cap follows, pressed past 60% of the travel
    panel.Reset();
    out = Tip(panel, s, 0.02f);
    CHECK(!out.pressed && Close(out.pressM, 0.0f));
    out = Tip(panel, s, -0.001f);
    CHECK(out.pressHand == 1 && !out.pressed && Close(out.pressM, 0.001f));
    out = Tip(panel, s, -0.0025f);
    CHECK(out.pressed && out.hapticCount >= 1);
    // pushed through: the cap stops at its travel, the press holds
    out = Tip(panel, s, -0.03f);
    CHECK(out.pressed && Close(out.pressM, 0.003f));
    // held: the progress fills over holdS, the repair runs once
    bool fired = false;
    int fires = 0;
    for (int i = 0; i < 60; ++i) {
        out = Tip(panel, s, -0.0028f);
        fires += out.repair ? 1 : 0;
        fired = fired || out.repair;
    }
    CHECK(fired && fires == 1 && Close(out.progress, 1.0f));
    // let go: the progress empties; a new press may fire again
    out = Tip(panel, s, 0.01f);
    CHECK(!out.pressed && Close(out.progress, 0.0f));
    // lifted before the hold is over: nothing ran
    out = Tip(panel, s, -0.0028f);
    CHECK(out.pressed);
    out = Tip(panel, s, -0.0028f, 0.0, 1.0);  // dt is clamped to 0.1
    CHECK(!out.repair && out.progress < 0.2f);
    out = Tip(panel, s, 0.01f);
    CHECK(!out.pressed && !out.repair);
    // off the axis: no press
    panel.Reset();
    Tip(panel, s, 0.02f, 0.05f);
    out = Tip(panel, s, -0.0028f, 0.05f);
    CHECK(out.pressHand == -1 && !out.pressed);
    // inactive: everything lets go
    panel.Reset();
    Tip(panel, s, 0.02f);
    Tip(panel, s, -0.0028f);
    cp::Input off = Base();
    off.active = false;
    out = panel.Step(off, s);
    CHECK(!out.pressed && out.pressHand == -1);
}

cp::Output Grip(cp::Panel& panel, cp::Settings const& s, cp::Hand const& hand, double dt = 0.05) {
    cp::Input in = Base();
    in.deltaSeconds = dt;
    in.hands[0] = hand;
    return panel.Step(in, s);
}

void TestLever() {
    cp::Settings s;
    cp::Panel panel;
    cp::Hand hand;
    hand.tracked = true;
    hand.grip = {0.0f, 0.94f, 0.0f};
    // a squeeze held before reaching the handle doesn't take it
    hand.squeeze = 1.0f;
    hand.grip = {0.0f, 0.5f, 0.5f};
    cp::Output out = Grip(panel, s, hand);
    CHECK(!out.held);
    hand.grip = {0.0f, 0.94f, 0.0f};
    out = Grip(panel, s, hand);
    CHECK(!out.held && out.inReach[0]);
    // a new squeeze at the handle takes it
    hand.squeeze = 0.0f;
    Grip(panel, s, hand);
    hand.squeeze = 1.0f;
    out = Grip(panel, s, hand);
    CHECK(out.held && out.holdHand == 0 && Close(out.pullDeg, 0.0f, 0.5f));
    // the hand swings the handle out about the hinge; it stops at its pull
    float const a = 20.0f * 0.017453293f;
    hand.grip = {0.0f, 1.0f - 0.06f * std::cos(a), -0.06f * std::sin(a)};
    out = Grip(panel, s, hand);
    CHECK(out.held && Close(out.pullDeg, 20.0f, 0.05f));
    hand.grip = {0.0f, 1.0f, -0.06f};  // 90 degrees out: held at the pull
    out = Grip(panel, s, hand);
    CHECK(Close(out.pullDeg, 30.0f, 0.01f) && !out.extract);
    // held at the end for pullHoldS: the extraction runs once
    int fires = 0;
    for (int i = 0; i < 20; ++i) {
        out = Grip(panel, s, hand);
        fires += out.extract ? 1 : 0;
    }
    CHECK(fires == 1);
    // let go: it swings back; not again until it was back below half its pull
    hand.squeeze = 0.0f;
    out = Grip(panel, s, hand, 0.05);
    CHECK(!out.held && Close(out.pullDeg, 30.0f - 12.0f, 0.01f));
    for (int i = 0; i < 10; ++i) {
        out = Grip(panel, s, hand);
    }
    CHECK(Close(out.pullDeg, 0.0f));
    // pulled the other way (into the box): it stays at 0
    hand.grip = {0.0f, 0.94f, 0.0f};
    Grip(panel, s, hand);
    hand.squeeze = 1.0f;
    out = Grip(panel, s, hand);
    CHECK(out.held);
    hand.grip = {0.0f, 1.0f - 0.06f * std::cos(a), 0.06f * std::sin(a)};
    out = Grip(panel, s, hand);
    CHECK(Close(out.pullDeg, 0.0f));
}

void TestLamps() {
    CHECK(cp::HealthLamps(0.0f, 9) == 0);
    CHECK(cp::HealthLamps(1.0f, 9) == 9);
    CHECK(cp::HealthLamps(0.01f, 9) == 1);
    CHECK(cp::HealthLamps(0.5f, 9) == 5);
    CHECK(cp::HealthLamps(1.0f / 9.0f, 9) == 1);
    CHECK(cp::HealthColour(0.9f) == cp::Colour::Green);
    CHECK(cp::HealthColour(0.5f) == cp::Colour::Amber);
    CHECK(cp::HealthColour(0.2f) == cp::Colour::Red);
    CHECK(cp::ProgressLamps(0.0f, 8) == 0);
    CHECK(cp::ProgressLamps(0.125f, 8) == 1);
    CHECK(cp::ProgressLamps(0.99f, 8) == 7);
    CHECK(cp::ProgressLamps(1.0f, 8) == 8);
}

// Working the panel, the gunner keeps the turret (core/vr/MakoGunner's `busy`; the user, 2026-11-05: "the aim should remain in
// auto mode"): the hand at the handle points out through the windshield with a finger on the trigger, which would take the gun.
void TestGunnerWhileBusy() {
    namespace mg = mevr::vr::mako_gunner;
    mg::Settings s;
    mg::ControlState state;
    mg::Hands reaching;
    reaching.handFree = true;
    reaching.handIntent = true;
    reaching.handPointsOut = true;
    reaching.busy = true;
    CHECK(mg::Step(state, reaching, 0.0, s) == mg::Control::Gunner);
    mg::Hands thumb = reaching;  // the other hand on the stick, its thumbstick pushed: still the gunner's
    thumb.stickHeld = true;
    thumb.thumbX = 0.9f;
    CHECK(mg::Step(state, thumb, 0.5, s) == mg::Control::Gunner);
    // the player had the gun and reaches for the button: the gunner's at once, not after the handback
    mg::ControlState had;
    mg::Hands aiming = reaching;
    aiming.busy = false;
    CHECK(mg::Step(had, aiming, 0.0, s) == mg::Control::Hand);
    mg::Hands pressing;
    pressing.busy = true;
    CHECK(mg::Step(had, pressing, 0.1, s) == mg::Control::Gunner);
    // let go, the hand down: the gunner keeps it; the next aim takes it again
    CHECK(mg::Step(had, mg::Hands{}, 0.2, s) == mg::Control::Gunner);
    CHECK(mg::Step(had, aiming, 0.3, s) == mg::Control::Hand);
    // the gunner off ([Mako] gunner = 0): the old rules, no auto mode to keep
    mg::Settings off = s;
    off.on = false;
    mg::ControlState old;
    CHECK(mg::Step(old, pressing, 0.0, off) == mg::Control::Hand);
}

}  // namespace

int RunCockpitPanelTests() {
    TestButton();
    TestLever();
    TestLamps();
    TestGunnerWhileBusy();
    std::printf("cockpit panel tests: %d failure(s)\n", gFailures);
    return gFailures;
}
