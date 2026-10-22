#pragma once

#include "core/vr/Controllers.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

// Motion controllers as an Xbox pad, which ME LE already understands. Pure functions (tested
// offline); game/gamepad_bridge feeds the result to the game's XInput calls.
//
// Touch -> Xbox: sticks -> sticks, triggers -> triggers, grips -> LB / RB, A/B (right) -> A/B,
// X/Y (left) -> X/Y, stick clicks -> LS / RS, left menu -> Start; left grip + menu -> Back. The
// left grip on the held gun's foregrip is the support grip instead (LT held, no LB; below), and the
// right grip squeezed at a body holster draws a weapon instead of RB (Options::rightGripFree).
// With the body holsters' "grip holds the gun" (Options::rightGripHolds) the right grip is never RB;
// RB (LE1 on foot: hold = power wheel, tap = mapped power) comes from Options::rbSource instead
// (research/19, "The right grip holds the gun").
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
};

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
    // Not across the boundary: set by the host while the stick clicks belong to the test loop's chord
    // (no RB from the right stick click then), and cleared while Halcyon's menu is open (a squeeze
    // kept from the game there isn't counted: it must never draw a weapon).
    bool stickClicksReserved = false;
    bool countKept = true;
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
inline constexpr std::uint32_t kRbSourceShift = 8;            // Options::rbSource in bits 8-11 (host v10)
inline constexpr std::uint32_t kRbSourceMask = 0xFu << kRbSourceShift;
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
