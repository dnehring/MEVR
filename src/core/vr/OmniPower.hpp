// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace mevr::vr::omni_power {

inline constexpr float kRiseS = 0.60f;
inline constexpr float kFlickerS = 0.15f;
inline constexpr float kDim = 0.15f;

// Linear luminance, independent of frame rate. Two small strikes die away before the steady rise.
inline float Luminance(float seconds) {
    if (!std::isfinite(seconds)) return seconds > 0.0f ? 1.0f : kDim;
    float const t = std::clamp(seconds / kRiseS, 0.0f, 1.0f);
    float const base = kDim + (1.0f - kDim) * t * t;
    float const strike = seconds > 0.0f && seconds < kFlickerS
        ? 0.035f * std::sin(seconds * 45.0f) * std::sin(seconds * 45.0f) * (1.0f - seconds / kFlickerS)
        : 0.0f;
    return base + strike;
}

inline float Age(std::uint64_t onMs, std::uint64_t nowMs) {
    return nowMs >= onMs ? static_cast<float>(nowMs - onMs) / 1000.0f : 0.0f;
}

// Only explicit luminance controls: never lifetime, spawn rate, alpha, size or an arbitrary float parameter.
inline bool BrightnessParameter(std::string_view name) {
    std::string key;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != '_' && c != ' ') key += c;
    }
    return key == "brightness" || key == "intensity" || key == "glowintensity" || key == "emissiveintensity" ||
        key == "emissivemultiplier" || key == "glowbrightness";
}

} // namespace mevr::vr::omni_power
