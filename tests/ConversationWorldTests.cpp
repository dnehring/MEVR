// SPDX-License-Identifier: MIT
// Offline tests for conversations in the world (research/59, core/vr/ConversationWorld): the setting's words, the conversation
// movie's subtitle and wheel bands on the HUD frame, the subtitle's place under a face, the place following the head bone's jumps, and
// the host's latest state.

#include "core/vr/ConversationWorld.hpp"

#include <cmath>
#include <cstdio>
#include <span>
#include <string_view>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ConversationWorldTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace cw = mevr::vr::conversation_world;

bool Close(double a, double b, double tolerance = 1e-3) {
    return std::abs(a - b) <= tolerance;
}

void TestParseMode() {
    CHECK(cw::ParseMode("world", cw::Mode::Vr) == cw::Mode::World);
    CHECK(cw::ParseMode(" World ", cw::Mode::Vr) == cw::Mode::World);
    CHECK(cw::ParseMode("2", cw::Mode::Vr) == cw::Mode::World);
    CHECK(cw::ParseMode("1", cw::Mode::Vr) == cw::Mode::Screen);
    CHECK(cw::ParseMode("screen", cw::Mode::Vr) == cw::Mode::Screen);
    CHECK(cw::ParseMode("0", cw::Mode::Screen) == cw::Mode::Vr);
    CHECK(cw::ParseMode("vr", cw::Mode::Screen) == cw::Mode::Vr);
    CHECK(cw::ParseMode("", cw::Mode::Screen) == cw::Mode::Screen);        // missing: the fallback
    CHECK(cw::ParseMode("sideways", cw::Mode::Vr) == cw::Mode::Vr);        // unknown: the fallback
    CHECK(cw::ParseMode(nullptr, cw::Mode::World) == cw::Mode::World);
    CHECK(std::string_view(cw::Name(cw::Mode::World)) == "world");
}

void TestArrangement() {
    // From the player at the origin facing +X (yaw 0): in view within 60 degrees and 1.2 to 6 m (120 to 600 units).
    cw::Vec3 const player{0.0, 0.0, 100.0};
    CHECK(cw::InView(player, 0.0, {250.0, 0.0, 100.0}, 60.0, 120.0, 600.0));
    CHECK(cw::InView(player, 0.0, {200.0, 200.0, 100.0}, 60.0, 120.0, 600.0));     // 45 degrees left
    CHECK(!cw::InView(player, 0.0, {100.0, 300.0, 100.0}, 60.0, 120.0, 600.0));    // 72 degrees: out of the cone
    CHECK(!cw::InView(player, 0.0, {-250.0, 0.0, 100.0}, 60.0, 120.0, 600.0));     // behind
    CHECK(!cw::InView(player, 0.0, {80.0, 0.0, 100.0}, 60.0, 120.0, 600.0));       // too close
    CHECK(!cw::InView(player, 0.0, {700.0, 0.0, 100.0}, 60.0, 120.0, 600.0));      // too far
    CHECK(cw::InView(player, 170.0, {-250.0, 60.0, 100.0}, 60.0, 120.0, 600.0));   // the wrap at 180
    // Seats: around the owner's bearing (here +X), 25 degrees apart, right (negative yaw) then left, on a 2.5 m arc, facing the player.
    std::vector<cw::ArcSeat> const seats = cw::ArcSeats(player, {300.0, 0.0, 100.0}, 3, 250.0, 25.0, 60.0);
    CHECK(seats.size() == 3);
    CHECK(Close(std::hypot(seats[0].at.x, seats[0].at.y), 250.0, 1e-6) && seats[0].at.z == 100.0);
    CHECK(seats[0].at.y < 0.0 && seats[1].at.y > 0.0 && seats[2].at.y < seats[0].at.y);   // right, left, further right
    CHECK(Close(std::atan2(seats[0].at.y, seats[0].at.x) * 180.0 / 3.14159265358979323846, -25.0, 1e-6));
    CHECK(Close(std::atan2(seats[2].at.y, seats[2].at.x) * 180.0 / 3.14159265358979323846, -50.0, 1e-6));
    CHECK(Close(seats[0].yawDeg, 155.0, 1e-6));  // facing back toward the origin
    // Never beyond the view's half angle: the fifth seat would be 75 degrees, capped at 60.
    std::vector<cw::ArcSeat> const many = cw::ArcSeats(player, {300.0, 0.0, 100.0}, 5, 250.0, 25.0, 60.0);
    CHECK(Close(std::atan2(many[4].at.y, many[4].at.x) * 180.0 / 3.14159265358979323846, -60.0, 1e-6));
    CHECK(cw::ArcSeats(player, {300.0, 0.0, 100.0}, 0, 250.0, 25.0, 60.0).empty());
}

void TestSetPieces() {
    // The scan's framed conversations go to the 3D screen in the world mode, by the game's full name or its short forms.
    CHECK(cw::IsSetPiece("BioConversation nor20_ilos_open_d.nor20_ilos_open_dlg"));
    CHECK(cw::IsSetPiece("end90_saren_D.end90_saren_dlg"));
    CHECK(cw::IsSetPiece("JUG70_Sovereign_dlg"));
    CHECK(cw::IsSetPiece("sta60_scenicview"));
    CHECK(cw::IsSetPiece("nor20_ilos_open_d"));
    // Framed for their cameras alone and seen in first person with a place for the player (research/69): in the world.
    CHECK(!cw::IsSetPiece("sta90_council_saren"));
    CHECK(!cw::IsSetPiece("BioConversation nor10_comm_room_d.nor10_comm_room_dlg"));
    CHECK(!cw::IsSetPiece("nor10_captain") && !cw::IsSetPiece("nor20_ilos_approach") && !cw::IsSetPiece("los10_trig_door00"));
    CHECK(!cw::IsSetPiece("sta60_wrex_at_fists"));
    CHECK(!cw::IsSetPiece("BioConversation nor20_medbay_d.nor20_medbay_dlg"));  // the medical bay stays in the world
    CHECK(cw::IsSetPiece("end90_council_alive_dlg"));                           // Shepard walked by the game
    CHECK(cw::IsSetPiece("BioConversation jug20_sal_captainkirrahe_D.jug20_sal_captainkirrahe_dlg"));
    CHECK(!cw::IsSetPiece("end90_saren2_dlg"));                                 // an NPC's walk: in the world, to check
    CHECK(!cw::IsSetPiece("nor10_captain_extra"));
    CHECK(!cw::IsSetPiece(""));
}

