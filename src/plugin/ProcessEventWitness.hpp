// SPDX-License-Identifier: MIT
#pragma once
#include "plugin/ProcessEventDiagnostic.hpp"

namespace mevr::host::process_event::diagnostic {
// Pure arithmetic over .text/.pdata facts supplied by Exe::LiveImage; no reads.
inline TargetInspection InspectBounds(std::uintptr_t moduleBase, std::uint32_t textRva,
    std::size_t textSize, std::uintptr_t target, std::uintptr_t baseTarget,
    bool known, bool found, std::uint32_t begin, std::uint32_t end) noexcept {
    TargetInspection out{};
    out.target = target; out.baseTarget = baseTarget; out.moduleBase = moduleBase;
    out.sameAsBase = target != 0 && target == baseTarget;
    out.reason = "outside-main-text";
    if (!moduleBase || !target || target < moduleBase || target - moduleBase > UINT32_MAX) return out;
    out.rva = static_cast<std::uint32_t>(target - moduleBase);
    if (out.rva < textRva || out.rva - textRva >= textSize) return out;
    out.inMainText = true;
    out.boundsKnown = known && found;
    out.reason = "function-bounds-unavailable";
    if (!out.boundsKnown) return out;
    out.reason = "invalid-or-excessive-function-bounds";
    if (begin < textRva || end <= begin || end - begin > kMaxInspectedFunctionBytes ||
        static_cast<std::uint64_t>(end) - textRva > textSize ||
        end > std::numeric_limits<std::uintptr_t>::max() - moduleBase) return out;
    out.functionBegin = moduleBase + begin; out.functionEnd = moduleBase + end;
    out.exactFunctionStart = begin == out.rva;
    out.reason = out.exactFunctionStart ? "bounded-exact-function-start-only" : "target-is-not-function-start";
    out.boundsUsable = out.exactFunctionStart;
    return out;
}

inline void SaturatingIncrement(std::uint64_t& count) noexcept {
    if (count != std::numeric_limits<std::uint64_t>::max()) ++count;
}

inline bool WithinInspection(TargetInspection const& target, std::uintptr_t address, std::size_t size) noexcept {
    if (!size || !target.boundsUsable) return false;
    if (address == target.baseTarget && address != 0 && size == 1) return true;
    return address >= target.functionBegin && address < target.functionEnd && size <= target.functionEnd - address;
}

// Caller serializes access. Stores only numeric identities; never dereferences
// an engine object. This is a short sample epoch, not an object incarnation ID.
class NaturalCallWitness {
public:
    struct Ticket { std::uint64_t epoch{}; bool matching{}; };
    WitnessSnapshot Configure(ControllerWatch const& identity, TargetInspection const& target,
                              bool ownedBaseReady, std::uint64_t now) noexcept {
        if (snapshot_.status == WatchStatus::EpochExhausted) return snapshot_;
        bool const valid = ownedBaseReady && identity.object && identity.classAddress && identity.index >= 0 &&
            identity.target && identity.gameThread && target.target == identity.target && target.boundsUsable &&
            target.inMainText && target.exactFunctionStart && target.target == target.functionBegin &&
            !target.sameAsBase && target.functionBegin < target.functionEnd;
        char const* reason = !ownedBaseReady ? "owned-base-chain-unavailable" :
            !identity.object ? "validated-controller-unavailable" : target.sameAsBase ? "slot-already-is-base-target" :
            !valid ? "controller-or-target-bounds-unavailable" : "bounded-passive-observation-only";
        bool const changed = !(identity == snapshot_.identity) ||
            target.functionBegin != snapshot_.functionBegin || target.functionEnd != snapshot_.functionEnd ||
            target.baseTarget != baseTarget_ || valid != configuredValid_;
        if (changed) {
            if (snapshot_.epoch == std::numeric_limits<std::uint64_t>::max()) {
                snapshot_.status = WatchStatus::EpochExhausted;
                snapshot_.reason = "scope-epoch-exhausted";
                return snapshot_;
            }
            auto const epoch = snapshot_.epoch + 1;
            snapshot_ = {};
            snapshot_.epoch = epoch; snapshot_.identity = identity;
            snapshot_.functionBegin = target.functionBegin; snapshot_.functionEnd = target.functionEnd;
            configuredValid_ = valid;
            baseTarget_ = target.baseTarget;
            snapshot_.reason = reason;
            if (valid && now <= std::numeric_limits<std::uint64_t>::max() - kWatchDurationMs) {
                snapshot_.status = WatchStatus::Watching;
                snapshot_.startedMs = now; snapshot_.expiresMs = now + kWatchDurationMs;
            } else if (valid) snapshot_.reason = "observation-clock-overflow";
        }
        return Sample(now);
    }
    WitnessSnapshot Sample(std::uint64_t now) noexcept {
        if (snapshot_.status == WatchStatus::Watching && now >= snapshot_.expiresMs) {
            snapshot_.status = WatchStatus::Expired;
            snapshot_.reason = "fixed-observation-window-expired";
        }
        return snapshot_;
    }
    Ticket Enter(std::uintptr_t object, std::uint32_t thread, std::uintptr_t caller,
                 std::uintptr_t function, std::uint64_t now) noexcept {
        Sample(now);
        if (snapshot_.status != WatchStatus::Watching || object != snapshot_.identity.object ||
            thread != snapshot_.identity.gameThread) return {};
        SaturatingIncrement(snapshot_.entries);
        // A return address is the instruction AFTER the call. Require it inside
        // the exact observed slot-target function; no arbitrary stack walking.
        bool const matching = caller > snapshot_.functionBegin && caller < snapshot_.functionEnd;
        if (matching) {
            SaturatingIncrement(snapshot_.matchingEntries);
            snapshot_.lastMatchingCaller = caller; snapshot_.lastMatchingFunction = function;
        }
        return {snapshot_.epoch, matching};
    }
    void ReturnedOriginal(Ticket ticket) noexcept {
        // An entry before expiry may return after it. Scope changes invalidate
        // old tickets; faults/nonlocal exits simply never reach this method.
        if (!ticket.epoch || ticket.epoch != snapshot_.epoch) return;
        SaturatingIncrement(snapshot_.originalReturns);
        if (ticket.matching) SaturatingIncrement(snapshot_.matchingOriginalReturns);
    }
private:
    WitnessSnapshot snapshot_{};
    bool configuredValid_{};
    std::uintptr_t baseTarget_{};
};
} // namespace mevr::host::process_event::diagnostic
