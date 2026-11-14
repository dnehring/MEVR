// SPDX-License-Identifier: MIT
// Included only by the production diagnostic control host, inside its private
// namespace. Reuses the shared bridge and read-only SDK context sampler.
namespace input_context = le2_diagnostic::input_context;
namespace pad_bridge = game::gamepad_bridge;
namespace pad_policy = qa::diagnostic_pad;

char const* PadPhase(pad_policy::Phase phase) {
    switch (phase) {
    case pad_policy::Phase::Idle: return "idle";
    case pad_policy::Phase::Published: return "published";
    case pad_policy::Phase::Consumed: return "output-written";
    case pad_policy::Phase::DrainPending: return "neutral-pending";
    case pad_policy::Phase::NeutralEmitted: return "neutral-written";
    }
    return "unknown";
}
char const* PadEnd(pad_policy::End end) {
    switch (end) {
    case pad_policy::End::None: return "none";
    case pad_policy::End::Expired: return "expired";
    case pad_policy::End::RunCancelled: return "cancelled";
    case pad_policy::End::PhysicalConflict: return "physical-conflict";
    case pad_policy::End::PhysicalError: return "physical-error";
    case pad_policy::End::ClockRegressed: return "clock-regressed";
    case pad_policy::End::StartTimedOut: return "start-timed-out";
    }
    return "unknown";
}
bool Outstanding(pad_policy::Observation const& state) {
    return state.phase == pad_policy::Phase::Published || state.phase == pad_policy::Phase::Consumed ||
           state.phase == pad_policy::Phase::DrainPending;
}
Value PadJson(pad_policy::Pad const& pad) {
    auto out = Value::Object();
    out["buttons"] = static_cast<int>(pad.buttons);
    out["leftTrigger"] = static_cast<int>(pad.leftTrigger); out["rightTrigger"] = static_cast<int>(pad.rightTrigger);
    out["leftX"] = static_cast<int>(pad.leftX); out["leftY"] = static_cast<int>(pad.leftY);
    out["rightX"] = static_cast<int>(pad.rightX); out["rightY"] = static_cast<int>(pad.rightY);
    return out;
}
Value InputBridgeJson(pad_bridge::DiagnosticObservation const& state) {
    auto out = Value::Object();
    out["available"] = state.available; out["optedIn"] = state.optedIn;
    out["disabled"] = state.disabled; out["faulted"] = state.faulted;
    out["backend"] = state.backend; out["hookCount"] = static_cast<int>(state.hookCount);
    out["generation"] = std::to_string(state.generation);
    out["callbacks"] = std::to_string(state.callbacks);
    out["recursivePassthrough"] = std::to_string(state.recursivePassthrough);
    out["foreignCallerPassthrough"] = std::to_string(state.foreignCallerPassthrough);
    out["staleCallbackPassthrough"] = std::to_string(state.staleCallbackPassthrough);
    out["nullBufferPassthrough"] = std::to_string(state.nullBufferPassthrough);
    out["callbacksInFlight"] = static_cast<int>(state.callbacksInFlight);
    out["outputWrites"] = std::to_string(state.outputWrites);
    out["neutralWrites"] = std::to_string(state.neutralWrites);
    out["cancellations"] = std::to_string(state.cancellations);
    out["mainTextBegin"] = std::to_string(state.main.textBegin);
    out["mainTextEnd"] = std::to_string(state.main.textEnd);
    out["lastEligibleCaller"] = std::to_string(state.lastEligibleCaller);
    auto hooks = Value::Array();
    for (unsigned i = 0; i < state.recordCount && i < state.hooks.size(); ++i) {
        auto const& hook = state.hooks[i]; auto item = Value::Object();
        item["dll"] = hook.dll; item["ordinal100"] = hook.ordinal100;
        char const* status = "failed";
        switch (hook.status) {
        case pad_bridge::DiagnosticHookStatus::MissingOptionalExport: status = "missing-optional-export"; break;
        case pad_bridge::DiagnosticHookStatus::InvalidExport: status = "invalid-export"; break;
        case pad_bridge::DiagnosticHookStatus::Installed: status = "installed"; break;
        case pad_bridge::DiagnosticHookStatus::Alias: status = "alias"; break;
        case pad_bridge::DiagnosticHookStatus::Failed: break;
        }
        item["status"] = status;
        item["requestedModule"] = hook.mapping.requested.path; item["mappedOwner"] = hook.mapping.owner.path;
        item["requestedBase"] = std::to_string(hook.mapping.requested.base);
        item["ownerBase"] = std::to_string(hook.mapping.owner.base);
        item["target"] = std::to_string(hook.mapping.target);
        item["detour"] = std::to_string(hook.detour); item["original"] = std::to_string(hook.original);
        item["canonicalSlot"] = static_cast<int>(hook.canonicalSlot);
        hooks.Push(std::move(item));
    }
    out["hooks"] = std::move(hooks);
    auto source = Value::Object(); auto const& s = state.source;
    source["token"] = std::to_string(s.run); source["sequence"] = std::to_string(s.sequence);
    source["phase"] = PadPhase(s.phase); source["end"] = PadEnd(s.end); source["runOpen"] = s.runOpen;
    source["publishedMs"] = std::to_string(s.publishedMs); source["expiresMs"] = std::to_string(s.expiresMs);
    source["activationDeadlineMs"] = std::to_string(s.activationDeadlineMs);
    source["requestedDurationMs"] = static_cast<int>(s.requestedDurationMs);
    source["pulsePolls"] = std::to_string(s.pulsePolls);
    source["firstPulseMs"] = std::to_string(s.firstPulseMs); source["lastPulseMs"] = std::to_string(s.lastPulseMs);
    source["neutralPolls"] = std::to_string(s.neutralPolls); source["neutralMs"] = std::to_string(s.neutralMs);
    source["conflictingPolls"] = std::to_string(s.conflictingPolls);
    source["requested"] = PadJson(s.requested);
    source["physicalObserved"] = s.physicalObserved;
    if (s.physicalObserved) {
        source["physicalState"] = s.lastPhysical.state == pad_policy::PhysicalState::Connected ? "connected" :
            s.lastPhysical.state == pad_policy::PhysicalState::Disconnected ? "disconnected" : "error";
        source["physicalPad"] = PadJson(s.lastPhysical.pad);
        source["physicalSampleMs"] = std::to_string(s.lastPhysicalMs);
    }
    out["source"] = std::move(source);
    out["evidenceScope"] = "XInput-output-buffer-writes; not game-consumption or gameplay-effect";
    return out;
}