void TestStepPlayer() {
    // Walking during a conversation: the stick along the head's yaw, inside the leash.
    cw::Vec3 const anchor{0.0, 0.0, 100.0};
    cw::Vec3 const start{100.0, 0.0, 100.0};
    // Forward on the stick with the head along +Y: the step goes +Y at speed * dt.
    cw::PlayerStep s = cw::StepPlayer(start, anchor, 300.0, 0.0, 1.0, 90.0, 140.0, 0.1);
    CHECK(s.moved && !s.atLeash);
    CHECK(Close(s.place.x, 100.0, 1e-6) && Close(s.place.y, 14.0, 1e-6) && Close(s.place.z, 100.0));
    // Right on the stick with the head along +X: +Y.
    s = cw::StepPlayer(start, anchor, 300.0, 1.0, 0.0, 0.0, 140.0, 0.1);
    CHECK(s.moved && Close(s.place.x, 100.0, 1e-6) && Close(s.place.y, 14.0, 1e-6));
    // The dead zone and no time: no move.
    CHECK(!cw::StepPlayer(start, anchor, 300.0, 0.1, 0.05, 0.0, 140.0, 0.1).moved);
    CHECK(!cw::StepPlayer(start, anchor, 300.0, 0.0, 1.0, 0.0, 140.0, 0.0).moved);
    // Half deflection past the dead zone walks slower than full.
    cw::PlayerStep const half = cw::StepPlayer(start, anchor, 300.0, 0.0, 0.575, 0.0, 140.0, 0.1);
    CHECK(half.moved && Close(half.place.x - start.x, 7.0, 1e-6));
    // At the leash, pushing outward slides along the circle and stays at the radius.
    cw::Vec3 const edge{300.0, 0.0, 100.0};
    s = cw::StepPlayer(edge, anchor, 300.0, 0.0, 1.0, 0.0, 140.0, 0.1);  // straight out along +X
    CHECK(!s.moved && s.atLeash);
    s = cw::StepPlayer(edge, anchor, 300.0, 1.0, 1.0, 0.0, 140.0, 0.1);  // out and to the right: slides
    CHECK(s.moved && s.atLeash && Close(std::hypot(s.place.x, s.place.y), 300.0, 1e-6) && s.place.y > 0.0);
    // Outside already: a step nearer is taken, a step away is not.
    cw::Vec3 const outside{400.0, 0.0, 100.0};
    cw::PlayerStep const nearer = cw::StepPlayer(outside, anchor, 300.0, 0.0, -1.0, 0.0, 140.0, 0.1);
    CHECK(nearer.moved && !nearer.atLeash && Close(nearer.place.x, 386.0, 1e-6));  // as it is, no snap onto the circle
    CHECK(!cw::StepPlayer(outside, anchor, 300.0, 0.0, 1.0, 0.0, 140.0, 0.1).moved);
    // No leash: free.
    CHECK(cw::StepPlayer(outside, anchor, 0.0, 0.0, 1.0, 0.0, 140.0, 0.1).moved);
}

void TestKeepClear() {
    // People in the way of the walk: a step that would end within the clearance of someone is pushed out to it.
    cw::Vec3 const person{200.0, 0.0, 100.0};
    std::vector<cw::Vec3> const people{person};
    // Far enough: the step stands.
    cw::ClearStep c = cw::KeepClear({100.0, 0.0, 100.0}, {114.0, 0.0, 100.0}, people, 70.0);
    CHECK(c.moved && !c.held && Close(c.place.x, 114.0, 1e-6));
    // Straight at them from outside: the step ends at the clearance.
    c = cw::KeepClear({120.0, 0.0, 100.0}, {134.0, 0.0, 100.0}, people, 70.0);
    CHECK(c.moved && c.held && Close(c.place.x, 130.0, 1e-6) && Close(c.place.y, 0.0, 1e-6));
    // From the clearance itself, toward them and to the side: slides around them at the clearance.
    c = cw::KeepClear({130.0, 0.0, 100.0}, {140.0, 10.0, 100.0}, people, 70.0);
    CHECK(c.moved && c.held && Close(std::hypot(c.place.x - 200.0, c.place.y), 70.0, 1e-6) && c.place.y > 0.0);
    // Already inside (they walked up): a step away is taken as it is, a step nearer is refused.
    c = cw::KeepClear({160.0, 0.0, 100.0}, {146.0, 0.0, 100.0}, people, 70.0);
    CHECK(c.moved && c.held && Close(c.place.x, 146.0, 1e-6));
    c = cw::KeepClear({160.0, 0.0, 100.0}, {174.0, 0.0, 100.0}, people, 70.0);
    CHECK(!c.moved && c.held && Close(c.place.x, 160.0, 1e-6));
    // No clearance: free.
    CHECK(cw::KeepClear({120.0, 0.0, 100.0}, {134.0, 0.0, 100.0}, people, 0.0).moved);
    // Two people closer together than two clearances: no way between them, the step is held where it started.
    std::vector<cw::Vec3> const two{{200.0, 0.0, 100.0}, {200.0, 100.0, 100.0}};
    c = cw::KeepClear({120.0, 50.0, 100.0}, {170.0, 50.0, 100.0}, two, 70.0);
    CHECK(c.held && !c.moved && Close(c.place.x, 120.0, 1e-6));
    // Two people with room between them: the step passes.
    std::vector<cw::Vec3> const apart{{200.0, 0.0, 100.0}, {200.0, 200.0, 100.0}};
    c = cw::KeepClear({120.0, 100.0, 100.0}, {170.0, 100.0, 100.0}, apart, 70.0);
    CHECK(c.moved && !c.held && Close(c.place.x, 170.0, 1e-6));
}

