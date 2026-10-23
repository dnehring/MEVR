// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "core/vr/Controllers.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

// Motion controllers as an Xbox pad, which ME LE already understands. Pure functions (tested
// offline); game/gamepad_bridge feeds the result to the game's XInput calls.
//
// Touch -> Xbox: sticks -> sticks, triggers -> triggers, grips -> LB / RB, A/B (right) -> A/B,
// X/Y (left) -> X/Y, stick clicks -> LS / RS (LS held back briefly: GateClick), left menu -> Start;
// left grip + menu -> Back. While the squad weapon menu is open (Options::squadMenu) the sticks, A, B
// and the stick clicks are the menu's; while the left grip holds the game's own weapon wheel open
// (Options::nativeWheel) one stick steers it and A queues, the rest is kept from the game. The
// left grip on the held gun's foregrip is the support grip instead (LT held, no LB; below), and the
// right grip squeezed at a body holster draws a weapon instead of RB (Options::rightGripFree).
// With the body holsters' "grip holds the gun" (Options::rightGripHolds) the right grip is never RB;
// RB (LE1 on foot: hold = power wheel, tap = mapped power) comes from Options::rbSource instead
// (research/19, "The right grip holds the gun"). B (melee) and X (grenade) can be kept from the game
// on foot (Options::meleeButton / grenadeButton) once the physical swing and throw replace them
// (research/20).
namespace mevr::game::gamepad {

// The control that sends RB while the right grip holds the gun (Options::rightGripHolds).
enum class RbSource : std::uint8_t {
    None = 0,             // no RB at all
    RightStickClick = 1,  // R3 (the default): also still R3, which Halcyon's double-click recenter reads
    Y = 2,                // instead of Y (first aid)
    X = 3,                // instead of X (grenade)
    B = 4,                // instead of B (melee)
};
char const* Name(RbSource source);

// RB from the right stick click (RbSource::RightStickClick) is decided per press:
// - a press with the stick pushed past this (turning) isn't RB;
inline constexpr float kRbStickCentered = 0.5f;
// - nor one with the left stick clicked too (Halcyon's both-sticks menu chord), seen within the
//   first kRbCommitMs: RB only starts once the click has been held that long alone;
inline constexpr std::uint64_t kRbCommitMs = 100;
// - a click released before then is a tap: RB is sent for kRbTapMs, but only once the double-click
//   window of Halcyon's recenter (kRbDoubleClickMs, press to press) has passed without a second press;
//   a second press inside it is the recenter, and neither click is RB.
inline constexpr std::uint64_t kRbTapMs = 80;
inline constexpr std::uint64_t kRbDoubleClickMs = 400;

// The right stick click as RB, one press at a time (pure; RbFromStickClick).
struct RbClick {
    enum class Phase : std::uint8_t {
        Idle,     // no press, or a press that's over
        Pending,  // pressed, not yet held kRbCommitMs
        Held,     // RB while the click stays down
        TapWait,  // a quick click: RB once the double-click window has passed
        Tap,      // RB for kRbTapMs
        Ignored,  // this press isn't RB (deflected, chord, second click of a double-click, not the source)
    };
    Phase phase = Phase::Idle;
    bool down = false;              // the click, last step
    std::uint64_t pressMs = 0;      // this press (or the tap's press)
    std::uint64_t lastPressMs = 0;  // the last press Halcyon's double-click counts (0: none)
    std::uint64_t tapUntilMs = 0;
};
struct RbClickInput {
    bool click = false;        // the right stick click now
    float stickX = 0.0f;       // the right stick
    float stickY = 0.0f;
    bool otherClick = false;   // the left stick click (Halcyon's both-sticks menu chord)
    bool reserved = false;     // the stick clicks belong to the test loop's chord
    bool enabled = false;      // a new press may be RB (it's the RB source); a press already RB carries on
    std::uint64_t nowMs = 0;   // a millisecond clock; 0: none, so RB just follows an accepted click
};
// One step. True while the game should see RB.
bool RbFromStickClick(RbClick& state, RbClickInput const& input);

// Both stick clicks are a chord, not L3 + R3: held kStickHoldMs, Halcyon's menu opens or closes (as
// before); tapped (the second click down within kStickJoinMs of the first, both up again within
// kStickTapMs of the first press), the test loop takes a snapshot. A tap is over before a hold could
// fire, and a hold outlasts any tap, so neither is ever both. The join window is RB's commit delay:
// a second click later than that comes after R3 may already be RB (or L3 crouch), so it's no tap.
inline constexpr std::uint64_t kStickJoinMs = kRbCommitMs;
inline constexpr std::uint64_t kStickTapMs = 300;
inline constexpr std::uint64_t kStickHoldMs = 400;

struct StickChord {
    bool left = false;              // the clicks, last step
    bool right = false;
    std::uint64_t firstPressMs = 0; // the first click of this sequence (both were up before it)
    std::uint64_t bothSinceMs = 0;  // both down since (0: not both down now)
    bool both = false;              // both were down together in this sequence
    bool joined = false;            // ...the second within kStickJoinMs of the first
    bool held = false;              // the hold fired in this sequence
};
struct StickChordStep {
    bool tap = false;   // both released: a snapshot
    bool hold = false;  // both held kStickHoldMs: Halcyon's menu
};
// One step on the player's own clicks (never a replay's). `nowMs` a millisecond clock (nonzero).
StickChordStep UpdateStickChord(StickChord& chord, bool leftClick, bool rightClick, std::uint64_t nowMs);

// L3 (crouch) waits kStickJoinMs to see whether the right click joins (the chord above): a click held
// that long alone reaches the game; one released sooner reaches it as a kRbTapMs press afterwards;
// one the right click joins never does, nor does it come back when the right click lets go first.
struct ClickGate {
    enum class Phase : std::uint8_t { Idle, Pending, Pass, Pulse, Blocked };
    Phase phase = Phase::Idle;
    bool down = false;
    std::uint64_t pressMs = 0;
    std::uint64_t pulseUntilMs = 0;
};
// One step. True while the game should see the click. `nowMs` 0 (no clock): the click passes as is.
bool GateClick(ClickGate& gate, bool click, bool otherClick, std::uint64_t nowMs);

// The support grip (game/support_grip): the left grip squeezed with the left hand near the held
// gun's foregrip holds the gun with both hands, and the game then sees LT held (aimed, "focused"
// fire) instead of LB. A grip squeezed anywhere else is LB, as always.
//
// Adapted from Overlord's `support_grip` (https://github.com/Aeka0/Overlord, GPL-3.0, Aeka0;
// src/client/component/vr/gameplay/support_grip.hpp): only a NEW squeeze within `acquire` takes
// hold (holding the grip while moving in never grabs), and it lets go when the grip opens or the
// hand leaves `release` (> acquire: hysteresis). Ours adds: a squeeze away from the gun is LB, and
// a hold lost by distance gives nothing (no surprise LB, which opens the power wheel) until the
// grip opens.
enum class LeftGrip {
    Open,      // not squeezed
    Shoulder,  // squeezed away from the gun: LB if Options::gripShoulder
    Support,   // holding the gun's foregrip
    Held,      // squeezed, but the hold was lost: nothing until the grip opens
};

struct SupportInput {
    // The left hand's distance from the foregrip, meters; negative or not finite: no gun to hold.
    float distance = -1.0f;
    float acquire = 0.12f;
    float release = 0.22f;
};

struct SupportGrip {
    bool armed = true;  // the grip has been open since the last squeeze: the next squeeze is new
    bool engaged = false;
    bool shoulder = false;
};

// One step with the left grip's latched state. Pure; tested offline.
LeftGrip UpdateSupportGrip(SupportGrip& grip, bool gripDown, SupportInput const& input);

// Grip buttons use hysteresis so a half-squeezed grip doesn't chatter.
struct Latches {
    bool leftGrip = false;
    bool rightGrip = false;
    bool leftTrigger = false;  // as a button (Options::leftTriggerShoulder)
    SupportGrip support;
    LeftGrip left = LeftGrip::Open;  // the last step's answer
    // The body holsters (game/holsters): the current right-grip squeeze began while
    // Options::rightGripFree or Options::rightGripHolds was set, so it isn't RB until the grip opens;
    // and how many such squeezes there have been (the mod acts on each new one).
    bool rightFree = false;
    std::uint32_t rightFreeCount = 0;
    // The current squeeze began while Options::rightGripHolds was set (it holds the gun).
    bool rightHold = false;
    // RB from Options::rbSource (rightGripHolds): the right stick click's state; for the face buttons
    // B, X, Y (bits 1, 2, 4) whether each is down and whether its current press is RB (decided when
    // it went down).
    RbClick rbClick;
    std::uint8_t faceDown = 0;
    std::uint8_t faceRb = 0;
    // L3, held back while it might be the start of the both-sticks chord.
    ClickGate leftClick;
    // The squad weapon menu (game/squad_weapons): the left-grip squeezes that began away from the gun's
    // foregrip (not the support grip), counted like the right grip's kept squeezes; whether the current
    // squeeze is one.
    std::uint32_t leftAwayCount = 0;
    bool leftAway = false;
    // Controls in use when the squad menu closed (kSquadLeftStick...): kept from the game until each is
    // released or centred, so the flick that chose a weapon doesn't turn, nor A walk off as "use".
    std::uint8_t squadHeld = 0;
    // The game's own weapon wheel held open by the left grip (Options::nativeWheel): whether the flag was
    // set last step, and whether the steering stick has been centred since it was set (until then the
    // game sees no steering, so a stick pushed while turning or walking doesn't pick an entry).
    bool wheelWas = false;
    bool wheelArmed = false;
};

// Which stick steers the game's weapon wheel while the left grip holds it open (Options::wheelStick).
enum class WheelStick : std::uint8_t {
    Right = 0,   // the game's own control (the default)
    Left = 1,    // the left stick, on the hand that holds the grip (sent to the game as the right stick)
    Either = 2,  // whichever is pushed further
};
char const* Name(WheelStick stick);

// The squad menu's controls (Options::squadMenu): none reaches the game while it's open.
inline constexpr std::uint8_t kSquadLeftStick = 1;
inline constexpr std::uint8_t kSquadRightStick = 2;
inline constexpr std::uint8_t kSquadA = 4;
inline constexpr std::uint8_t kSquadB = 8;
inline constexpr std::uint8_t kSquadL3 = 16;
inline constexpr std::uint8_t kSquadR3 = 32;
inline constexpr std::uint8_t kSquadAll = 63;
// A stick inside this is centred (released) for that purpose.
inline constexpr float kSquadStickCentred = 0.25f;

// What the host reports of the left grip (HostServices::leftGripAway, v11): the count of squeezes that
// began away from the gun, shifted up one bit, and whether the current squeeze is one of them.
inline std::uint32_t LeftGripReport(Latches const& latches) {
    return (latches.leftAwayCount << 1) | (latches.leftAway ? 1u : 0u);
}

// What the host reports of the right grip (HostServices::rightGripFree): the count of squeezes kept
// from the game, shifted up one bit, and whether the current squeeze is one of them.
inline std::uint32_t RightGripReport(Latches const& latches) {
    return (latches.rightFreeCount << 1) | (latches.rightFree ? 1u : 0u);
}

struct Options {
    // The left trigger reaches the game as LT (false: it's freed for something else; the game
    // sees no LT from it).
    bool leftTrigger = true;
    // While the support grip holds, the game sees LT fully pulled (false: the grip only steadies
    // the gun, e.g. a sniper rifle, whose aimed mode is a scope camera).
    bool supportAims = true;
    // A left grip squeezed away from the gun is LB (the weapon wheel). Off with the two-handed
    // grip: in play the grip opened the wheel whenever the hand wasn't right at the foregrip.
    bool gripShoulder = true;
    // The left trigger is LB (the weapon wheel) instead of LT.
    bool leftTriggerShoulder = false;
    // The right hand is at a body holster (game/holsters): a NEW right-grip squeeze isn't RB (the
    // power wheel) until the grip opens. A squeeze already held stays RB.
    bool rightGripFree = false;
    // The right grip holds the gun (game/holsters, [Holsters] gripHolds): a NEW right-grip squeeze is
    // never RB (it's counted like a rightGripFree one); RB comes from rbSource. A squeeze keeps the
    // meaning it began with, and so does a press of rbSource.
    bool rightGripHolds = false;
    RbSource rbSource = RbSource::None;  // the mod sets it from [Holsters] powerWheel
    // Not across the boundary: stickClicksReserved, set by the host while the stick clicks belong to
    // something else (no RB from the right stick click then; nothing sets it since the test loop's chord
    // became the both-sticks tap); countKept, cleared while Halcyon's menu is open (a squeeze kept from
    // the game there isn't counted: it must never draw a weapon, nor open the squad menu).
    bool stickClicksReserved = false;
    bool countKept = true;
    // The squad weapon menu is open (game/squad_weapons, kFlagSquadMenu): the sticks, A, B and both
    // stick clicks are its own (Latches::squadHeld keeps them from the game until released).
    bool squadMenu = false;
    // The left grip holds the game's own weapon wheel open ([Squad] style = native, kFlagNativeWheel):
    // the steering stick (wheelStick) reaches the game as the right stick once it has been centred since
    // the wheel opened; the left stick never walks, B and the stick clicks are the mod's; A still reaches
    // the game (A queues the highlighted entry), and wheelPressA adds an A the mod sends to confirm on
    // release. Whatever is in use when it closes stays kept from the game until released or centred.
    bool nativeWheel = false;
    WheelStick wheelStick = WheelStick::Right;
    bool wheelPressA = false;
    // The face buttons for melee and grenades (game/throwables, research/20): false keeps B (melee) or
    // X (grenade) from the game, so only the physical swing or throw does them. A press that is RB
    // (rbSource = b or x) is still RB. The mod clears them only on foot, so menus keep B.
    bool meleeButton = true;
    bool grenadeButton = true;
    // Comfort (game/comfort, research/31): the mod turns the player itself (snap, or smooth at its own speed), so the
    // game sees no right-stick X from the motion controllers (a real pad's still reaches it). Never while the squad
    // menu or the game's wheel owns the stick. The mod sets it only on foot.
    bool modTurns = false;
    // The left stick's forward follows the left controller's yaw (laid flat) instead of the head's: the host turns
    // the motion controllers' left stick before it merges them (needs the poses and Halcyon's head look, so it isn't
    // in FromControllers). The mod sets it only on foot, outside menus and wheels.
    bool handWalk = false;
    SupportInput support;
};

// Options across the host/mod boundary (HostServices::setSupportGrip): flags. A host older than a
// flag ignores it (the mapping then works as before it).
inline constexpr std::uint32_t kFlagLeftTrigger = 1;  // Options::leftTrigger
inline constexpr std::uint32_t kFlagSupportAims = 2;  // Options::supportAims
inline constexpr std::uint32_t kFlagGripShoulder = 4;         // Options::gripShoulder
inline constexpr std::uint32_t kFlagLeftTriggerShoulder = 8;  // Options::leftTriggerShoulder
inline constexpr std::uint32_t kFlagRightGripFree = 16;       // Options::rightGripFree
inline constexpr std::uint32_t kFlagRightGripHolds = 32;      // Options::rightGripHolds (host v10)
inline constexpr std::uint32_t kFlagSquadMenu = 64;           // Options::squadMenu (host v11)
inline constexpr std::uint32_t kFlagNativeWheel = 128;        // Options::nativeWheel (host v12)
inline constexpr std::uint32_t kRbSourceShift = 8;            // Options::rbSource in bits 8-11 (host v10)
inline constexpr std::uint32_t kRbSourceMask = 0xFu << kRbSourceShift;
inline constexpr std::uint32_t kFlagWheelPressA = 1u << 12;   // Options::wheelPressA (host v12)
inline constexpr std::uint32_t kWheelStickShift = 13;         // Options::wheelStick in bits 13-14 (host v12)
inline constexpr std::uint32_t kWheelStickMask = 3u << kWheelStickShift;
// Inverted, so 0 (an older mod, or none sent) keeps both buttons (host v14).
inline constexpr std::uint32_t kFlagNoMeleeButton = 1u << 15;    // !Options::meleeButton
inline constexpr std::uint32_t kFlagNoGrenadeButton = 1u << 16;  // !Options::grenadeButton
inline constexpr std::uint32_t kFlagModTurns = 1u << 17;         // Options::modTurns (host v17)
inline constexpr std::uint32_t kFlagHandWalk = 1u << 18;         // Options::handWalk (host v17)
std::uint32_t ToFlags(Options const& options);
Options FromFlags(float distance, float acquire, float release, std::uint32_t flags);

XINPUT_GAMEPAD FromControllers(vr::controllers::State const& state, Latches& latches, Options const& options);
// Default options: no support grip, LT from the left trigger (the mapping before the support grip).
XINPUT_GAMEPAD FromControllers(vr::controllers::State const& state, Latches& latches);

// Both pads at once: buttons combined, each stick and trigger taken from whichever is pushed
// further, so a real gamepad keeps working next to the motion controllers.
XINPUT_GAMEPAD Merge(XINPUT_GAMEPAD const& a, XINPUT_GAMEPAD const& b);

// Turns a stick by `yawUnits` (Unreal units, + = right), so pushing forward moves the character
// where the player looks rather than where the game camera faces.
void RotateStick(SHORT& x, SHORT& y, int yawUnits);

} // namespace mevr::game::gamepad
