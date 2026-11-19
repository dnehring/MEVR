// SPDX-License-Identifier: MIT
// plugin/PadMerge (research/le2-renderer-integration.md "Step 6: the pad merge"): the motion controllers as the game's pad
// with the mod's flags, shared by both Halcyon hosts. The first test drives it and a copy of LE1's MergeMotionControllers as
// it was before the move (plugin/HalcyonBridge.cpp at c10782d4, its mapping core) with the same random controllers, flags,
// menus and trigger owners, and wants the same pad and the same reports at every step: LE1 is unchanged.
#include "plugin/PadMerge.hpp"

#include "core/vr/Comfort.hpp"
#include "game/GamepadMapping.hpp"
#include "game/NativeMapInput.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>

namespace {

namespace gp = mevr::game::gamepad;
namespace pm = mevr::host::pad_merge;
using mevr::vr::controllers::State;

int gFailures = 0;
void Check(bool ok, int line, char const* what) {
    if (!ok) {
        std::printf("FAIL PadMergeTests.cpp:%d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(x) Check((x), __LINE__, #x)

bool Same(XINPUT_GAMEPAD const& a, XINPUT_GAMEPAD const& b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

// ---- LE1's merge before the move (the reference) -----------------------------------------------------------------------
struct Reference {
    gp::Latches latches;
    std::optional<double> handYaw;
    std::uint32_t leftReport = 0;
    std::uint32_t rightReport = 0;
    bool supportHeld = false;

    struct Input {
        bool fresh = false;  // the mod's input within 250 ms
        float distance = -1.0f, acquire = 0.12f, release = 0.22f;
        std::uint32_t flags = 0;
        bool ownMenuOpen = false;
        int headLook = 0;
        bool ownsLeftTrigger = false;
        bool ownsTriggers = false;
    };

    void Walk(XINPUT_GAMEPAD& pad, State const& controllers, bool on, int headLook) {
        namespace cf = mevr::vr::comfort;
        if (!on) {
            handYaw.reset();
            return;
        }
        if (controllers.left.aimValid) {
            XrQuaternionf const& q = controllers.left.aim.orientation;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            cf::Forward(q.x, q.y, q.z, q.w, x, y, z);
            if (std::optional<double> const yaw = cf::FlatYawLeft(x, y, z)) {
                handYaw = yaw;
            }
        }
        if (!handYaw || (pad.sThumbLX == 0 && pad.sThumbLY == 0)) {
            return;
        }
        gp::RotateStick(pad.sThumbLX, pad.sThumbLY, cf::HandWalkUnits(*handYaw, headLook));
    }

    XINPUT_GAMEPAD Merge(XINPUT_GAMEPAD const& real, State const& controllers, Input const& in) {
        gp::Options options = in.fresh ? gp::FromFlags(in.distance, in.acquire, in.release, in.flags) : gp::Options{};
        if (in.ownMenuOpen) {
            options.rightGripFree = false;
            options.countKept = false;
        }
        XINPUT_GAMEPAD touch = gp::FromControllers(controllers, latches, options);
        bool const owned = options.squadMenu || options.nativeWheel;
        Walk(touch, controllers, options.handWalk && !owned && !in.ownMenuOpen, in.headLook);
        XINPUT_GAMEPAD pad = gp::Merge(real, touch);
        leftReport = gp::LeftGripReport(latches);
        rightReport = gp::RightGripReport(latches);
        supportHeld = latches.left == gp::LeftGrip::Support;
        if (controllers.left.stickClick && controllers.right.stickClick) {
            pad.wButtons &= ~(XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB);
        }
        if (in.ownsLeftTrigger) {
            pad.bLeftTrigger = 0;
        }
        if (in.ownsTriggers) {
            pad.bLeftTrigger = 0;
            pad.bRightTrigger = 0;
        }
        return pad;
    }
};

// The renderer's answers for PadMerge, from the test's input.
int gHeadLook = 0;
bool gOwnsLeft = false;
bool gOwnsBoth = false;
int HeadLook() { return gHeadLook; }
bool OwnsLeft() { return gOwnsLeft; }
bool OwnsBoth() { return gOwnsBoth; }

XrQuaternionf RandomOrientation(std::mt19937& rng) {
    std::normal_distribution<float> n(0.0f, 1.0f);
    XrQuaternionf q{n(rng), n(rng), n(rng), n(rng)};
    float const length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length < 1e-3f) return {0.0f, 0.0f, 0.0f, 1.0f};
    return {q.x / length, q.y / length, q.z / length, q.w / length};
}

void RandomHand(std::mt19937& rng, mevr::vr::controllers::Hand& hand, mevr::vr::controllers::Hand const& last) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> axis(-1.0f, 1.0f);
    auto keep = [&](float p) { return unit(rng) < p; };
    // Mostly what it was a step ago (presses last, gates and latches step), now and then something new.
    hand = last;
    if (!keep(0.8f)) hand.trigger = keep(0.5f) ? 0.0f : unit(rng);
    if (!keep(0.8f)) hand.squeeze = keep(0.5f) ? 0.0f : unit(rng);
    if (!keep(0.7f)) hand.stickX = keep(0.4f) ? 0.0f : axis(rng);
    if (!keep(0.7f)) hand.stickY = keep(0.4f) ? 0.0f : axis(rng);
    if (!keep(0.85f)) hand.stickClick = keep(0.5f);
    if (!keep(0.85f)) hand.primary = keep(0.5f);
    if (!keep(0.85f)) hand.secondary = keep(0.5f);
    if (!keep(0.95f)) hand.menu = keep(0.8f) ? false : true;
    hand.aimValid = keep(0.9f);
    if (!keep(0.7f)) hand.aim.orientation = RandomOrientation(rng);
    hand.gripValid = hand.aimValid;
    hand.grip = hand.aim;
}

// The mapping core, LE1's way, over random steps.
void TestSameAsLe1Before() {
    pm::Reset();
    Reference reference;
    std::mt19937 rng(20261114);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_int_distribution<int> time(1, 60);
    std::uniform_int_distribution<int> look(-12000, 12000);
    // The flags the mod sends (game/GamepadMapping's kFlag...), drawn bit by bit, and the rbSource and wheel stick fields.
    std::uint32_t const bits[] = {gp::kFlagLeftTrigger, gp::kFlagSupportAims, gp::kFlagGripShoulder, gp::kFlagLeftTriggerShoulder,
        gp::kFlagRightGripFree, gp::kFlagRightGripHolds, gp::kFlagSquadMenu, gp::kFlagNativeWheel, gp::kFlagWheelPressA,
        gp::kFlagNoMeleeButton, gp::kFlagNoGrenadeButton, gp::kFlagModTurns, gp::kFlagHandWalk, gp::kFlagMakoDrive,
        gp::kFlagMakoLever, gp::kFlagMakoStick, gp::kFlagPowerWheel, gp::kFlagModPowerHold, gp::kFlagSquadPlace,
        gp::kFlagHandPowerWheel};
    State controllers;
    controllers.active = true;
    controllers.timeMs = 1000;
    Reference::Input in;
    unsigned mismatches = 0;
    unsigned supportSteps = 0, walkedSteps = 0, chordSteps = 0;
    for (int step = 0; step < 20000; ++step) {
        State const last = controllers;
        RandomHand(rng, controllers.left, last.left);
        RandomHand(rng, controllers.right, last.right);
        controllers.timeMs += static_cast<std::uint64_t>(time(rng));
        ++controllers.sequence;
        // The mod's input changes now and then; sometimes it goes stale.
        if (step % 97 == 0) {
            in.flags = 0;
            for (std::uint32_t const bit : bits) {
                if (unit(rng) < 0.3f) in.flags |= bit;
            }
            in.flags |= (static_cast<std::uint32_t>(unit(rng) * 5.0f) << gp::kRbSourceShift) & gp::kRbSourceMask;
            in.flags |= (static_cast<std::uint32_t>(unit(rng) * 2.0f) << gp::kWheelStickShift) & gp::kWheelStickMask;
            in.distance = unit(rng) < 0.4f ? -1.0f : unit(rng) * 0.4f;
            in.fresh = unit(rng) < 0.8f;
        }
        if (step % 13 == 0) {
            in.ownMenuOpen = unit(rng) < 0.1f;
            in.ownsLeftTrigger = unit(rng) < 0.1f;
            in.ownsTriggers = unit(rng) < 0.1f;
            in.headLook = unit(rng) < 0.5f ? 0 : look(rng);
        }
        XINPUT_GAMEPAD real{};
        if (unit(rng) < 0.2f) {
            real.wButtons = static_cast<WORD>(rng() & 0xF3FF);
            real.bLeftTrigger = static_cast<BYTE>(rng() & 0xFF);
            real.sThumbRX = static_cast<SHORT>(static_cast<int>(rng() & 0xFFFF) - 32768);
        }
        // PadMerge reads the mod's input at a GetTickCount64 clock: fresh = just sent, stale = sent long before `now`.
        pm::SetSupportGrip(in.distance, in.acquire, in.release, in.flags);
        std::uint64_t const now = GetTickCount64() + (in.fresh ? 0 : 10000);
        gHeadLook = in.headLook;
        gOwnsLeft = in.ownsLeftTrigger;
        gOwnsBoth = in.ownsTriggers;
        pm::Renderer renderer;
        renderer.ownMenuOpen = in.ownMenuOpen;
        renderer.headLookYawUU = &HeadLook;
        renderer.ownsLeftTrigger = &OwnsLeft;
        renderer.ownsTriggers = &OwnsBoth;
        XINPUT_GAMEPAD const got = pm::Merge(real, controllers, renderer, now);
        XINPUT_GAMEPAD const want = reference.Merge(real, controllers, in);
        bool const same = Same(got, want) && pm::LeftGripAway() == reference.leftReport &&
            pm::RightGripFree() == reference.rightReport && pm::SupportGripHeld() == reference.supportHeld;
        if (!same && mismatches++ < 5) {
            std::printf("step %d: buttons %04x/%04x LT %u/%u RT %u/%u L (%d,%d)/(%d,%d) R (%d,%d)/(%d,%d) reports %u/%u %u/%u %d/%d\n",
                step, got.wButtons, want.wButtons, got.bLeftTrigger, want.bLeftTrigger, got.bRightTrigger, want.bRightTrigger,
                got.sThumbLX, got.sThumbLY, want.sThumbLX, want.sThumbLY, got.sThumbRX, got.sThumbRY, want.sThumbRX, want.sThumbRY,
                pm::LeftGripAway(), reference.leftReport, pm::RightGripFree(), reference.rightReport, pm::SupportGripHeld(),
                reference.supportHeld);
        }
        supportSteps += reference.supportHeld ? 1u : 0u;
        walkedSteps += reference.handYaw ? 1u : 0u;
        chordSteps += controllers.left.stickClick && controllers.right.stickClick ? 1u : 0u;
    }
    CHECK(mismatches == 0);
    // The random walk reached the parts it is meant to compare.
    CHECK(supportSteps > 50);
    CHECK(walkedSteps > 500);
    CHECK(chordSteps > 500);
    std::printf("pad merge: 20000 random steps, %u mismatches with LE1's merge before the move (support grip held %u, "
                "walking toward the hand %u, both clicks down %u)\n", mismatches, supportSteps, walkedSteps, chordSteps);
}

State Controllers(std::uint64_t timeMs) {
    State s;
    s.active = true;
    s.timeMs = timeMs;
    s.left.aimValid = s.right.aimValid = true;
    s.left.aim.orientation = s.right.aim.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    return s;
}

// The mod's input goes stale after kInputMaxAgeMs: the mapping's defaults again (LT from the left trigger).
void TestStaleInput() {
    pm::Reset();
    pm::Renderer const renderer;
    State s = Controllers(1000);
    s.left.trigger = 1.0f;
    pm::SetSupportGrip(-1.0f, 0.12f, 0.22f, 0);  // the left trigger freed (no kFlagLeftTrigger)
    std::uint64_t const now = GetTickCount64();
    CHECK(pm::Merge({}, s, renderer, now).bLeftTrigger == 0);
    CHECK(pm::FreshFlags(now) == 0u);
    pm::SetSupportGrip(-1.0f, 0.12f, 0.22f, gp::kFlagCockpitLook);
    CHECK(pm::FreshFlags(GetTickCount64()) == gp::kFlagCockpitLook);
    CHECK(pm::FreshFlags(GetTickCount64() + pm::kInputMaxAgeMs + 1000) == 0u);
    s.timeMs += 16;
    CHECK(pm::Merge({}, s, renderer, GetTickCount64() + pm::kInputMaxAgeMs + 1000).bLeftTrigger == 255);
    pm::ForgetSupportGrip();
    CHECK(pm::FreshFlags(GetTickCount64()) == 0u);
}

// Both stick clicks down are the chord (our menu), never L3 or R3 for the game; the trigger owners take their triggers.
void TestChordAndOwners() {
    pm::Reset();
    pm::Renderer renderer;
    State s = Controllers(1000);
    s.left.stickClick = s.right.stickClick = true;
    for (int i = 0; i < 40; ++i) {
        s.timeMs += 16;
        XINPUT_GAMEPAD const pad = pm::Merge({}, s, renderer, GetTickCount64());
        CHECK((pad.wButtons & (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB)) == 0);
    }
    s.left.stickClick = s.right.stickClick = false;
    s.left.trigger = s.right.trigger = 1.0f;
    gOwnsLeft = true;
    gOwnsBoth = false;
    renderer.ownsLeftTrigger = &OwnsLeft;
    renderer.ownsTriggers = &OwnsBoth;
    s.timeMs += 16;
    XINPUT_GAMEPAD pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 255);
    gOwnsLeft = false;
    gOwnsBoth = true;
    s.timeMs += 16;
    pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 0);
}

// The support grip (v7): a new left squeeze near the foregrip holds the gun, LT to the game; no controllers let it go.
void TestSupportGrip() {
    pm::Reset();
    pm::Renderer const renderer;
    State s = Controllers(1000);
    std::uint32_t const flags = gp::kFlagLeftTrigger | gp::kFlagSupportAims;
    pm::SetSupportGrip(0.05f, 0.12f, 0.22f, flags);
    (void)pm::Merge({}, s, renderer, GetTickCount64());  // the grip open: armed
    s.left.squeeze = 1.0f;
    s.timeMs += 16;
    pm::SetSupportGrip(0.05f, 0.12f, 0.22f, flags);
    XINPUT_GAMEPAD const pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pm::SupportGripHeld());
    CHECK(pad.bLeftTrigger == 255);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);
    pm::NoControllers();
    CHECK(!pm::SupportGripHeld());
}