void TestSceneRules() {
    // A newcomer during a flowed scene (research/69 item 1).
    using R = cw::NewcomerRule;
    cw::NewcomerFacts scene;  // another scene of the level, started by an action
    scene.sceneOurs = true;
    scene.hasStartAction = true;
    CHECK(cw::DecideNewcomer(scene, R::Hold) == R::Hold);
    CHECK(cw::DecideNewcomer(scene, R::Refuse) == R::Refuse);
    CHECK(cw::DecideNewcomer(scene, R::Through) == R::Through);
    cw::NewcomerFacts bark = scene;  // a bystander's line: refused as the game would, never held
    bark.gameBark = true;
    CHECK(cw::DecideNewcomer(bark, R::Hold) == R::Refuse);
    CHECK(cw::DecideNewcomer(bark, R::Refuse) == R::Refuse);
    CHECK(cw::DecideNewcomer(bark, R::Through) == R::Through);
    cw::NewcomerFacts byCode = scene;  // nothing to fire again: holding would lose it
    byCode.hasStartAction = false;
    CHECK(cw::DecideNewcomer(byCode, R::Hold) == R::Through);
    cw::NewcomerFacts notOurs = scene;  // the game's own ambient conversation: the game's behaviour
    notOurs.sceneOurs = false;
    CHECK(cw::DecideNewcomer(notOurs, R::Hold) == R::Through);
    cw::NewcomerFacts same = scene;
    same.same = true;
    CHECK(cw::DecideNewcomer(same, R::Hold) == R::Through);
    CHECK(cw::ParseNewcomerRule(" Hold ", R::Through) == R::Hold && cw::ParseNewcomerRule("refuse", R::Hold) == R::Refuse);
    CHECK(cw::ParseNewcomerRule("through", R::Hold) == R::Through && cw::ParseNewcomerRule("?", R::Hold) == R::Hold);
    CHECK(std::string(cw::Name(R::Hold)) == "hold");
    // The opening of a staged conversation: a cut only when the composition is another one.
    {
        using O = cw::Opening;
        using OR = cw::OpeningRule;
        cw::OpeningFacts near;  // the player walked up to someone and used him: the mark is a step away
        near.known = true;
        near.markUnits = 90.0;
        near.sightToMark = true;
        CHECK(cw::DecideOpening(near, OR::Auto) == O::Stay);
        CHECK(cw::DecideOpening(near, OR::Accept) == O::Cut);  // the old way, by the setting
        cw::OpeningFacts hall = near;  // the council: the podium 24 m on, 3 m up the stairs, in sight
        hall.markUnits = 2400.0;
        hall.riseUnits = 290.0;
        CHECK(cw::DecideOpening(hall, OR::Auto) == O::WalkIn);
        CHECK(cw::DecideOpening(hall, OR::Keep) == O::Stay);
        cw::OpeningFacts elsewhere = hall;  // behind a wall, on another deck, too far, or nowhere to stand
        elsewhere.sightToMark = false;
        CHECK(cw::DecideOpening(elsewhere, OR::Auto) == O::Cut);
        elsewhere = hall;
        elsewhere.riseUnits = 900.0;
        CHECK(cw::DecideOpening(elsewhere, OR::Auto) == O::Cut);
        elsewhere = hall;
        elsewhere.markUnits = 9000.0;
        CHECK(cw::DecideOpening(elsewhere, OR::Auto) == O::Cut);
        // A placement the game repeats every tick (a walk's last step onto its goal): undone once, accepted when it comes back.
        cw::Insistence snap;
        cw::Vec3 const goal{100.0, 200.0, 0.0};
        CHECK(!cw::Insists(snap, goal, 33.0));
        CHECK(cw::Insists(snap, cw::Vec3{101.0, 200.0, 0.0}, 33.0));
        cw::Insistence slow;  // with a limit of three: twice no, then yes
        cw::InsistLimits three;
        three.ticks = 3;
        CHECK(!cw::Insists(slow, goal, 33.0, three));
        CHECK(!cw::Insists(slow, goal, 33.0, three));
        CHECK(cw::Insists(slow, goal, 33.0, three));
        // Somewhere else each tick, or a calm tick in between: the count starts again.
        cw::Insistence wander;
        CHECK(!cw::Insists(wander, goal, 40.0));
        CHECK(!cw::Insists(wander, cw::Vec3{160.0, 200.0, 0.0}, 40.0));
        CHECK(!cw::Insists(wander, cw::Vec3{220.0, 200.0, 0.0}, 40.0));
        cw::Insistence calmed;
        CHECK(!cw::Insists(calmed, goal, 33.0));
        cw::Calm(calmed);
        CHECK(!cw::Insists(calmed, goal, 33.0));
        // A walk-in nobody walks: guided after the wait, taken back by the stick, given up when blocked.
        {
            using G = cw::Guide;
            cw::GuideState g;
            CHECK(cw::StepGuide(g, false, false, false, 0.1, 5.0) == G::Wait);  // no mark wanted
            for (int i = 0; i < 39; ++i) {
                CHECK(cw::StepGuide(g, true, false, false, 0.125, 5.0) == G::Wait);  // 4.875 s of waiting (eighths: exact sums)
            }
            CHECK(cw::StepGuide(g, true, false, false, 0.125, 5.0) == G::Walk);
            CHECK(cw::StepGuide(g, true, false, true, 0.1, 5.0) == G::Walk);
            CHECK(cw::StepGuide(g, true, true, true, 0.1, 5.0) == G::Wait);    // the stick: his own walk
            CHECK(cw::StepGuide(g, true, false, true, 0.1, 5.0) == G::Wait);   // and the wait starts again
            cw::GuideState blocked;
            CHECK(cw::StepGuide(blocked, true, false, false, 0.1, 0.0) == G::Walk);  // no wait: at once
            for (int i = 0; i < 16; ++i) {
                CHECK(cw::StepGuide(blocked, true, false, false, 0.125, 0.0) == G::Walk);  // 2 s against a wall
            }
            CHECK(cw::StepGuide(blocked, true, false, false, 0.125, 0.0) == G::GaveUp);
            CHECK(cw::StepGuide(blocked, true, false, true, 0.1, 0.0) == G::GaveUp);  // for this conversation
            CHECK(cw::StepGuide(blocked, true, true, false, 0.1, 0.0) == G::Wait);    // until he walks himself
            cw::GuideState never;
            for (int i = 0; i < 200; ++i) {
                CHECK(cw::StepGuide(never, true, false, false, 0.1, -1.0) == G::Wait);
            }
            cw::GuideState there;  // arrived: the state starts over
            CHECK(cw::StepGuide(there, true, false, false, 0.1, 0.0) == G::Walk);
            CHECK(cw::StepGuide(there, false, false, true, 0.1, 0.0) == G::Wait);
            CHECK(!there.guiding && there.idleS == 0.0);
        }
        // Who turns: the body stays while the look-at chain covers the target, and re-faces only so far as it must.
        {
            cw::AttentionState a;
            cw::AttentionLimits const lim;  // comfort 60, settle 25, dwell 1 s
            // A speaker 40 degrees to the side: the head's business, for as long as it lasts.
            for (int i = 0; i < 80; ++i) {
                cw::AttentionStep const s = cw::StepAttention(a, 10.0, 50.0, 0.125, lim);
                CHECK(!s.turning && !s.began);
            }
            CHECK(a.outsideS == 0.0);
            // A glance past the comfort cone that ends in time: nothing.
            for (int i = 0; i < 7; ++i) {
                CHECK(!cw::StepAttention(a, 0.0, 100.0, 0.125, lim).turning);  // 0.875 s
            }
            CHECK(!cw::StepAttention(a, 0.0, 30.0, 0.125, lim).turning);
            CHECK(a.outsideS == 0.0);
            // Held past the dwell: the body turns, only until the target is 25 degrees off, on the side it came from.
            cw::AttentionStep began;
            for (int i = 0; i < 8; ++i) {
                began = cw::StepAttention(a, 0.0, 100.0, 0.125, lim);
            }
            CHECK(began.began && began.turning && std::abs(began.goalYawDeg - 75.0) < 1e-9 && std::abs(began.offDeg - 100.0) < 1e-9);
            CHECK(!cw::StepAttention(a, 40.0, 100.0, 0.125, lim).began);        // on its way: one beginning
            CHECK(cw::StepAttention(a, 40.0, 100.0, 0.125, lim).turning);
            CHECK(!cw::StepAttention(a, 75.0, 100.0, 0.125, lim).turning);      // arrived
            CHECK(!a.turning && a.outsideS == 0.0);
            // Across the wrap: a target at -170 from a body at 150 is 40 degrees to its left: inside.
            cw::AttentionState w;
            CHECK(!cw::StepAttention(w, 150.0, -170.0, 5.0, lim).turning);
            // ... and one at -100 is 110 to its left: the body turns left, to 25 short of it.
            cw::AttentionStep const left = cw::StepAttention(w, 150.0, -100.0, 5.0, lim);
            CHECK(left.began && std::abs(left.goalYawDeg - (-125.0)) < 1e-9 && left.offDeg > 0.0);
            cw::AttentionState r;
            cw::AttentionStep const right = cw::StepAttention(r, -150.0, 100.0, 5.0, lim);
            CHECK(right.began && std::abs(right.goalYawDeg - 125.0) < 1e-9 && right.offDeg < 0.0);
            // Nobody to look at: the state starts over.
            CHECK(!cw::StepAttention(w, 150.0, std::nullopt, 0.1, lim).turning && !w.turning);
            // The turn nothing animates: capped, slower near the goal, never past it.
            CHECK(std::abs(cw::TurnStepDeg(90.0, 0.1, 120.0) - 12.0) < 1e-9);    // the cap
            CHECK(std::abs(cw::TurnStepDeg(-30.0, 0.1, 120.0) + 6.0) < 1e-9);    // 60 deg/s at 30 off
            CHECK(std::abs(cw::TurnStepDeg(2.0, 0.1, 120.0) - 2.0) < 1e-9);      // the floor (24 deg/s) ends it
            CHECK(cw::TurnStepDeg(0.0, 0.1, 120.0) == 0.0);
            using T = cw::TurnRule;
            CHECK(cw::ParseTurnRule("Look", T::Always) == T::Look && cw::ParseTurnRule(" always ", T::Look) == T::Always);
            CHECK(cw::ParseTurnRule("off", T::Look) == T::Off && cw::ParseTurnRule("sideways", T::Look) == T::Look);
            CHECK(cw::ParseTurnRule("roles", T::Look) == T::Roles);
        }
        // Who attends to whom, by their part in the line (the Spectre ceremony: Udina speaks to the player; Anderson stands
        // in front of Udina with his back to him, the councillors face them from across the chamber).
        {
            using Part = cw::Role;
            std::uint64_t const udina = 0x1000, player = 0x2000, anderson = 0x3000, tevos = 0x4000;
            CHECK(cw::RoleOf(udina, udina, player) == Part::Speaker && cw::RoleOf(player, udina, player) == Part::Addressed);
            CHECK(cw::RoleOf(anderson, udina, player) == Part::Bystander && cw::RoleOf(udina, udina, udina) == Part::Speaker);
            CHECK(cw::RoleOf(0, 0, 0) == Part::Bystander);
            CHECK(cw::PartnerOf(udina, udina, player) == player && cw::PartnerOf(player, udina, player) == udina);
            CHECK(cw::PartnerOf(anderson, udina, player) == udina && cw::PartnerOf(udina, udina, 0) == 0);
            CHECK(cw::PartnerOf(udina, udina, udina) == 0 && cw::PartnerOf(anderson, 0, player) == 0);
            cw::RoleLimits const lim;  // glance 70, kept to 85
            // The speaker 78 degrees off the one he speaks to: he looks, and his body may turn (StepAttention decides).
            cw::RoleState s;
            cw::RoleAttention const speaks = cw::AttendAs(s, Part::Speaker, udina, 78.0, true, lim);
            CHECK(speaks.look && speaks.bodyMayTurn && std::abs(speaks.delayS - 0.15) < 1e-9);
            // Anderson, 147 degrees off Udina: heard, not looked at; the body never turns.
            cw::RoleState a;
            cw::RoleAttention const hears = cw::AttendAs(a, Part::Bystander, anderson, -147.0, true, lim);
            CHECK(!hears.look && !hears.bodyMayTurn);
            // A councillor, 12 degrees off him across the chamber: a glance, later than the pair, never the body.
            cw::RoleState c;
            cw::RoleAttention const glance = cw::AttendAs(c, Part::Bystander, tevos, 12.0, true, lim);
            CHECK(glance.look && !glance.bodyMayTurn && glance.delayS >= 0.45 && glance.delayS <= 1.05 + 1e-9);
            // Not in unison: another bystander's glance begins at another moment; the same one's at the same.
            CHECK(cw::AttendAs(c, Part::Bystander, tevos, 12.0, true, lim).delayS == glance.delayS);
            cw::RoleState c2;
            CHECK(cw::AttendAs(c2, Part::Bystander, anderson, 12.0, true, lim).delayS != glance.delayS);
            // The glance is kept until the speaker is past 85 degrees, and taken again only within 70.
            CHECK(cw::AttendAs(c, Part::Bystander, tevos, 80.0, true, lim).look);
            CHECK(!cw::AttendAs(c, Part::Bystander, tevos, 90.0, true, lim).look);
            CHECK(!cw::AttendAs(c, Part::Bystander, tevos, 80.0, true, lim).look);
            CHECK(cw::AttendAs(c, Part::Bystander, tevos, -60.0, true, lim).look);
            // One of the pair whose body cannot turn for it (the other is not the player, or far): the look only within
            // 120 degrees; the ceremony's first line, Udina with the player 8 m behind him (176 degrees): no look.
            cw::RoleState u;
            cw::RoleAttention const behind = cw::AttendAs(u, Part::Speaker, udina, 176.0, false, lim);
            CHECK(!behind.look && !behind.bodyMayTurn);
            CHECK(cw::AttendAs(u, Part::Speaker, udina, 100.0, false, lim).look);
            CHECK(cw::AttendAs(u, Part::Speaker, udina, 176.0, true, lim).look);  // near him: the body turns and the head leads
            // Nobody to attend to.
            CHECK(!cw::AttendAs(c, Part::Bystander, tevos, std::nullopt, true, lim).look && !c.glancing);
            CHECK(!cw::AttendAs(s, Part::Speaker, udina, std::nullopt, true, lim).look);
            // The look-at chain's reach as the game's code has it: the humanoid definition's strengths leave the head 33
            // degrees short of a target 79 off the body (measured: 21 to 36), more than the eyes' 25; with 0.4 of the rest
            // added to each link (the pair's heads) 9 short.
            cw::LookLink const chain[] = {{0.125, 50.0}, {0.25, 50.0}, {0.55, 50.0}};
            CHECK(std::abs(cw::HeadShortDeg(79.0, chain) - 32.75) < 0.01);
            CHECK(cw::HeadShortDeg(70.0, chain) < 25.0 && cw::HeadShortDeg(75.0, chain) > 25.0);  // the eyes cover it up to 71
            CHECK(std::abs(cw::StrongerLink(0.55, 0.4) - 0.73) < 1e-9 && cw::StrongerLink(1.0, 0.4) == 1.0 && cw::StrongerLink(0.25, 0.0) == 0.25);
            cw::LookLink const direct[] = {{cw::StrongerLink(0.125, 0.4), 50.0}, {cw::StrongerLink(0.25, 0.4), 50.0}, {cw::StrongerLink(0.55, 0.4), 50.0}};
            CHECK(cw::HeadShortDeg(79.0, direct) < 10.0);
            CHECK(cw::HeadShortDeg(-79.0, chain) == cw::HeadShortDeg(79.0, chain));
            cw::LookLink const stiff[] = {{1.0, 10.0}};
            CHECK(std::abs(cw::HeadShortDeg(40.0, stiff) - 30.0) < 1e-9);  // a link never turns past its limit
        }
        // Where a voice comes from: near speakers from their heads, far ones left to the game's centred mix.
        {
            cw::VoiceLimits const lim;  // placed within 8 m (kept to 10); full level within 2 m, 3 dB a doubling, at most 6
            cw::Vec3 const ear{0.0, 0.0, 170.0};
            // Anderson, 1.8 m to the right: placed at his head, at the game's level.
            cw::VoiceState anderson;
            cw::VoiceAt const beside = cw::PlaceVoice(anderson, ear, {0.0, 180.0, 170.0}, lim);
            CHECK(beside.placed && std::abs(beside.at.y - 180.0) < 1e-9 && std::abs(beside.at.x) < 1e-9 && std::abs(beside.distanceUnits - 180.0) < 1e-9);
            CHECK(beside.gain == 1.0);
            // Someone 4 m ahead: 3 dB quieter; 8 m: 6 dB; never more.
            cw::VoiceState far4;
            cw::VoiceAt const ahead = cw::PlaceVoice(far4, ear, {400.0, 0.0, 170.0}, lim);
            CHECK(ahead.placed && std::abs(ahead.at.x - 400.0) < 1e-9 && std::abs(20.0 * std::log10(ahead.gain) + 3.0) < 1e-6);
            CHECK(std::abs(20.0 * std::log10(cw::PlaceVoice(far4, ear, {800.0, 0.0, 170.0}, lim).gain) + 6.0) < 1e-6);
            cw::VoiceLimits wide = lim;
            wide.withinUnits = wide.keepUnits = 5000.0;
            cw::VoiceState far30;
            CHECK(std::abs(20.0 * std::log10(cw::PlaceVoice(far30, ear, {3000.0, 0.0, 170.0}, wide).gain) + 6.0) < 1e-6);
            wide.falloffDb = 0.0;
            CHECK(cw::PlaceVoice(far30, ear, {3000.0, 0.0, 170.0}, wide).gain == 1.0);
            // A councillor, 17 m away: the game's own mix (the intercom).
            cw::VoiceState council;
            CHECK(!cw::PlaceVoice(council, ear, {1700.0, 100.0, 270.0}, lim).placed);
            // A step back mid-line changes nothing: placed at 7.5 m stays placed at 9.5, lets go past 10, comes back within 8.
            cw::VoiceState walker;
            CHECK(cw::PlaceVoice(walker, ear, {750.0, 0.0, 170.0}, lim).placed);
            CHECK(cw::PlaceVoice(walker, ear, {950.0, 0.0, 170.0}, lim).placed);
            CHECK(!cw::PlaceVoice(walker, ear, {1050.0, 0.0, 170.0}, lim).placed);
            CHECK(!cw::PlaceVoice(walker, ear, {900.0, 0.0, 170.0}, lim).placed);
            CHECK(cw::PlaceVoice(walker, ear, {790.0, 0.0, 170.0}, lim).placed);
            // At the listener's own place, and a place that is no number.
            cw::VoiceState onTheSpot;
            CHECK(cw::PlaceVoice(onTheSpot, ear, ear, lim).placed);
            CHECK(!cw::PlaceVoice(onTheSpot, ear, {std::nan(""), 0.0, 0.0}, lim).placed && !onTheSpot.placed);
        }
        // The wheel's stick is not the walk's until it has been let go.
        {
            cw::StickLatch latch;
            CHECK(cw::StickWalks(latch, false, 0.9));            // no wheel yet: a walk-in walked with the stick held
            CHECK(!cw::StickWalks(latch, true, 0.9));            // the wheel is up: the stick is the wheel's
            CHECK(!cw::StickWalks(latch, false, 0.9));           // the reply picked, the stick still out: no walk
            CHECK(!cw::StickWalks(latch, false, 0.4));
            CHECK(!cw::StickWalks(latch, false, 0.1) && !latch.held);  // let go
            CHECK(cw::StickWalks(latch, false, 0.9));            // pushed again: a walk
            cw::StickLatch centred;
            CHECK(!cw::StickWalks(centred, true, 0.0));
            CHECK(!cw::StickWalks(centred, false, 0.0));         // closed with the stick at rest: free from the next tick
            CHECK(cw::StickWalks(centred, false, 0.9));
        }
        // What a script's black screen covered: someone gone, moved at once or new in the player's view.
        {
            cw::Seen const anderson{1, {100.0, 0.0, 0.0}};
            cw::Seen const udina{2, {400.0, 50.0, 0.0}};
            cw::Seen const before[] = {anderson, udina};
            cw::Seen const stepped[] = {udina, {1, {130.0, 20.0, 0.0}}};                 // a step: nothing
            CHECK(cw::CoveredChanges(before, stepped).Total() == 0);
            cw::Seen const gone[] = {udina};                                           // switched off on the stairs
            CHECK(cw::CoveredChanges(before, gone).gone == 1 && cw::CoveredChanges(before, gone).Total() == 1);
            cw::Seen const moved[] = {{1, {100.0, 900.0, 0.0}}, udina};               // teleported in plain sight
            CHECK(cw::CoveredChanges(before, moved).moved == 1);
            cw::Seen const more[] = {anderson, udina, {3, {0.0, 0.0, 0.0}}};          // someone put into the view
            CHECK(cw::CoveredChanges(before, more).appeared == 1 && cw::CoveredChanges(before, more).gone == 0);
            CHECK(cw::CoveredChanges({}, {}).Total() == 0);
            // How near the change is: the Council appearing 20 m ahead is far, someone put beside the player is near.
            cw::Vec3 const eye{0.0, 0.0, 0.0};
            CHECK(std::isinf(cw::NearestChange(before, stepped, eye)));
            cw::Seen const council[] = {anderson, udina, {7, {2000.0, 0.0, 0.0}}, {8, {2100.0, 100.0, 0.0}}};
            CHECK(std::fabs(cw::NearestChange(before, council, eye) - 2000.0) < 1e-9);
            CHECK(std::fabs(cw::NearestChange(before, more, eye)) < 1e-9);
            CHECK(std::fabs(cw::NearestChange(before, gone, eye) - std::hypot(100.0, 0.0)) < 150.0);  // Anderson, from where he stood
            CHECK(cw::NearestChange(before, moved, eye) <= std::hypot(100.0, 900.0));
        }
        // The guided walk follows the way Shepard went, not the straight line to him.
        {
            // Along +X for 4 m, then a corner and 4 m along +Y, a point every half metre.
            std::vector<cw::Vec3> trail;
            for (int i = 0; i <= 8; ++i) trail.push_back({i * 50.0, 0.0, 0.0});
            for (int i = 1; i <= 8; ++i) trail.push_back({400.0, i * 50.0, 0.0});
            auto const first = cw::TrailTarget(trail, cw::Vec3{0.0, 0.0, 0.0});
            CHECK(first && first->x == 150.0 && first->y == 0.0);                      // 1.5 m ahead along the trail
            auto const corner = cw::TrailTarget(trail, cw::Vec3{330.0, 20.0, 0.0});    // near the corner: round it, not across
            CHECK(corner && corner->x == 400.0 && corner->y == 100.0);
            auto const off = cw::TrailTarget(trail, cw::Vec3{200.0, -120.0, 0.0});     // off the trail: its nearest part, ahead
            CHECK(off && off->x == 350.0 && off->y == 0.0);
            CHECK(!cw::TrailTarget(trail, cw::Vec3{400.0, 330.0, 0.0}));               // the end is near: the pawn himself
            CHECK(!cw::TrailTarget({}, cw::Vec3{0.0, 0.0, 0.0}));
        }
        // How a walk-in is walked: the body by default, the view when the pawn must not be moved.
        {
            using W = cw::WalkInWay;
            using WR = cw::WalkInRule;
            CHECK(cw::DecideWalkIn({}, WR::Auto) == W::Body);
            CHECK(cw::DecideWalkIn({true, false}, WR::Auto) == W::View);   // a moment's walk was on its way (the council)
            CHECK(cw::DecideWalkIn({false, true}, WR::Auto) == W::View);   // the mark stands inside a trigger
            CHECK(cw::DecideWalkIn({true, true}, WR::Body) == W::Body);    // by the setting
            CHECK(cw::DecideWalkIn({}, WR::View) == W::View);
            CHECK(cw::ParseWalkInRule("View", WR::Auto) == WR::View);
            CHECK(cw::ParseWalkInRule(" body ", WR::Auto) == WR::Body);
            CHECK(cw::ParseWalkInRule("auto", WR::Body) == WR::Auto);
            CHECK(cw::ParseWalkInRule("sideways", WR::Body) == WR::Body);
            CHECK(std::string_view(cw::Name(W::View)) == "view" && std::string_view(cw::Name(W::Body)) == "body");
        }
        // A stage mark metres away is never accepted this way, however often the game asks.
        cw::Insistence mark;
        for (int i = 0; i < 10; ++i) {
            CHECK(!cw::Insists(mark, goal, 450.0));
        }
        elsewhere = hall;
        elsewhere.floorAtMark = false;
        CHECK(cw::DecideOpening(elsewhere, OR::Auto) == O::Cut);
        cw::OpeningFacts rearranged = near;  // the stage put the people he was looking at somewhere else
        rearranged.peopleMovedUnits = 500.0;
        CHECK(cw::DecideOpening(rearranged, OR::Auto) == O::Cut);
        cw::OpeningFacts noBody = near;
        noBody.shepardHidden = true;
        CHECK(cw::DecideOpening(noBody, OR::Auto) == O::Cut);
        cw::OpeningFacts unknown = near;  // no place on foot to stay at (the conversation followed a cutscene)
        unknown.known = false;
        CHECK(cw::DecideOpening(unknown, OR::Auto) == O::Cut);
        CHECK(cw::DecideOpening(unknown, OR::Keep) == O::Stay);
        CHECK(cw::ParseOpeningRule(" Auto ", OR::Accept) == OR::Auto && cw::ParseOpeningRule("keep", OR::Accept) == OR::Keep);
        CHECK(cw::ParseOpeningRule("?", OR::Accept) == OR::Accept && std::string(cw::Name(OR::Auto)) == "auto");
        CHECK(std::string(cw::Name(O::WalkIn)) == "walk in" && std::string(cw::Name(O::Cut)) == "cut");
    }
    // The held starts: first come, first fired, once free; a start held twice counts; old ones are dropped.
    std::vector<cw::HeldStart> held;
    CHECK(cw::HoldStart(held, 11, 1000) && cw::HoldStart(held, 22, 1500));
    CHECK(!cw::HoldStart(held, 11, 1600) && held.size() == 2 && held[0].times == 2);
    CHECK(!cw::HoldStart(held, 0, 1600));
    CHECK(!cw::ReleaseStart(held, false, 2000, 60000));          // the scene still plays
    auto first = cw::ReleaseStart(held, true, 2000, 60000);
    CHECK(first && *first == 11 && held.size() == 1);
    std::vector<std::uint64_t> expired;
    CHECK(!cw::ReleaseStart(held, true, 90000, 60000, &expired));  // 22 waited too long: dropped, not fired
    CHECK(expired.size() == 1 && expired[0] == 22 && held.empty());
    for (std::uint64_t id = 1; id <= 8; ++id) CHECK(cw::HoldStart(held, id, 0));
    CHECK(!cw::HoldStart(held, 9, 0));  // eight at most
}

