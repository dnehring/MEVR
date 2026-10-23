// SPDX-License-Identifier: MIT
#include "core/ik/Throwing.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::ik::throwing {
namespace {

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// `strength` 0.5 is the designed level; 1 doubles it (amplitudes clamp at 1). As body_slots.
float Scaled(double strength, double level) {
    if (!(strength > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(strength * 2.0 * level, 0.0, 1.0));
}

} // namespace

// ---- SampleClock ---------------------------------------------------------------------------------

std::optional<double> SampleClock::Update(std::uint64_t sequence, double observedSeconds) {
    // Start over at the first sample, after a gap (a pause, a stall, a session restart) or a sequence
    // that went back.
    if (!have_ || sequence < sequence_ || observedSeconds - observed_ > 0.25 || sequence - sequence_ > 30) {
        have_ = true;
        sequence_ = sequence;
        observed_ = observedSeconds;
        time_ = observedSeconds;
        return time_;
    }
    if (sequence == sequence_) {
        return std::nullopt;
    }
    double const steps = static_cast<double>(sequence - sequence_);
    double const seen = observedSeconds - observed_;
    if (seen > 0.0) {
        period_ = std::clamp(period_ + (seen / steps - period_) * 0.05, 1.0 / 200.0, 1.0 / 30.0);
    }
    time_ += steps * period_;
    time_ += (observedSeconds - time_) * 0.02;  // never drift away from the observer's clock
    sequence_ = sequence;
    observed_ = observedSeconds;
    return time_;
}

// ---- MotionHistory -------------------------------------------------------------------------------

bool MotionHistory::Add(double t, Vec3 p) {
    if (!std::isfinite(t) || !Finite(p)) {
        Reset();
        return true;
    }
    bool broke = false;
    if (count_ > 0) {
        Sample const& newest = Newest();
        if (!(t > newest.t)) {
            return false;
        }
        if (t - newest.t > kMaxGapSeconds || Length(p - newest.p) > kMaxStepMeters) {
            Reset();
            broke = true;
        }
    }
    if (count_ == kCapacity) {
        start_ = (start_ + 1) % kCapacity;
        --count_;
    }
    samples_[(start_ + count_) % kCapacity] = {t, p};
    ++count_;
    return broke;
}

Sample const& MotionHistory::At(int i) const {
    return samples_[(start_ + std::clamp(i, 0, std::max(count_ - 1, 0))) % kCapacity];
}

std::optional<Vec3> MotionHistory::FitVelocity(double t0, double t1) const {
    int n = 0;
    double tSum = 0.0;
    Vec3 pSum;
    for (int i = 0; i < count_; ++i) {
        Sample const& s = At(i);
        if (s.t >= t0 && s.t <= t1) {
            ++n;
            tSum += s.t;
            pSum = pSum + s.p;
        }
    }
    if (n < 2) {
        return std::nullopt;
    }
    double const tMean = tSum / n;
    Vec3 const pMean = pSum * (1.0 / n);
    double tt = 0.0;
    Vec3 tp;
    for (int i = 0; i < count_; ++i) {
        Sample const& s = At(i);
        if (s.t >= t0 && s.t <= t1) {
            double const dt = s.t - tMean;
            tt += dt * dt;
            tp = tp + (s.p - pMean) * dt;
        }
    }
    if (!(tt > 1e-12)) {
        return std::nullopt;
    }
    return tp * (1.0 / tt);
}

Release EstimateRelease(MotionHistory const& history, double now, double window, double halfWidth, double smoothing) {
    Release release;
    if (history.Count() < 2) {
        return release;
    }
    double bestSpeed = -1.0;
    double bestT = now;
    for (int i = 0; i < history.Count(); ++i) {
        Sample const& s = history.At(i);
        if (s.t < now - window || s.t > now) {
            continue;
        }
        ++release.samples;
        auto const v = history.FitVelocity(s.t - halfWidth, s.t + halfWidth);
        if (v && Length(*v) > bestSpeed) {
            bestSpeed = Length(*v);
            bestT = s.t;
        }
    }
    std::optional<Vec3> v = bestSpeed >= 0.0 ? history.FitVelocity(bestT - smoothing, bestT + smoothing) : std::nullopt;
    if (!v) {
        v = history.FitVelocity(now - window, now);  // too few samples for the peak search
        bestT = now;
    }
    if (!v || !Finite(*v)) {
        return release;
    }
    release.valid = true;
    release.velocity = *v;
    release.speed = Length(*v);
    release.peakAgo = now - bestT;
    return release;
}

// ---- The grip --------------------------------------------------------------------------------------

bool UpdateGrip(GripLatch& latch, float squeeze, double handSpeed, GripParams const& params) {
    if (!latch.down) {
        if (squeeze >= params.on) {
            latch.down = true;
            latch.peak = squeeze;
        }
        return latch.down;
    }
    latch.peak = std::max(latch.peak, squeeze);
    bool const early = params.earlyDrop > 0.0f && handSpeed >= params.earlySpeed && squeeze <= latch.peak - params.earlyDrop;
    if (squeeze <= params.off || early) {
        latch.down = false;
        latch.peak = 0.0f;
    }
    return latch.down;
}

// ---- The grenade in a hand -------------------------------------------------------------------------

char const* Name(Event event) {
    switch (event) {
    case Event::None: return "none";
    case Event::Grabbed: return "grabbed";
    case Event::Refused: return "refused";
    case Event::Thrown: return "thrown";
    case Event::Cancelled: return "cancelled";
    case Event::Dropped: return "dropped";
    }
    return "?";
}

double GrenadeMachine::HandSpeed(int hand, double now) const {
    if (hand < 0 || hand > 1) {
        return 0.0;
    }
    auto const v = history_[hand].FitVelocity(now - 0.04, now);
    return v ? Length(*v) : 0.0;
}

void GrenadeMachine::Reset() {
    for (MotionHistory& h : history_) {
        h.Reset();
    }
    inSlot_ = {false, false};
    grip_ = {};
    holder_ = -1;
}

Step GrenadeMachine::Update(Input const& input) {
    Step step;
    for (int h = 0; h < 2; ++h) {
        HandInput const& hand = input.hands[h];
        if (hand.tracked) {
            history_[h].Add(input.now, hand.position);
        } else {
            history_[h].Reset();
        }
        bool const was = inSlot_[h];
        bool in = false;
        if (hand.tracked && std::isfinite(hand.distance) && hand.distance >= 0.0 && input.radius > 0.0) {
            in = was ? hand.distance <= input.radius + input.exitMargin : hand.distance <= input.radius;
        }
        inSlot_[h] = in;
        step.entered[h] = in && !was ? 1 : 0;
        step.left[h] = was && !in ? 1 : 0;
    }

    if (holder_ < 0) {
        for (int h = 0; h < 2; ++h) {
            HandInput const& hand = input.hands[h];
            if (!hand.newSqueeze || !inSlot_[h] || !hand.allowed || !hand.tracked) {
                continue;
            }
            step.hand = h;
            if (!input.canGrab) {
                step.event = Event::Refused;
                break;
            }
            holder_ = h;
            grip_[h] = {true, std::max(hand.squeeze, input.grip.on)};
            step.event = Event::Grabbed;
            break;
        }
        return step;
    }

    int const h = holder_;
    HandInput const& hand = input.hands[h];
    step.hand = h;
    if (!hand.tracked) {
        holder_ = -1;
        grip_[h] = {};
        step.event = Event::Dropped;
        return step;
    }
    bool const down = UpdateGrip(grip_[h], hand.squeeze, HandSpeed(h, input.now), input.grip);
    if (!down) {
        step.release = EstimateRelease(history_[h], input.now);
        step.event = step.release.valid && step.release.speed >= input.throwSpeedMin ? Event::Thrown : Event::Cancelled;
        holder_ = -1;
    }
    return step;
}

// ---- Melee -----------------------------------------------------------------------------------------

void SwingDetector::Reset() {
    history_.Reset();
    stroke_ = false;
    spent_ = false;
}

SwingStep SwingDetector::Update(SwingInput const& input, SwingParams const& params) {
    SwingStep step;
    if (!input.tracked || !Finite(input.position)) {
        Reset();
        return step;
    }
    if (history_.Count() > 0 && !(input.now > history_.Newest().t)) {
        return step;  // the same sample again
    }
    if (history_.Add(input.now, input.position)) {
        stroke_ = false;  // a jump: no stroke spans it
        spent_ = false;
    }
    auto const v = history_.FitVelocity(input.now - params.fitSeconds, input.now);
    if (!v) {
        return step;
    }
    double const speed = Length(*v);
    step.speed = speed;
    if (!(speed < params.maxSpeed)) {
        stroke_ = false;
        spent_ = false;
        return step;
    }
    peakSpeed_ = std::max(peakSpeed_, speed);
    if (speed < params.minSpeed) {
        stroke_ = false;  // the stroke is over; the next one may strike again
        spent_ = false;
        return step;
    }
    Vec3 const direction = Normalize(*v);
    if (!stroke_ || Dot(direction, strokeDirection_) < 0.5) {
        // A new stroke (or one that turned sharply, a back-and-forth): it starts where the hand was a
        // velocity window ago, so the first fast sample already counts its travel.
        stroke_ = true;
        spent_ = false;
        strokeStart_ = history_.At(0).p;
        for (int i = history_.Count() - 1; i >= 0; --i) {
            if (history_.At(i).t <= input.now - params.fitSeconds) {
                strokeStart_ = history_.At(i).p;
                break;
            }
        }
    }
    strokeDirection_ = direction;
    Vec3 const travelled = input.position - strokeStart_;
    step.travel = Dot(travelled, direction) > 0.0 ? Length(travelled) : 0.0;
    step.direction = Normalize(travelled, direction);
    step.forward = Dot(step.direction, input.bodyForward);
    if (spent_ || step.travel < params.minTravel) {
        return step;
    }
    spent_ = true;  // one decision per stroke
    if (input.suppressed) {
        step.refused = "suppressed (a grenade in hand, firing, a draw just now, or the other hand holds the gun)";
    } else if (step.forward < params.forwardMin) {
        step.refused = "not toward the front (a pull back)";
    } else if (input.now < readyAt_) {
        step.refused = "cooldown";
    } else {
        step.strike = true;
        readyAt_ = input.now + params.cooldown;
    }
    return step;
}

// ---- Arc ---------------------------------------------------------------------------------------------

std::vector<Vec3> ArcPoints(Vec3 start, Vec3 velocity, double gravityZ, double dt, double maxSeconds, double maxDrop) {
    std::vector<Vec3> points;
    if (!Finite(start) || !Finite(velocity) || !std::isfinite(gravityZ) || !(dt > 0.0) || !(maxSeconds > 0.0)) {
        return points;
    }
    int const steps = static_cast<int>(std::ceil(maxSeconds / dt));
    points.reserve(static_cast<std::size_t>(steps) + 1);
    points.push_back(start);
    for (int i = 1; i <= steps; ++i) {
        double const t = i * dt;
        Vec3 const p{start.x + velocity.x * t, start.y + velocity.y * t, start.z + velocity.z * t + 0.5 * gravityZ * t * t};
        points.push_back(p);
        if (p.z < start.z - maxDrop) {
            break;
        }
    }
    return points;
}

// ---- Haptics ---------------------------------------------------------------------------------------

Buzz GrabClick(double strength) {
    return {Scaled(strength, 0.85), 280.0f, 0.025f};
}

Buzz HoldBuzz(double strength) {
    return {Scaled(strength, 0.06), 55.0f, 0.0f};
}

Buzz ThrowPulse(double strength) {
    return {Scaled(strength, 0.5), 180.0f, 0.04f};
}

Buzz StowClick(double strength) {
    return {Scaled(strength, 0.6), 200.0f, 0.02f};
}

Buzz RefusedBuzz(double strength) {
    return {Scaled(strength, 0.3), 60.0f, 0.12f};
}

Buzz StrikeTick(double strength) {
    return {Scaled(strength, 0.45), 140.0f, 0.03f};
}

Buzz HitThump(double strength) {
    return {Scaled(strength, 1.0), 80.0f, 0.07f};
}

} // namespace mevr::ik::throwing
