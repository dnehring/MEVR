// SPDX-License-Identifier: MIT
// Offline tests for the game pointer's input as the game's GUI events (core/vr/GuiEvents, research/57 section 6): the host's
// queue (moves coalesced, buttons never dropped) and the mod's per-tick schedule (a move before each button or notch, one of
// those per tick).

#include "core/vr/GuiEvents.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/GuiEventsTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace ge = mevr::vr::gui_events;

std::vector<std::uint8_t> TakeAll(ge::Queue& q) {
    std::uint8_t buffer[ge::Queue::kCapacity] = {};
    std::uint32_t const n = q.Take(buffer, static_cast<std::uint32_t>(ge::Queue::kCapacity));
    return {buffer, buffer + n};
}

void TestQueue() {
    ge::Queue q;
    q.Move();
    q.Move();
    q.Move();  // a hover: one move
    q.Button(true);
    q.Move();
    q.Button(false);
    q.Wheel(-2);
    std::vector<std::uint8_t> const all = TakeAll(q);
    std::vector<std::uint8_t> const want = {ge::kMouseMove, ge::kLeftDown, ge::kMouseMove, ge::kLeftUp, ge::kWheelDown, ge::kWheelDown};
    CHECK(all == want);
    CHECK(q.Size() == 0);
    // Take in pieces.
    q.Wheel(3);
    std::uint8_t two[2] = {};
    CHECK(q.Take(two, 2) == 2 && two[0] == ge::kWheelUp && q.Size() == 1);
    q.Clear();
    // Full: the oldest notches go, the buttons stay.
    q.Button(true);
    q.Wheel(-200);
    q.Button(false);
    std::vector<std::uint8_t> const full = TakeAll(q);
    CHECK(full.size() == ge::Queue::kCapacity);
    CHECK(full.front() == ge::kLeftDown && full.back() == ge::kLeftUp);
}

void TestScheduler() {
    ge::Scheduler s;
    std::uint8_t const tap[] = {ge::kMouseMove, ge::kMouseMove, ge::kLeftDown, ge::kLeftUp};
    s.Add(tap, 4);
    // Tick 1: the move, then the button down.
    std::vector<std::uint8_t> t = s.Tick();
    CHECK((t == std::vector<std::uint8_t>{ge::kMouseMove, ge::kLeftDown}));
    // Tick 2: the up, with a move before it (the place it's for).
    t = s.Tick();
    CHECK((t == std::vector<std::uint8_t>{ge::kMouseMove, ge::kLeftUp}));
    CHECK(s.Tick().empty() && s.Waiting() == 0);
    // A hover only: one move a tick.
    std::uint8_t const hover[] = {ge::kMouseMove};
    s.Add(hover, 1);
    CHECK((s.Tick() == std::vector<std::uint8_t>{ge::kMouseMove}));
    // Three notches: one a tick.
    std::uint8_t const wheel[] = {ge::kWheelDown, ge::kWheelDown, ge::kWheelDown};
    s.Add(wheel, 3);
    int notches = 0;
    for (int i = 0; i < 5; ++i) {
        for (std::uint8_t e : s.Tick()) {
            notches += e == ge::kWheelDown ? 1 : 0;
        }
    }
    CHECK(notches == 3);
}

// The bypass puzzle's d-pad (research/53 section 6.3): pad directions are never dropped and go alone, one a tick, with no mouse
// motion before them (a motion puts the puzzle in its mouse mode).
void TestControls() {
    CHECK(ge::IsControl(ge::kControlUp) && ge::IsControl(ge::kControlDownRelease) && ge::IsControl(ge::kControlRight));
    CHECK(!ge::IsControl(ge::kMouseMove) && !ge::IsControl(ge::kLeftDown) && !ge::IsControl(12) && !ge::IsControl(29));
    ge::Queue q;
    q.Control(ge::kControlLeft);
    q.Control(12);  // not a pad direction: ignored
    q.Control(ge::kControlLeftRelease);
    CHECK((TakeAll(q) == std::vector<std::uint8_t>{ge::kControlLeft, ge::kControlLeftRelease}));
    // Full: notches go, the pad's press and release stay.
    q.Control(ge::kControlRight);
    q.Wheel(-200);
    q.Control(ge::kControlRightRelease);
    std::vector<std::uint8_t> const full = TakeAll(q);
    CHECK(full.size() == ge::Queue::kCapacity && full.front() == ge::kControlRight && full.back() == ge::kControlRightRelease);

    ge::Scheduler s;
    std::uint8_t const press[] = {ge::kControlUp, ge::kControlUpRelease};
    s.Add(press, 2);
    CHECK((s.Tick() == std::vector<std::uint8_t>{ge::kControlUp}));
    CHECK((s.Tick() == std::vector<std::uint8_t>{ge::kControlUpRelease}));
    CHECK(s.Tick().empty());
    // After a hover's motion the direction waits for the next tick, alone.
    std::uint8_t const mixed[] = {ge::kMouseMove, ge::kControlLeft};
    s.Add(mixed, 2);
    CHECK((s.Tick() == std::vector<std::uint8_t>{ge::kMouseMove}));
    CHECK((s.Tick() == std::vector<std::uint8_t>{ge::kControlLeft}));
}

} // namespace

int RunGuiEventsTests() {
    gFailures = 0;
    TestQueue();
    TestScheduler();
    TestControls();
    return gFailures;
}
