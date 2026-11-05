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
    pressByTouch_ = false;
    lastTouch_ = false;
    latched_[0] = latched_[1] = false;
    touchArmed_[0] = touchArmed_[1] = false;
    touchLatched_[0] = touchLatched_[1] = false;
    wasHit_ = false;
    reported_ = false;
    wheelAccum_ = 0.0f;
    return out;
}

namespace {

struct Touch {
    bool near = false;    // in reach in front of (or just behind) the touchable part
    bool inArea = false;  // over the touchable part (with its margin), at any depth
    float u = 0.0f;
    float v = 0.0f;
    float d = 0.0f;       // in front of the surface, meters (the quad's +Z faces the viewer)
};

Touch TouchOf(Quad const& quad, HandInput const& hand, Input const& input, Settings const& s) {
    Touch t;
    if (!s.touch || !hand.tipValid) {
        return t;
    }
    XrQuaternionf const q = quad.pose.orientation;
    XrVector3f const rel{hand.tip.x - quad.pose.position.x, hand.tip.y - quad.pose.position.y, hand.tip.z - quad.pose.position.z};
    XrVector3f const local = overlay::Rotate(XrQuaternionf{-q.x, -q.y, -q.z, q.w}, rel);
    t.u = local.x / quad.width + 0.5f;
    t.v = 0.5f - local.y / quad.height;
    t.d = local.z;
    float const mu = s.touchMarginM / quad.width;
    float const mv = s.touchMarginM / quad.height;
    t.inArea = t.u >= input.touchU0 - mu && t.u <= input.touchU1 + mu && t.v >= input.touchV0 - mv && t.v <= input.touchV1 + mv;
    t.near = t.inArea && t.d < s.touchHoverM && t.d > -s.touchBehindM;
    return t;
}

} // namespace

Output Pointer::Step(Quad const& quad, Input const& input) {
    if (!input.active || !(quad.width > 0.0f) || !(quad.height > 0.0f) || input.width <= 0 || input.height <= 0) {
        return Release();
    }
    Output out;
    bool const wasHit = wasHit_;
    bool triggers[2];
    menu_pointer::Hit hits[2];
    bool on[2];
    Touch touches[2];
    for (int i = 0; i < 2; ++i) {
        HandInput const& hand = input.hands[i];
        triggers[i] = Latch(i, hand.trigger);
        on[i] = hand.aimValid &&
            menu_pointer::Intersect(quad, hand.aim.position, menu_pointer::AimDirection(hand.aim), hits[i]);
        // A fingertip's touch: armed in front of the surface, pressed at it, let go once pulled back. A press holds while the
        // tip stays at the surface or behind it (sliding off the edge too, as a drag), so the button up follows the down.
        Touch const& t = touches[i] = TouchOf(quad, hand, input, settings_);
        if (touchLatched_[i]) {
            touchLatched_[i] = hand.tipValid && t.d <= settings_.touchReleaseM && t.d > -settings_.touchBehindM;
        } else if (t.near && t.d > settings_.touchPressM) {
            touchArmed_[i] = true;
        } else if (t.near && touchArmed_[i]) {
            touchLatched_[i] = true;  // reached the surface from the front
            touchArmed_[i] = false;
        }
        if (!t.near) {
            touchArmed_[i] = false;
        }
    }
    // A fingertip near the quad takes over from the rays (the hand that presses, else the nearer one), unless a trigger's press
    // is open.
    int touchHand = -1;
    if (!(pressing_ && !pressByTouch_)) {
        for (int i = 0; i < 2; ++i) {
            if (touchLatched_[i]) {
                touchHand = i;
            }
        }
        if (touchHand < 0) {
            for (int i = 0; i < 2; ++i) {
                if (touches[i].near && (touchHand < 0 || touches[i].d < touches[touchHand].d)) {
                    touchHand = i;
                }
            }
        }
    }
    bool switched = false;
    bool pointing = false;
    float u = 0.0f;
    float v = 0.0f;
    float k = std::clamp(settings_.smoothing, 0.0f, 1.0f);
    if (touchHand >= 0) {
        switched = hand_ != touchHand || (wasHit && !lastTouch_);
        hand_ = touchHand;
        pointing = true;
        u = std::clamp(touches[hand_].u, 0.0f, 1.0f);
        v = std::clamp(touches[hand_].v, 0.0f, 1.0f);
        k = std::clamp(settings_.touchSmoothing, 0.0f, 1.0f);
        out.touch = true;
        out.touchDistance = touches[hand_].d;
    } else {
        // Which hand points: keep the pointing hand while it's on the quad (and through a press); else the other
        // one if it is on the quad (or pulls its trigger there while ours doesn't).
        int const other = 1 - hand_;
        if (!pressing_ && on[other] && (!on[hand_] || (triggers[other] && !triggers[hand_]))) {
            hand_ = other;
            switched = true;
        }
        switched = switched || (wasHit && lastTouch_);
        pointing = on[hand_];
        if (pointing) {
            u = std::clamp(hits[hand_].u, 0.0f, 1.0f);
            v = std::clamp(hits[hand_].v, 0.0f, 1.0f);
            out.distance = hits[hand_].distance;
        }
    }
    lastTouch_ = touchHand >= 0;
    if (pointing) {
        // No smoothing from a stale point, nor from the other hand's (a pull with the other hand switches to it and clicks at
        // once: it clicked between the two rays before, 2026-11-04), nor across a change between ray and fingertip.
        bool const arrived = !wasHit || switched;
        smoothU_ = arrived ? u : smoothU_ + (u - smoothU_) * k;
        smoothV_ = arrived ? v : smoothV_ + (v - smoothV_) * k;
    }
    // A press starts only on the quad; once started it lasts until the trigger lets go (or the fingertip pulls back), even if
    // the point slides off (dragging a slider or a map past the edge), so the button up always follows the button down.
    bool const wasPressing = pressing_;
    if (wasPressing) {
        pressing_ = pressByTouch_ ? touchLatched_[hand_] : triggers[hand_];
    } else if (touchHand >= 0) {
        pressing_ = touchLatched_[hand_];
        pressByTouch_ = pressing_;
    } else {
        pressing_ = pointing && triggers[hand_];
        pressByTouch_ = false;
    }
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