// The holsters' grip that holds the gun (v10): the squeeze is counted for the mod (v9 report), never RB; not counted
// while the renderer's own menu is open.
void TestRightGripHolds() {
    pm::Reset();
    pm::Renderer renderer;
    State s = Controllers(1000);
    std::uint32_t const flags = gp::kFlagLeftTrigger | gp::kFlagSupportAims | gp::kFlagRightGripHolds;
    pm::SetSupportGrip(-1.0f, 0.12f, 0.22f, flags);
    (void)pm::Merge({}, s, renderer, GetTickCount64());
    s.right.squeeze = 1.0f;
    s.timeMs += 16;
    XINPUT_GAMEPAD pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
    CHECK(pm::RightGripFree() == ((1u << 1) | 1u));
    s.right.squeeze = 0.0f;
    s.timeMs += 16;
    (void)pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pm::RightGripFree() == (1u << 1));
    renderer.ownMenuOpen = true;  // Halcyon's menu: a squeeze there isn't counted
    s.right.squeeze = 1.0f;
    s.timeMs += 16;
    pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
    CHECK((pm::RightGripFree() >> 1) == 1u);
}

// The left grip away from the gun (v11): counted for the squad and the grenades.
void TestLeftGripAway() {
    pm::Reset();
    pm::Renderer const renderer;
    State s = Controllers(1000);
    pm::SetSupportGrip(1.0f, 0.12f, 0.22f, gp::kFlagLeftTrigger | gp::kFlagSupportAims);
    (void)pm::Merge({}, s, renderer, GetTickCount64());
    s.left.squeeze = 1.0f;
    s.timeMs += 16;
    (void)pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pm::LeftGripAway() == ((1u << 1) | 1u));
    CHECK(!pm::SupportGripHeld());
}

