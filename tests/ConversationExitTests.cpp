// SPDX-License-Identifier: MIT
// Offline tests for people leaving a first-person conversation (core/vr/ConversationExit, research/conversation-exits.md): which
// removals are held, where the person walks, when the game's change is applied, swaps and the live audit's rule.

#include "core/vr/ConversationExit.hpp"

#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ConversationExitTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace cx = mevr::vr::conversation_exit;

void TestParseMode() {
    CHECK(cx::ParseMode("walk", cx::Mode::Off) == cx::Mode::Walk);
    CHECK(cx::ParseMode(" Walk ", cx::Mode::Off) == cx::Mode::Walk);
    CHECK(cx::ParseMode("on", cx::Mode::Off) == cx::Mode::Walk);
    CHECK(cx::ParseMode("hold", cx::Mode::Walk) == cx::Mode::Hold);
    CHECK(cx::ParseMode("1", cx::Mode::Walk) == cx::Mode::Hold);
    CHECK(cx::ParseMode("off", cx::Mode::Walk) == cx::Mode::Off);
    CHECK(cx::ParseMode("0", cx::Mode::Walk) == cx::Mode::Off);
    CHECK(cx::ParseMode("", cx::Mode::Walk) == cx::Mode::Walk);       // missing: the fallback
    CHECK(cx::ParseMode("sideways", cx::Mode::Hold) == cx::Mode::Hold);
    CHECK(cx::ParseMode(nullptr, cx::Mode::Walk) == cx::Mode::Walk);
    CHECK(std::string_view(cx::Name(cx::Mode::Walk)) == "walk");
    CHECK(std::string_view(cx::Name(cx::Mode::Hold)) == "hold");
    CHECK(std::string_view(cx::Name(cx::Mode::Off)) == "off");
}

void TestClassify() {
    cx::Subject udina;
    udina.person = true;
    udina.seen = true;
    cx::Context ctx;
    ctx.worldMode = true;
    ctx.sceneShown = true;
    // Udina at sta90_captain_dlg's end: in view, the next conversation already shown in the world.
    CHECK(cx::Classify(cx::Kind::Deactivate, udina, ctx).hold);
    CHECK(cx::Classify(cx::Kind::Hide, udina, ctx).hold);
    CHECK(cx::Classify(cx::Kind::Teleport, udina, ctx).hold);
    // Never held: a destroyed actor, a level streamed out.
    CHECK(!cx::Classify(cx::Kind::Destroy, udina, ctx).hold);
    CHECK(!cx::Classify(cx::Kind::StreamOut, udina, ctx).hold);
    // The setting.
    cx::Context off = ctx;
    off.mode = cx::Mode::Off;
    CHECK(!cx::Classify(cx::Kind::Deactivate, udina, off).hold);
    cx::Context hold = ctx;
    hold.mode = cx::Mode::Hold;
    CHECK(cx::Classify(cx::Kind::Deactivate, udina, hold).hold);  // hold: still held (the look-away alone)
    cx::Context screen = ctx;
    screen.worldMode = false;
    CHECK(!cx::Classify(cx::Kind::Deactivate, udina, screen).hold);
    // Who.
    cx::Subject s = udina;
    s.squad = true;
    CHECK(!cx::Classify(cx::Kind::Deactivate, s, ctx).hold);
    s = udina;
    s.player = true;
    CHECK(!cx::Classify(cx::Kind::Deactivate, s, ctx).hold);
    s = udina;
    s.neededNext = true;
    CHECK(!cx::Classify(cx::Kind::Deactivate, s, ctx).hold);
    s = udina;
    s.person = false;
    CHECK(!cx::Classify(cx::Kind::Deactivate, s, ctx).hold);
    s = udina;
    s.seen = false;  // out of sight: at once, nobody sees it
    CHECK(!cx::Classify(cx::Kind::Deactivate, s, ctx).hold);
    // When: just after a scene shown in the world ended (Chorban deactivated at his conversation's Out), not long after.
    cx::Context after = ctx;
    after.sceneShown = false;
    after.sinceSceneEndS = 0.1;
    CHECK(cx::Classify(cx::Kind::Deactivate, udina, after).hold);
    after.sinceSceneEndS = 2.5;
    CHECK(!cx::Classify(cx::Kind::Deactivate, udina, after).hold);
    after.sinceSceneEndS = -1.0;  // never a scene in this level
    CHECK(!cx::Classify(cx::Kind::Deactivate, udina, after).hold);
    // Under the game's own black screen (the council hearing's end deactivates the councillors under it): at once.
    cx::Context black = ctx;
    black.underBlack = true;
    CHECK(!cx::Classify(cx::Kind::Deactivate, udina, black).hold);
}

