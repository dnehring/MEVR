#include "game/GamepadMapping.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace mevr::game::gamepad {
namespace {

constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;

SHORT ToAxis(float value) {
    float const clamped = std::clamp(value, -1.0f, 1.0f);
    return static_cast<SHORT>(std::lround(clamped * 32767.0f));
}

BYTE ToTrigger(float value) {
    return static_cast<BYTE>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

bool Latch(bool& latched, float squeeze) {
    latched = latched ? squeeze > kGripOff : squeeze >= kGripOn;
    return latched;
}

// The stick pair pushed further from center.
void Stronger(SHORT ax, SHORT ay, SHORT bx, SHORT by, SHORT& outX, SHORT& outY) {
    long long const a = static_cast<long long>(ax) * ax + static_cast<long long>(ay) * ay;
    long long const b = static_cast<long long>(bx) * bx + static_cast<long long>(by) * by;
    outX = a >= b ? ax : bx;
    outY = a >= b ? ay : by;
}

} // namespace

LeftGrip UpdateSupportGrip(SupportGrip& grip, bool gripDown, SupportInput const& input) {
    bool const available = std::isfinite(input.distance) && input.distance >= 0.0f && std::isfinite(input.acquire) &&
        std::isfinite(input.release) && input.acquire > 0.0f && input.release >= input.acquire;
    if (!gripDown) {
        grip.engaged = false;
        grip.shoulder = false;
        grip.armed = true;
        return LeftGrip::Open;
    }
    if (grip.armed) {  // a new squeeze: on the foregrip, or the shoulder button
        grip.armed = false;
        grip.engaged = available && input.distance <= input.acquire;
        grip.shoulder = !grip.engaged;
        return grip.engaged ? LeftGrip::Support : LeftGrip::Shoulder;
    }
    if (grip.engaged) {
        if (!available || input.distance > input.release) {
            grip.engaged = false;  // let go; nothing more until the grip opens
            return LeftGrip::Held;
        }
        return LeftGrip::Support;
    }
    return grip.shoulder ? LeftGrip::Shoulder : LeftGrip::Held;
}

std::uint32_t ToFlags(Options const& options) {
    return (options.leftTrigger ? kFlagLeftTrigger : 0u) | (options.supportAims ? kFlagSupportAims : 0u) |
           (options.gripShoulder ? kFlagGripShoulder : 0u) | (options.leftTriggerShoulder ? kFlagLeftTriggerShoulder : 0u) |
           (options.rightGripFree ? kFlagRightGripFree : 0u);
}

Options FromFlags(float distance, float acquire, float release, std::uint32_t flags) {
    Options options;
    options.leftTrigger = (flags & kFlagLeftTrigger) != 0;
    options.supportAims = (flags & kFlagSupportAims) != 0;
    options.gripShoulder = (flags & kFlagGripShoulder) != 0;
    options.leftTriggerShoulder = (flags & kFlagLeftTriggerShoulder) != 0;
    options.rightGripFree = (flags & kFlagRightGripFree) != 0;
    options.support = {distance, acquire, release};
    return options;
}

XINPUT_GAMEPAD FromControllers(vr::controllers::State const& state, Latches& latches) {
    return FromControllers(state, latches, Options{});
}

XINPUT_GAMEPAD FromControllers(vr::controllers::State const& state, Latches& latches, Options const& options) {
    auto const& l = state.left;
    auto const& r = state.right;
    XINPUT_GAMEPAD pad{};
    pad.sThumbLX = ToAxis(l.stickX);
    pad.sThumbLY = ToAxis(l.stickY);
    pad.sThumbRX = ToAxis(r.stickX);
    pad.sThumbRY = ToAxis(r.stickY);
    pad.bLeftTrigger = options.leftTrigger && !options.leftTriggerShoulder ? ToTrigger(l.trigger) : BYTE{0};
    pad.bRightTrigger = ToTrigger(r.trigger);

    WORD buttons = 0;
    bool const leftGrip = Latch(latches.leftGrip, l.squeeze);
    latches.left = UpdateSupportGrip(latches.support, leftGrip, options.support);
    if (latches.left == LeftGrip::Shoulder && options.gripShoulder) buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    bool const leftTriggerDown = Latch(latches.leftTrigger, l.trigger);
    if (options.leftTriggerShoulder && leftTriggerDown) buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (latches.left == LeftGrip::Support && options.supportAims) pad.bLeftTrigger = 255;
    // The right grip is RB, except a squeeze that began at a body holster (Options::rightGripFree):
    // that one stays off RB until the grip opens, wherever the hand goes meanwhile.
    bool const rightWasDown = latches.rightGrip;
    bool const rightGrip = Latch(latches.rightGrip, r.squeeze);
    if (rightGrip && !rightWasDown) {
        latches.rightFree = options.rightGripFree;
        if (latches.rightFree) {
            ++latches.rightFreeCount;
        }
    } else if (!rightGrip) {
        latches.rightFree = false;
    }
    if (rightGrip && !latches.rightFree) buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (r.primary) buttons |= XINPUT_GAMEPAD_A;
    if (r.secondary) buttons |= XINPUT_GAMEPAD_B;
    if (l.primary) buttons |= XINPUT_GAMEPAD_X;
    if (l.secondary) buttons |= XINPUT_GAMEPAD_Y;
    if (l.stickClick) buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (r.stickClick) buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    if (l.menu) buttons |= leftGrip ? XINPUT_GAMEPAD_BACK : XINPUT_GAMEPAD_START;
    pad.wButtons = buttons;
    return pad;
}

XINPUT_GAMEPAD Merge(XINPUT_GAMEPAD const& a, XINPUT_GAMEPAD const& b) {
    XINPUT_GAMEPAD out{};
    out.wButtons = a.wButtons | b.wButtons;
    out.bLeftTrigger = std::max(a.bLeftTrigger, b.bLeftTrigger);
    out.bRightTrigger = std::max(a.bRightTrigger, b.bRightTrigger);
    Stronger(a.sThumbLX, a.sThumbLY, b.sThumbLX, b.sThumbLY, out.sThumbLX, out.sThumbLY);
    Stronger(a.sThumbRX, a.sThumbRY, b.sThumbRX, b.sThumbRY, out.sThumbRX, out.sThumbRY);
    return out;
}

void RotateStick(SHORT& x, SHORT& y, int yawUnits) {
    if (yawUnits == 0 || (x == 0 && y == 0)) {
        return;
    }
    double const yaw = yawUnits * std::numbers::pi / 32768.0;
    double const c = std::cos(yaw);
    double const s = std::sin(yaw);
    double const rx = x * c + y * s;
    double const ry = -x * s + y * c;
    x = static_cast<SHORT>(std::clamp(std::lround(rx), -32768L, 32767L));
    y = static_cast<SHORT>(std::clamp(std::lround(ry), -32768L, 32767L));
}

} // namespace mevr::game::gamepad