void TestFlowEntries() {
    // The scan's chain starts (research/59 "Flowing conversations"): Garrus arguing with the Executor is the conversation's
    // first two entries; Udina's arrival lines; the key in any of the name's forms.
    CHECK(cw::ConversationKey("BioConversation sta70_garrus_D.sta70_garrus_dlg") == "sta70_garrus");
    CHECK(cw::ConversationKey("STA70_Garrus_dlg") == "sta70_garrus");
    CHECK(cw::ConversationKey("sta70_garrus") == "sta70_garrus");
    std::span<int const> const garrus = cw::FlowEntries("BioConversation sta70_garrus_D.sta70_garrus_dlg");
    CHECK(garrus.size() == 2 && garrus[0] == 0 && garrus[1] == 1);
    std::span<int const> const udina = cw::FlowEntries("sta20_human_ambassador_enter_dlg");
    CHECK(udina.size() == 9 && udina[0] == 35 && udina[8] == 94);
    CHECK(cw::FlowEntries("sta60_wrex_at_fists_dlg").empty());   // a set piece: on the 3D screen, never barks
    CHECK(cw::FlowEntries("sp110_distress_call_dlg").empty());   // a transmission: its hologram needs the stage
    CHECK(cw::FlowEntries("nor10_backgrounds_dlg").empty());     // narration under the character creation
    CHECK(cw::FlowEntries("sta60_quarian_dlg").empty());         // a wheel from its first line
    CHECK(cw::FlowEntries("sta30_krogan_cutscene_dlg").empty()); // its chain start sits behind an unconditional start: never plays
    CHECK(cw::FlowEntries("").empty());
    // A chain that applies plot effects after its first line stays a conversation (research/69 item 1): as barks it could be
    // cut short (distance, another conversation starting) and lose them.
    CHECK(cw::FlowEntries("war50_krogan_dlg").empty());
    CHECK(cw::FlowEntries("jug20_wrex_dlg").empty());
    CHECK(cw::FlowEntries("war40_juliannabaynham_dlg").empty());
    std::span<int const> const olar = cw::FlowEntries("ice60_trig12_olar_execution_dlg");  // played as barks on the fake headset
    CHECK(olar.size() == 2 && olar[0] == 0 && olar[1] == 1);
    // Every table row is a non-set-piece with entries in order.
    for (char const* name : {"sta30_chellik", "ice50_trig22_juleth_ambush", "nor10_joker", "jug80_saren", "pro20_powell"}) {
        std::span<int const> const e = cw::FlowEntries(name);
        CHECK(!e.empty() && !cw::IsSetPiece(name));
        for (std::size_t i = 1; i < e.size(); ++i) {
            CHECK(e[i] > e[i - 1]);
        }
    }
}

