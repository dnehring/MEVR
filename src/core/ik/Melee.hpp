// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Throwing.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace mevr::ik::melee {
enum class Intent { Light, Heavy };
struct Hand {
    throwing::SwingInput motion;
    bool available = false; // no support grip, clip, power/menu, holster or firing ownership
    bool neutral = false;   // all competing hand controls released; required after interruption
    bool heavy = false;     // semantic modifier; speed alone never selects heavy
};
struct Input {
    std::uint64_t generation = 0;
    bool allowed = false; // fresh gameplay/focus/native eligibility, including authored seating
    std::array<Hand, 2> hands;
};
struct Request {
    Intent intent = Intent::Light;
    int hand = -1;
    std::uint64_t stroke = 0;
    std::uint64_t generation = 0;
};
// Reuses ME1's detector. This only produces intent: it never owns target selection or damage.
class Gestures {
public:
    std::optional<Request> Step(Input const&, throwing::SwingParams const& = {});
    void Reset();
private:
    std::array<throwing::SwingDetector, 2> detectors_;
    std::array<bool, 2> ready_{};
    std::array<bool, 2> heavyReleased_{};
    std::uint64_t generation_ = 0;
    std::uint64_t stroke_ = 0;
    double readyAt_ = 0;
};
} // namespace mevr::ik::melee
