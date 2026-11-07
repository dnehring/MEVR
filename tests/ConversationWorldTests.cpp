// SPDX-License-Identifier: MIT
// Offline tests for conversations in the world (research/59, core/vr/ConversationWorld): the setting's words, the conversation
// movie's subtitle and wheel bands on the HUD frame, the subtitle's place under a face, the place following the head bone's jumps, and
// the host's latest state.

#include "core/vr/ConversationWorld.hpp"

#include <cmath>
#include <cstdio>
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

void TestSetPieces() {
    // The scan's framed conversations go to the 3D screen in the world mode, by the game's full name or its short forms.
    CHECK(cw::IsSetPiece("BioConversation nor10_comm_room_d.nor10_comm_room_dlg"));
    CHECK(cw::IsSetPiece("end90_saren_D.end90_saren_dlg"));
    CHECK(cw::IsSetPiece("JUG70_Sovereign_dlg"));
    CHECK(cw::IsSetPiece("sta90_council_saren"));
    CHECK(cw::IsSetPiece("nor10_comm_room_d"));
    CHECK(!cw::IsSetPiece("BioConversation nor20_medbay_d.nor20_medbay_dlg"));  // the medical bay stays in the world
    CHECK(!cw::IsSetPiece("end90_saren2_dlg"));                                 // a walk, not a set piece
    CHECK(!cw::IsSetPiece("nor10_captain_extra"));
    CHECK(!cw::IsSetPiece(""));
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
    cw::Reset();
    CHECK(!cw::Latest().has_value());
}

} // namespace

int RunConversationWorldTests() {
    gFailures = 0;
    TestParseMode();
    TestSetPieces();
    TestCrops();
    TestSubtitlePoint();
    TestFollowHead();
    TestMarkWalk();
    TestWalkStep();
    TestLatest();
    std::printf("conversation world tests: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
