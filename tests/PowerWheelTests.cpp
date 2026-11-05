// SPDX-License-Identifier: MIT
// Offline tests for the power wheel in the game (research/55): the left hand's controls through an opening
// (core/vr/PowerWheel's Machine), the wheel's placement and the hand's offset in its plane, the shared power table, the
// host's latest view, and the pad mapping's power wheel option (game/GamepadMapping). The layout and pick are in
// tests/Main.cpp (TestPowerWheel).

#include "core/vr/Controllers.hpp"
#include "core/vr/PowerWheel.hpp"
#include "game/GamepadMapping.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/PowerWheelTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace pw = mevr::vr::power_wheel;
namespace pad = mevr::game::gamepad;

bool Close(float a, float b, float tolerance = 1e-3f) {
    return std::abs(a - b) <= tolerance;
}

// One step of the machine at `time` with X `button`, the hand at (x, y) metres, the trigger and grip as given.
pw::Event Step(pw::Machine& m, std::vector<float> const& angles, double time, bool button, float x = 0.0f, float y = 0.0f,
    float trigger = 0.0f, float grip = 0.0f, bool allowed = true, float stickX = 0.0f, float stickY = 0.0f) {
    pw::MachineInput in;
    in.time = time;
    in.allowed = allowed;
    in.button = button;
    in.handValid = true;
    in.hand = {x, y};
    in.trigger = trigger;
    in.grip = grip;
    in.stickX = stickX;
    in.stickY = stickY;
    return pw::Step(m, in, angles, pw::Settings{});
}

void TestMachine() {
    std::vector<float> const six = pw::Angles(6);  // 2 is near the top (82.8), 3 to the upper right (32.2)

    // Held: X opens it, the hand moves up (tile 2), letting go equips it.
    {
        pw::Machine m;
        CHECK(Step(m, six, 0.0, false) == pw::Event::None);
        CHECK(Step(m, six, 0.1, true) == pw::Event::Opened && m.open && m.serial == 1);
        CHECK(Step(m, six, 0.2, true, 0.0f, 0.01f) == pw::Event::None && m.selected == -1);  // inside the dead zone
        CHECK(Step(m, six, 0.3, true, 0.0f, 0.06f) == pw::Event::None && m.selected == 2);
        CHECK(Step(m, six, 0.6, false, 0.0f, 0.06f) == pw::Event::Equipped && !m.open && m.equipped == 2);
    }
    // Held and let go with nothing picked: closed unequipped.
    {
        pw::Machine m;
        Step(m, six, 0.0, true);
        CHECK(Step(m, six, 0.6, false) == pw::Event::Cancelled && !m.open);
    }
    // Tapped: it stays open; a pinch of the left trigger equips the pick.
    {
        pw::Machine m;
        Step(m, six, 0.0, true);
        CHECK(Step(m, six, 0.1, false) == pw::Event::None && m.open && m.toggled);
        Step(m, six, 0.3, false, 0.06f, 0.04f);  // to the upper right: tile 3
        CHECK(m.selected == 3);
        CHECK(Step(m, six, 0.4, false, 0.06f, 0.04f, 0.9f) == pw::Event::Equipped && m.equipped == 3);
    }
    // Tapped, then X again equips the pick; with nothing picked it just closes.
    {
        pw::Machine m;
        Step(m, six, 0.0, true);
        Step(m, six, 0.1, false);
        Step(m, six, 0.3, false, -0.06f, 0.0f);  // left: tile 0
        CHECK(Step(m, six, 0.5, true, -0.06f, 0.0f) == pw::Event::Equipped && m.equipped == 0);
        pw::Machine n;
        Step(n, six, 0.0, true);
        Step(n, six, 0.1, false);
        CHECK(Step(n, six, 0.5, true) == pw::Event::Cancelled && !n.open);
    }
    // A trigger already held when it opens confirms nothing until let go and pulled again; a grip squeeze also equips.
    {
        pw::Machine m;
        Step(m, six, 0.0, true, 0.0f, 0.0f, 0.9f);
        Step(m, six, 0.1, false, 0.0f, 0.06f, 0.9f);  // a tap; the hand up
        CHECK(m.open && m.selected == 2);
        CHECK(Step(m, six, 0.2, false, 0.0f, 0.06f, 0.9f) == pw::Event::None && m.open);
        CHECK(Step(m, six, 0.3, false, 0.0f, 0.06f, 0.0f) == pw::Event::None && m.open);
        CHECK(Step(m, six, 0.4, false, 0.0f, 0.06f, 0.0f, 0.9f) == pw::Event::Equipped && m.equipped == 2);
    }
    // The stick picks by its direction; let go, the hand (still where it was) doesn't take over until it moves on.
    {
        pw::Machine m;
        Step(m, six, 0.0, true, 0.05f, 0.0f);
        Step(m, six, 0.1, true, 0.05f, 0.0f, 0.0f, 0.0f, true, -1.0f, 0.0f);  // the stick left: tile 0
        CHECK(m.selected == 0);
        Step(m, six, 0.2, true, 0.05f, 0.0f);  // stick centred, the hand still to the right
        CHECK(m.selected == 0);
        Step(m, six, 0.3, true, 0.0f, 0.07f);  // the hand moves on, up
        CHECK(m.selected == 2);
    }
    // Leaving the allowed state closes it unequipped; nothing opens while not allowed or with no powers.
    {
        pw::Machine m;
        Step(m, six, 0.0, true);
        CHECK(Step(m, six, 0.1, true, 0.0f, 0.06f, 0.0f, 0.0f, false) == pw::Event::Cancelled && !m.open);
        pw::Machine n;
        CHECK(Step(n, six, 0.0, true, 0.0f, 0.0f, 0.0f, 0.0f, false) == pw::Event::None && !n.open);
        CHECK(Step(n, {}, 0.1, false) == pw::Event::None);
        CHECK(Step(n, {}, 0.2, true) == pw::Event::None && !n.open);
    }
}

