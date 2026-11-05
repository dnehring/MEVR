// SPDX-License-Identifier: MIT
#include "core/vr/OmniPad.hpp"

#include "core/vr/GuiEvents.hpp"
#include "core/vr/OverlayPlacement.hpp"

#include <cmath>

namespace mevr::vr::omni_pad {

namespace ge = gui_events;

char const* Name(Arm arm) {
    switch (arm) {
    case Arm::Up: return "up";
    case Arm::Down: return "down";
    case Arm::Left: return "left";
    case Arm::Right: return "right";
    case Arm::None: break;
    }
    return "";
}

std::uint8_t PressEvent(Arm arm) {
    switch (arm) {
    case Arm::Up: return ge::kControlUp;
    case Arm::Down: return ge::kControlDown;
    case Arm::Left: return ge::kControlLeft;
    case Arm::Right: return ge::kControlRight;
    case Arm::None: break;
    }
    return 0;
}

std::uint8_t ReleaseEvent(Arm arm) {
    switch (arm) {
    case Arm::Up: return ge::kControlUpRelease;
    case Arm::Down: return ge::kControlDownRelease;
    case Arm::Left: return ge::kControlLeftRelease;
    case Arm::Right: return ge::kControlRightRelease;
    case Arm::None: break;
    }
    return 0;
}

std::vector<std::uint8_t> Transition(Arm from, Arm to) {
    std::vector<std::uint8_t> out;
    if (from == to) {
        return out;
    }
    if (from != Arm::None) {
        out.push_back(ReleaseEvent(from));
    }
    if (to != Arm::None) {
        out.push_back(PressEvent(to));
    }
    return out;
}

Arm ArmAt(float x, float y, float width, float height, float hubFraction) {
    if (!(width > 0.0f) || !(height > 0.0f) || !(x >= 0.0f) || !(y >= 0.0f) || x > width || y > height) {
        return Arm::None;
    }
    float const dx = (x - 0.5f * width) / width;   // -0.5 .. 0.5
    float const dy = (y - 0.5f * height) / height;
    if (std::hypot(dx, dy) < hubFraction) {
        return Arm::None;
    }
    if (std::fabs(dx) > std::fabs(dy)) {
        return dx < 0.0f ? Arm::Left : Arm::Right;
    }
    return dy < 0.0f ? Arm::Up : Arm::Down;
}

Pad Beside(world_ui::Panel const& screen, float sizeM, float gapM) {
    Pad pad;
    pad.sizeM = sizeM;
    XrVector3f const right = overlay::Rotate(screen.pose.orientation, {1.0f, 0.0f, 0.0f});
    float const shift = 0.5f * screen.widthM + gapM + 0.5f * sizeM;
    pad.pose.orientation = screen.pose.orientation;
    pad.pose.position = {screen.pose.position.x + right.x * shift, screen.pose.position.y + right.y * shift,
        screen.pose.position.z + right.z * shift};
    return pad;
}

} // namespace mevr::vr::omni_pad