// Walking toward the left hand (comfort, v17): the stick turned to the hand's flat yaw, less the renderer's head look.
void TestWalkTowardHand() {
    pm::Reset();
    pm::Renderer renderer;
    renderer.headLookYawUU = &HeadLook;
    gHeadLook = 0;
    State s = Controllers(1000);
    // The left hand pointing 90 degrees left: forward (-Z) turned about +Y.
    float const half = 0.5f * 3.14159265f * 0.5f;
    s.left.aim.orientation = {0.0f, std::sin(half), 0.0f, std::cos(half)};
    s.left.stickY = 1.0f;
    pm::SetSupportGrip(-1.0f, 0.12f, 0.22f, gp::kFlagLeftTrigger | gp::kFlagSupportAims | gp::kFlagHandWalk);
    XINPUT_GAMEPAD pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pad.sThumbLX < -30000 && std::abs(pad.sThumbLY) < 2000);  // pushed forward, it walks to the left
    gHeadLook = 16384;  // the renderer then turns the stick 90 degrees left itself: the merge leaves it ahead
    s.timeMs += 16;
    pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(std::abs(pad.sThumbLX) < 2000 && pad.sThumbLY > 30000);
}

// The owner's L3 + R3 priority on ME2 (Renderer::gateRightClick): R3 alone is held back kStickJoinMs (a chord may follow),
// then reaches the game; with L3 joining it never does. LE1 (false): R3 at once.
void TestRightClickGate() {
    pm::Reset();
    pm::Renderer me2;
    me2.gateRightClick = true;
    State s = Controllers(1000);
    s.right.stickClick = true;
    XINPUT_GAMEPAD pad = pm::Merge({}, s, me2, GetTickCount64());
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) == 0);  // held back
    s.timeMs += gp::kStickJoinMs + 10;
    pad = pm::Merge({}, s, me2, GetTickCount64());
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);  // alone long enough: the game's
    s.right.stickClick = false;
    s.timeMs += 50;
    (void)pm::Merge({}, s, me2, GetTickCount64());
    // R3 then L3 within the join window: the chord, neither reaches the game, nor R3 when L3 lets go first.
    s.right.stickClick = true;
    s.timeMs += 500;
    pad = pm::Merge({}, s, me2, GetTickCount64());
    s.left.stickClick = true;
    s.timeMs += 30;
    pad = pm::Merge({}, s, me2, GetTickCount64());
    CHECK((pad.wButtons & (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB)) == 0);
    for (int i = 0; i < 30; ++i) {
        s.timeMs += 16;
        pad = pm::Merge({}, s, me2, GetTickCount64());
        CHECK((pad.wButtons & (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB)) == 0);
    }
    s.left.stickClick = false;
    s.timeMs += 16;
    pad = pm::Merge({}, s, me2, GetTickCount64());
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) == 0);
    // LE1: R3 at once.
    pm::Reset();
    pm::Renderer const le1;
    State t = Controllers(1000);
    t.right.stickClick = true;
    pad = pm::Merge({}, t, le1, GetTickCount64());
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
}

