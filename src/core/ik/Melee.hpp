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
    bool chamberHeavy = false; // opt-in right-hand hold then strike, with all controls released
    std::array<Hand, 2> hands;
};
struct Request {
    Intent intent = Intent::Light;
    int hand = -1;
    std::uint64_t stroke = 0;
    std::uint64_t generation = 0;
};
// Candidate physical binding: enter a small box in front of the right shoulder, remain still
// for 350 ms, then strike forward within 1.25 s. No button or grip is repurposed. A neutral,
// stationary sample OUTSIDE the box is required on startup and after cancellation/consumption.
// All coordinates are head-relative metres and body yaw, independent of standing eye height.
class HeavyChamber {
public:
    enum class Phase { Release, Idle, Holding, Armed };
    void Step(Hand const&);
    void Reset();
    void Consume();
    Phase State() const { return phase_; }
    bool ArmedNow() const { return armedNow_; }
    static bool InZone(throwing::SwingInput const&);
private:
    Phase phase_ = Phase::Release;
    bool sampled_ = false;
    bool armedNow_ = false;
    double lastTime_ = 0;
    double began_ = 0;
    double deadline_ = 0;
    Vec3 lastPosition_;
    Vec3 anchor_;
};
// Reuses ME1's detector. This only produces intent: it never owns target selection or damage.
class Gestures {
public:
    std::optional<Request> Step(Input const&, throwing::SwingParams const& = {});
    void Reset();
    bool HeavyArmedNow() const { return chamber_.ArmedNow(); }
    HeavyChamber::Phase HeavyState() const { return chamber_.State(); }
private:
    HeavyChamber chamber_;
    bool chamberEnabled_ = false;
    std::array<throwing::SwingDetector, 2> detectors_;
    std::array<bool, 2> ready_{};
    std::array<bool, 2> heavyReleased_{};
    std::uint64_t generation_ = 0;
    std::uint64_t stroke_ = 0;
    double readyAt_ = 0;
};
} // namespace mevr::ik::melee
