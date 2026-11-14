// SPDX-License-Identifier: MIT
#include "core/vr/PhysicalReloadRequest.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

using namespace mevr::vr::reload;

static void Check(bool value, char const* expression, int line) {
    if (!value) throw std::runtime_error("line " + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) Check(bool(expression), #expression, __LINE__)

struct Rig {
    Coordinator coordinator;
    Frame frame;
    std::uint64_t sample = 0;
    explicit Rig(std::uint64_t epoch = 7, Limits limits = {}) : coordinator(epoch, limits) {
        frame.identity = {1, 2, 3};
        frame.supportedFiniteReserve = frame.eligible = frame.contextAllowed = frame.trackingValid = true;
        frame.grant = {frame.identity, 1, Hand::Left, true};
    }
    StepResult Send(Signals down = {}) {
        frame.sample = ++sample;
        frame.down = down;
        return coordinator.Step(frame);
    }
    void Eject() {
        Send(); // Arm/release; a held previous input cannot become a fresh edge.
        CHECK(Send({true, false, false}).admission == Admission::Ejected);
    }
    ClipKey Acquire() {
        Send();
        auto result = Send({false, true, false});
        CHECK(result.admission == Admission::Acquired);
        CHECK(result.acquired.has_value());
        return *result.acquired;
    }
    ClipKey MakeClip() { Eject(); return Acquire(); }
    StepResult Insert(ClipKey key) {
        Send();
        frame.insertedClip = key;
        return Send({false, false, true});
    }
    RequestKey Request() {
        auto key = MakeClip();
        auto result = Insert(key);
        CHECK(result.admission == Admission::Requested);
        CHECK(result.request.has_value());
        return *result.request;
    }
};

static void GestureStagesAndOneShotRequest() {
    Rig r;
    r.Send();
    CHECK(r.Send({false, true, false}).admission == Admission::WrongPhase);
    r.Send();
    CHECK(r.Send({false, false, true}).admission == Admission::WrongPhase);
    r.Eject();
    CHECK(!r.coordinator.Pending());
    CHECK(r.coordinator.GetPhase() == Phase::Ejected);
    CHECK(!r.Send({true, false, false}).request);
    auto clip = r.Acquire();
    CHECK(!r.coordinator.Pending()); // No native request before insertion.
    auto inserted = r.Insert(clip);
    CHECK(inserted.request);
    CHECK(inserted.request->identity == r.frame.identity);
    CHECK(!r.coordinator.HeldClip());
    for (int i = 0; i < 100; ++i) {
        auto result = r.Send({false, false, true});
        CHECK(!result.request);
        CHECK(!result.acquired);
        CHECK(result.reconciliationRequired);
    }
    CHECK(r.coordinator.Observe({*inserted.request, FeedbackKind::Accepted, false}) == Observation::Accepted);
    CHECK(r.coordinator.Pending());
    CHECK(!r.Send().request);
    CHECK(r.coordinator.Observe({*inserted.request, FeedbackKind::Finished, true}) == Observation::Settled);
    CHECK(!r.coordinator.Pending());
    CHECK(r.coordinator.Observe({*inserted.request, FeedbackKind::Finished, true}) == Observation::Stale);
}

static void InitialHeldAndRejectedSignalsRequireRelease() {
    Rig r;
    CHECK(r.Send({true, false, false}).admission == Admission::ReleaseRequired);
    CHECK(r.Send({true, false, false}).admission == Admission::ReleaseRequired);
    r.frame.contextAllowed = false;
    CHECK(r.Send({true, true, false}).admission == Admission::GuardBlocked);
    r.frame.contextAllowed = true;
    CHECK(r.Send({true, true, false}).admission == Admission::ReleaseRequired);
    r.Send({false, true, false});
    CHECK(r.coordinator.GetPhase() == Phase::Idle);
    r.Send();
    CHECK(r.Send({true, false, false}).admission == Admission::Ejected);
    CHECK(!r.Send({false, false, true}).request); // No acquisition yet.
}

static void ConflictingEdgesCannotSkipStages() {
    Rig r;
    r.Send();
    CHECK(r.Send({true, true, true}).admission == Admission::ConflictingEdges);
    CHECK(r.coordinator.GetPhase() == Phase::Idle);
    CHECK(r.Send({true, false, false}).admission == Admission::NoEdge);
    r.Send();
    CHECK(r.Send({true, false, false}).admission == Admission::Ejected);
    r.Send();
    CHECK(r.Send({false, true, true}).admission == Admission::ConflictingEdges);
    CHECK(r.coordinator.GetPhase() == Phase::Ejected);
    CHECK(!r.coordinator.HeldClip());
}

static void OldClipAndForgedIdentityAreRejected() {
    Rig r;
    auto first = r.MakeClip();
    auto wrong = first;
    ++wrong.serial;
    CHECK(r.Insert(wrong).admission == Admission::InvalidClip);
    CHECK(r.coordinator.HeldClip() == first);
    wrong = first; ++wrong.epoch;
    CHECK(r.Insert(wrong).admission == Admission::InvalidClip);
    wrong = first; ++wrong.identity.weapon;
    CHECK(r.Insert(wrong).admission == Admission::InvalidClip);
    wrong = first; ++wrong.grant.lease;
    CHECK(r.Insert(wrong).admission == Admission::InvalidClip);
    wrong = first; wrong.grant.hand = Hand::Right;
    CHECK(r.Insert(wrong).admission == Admission::InvalidClip);
    auto request = *r.Insert(first).request;
    CHECK(r.coordinator.Observe({request, FeedbackKind::Finished, true}) == Observation::Settled);
    auto second = r.MakeClip();
    CHECK(second.serial > first.serial);
    CHECK(r.Insert(first).admission == Admission::InvalidClip);
    auto next = r.Insert(second);
    CHECK(next.request && next.request->serial > request.serial);
    CHECK(r.coordinator.Observe({request, FeedbackKind::Rejected, true}) == Observation::Stale);
    CHECK(r.coordinator.Pending()->key == *next.request);
}

static void EligibilityAndContextLossDiscardHeldItem() {
    std::array<bool Frame::*, 4> guards{&Frame::supportedFiniteReserve, &Frame::eligible,
                                       &Frame::contextAllowed, &Frame::trackingValid};
    for (auto guard : guards) {
        Rig r;
        auto clip = r.MakeClip();
        r.frame.*guard = false;
        r.frame.insertedClip = clip;
        auto blocked = r.Send({false, false, true});
        CHECK(blocked.admission == Admission::GuardBlocked);
        CHECK(blocked.clipInvalidated && blocked.gestureCancelled);
        CHECK(!blocked.request && !r.coordinator.HeldClip());
        r.frame.*guard = true;
        CHECK(r.Send({false, false, true}).admission == Admission::ReleaseRequired);
        r.Send();
        CHECK(r.Send({false, false, true}).admission == Admission::WrongPhase);
    }
}

static void ExclusiveGrantIsMandatoryAndCannotBeReused() {
    Rig r;
    auto clip = r.MakeClip();
    r.frame.grant.exclusive = false;
    auto lost = r.Send();
    CHECK(lost.admission == Admission::InvalidGrant && lost.clipInvalidated);
    r.frame.grant.exclusive = true;
    CHECK(r.Send().admission == Admission::InvalidGrant); // Revoked lease cannot return.
    ++r.frame.grant.lease;
    CHECK(r.Send({true, false, false}).admission == Admission::ReleaseRequired);
    r.Send();
    CHECK(r.Send({true, false, false}).admission == Admission::Ejected);
    auto fresh = r.Acquire();
    CHECK(fresh.grant.lease != clip.grant.lease);
    r.frame.grant.hand = Hand::Right;
    CHECK(r.Send().admission == Admission::InvalidGrant); // Same lease, different hand.
    ++r.frame.grant.lease;
    r.Send();
    CHECK(!r.coordinator.HeldClip());
    r.Eject();
    CHECK(r.Acquire().grant.hand == Hand::Right);
}

static void InvalidGrantFieldsFailClosed() {
    for (int change = 0; change < 5; ++change) {
        Rig r;
        if (change == 0) r.frame.grant.lease = 0;
        if (change == 1) r.frame.grant.hand = Hand::None;
        if (change == 2) r.frame.grant.hand = static_cast<Hand>(99);
        if (change == 3) r.frame.grant.identity.weapon = 0;
        if (change == 4) r.frame.grant.exclusive = false;
        CHECK(r.Send().admission == Admission::InvalidGrant);
        CHECK(!r.Send({true, false, false}).request);
        CHECK(r.coordinator.GetPhase() == Phase::Idle);
    }
}

static void ChangedLeaseDiscardsClipEvenForSameHand() {
    Rig r;
    auto old = r.MakeClip();
    ++r.frame.grant.lease;
    auto changed = r.Send();
    CHECK(changed.clipInvalidated && changed.gestureCancelled);
    CHECK(!r.coordinator.HeldClip());
    r.Eject();
    auto fresh = r.Acquire();
    CHECK(r.Insert(old).admission == Admission::InvalidClip);
    CHECK(r.Insert(fresh).request);
}

static void DeliberateDropInvalidatesClipWithoutRevokingGrant() {
    Rig r;
    auto old = r.MakeClip();
    auto grant = r.frame.grant;
    auto cancelled = r.coordinator.CancelGesture();
    CHECK(cancelled.admission == Admission::Cancelled);
    CHECK(cancelled.clipInvalidated && cancelled.gestureCancelled);
    CHECK(!cancelled.reconciliationRequired && !r.coordinator.HeldClip());
    CHECK(r.frame.grant == grant);
    CHECK(!r.coordinator.CancelGesture().clipInvalidated); // Idempotent drop.
    CHECK(r.Send({false, true, false}).admission == Admission::ReleaseRequired);
    auto fresh = r.MakeClip();
    CHECK(fresh.grant == old.grant && fresh.serial > old.serial);
    CHECK(r.Insert(old).admission == Admission::InvalidClip);
    CHECK(r.Insert(fresh).request);
}

static void DeliberateCancelCannotCancelOutstandingNativeWork() {
    Rig r;
    auto request = r.Request();
    auto cancelled = r.coordinator.CancelGesture();
    CHECK(cancelled.admission == Admission::Cancelled && cancelled.reconciliationRequired);
    CHECK(!cancelled.clipInvalidated); // Already consumed; no second physical item.
    CHECK(r.coordinator.Pending()->key == request && r.coordinator.Pending()->uncertain);
    CHECK(r.coordinator.Observe({request, FeedbackKind::Accepted, false}) == Observation::Accepted);
    CHECK(r.coordinator.Pending()->uncertain);
    CHECK(!r.Send({true, false, false}).request);
    CHECK(r.coordinator.Observe({request, FeedbackKind::Aborted, false}) == Observation::NeedsReconciliation);
    CHECK(r.coordinator.Observe({request, FeedbackKind::Aborted, true}) == Observation::Settled);
    CHECK(r.Send({true, false, false}).admission == Admission::ReleaseRequired);
}

static void PendingRequestSurvivesWorldWeaponAndContextChanges() {
    std::array<std::uint64_t Identity::*, 3> generations{&Identity::world, &Identity::weapon, &Identity::context};
    for (auto generation : generations) {
        Rig r;
        auto old = r.Request();
        ++(r.frame.identity.*generation);
        r.frame.grant.identity = r.frame.identity;
        ++r.frame.grant.lease;
        auto change = r.Send();
        CHECK(change.reconciliationRequired && !change.request);
        CHECK(r.coordinator.Pending()->key == old);
        CHECK(r.coordinator.Pending()->uncertain);
        auto wrong = old; wrong.identity = r.frame.identity;
        CHECK(r.coordinator.Observe({wrong, FeedbackKind::Finished, true}) == Observation::Stale);
        CHECK(r.coordinator.Observe({old, FeedbackKind::Accepted, false}) == Observation::Accepted);
        CHECK(r.coordinator.GetPhase() == Phase::Idle && !r.coordinator.HeldClip());
        CHECK(r.coordinator.Observe({old, FeedbackKind::Aborted, false}) == Observation::NeedsReconciliation);
        CHECK(!r.Send({true, false, false}).request);
        CHECK(r.coordinator.Observe({old, FeedbackKind::Aborted, true}) == Observation::Settled);
        CHECK(r.Send({true, false, false}).admission == Admission::ReleaseRequired);
        auto fresh = r.Request();
        CHECK(fresh.identity == r.frame.identity && fresh.serial > old.serial);
        CHECK(r.coordinator.Observe({old, FeedbackKind::Finished, true}) == Observation::Stale);
        CHECK(r.coordinator.Pending()->key == fresh);
    }
}

static void TrackingModeOrGrantLossCannotClearPending() {
    for (int change = 0; change < 3; ++change) {
        Rig r;
        auto request = r.Request();
        if (change == 0) r.frame.trackingValid = false;
        if (change == 1) r.frame.contextAllowed = false;
        if (change == 2) r.frame.grant = {};
        auto result = r.Send();
        CHECK(result.reconciliationRequired && !result.request);
        CHECK(r.coordinator.Pending()->uncertain);
        CHECK(r.coordinator.Pending()->key == request);
        CHECK(r.coordinator.Observe({request, FeedbackKind::Finished, true}) == Observation::Settled);
        CHECK(!r.Send({true, false, false}).request);
    }
}

static void NoTimeoutOrVisualRetryOfUncertainWork() {
    Rig r;
    auto request = r.Request();
    CHECK(r.coordinator.Observe({request, FeedbackKind::Uncertain, false}) == Observation::Uncertain);
    for (int i = 0; i < 5000; ++i) {
        auto result = r.Send({i % 2 == 0, i % 3 == 0, i % 5 == 0});
        CHECK(!result.request && !result.acquired && result.reconciliationRequired);
    }
    CHECK(r.coordinator.Observe({request, FeedbackKind::Rejected, false}) == Observation::NeedsReconciliation);
    CHECK(r.coordinator.Pending());
    CHECK(r.coordinator.Observe({request, FeedbackKind::Rejected, true}) == Observation::Settled);
    CHECK(r.Request().serial > request.serial);
}

static void ContradictoryRejectionCannotEraseAcceptedWork() {
    Rig r;
    auto request = r.Request();
    CHECK(r.coordinator.Observe({request, FeedbackKind::Accepted, false}) == Observation::Accepted);
    CHECK(r.coordinator.Observe({request, FeedbackKind::Accepted, false}) == Observation::Accepted);
    r.frame.eligible = false; // Native reload can itself make CanReload false.
    CHECK(r.Send().admission == Admission::WaitingNative);
    CHECK(!r.coordinator.Pending()->uncertain);
    CHECK(r.coordinator.Observe({request, FeedbackKind::Rejected, true}) == Observation::Contradictory);
    CHECK(r.coordinator.Pending()->uncertain);
    CHECK(r.coordinator.Observe({request, FeedbackKind::Aborted, true}) == Observation::Settled);
    // Abort settles execution only. There is deliberately no ammo/refund result.
    CHECK(r.Send().admission == Admission::GuardBlocked);
}

static void FeedbackMustMatchWholeRequestIdentity() {
    Rig r;
    auto request = r.Request();
    for (int change = 0; change < 5; ++change) {
        auto wrong = request;
        if (change == 0) ++wrong.epoch;
        if (change == 1) ++wrong.serial;
        if (change == 2) ++wrong.identity.world;
        if (change == 3) ++wrong.identity.weapon;
        if (change == 4) ++wrong.identity.context;
        CHECK(r.coordinator.Observe({wrong, FeedbackKind::Accepted, false}) == Observation::Stale);
        CHECK(r.coordinator.Observe({wrong, FeedbackKind::Finished, true}) == Observation::Stale);
        CHECK(!r.coordinator.Pending()->accepted);
    }
    // An authoritative immediate completion may arrive without a separate ack.
    CHECK(r.coordinator.Observe({request, FeedbackKind::Finished, true}) == Observation::Settled);
}

static void ReplayedSamplesCannotCreateEdgesOrInvalidateCurrentState() {
    Rig r;
    auto clip = r.MakeClip();
    auto replay = r.frame;
    replay.down = {};
    replay.identity = {9, 9, 9};
    CHECK(r.coordinator.Step(replay).admission == Admission::StaleSample);
    --replay.sample;
    CHECK(r.coordinator.Step(replay).admission == Admission::StaleSample);
    CHECK(r.coordinator.HeldClip() == clip);
    CHECK(r.Send({false, true, false}).admission == Admission::NoEdge);
    CHECK(r.Insert(clip).request);
}

static void GenerationRegressionAndZeroNeverBecomeCurrent() {
    Rig r;
    auto clip = r.MakeClip();
    r.frame.identity.weapon = 1;
    CHECK(r.Send().admission == Admission::InvalidIdentity);
    CHECK(!r.coordinator.HeldClip());
    r.frame.identity.weapon = 2;
    r.frame.grant.identity = r.frame.identity;
    ++r.frame.grant.lease;
    auto newClip = r.MakeClip();
    CHECK(newClip != clip);
    r.frame.identity.world = 0;
    CHECK(r.Send().admission == Admission::InvalidIdentity);
    CHECK(!r.coordinator.Pending());
}

static void NonzeroCountersAndExhaustionFailClosed() {
    static_assert(!std::is_copy_constructible_v<Coordinator> && !std::is_move_constructible_v<Coordinator>);
    Rig bad(0);
    CHECK(bad.coordinator.Faulted());
    CHECK(bad.Send().admission == Admission::Exhausted);
    Rig noBudget(8, {0, 1});
    CHECK(noBudget.coordinator.Faulted());
    Rig clipLimit(8, {1, 2});
    auto key = clipLimit.Request();
    clipLimit.coordinator.Observe({key, FeedbackKind::Finished, true});
    clipLimit.Eject(); clipLimit.Send();
    CHECK(clipLimit.Send({false, true, false}).admission == Admission::Exhausted);
    CHECK(clipLimit.coordinator.Faulted() && !clipLimit.coordinator.HeldClip());
    Rig requestLimit(9, {2, 1});
    auto one = requestLimit.Request();
    requestLimit.coordinator.Observe({one, FeedbackKind::Finished, true});
    auto two = requestLimit.MakeClip();
    CHECK(requestLimit.Insert(two).admission == Admission::Exhausted);
    CHECK(requestLimit.coordinator.Faulted() && !requestLimit.coordinator.Pending());
    CHECK(!requestLimit.Send({true, true, true}).request);
    Rig leaseLimit;
    leaseLimit.frame.grant.lease = std::numeric_limits<std::uint64_t>::max();
    leaseLimit.Send();
    leaseLimit.frame.grant.exclusive = false; leaseLimit.Send();
    leaseLimit.frame.grant.exclusive = true;
    CHECK(leaseLimit.Send().admission == Admission::InvalidGrant);
    leaseLimit.frame.grant.lease = 1;
    CHECK(leaseLimit.Send().admission == Admission::InvalidGrant);
    Rig sampleLimit;
    sampleLimit.frame.sample = std::numeric_limits<std::uint64_t>::max();
    sampleLimit.coordinator.Step(sampleLimit.frame);
    CHECK(sampleLimit.Send().admission == Admission::StaleSample);
}

int main() {
    std::array tests{
        GestureStagesAndOneShotRequest, InitialHeldAndRejectedSignalsRequireRelease,
        ConflictingEdgesCannotSkipStages, OldClipAndForgedIdentityAreRejected,
        EligibilityAndContextLossDiscardHeldItem, ExclusiveGrantIsMandatoryAndCannotBeReused,
        InvalidGrantFieldsFailClosed, ChangedLeaseDiscardsClipEvenForSameHand,
        DeliberateDropInvalidatesClipWithoutRevokingGrant, DeliberateCancelCannotCancelOutstandingNativeWork,
        PendingRequestSurvivesWorldWeaponAndContextChanges, TrackingModeOrGrantLossCannotClearPending,
        NoTimeoutOrVisualRetryOfUncertainWork, ContradictoryRejectionCannotEraseAcceptedWork,
        FeedbackMustMatchWholeRequestIdentity, ReplayedSamplesCannotCreateEdgesOrInvalidateCurrentState,
        GenerationRegressionAndZeroNeverBecomeCurrent, NonzeroCountersAndExhaustionFailClosed
    };
    try {
        for (auto test : tests) test();
        std::cout << tests.size() << " reload coordinator tests passed; offline logic only\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "Reload coordinator failure: " << error.what() << '\n';
        return 1;
    }
}