// The sequels' bridge (Renderer::keepPowerClick, the owner 2026-11-17): with the mod owning the click (kFlagModPowerHold) R3
// never reaches the game, held or tapped; without the mod's flag it's the game's R3 after the gate; LE1's renderer sends it.
void TestPowerClickKept() {
    std::uint32_t const owned = gp::kFlagLeftTrigger | gp::kFlagSupportAims | gp::kFlagRightGripHolds | gp::kFlagModPowerHold |
        (static_cast<std::uint32_t>(gp::RbSource::RightStickClick) << gp::kRbSourceShift);
    auto const run = [](pm::Renderer const& renderer, std::uint32_t flags) {
        pm::Reset();
        State s = Controllers(1000);
        WORD buttons = 0;
        for (int i = 0; i < 80; ++i) {  // released, held 0.8 s, released, a 50 ms tap, released
            s.right.stickClick = (i >= 5 && i < 55) || (i >= 65 && i < 68);
            s.timeMs += 16;
            pm::SetSupportGrip(-1.0f, 0.12f, 0.22f, flags);
            buttons = static_cast<WORD>(buttons | pm::Merge({}, s, renderer, GetTickCount64()).wButtons);
        }
        return buttons;
    };
    pm::Renderer sequel;
    sequel.gateRightClick = true;
    sequel.keepPowerClick = true;
    CHECK((run(sequel, owned) & (XINPUT_GAMEPAD_RIGHT_THUMB | XINPUT_GAMEPAD_RIGHT_SHOULDER)) == 0);
    CHECK((run(sequel, owned & ~gp::kFlagModPowerHold) & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);  // the game's own click
    pm::Renderer const le1;
    CHECK((run(le1, owned) & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
    pm::Reset();
}

// ME2's interrupts on the triggers (Renderer::conversationTriggers, M5.2): in the conversation mode the left trigger is LT
// and the right RT whatever the mod's flags say on foot ([Controls] leftTrigger = lb, the left trigger freed for the squad, a
// support grip still latched, the Mako's lever) and whatever of the host's owns a trigger; out of it, the flags again.
void TestConversationTriggers() {
    pm::Reset();
    pm::Renderer renderer;
    State s = Controllers(1000);
    // On foot with the squad wheel's LB ([Controls] leftTrigger = lb) and the trigger not LT.
    std::uint32_t const onFoot = gp::kFlagLeftTriggerShoulder | gp::kFlagSupportAims;
    pm::SetSupportGrip(-1.0f, 0.12f, 0.22f, onFoot);
    s.left.trigger = s.right.trigger = 1.0f;
    XINPUT_GAMEPAD pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pad.bLeftTrigger == 0 && (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0 && pad.bRightTrigger == 255);
    // The conversation mode: LT and RT, no LB.
    renderer.conversationTriggers = true;
    s.timeMs += 16;
    pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pad.bLeftTrigger == 255 && pad.bRightTrigger == 255 && (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);
    // Let go: nothing (no interrupt by itself).
    s.left.trigger = s.right.trigger = 0.0f;
    s.timeMs += 16;
    pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 0);
    // The host's trigger owners don't take them there (a pointer on a quad of ours).
    gOwnsLeft = true;
    gOwnsBoth = true;
    renderer.ownsLeftTrigger = &OwnsLeft;
    renderer.ownsTriggers = &OwnsBoth;
    s.left.trigger = 1.0f;
    s.timeMs += 16;
    pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pad.bLeftTrigger == 255 && pad.bRightTrigger == 0);
    s.left.trigger = 0.0f;
    s.right.trigger = 1.0f;
    s.timeMs += 16;
    pad = pm::Merge({}, s, renderer, GetTickCount64());
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 255);
    // A support grip latched from gameplay pulls no LT in the conversation (an accidental Paragon interrupt).
    pm::Reset();
    pm::Renderer foot;
    State g = Controllers(1000);
    std::uint32_t const grip = gp::kFlagLeftTrigger | gp::kFlagSupportAims;
    pm::SetSupportGrip(0.05f, 0.12f, 0.22f, grip);
    (void)pm::Merge({}, g, foot, GetTickCount64());  // the grip open: armed
    g.left.squeeze = 1.0f;
    g.timeMs += 16;
    pad = pm::Merge({}, g, foot, GetTickCount64());
    CHECK(pm::SupportGripHeld() && pad.bLeftTrigger == 255);  // on foot: aimed fire
    pm::Renderer convo;
    convo.conversationTriggers = true;
    g.timeMs += 16;
    pad = pm::Merge({}, g, convo, GetTickCount64());
    CHECK(pad.bLeftTrigger == 0);
    // The Mako's driving keeps the left trigger off its lever on foot... in the conversation mode it's LT.
    pm::SetSupportGrip(-1.0f, 0.12f, 0.22f, gp::kFlagLeftTrigger | gp::kFlagMakoDrive);
    State m = Controllers(5000);
    m.left.trigger = 1.0f;
    pad = pm::Merge({}, m, foot, GetTickCount64());
    CHECK(pad.bLeftTrigger == 0);
    m.timeMs += 16;
    pad = pm::Merge({}, m, convo, GetTickCount64());
    CHECK(pad.bLeftTrigger == 255);
    // LE3's conversation wheel reads unrotated left-stick axes and A; X remains native skip.
    // A stale gameplay wheel/hand-walk setting must not steal or rotate this input.
    pm::Reset();
    pm::SetSupportGrip(-1, 0.12f, 0.22f, gp::kFlagHandWalk | gp::kFlagSquadMenu | gp::kFlagNativeWheel |
        gp::kFlagHandPowerWheel | gp::kFlagPowerWheel | gp::kFlagWheelPressA);
    State wheel = Controllers(7000);
    wheel.left.stickX = 0.75f;
    wheel.right.primary = true;
    wheel.left.primary = true;
    pad = pm::Merge({}, wheel, convo, GetTickCount64());
    CHECK(pad.sThumbLX > 20000 && pad.sThumbLY == 0);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_A) != 0 && (pad.wButtons & XINPUT_GAMEPAD_X) != 0);
    // ConversationTriggers itself.
    gp::Options o;
    o.leftTrigger = false;
    o.leftTriggerShoulder = true;
    o.supportAims = true;
    o.makoDrive = o.makoLever = o.makoStick = o.powerWheel = true;
    gp::Options const c = pm::ConversationTriggers(o);
    CHECK(c.leftTrigger && !c.leftTriggerShoulder && !c.supportAims && !c.makoDrive && !c.makoLever && !c.makoStick && !c.powerWheel);
}

