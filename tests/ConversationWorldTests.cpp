// SPDX-License-Identifier: MIT
// Offline tests for conversations in the world (research/59, core/vr/ConversationWorld): the setting's words, the conversation
// movie's subtitle and wheel bands on the HUD frame, the subtitle's place under a face, the held view moved with the pawn, and
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

void TestCrops() {
    // The HUD frame of the PS VR2's eye: 2344 x 2392, the 1280 x 720 stage across its width, its bottom on the frame's.
    std::uint32_t const w = 2344;
    std::uint32_t const h = 2392;
    double const scale = w / 1280.0;
    auto const sub = cw::DefaultSubtitleCrop(w, h);
    CHECK(sub.Valid());
    // The band's bottom: stage y 640, 80 stage px above the stage's bottom edge, which is the frame's bottom.
    CHECK(Close((sub.y + sub.height) * h, h - 80.0 * scale, 0.5));
    CHECK(Close(sub.height * h, 80.0 * scale, 0.5));
    CHECK(Close(sub.x, 140.0 / 1280.0) && Close(sub.width, 1000.0 / 1280.0));
    auto const wheel = cw::DefaultWheelCrop(w, h);
    CHECK(wheel.Valid());
    CHECK(Close(wheel.y + wheel.height, 1.0, 1e-4));  // down to the stage's bottom
    CHECK(wheel.x > sub.x && wheel.x + wheel.width < sub.x + sub.width);
    // The wheel's centre on the movie's authored x (639.7 of 1280), within a stage pixel.
    CHECK(Close((wheel.x + wheel.width * 0.5) * 1280.0, 640.0, 1.0));
    // A frame exactly 16:9 puts the stage on the whole frame: the bands at their stage places.
    auto const sub169 = cw::DefaultSubtitleCrop(1280, 720);
    CHECK(Close(sub169.y * 720.0, 560.0, 0.01) && Close(sub169.height * 720.0, 80.0, 0.01));
    // No frame: nothing.
    CHECK(!cw::DefaultSubtitleCrop(0, 0).Valid());
    // A frame shorter than the stage (wider than 16:9): the band clipped to the frame, never negative.
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
    XrVector3f const near{0.2f, 1.6f, 0.0f};
    CHECK(cw::SubtitlePoint(near, eyes, 0.2f, 0.5f, out));
    CHECK(Close(out.x, 0.1) && Close(out.y, 1.4));
    // Straight above or below the eyes (no level line to them): no place.
    CHECK(!cw::SubtitlePoint({0.0f, 0.5f, 0.0f}, eyes, 0.2f, 0.1f, out));
    // A negative drop is none.
    CHECK(cw::SubtitlePoint(face, eyes, -1.0f, 0.0f, out) && Close(out.y, 1.6));
}

void TestHeldLocation() {
    cw::Vec3 const view{100.0, 200.0, 160.0};
    cw::Vec3 const start{90.0, 195.0, 92.0};
    cw::Vec3 const now{140.0, 175.0, 92.0};  // the stage moved Shepard 50 units along X, -20 along Y
    cw::Vec3 const held = cw::HeldLocation(view, start, now, true);
    CHECK(Close(held.x, 150.0) && Close(held.y, 180.0) && Close(held.z, 160.0));
    cw::Vec3 const still = cw::HeldLocation(view, start, now, false);
    CHECK(Close(still.x, 100.0) && Close(still.y, 200.0) && Close(still.z, 160.0));
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
    TestCrops();
    TestSubtitlePoint();
    TestHeldLocation();
    TestLatest();
    std::printf("conversation world tests: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