void TestPlacement() {
    // The palm half a metre ahead of the eye and a little low: the wheel faces the eye, upright.
    XrVector3f const eye{0.0f, 1.6f, 0.0f};
    XrVector3f const palm{0.0f, 1.3f, -0.5f};
    XrPosef const wheel = pw::Place(palm, eye, 0.04f);
    float const d = std::hypot(wheel.position.y - palm.y, wheel.position.z - palm.z);
    CHECK(Close(d, 0.04f));
    CHECK(wheel.position.z > palm.z && wheel.position.y > palm.y);  // moved toward the eye
    pw::Vec2 const centre = pw::InPlane(wheel, wheel.position);
    CHECK(Close(centre.x, 0.0f) && Close(centre.y, 0.0f));
    // A point to the right of the wheel as the eye sees it (+x), and one above (+y).
    pw::Vec2 const right = pw::InPlane(wheel, {wheel.position.x + 0.1f, wheel.position.y, wheel.position.z});
    CHECK(Close(right.x, 0.1f) && Close(right.y, 0.0f));
    pw::Vec2 const up = pw::InPlane(wheel, {wheel.position.x, wheel.position.y + 0.1f, wheel.position.z});
    CHECK(Close(up.y, 0.1f * 0.5f / std::hypot(0.3f, 0.5f), 2e-3f) && Close(up.x, 0.0f));  // the wheel leans back 31 degrees to face the eye
    // Its +Z points at the eye.
    float const qx = wheel.orientation.x, qy = wheel.orientation.y, qz = wheel.orientation.z, qw = wheel.orientation.w;
    XrVector3f const z{2.0f * (qx * qz + qw * qy), 2.0f * (qy * qz - qw * qx), 1.0f - 2.0f * (qx * qx + qy * qy)};
    float const toEyeY = eye.y - wheel.position.y;
    float const toEyeZ = eye.z - wheel.position.z;
    float const length = std::hypot(toEyeY, toEyeZ);
    CHECK(Close(z.x, 0.0f) && Close(z.y, toEyeY / length) && Close(z.z, toEyeZ / length));
}

void TestTableAndView() {
    pw::PowerInfo const* const thr = pw::FindPower("TK_Throw");
    CHECK(thr != nullptr && thr->icon == 1 && thr->discipline == 0);
    pw::PowerInfo const* const overload = pw::FindPower("EL_EMP_Delivery");
    CHECK(overload != nullptr && overload->discipline == 1 && overload->icon == 7);
    pw::PowerInfo const* const assassination = pw::FindPower("SW_Critical");
    CHECK(assassination != nullptr && assassination->icon == 19 && assassination->discipline == 2);
    CHECK(pw::FindPower("Rifle_Butt") == nullptr && pw::FindPower("MD_Heal") == nullptr && pw::FindPower(nullptr) == nullptr);

    pw::View view;
    view.open = 1;
    view.count = 2;
    view.selected = 1;
    pw::Publish(view);
    pw::View const latest = pw::Latest();
    CHECK(latest.open == 1 && latest.count == 2 && latest.selected == 1 && latest.size == sizeof(pw::View));
    view.open = 0;
    pw::Publish(view);
    CHECK(pw::Latest().open == 0);
}

void TestPad() {
    mevr::vr::controllers::State in;
    in.active = true;
    in.left.stickY = 1.0f;
    in.left.primary = true;
    in.left.trigger = 1.0f;
    in.left.squeeze = 1.0f;
    in.left.stickClick = true;
    in.right.stickX = 1.0f;
    in.right.primary = true;
    pad::Latches latches;
    pad::Options options;
    options.powerWheel = true;
    XINPUT_GAMEPAD const open = pad::FromControllers(in, latches, options);
    CHECK(open.sThumbLY == 0 && open.bLeftTrigger == 0);
    CHECK((open.wButtons & (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_LEFT_THUMB)) == 0);
    CHECK(open.sThumbRX != 0 && (open.wButtons & XINPUT_GAMEPAD_A) != 0);  // the right hand stays the game's
    // Closed with everything still held: kept from the game until each is let go.
    options.powerWheel = false;
    XINPUT_GAMEPAD const held = pad::FromControllers(in, latches, options);
    CHECK(held.sThumbLY == 0 && held.bLeftTrigger == 0 && (held.wButtons & XINPUT_GAMEPAD_X) == 0);
    in.left = {};
    pad::FromControllers(in, latches, options);
    in.left.stickY = 1.0f;
    in.left.primary = true;
    XINPUT_GAMEPAD const free = pad::FromControllers(in, latches, options);
    CHECK(free.sThumbLY != 0 && (free.wButtons & XINPUT_GAMEPAD_X) != 0);
    // The flag across the boundary.
    pad::Options flagged;
    flagged.powerWheel = true;
    CHECK(pad::FromFlags(-1.0f, 0.1f, 0.2f, pad::ToFlags(flagged)).powerWheel);
    CHECK(!pad::FromFlags(-1.0f, 0.1f, 0.2f, pad::ToFlags(pad::Options{})).powerWheel);
}

}  // namespace

int RunPowerWheelTests() {
    TestMachine();
    TestPlacement();
    TestTableAndView();
    TestPad();
    return gFailures;
}