void TestOrbitalTriggers() {
    pm::Reset();
    pm::Renderer orbital;
    orbital.orbitalTriggers = true;
    gOwnsBoth = true;
    orbital.ownsTriggers = &OwnsBoth;
    State s = Controllers(1000);
    pm::SetSupportGrip(-1, 0.12f, 0.22f, gp::kFlagLeftTriggerShoulder | gp::kFlagSupportAims);
    s.left.trigger = s.right.trigger = 1;
    auto pad = pm::Merge({}, s, orbital, GetTickCount64());
    CHECK(pad.bLeftTrigger == 255 && pad.bRightTrigger == 255);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);
    // A planet-selection pull is held through orbit entry, then released independently.
    gp::CloseLatch latch;
    latch.leftTrigger = latch.rightTrigger = true;
    CHECK(gp::ApplyCloseLatch(latch, pad));
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 0);
    pad.bLeftTrigger = 0;
    pad.bRightTrigger = 255;
    CHECK(gp::ApplyCloseLatch(latch, pad));
    CHECK(!latch.leftTrigger && latch.rightTrigger);
    pad.bLeftTrigger = 255;
    pad.bRightTrigger = 0;
    CHECK(!gp::ApplyCloseLatch(latch, pad));
    CHECK(pad.bLeftTrigger == 255 && pad.bRightTrigger == 0);
    gOwnsBoth = false;
}

} // namespace

