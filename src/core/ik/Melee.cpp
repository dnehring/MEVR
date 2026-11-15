// SPDX-License-Identifier: MIT
#include "core/ik/Melee.hpp"
#include <algorithm>
#include <cmath>

namespace mevr::ik::melee {
void Gestures::Reset() {
    for (auto& detector : detectors_) detector.Reset();
    ready_.fill(false);
    heavyReleased_.fill(false);
    readyAt_ = 0;
}
std::optional<Request> Gestures::Step(Input const& in, throwing::SwingParams const& params) {
    if (generation_ != in.generation || !in.allowed) {
        Reset();
        generation_ = in.generation;
        return std::nullopt;
    }
    std::optional<Request> request;
    for (std::size_t h = 0; h < in.hands.size(); ++h) {
        auto const& hand = in.hands[h];
        if (!hand.available || !hand.motion.tracked || hand.motion.suppressed || !std::isfinite(hand.motion.now)) {
            detectors_[h].Reset();
            ready_[h] = heavyReleased_[h] = false;
            continue;
        }
        if (!ready_[h]) {
            // Prime an entirely new history only after release; never inherit a clip/menu swing.
            if (hand.neutral && !hand.heavy) ready_[h] = true;
            else continue;
        }
        if (!hand.heavy) heavyReleased_[h] = true;
        auto const swing = detectors_[h].Update(hand.motion, params);
        if (swing.discontinuous) {
            detectors_[h].Reset();
            ready_[h] = heavyReleased_[h] = false;
            continue;
        }
        if (swing.strike && hand.motion.now >= readyAt_ && !request && (!hand.heavy || heavyReleased_[h])) {
            request = Request{hand.heavy ? Intent::Heavy : Intent::Light, static_cast<int>(h), ++stroke_, generation_};
            readyAt_ = hand.motion.now + std::max(0.0, params.cooldown);
            if (hand.heavy) heavyReleased_[h] = false; // release/rearm for each heavy attack
        }
    }
    return request;
}
} // namespace mevr::ik::melee
