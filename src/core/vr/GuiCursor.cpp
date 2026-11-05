// SPDX-License-Identifier: MIT
#include "core/vr/GuiCursor.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::gui_cursor {

Steer::Steer(Settings const& settings) : settings_(settings), gain_{settings.gain, settings.gain} {}

void Steer::Reset() {
    waiting_ = false;
    waited_ = 0;
    onTarget_ = false;
}

Move Steer::Step(float mouseX, float mouseY, std::optional<float> targetX, std::optional<float> targetY) {
    Move move;
    float const mouse[2] = {mouseX, mouseY};
    if (waiting_) {
        float const moved[2] = {mouse[0] - from_[0], mouse[1] - from_[1]};
        bool const shown = std::fabs(moved[0]) > 0.05f || std::fabs(moved[1]) > 0.05f;
        if (!shown && ++waited_ < settings_.waitTicks) {
            return move;  // the move hasn't reached the movie yet
        }
        waiting_ = false;
        if (shown) {
            // What the move did: the gain per axis, from moves big enough to measure that went the way they were sent.
            for (int axis = 0; axis < 2; ++axis) {
                if (std::abs(sent_[axis]) >= 4 && moved[axis] * static_cast<float>(sent_[axis]) > 0.0f) {
                    float const measured = moved[axis] / static_cast<float>(sent_[axis]);
                    float const learned = gain_[axis] + (measured - gain_[axis]) * std::clamp(settings_.learn, 0.0f, 1.0f);
                    gain_[axis] = std::clamp(learned, settings_.minGain, settings_.maxGain);
                }
            }
        }
    }
    if (!targetX || !targetY) {
        onTarget_ = false;
        return move;
    }
    float const target[2] = {*targetX, *targetY};
    int counts[2] = {0, 0};
    onTarget_ = true;
    for (int axis = 0; axis < 2; ++axis) {
        float const gap = target[axis] - mouse[axis];
        if (std::fabs(gap) <= settings_.onTarget) {
            continue;
        }
        onTarget_ = false;
        float const wanted = gap * std::clamp(settings_.approach, 0.1f, 1.0f) / gain_[axis];
        int c = static_cast<int>(std::lround(wanted));
        if (c == 0) {
            c = gap > 0.0f ? 1 : -1;  // under a count: one count, so it still gets there
        }
        counts[axis] = std::clamp(c, -settings_.maxCounts, settings_.maxCounts);
    }
    if (counts[0] != 0 || counts[1] != 0) {
        move.dx = counts[0];
        move.dy = counts[1];
        waiting_ = true;
        waited_ = 0;
        sent_[0] = counts[0];
        sent_[1] = counts[1];
        from_[0] = mouse[0];
        from_[1] = mouse[1];
    }
    return move;
}

} // namespace mevr::vr::gui_cursor