void TestNativeMapTransitions() {
    gp::NativeMapInput gate;
    XINPUT_GAMEPAD pad{};
    auto held = [&] { pad.bLeftTrigger = 255; pad.bRightTrigger = 255; };
    pad.sThumbLX = 24000; pad.sThumbLY = -16000; pad.sThumbRX = 11000;
    held();
    gate.Apply(true, false, 1, true, pad);
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 0);
    CHECK(pad.sThumbLX == 24000 && pad.sThumbLY == -16000 && pad.sThumbRX == 11000);
    gate.Apply(true, false, 1, true, pad); // both physically released
    held(); gate.Apply(true, false, 1, true, pad);
    CHECK(pad.bLeftTrigger == 255 && pad.bRightTrigger == 255);
    held(); gate.Apply(true, true, 1, true, pad); // selecting a planet must not launch on orbit entry
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 0);
    pad.bLeftTrigger = 0; pad.bRightTrigger = 255; gate.Apply(true, true, 1, true, pad);
    held(); gate.Apply(true, true, 1, true, pad);
    CHECK(pad.bLeftTrigger == 255 && pad.bRightTrigger == 0); // independent release
    pad.bLeftTrigger = pad.bRightTrigger = 0;
    gate.Apply(true, true, 1, false, pad); // neutral output while menu owns input is not release
    held(); gate.Apply(true, true, 1, true, pad);
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 0);
    gate.Apply(true, true, 1, true, pad);
    held(); gate.Apply(true, true, 2, true, pad); // session/recenter generation
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 0);
    gate.Apply(true, true, 2, true, pad);
    held(); gate.Apply(false, false, 2, true, pad);
    CHECK(pad.bLeftTrigger == 255 && pad.bRightTrigger == 255); // never alters non-map mapping
    gate.Apply(true, false, 2, true, pad);
    CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 0);
    CHECK(pad.sThumbLX == 24000 && pad.sThumbLY == -16000 && pad.sThumbRX == 11000); // pursuit steering intact
}

