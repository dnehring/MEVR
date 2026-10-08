#pragma once

#include "core/vr/Controllers.hpp"

#include <Windows.h>
#include <Xinput.h>

// Motion controllers as an Xbox pad, which ME LE already understands. Pure functions (tested
// offline); game/gamepad_bridge feeds the result to the game's XInput calls.
//
// Touch -> Xbox: sticks -> sticks, triggers -> triggers, grips -> LB / RB, A/B (right) -> A/B,
// X/Y (left) -> X/Y, stick clicks -> LS / RS, left menu -> Start; left grip + menu -> Back.
namespace mevr::game::gamepad {

// Grip buttons use hysteresis so a half-squeezed grip doesn't chatter.
struct Latches {
    bool leftGrip = false;
    bool rightGrip = false;
};

XINPUT_GAMEPAD FromControllers(vr::controllers::State const& state, Latches& latches);

// Both pads at once: buttons combined, each stick and trigger taken from whichever is pushed
// further, so a real gamepad keeps working next to the motion controllers.
XINPUT_GAMEPAD Merge(XINPUT_GAMEPAD const& a, XINPUT_GAMEPAD const& b);

// Turns a stick by `yawUnits` (Unreal units, + = right), so pushing forward moves the character
// where the player looks rather than where the game camera faces.
void RotateStick(SHORT& x, SHORT& y, int yawUnits);

} // namespace mevr::game::gamepad
