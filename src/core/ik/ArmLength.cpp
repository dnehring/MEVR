#include "core/ik/ArmLength.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::ik {

std::optional<ArmLengths> ArmLengthFromTPose(ArmLengthSample const& sample, double shoulderHalfWidth) {
    Vec3 const span = sample.right - sample.left;
    double const width = Length(span);
    if (!std::isfinite(width) || width <= 2.0 * shoulderHalfWidth + 0.2) {
        return std::nullopt;
    }
    Vec3 const axis = span * (1.0 / width);
    double const along = Dot(sample.head - sample.left, axis);
    // The head between the hands, with room for both shoulders.
    if (along < shoulderHalfWidth + 0.1 || along > width - shoulderHalfWidth - 0.1) {
        return std::nullopt;
    }
    Vec3 const center = sample.left + axis * along;
    ArmLengths out;
    out.left = Length(sample.left - (center - axis * shoulderHalfWidth));
    out.right = Length(sample.right - (center + axis * shoulderHalfWidth));
    out.mean = 0.5 * (out.left + out.right);
    if (!(out.mean >= 0.3 && out.mean <= 1.2)) {
        return std::nullopt;
    }
    return out;
}

double ReachWeight(double meters, double nearMeters, double fadeMeters) {
    if (!std::isfinite(nearMeters) || !(nearMeters > 0.0) || !std::isfinite(meters)) {
        return 1.0;
    }
    double const fade = std::isfinite(fadeMeters) && fadeMeters > 1e-6 ? fadeMeters : 1e-6;
    double const t = std::clamp((meters - nearMeters) / fade, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double ScaledReach(double full, double weight) {
    if (!std::isfinite(full) || !std::isfinite(weight)) {
        return 1.0;
    }
    return 1.0 + (full - 1.0) * std::clamp(weight, 0.0, 1.0);
}

} // namespace mevr::ik
