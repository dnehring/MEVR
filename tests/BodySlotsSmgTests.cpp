// SPDX-License-Identifier: MIT
// The SMG's two places on the sequels (lane SG, research/le2-vrik.md "The SMG at the small of the back"): the front of the
// right thigh (the default) and the small of the back, and a right hand that loses tracking behind the back. Also the other
// sequel-only slot rules: a draw the game refuses (ForgetDraw) and ME3's heavy weapon in hand (Input::switchLosesHeld).
#include "core/ik/BodySlots.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string_view>

namespace {
int failures = 0;
void Check(bool ok, char const* what, int line) {
    if (!ok) { std::printf("FAIL tests/BodySlotsSmgTests.cpp:%d: %s\n", line, what); ++failures; }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)
bool NearD(double a, double b, double epsilon) { return std::abs(a - b) <= epsilon; }
bool NearIk(mevr::ik::Vec3 a, mevr::ik::Vec3 b, double epsilon = 1e-8) { return mevr::ik::Length(a - b) < epsilon; }
}

int RunBodySlotsSmgTests() {
    using namespace mevr::ik;
    using namespace mevr::ik::body_slots;

    // Names: the slot's name is its [Holsters] key and smgSlot's value.
    CHECK(ParseSmgPlace("thighRight") == SmgPlace::ThighRight);
    CHECK(ParseSmgPlace("THIGH") == SmgPlace::ThighRight);
    CHECK(ParseSmgPlace(" Lower_Back ") == SmgPlace::LowerBack);
    CHECK(ParseSmgPlace("back") == SmgPlace::LowerBack);
    CHECK(!ParseSmgPlace("hipLeft"));
    CHECK(std::string_view(Name(SmgPlace::ThighRight)) == "thighRight");
    CHECK(std::string_view(Name(SmgPlace::LowerBack)) == "lowerBack");

    // The default is still the thigh; the lower back changes only the SMG's slot.
    Layout const thigh = DefaultLayout();
    Layout const back = DefaultLayout(SmgPlace::LowerBack);
    CHECK(thigh.smgPlace == SmgPlace::ThighRight && std::string_view(thigh.slots[kSmgSlot].name) == "thighRight");
    CHECK(NearIk(thigh.slots[kSmgSlot].offset, {0.08, -0.90, 0.15}));
    Slot const& smg = back.slots[kSmgSlot];
    CHECK(back.smgPlace == SmgPlace::LowerBack && std::string_view(smg.name) == "lowerBack" && smg.weapon == Weapon::Smg);
    for (int i = 0; i < kSmgSlot; ++i) {
        CHECK(NearIk(back.slots[i].offset, thigh.slots[i].offset) && back.slots[i].weapon == thigh.slots[i].weapon &&
              back.slots[i].radius == thigh.slots[i].radius);
    }
    // Behind the body, on the spine, at the belt.
    CHECK(smg.offset.z < -0.15 && std::abs(smg.offset.x) < 0.05 && smg.offset.y < -0.5 && smg.offset.y > -0.7);

    // Positions in the body frame (right, up, forward) at yaw 0, where forward is -Z.
    Frame const frame{{0.0, 1.6, 0.0}, 0.0};
    auto const at = [&frame](Vec3 body) { return frame.anchor + Vec3{body.x, body.y, -body.z}; };
    auto const center = [&frame](Layout const& layout, int i, double scale) { return SlotCenter(frame, layout.slots[i], scale); };
    // The game's own lower-back attach point (EASlot_LowerBack's Socket_06 on Shepard's reference skeleton, scaled to the
    // 1.65 m eye height) is inside the zone, and outside the pistol's.
    Vec3 const socket = at({0.10, -0.60, -0.20});
    CHECK(Length(socket - center(back, kSmgSlot, 1.0)) < smg.radius);
    CHECK(Length(socket - center(back, 0, 1.0)) > back.slots[0].radius);

    // No overlap with the other slots (the two shoulders' known overlap aside), for standing eye heights 1.50 to 1.95 m.
    for (double eye : {1.50, 1.65, 1.80, 1.95}) {
        Layout scaled = back;
        scaled.eyeHeight = eye;
        double const s = Scale(scaled);
        for (int i = 0; i < kSlots; ++i) {
            for (int j = i + 1; j < kSlots; ++j) {
                if (i == 1 && j == 3) {
                    continue;
                }
                CHECK(Length(center(back, i, s) - center(back, j, s)) > back.slots[i].radius + back.slots[j].radius);
            }
        }
    }
    // A hand hanging at the side, the low hip-fire hand, the belly rest and the shouldered aim point are clear of it.
    for (Vec3 const body : {Vec3{0.25, -0.85, -0.03}, Vec3{0.15, -0.45, 0.30}, Vec3{0.08, -0.55, 0.15}, Vec3{0.15, -0.15, 0.10}}) {
        CHECK(Length(at(body) - center(back, kSmgSlot, 1.0)) > smg.radius + back.exitMargin);
    }

    // Reachable: the right hand swung back from the side, round under the pistol's hip slot to the small of the back,
    // enters no other slot on the way and ends in the SMG's; a squeeze there draws the SMG.
    auto const inputAt = [&](Layout const& layout, Vec3 hand) {
        Input in;
        for (int i = 0; i < kSlots; ++i) {
            in.distance[i] = Length(hand - center(layout, i, 1.0));
            in.radius[i] = layout.slots[i].radius;
            in.weapon[i] = layout.slots[i].weapon;
            in.available[i] = true;
        }
        in.exitMargin = layout.exitMargin;
        in.gripHolds = true;
        return in;
    };
    std::array<Vec3, 5> const path{{{0.25, -0.85, -0.03}, {0.24, -0.80, -0.16}, {0.18, -0.70, -0.26}, {0.08, -0.62, -0.29},
        {0.03, -0.60, -0.25}}};
    Machine reach;
    int smgEntries = 0;
    int otherEntries = 0;
    for (std::size_t k = 0; k + 1 < path.size(); ++k) {
        for (int t = 0; t <= 20; ++t) {
            Step const step = reach.Update(inputAt(back, at(path[k] + (path[k + 1] - path[k]) * (t / 20.0))));
            if (step.entered == kSmgSlot) {
                ++smgEntries;
            } else if (step.entered >= 0) {
                ++otherEntries;
            }
        }
    }
    CHECK(smgEntries == 1 && otherEntries == 0 && reach.InSlot() == kSmgSlot);
    {
        Input squeeze = inputAt(back, at(path.back()));
        squeeze.gripDown = squeeze.newSqueeze = true;
        Step const step = reach.Update(squeeze);
        CHECK(step.action == Action::Draw && step.slot == kSmgSlot && step.weapon == Weapon::Smg);
    }

    // Seated: the chair's back covers the small of the back, so the SMG is beside it, behind the right hip; no seated zone
    // overlaps another or the grenade's, and the cheek aim point is clear. The thigh's seated place is unchanged.
    Layout const seated = SeatedLayout(back);
    CHECK(seated.smgPlace == SmgPlace::LowerBack && std::string_view(seated.slots[kSmgSlot].name) == "lowerBack");
    CHECK(NearIk(seated.slots[kSmgSlot].offset, SeatedSmgSlot(SmgPlace::LowerBack).offset));
    CHECK(seated.slots[kSmgSlot].offset.x > 0.25 && seated.slots[kSmgSlot].offset.z < -0.15);
    CHECK(NearIk(SeatedLayout(thigh).slots[kSmgSlot].offset, {0.15, -0.60, 0.55}));
    {
        Frame const chair{{0.0, 1.2, 0.0}, 0.0};
        Slot const grenade = SeatedGrenadeSlot();
        Vec3 const aim = chair.anchor + Vec3{0.15, -0.15, -0.10};
        Vec3 const c = SlotCenter(chair, seated.slots[kSmgSlot], 1.0);
        CHECK(Length(c - chair.anchor) > seated.slots[kSmgSlot].radius);
        CHECK(Length(c - aim) > seated.slots[kSmgSlot].radius + seated.exitMargin);
        CHECK(Length(c - SlotCenter(chair, grenade, 1.0)) > seated.slots[kSmgSlot].radius + grenade.radius);
        for (int i = 0; i < kSmgSlot; ++i) {
            CHECK(Length(c - SlotCenter(chair, seated.slots[i], 1.0)) > seated.slots[kSmgSlot].radius + seated.slots[i].radius);
        }
    }

    // Which slot keeps a lost hand: only the SMG's at the small of the back, while it's on (never the thigh, never ME1's
    // switched-off fifth slot).
    CHECK(KeepsLostHand(back, kSmgSlot));
    CHECK(KeepsLostHand(seated, kSmgSlot));
    for (int i = 0; i < kSmgSlot; ++i) {
        CHECK(!KeepsLostHand(back, i));
    }
    for (int i = 0; i < kSlots; ++i) {
        CHECK(!KeepsLostHand(thigh, i));
    }
    Layout off = back;
    off.slots[kSmgSlot].weapon = Weapon::None;
    CHECK(!KeepsLostHand(off, kSmgSlot));

    // A lost hand: nothing kept until a tracked hand was at such a slot.
    LostHand lost;
    CHECK(!lost.Keeps(0.75, 0.011) && !lost.Holding());
    Vec3 const handBody{0.03, -0.60, -0.25};
    Vec3 const hand = at(handBody);
    lost.Tracked(hand, frame, false);
    CHECK(!lost.Keeps(0.75, 0.011));
    lost.Tracked(hand, frame, true);
    CHECK(lost.Keeps(0.75, 0.011) && !lost.Holding());
    CHECK(!lost.Keeps(0.0, 0.011));  // lostHandHoldS 0: never
    // Kept in the body frame: the body turned 90 degrees left and stepped aside, the hand went with it.
    Frame const turned{{0.3, 1.6, -0.1}, std::numbers::pi / 2.0};
    Vec3 const kept = lost.Kept(turned, 0.011);
    CHECK(lost.Holding() && NearD(lost.LostSeconds(), 0.011, 1e-12));
    CHECK(NearIk(kept, SlotCenter(turned, Slot{"", Weapon::Smg, handBody, 0.1}, 1.0), 1e-9));
    // Kept until the seconds are used up, then not.
    double elapsed = 0.011;
    while (lost.Keeps(0.75, 0.011)) {
        lost.Kept(turned, 0.011);
        elapsed += 0.011;
    }
    CHECK(elapsed <= 0.75 + 1e-9 && elapsed > 0.73);
    // Tracked again: the clock stops and starts afresh.
    lost.Tracked(hand, frame, true);
    CHECK(!lost.Holding() && lost.Keeps(0.75, 0.011));
    lost.Reset();
    CHECK(!lost.Keeps(0.75, 0.011));

    // A squeeze while the hand is lost at the slot draws the SMG at its kept place.
    Machine grab;
    lost.Tracked(hand, frame, KeepsLostHand(back, kSmgSlot));
    grab.Update(inputAt(back, hand));
    CHECK(grab.InSlot() == kSmgSlot && lost.Keeps(0.75, 0.011));
    {
        Input squeeze = inputAt(back, lost.Kept(frame, 0.011));
        squeeze.gripDown = squeeze.newSqueeze = true;
        Step const step = grab.Update(squeeze);
        CHECK(step.action == Action::Draw && step.weapon == Weapon::Smg);
    }

    // A draw the game refuses (the sequels' holsters where the game allows no combat): forgotten, the release at the slot
    // sends nothing back; without it the release would wait for a draw that never comes (lane FX2, 2026-11-17).
    for (bool forget : {true, false}) {
        Machine refused;
        Vec3 const atSmg = center(thigh, kSmgSlot, 1.0);
        refused.Update(inputAt(thigh, atSmg));
        Input squeeze = inputAt(thigh, atSmg);
        squeeze.gripDown = squeeze.newSqueeze = true;
        CHECK(refused.Update(squeeze).action == Action::Draw);
        if (forget) {
            refused.ForgetDraw();
        }
        Input release = inputAt(thigh, atSmg);
        Step const let = refused.Update(release);
        CHECK(forget ? let.action == Action::None && refused.CurrentPhase() == Phase::Idle
                     : let.action == Action::ReturnArmed && refused.CurrentPhase() == Phase::Returning);
    }

    // ME3's heavy weapon (the owner, 2026-11-17, docs/trilogy-plan.md question 13): it has no slot (held None) and a switch
    // away throws it away, so a squeeze at any slot whose gun would be drawn is refused, nothing is on its way and the
    // release sends nothing; with the gun-holding grip and without. A slot whose weapon isn't carried stays Unavailable. ME2
    // (a switch keeps its heavy weapon: switchLosesHeld false) draws as before.
    for (bool holds : {true, false}) {
        for (int slot : {0, 1, 2, kSmgSlot}) {  // the pistol's hip, the assault rifle's shoulder (near the heavy weapon), the
                                                // shotgun's hip, the SMG's thigh
            Vec3 const atSlot = center(thigh, slot, 1.0);
            Machine heavy;
            Input in = inputAt(thigh, atSlot);
            in.gripHolds = holds;
            in.switchLosesHeld = true;
            heavy.Update(in);
            Input squeeze = in;
            squeeze.gripDown = squeeze.newSqueeze = true;
            Step const refusedGrab = heavy.Update(squeeze);
            CHECK(refusedGrab.action == Action::SwitchRefused && refusedGrab.slot == slot &&
                  refusedGrab.weapon == thigh.slots[slot].weapon);
            Input hold = in;
            hold.gripDown = true;
            CHECK(heavy.Update(hold).action == Action::None);
            Step const let = heavy.Update(in);
            CHECK(let.action == Action::None && heavy.CurrentPhase() == Phase::Idle);
            // Not carried: the usual refusal.
            Machine none;
            Input empty = in;
            empty.available[slot] = false;
            none.Update(empty);
            Input emptySqueeze = empty;
            emptySqueeze.gripDown = emptySqueeze.newSqueeze = true;
            CHECK(none.Update(emptySqueeze).action == Action::Unavailable);
            // ME2: the same squeeze draws.
            Machine me2;
            Input keeps = in;
            keeps.switchLosesHeld = false;
            me2.Update(keeps);
            Input drawSqueeze = keeps;
            drawSqueeze.gripDown = drawSqueeze.newSqueeze = true;
            CHECK(me2.Update(drawSqueeze).action == Action::Draw);
        }
    }
    CHECK(std::string_view(Name(Action::SwitchRefused)) == "switch refused");
    return failures;
}