void TestCrops() {
    // The HUD frame of the PS VR2's eye: 2344 x 2392, the 1280 x 720 stage across its width, centred top to bottom.
    std::uint32_t const w = 2344;
    std::uint32_t const h = 2392;
    double const scale = w / 1280.0;
    double const top = (h - 720.0 * scale) / 2.0;  // 537 px
    CHECK(Close(top, 536.75, 0.01));
    auto const wheel = cw::DefaultWheelCrop(w, h);
    CHECK(wheel.Valid());
    // The wheel measured at frame y 1620..1780 (its rows) inside the band.
    CHECK(wheel.y * h < 1620.0 && (wheel.y + wheel.height) * h > 1780.0);
    // Centred on the movie's authored x (639.7 of 1280), within a stage pixel.
    CHECK(Close((wheel.x + wheel.width * 0.5) * 1280.0, 640.0, 1.0));
    auto const sub = cw::DefaultSubtitleCrop(w, h);
    CHECK(sub.Valid());
    // A one-row line measured at frame y 1569 inside the band; the band ends where the wheel's begins.
    CHECK(sub.y * h < 1569.0 && (sub.y + sub.height) * h > 1569.0);
    CHECK(Close(sub.y + sub.height, wheel.y, 1e-4));
    CHECK(Close(sub.x, 80.0 / 1280.0) && Close(sub.width, 1120.0 / 1280.0));
    // A frame exactly 16:9 puts the stage on the whole frame: the bands at their stage places.
    auto const sub169 = cw::DefaultSubtitleCrop(1280, 720);
    CHECK(Close(sub169.y * 720.0, 480.0, 0.01) && Close(sub169.height * 720.0, 106.0, 0.01));
    // No frame: nothing.
    CHECK(!cw::DefaultSubtitleCrop(0, 0).Valid());
    // A frame wider than 16:9 (the stage taller than the frame): the band clipped to the frame, never negative.
    auto const wide = cw::DefaultWheelCrop(2000, 500);
    CHECK(wide.y >= 0.0f && wide.y + wide.height <= 1.0001f);
}

