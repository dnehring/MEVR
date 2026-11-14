// SPDX-License-Identifier: MIT
#include "core/qa/DiagnosticPad.hpp"
#include <limits>

namespace mevr::qa::diagnostic_pad {
namespace {
void Increment(std::uint64_t& value) { if (value != std::numeric_limits<std::uint64_t>::max()) ++value; }
}
bool Source::Outstanding() const {
    return state_.phase == Phase::Published || state_.phase == Phase::Consumed || state_.phase == Phase::DrainPending;
}
void Source::Stop(End reason) {
    if (state_.phase == Phase::Published || state_.phase == Phase::Consumed) {
        state_.phase = Phase::DrainPending;
        state_.end = reason;
    }
}
bool Source::Time(std::uint64_t nowMs) {
    if (haveClock_ && nowMs < lastClock_) { Stop(End::ClockRegressed); return false; }
    haveClock_ = true; lastClock_ = nowMs;
    return true;
}
Admission Source::BeginRun(std::uint64_t run) {
    std::lock_guard lock(mutex_);
    if (run == 0 || run == state_.run) return Admission::InvalidRun;
    if (state_.runOpen) return Admission::RunAlreadyOpen;
    if (Outstanding()) return Admission::Busy;
    state_ = {}; state_.run = run; state_.runOpen = true;
    return Admission::Accepted;
}
Admission Source::Publish(std::uint64_t run, std::uint64_t sequence, Pad pad,
                          std::uint32_t durationMs, std::uint64_t nowMs) {
    std::lock_guard lock(mutex_);
    if (run == 0 || run != state_.run || !state_.runOpen) return Admission::InvalidRun;
    if (sequence == 0 || sequence <= state_.sequence) return Admission::StaleSequence;
    if (Outstanding()) return Admission::Busy; // no replacement or queue
    if (pad == Pad{} || (pad.buttons & ~kButtons) != 0 || durationMs == 0 || durationMs > kMaxPulseMs ||
        nowMs > std::numeric_limits<std::uint64_t>::max() - kActivationWaitMs - durationMs) return Admission::InvalidPulse;
    if (!Time(nowMs)) return Admission::ClockRegressed;
    state_ = {}; state_.run = run; state_.runOpen = true; state_.sequence = sequence;
    state_.requested = pad; state_.publishedMs = nowMs;
    state_.activationDeadlineMs = nowMs + kActivationWaitMs;
    state_.requestedDurationMs = durationMs;
    state_.phase = Phase::Published;
    return Admission::Accepted;
}
bool Source::CancelRun(std::uint64_t run, std::uint64_t nowMs) {
    std::lock_guard lock(mutex_);
    if (run == 0 || run != state_.run || !state_.runOpen) return false;
    Time(nowMs); Stop(End::RunCancelled); state_.runOpen = false;
    return true;
}
PollResult Source::Emit(Pad pad, std::uint32_t originalPacket) {
    if (!havePacket_ || pad != lastEmitted_) {
        // Packet numbers may wrap. Ensure a changed synthetic sample differs
        // from the underlying packet too; do not copy a stale physical packet.
        packet_ = havePacket_ ? packet_ + 1 : originalPacket + 1;
        if (packet_ == originalPacket) ++packet_;
        havePacket_ = true; lastEmitted_ = pad;
    }
    return {true,pad,packet_};
}
PollResult Source::Poll(std::uint32_t index, Physical physical, std::uint64_t nowMs) {
    if (index != 0) return {}; // no telemetry or clock side effect for other slots
    std::lock_guard lock(mutex_);
    if (!Outstanding()) return {};
    bool const clockValid = Time(nowMs);
    state_.physicalObserved = true; state_.lastPhysical = physical; state_.lastPhysicalMs = nowMs;
    if (physical.state != PhysicalState::Connected && physical.state != PhysicalState::Disconnected) {
        Stop(End::PhysicalError); Increment(state_.conflictingPolls); return {};
    }
    if (physical.state == PhysicalState::Connected && physical.pad != Pad{}) {
        Stop(End::PhysicalConflict); Increment(state_.conflictingPolls); return {};
    }
    // Waiting for the first eligible poll is bounded separately from the hold.
    // Expiry/invalid clock wins before any nonneutral output. A cancelled run
    // still emits its neutral release when a safe poll becomes available.
    if (clockValid && state_.phase == Phase::Published && nowMs >= state_.activationDeadlineMs)
        Stop(End::StartTimedOut);
    if (clockValid && state_.phase == Phase::Consumed && nowMs >= state_.expiresMs) Stop(End::Expired);
    if (state_.phase == Phase::DrainPending) {
        if (state_.neutralPolls == 0) {
            state_.neutralFirstMs = nowMs;
            // A rollback cannot shorten the release window. Near the clock's
            // representable end, saturate rather than wrap into the past.
            auto const anchor = lastClock_;
            auto const maximum = std::numeric_limits<std::uint64_t>::max();
            state_.neutralDeadlineMs = anchor > maximum - kNeutralDwellMs ? maximum : anchor + kNeutralDwellMs;
        }
        state_.neutralLastMs = nowMs;
        Increment(state_.neutralPolls); state_.neutralMs = nowMs;
        if (clockValid && nowMs >= state_.neutralDeadlineMs) state_.phase = Phase::NeutralEmitted;
        return Emit({},physical.packet);
    }
    if (!clockValid || !state_.runOpen) return {};
    if (state_.pulsePolls == 0) {
        // Publish reserved enough headroom for the entire activation window
        // plus this hold. This addition cannot overflow before its deadline.
        state_.firstPulseMs = nowMs;
        state_.expiresMs = nowMs + state_.requestedDurationMs;
    }
    Increment(state_.pulsePolls); state_.lastPulseMs = nowMs; state_.phase = Phase::Consumed;
    return Emit(state_.requested,physical.packet);
}
Observation Source::Snapshot() const { std::lock_guard lock(mutex_); return state_; }
}
