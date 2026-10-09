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
// left grip on the held gun's foregrip is the support grip instead (LT held, no LB; below).
namespace mevr::game::gamepad {

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
    Shoulder,  // squeezed away from the gun: LB
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
    SupportGrip support;
    LeftGrip left = LeftGrip::Open;  // the last step's answer
};

struct Options {
    // The left trigger reaches the game as LT (false: it's freed for something else; the game
    // sees no LT from it).
    bool leftTrigger = true;
    // While the support grip holds, the game sees LT fully pulled (false: the grip only steadies
    // the gun, e.g. a sniper rifle, whose aimed mode is a scope camera).
    bool supportAims = true;
    SupportInput support;
};

// Options across the host/mod boundary (HostServices::setSupportGrip): two flags.
inline constexpr std::uint32_t kFlagLeftTrigger = 1;  // Options::leftTrigger
inline constexpr std::uint32_t kFlagSupportAims = 2;  // Options::supportAims
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