void TestSubtitlePoint() {
    // A face 2 m ahead (-Z), at eye height; the line 0.22 m under it, moved 0.15 m toward the eyes.
    XrVector3f const face{0.0f, 1.6f, -2.0f};
    XrVector3f const eyes{0.0f, 1.7f, 0.0f};
    XrVector3f out{};
    CHECK(cw::SubtitlePoint(face, eyes, 0.22f, 0.15f, out));
    CHECK(Close(out.x, 0.0) && Close(out.y, 1.38) && Close(out.z, -1.85));
    // Toward the eyes at most half the way: a face 0.2 m away takes 0.1 m.
    XrVector3f const closeFace{0.2f, 1.6f, 0.0f};
    CHECK(cw::SubtitlePoint(closeFace, eyes, 0.2f, 0.5f, out));
    CHECK(Close(out.x, 0.1) && Close(out.y, 1.4));
    // Straight above or below the eyes (no level line to them): no place.
    CHECK(!cw::SubtitlePoint({0.0f, 0.5f, 0.0f}, eyes, 0.2f, 0.1f, out));
    // A negative drop is none.
    CHECK(cw::SubtitlePoint(face, eyes, -1.0f, 0.0f, out) && Close(out.y, 1.6));
}

void TestFollowHead() {
    cw::Vec3 const place{100.0, 200.0, 160.0};
    cw::Vec3 const anchor{95.0, 190.0, 130.0};
    // A gesture's 15 units: the place stays.
    cw::Follow const still = cw::FollowHead(place, anchor, {105.0, 195.0, 135.0}, 40.0);
    CHECK(!still.jumped && Close(still.place.x, 100.0) && Close(still.anchor.x, 95.0));
    // The stage puts Shepard 2.25 m along -Y: the place moves with him, the new head is the anchor.
    cw::Follow const moved = cw::FollowHead(place, anchor, {95.0, -35.0, 130.0}, 40.0);
    CHECK(moved.jumped && Close(moved.place.x, 100.0) && Close(moved.place.y, -25.0) && Close(moved.place.z, 160.0));
    CHECK(Close(moved.anchor.y, -35.0));
    // Exactly at the threshold: no jump.
    CHECK(!cw::FollowHead(place, anchor, {135.0, 190.0, 130.0}, 40.0).jumped);

    // Settled: a jump there and back within a tick (the mesh catching up with a pawn put back) isn't followed...
    cw::FollowState s{place, anchor, 0};
    CHECK(!cw::FollowHeadSettled(s, {95.0, -35.0, 130.0}, 40.0, 3));
    CHECK(!cw::FollowHeadSettled(s, {95.0, 190.0, 130.0}, 40.0, 3));
    CHECK(s.away == 0 && Close(s.place.y, 200.0));
    // ... a lasting one is, on its third tick, by the head's place then.
    CHECK(!cw::FollowHeadSettled(s, {95.0, -35.0, 130.0}, 40.0, 3));
    CHECK(!cw::FollowHeadSettled(s, {95.0, -35.0, 130.0}, 40.0, 3));
    CHECK(cw::FollowHeadSettled(s, {95.0, -36.0, 130.0}, 40.0, 3));
    CHECK(Close(s.place.y, -26.0) && Close(s.anchor.y, -36.0) && s.away == 0);
    // settle 0 or 1: at once.
    cw::FollowState now{place, anchor, 0};
    CHECK(cw::FollowHeadSettled(now, {95.0, -35.0, 130.0}, 40.0, 0));
}

