// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

// Original engine-independent P4 prototype. No ammo model, native calls, timers,
// geometry or global hand arbitration. See research/le2-reload-core.md.
namespace mevr::vr::reload {

struct Identity {
    // Host-owned, nonzero monotonic generations within one host epoch. Each
    // changed component must increase, including across world replacement.
    std::uint64_t world = 0;
    std::uint64_t weapon = 0;
    std::uint64_t context = 0;
    bool operator==(Identity const&) const = default;
    [[nodiscard]] bool Valid() const { return world && weapon && context; }
};

enum class Hand { None, Left, Right };

struct HandGrant {
    Identity identity;
    // Caller-issued exclusive lease. A revoked/changed grant requires a NEW
    // monotonically increasing lease, not reuse of a released token.
    std::uint64_t lease = 0;
    Hand hand = Hand::None;
    bool exclusive = false;
    bool operator==(HandGrant const&) const = default;
};

struct ClipKey {
    std::uint64_t epoch = 0;
    std::uint64_t serial = 0;
    Identity identity;
    HandGrant grant;
    bool operator==(ClipKey const&) const = default;
};

struct RequestKey {
    std::uint64_t epoch = 0;
    std::uint64_t serial = 0;
    Identity identity;
    bool operator==(RequestKey const&) const = default;
};

struct Signals {
    bool eject = false;
    bool acquire = false;
    bool insert = false;
    [[nodiscard]] bool Released() const { return !eject && !acquire && !insert; }
};

struct Frame {
    // Strictly increasing coherent observations. Replayed/out-of-order samples
    // cannot release a latch, change identity or resolve a pending request.
    std::uint64_t sample = 0;
    Identity identity;
    bool supportedFiniteReserve = false;
    bool eligible = false;        // Caller verifies current native eligibility.
    bool contextAllowed = false;
    bool trackingValid = false;
    HandGrant grant;
    Signals down;
    ClipKey insertedClip;         // Meaningful only on an insertion rising edge.
};

enum class Phase { Idle, Ejected, HoldingClip };
enum class Admission {
    NoEdge, StaleSample, InvalidIdentity, InvalidGrant, GuardBlocked,
    ReleaseRequired, ConflictingEdges, WrongPhase, InvalidClip, WaitingNative,
    Ejected, Acquired, Requested, Cancelled, Exhausted
};

struct PendingRequest {
    RequestKey key;
    bool accepted = false;
    bool uncertain = false;
};

struct StepResult {
    Admission admission = Admission::NoEdge;
    std::optional<ClipKey> acquired;
    // This one-shot output is permission to request native TryReload through a
    // future adapter. It is NOT permission to call DoReload or transfer ammo.
    std::optional<RequestKey> request;
    bool clipInvalidated = false;
    bool gestureCancelled = false;
    bool reconciliationRequired = false;
};

enum class FeedbackKind { Accepted, Rejected, Finished, Aborted, Uncertain };
struct Feedback {
    RequestKey request;
    FeedbackKind kind = FeedbackKind::Uncertain;
    // Terminal feedback only: the adapter has observed authoritative native
    // quiescence/outcome. Timeout, animation disappearance or an input release
    // are not reconciliation. Aborted does not mean no ammo transfer/refund.
    bool nativeReconciled = false;
};
enum class Observation { Stale, Accepted, Uncertain, NeedsReconciliation, Contradictory, Settled };

struct Limits {
    // Optional lower session budgets. Counters never wrap or reset.
    std::uint64_t clips = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t requests = std::numeric_limits<std::uint64_t>::max();
};

class Coordinator {
public:
    // A fresh host epoch is mandatory for each lifetime. Construction is NOT a
    // way to clear uncertain native work: all old requests must first settle.
    explicit Coordinator(std::uint64_t epoch, Limits limits = {})
        : epoch_(epoch), limits_(limits), faulted_(!epoch || !limits.clips || !limits.requests) {}
    Coordinator(Coordinator const&) = delete;
    Coordinator& operator=(Coordinator const&) = delete;
    Coordinator(Coordinator&&) = delete;
    Coordinator& operator=(Coordinator&&) = delete;

    [[nodiscard]] Phase GetPhase() const { return phase_; }
    [[nodiscard]] std::optional<ClipKey> const& HeldClip() const { return clip_; }
    [[nodiscard]] std::optional<PendingRequest> const& Pending() const { return pending_; }
    [[nodiscard]] bool Faulted() const { return faulted_; }

    // Deliberate local clip drop/gesture cancellation. This is NOT a native
    // cancel operation. Any issued request remains unresolved and may transfer
    // ammunition. Idempotent; recovery still requires a fresh all-up sample.
    StepResult CancelGesture() {
        StepResult result;
        Invalidate(result);
        if (pending_) pending_->uncertain = true;
        result.admission = Admission::Cancelled;
        result.reconciliationRequired = pending_.has_value();
        return result;
    }

