// SPDX-License-Identifier: MIT
// Offline tests for the bypass puzzle's d-pad (core/vr/OmniPad, research/53 section 6.3): the arms' events, the arm under a touch,
// and where the pad stands beside the screen.

#include "core/vr/GuiEvents.hpp"
#include "core/vr/OmniPad.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/OmniPadTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace op = mevr::vr::omni_pad;
namespace ge = mevr::vr::gui_events;

void TestEvents() {
    // The game's own d-pad events: up takes the marker a ring in (the puzzle's DPadUp and Key.UP), down out, left/right turn.
    CHECK(op::PressEvent(op::Arm::Up) == ge::kControlUp && op::ReleaseEvent(op::Arm::Up) == ge::kControlUpRelease);
    CHECK(op::PressEvent(op::Arm::Down) == ge::kControlDown && op::ReleaseEvent(op::Arm::Down) == ge::kControlDownRelease);
    CHECK(op::PressEvent(op::Arm::Left) == ge::kControlLeft && op::ReleaseEvent(op::Arm::Left) == ge::kControlLeftRelease);
    CHECK(op::PressEvent(op::Arm::Right) == ge::kControlRight && op::ReleaseEvent(op::Arm::Right) == ge::kControlRightRelease);
    CHECK(op::PressEvent(op::Arm::None) == 0 && op::ReleaseEvent(op::Arm::None) == 0);
    CHECK(std::string_view(op::Name(op::Arm::Left)) == "left" && std::string_view(op::Name(op::Arm::None)).empty());
    // Pressed, slid across, let go.
    CHECK((op::Transition(op::Arm::None, op::Arm::Left) == std::vector<std::uint8_t>{ge::kControlLeft}));
    CHECK((op::Transition(op::Arm::Left, op::Arm::Up) == std::vector<std::uint8_t>{ge::kControlLeftRelease, ge::kControlUp}));
    CHECK((op::Transition(op::Arm::Up, op::Arm::None) == std::vector<std::uint8_t>{ge::kControlUpRelease}));
    CHECK(op::Transition(op::Arm::Right, op::Arm::Right).empty());
    for (std::uint8_t e : op::Transition(op::Arm::Down, op::Arm::Right)) {
        CHECK(ge::IsControl(e));
    }
}

void TestArmAt() {
    float const w = 480.0f;
    CHECK(op::ArmAt(240, 40, w, w) == op::Arm::Up);
    CHECK(op::ArmAt(240, 440, w, w) == op::Arm::Down);
    CHECK(op::ArmAt(30, 240, w, w) == op::Arm::Left);
    CHECK(op::ArmAt(450, 240, w, w) == op::Arm::Right);
    CHECK(op::ArmAt(240, 240, w, w) == op::Arm::None);  // the hub
    CHECK(op::ArmAt(240 + 50, 240, w, w) == op::Arm::None);  // still inside the hub (0.13 x 480 = 62 px)
    CHECK(op::ArmAt(240 + 70, 240, w, w) == op::Arm::Right);
    // A corner goes to the nearer arm.
    CHECK(op::ArmAt(60, 20, w, w) == op::Arm::Up);
    CHECK(op::ArmAt(20, 60, w, w) == op::Arm::Left);
    CHECK(op::ArmAt(470, 460, w, w) == op::Arm::Right);
    // Off the canvas, or a canvas of no size.
    CHECK(op::ArmAt(-1, 240, w, w) == op::Arm::None && op::ArmAt(240, 481, w, w) == op::Arm::None);
    CHECK(op::ArmAt(10, 10, 0, 0) == op::Arm::None);
}

void TestBeside() {
    mevr::vr::world_ui::Panel screen;
    screen.pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    screen.pose.position = {0.0f, 1.0f, -0.5f};
    screen.widthM = 0.30f;
    screen.heightM = 0.22f;
    op::Pad const pad = op::Beside(screen);
    // To its right in its plane, level with its centre: half the screen + the gap + half the pad.
    CHECK(std::fabs(pad.pose.position.x - (0.15f + op::kGapM + 0.5f * op::kSizeM)) < 1e-5f);
    CHECK(std::fabs(pad.pose.position.y - 1.0f) < 1e-6f && std::fabs(pad.pose.position.z + 0.5f) < 1e-6f);
    CHECK(pad.sizeM == op::kSizeM && pad.pose.orientation.w == 1.0f);
    // Turned 90 degrees about up (facing +X): "right" of the screen is -Z.
    float const s = std::sqrt(0.5f);
    screen.pose.orientation = {0.0f, s, 0.0f, s};
    op::Pad const turned = op::Beside(screen, 0.2f, 0.0f);
    CHECK(std::fabs(turned.pose.position.z - (-0.5f - 0.25f)) < 1e-5f && std::fabs(turned.pose.position.x) < 1e-5f);
}

} // namespace

int RunOmniPadTests() {
    gFailures = 0;
    TestEvents();
    TestArmAt();
    TestBeside();
    return gFailures;
}
