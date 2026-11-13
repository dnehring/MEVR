// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Calibration.hpp"
#include "core/vr/Controllers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace mevr::vr::manual_calibration {

// Separate from the measured Calibration.<key> section: a new guided run cannot erase these deltas.
inline std::string Section(std::string_view profile) { return "ManualCalibration." + std::string(profile); }

struct Field {
    char const* menu;
    char const* ini;
    float limit;
    bool angle;
};
inline constexpr std::array<Field, 13> kFields{{
    {"manual_height", "heightOffsetM", 0.5f, false},
    {"manual_left_out", "leftOutM", 0.15f, false},
    {"manual_left_up", "leftUpM", 0.15f, false},
    {"manual_left_forward", "leftForwardM", 0.15f, false},
    {"manual_left_pitch", "leftPitchDeg", 45.0f, true},
    {"manual_left_yaw", "leftYawDeg", 45.0f, true},
    {"manual_left_roll", "leftRollDeg", 45.0f, true},
    {"manual_right_out", "rightOutM", 0.15f, false},
    {"manual_right_up", "rightUpM", 0.15f, false},
    {"manual_right_forward", "rightForwardM", 0.15f, false},
    {"manual_right_pitch", "rightPitchDeg", 45.0f, true},
    {"manual_right_yaw", "rightYawDeg", 45.0f, true},
    {"manual_right_roll", "rightRollDeg", 45.0f, true},
}};

inline float Bounded(float value, float limit) {
    return std::isfinite(value) ? std::clamp(value, -limit, limit) : 0.0f;
}

struct Settings {
    std::array<float, kFields.size()> values{};
    float Height() const { return Bounded(values[0], kFields[0].limit); }
};

// A rigid correction in the raw GRIP frame. Positive position: outward/up/forward; positive angles:
// pitch down, yaw outward, roll the top outward. Left mirrors right. Aim and grip keep their relative
// transform, so calibrated grip-based aim, uncalibrated aim rays, hands and interaction points agree.
// Raw samples still feed the guided calibration and menu; apply exactly once before gameplay publication.
inline controllers::State Apply(controllers::State state, Settings const& settings) {
    namespace cal = calibration;
    for (int side : {0, 1}) {
        auto& hand = side == 0 ? state.left : state.right;
        if (!state.active || !hand.gripValid) continue;
        std::size_t const start = side == 0 ? 1 : 7;
        auto value = [&](std::size_t i) { return Bounded(settings.values[start + i], kFields[start + i].limit); };
        bool changed = false;
        for (std::size_t i = 0; i < 6; ++i) changed = changed || value(i) != 0.0f;
        if (!changed) continue; // exact identity for existing installations, including pose bits
        float const mirror = side == 0 ? -1.0f : 1.0f;
        XrPosef const raw = hand.grip;
        XrVector3f const shift = cal::Rotate(raw.orientation, {mirror * value(0), value(1), -value(2)});
        constexpr float halfRadians = 0.00872664626f;
        float const p = -value(3) * halfRadians;
        float const y = -mirror * value(4) * halfRadians;
        float const r = -mirror * value(5) * halfRadians;
        auto const local = cal::Multiply(cal::Multiply({0, std::sin(y), 0, std::cos(y)},
            {std::sin(p), 0, 0, std::cos(p)}), {0, 0, std::sin(r), std::cos(r)});
        hand.grip.position = {raw.position.x + shift.x, raw.position.y + shift.y, raw.position.z + shift.z};
        hand.grip.orientation = cal::Normalize(cal::Multiply(raw.orientation, local));
        if (hand.aimValid) {
            auto const delta = cal::Multiply(hand.grip.orientation, cal::Conjugate(cal::Normalize(raw.orientation)));
            auto const relative = cal::Rotate(delta, {hand.aim.position.x - raw.position.x,
                hand.aim.position.y - raw.position.y, hand.aim.position.z - raw.position.z});
            hand.aim.position = {hand.grip.position.x + relative.x, hand.grip.position.y + relative.y,
                hand.grip.position.z + relative.z};
            hand.aim.orientation = cal::Normalize(cal::Multiply(delta, hand.aim.orientation));
        }
    }
    return state;
}

} // namespace mevr::vr::manual_calibration
