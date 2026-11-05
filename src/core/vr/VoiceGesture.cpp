// SPDX-License-Identifier: MIT
#include "core/vr/VoiceGesture.hpp"

namespace mevr::vr::voice_gesture {

Step Machine::Advance(Input const& in, Settings const& s) {
    Step step;
    if (!listening) {
        if (s.enabled && in.usable && in.newSqueeze && in.squeezed && in.handToEarM <= s.radioRadiusM) {
            listening = true;
            heldS = 0.0f;
            step.started = true;
        }
        return step;
    }
    heldS += in.dt;
    step.heldS = heldS;
    if (!in.usable || !s.enabled) {
        listening = false;
        step.cancelled = true;
        return step;
    }
    if (!in.squeezed || heldS >= s.maxHoldS) {
        listening = false;
        step.ended = true;
        step.blip = heldS < s.minHoldS;
    }
    return step;
}

void Machine::Reset() {
    listening = false;
    heldS = 0.0f;
}

}  // namespace mevr::vr::voice_gesture