void TestMarkWalk() {
    cw::Vec3 const standing{100.0, 200.0, 50.0};
    cw::Vec3 const mark{250.0, 200.0, 50.0};
    cw::MarkWalk walk;
    CHECK(walk.Phase() == cw::WalkPhase::Idle);
    CHECK(!walk.StartIfSettled(standing, 10.0, 2.5, 50.0));  // no placement, no walk

    walk.Jump(mark, standing, 10.0, 50.0);
    CHECK(Close(walk.Wanted().x, 250.0) && Close(walk.WantedAtS(), 10.0));
    CHECK(walk.Phase() == cw::WalkPhase::Waiting);
    CHECK(!walk.StartIfSettled(standing, 12.499, 2.5, 50.0));
    CHECK(walk.StartIfSettled(standing, 12.5, 2.5, 50.0));  // exact settle boundary
    CHECK(walk.Phase() == cw::WalkPhase::Walking);
    CHECK(!walk.StartIfSettled(standing, 20.0, 2.5, 50.0));  // start once
    walk.End();
    CHECK(!walk.StartIfSettled(standing, 30.0, 2.5, 50.0));  // arrival/timeout consumes the request

    // Every newer placement restarts the clock, including the SAME mark.
    walk.Jump(mark, standing, 30.0, 50.0);
    walk.Jump(mark, standing, 32.0, 50.0);
    CHECK(Close(walk.WantedAtS(), 32.0));
    CHECK(!walk.StartIfSettled(standing, 32.5, 2.5, 50.0));
    walk.Jump({100.0, 400.0, 50.0}, standing, 34.0, 50.0);
    CHECK(!walk.StartIfSettled(standing, 36.499, 2.5, 50.0));
    CHECK(walk.StartIfSettled(standing, 36.5, 2.5, 50.0));
    CHECK(Close(walk.Wanted().y, 400.0));

    // A newer mark interrupts a running walk; a return to where the actor stands cancels either phase.
    walk.Jump(mark, standing, 37.0, 50.0);
    CHECK(walk.Phase() == cw::WalkPhase::Waiting);
    walk.Jump(standing, standing, 38.0, 50.0);
    CHECK(walk.Phase() == cw::WalkPhase::Idle);
    CHECK(!walk.StartIfSettled(standing, 100.0, 2.5, 50.0));
    walk.Jump(mark, standing, 100.0, 50.0);
    CHECK(walk.StartIfSettled(standing, 102.5, 2.5, 50.0));
    cw::Vec3 const halfway{180.0, 200.0, 50.0};
    walk.Jump({210.0, 200.0, 50.0}, halfway, 103.0, 50.0);
    CHECK(walk.Phase() == cw::WalkPhase::Idle);  // within 0.5 m of its CURRENT place

    // Strict minimum distance, including vertical distance; zero dwell can start at once.
    walk.Jump({150.0, 200.0, 50.0}, standing, 104.0, 50.0);
    CHECK(walk.Phase() == cw::WalkPhase::Idle);
    walk.Jump({100.0, 200.0, 100.01}, standing, 104.0, 50.0);
    CHECK(walk.StartIfSettled(standing, 104.0, 0.0, 50.0));

    // The actor can move on its own during the wait: re-check the distance when deciding.
    walk.Jump(mark, standing, 110.0, 50.0);
    CHECK(!walk.StartIfSettled({240.0, 200.0, 50.0}, 112.5, 2.5, 50.0));
    CHECK(walk.Phase() == cw::WalkPhase::Idle);
    // Anderson's out/back requests every line never settle into a walk.
    for (int i = 0; i < 13; ++i) {
        double const now = 120.0 + i * 2.0;
        walk.Jump(mark, standing, now, 50.0);
        CHECK(!walk.StartIfSettled(standing, now + 0.9, 2.5, 50.0));
        walk.Jump(standing, standing, now + 1.0, 50.0);
        CHECK(!walk.StartIfSettled(standing, now + 1.9, 2.5, 50.0));
    }
    // Plans belong to individual actors.
    cw::MarkWalk other;
    other.Jump(mark, standing, 150.0, 50.0);
    walk.Jump(mark, standing, 152.0, 50.0);
    CHECK(other.StartIfSettled(standing, 152.5, 2.5, 50.0));
    CHECK(!walk.StartIfSettled(standing, 152.5, 2.5, 50.0));
}

