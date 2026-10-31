// SPDX-License-Identifier: MIT
#include "core/vr/GamePointer.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::game_pointer {

Pointer::Pointer(Settings const& settings) : settings_(settings) {}

bool Pointer::Latch(int hand, float value) {
    bool& latched = latched_[hand];
    latched = latched ? value > settings_.triggerOff : value >= settings_.triggerOn;
    return latched;
}

Output Pointer::Release() {
    Output out;
    out.hand = hand_;
    out.left = wasHit_;
    out.pressed = false;
    out.pressEdge = pressing_;  // the host sends the button up at the last point it sent
    pressing_ = false;
    latched_[0] = latched_[1] = false;
    wasHit_ = false;
    reported_ = false;
    wheelAccum_ = 0.0f;
    return out;
}

Output Pointer::Step(Quad const& quad, Input const& input) {
    if (!input.active || !(quad.width > 0.0f) || !(quad.height > 0.0f) || input.width <= 0 || input.height <= 0) {
        return Release();
    }
    Output out;
    bool const wasHit = wasHit_;
    bool triggers[2];
    menu_pointer::Hit hits[2];
    bool on[2];
    for (int i = 0; i < 2; ++i) {
        HandInput const& hand = input.hands[i];
        triggers[i] = Latch(i, hand.trigger);
        on[i] = hand.aimValid &&
            menu_pointer::Intersect(quad, hand.aim.position, menu_pointer::AimDirection(hand.aim), hits[i]);
    }
    // Which hand points: keep the pointing hand while it's on the quad (and through a press); else the other
    // one if it is on the quad (or pulls its trigger there while ours doesn't).
    int const other = 1 - hand_;
    if (!pressing_ && on[other] && (!on[hand_] || (triggers[other] && !triggers[hand_]))) {
        hand_ = other;
    }
    bool const pointing = on[hand_];
    if (pointing) {
        float const u = std::clamp(hits[hand_].u, 0.0f, 1.0f);
        float const v = std::clamp(hits[hand_].v, 0.0f, 1.0f);
        bool const arrived = !wasHit;  // no smoothing from a stale point
        float const k = std::clamp(settings_.smoothing, 0.0f, 1.0f);
        smoothU_ = arrived ? u : smoothU_ + (u - smoothU_) * k;
        smoothV_ = arrived ? v : smoothV_ + (v - smoothV_) * k;
        out.distance = hits[hand_].distance;
    }
    // A press starts only on the quad; once started it lasts until the trigger lets go, even if the ray slides
    // off (dragging a slider or a map past the edge), so the button up always follows the button down.
    bool const wasPressing = pressing_;
    pressing_ = wasPressing ? triggers[hand_] : (pointing && triggers[hand_]);
    out.hand = hand_;
    out.onQuad = pointing;
    out.hit = pointing || pressing_;
    out.pressed = pressing_;
    out.pressEdge = pressing_ != wasPressing;
    out.left = !out.hit && wasHit;
    wasHit_ = out.hit;
    if (!out.hit) {
        // Off the quad: the last point stays (a button up that ends here goes there), nothing moves.
        ToPixel(smoothU_, smoothV_, input.width, input.height, out.x, out.y);
        out.u = smoothU_;
        out.v = smoothV_;
        out.point = PointOnQuad(quad, smoothU_, smoothV_);
        reported_ = false;
        wheelAccum_ = 0.0f;
        return out;
    }
    out.u = smoothU_;
    out.v = smoothV_;
    out.point = PointOnQuad(quad, smoothU_, smoothV_);
    ToPixel(smoothU_, smoothV_, input.width, input.height, out.x, out.y);
    float const px = static_cast<float>(out.x);
    float const py = static_cast<float>(out.y);
    if (!reported_ || std::fabs(px - reportedX_) > settings_.deadBandPx || std::fabs(py - reportedY_) > settings_.deadBandPx) {
        out.moved = true;
        reported_ = true;
        reportedX_ = px;
        reportedY_ = py;
    } else {
        // Within the dead band: report the point already sent, so the host's button messages carry it.
        out.x = static_cast<int>(reportedX_);
        out.y = static_cast<int>(reportedY_);
    }
    // The wheel: whole notches as they accumulate.
    float const stick = input.rightStickY;
    if (std::fabs(stick) > settings_.stickDeadZone && input.dt > 0.0f) {
        wheelAccum_ += stick * settings_.wheelPerSecond * std::min(input.dt, 0.1f);
    } else if (std::fabs(stick) <= settings_.stickDeadZone) {
        wheelAccum_ = 0.0f;  // let go: nothing half-turned carries over
    }
    float whole = 0.0f;
    wheelAccum_ = std::modf(wheelAccum_, &whole);
    out.wheelNotches = static_cast<int>(whole);
    return out;
}

void ToPixel(float u, float v, int width, int height, int& x, int& y) {
    int const w = std::max(width, 1);
    int const h = std::max(height, 1);
    x = std::clamp(static_cast<int>(std::floor(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(w))), 0, w - 1);
    y = std::clamp(static_cast<int>(std::floor(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(h))), 0, h - 1);
}

XrVector3f PointOnQuad(Quad const& quad, float u, float v) {
    XrVector3f const local{(u - 0.5f) * quad.width, (0.5f - v) * quad.height, 0.0f};
    XrVector3f const offset = overlay::Rotate(quad.pose.orientation, local);
    return {quad.pose.position.x + offset.x, quad.pose.position.y + offset.y, quad.pose.position.z + offset.z};
}

float DotSize(XrVector3f const& eye, XrVector3f const& point, float angularSize) {
    float const dx = point.x - eye.x, dy = point.y - eye.y, dz = point.z - eye.z;
    float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    return 2.0f * distance * std::tan(std::max(angularSize, 0.0f) * 0.5f);
}

} // namespace mevr::vr::game_pointer
