// SPDX-License-Identifier: MIT
#include "core/ik/BodyScale.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::ik::body_scale {

bool Scale::Active() const {
    return std::isfinite(s) && s > 0.0 && std::abs(s - 1.0) > kOffWithin;
}

Vec3 Apply(Scale const& scale, Vec3 p) {
    return scale.Active() ? scale.pivot + (p - scale.pivot) * scale.s : p;
}

Vec3 Unscale(Scale const& scale, Vec3 p) {
    return scale.Active() ? scale.pivot + (p - scale.pivot) * (1.0 / scale.s) : p;
}

Vec3 UnscaleVector(Scale const& scale, Vec3 v) {
    return scale.Active() ? v * (1.0 / scale.s) : v;
}

double UnscaleLength(Scale const& scale, double length) {
    return scale.Active() ? length / scale.s : length;
}

Transform Apply(Scale const& scale, Transform t) {
    if (!scale.Active()) {
        return t;
    }
    t.translation = Apply(scale, t.translation);
    t.scale *= scale.s;
    return t;
}

std::optional<double> Factor(double viewEyeHeight, double shepardEyeHeight, double unitsPerMeter) {
    double const minimum = 0.5 * unitsPerMeter;
    if (!std::isfinite(viewEyeHeight) || !std::isfinite(shepardEyeHeight) || !(unitsPerMeter > 0.0) || viewEyeHeight < minimum ||
        shepardEyeHeight < minimum) {
        return std::nullopt;
    }
    return std::clamp(viewEyeHeight / shepardEyeHeight, kMin, kMax);
}

double Approach(double current, double target, double ratePerSecond, double dt) {
    double const step = std::max(0.0, ratePerSecond) * std::max(0.0, dt);
    return current + std::clamp(target - current, -step, step);
}

} // namespace mevr::ik::body_scale
