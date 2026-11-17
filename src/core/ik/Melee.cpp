// SPDX-License-Identifier: MIT
#include "core/ik/Melee.hpp"
#include <algorithm>
#include <cmath>

namespace mevr::ik::melee {
namespace {
bool Finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
}
bool HeavyChamber::InZone(throwing::SwingInput const& motion) {
    auto const forward = Vec3{motion.bodyForward.x, 0, motion.bodyForward.z};
    if (!Finite(motion.position) || !Finite(forward) || Length(forward) < 0.5) return false;
    auto const f = Normalize(forward);
    auto const right = Vec3{-f.z, 0, f.x};
    double const r = Dot(motion.position, right), a = Dot(motion.position, f);
    // Ahead of the shoulder holsters, to the right of a normal sight line. These are candidate
    // comfort bounds, not native reach/weapon collision admission or a class-specific pose.
    return r >= 0.18 && r <= 0.42 && motion.position.y >= -0.22 && motion.position.y <= 0.02 &&
        a >= 0.12 && a <= 0.34;
}
void HeavyChamber::Reset() { *this = {}; }
void HeavyChamber::Consume() {
    phase_ = Phase::Release;
    armedNow_ = false;
}
void HeavyChamber::Step(Hand const& hand) {
    armedNow_ = false;
    auto const& motion = hand.motion;
    if (!hand.available || !hand.neutral || !motion.tracked || motion.suppressed ||
        !std::isfinite(motion.now) || !Finite(motion.position) || !Finite(motion.bodyForward) ||
        std::hypot(motion.bodyForward.x, motion.bodyForward.z) < 0.5) { Reset(); return; }
    if (sampled_ && motion.now == lastTime_) return; // no elapsed time from repeated poses
    if (sampled_ && (motion.now < lastTime_ || motion.now - lastTime_ > throwing::MotionHistory::kMaxGapSeconds ||
        Length(motion.position - lastPosition_) > throwing::MotionHistory::kMaxStepMeters)) {
        Reset(); // a recenter, gap or backwards clock cannot carry armed intent
        return;
    }
    double const speed = sampled_ ? Length(motion.position - lastPosition_) / (motion.now - lastTime_) : 0;
    if (!std::isfinite(speed) || speed >= throwing::SwingParams{}.maxSpeed) { Reset(); return; }
    bool const still = sampled_ && speed <= 0.25;
    lastTime_ = motion.now;
    lastPosition_ = motion.position;
    sampled_ = true;
    bool const inside = InZone(motion);
    if (phase_ == Phase::Release) {
        if (!inside && still) phase_ = Phase::Idle;
        return;
    }
    if (phase_ == Phase::Armed) {
        if (motion.now >= deadline_) Consume(); // no late strike or automatic re-arm while held
        return;
    }
    if (!inside || !still) { phase_ = Phase::Idle; return; }
    if (phase_ != Phase::Holding || Length(motion.position - anchor_) > 0.035) {
        phase_ = Phase::Holding;
        began_ = motion.now;
        anchor_ = motion.position;
    }
    if (motion.now - began_ >= 0.35) {
        phase_ = Phase::Armed;
        deadline_ = motion.now + 1.25;
        armedNow_ = true;
    }
}
void Gestures::Reset() {
    for (auto& detector : detectors_) detector.Reset();
    ready_.fill(false);
    heavyReleased_.fill(false);
    readyAt_ = 0;
    chamber_.Reset();
}
void Gestures::ResetPeaks() {
    for (auto& detector : detectors_) detector.ResetPeak();
}
Strokes Gestures::StepAll(Input const& in, throwing::SwingParams const& params) {
    if (rules_ != in.rules) { Reset(); rules_ = in.rules; }
    return in.rules == Rules::Physical ? StepPhysical(in, params) : StepStaged(in, params);
}
std::optional<Request> Gestures::Step(Input const& in, throwing::SwingParams const& params) {
    auto const strokes = StepAll(in, params);
    for (auto const& request : strokes.requests)
        if (request) return request;
    return std::nullopt;
}
// ME1's loop as game/Throwables ran it (research/20): each hand's detector on every sample, its own suppression refusing a
// stroke (the detector keeps its history and spends the stroke), each strike a request; the shared wait is the caller's.
Strokes Gestures::StepPhysical(Input const& in, throwing::SwingParams const& params) {
    Strokes out;
    if (generation_ != in.generation || !in.allowed) {
        Reset();
        generation_ = in.generation;
        return out;
    }
    for (std::size_t h = 0; h < in.hands.size(); ++h) {
        auto const& hand = in.hands[h];
        out.hands[h] = detectors_[h].Update(hand.motion, hand.params.value_or(params));
        if (out.hands[h].strike) out.requests[h] = Request{Intent::Light, static_cast<int>(h), ++stroke_, generation_};
    }
    return out;
}
Strokes Gestures::StepStaged(Input const& source, throwing::SwingParams const& params) {
    Strokes out;
    auto in = source;
    if (chamberEnabled_ != in.chamberHeavy) { Reset(); chamberEnabled_ = in.chamberHeavy; }
    if (generation_ != in.generation || !in.allowed) {
        Reset();
        generation_ = in.generation;
        return out;
    }
    if (in.chamberHeavy) {
        chamber_.Step(in.hands[1]);
        auto const phase = chamber_.State();
        in.hands[1].heavy = phase == HeavyChamber::Phase::Armed;
        if (chamber_.ArmedNow()) {
            // The stroke starts AFTER the cue, never from motion accumulated before arming.
            detectors_[1].Reset();
            ready_[1] = heavyReleased_[1] = true;
        }
        if (phase == HeavyChamber::Phase::Release ||
            (phase != HeavyChamber::Phase::Armed && HeavyChamber::InZone(in.hands[1].motion)))
            in.hands[1].available = false;
    } else chamber_.Reset();
    std::optional<Request> request;
    for (std::size_t index = 0; index < in.hands.size(); ++index) {
        // A deliberately armed strike wins simultaneous light motion from the other hand.
        auto const h = in.chamberHeavy && in.hands[1].heavy ? 1 - index : index;
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
        auto strokeParams = params;
        if (in.chamberHeavy && hand.heavy) strokeParams.forwardMin = std::max(0.707, params.forwardMin);
        auto const swing = detectors_[h].Update(hand.motion, strokeParams);
        out.hands[h] = swing;
        if (swing.discontinuous) {
            detectors_[h].Reset();
            ready_[h] = heavyReleased_[h] = false;
            if (h == 1 && in.chamberHeavy) chamber_.Reset();
            continue;
        }
        if (swing.strike && hand.motion.now >= readyAt_ && !request && (!hand.heavy || heavyReleased_[h])) {
            request = Request{hand.heavy ? Intent::Heavy : Intent::Light, static_cast<int>(h), ++stroke_, generation_};
            out.requests[h] = request;
            readyAt_ = hand.motion.now + std::max(0.0, params.cooldown);
            if (hand.heavy) heavyReleased_[h] = false; // release/rearm for each heavy attack
            if (h == 1 && in.chamberHeavy && hand.heavy) chamber_.Consume();
        }
    }
    return out;
}
} // namespace mevr::ik::melee
