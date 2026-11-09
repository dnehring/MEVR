// SPDX-License-Identifier: MIT
// A dip from black between the world and a cinematic (core/vr/TransitionFade.hpp).

#include "core/vr/TransitionFade.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::transition_fade {

bool Fades(View from, View to) {
    auto const cinematic = [](View v) { return v == View::World || v == View::Screen || v == View::CinemaFlat; };
    return from != to && cinematic(from) && cinematic(to);
}

void Fade::Cut(double nowS, float fadeS) {
    if (fadeS > 0.0f && std::isfinite(fadeS) && std::isfinite(nowS)) {
        fading_ = true;
        startS_ = nowS;
        ++count_;
    }
}

float Fade::Update(View view, double nowS, float fadeS) {
    if (view != last_) {
        if (Fades(last_, view) && fadeS > 0.0f && std::isfinite(fadeS)) {
            fading_ = true;
            startS_ = nowS;
            ++count_;
        }
        last_ = view;
    }
    if (!fading_) {
        return 0.0f;
    }
    double const t = nowS - startS_;
    if (!(fadeS > 0.0f) || !std::isfinite(t) || t < 0.0) {
        fading_ = false;
        return 0.0f;
    }
    if (t < kHoldS) {
        return 1.0f;
    }
    double const x = (t - kHoldS) / static_cast<double>(fadeS);
    if (x >= 1.0) {
        fading_ = false;
        return 0.0f;
    }
    double const s = x * x * (3.0 - 2.0 * x);  // smoothstep: eases out of the black and into the view
    return static_cast<float>(1.0 - s);
}

} // namespace mevr::vr::transition_fade