void TestNativeMapLease() {
    gp::NativeMapInputLease lease;
    pm::Reset();
    pm::Renderer map;
    map.orbitalTriggers = true;
    // The host must keep native pursuit/reticle steering despite stale on-foot flags.
    pm::SetSupportGrip(-1, 0.12f, 0.22f, gp::kFlagHandWalk | gp::kFlagSquadMenu |
        gp::kFlagHandPowerWheel | gp::kFlagModTurns | gp::kFlagLeftTriggerShoulder);
    State hands = Controllers(1000);
    hands.left.trigger = hands.right.trigger = 1;
    hands.left.stickX = .75f; hands.left.stickY = -.5f;
    hands.right.stickX = .5f;
    auto raw = [&] { return pm::Merge({}, hands, map, GetTickCount64()); };
    auto pad = raw();
    auto const steering = pad;
    auto apply = [&](std::uint64_t now, bool allowed = true, bool orbital = false, std::uint64_t generation = 1) {
        pad = raw(); lease.Apply(true, orbital, generation, allowed, now, pad);
        CHECK(pad.sThumbLX == steering.sThumbLX && pad.sThumbLY == steering.sThumbLY && pad.sThumbRX == steering.sThumbRX);
    };
    auto blocked = [&] { CHECK(pad.bLeftTrigger == 0 && pad.bRightTrigger == 0); };
    auto pass = [&] { CHECK(pad.bLeftTrigger == 255 && pad.bRightTrigger == 255); };
    apply(1000); blocked(); // no owner publication
    lease.Publish(true, false, 1, true, 1000);
    apply(1001); blocked(); // entry while held
    hands.left.trigger = hands.right.trigger = 0; apply(1002);
    hands.left.trigger = hands.right.trigger = 1; apply(1003); pass();
    // A valid frame renewal must not demand release every frame.
    lease.Publish(true, false, 1, true, 1016); apply(1017); pass();
    apply(1267); blocked(); // rendering stopped; held trigger cannot survive expiry
    lease.Publish(true, false, 1, true, 1268); apply(1269); blocked();
    hands.left.trigger = 0; apply(1270);
    hands.left.trigger = 1; apply(1271);
    CHECK(pad.bLeftTrigger == 255 && pad.bRightTrigger == 0);
    hands.right.trigger = 0; apply(1272);
    hands.right.trigger = 1; apply(1273); pass();
    // A menu, tracking loss or image absence can begin and end with no XInput poll.
    lease.Publish(true, false, 1, false, 1280);
    lease.Publish(true, false, 1, true, 1290); apply(1291); blocked();
    hands.left.trigger = hands.right.trigger = 0; apply(1292);
    hands.left.trigger = hands.right.trigger = 1; apply(1293); pass();
    // Same-poll ownership takes precedence over a still-fresh render publication.
    apply(1294, false); blocked();
    apply(1295); blocked(); // ownership alone cannot revive the expired lease
    lease.Publish(true, false, 1, true, 1300); apply(1301); blocked();
    hands.left.trigger = hands.right.trigger = 0; apply(1302);
    hands.left.trigger = hands.right.trigger = 1; apply(1303); pass();
    apply(1304, true, true); blocked(); // entering orbit before its first publication
    lease.Publish(true, true, 1, true, 1305); apply(1306, true, true); blocked();
    hands.left.trigger = hands.right.trigger = 0; apply(1307, true, true);
    hands.left.trigger = hands.right.trigger = 1; apply(1308, true, true); pass();
    lease.Publish(true, true, 2, true, 1310); apply(1311, true, true, 2); blocked(); // space/recenter/session
    hands.left.trigger = hands.right.trigger = 0; apply(1312, true, true, 2);
    hands.left.trigger = hands.right.trigger = 1; apply(1313, true, true, 2); pass();
    apply(1309, true, true, 2); blocked(); // clock reversal fails closed
    lease.Reset(); apply(1314, true, true, 2); blocked();
    pad = raw(); lease.Apply(false, false, 2, false, 1315, pad); pass(); // no non-map takeover
}

int main() {
    TestNativeMapLease();
    TestNativeMapTransitions();
    TestSameAsLe1Before();
    TestStaleInput();
    TestChordAndOwners();
    TestSupportGrip();
    TestRightGripHolds();
    TestLeftGripAway();
    TestWalkTowardHand();
    TestRightClickGate();
    TestPowerClickKept();
    TestConversationTriggers();
    TestOrbitalTriggers();
    if (gFailures == 0) {
        std::printf("pad merge tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