struct NativeInput {
    hook::Context* hooks{}; // resident monolithic host; never supplied over the wire
    std::uint64_t token{};
    std::optional<input_context::Context> admitted;
    std::string cancellationReason;
    ~NativeInput() { Disable(); }
    void Cancel(char const* reason) {
        if (token) pad_bridge::DiagnosticCancelRun(token);
        admitted.reset(); cancellationReason = reason;
    }
    void Disable() noexcept {
        try { Cancel("endpoint-disabled-or-closed"); pad_bridge::DiagnosticDisable(); } catch (...) {}
    }
    bool Active() const { return token && Outstanding(pad_bridge::DiagnosticSnapshot().source); }
    void Tick(std::uint32_t thread) {
        if (!admitted) return;
        auto const state = pad_bridge::DiagnosticSnapshot();
        if (!state.available) { Cancel("bridge-unavailable"); return; }
        if (state.source.phase == pad_policy::Phase::NeutralEmitted) { admitted.reset(); return; }
        auto const current = input_context::CaptureInputContext(thread);
        if (!input_context::SameContext(*admitted, current)) Cancel("engine-input-context-changed");
    }
    Value Observe(std::uint32_t thread) const {
        auto out = Value::Object();
        out["context"] = input_context::ContextJson(input_context::CaptureInputContext(thread));
        out["bridge"] = InputBridgeJson(pad_bridge::DiagnosticSnapshot());
        out["cancellationReason"] = cancellationReason;
        out["gameplayEffectProven"] = false; out["vrRoutingProven"] = false;
        return out;
    }
    // Ownership/identity/deadline/sequence were checked by the existing endpoint.
    // Installation is opt-in only when this explicit pad-pulse is admitted.
    bool Publish(wire::PadPulse const& pulse, std::uint64_t sequence, std::uint32_t thread,
                 std::uint64_t deadline, Endpoint::UtcClock const& clock, std::string& reason) {
        reason = "previous-pulse-release-pending";
        if (Active()) return false;
        reason = "diagnostic-hook-service-unavailable";
        if (!hooks) return false;
        auto context = input_context::CaptureInputContext(thread);
        reason = "requested-input-context-unavailable-or-mismatched";
        auto const expected = pulse.context == "default" ? input_context::Category::Default : input_context::Category::PartySelect;
        if (!context.available || context.category != expected) return false;
        reason = "diagnostic-input-installation-unavailable";
        if (!pad_bridge::DiagnosticTryInstall(*hooks, true)) return false;
        // Installing hooks may take time. Recheck both context and request age.
        reason = "input-context-changed-before-publication";
        if (!input_context::SameContext(context, input_context::CaptureInputContext(thread))) return false;
        reason = "input-request-expired-before-publication";
        if (clock() >= deadline) return false;
        static std::uint64_t nextToken{}; // serialized game-thread, process lifetime; never reused
        reason = "input-token-space-exhausted";
        if (nextToken == std::numeric_limits<std::uint64_t>::max()) return false;
        if (token) pad_bridge::DiagnosticCancelRun(token);
        token = ++nextToken;
        reason = "input-run-admission-refused";
        if (pad_bridge::DiagnosticBeginRun(token) != pad_policy::Admission::Accepted) return false;
        // Keep the context before publishing so every subsequent tick can cancel.
        admitted = context; cancellationReason.clear();
        reason = "input-publication-refused";
        if (pad_bridge::DiagnosticPublish(token, sequence, pulse.pad, pulse.durationMs) != pad_policy::Admission::Accepted) {
            Cancel("publication-refused"); return false;
        }
        reason = "published-not-yet-proven-consumed";
        return true;
    }
};