void TestWalkStep() {
    // Review game#2: 0.25 s at 140 units/s is legitimate travel, even past the 30-unit teleport threshold.
    CHECK(Close(cw::BeyondWalk({}, {35, 0, 0}, {300, 0, 0}, 140, .25), 0));
    CHECK(Close(cw::BeyondWalk({}, {10, 0, 0}, {300, 0, 0}, 140, .25), 0)); // collision shortened it
    CHECK(cw::BeyondWalk({}, {65, 0, 0}, {300, 0, 0}, 140, .25) >= 30); // too far
    CHECK(cw::BeyondWalk({}, {0, 35, 0}, {300, 0, 0}, 140, .25) >= 30); // wrong direction
    CHECK(cw::BeyondWalk({}, {-35, 0, 0}, {300, 0, 0}, 140, .25) >= 30);
    CHECK(Close(cw::BeyondWalk({}, {35, 0, 0}, {300, 0, 0}, 140, 0), 35));
    CHECK(Close(cw::BeyondWalk({}, {35, 0, 0}, {10, 0, 0}, 140, .25), 25)); // no allowance past the mark
    // 1.4 m/s at 100 units/m; a diagonal 3D step keeps that speed.
    cw::WalkStep const step = cw::StepTowardMark({}, {300.0, 400.0, 0.0}, 140.0, 0.1);
    CHECK(!step.arrived && Close(step.place.x, 8.4) && Close(step.place.y, 11.2));
    CHECK(Close(std::hypot(step.velocity.x, step.velocity.y, step.velocity.z), 140.0));
    cw::WalkStep const finalStep = cw::StepTowardMark({295.0, 400.0, 0.0}, {300.0, 400.0, 0.0}, 140.0, 0.1);
    CHECK(finalStep.arrived && Close(finalStep.place.x, 300.0) && Close(finalStep.velocity.x, 50.0));
    cw::WalkStep const arrived = cw::StepTowardMark(finalStep.place, finalStep.place, 140.0, 0.1);
    CHECK(arrived.arrived && Close(arrived.velocity.x, 0.0));
    CHECK(Close(cw::StepTowardMark({}, {300.0, 0.0, 0.0}, 140.0, 0.0).place.x, 0.0));
    CHECK(Close(cw::StepTowardMark({}, {300.0, 0.0, 0.0}, 0.0, 0.1).place.x, 0.0));
    CHECK(Close(cw::StepTowardMark({}, {0.0, 0.0, -100.0}, 140.0, 0.1).place.z, -14.0));
}

void TestLatest() {
    cw::Reset();
    CHECK(!cw::Latest().has_value());
    cw::State s;
    s.active = 1;
    s.conversation = 7;
    s.subtitle = 1;
    cw::Publish(s);
    auto const got = cw::Latest();
    CHECK(got.has_value() && got->conversation == 7 && got->size == sizeof(cw::State) && got->sequence > 0 && got->timeMs > 0);
    s.active = 0;  // the mod's state when the conversation leaves the world: nothing shown
    cw::Publish(s);
    CHECK(!cw::Latest().has_value());
    CHECK(!cw::Armed());                               // ... and not armed either
    s.holdPad[0] = 1;                                  // on foot, armed: no scene, but one that begins now is the mod's
    cw::Publish(s);
    CHECK(!cw::Latest().has_value() && cw::Armed());
    s.active = 1;
    cw::Publish(s);
    CHECK(cw::Latest().has_value() && !cw::Armed());   // a scene in the world is not "coming"
    cw::Reset();
    CHECK(!cw::Armed());
    CHECK(!cw::Latest().has_value());
}

} // namespace

int RunConversationWorldTests() {
    gFailures = 0;
    TestParseMode();
    TestSetPieces();
    TestFlowEntries();
    TestStepPlayer();
    TestKeepClear();
    TestSceneRules();
    TestArrangement();
    TestCrops();
    TestSubtitlePoint();
    TestFollowHead();
    TestMarkWalk();
    TestWalkStep();
    TestLatest();
    std::printf("conversation world tests: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
