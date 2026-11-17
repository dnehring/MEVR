// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Throwing.hpp"
#include <array>
#include <cstdint>
#include <optional>

// Melee by gesture, the trilogy's one gesture core (docs/trilogy-parity-ledger.md G43, Forks 12): each hand's fast stroke
// relative to the head (ik::throwing::SwingDetector) becomes a request under one of two rule sets; game/Melee turns a
// request into the game's melee through each game's adapter. Pure; game thread in use.
namespace mevr::ik::melee {
enum class Intent { Light, Heavy };
// The rules the strokes follow.
//   Physical: ME1's since 2026-11-05 (research/20 "Gesture only"), ME2's since 2026-11-17: a suppressed hand's stroke is
//     refused and spent (its history kept), no hand waits for a neutral pose, either hand may strike in the same step, and
//     the cooldown both hands share is the dispatcher's (it starts when the game swings: throwing::MeleeWait). Exactly
//     ME1's two SwingDetectors, as game/Throwables ran them until 2026-11-17 (tests/MeleeTests.cpp compares them).
//   Staged: ME3's opt-in rules (research/le3-melee-audit.md; the owner's question 7, docs/trilogy-plan.md): a hand that is
//     unavailable or suppressed starts over and must be neutral again, one request a step, the shared cooldown starts at
//     the request, and the heavy chamber.
enum class Rules : std::uint8_t { Staged, Physical };
struct Hand {
    throwing::SwingInput motion;
    bool available = false; // Staged: no support grip, clip, power/menu, holster or firing ownership
    bool neutral = false;   // Staged: all competing hand controls released; required after interruption
    bool heavy = false;     // Staged: semantic modifier; speed alone never selects heavy
    // Physical: this hand's stroke rules (ME1's gun hand strikes at its own speed); nullopt: Step's.
    std::optional<throwing::SwingParams> params;
};
struct Input {
    std::uint64_t generation = 0;
    bool allowed = false; // fresh gameplay/focus/native eligibility, including authored seating
    bool chamberHeavy = false; // Staged: opt-in right-hand hold then strike, with all controls released
    Rules rules = Rules::Staged;
    std::array<Hand, 2> hands;
};
struct Request {
    Intent intent = Intent::Light;
    int hand = -1;
    std::uint64_t stroke = 0;
    std::uint64_t generation = 0;
};
// One step: each hand's stroke (a strike, a refusal with its reason, or nothing) and each striking hand's request (Staged:
// at most one in the step).
struct Strokes {
    std::array<throwing::SwingStep, 2> hands{};
    std::array<std::optional<Request>, 2> requests{};
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
    Strokes StepAll(Input const&, throwing::SwingParams const& = {});
    // StepAll's first request (Staged: its only one).
    std::optional<Request> Step(Input const&, throwing::SwingParams const& = {});
    void Reset();
    bool HeavyArmedNow() const { return chamber_.ArmedNow(); }
    HeavyChamber::Phase HeavyState() const { return chamber_.State(); }
    // Each hand's detector (diagnostics: its peak speed since ResetPeaks).
    throwing::SwingDetector const& Detector(int hand) const { return detectors_[hand == 0 ? 0 : 1]; }
    void ResetPeaks();
private:
    Strokes StepPhysical(Input const&, throwing::SwingParams const&);
    Strokes StepStaged(Input const&, throwing::SwingParams const&);
    Rules rules_ = Rules::Staged;
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