    StepResult Step(Frame const& frame) {
        StepResult result;
        auto done = [&](Admission admission) {
            result.admission = admission;
            result.reconciliationRequired = pending_.has_value();
            return result;
        };
        if (!frame.sample || frame.sample <= lastSample_)
            return done(Admission::StaleSample);
        lastSample_ = frame.sample;
        Signals edges{frame.down.eject && !previous_.eject,
                      frame.down.acquire && !previous_.acquire,
                      frame.down.insert && !previous_.insert};
        // Update every fresh sample even if all admission checks below fail.
        previous_ = frame.down;
        if (faulted_) return done(Admission::Exhausted);

        if (!frame.identity.Valid() || Regressed(frame.identity)) {
            Invalidate(result);
            RevokeGrant();
            if (pending_) pending_->uncertain = true;
            return done(Admission::InvalidIdentity);
        }
        if (!identity_ || *identity_ != frame.identity) {
            Invalidate(result);
            RevokeGrant();
            identity_ = frame.identity;
            if (pending_) pending_->uncertain = true;
        }

        bool grantValid = ValidGrant(frame.grant, frame.identity);
        if (!grantValid) {
            Invalidate(result);
            RevokeGrant();
            if (pending_) pending_->uncertain = true;
            return done(Admission::InvalidGrant);
        }
        if (!grant_ || *grant_ != frame.grant) {
            // A different hand/lease invalidates any physical item before the
            // newly granted hand can begin. No global ownership is inferred.
            Invalidate(result);
            grant_ = frame.grant;
            highestLease_ = frame.grant.lease;
            if (pending_) pending_->uncertain = true;
        }

        bool contextValid = frame.supportedFiniteReserve && frame.contextAllowed && frame.trackingValid;
        if (!contextValid) {
            Invalidate(result);
            if (pending_) pending_->uncertain = true;
            return done(Admission::GuardBlocked);
        }
        if (pending_) return done(Admission::WaitingNative);
        if (!frame.eligible) {
            Invalidate(result);
            return done(Admission::GuardBlocked);
        }
        if (!armed_) {
            // Initial activation and every interrupted/settled lifecycle require
            // a fresh observed release of ALL gesture signals.
            if (frame.down.Released()) armed_ = true;
            return done(Admission::ReleaseRequired);
        }
        unsigned edgeCount = unsigned(edges.eject) + unsigned(edges.acquire) + unsigned(edges.insert);
        if (!edgeCount) return done(Admission::NoEdge);
        if (edgeCount != 1) return done(Admission::ConflictingEdges);

        if (phase_ == Phase::Idle && edges.eject) {
            phase_ = Phase::Ejected;
            return done(Admission::Ejected);
        }
        if (phase_ == Phase::Ejected && edges.acquire) {
            if (clipSerial_ >= limits_.clips) {
                Exhaust(result);
                return done(Admission::Exhausted);
            }
            clip_ = ClipKey{epoch_, ++clipSerial_, frame.identity, *grant_};
            phase_ = Phase::HoldingClip;
            result.acquired = clip_;
            return done(Admission::Acquired);
        }
        if (phase_ == Phase::HoldingClip && edges.insert) {
            if (!clip_ || frame.insertedClip != *clip_)
                return done(Admission::InvalidClip);
            if (requestSerial_ >= limits_.requests) {
                Exhaust(result);
                return done(Admission::Exhausted);
            }
            RequestKey key{epoch_, ++requestSerial_, frame.identity};
            pending_ = PendingRequest{key, false, false};
            clip_.reset(); // Consumed token can never be inserted again.
            phase_ = Phase::Idle;
            armed_ = false;
            result.request = key;
            return done(Admission::Requested);
        }
        return done(Admission::WrongPhase);
    }

    Observation Observe(Feedback const& feedback) {
        if (!pending_ || feedback.request != pending_->key)
            return Observation::Stale;
        switch (feedback.kind) {
        case FeedbackKind::Accepted:
            pending_->accepted = true;
            // Acceptance does not clear uncertainty or prove completion/ammo.
            return Observation::Accepted;
        case FeedbackKind::Uncertain:
            pending_->uncertain = true;
            return Observation::Uncertain;
        case FeedbackKind::Rejected:
            // Accepted means the native path began. A later rejection conflicts
            // with that fact; observed finish/abort is needed to reconcile it.
            if (pending_->accepted) {
                pending_->uncertain = true;
                return Observation::Contradictory;
            }
            [[fallthrough]];
        case FeedbackKind::Finished:
        case FeedbackKind::Aborted:
            if (!feedback.nativeReconciled) {
                pending_->uncertain = true;
                return Observation::NeedsReconciliation;
            }
            pending_.reset();
            // This also handles a matching old-generation terminal report: it
            // releases only the old outstanding gate. No old gesture is revived.
            armed_ = false;
            return Observation::Settled;
        }
        return Observation::Stale;
    }

private:
    [[nodiscard]] bool Regressed(Identity const& identity) const {
        return identity_ && (identity.world < identity_->world || identity.weapon < identity_->weapon ||
                             identity.context < identity_->context);
    }
    [[nodiscard]] bool ValidGrant(HandGrant const& grant, Identity const& identity) const {
        if (!grant.exclusive || !grant.lease || (grant.hand != Hand::Left && grant.hand != Hand::Right) ||
            grant.identity != identity)
            return false;
        return (grant_ && grant == *grant_) || grant.lease > highestLease_;
    }
    void RevokeGrant() { grant_.reset(); }
    void Invalidate(StepResult& result) {
        result.clipInvalidated = result.clipInvalidated || clip_.has_value();
        result.gestureCancelled = result.gestureCancelled || phase_ != Phase::Idle;
        clip_.reset();
        phase_ = Phase::Idle;
        armed_ = false;
    }
    void Exhaust(StepResult& result) {
        Invalidate(result);
        faulted_ = true;
    }

    std::uint64_t epoch_;
    Limits limits_;
    bool faulted_ = false;
    bool armed_ = false;
    std::uint64_t lastSample_ = 0;
    std::uint64_t clipSerial_ = 0;
    std::uint64_t requestSerial_ = 0;
    std::uint64_t highestLease_ = 0;
    std::optional<Identity> identity_;
    std::optional<HandGrant> grant_;
    Signals previous_;
    Phase phase_ = Phase::Idle;
    std::optional<ClipKey> clip_;
    std::optional<PendingRequest> pending_;
};

} // namespace mevr::vr::reload
