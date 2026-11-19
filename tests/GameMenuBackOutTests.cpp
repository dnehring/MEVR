// SPDX-License-Identifier: MIT
// Offline tests for backing out of the game's screens by waiting for each to go (vr::game_menu::StepBackOut, ME3's rail Close
// and page switch; research/le3-native-gui.md, lane OMX): one B per screen, the next for the screen that takes its place, a
// second B to the same screen only after resendMs, and the limits.

#include "core/vr/GameMenuRequest.hpp"

#include <cstdio>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/GameMenuBackOutTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace gm = mevr::vr::game_menu;

gm::BackOutScreen const kMap{0x1000, "Areamap"};
gm::BackOutScreen const kBrowser{0x2000, "MenuBrowser"};
gm::BackOutScreen const kNone{};

// The defect lane CTL found: the area map's close (its B release starts an animation; another B starts it again) under a B
// every ~190 ms. The rule sends one B and waits for the map to give way, then backs out of the Mission Computer.
void MapThenBrowserClose() {
    gm::BackOutRules const rules;
    gm::BackOut s;
    std::uint64_t t = 100000;
    CHECK(gm::StepBackOut(s, kMap, t, rules) == gm::BackOutStep::Send);  // the screen up at the start: at once
    CHECK(s.sends == 1 && s.sentToSame == 1 && s.sentTo == kMap);
    // Every tick of the map's close (16 ms apart) up to just before resendMs: wait, never a second B.
    for (std::uint64_t dt = 16; dt < rules.resendMs; dt += 16) {
        CHECK(gm::StepBackOut(s, kMap, t + dt, rules) == gm::BackOutStep::Wait);
    }
    CHECK(s.sends == 1);
    // The map gives way to the Mission Computer 270 ms after its B: the new screen waits settleMs, then gets its B.
    t += 270;
    s = {};
    CHECK(gm::StepBackOut(s, kMap, 100000, rules) == gm::BackOutStep::Send);
    CHECK(gm::StepBackOut(s, kMap, t - 16, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(s, kBrowser, t, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(s, kBrowser, t + rules.settleMs - 1, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(s, kBrowser, t + rules.settleMs, rules) == gm::BackOutStep::Send);
    CHECK(s.sends == 2 && s.sentToSame == 1 && s.sentTo == kBrowser);
}

// A screen that takes no notice of its B (still opening) gets another after resendMs, at most maxPerScreen in all; then the
// back-out gives up instead of looping.
void ResendThenGiveUp() {
    gm::BackOutRules const rules;
    gm::BackOut s;
    std::uint64_t t = 5000;
    CHECK(gm::StepBackOut(s, kBrowser, t, rules) == gm::BackOutStep::Send);
    CHECK(gm::StepBackOut(s, kBrowser, t + rules.resendMs - 1, rules) == gm::BackOutStep::Wait);
    t += rules.resendMs;
    CHECK(gm::StepBackOut(s, kBrowser, t, rules) == gm::BackOutStep::Send);
    CHECK(s.sentToSame == 2 && s.sends == 2);
    t += rules.resendMs;
    CHECK(gm::StepBackOut(s, kBrowser, t, rules) == gm::BackOutStep::Send);
    CHECK(s.sentToSame == 3);
    t += rules.resendMs;
    CHECK(gm::StepBackOut(s, kBrowser, t, rules) == gm::BackOutStep::GiveUp);
    CHECK(s.sends == 3);
}

// A screen is its object and its tag: a new movie with the same tag (ME3 makes one for each page and each return) is a new
// screen; the same object under another tag is too.
void IdentityIsObjectAndTag() {
    gm::BackOutRules const rules;
    gm::BackOut s;
    CHECK(gm::StepBackOut(s, kBrowser, 1000, rules) == gm::BackOutStep::Send);
    gm::BackOutScreen const newBrowser{0x3000, "MenuBrowser"};
    CHECK(gm::StepBackOut(s, newBrowser, 1100, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(s, newBrowser, 1100 + rules.settleMs, rules) == gm::BackOutStep::Send);
    CHECK(s.sentTo == newBrowser && s.sentToSame == 1 && s.sends == 2);
    gm::BackOutScreen const renamed{0x3000, "Journal"};
    CHECK(gm::StepBackOut(s, renamed, 1200 + rules.settleMs, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(s, renamed, 1200 + 2 * rules.settleMs, rules) == gm::BackOutStep::Send);
}

// No screen in focus (between two): wait, then give up after noFocusMs; a screen coming back in time gets its B.
void NoFocus() {
    gm::BackOutRules const rules;
    gm::BackOut s;
    CHECK(gm::StepBackOut(s, kMap, 0x10000, rules) == gm::BackOutStep::Send);
    CHECK(gm::StepBackOut(s, kNone, 0x10000 + 100, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(s, kNone, 0x10000 + 100 + rules.noFocusMs - 1, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(s, kNone, 0x10000 + 100 + rules.noFocusMs, rules) == gm::BackOutStep::GiveUp);
    gm::BackOut t;
    CHECK(gm::StepBackOut(t, kMap, 0x10000, rules) == gm::BackOutStep::Send);
    CHECK(gm::StepBackOut(t, kNone, 0x10000 + 100, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(t, kBrowser, 0x10000 + 300, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(t, kBrowser, 0x10000 + 300 + rules.settleMs, rules) == gm::BackOutStep::Send);
    // Nothing in focus from the start: waits as well.
    gm::BackOut u;
    CHECK(gm::StepBackOut(u, kNone, 50, rules) == gm::BackOutStep::Wait);
    CHECK(u.sends == 0);
}

// maxSends bounds a chain of screens each giving way to another.
void MaxSends() {
    gm::BackOutRules rules;
    rules.maxSends = 3;
    gm::BackOut s;
    std::uint64_t t = 1000;
    CHECK(gm::StepBackOut(s, gm::BackOutScreen{0x100, "Page"}, t, rules) == gm::BackOutStep::Send);
    for (std::uintptr_t i = 2; i <= 3; ++i) {
        gm::BackOutScreen const screen{i * 0x100, "Page"};
        t += rules.settleMs;
        CHECK(gm::StepBackOut(s, screen, t, rules) == gm::BackOutStep::Wait);
        t += rules.settleMs;
        CHECK(gm::StepBackOut(s, screen, t, rules) == gm::BackOutStep::Send);
    }
    CHECK(s.sends == 3);
    gm::BackOutScreen const fourth{0x900, "Page"};
    CHECK(gm::StepBackOut(s, fourth, t + 1, rules) == gm::BackOutStep::Wait);
    CHECK(gm::StepBackOut(s, fourth, t + 1 + rules.settleMs, rules) == gm::BackOutStep::GiveUp);
}

} // namespace

int RunGameMenuBackOutTests() {
    MapThenBrowserClose();
    ResendThenGiveUp();
    IdentityIsObjectAndTag();
    NoFocus();
    MaxSends();
    return gFailures;
}