void TestCandidates() {
    cx::Limits const lim;
    cx::Vec3 const person{1000.0, 0.0, 50.0};
    cx::Vec3 const eyes{0.0, 0.0, 160.0};
    std::vector<cx::Vec3> const pts = cx::CandidatePoints(person, eyes, lim);
    CHECK(pts.size() == 36);
    // The first ones straight away from the eyes, at min, mid and max; all on the person's height; all within the limits.
    CHECK(std::abs(pts[0].x - (1000.0 + lim.minUnits)) < 1e-6 && std::abs(pts[0].y) < 1e-6);
    CHECK(std::abs(pts[2].x - (1000.0 + lim.maxUnits)) < 1e-6);
    bool heights = true, ranges = true;
    for (cx::Vec3 const& p : pts) {
        heights = heights && p.z == person.z;
        double const d = std::hypot(p.x - person.x, p.y - person.y);
        ranges = ranges && d >= lim.minUnits - 1e-6 && d <= lim.maxUnits + 1e-6;
    }
    CHECK(heights);
    CHECK(ranges);
    // The last bearing is straight back toward the eyes.
    CHECK(pts.back().x < person.x);
}

void TestPick() {
    cx::Limits const lim;
    cx::Vec3 const person{1000.0, 0.0, 0.0};
    cx::Vec3 const eyes{0.0, 0.0, 0.0};
    std::vector<cx::Candidate> c(4);
    c[0] = {{1400.0, 0.0, 0.0}, true, false, false};   // straight away, but in sight
    c[1] = {{1000.0, 400.0, 0.0}, true, true, false};  // to the side, hidden (behind a pillar)
    c[2] = {{1600.0, 100.0, 0.0}, false, true, false}; // away and hidden, but no way there
    c[3] = {{1500.0, -50.0, 0.0}, true, true, false};  // away and hidden, a clear way: the best
    auto pick = cx::PickDestination(c, person, eyes, lim);
    CHECK(pick && pick->index == 3 && pick->hidden);
    // Where the script sends them wins if the way is clear, seen or not (a teleport's destination: the walk is its move).
    c.push_back({{900.0, 300.0, 0.0}, true, false, true});
    pick = cx::PickDestination(c, person, eyes, lim);
    CHECK(pick && pick->index == 4 && !pick->hidden);
    c[4].path = false;  // no way there: the hidden places again
    pick = cx::PickDestination(c, person, eyes, lim);
    CHECK(pick && pick->index == 3);
    // Never toward the player.
    std::vector<cx::Candidate> toward{{{500.0, 0.0, 0.0}, true, true, false}};
    CHECK(!cx::PickDestination(toward, person, eyes, lim));
    // Too near or too far.
    std::vector<cx::Candidate> range{{{1100.0, 0.0, 0.0}, true, true, false}, {{3000.0, 0.0, 0.0}, true, true, false}};
    CHECK(!cx::PickDestination(range, person, eyes, lim));
    // Nowhere hidden (an open hall, the council chamber): the farthest place straight away, not hidden (the view's edge decides).
    std::vector<cx::Candidate> open{{{1400.0, 0.0, 0.0}, true, false, false}, {{2050.0, 100.0, 0.0}, true, false, false},
        {{1000.0, 800.0, 0.0}, true, false, false}};
    pick = cx::PickDestination(open, person, eyes, lim);
    CHECK(pick && pick->index == 1 && !pick->hidden);
    // ... but never one off to the side (more than 60 degrees from straight away) or with no way.
    std::vector<cx::Candidate> side{{{1000.0, 800.0, 0.0}, true, false, false}, {{2000.0, 0.0, 0.0}, false, false, false}};
    CHECK(!cx::PickDestination(side, person, eyes, lim));
    // A hidden place beats a farther open one.
    open.push_back({{1000.0, 500.0, 0.0}, true, true, false});
    pick = cx::PickDestination(open, person, eyes, lim);
    CHECK(pick && pick->index == 3 && pick->hidden);
    // Among equally away hidden ones, the nearer.
    std::vector<cx::Candidate> two{{{1900.0, 0.0, 0.0}, true, true, false}, {{1400.0, 0.0, 0.0}, true, true, false}};
    pick = cx::PickDestination(two, person, eyes, lim);
    CHECK(pick && pick->index == 1);
}

