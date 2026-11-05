// SPDX-License-Identifier: MIT
#include "core/vr/MakoGunner.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace mevr::vr::mako_gunner {

char const* Name(Control control) {
    switch (control) {
    case Control::Gunner: return "the gunner";
    case Control::Stick: return "the stick";
    case Control::Hand: return "the hand";
    }
    return "?";
}

bool HandAims(Hands const& hands) {
    return hands.handFree && hands.handIntent && hands.handPointsOut;
}

bool StickAims(Hands const& hands, Settings const& settings) {
    return hands.stickHeld && std::hypot(hands.thumbX, hands.thumbY) > settings.stickDeadZone;
}

Control Step(ControlState& state, Hands const& hands, double now, Settings const& settings) {
    if (!settings.on) {
        // No gunner: the old rules, the stick's aim while it's held, else the hand's.
        state.control = hands.stickHeld ? Control::Stick : Control::Hand;
        state.lastManual = now;
        return state.control;
    }
    if (hands.busy) {
        state.control = Control::Gunner;  // the hand on the panel isn't aiming, and its grip or trigger isn't a takeover
        return state.control;
    }
    if (StickAims(hands, settings)) {
        state.control = Control::Stick;
        state.lastManual = now;
    } else if (HandAims(hands)) {
        state.control = Control::Hand;
        state.lastManual = now;
    } else if (state.control != Control::Gunner && now - state.lastManual >= settings.handbackS) {
        state.control = Control::Gunner;
    }
    return state.control;
}

bool IsBig(Candidate const& candidate, Settings const& settings) {
    return candidate.radiusM >= settings.bigRadiusM;
}

bool IsCrowd(Candidate const& candidate, Settings const& settings) {
    return candidate.neighbours + 1 >= settings.crowdCount;
}

bool UseCannon(Candidate const& candidate, Settings const& settings) {
    return settings.cannon && (IsBig(candidate, settings) || IsCrowd(candidate, settings));
}

void CountNeighbours(std::span<Candidate> candidates, std::span<double const> xyz, double radiusM) {
    std::size_t const n = std::min(candidates.size(), xyz.size() / 3);
    double const r2 = radiusM * radiusM;
    for (std::size_t i = 0; i < n; ++i) {
        int count = 0;
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }
            double const dx = xyz[3 * i] - xyz[3 * j];
            double const dy = xyz[3 * i + 1] - xyz[3 * j + 1];
            double const dz = xyz[3 * i + 2] - xyz[3 * j + 2];
            count += dx * dx + dy * dy + dz * dz <= r2 ? 1 : 0;
        }
        candidates[i].neighbours = count;
    }
}

int Choose(std::span<Candidate const> candidates, int current, Settings const& settings) {
    auto const valid = [&](Candidate const& c) { return c.visible && c.inCone && c.distanceM <= settings.rangeM; };
    Candidate const* nearest = nullptr;
    Candidate const* kept = nullptr;
    for (Candidate const& c : candidates) {
        if (!valid(c)) {
            continue;
        }
        if (c.id == current) {
            kept = &c;
        }
        if (nearest == nullptr || c.distanceM < nearest->distanceM) {
            nearest = &c;
        }
    }
    if (kept != nullptr && (nearest == nullptr || nearest->distanceM >= kept->distanceM * settings.stickyFactor)) {
        return kept->id;
    }
    return nearest != nullptr ? nearest->id : -1;
}

Orders Fire(FireState& state, double now, int target, bool cannon, Settings const& settings) {
    Orders orders;
    if (target != state.target) {
        state.target = target;
        state.since = now;
        state.mg = false;
        state.nextBurst = now;  // a new target: the first burst once settled
    }
    if (target < 0 || now - state.since < settings.settleS) {
        state.mg = false;
        return orders;
    }
    if (!state.mg && now >= state.nextBurst) {
        state.mg = true;
        state.burstUntil = now + settings.burstS;
    } else if (state.mg && now >= state.burstUntil) {
        state.mg = false;
        state.nextBurst = now + settings.pauseS;
    }
    orders.mg = state.mg;
    if (cannon && settings.cannon && now >= state.nextCannon) {
        orders.cannon = true;
        state.nextCannon = now + settings.cannonEveryS;
    }
    return orders;
}

Settings Sanitized(Settings s) {
    s.rangeM = std::clamp(s.rangeM, 10.0f, 400.0f);
    s.handbackS = std::clamp(s.handbackS, 0.5f, 30.0f);
    s.settleS = std::clamp(s.settleS, 0.0f, 2.0f);
    s.burstS = std::clamp(s.burstS, 0.1f, 10.0f);
    s.pauseS = std::clamp(s.pauseS, 0.0f, 10.0f);
    s.cannonEveryS = std::clamp(s.cannonEveryS, 0.5f, 30.0f);
    s.bigRadiusM = std::clamp(s.bigRadiusM, 0.1f, 20.0f);
    s.crowdCount = std::clamp(s.crowdCount, 2, 50);
    s.crowdRadiusM = std::clamp(s.crowdRadiusM, 0.5f, 50.0f);
    s.stickDeadZone = std::clamp(s.stickDeadZone, 0.05f, 0.9f);
    s.stickyFactor = std::clamp(s.stickyFactor, 0.1f, 1.0f);
    return s;
}

std::string Describe(Settings const& s) {
    if (!s.on) {
        return "no gunner: you aim the turret ([Mako] gunner = 0)";
    }
    return std::format("a gunner aims at hostiles within {:.0f} m and fires the machine gun{}; your thumbstick or your aiming right hand takes "
                       "the gun, the gunner's again {:.1f} s after",
        s.rangeM, s.cannon ? std::format(" and the cannon at targets {:.1f} m wide or {} together within {:.0f} m", s.bigRadiusM * 2.0f,
                                         s.crowdCount, s.crowdRadiusM) : std::string(), s.handbackS);
}

} // namespace mevr::vr::mako_gunner
