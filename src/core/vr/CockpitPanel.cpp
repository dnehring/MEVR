// SPDX-License-Identifier: MIT
#include "core/vr/CockpitPanel.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace mevr::vr::cockpit_panel {
namespace {

XrVector3f Sub(XrVector3f a, XrVector3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
XrVector3f Add(XrVector3f a, XrVector3f b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
XrVector3f Scale(XrVector3f a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
float Dot(XrVector3f a, XrVector3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
XrVector3f Cross(XrVector3f a, XrVector3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(XrVector3f a) {
    return std::sqrt(Dot(a, a));
}
// `v` without its part along the unit `axis`.
XrVector3f Square(XrVector3f v, XrVector3f axis) {
    return Sub(v, Scale(axis, Dot(v, axis)));
}
// `v` turned about the unit `axis` by `degrees` (the right-hand rule).
XrVector3f Turn(XrVector3f v, XrVector3f axis, float degrees) {
    float const a = degrees * 0.017453293f;
    float const c = std::cos(a);
    float const s = std::sin(a);
    return Add(Add(Scale(v, c), Scale(Cross(axis, v), s)), Scale(axis, Dot(axis, v) * (1.0f - c)));
}

}  // namespace

void Panel::Haptic(Output& out, int hand, float seconds, float amplitude) {
    if (hand < 0 || hand > 1 || out.hapticCount >= static_cast<int>(std::size(out.haptics))) {
        return;
    }
    out.haptics[out.hapticCount++] = {hand, seconds, amplitude};
}

void Panel::Reset() {
    *this = Panel();
}

Output Panel::Step(Input const& in, Settings const& s) {
    Output out;
    if (!in.active || !s.on) {
        Reset();
        return out;
    }
    Layout const& layout = in.layout;
    double const dt = std::clamp(in.deltaSeconds, 0.0, 0.1);

    // The button: each tip's depth along the press (+ in) from the cap's top, and its distance from the button's axis.
    float const travel = std::max(layout.travelM, 1e-4f);
    float depth[2]{};
    float radial[2]{};
    bool usable[2]{};
    for (int h = 0; h < 2; ++h) {
        Hand const& hand = in.hands[h];
        if (!hand.tracked || layout.travelM <= 0.0f) {
            continue;
        }
        XrVector3f const d = Sub(hand.tip, layout.buttonTop);
        depth[h] = Dot(d, layout.buttonIn);
        radial[h] = Length(Square(d, layout.buttonIn));
        usable[h] = true;
    }
    if (pressHand_ >= 0) {
        int const h = pressHand_;
        if (!usable[h] || radial[h] > s.buttonRadiusM * 1.5f || depth[h] > travel + s.behindM || depth[h] < -s.hoverM) {
            pressHand_ = -1;
        }
    }
    if (pressHand_ < 0) {
        for (int h = 0; h < 2; ++h) {
            if (usable[h] && front_[h] && depth[h] > 0.0f && radial[h] <= s.buttonRadiusM && h != holdHand_) {
                pressHand_ = h;
                break;
            }
        }
    }
    for (int h = 0; h < 2; ++h) {
        front_[h] = usable[h] && radial[h] <= s.buttonRadiusM && depth[h] <= 0.0f && depth[h] >= -s.hoverM;
    }
    bool const wasPressed = pressed_;
    float const pushed = pressHand_ >= 0 ? depth[pressHand_] : 0.0f;
    if (pressHand_ < 0) {
        pressed_ = false;
    } else if (!pressed_ && pushed >= s.pressShare * travel) {
        pressed_ = true;
    } else if (pressed_ && pushed < s.releaseShare * travel) {
        pressed_ = false;
    }
    out.pressHand = pressHand_;
    out.pressM = pressHand_ >= 0 ? std::clamp(pushed, 0.0f, travel) : 0.0f;
    if (pressed_ && !wasPressed) {
        Haptic(out, pressHand_, 0.03f, 0.6f);
        hold_ = 0.0;
        squares_ = 0;
        fired_ = false;
    }
    // The repair can't run: red instead of the progress. Not once it ran in this press (its own cooldown starts at once).
    Refusal const refusal = pressed_ && !fired_ ? in.refusal : Refusal::None;
    if (refusal != refused_) {
        refused_ = refusal;
        refusedFor_ = 0.0;
    } else if (refusal != Refusal::None) {
        refusedFor_ += dt;
    }
    if (refusal != Refusal::None) {
        double const period = std::max(static_cast<double>(s.refusalFlashS), 0.1);
        out.refused = refusal;
        out.refusedLit = refusal == Refusal::NoGel || std::fmod(refusedFor_, period) < period / 2.0;
    }
    if (!pressed_ || refusal != Refusal::None) {
        hold_ = 0.0;
        squares_ = 0;
        if (!pressed_) {
            fired_ = false;
        }
    } else {
        hold_ += dt;
        float const progress = std::min(1.0f, static_cast<float>(hold_ / std::max(s.holdS, 0.05f)));
        int const squares = ProgressLamps(progress, kProgressLamps);
        if (squares > squares_) {
            squares_ = squares;
            Haptic(out, pressHand_, 0.02f, 0.25f);
        }
        if (progress >= 1.0f && !fired_) {
            fired_ = true;
            out.repair = true;
            Haptic(out, pressHand_, 0.15f, 0.9f);
        }
        out.progress = progress;
    }
    out.pressed = pressed_;

    // The lever: each hand's grip latch, and where the handle's grip is now.
    float const pull = std::max(layout.pullDeg, 1.0f);
    XrVector3f const gripNow = Add(layout.hinge, Turn(Sub(layout.grip, layout.hinge), layout.hingeAxis, pull_));
    bool squeezed[2]{};
    bool inReach[2]{};
    for (int h = 0; h < 2; ++h) {
        Hand const& hand = in.hands[h];
        bool const was = down_[h];
        down_[h] = hand.tracked && hand.squeeze >= (was ? kGripOff : kGripOn);
        squeezed[h] = down_[h] && !was;
        inReach[h] = hand.tracked && Length(Sub(hand.grip, gripNow)) <= s.grabRadiusM;
    }
    if (holdHand_ >= 0 && !down_[holdHand_]) {
        holdHand_ = -1;
    }
    if (holdHand_ < 0) {
        for (int h = 0; h < 2; ++h) {
            if (squeezed[h] && inReach[h] && h != pressHand_) {
                holdHand_ = h;
                grabHand_ = in.hands[h].grip;
                grabShare_ = pull_ / pull;
                Haptic(out, h, 0.03f, 0.5f);
                break;
            }
        }
    }
    if (holdHand_ >= 0) {
        // The hand's travel along the pull since the grab, as a share of the whole swing (a pull down and toward the pilot counts
        // by its downward part; up or away, nothing).
        float const pulled = Dot(Sub(in.hands[holdHand_].grip, grabHand_), layout.pullDir);
        float const share = grabShare_ + pulled / std::max(s.pullTravelM, 0.005f);
        pull_ = std::clamp(share, 0.0f, 1.0f) * pull;
    } else {
        pull_ = std::max(0.0f, pull_ - s.returnDegPerS * static_cast<float>(dt));
    }
    bool const atEnd = pull_ >= s.pullShare * pull;
    if (holdHand_ >= 0 && atEnd) {
        atEnd_ += dt;
        if (!endTicked_) {
            endTicked_ = true;
            Haptic(out, holdHand_, 0.04f, 0.6f);
        }
    } else {
        atEnd_ = 0.0;
        if (!atEnd) {
            endTicked_ = false;
        }
    }
    if (armed_ && holdHand_ >= 0 && atEnd_ >= s.pullHoldS) {
        armed_ = false;
        out.extract = true;
        Haptic(out, holdHand_, 0.2f, 1.0f);
    }
    if (!armed_ && pull_ < 0.5f * pull) {
        armed_ = true;
    }
    out.pullDeg = pull_;
    out.held = holdHand_ >= 0;
    out.holdHand = holdHand_;
    for (int h = 0; h < 2; ++h) {
        out.inReach[h] = inReach[h] || holdHand_ == h;
    }
    return out;
}

void ThrottleClick::Reset() {
    *this = ThrottleClick();
}

ClickOutput ThrottleClick::Step(ClickInput const& in, Settings const& s) {
    ClickOutput out;
    if (!s.on || !s.throttleClick) {
        Reset();
        return out;
    }
    double const dt = std::clamp(in.deltaSeconds, 0.0, 0.1);
    auto const buzz = [&out](float seconds, float amplitude) {
        if (out.hapticCount < static_cast<int>(std::size(out.haptics))) {
            out.haptics[out.hapticCount++] = {0, seconds, amplitude};
        }
    };
    if (secondBuzzIn_ >= 0.0) {
        secondBuzzIn_ -= dt;
        if (secondBuzzIn_ < 0.0) {
            buzz(0.06f, 0.5f);
        }
    }
    bool const wasDown = down_;
    down_ = in.click;
    if (down_ && !wasDown) {
        onLever_ = in.leverHeld;
        if (onLever_ && in.refusal == Refusal::None) {
            out.repair = true;
            refused_ = Refusal::None;
            buzz(0.15f, 0.9f);
        } else if (onLever_) {
            refused_ = in.refusal;
            refusedFor_ = 0.0;
            buzz(0.06f, 0.5f);
            secondBuzzIn_ = 0.15;
        }
    } else if (!down_) {
        onLever_ = false;
    }
    if (refused_ != Refusal::None) {
        if (refusedFor_ >= s.clickRefusalS && !(down_ && onLever_)) {
            refused_ = Refusal::None;
        } else {
            double const period = std::max(static_cast<double>(s.refusalFlashS), 0.1);
            out.refused = refused_;
            out.refusedLit = refused_ == Refusal::NoGel || std::fmod(refusedFor_, period) < period / 2.0;
            refusedFor_ += dt;
        }
    }
    return out;
}

XrVector3f SwitchTip(SwitchLayout const& layout, float share) {
    return Add(layout.pivot, Turn(Sub(layout.tipRest, layout.pivot), layout.axis, layout.onDeg * std::clamp(share, 0.0f, 1.0f)));
}

void Toggle::Reset() {
    *this = Toggle();
}

SwitchOutput Toggle::Step(SwitchInput const& in, Settings const& s) {
    SwitchOutput out;
    out.on = in.on;
    double const dt = std::clamp(in.deltaSeconds, 0.0, 0.1);
    float const target = in.on ? 1.0f : 0.0f;
    if (shown_ < 0.0f) {
        shown_ = target;
    }
    SwitchLayout const& layout = in.layout;
    if (in.active && s.on) {
        // The throw's way at the lever's tip as shown: where a turn by + moves it.
        XrVector3f const arm = Sub(SwitchTip(layout, shown_), layout.pivot);
        XrVector3f way = Cross(layout.axis, arm);
        float const wayLength = Length(way);
        float const armLength = Length(arm);
        if (wayLength > 1e-6f && armLength > 1e-6f) {
            way = Scale(way, 1.0f / wayLength);
            for (int h = 0; h < 2; ++h) {
                Hand const& hand = in.hands[h];
                if (!hand.tracked) {
                    touching_[h] = spent_[h] = false;
                    continue;
                }
                // The tip's distance from the lever (the segment from its foot to its tip).
                XrVector3f const d = Sub(hand.tip, layout.pivot);
                float const along = std::clamp(Dot(d, arm) / (armLength * armLength), 0.0f, 1.0f);
                float const distance = Length(Sub(d, Scale(arm, along)));
                float const across = Dot(d, way);
                bool const reach = distance <= s.switchRadiusM * (touching_[h] ? 1.6f : 1.0f);
                if (reach && !touching_[h]) {
                    touching_[h] = true;
                    spent_[h] = false;
                    entered_[h] = across;
                    if (out.touchHand < 0) {
                        out.touchHand = h;
                    }
                } else if (!reach) {
                    touching_[h] = spent_[h] = false;
                }
                if (!touching_[h]) {
                    continue;
                }
                out.touchHand = out.touchHand < 0 ? h : out.touchHand;
                if (spent_[h]) {
                    continue;
                }
                float const pushed = across - entered_[h];
                if ((!out.on && pushed >= s.switchThrowM) || (out.on && pushed <= -s.switchThrowM)) {
                    out.on = !out.on;
                    out.flipped = true;
                    spent_[h] = true;
                    if (out.hapticCount < static_cast<int>(std::size(out.haptics))) {
                        out.haptics[out.hapticCount++] = {h, 0.025f, 0.9f};
                    }
                }
            }
        }
    } else {
        touching_[0] = touching_[1] = spent_[0] = spent_[1] = false;
    }
    float const goal = out.on ? 1.0f : 0.0f;
    float const step = s.switchFlipS > 0.0f ? static_cast<float>(dt) / s.switchFlipS : 1.0f;
    shown_ = shown_ < goal ? std::min(goal, shown_ + step) : std::max(goal, shown_ - step);
    out.throw_ = shown_;
    return out;
}

int HealthLamps(float health, int count) {
    if (!(health > 0.0f) || count <= 0) {
        return 0;
    }
    return std::clamp(static_cast<int>(std::ceil(health * static_cast<float>(count) - 1e-4f)), 1, count);
}

Colour HealthColour(float health) {
    return health > 0.6f ? Colour::Green : health > 0.3f ? Colour::Amber : Colour::Red;
}

int ProgressLamps(float progress, int count) {
    return std::clamp(static_cast<int>(std::floor(progress * static_cast<float>(count) + 1e-4f)), 0, count);
}

char const* Name(Colour colour) {
    switch (colour) {
    case Colour::Off: return "Off";
    case Colour::Green: return "Green";
    case Colour::Amber: return "Amber";
    case Colour::Red: return "Red";
    case Colour::White: return "White";
    case Colour::Blue: return "Blue";
    }
    return "Off";
}

char const* Name(Refusal refusal) {
    switch (refusal) {
    case Refusal::None: return "none";
    case Refusal::NoGel: return "no omni-gel";
    case Refusal::Cooling: return "cooling down";
    }
    return "none";
}

}  // namespace mevr::vr::cockpit_panel
