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

XINPUT_GAMEPAD FromControllers(vr::controllers::State const& state, Latches& latches) {
    auto const& l = state.left;
    auto const& r = state.right;
    XINPUT_GAMEPAD pad{};
    pad.sThumbLX = ToAxis(l.stickX);
    pad.sThumbLY = ToAxis(l.stickY);
    pad.sThumbRX = ToAxis(r.stickX);
    pad.sThumbRY = ToAxis(r.stickY);
    pad.bLeftTrigger = ToTrigger(l.trigger);
    pad.bRightTrigger = ToTrigger(r.trigger);

    WORD buttons = 0;
    bool const leftGrip = Latch(latches.leftGrip, l.squeeze);
    if (leftGrip) buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (Latch(latches.rightGrip, r.squeeze)) buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
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
