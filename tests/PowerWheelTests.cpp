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

// The right stick click held opens the game's power wheel, let go closes it; a tap, a chord, a press while turning and
// the second click of the double-click recenter open nothing (gamepad::PowerHoldFromStickClick).
void TestPowerHold() {
    using Phase = pad::PowerHold::Phase;
    pad::PowerHold s;
    pad::RbClickInput in;
    in.enabled = true;
    std::uint64_t now = 10000;
    auto const step = [&](bool click, std::uint64_t after) {
        now += after;
        in.click = click;
        in.nowMs = now;
        return pad::PowerHoldFromStickClick(s, in);
    };
    // A hold: nothing until kPowerHoldMs, then open once; the release closes it.
    CHECK(!step(true, 0).open && s.phase == Phase::Pending);
    CHECK(!step(true, pad::kPowerHoldMs - 1).open);
    pad::PowerHoldStep const opened = step(true, 1);
    CHECK(opened.open && !opened.close && s.phase == Phase::Open);
    CHECK(!step(true, 2000).open);  // held on: no second open
    pad::PowerHoldStep const closed = step(false, 10);
    CHECK(closed.close && !closed.open && s.phase == Phase::Idle);
    // A tap: nothing at all, ever.
    now += 1000;
    pad::PowerHoldStep const tapDown = step(true, 0);
    pad::PowerHoldStep const tapUp = step(false, 80);
    pad::PowerHoldStep const tapLater = step(false, 1000);
    CHECK(!tapDown.open && !tapUp.open && !tapUp.close && !tapLater.open && s.phase == Phase::Idle);
    // A double-click (the recenter), the second click held: neither opens.
    now += 1000;
    step(true, 0);
    step(false, 60);
    CHECK(!step(true, 100).open && s.phase == Phase::Ignored);
    CHECK(!step(true, 1000).open);
    CHECK(!step(false, 10).close && s.phase == Phase::Idle);
    // With the left stick click (the both-sticks chord), before or during the hold: nothing.
    now += 1000;
    in.otherClick = true;
    CHECK(!step(true, 0).open && s.phase == Phase::Ignored);
    CHECK(!step(true, 1000).open);
    in.otherClick = false;
    step(false, 10);
    now += 1000;
    step(true, 0);
    in.otherClick = true;
    CHECK(!step(true, 50).open && s.phase == Phase::Ignored);
    in.otherClick = false;
    CHECK(!step(true, 1000).open);
    step(false, 10);
    // Pressed while turning: nothing; tilting after the press steers the wheel and keeps it.
    now += 1000;
    in.stickX = 0.9f;
    CHECK(!step(true, 0).open && s.phase == Phase::Ignored);
    CHECK(!step(true, 1000).open);
    step(false, 10);
    in.stickX = 0.0f;
    now += 1000;
    step(true, 0);
    in.stickX = 0.9f;
    CHECK(step(true, pad::kPowerHoldMs).open);
    CHECK(step(false, 500).close);
    in.stickX = 0.0f;
    // Not the source (or no clock): nothing opens; a wheel already open still closes at the release.
    now += 1000;
    in.enabled = false;
    CHECK(!step(true, 0).open && !step(true, 1000).open);
    step(false, 10);
    in.enabled = true;
    now += 1000;
    step(true, 0);
    CHECK(step(true, pad::kPowerHoldMs).open);
    in.enabled = false;
    CHECK(step(false, 10).close);
    in.enabled = true;
    pad::PowerHold noClock;
    pad::RbClickInput none;
    none.enabled = true;
    none.click = true;
    CHECK(!pad::PowerHoldFromStickClick(noClock, none).open && !pad::PowerHoldFromStickClick(noClock, none).open);

    // The host: with modPowerHold the click is never RB (a long hold neither), still R3 for Halcyon's recenter.
    mevr::vr::controllers::State c;
    c.active = true;
    c.right.stickClick = true;
    pad::Latches latches;
    pad::Options options;
    options.rightGripHolds = true;
    options.rbSource = pad::RbSource::RightStickClick;
    options.modPowerHold = true;
    WORD seen = 0;
    for (std::uint64_t t = 1000; t <= 2000; t += 50) {
        c.timeMs = t;
        seen = static_cast<WORD>(seen | pad::FromControllers(c, latches, options).wButtons);
    }
    CHECK((seen & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0 && (seen & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
    // Without it the same hold is RB, as before.
    pad::Latches before;
    options.modPowerHold = false;
    c.right.stickClick = false;
    c.timeMs = 3000;
    pad::FromControllers(c, before, options);
    c.right.stickClick = true;
    WORD rb = 0;
    for (std::uint64_t t = 3050; t <= 4000; t += 50) {
        c.timeMs = t;
        rb = static_cast<WORD>(rb | pad::FromControllers(c, before, options).wButtons);
    }
    CHECK((rb & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
    // The flag across the boundary.
    pad::Options flagged;
    flagged.modPowerHold = true;
    CHECK(pad::FromFlags(-1.0f, 0.1f, 0.2f, pad::ToFlags(flagged)).modPowerHold);
    CHECK(!pad::FromFlags(-1.0f, 0.1f, 0.2f, pad::ToFlags(pad::Options{})).modPowerHold);
}

}  // namespace

// The lock (research/55 section 4): taken within acquireDeg, the nearest angle winning, kept to keepDeg, held through a
// short block of the line of sight, let go after graceS of it.
// A hostile as the lock sees it: its id, the angle from the hand's ray, the distance, in sight or not, the angle from the gaze.
struct C : pw::LockCandidate {
    C(std::uint64_t i, float hand, float metres, bool inSight, float gaze = 180.0f) {
        id = i;
        angleDeg = hand;
        distanceM = metres;
        visible = inSight;
        gazeDeg = gaze;
    }
};

void TestLock() {
    pw::LockSettings s;  // acquire 9, keep 16, range 35 m, grace 0.35 s
    s.autoLock = false;  // pointing only (TestAutoLock has the resolver)
    pw::LockState state;
    // Nothing near the ray: none.
    CHECK(pw::UpdateLock(state, {C{1, 12.0f, 10.0f, true}}, 0.0, s) == 0);
    // Within acquireDeg: the smaller angle wins; out of sight or out of range: never taken.
    CHECK(pw::UpdateLock(state, {C{1, 8.0f, 10.0f, true}, C{2, 3.0f, 20.0f, true}, C{3, 1.0f, 5.0f, false}, C{4, 0.5f, 40.0f, true}}, 0.1,
              s) == 2);
    CHECK(state.target == 2 && state.since == 0.1);
    // Kept while it drifts past acquireDeg (to 14) even with another nearer the ray, and its time kept.
    CHECK(pw::UpdateLock(state, {C{1, 2.0f, 10.0f, true}, C{2, 14.0f, 20.0f, true}}, 0.2, s) == 2 && state.since == 0.1);
    // Past keepDeg: the best other.
    CHECK(pw::UpdateLock(state, {C{1, 2.0f, 10.0f, true}, C{2, 17.0f, 20.0f, true}}, 0.3, s) == 1 && state.since == 0.3);
    // A near tie: the nearer one (0.05 degree per metre).
    pw::LockState tie;
    CHECK(pw::UpdateLock(tie, {C{7, 4.0f, 30.0f, true}, C{8, 4.5f, 5.0f, true}}, 0.0, s) == 8);
    // Blocked for less than the grace: kept; longer: let go (and nothing else to take).
    pw::LockState blocked;
    CHECK(pw::UpdateLock(blocked, {C{5, 2.0f, 10.0f, true}}, 1.0, s) == 5);
    CHECK(pw::UpdateLock(blocked, {C{5, 2.0f, 10.0f, false}}, 1.1, s) == 5);
    CHECK(pw::UpdateLock(blocked, {C{5, 2.0f, 10.0f, false}}, 1.4, s) == 5);
    CHECK(pw::UpdateLock(blocked, {C{5, 2.0f, 10.0f, false}}, 1.5, s) == 0);
    // Seen again: taken again, and a fresh block starts its own grace.
    CHECK(pw::UpdateLock(blocked, {C{5, 2.0f, 10.0f, true}}, 1.6, s) == 5);
    CHECK(pw::UpdateLock(blocked, {C{5, 2.0f, 10.0f, false}}, 1.7, s) == 5 && blocked.blockedSince == 1.7);
    CHECK(pw::UpdateLock(blocked, {C{5, 2.0f, 10.0f, true}}, 1.8, s) == 5 && blocked.blockedSince < 0.0);
    // Gone from the list (dead, out of the scan): let go.
    CHECK(pw::UpdateLock(blocked, {}, 1.9, s) == 0 && blocked.target == 0);
}

// The automatic resolver: nothing pointed at, the hostile the hand (within 30 degrees) and the gaze (within 20) agree on best;
// pointing at another takes over; an automatic lock that's then pointed at stays, now pointed.
void TestAutoLock() {
    pw::LockSettings const s;
    // Nothing within 9 degrees of the hand: the one the gaze is on (hand 25, gaze 2) beats one nearer the hand (15) but off the
    // gaze (18); one outside both cones or out of sight is never taken.
    pw::LockState state;
    CHECK(pw::UpdateLock(state, {C{1, 15.0f, 10.0f, true, 18.0f}, C{2, 25.0f, 10.0f, true, 2.0f}, C{3, 40.0f, 5.0f, true, 30.0f}}, 0.0, s) == 2);
    CHECK(state.automatic && state.since == 0.0);
    pw::LockState none;
    CHECK(pw::UpdateLock(none, {C{3, 40.0f, 5.0f, true, 30.0f}, C{4, 1.0f, 5.0f, false, 1.0f}}, 0.0, s) == 0);
    // Kept while it drifts within the cones plus 6 degrees (hand 33, gaze 24: both past the cones, within the margin).
    CHECK(pw::UpdateLock(state, {C{2, 33.0f, 10.0f, true, 24.0f}}, 0.1, s) == 2 && state.since == 0.0);
    // Another pointed at (within 9 degrees of the hand) takes over at once.
    CHECK(pw::UpdateLock(state, {C{2, 20.0f, 10.0f, true, 2.0f}, C{5, 5.0f, 12.0f, true, 30.0f}}, 0.2, s) == 5 && !state.automatic);
    // The pointed one drifts past keepDeg (16) but stays within the automatic cones: the same target, now automatic, its
    // time kept.
    CHECK(pw::UpdateLock(state, {C{5, 20.0f, 12.0f, true, 30.0f}}, 0.3, s) == 5 && state.automatic && state.since == 0.2);
    // An automatic target pointed at becomes a pointed lock, the same target.
    CHECK(pw::UpdateLock(state, {C{5, 4.0f, 12.0f, true, 30.0f}}, 0.4, s) == 5 && !state.automatic && state.since == 0.2);
    // Out of both cones and the margin: let go.
    pw::LockState drift;
    CHECK(pw::UpdateLock(drift, {C{6, 25.0f, 10.0f, true, 25.0f}}, 0.0, s) == 6 && drift.automatic);
    CHECK(pw::UpdateLock(drift, {C{6, 37.0f, 10.0f, true, 27.0f}}, 0.1, s) == 0);
}

// The push: the hand along its own ray, fast and far enough over the last 0.12 s; once, until it slows; the cooldown.
void TestPush() {
    pw::LockSettings const s;  // 1.6 m/s over 0.10 m, cooldown 0.6 s
    XrVector3f const forward{0.0f, 0.0f, -1.0f};
    double const dt = 1.0 / 90.0;
    // A slow reach (0.5 m/s): never.
    {
        pw::Push p;
        bool fired = false;
        for (int i = 0; i < 90; ++i) {
            fired = pw::StepPush(p, i * dt, {0.0f, 0.0f, -0.5f * static_cast<float>(i * dt)}, forward, s) || fired;
        }
        CHECK(!fired);
    }
    // A push at 2.5 m/s: fires once (about 0.04 s in, at 0.10 m of travel), not again while it keeps going.
    {
        pw::Push p;
        int fires = 0;
        int firstAt = -1;
        for (int i = 0; i < 20; ++i) {
            if (pw::StepPush(p, i * dt, {0.0f, 0.0f, -2.5f * static_cast<float>(i * dt)}, forward, s)) {
                ++fires;
                firstAt = firstAt < 0 ? i : firstAt;
            }
        }
        CHECK(fires == 1);
        CHECK(firstAt >= 3 && firstAt <= 6);
        // The hand stops (re-arms), then pushes again before the cooldown: nothing; after it: fires.
        float z = -2.5f * static_cast<float>(19 * dt);
        int i = 20;
        for (; i < 40; ++i) {
            CHECK(!pw::StepPush(p, i * dt, {0.0f, 0.0f, z}, forward, s));
        }
        CHECK(p.armed);
        double const start = i * dt;
        CHECK(start - p.lastFire < s.cooldownS);
        bool early = false;
        for (int k = 0; k < 12; ++k, ++i) {
            z -= 2.5f * static_cast<float>(dt);
            early = early || (i * dt - p.lastFire < s.cooldownS && pw::StepPush(p, i * dt, {0.0f, 0.0f, z}, forward, s));
        }
        CHECK(!early);
        // Wait out the cooldown, still, then push.
        for (; i * dt - p.lastFire < s.cooldownS + 0.1; ++i) {
            pw::StepPush(p, i * dt, {0.0f, 0.0f, z}, forward, s);
        }
        bool again = false;
        for (int k = 0; k < 20; ++k, ++i) {
            z -= 2.5f * static_cast<float>(dt);
            again = pw::StepPush(p, i * dt, {0.0f, 0.0f, z}, forward, s) || again;
        }
        CHECK(again);
    }
    // Fast but sideways, or pulled back toward the body: never.
    {
        pw::Push side;
        pw::Push back;
        bool fired = false;
        for (int i = 0; i < 30; ++i) {
            float const d = 2.5f * static_cast<float>(i * dt);
            fired = pw::StepPush(side, i * dt, {d, 0.0f, 0.0f}, forward, s) || fired;
            fired = pw::StepPush(back, i * dt, {0.0f, 0.0f, d}, forward, s) || fired;
        }
        CHECK(!fired);
    }
    // The ray tilted down 45 degrees: a push along it counts.
    {
        pw::Push p;
        XrVector3f const down{0.0f, -0.70710678f, -0.70710678f};
        bool fired = false;
        for (int i = 0; i < 20; ++i) {
            float const d = 2.5f * static_cast<float>(i * dt);
            fired = pw::StepPush(p, i * dt, {0.0f, -d * 0.70710678f, -d * 0.70710678f}, down, s) || fired;
        }
        CHECK(fired);
    }
}

// The lock view's latest copy, as the host reads it: what was sent, inactive once an inactive one is.
void TestLockView() {
    pw::LockView v;
    v.active = 1;
    v.icon = 4;
    v.target = {1.0f, 1.5f, -8.0f};
    pw::PublishLock(v);
    pw::LockView const got = pw::LatestLock();
    CHECK(got.active == 1 && got.icon == 4 && Close(got.target.z, -8.0f) && got.size == sizeof(pw::LockView));
    pw::PublishLock(pw::LockView{});
    CHECK(pw::LatestLock().active == 0);
}

// The cast leaves the hand (research/55 section 8): from the hand toward the projectile's own target at its own speed.
void TestFromHand() {
    // The game's velocity: 1300 units/s toward +X; the target 1000 units ahead, the hand 50 up and 30 left of the spawn.
    pw::Launch const l = pw::FromHand({0.0f, -30.0f, 50.0f}, {1000.0f, 0.0f, 0.0f}, {1300.0f, 0.0f, 0.0f}, 0.0f);
    CHECK(l.aimed && Close(l.start.y, -30.0f) && Close(l.start.z, 50.0f));
    float const speed = std::sqrt(l.velocity.x * l.velocity.x + l.velocity.y * l.velocity.y + l.velocity.z * l.velocity.z);
    CHECK(Close(speed, 1300.0f, 0.1f));  // its own speed
    CHECK(l.velocity.x > 1290.0f && l.velocity.y > 0.0f && l.velocity.z < 0.0f);  // toward the target: right and down a little
    // No velocity yet: the class's speed.
    pw::Launch const still = pw::FromHand({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 100.0f}, {}, 1300.0f);
    CHECK(still.aimed && Close(still.velocity.z, 1300.0f, 0.1f));
    // The target on the hand, or no speed at all: the game's velocity, unchanged.
    pw::Launch const onHand = pw::FromHand({5.0f, 5.0f, 5.0f}, {5.0f, 5.0f, 5.0f}, {10.0f, 0.0f, 0.0f}, 0.0f);
    CHECK(!onHand.aimed && Close(onHand.velocity.x, 10.0f));
    CHECK(!pw::FromHand({0.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}, {}, 0.0f).aimed);
    // The launch point: ahead of the aim pose along its forward (-Z when unrotated).
    XrPosef aim{};
    aim.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    aim.position = {0.1f, 1.2f, -0.3f};
    XrVector3f const p = pw::LaunchPoint(aim, 0.06f);
    CHECK(Close(p.x, 0.1f) && Close(p.y, 1.2f) && Close(p.z, -0.36f));
    // Turned 90 degrees left (about +Y): forward is -X.
    float const h = std::sqrt(0.5f);
    aim.orientation = {0.0f, h, 0.0f, h};
    XrVector3f const q = pw::LaunchPoint(aim, 0.10f);
    CHECK(Close(q.x, 0.0f) && Close(q.z, -0.3f));
}

int RunPowerWheelTests() {
    TestMachine();
    TestPlacement();
    TestTableAndView();
    TestPad();
    TestPowerHold();
    TestLock();
    TestAutoLock();
    TestPush();
    TestLockView();
    TestFromHand();
    return gFailures;
}
