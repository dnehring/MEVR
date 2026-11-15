// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include "core/vr/PowerWheel.hpp"

namespace mevr::vr::power_wheel {
// Class-derived roles, not a fixed power-name roster. Native revive keeps LE1's
// raised-hand gesture; weapon buffs remain tied to the held gun.
inline SelfGesture NativeSelfGesture(bool ammo, bool revive, bool buff, bool weaponBuff = false) {
    return ammo || weaponBuff ? SelfGesture::Gun : revive ? SelfGesture::Raise : buff ? SelfGesture::Chest : SelfGesture::None;
}

inline bool NativeLocationInRange(double distance, double minimum, double maximum) {
    return std::isfinite(distance) && std::isfinite(minimum) && std::isfinite(maximum) &&
        minimum >= 0 && maximum >= minimum && maximum > 0 && distance >= minimum && distance <= maximum;
}
// Read-only presentation of the game's current values. Missing/invalid observations
// never turn into a ready power; the native CanUsePower override still decides casts.
struct NativePowerState {
    float rank = 0;
    bool enabled = false;
    float remaining = 0;
    float total = 0;
    bool grenade = false;
    std::optional<int> grenades;
};
struct NativePowerView {
    bool valid = false;
    bool learned = false;
    bool enabled = false;
    float cooldown = 1;
};
inline NativePowerView EvaluateNativePower(NativePowerState const& s) {
    if (!std::isfinite(s.rank) || !std::isfinite(s.remaining) || !std::isfinite(s.total) ||
        s.rank < 0 || s.total < 0 || (s.grenade && (!s.grenades || *s.grenades < 0))) return {};
    float const remaining = std::max(s.remaining, 0.0f);
    return {true, s.rank > 0, s.enabled && s.rank > 0 && (!s.grenade || *s.grenades > 0),
        s.total > 0 ? std::clamp(remaining / s.total, 0.0f, 1.0f) : (remaining > 0 ? 1.0f : 0.0f)};
}
}  // namespace mevr::vr::power_wheel