void TestStep() {
    cx::Limits const lim;
    // Walking out of sight: kept while seen and walking, applied once unseen for settleS.
    cx::Held h;
    h.heldS = 10.0;
    cx::StepResult r = cx::Step(h, {10.5, true, true, false}, lim);
    CHECK(r.act == cx::Act::Keep);
    r = cx::Step(h, {12.0, false, true, false}, lim);
    CHECK(r.act == cx::Act::Keep);  // just went out of sight
    r = cx::Step(h, {12.1, true, true, false}, lim);  // seen again: the settle starts over
    CHECK(r.act == cx::Act::Keep);
    r = cx::Step(h, {12.2, false, true, false}, lim);
    CHECK(r.act == cx::Act::Keep);
    r = cx::Step(h, {12.2 + lim.settleS, false, true, false}, lim);
    CHECK(r.act == cx::Act::Apply);
    // No walk: the look-away's wait, then under a dip.
    cx::Held w;
    w.heldS = 0.0;
    r = cx::Step(w, {0.0, true, false, false}, lim);
    CHECK(r.act == cx::Act::Keep);
    r = cx::Step(w, {lim.lookAwayS - 0.1, true, false, false}, lim);
    CHECK(r.act == cx::Act::Keep);
    r = cx::Step(w, {lim.lookAwayS, true, false, false}, lim);
    CHECK(r.act == cx::Act::ApplyUnderDip);
    // Looking away during the wait: applied without a dip.
    cx::Held a;
    a.heldS = 0.0;
    cx::Step(a, {0.0, true, false, false}, lim);
    cx::Step(a, {1.0, false, false, false}, lim);
    r = cx::Step(a, {1.0 + lim.settleS, false, false, false}, lim);
    CHECK(r.act == cx::Act::Apply);
    // A walk that never gets out of sight: after walkMaxS it counts as no walk, the wait follows, then the dip.
    cx::Held long_;
    long_.heldS = 0.0;
    r = cx::Step(long_, {lim.walkMaxS - 0.1, true, true, false}, lim);
    CHECK(r.act == cx::Act::Keep);
    CHECK(!cx::WalkOverdue(long_, lim.walkMaxS - 0.1, lim));
    CHECK(cx::WalkOverdue(long_, lim.walkMaxS, lim));
    r = cx::Step(long_, {lim.walkMaxS, true, true, false}, lim);
    CHECK(r.act == cx::Act::Keep);  // the wait begins here
    r = cx::Step(long_, {lim.walkMaxS + lim.lookAwayS, true, true, false}, lim);
    CHECK(r.act == cx::Act::ApplyUnderDip);
    // The absolute cap.
    cx::Held cap;
    cap.heldS = 0.0;
    r = cx::Step(cap, {lim.holdMaxS, true, true, false}, lim);
    CHECK(r.act == cx::Act::ApplyUnderDip);
    // The player sees no world (a screen, a load, the 3D screen): at once, no dip needed.
    cx::Held away;
    away.heldS = 0.0;
    r = cx::Step(away, {0.1, true, true, true}, lim);
    CHECK(r.act == cx::Act::Apply);
}

void TestSwap() {
    cx::Limits const lim;
    CHECK(cx::IsSwap({0, 0, 0}, {50, 0, 0}, 0.0, lim));     // Benezia: the combat pawn shown where the conversation's stood
    CHECK(!cx::IsSwap({0, 0, 0}, {500, 0, 0}, 0.0, lim));   // someone shown elsewhere: no swap
    CHECK(!cx::IsSwap({0, 0, 0}, {50, 0, 0}, 3.0, lim));    // long after the hold: no swap
    CHECK(!cx::IsSwap({0, 0, 0}, {50, 0, 0}, -1.0, lim));
}

void TestVanished() {
    CHECK(cx::VanishedInView(true, true, false, false));    // the pop
    CHECK(!cx::VanishedInView(true, true, false, true));    // under a dip
    CHECK(!cx::VanishedInView(false, true, false, false));  // not seen: nobody saw it
    CHECK(!cx::VanishedInView(true, true, true, false));    // still there
    CHECK(!cx::VanishedInView(true, false, false, false));  // already gone
}

} // namespace

int RunConversationExitTests() {
    gFailures = 0;
    TestParseMode();
    TestClassify();
    TestCandidates();
    TestPick();
    TestStep();
    TestSwap();
    TestVanished();
    return gFailures;
}
