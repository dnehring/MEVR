// SPDX-License-Identifier: MIT
#include "core/vr/Sprint.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::sprint {

bool Step(State& s, Settings const& settings, bool held, bool moving, float dtIn) {
    float const dt = std::isfinite(dtIn) ? std::clamp(dtIn, 0.0f, 0.25f) : 0.0f;
    bool const pressed = held && !s.wasHeld;
    s.wasHeld = held;
    if (s.exhausted && pressed && s.stamina >= settings.restartFraction) {
        s.exhausted = false;
    }
    s.boosting = settings.enabled && held && moving && !s.exhausted && s.stamina > 0.0f;
    if (s.boosting) {
        s.sinceBoostS = 0.0f;
        s.stamina -= dt / std::max(settings.staminaS, 0.1f);
        if (s.stamina <= 0.0f) {
            s.stamina = 0.0f;
            s.exhausted = true;
            ++s.emptied;
        }
    } else {
        s.sinceBoostS += dt;
        if (s.sinceBoostS >= settings.refillDelayS) {
            s.stamina = std::min(1.0f, s.stamina + dt / std::max(settings.refillS, 0.1f));
        }
    }
    return s.boosting;
}

float SpeedFactor(float runSpeed, float sprintSpeed) {
    if (!std::isfinite(runSpeed) || !std::isfinite(sprintSpeed) || !(runSpeed > 1.0f) || !(sprintSpeed > 1.0f)) {
        return 1.0f;
    }
    return std::clamp(sprintSpeed / runSpeed, 1.0f, 2.0f);
}

} // namespace mevr::vr::sprint
