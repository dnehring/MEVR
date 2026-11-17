// SPDX-License-Identifier: MIT
#include "core/vr/FlareWindow.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::flare_window {

std::optional<Mat4> WindowInverse(Mat4 const& translatedView, Mat4 const& projection) {
    auto const inverse = screen_matrix::Inverse(screen_matrix::Multiply(translatedView, projection));
    if (!inverse) {
        return std::nullopt;
    }
    for (float const v : *inverse) {
        if (!std::isfinite(v)) {
            return std::nullopt;
        }
    }
    return inverse;
}

bool Differs(Mat4 const& a, Mat4 const& b) {
    for (int i = 0; i < 16; ++i) {
        float const scale = std::max(1e-4f, 1e-4f * std::max(std::fabs(a[i]), std::fabs(b[i])));
        if (!(std::fabs(a[i] - b[i]) <= scale)) {
            return true;
        }
    }
    return false;
}

std::optional<std::array<float, 3>> Unproject(Mat4 const& inverse, float x, float y, float z) {
    float out[4] = {};
    float const in[4] = {x, y, z, 1.0f};
    for (int c = 0; c < 4; ++c) {
        for (int k = 0; k < 4; ++k) {
            out[c] += in[k] * inverse[k * 4 + c];
        }
    }
    if (std::fabs(out[3]) < 1e-12f) {
        return std::nullopt;
    }
    return std::array<float, 3>{out[0] / out[3], out[1] / out[3], out[2] / out[3]};
}

} // namespace mevr::vr::flare_window
