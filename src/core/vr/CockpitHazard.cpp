// SPDX-License-Identifier: MIT
#include "core/vr/CockpitHazard.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace mevr::vr::cockpit_hazard {

bool Alarm(bool was, std::optional<float> health, bool vehicle, Settings const& settings) {
    if (!settings.on || !vehicle) {
        return false;
    }
    if (!health) {
        return was;  // a missed read keeps what was
    }
    float const h = *health;
    return was ? h < settings.clearAbove : h < settings.below;
}

float Flash(double seconds, float periodS) {
    double const period = std::max(static_cast<double>(periodS), 0.05);
    double const phase = std::fmod(std::max(seconds, 0.0), period) / period;
    double const c = 0.5 + 0.5 * std::cos(2.0 * std::numbers::pi * phase);
    return static_cast<float>(c * c * c);
}

Levels LevelsAt(float flash) {
    float const f = std::clamp(flash, 0.0f, 1.0f);
    return {kLampLow + (kLampHigh - kLampLow) * f, kWashLow + (kWashHigh - kWashLow) * f};
}

Settings Sanitized(Settings s) {
    s.below = std::clamp(s.below, 0.0f, 1.0f);
    s.clearAbove = std::clamp(std::max(s.clearAbove, s.below + 0.02f), 0.0f, 1.01f);
    s.periodS = std::clamp(s.periodS, 0.2f, 5.0f);
    return s;
}

std::string Describe(Settings const& s) {
    if (!s.on) {
        return "hazard lights off ([Cockpit] hazard = 0)";
    }
    return std::format("hazard lights under {:.0f}% hull (off again over {:.0f}%), a flash every {:.2f} s, {}", s.below * 100.0f,
        s.clearAbove * 100.0f, s.periodS, s.wash ? "the whole cabin red" : "only the lamps red");
}

} // namespace mevr::vr::cockpit_hazard
