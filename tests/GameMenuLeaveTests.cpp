// SPDX-License-Identifier: MIT
// Offline tests for our menu's Leave entry (ME2 and ME3; the owner's answer to question 16, 2026-11-17; research/28 "Leave"):
// the request's value, when a level's leave event counts as armed, when the menu offers Leave, and the host's freshness rule
// for the mod's word (vr::game_menu::Offers).

#include "core/vr/GameMenuRequest.hpp"

#include <cstdio>
#include <cstring>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/GameMenuLeaveTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace gm = mevr::vr::game_menu;

// Leave is a request like the others: it survives the slot and its encoding with either origin, and a value past it is None
// (a newer host's request an older mod doesn't know).
void RequestValue() {
    CHECK(gm::FromValue(9) == gm::Action::Leave);
    CHECK(gm::FromValue(10) == gm::Action::None);
    CHECK(std::strcmp(gm::Name(gm::Action::Leave), "leave") == 0);
    CHECK(!gm::IsScreen(gm::Action::Leave) && gm::ShowFunction(gm::Action::Leave) == nullptr);
    CHECK(gm::DecodeAction(gm::Encode(gm::Action::Leave, gm::Origin::Menu)) == gm::Action::Leave);
    CHECK(gm::DecodeOrigin(gm::Encode(gm::Action::Leave, gm::Origin::OmniRail)) == gm::Origin::OmniRail);
    gm::Slot slot;
    CHECK(slot.Put(gm::Action::Leave) == gm::Action::None);
    CHECK(slot.Take() == gm::Action::Leave);
    CHECK(slot.Take() == gm::Action::None);
    // The schedule runs it like the others: after the menu's pause has been lifted for kSettleTicks ticks.
    gm::Schedule schedule;
    gm::Take(schedule, gm::Action::Leave);
    CHECK(gm::Advance(schedule, false, true).run == gm::Action::None);
    gm::Step step;
    for (int i = 0; i < gm::kSettleTicks; ++i) step = gm::Advance(schedule, false, false);
    CHECK(step.run == gm::Action::Leave);
}

// An event is armed as SequenceEvent.CheckActivate would take it (enabled, not used up) and only if its output reaches
// something that acts: the post-credits cabins' event leads straight into a closed gate.
void Armed() {
    gm::LeaveEvent e;
    e.enabled = true;
    e.openPaths = 1;
    CHECK(gm::Armed(e));
    e.maxTriggers = 1;
    e.triggers = 0;
    CHECK(gm::Armed(e));
    e.triggers = 1;
    CHECK(!gm::Armed(e));  // used up
    e.maxTriggers = 0;
    e.triggers = 7;
    CHECK(gm::Armed(e));   // no limit
    e.enabled = false;
    CHECK(!gm::Armed(e));  // toggled off by its level
    e.enabled = true;
    e.openPaths = 0;
    CHECK(!gm::Armed(e));  // only closed gates (or nothing) behind it
}

// The menu offers Leave on foot, out of combat, with an armed event loaded; each refusal says why.
void Offer() {
    gm::LeaveFacts f;
    CHECK(gm::LeaveOffer(f) == gm::LeaveWhy::Unknown);
    f.known = true;
    CHECK(gm::LeaveOffer(f) == gm::LeaveWhy::NotOnFoot);
    f.onFoot = true;
    f.inCombat = true;
    f.events = 1;
    f.armed = 1;
    CHECK(gm::LeaveOffer(f) == gm::LeaveWhy::InCombat);
    f.inCombat = false;
    CHECK(gm::LeaveOffer(f) == gm::LeaveWhy::Offered);
    f.armed = 0;
    CHECK(gm::LeaveOffer(f) == gm::LeaveWhy::NoneArmed);
    f.events = 0;
    CHECK(gm::LeaveOffer(f) == gm::LeaveWhy::NoEvent);  // Freedom's Progress, the Normandy, every ME1 level
    for (gm::LeaveWhy why : {gm::LeaveWhy::Offered, gm::LeaveWhy::Unknown, gm::LeaveWhy::NotOnFoot, gm::LeaveWhy::InCombat,
             gm::LeaveWhy::NoEvent, gm::LeaveWhy::NoneArmed}) {
        CHECK(std::strcmp(gm::Text(why), "?") != 0);
    }
}

// The host shows the entry only while the mod's word is fresh: nothing before the first word, the bits for kOfferFreshMs after
// it, nothing once the mod stops saying it (gone, reloading, a stalled game thread past a second), nothing after Forget.
void Freshness() {
    gm::Offers offers;
    CHECK(offers.Current(5000) == 0);
    offers.Publish(gm::kOfferLeave, 5000);
    CHECK(offers.Current(5000) == gm::kOfferLeave);
    CHECK(offers.Current(5000 + gm::kOfferFreshMs) == gm::kOfferLeave);
    CHECK(offers.Current(5000 + gm::kOfferFreshMs + 1) == 0);
    CHECK(offers.Current(4999) == 0);  // a clock before the word: not trusted
    offers.Publish(0, 6000);
    CHECK(offers.Current(6000) == 0);
    offers.Publish(gm::kOfferLeave, 0);  // a zero clock still counts as published
    CHECK(offers.Current(1) == gm::kOfferLeave);
    offers.Forget();
    CHECK(offers.Current(1) == 0);
}

} // namespace

int RunGameMenuLeaveTests() {
    gFailures = 0;
    RequestValue();
    Armed();
    Offer();
    Freshness();
    if (gFailures == 0) {
        std::printf("game menu leave tests: ok\n");
    }
    return gFailures;
}
