// SPDX-License-Identifier: MIT
#include "core/ik/Vent.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::ik::vent {
namespace {

bool Finite(OnGun const& p) {
    return std::isfinite(p.along) && std::isfinite(p.up) && std::isfinite(p.side);
}

float Scaled(double strength, double level) {
    if (!(strength > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(strength * 2.0 * level, 0.0, 1.0));
}

} // namespace

bool InZone(OnGun const& p, Zone const& zone, double margin) {
    if (!Finite(p) || !std::isfinite(margin)) {
        return false;
    }
    return p.along >= -zone.backMeters - margin && p.along <= zone.frontMeters + margin && p.up <= zone.aboveMeters + margin &&
        p.up >= -zone.belowMeters - margin && std::abs(p.side) <= zone.sideMeters + margin;
}

char const* Name(Event event) {
    switch (event) {
    case Event::None: return "none";
    case Event::Grabbed: return "grabbed";
    case Event::Vented: return "vented";
    case Event::Refused: return "refused";
    case Event::Slow: return "slow";
    case Event::Lapsed: return "lapsed";
    case Event::Lost: return "lost";
    }
    return "?";
}

char const* Refusal(double heat, double cooldownLeft, Params const& params) {
    if (cooldownLeft > 0.0) {
        return "cooldown";
    }
    if (!(heat >= 0.0)) {
        return "the heat is unknown";
    }
    if (heat < params.minHeat) {
        return "the gun is cool";
    }
    return nullptr;
}

double Machine::HandSpeed(double now) const {
    auto const v = history_.FitVelocity(now - 0.04, now);
    return v ? Length(*v) : 0.0;
}

void Machine::Reset() {
    history_.Reset();
    inZone_ = false;
    holding_ = false;
    lapsed_ = false;
    grip_ = {};
}

void Machine::ResetAll() {
    Reset();
    readyAt_ = 0.0;
    vents_ = 0;
}

Step Machine::Update(Input const& input, Zone const& zone, Params const& params) {
    Step step;
    bool const valid = input.onGunValid && Finite(input.onGun) && std::isfinite(input.now);
    if (!valid) {
        if (holding_) {
            step.event = Event::Lost;
            step.why = "the gun's frame went away (the weapon away, or arm IK off)";
        }
        step.left = inZone_;
        Reset();
        return step;
    }
    if (history_.Add(input.now, AsVec(input.onGun)) && holding_) {
        // A jump in the hand's place on the gun: no stroke spans it; the squeeze goes on as a hold.
        start_ = input.onGun;
    }
    bool const was = inZone_;
    inZone_ = mevr::ik::vent::InZone(input.onGun, zone, was ? 0.02 : 0.0);  // the free function, not this member
    step.entered = inZone_ && !was;
    step.left = was && !inZone_;

    if (!holding_) {
        if (lapsed_) {
            lapsed_ = input.squeeze > params.grip.off;  // until the grip opens
            return step;
        }
        if (!input.newSqueeze || !inZone_) {
            return step;
        }
        if (!input.canVent) {
            step.event = Event::Refused;
            step.why = "not now";
            return step;
        }
        if (char const* const why = Refusal(input.heat, CooldownLeft(input.now), params)) {
            step.event = Event::Refused;
            step.why = why;
            return step;
        }
        holding_ = true;
        heldSince_ = input.now;
        start_ = input.onGun;
        grip_ = {true, std::max(input.squeeze, params.grip.on)};
        step.event = Event::Grabbed;
        return step;
    }

    bool const down = throwing::UpdateGrip(grip_, input.squeeze, HandSpeed(input.now), params.grip);
    if (down) {
        if (input.now - heldSince_ > params.maxHoldSeconds) {
            holding_ = false;
            lapsed_ = true;
            step.event = Event::Lapsed;
            step.why = "held too long: a grip, not a rack";
        }
        return step;
    }
    holding_ = false;
    // The rack is what the hand did since it took the receiver: the window never reaches back before the squeeze (a
    // reach to the receiver, or a tracking jump just before it, isn't the stroke).
    double const window = std::clamp(input.now - heldSince_, 0.03, 0.12);
    step.release = throwing::EstimateRelease(history_, input.now, window);
    step.back = step.release.valid ? -step.release.velocity.x : 0.0;
    step.travel = start_.along - input.onGun.along;
    if (!step.release.valid || step.release.speed < params.minSpeed) {
        step.event = Event::Slow;
        step.why = "too slow";
    } else if (step.back < params.backFraction * step.release.speed) {
        step.event = Event::Slow;
        step.why = "not back along the gun";
    } else if (step.travel < params.minTravel) {
        step.event = Event::Slow;
        step.why = "too short a pull";
    } else {
        step.event = Event::Vented;
        readyAt_ = input.now + params.cooldownSeconds;
        ++vents_;
    }
    return step;
}

throwing::Buzz EdgeTick(double strength) {
    return {Scaled(strength, 0.3), 220.0f, 0.015f};
}

throwing::Buzz VentPulse(double strength) {
    return {Scaled(strength, 0.9), 110.0f, 0.09f};
}

} // namespace mevr::ik::vent
