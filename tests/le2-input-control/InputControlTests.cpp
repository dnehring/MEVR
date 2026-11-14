// SPDX-License-Identifier: MIT
#include "engine/Le2ControlHost.hpp"
#include "engine/Le2InputContext.hpp"
#include "game/GamepadBridge.hpp"
#include "core/qa/ControlProtocol.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace pad = mevr::qa::diagnostic_pad;
namespace bridge = mevr::game::gamepad_bridge;
namespace context = mevr::engine::le2_diagnostic::input_context;
namespace mevr::hook { class Context {}; }

namespace fake {
std::unique_ptr<pad::Source> source;
context::Context current;
bridge::DiagnosticObservation bridgeState;
std::function<void()> onInstall, onCapture;
std::vector<std::string> calls;
std::uint64_t monotonic{}, utc{};
unsigned captures{}, installs{}, begins{}, publishes{}, cancels{}, disables{};
bool installOk{}, beginOk{}, publishOk{}, lastOptIn{};
std::uint32_t lastThread{};
void Reset() {
    source = std::make_unique<pad::Source>(); current = {}; current.available = true;
    current.category = context::Category::Default; current.controller.address = 100;
    current.modeActive = true; current.allowMovement = true;
    bridgeState = {}; onInstall = {}; onCapture = {}; calls.clear();
    monotonic = 100; utc = 1000; captures = installs = begins = publishes = cancels = disables = 0;
    installOk = beginOk = publishOk = true; lastOptIn = false; lastThread = 0;
}
pad::PollResult Poll(pad::Physical physical = {pad::PhysicalState::Disconnected,{},0}) {
    return source->Poll(0,physical,monotonic);
}
}

// Only the external services are fakes. Admission, cancellation, ordering and
// JSON below come from the exact production include. Release/expiry uses the
// real primitive Source, not a separate simulation of its state machine.
namespace mevr::game::gamepad_bridge {
bool DiagnosticTryInstall(hook::Context&, bool optIn) {
    ++fake::installs; fake::calls.push_back("install"); fake::lastOptIn = optIn;
    if (fake::onInstall) fake::onInstall();
    fake::bridgeState.optedIn = optIn;
    fake::bridgeState.available = optIn && fake::installOk && !fake::bridgeState.disabled;
    return fake::bridgeState.available;
}
pad::Admission DiagnosticBeginRun(std::uint64_t run) {
    ++fake::begins; fake::calls.push_back("begin");
    if (!fake::beginOk || !fake::bridgeState.available) return pad::Admission::InvalidRun;
    return fake::source->BeginRun(run);
}
pad::Admission DiagnosticPublish(std::uint64_t run, std::uint64_t sequence, pad::Pad value, std::uint32_t duration) {
    ++fake::publishes; fake::calls.push_back("publish");
    if (!fake::publishOk || !fake::bridgeState.available) return pad::Admission::InvalidPulse;
    return fake::source->Publish(run,sequence,value,duration,fake::monotonic);
}
bool DiagnosticCancelRun(std::uint64_t run) {
    ++fake::cancels; fake::calls.push_back("cancel");
    return fake::source->CancelRun(run,fake::monotonic);
}
DiagnosticObservation DiagnosticSnapshot() {
    auto out = fake::bridgeState; out.source = fake::source->Snapshot(); return out;
}
void DiagnosticDisable() {
    ++fake::disables; fake::calls.push_back("disable");
    fake::bridgeState.available = false; fake::bridgeState.disabled = true;
}
}
namespace mevr::engine::le2_diagnostic::input_context {
Context CaptureInputContext(std::uint32_t thread) {
    ++fake::captures; fake::lastThread = thread; fake::calls.push_back("capture");
    if (fake::onCapture) fake::onCapture();
    return fake::current;
}
bool SameContext(Context const& a, Context const& b) {
    return a.available && b.available && a.category != Category::Unavailable &&
        static_cast<State const&>(a) == static_cast<State const&>(b);
}
json::Value ContextJson(Context const& value) {
    auto out = json::Value::Object(); out["available"] = value.available;
    out["category"] = value.category == Category::Default ? "default" : "party-select";
    return out;
}
}
namespace mevr::engine::le2_control {
namespace {
using json::Value;
namespace wire = qa::control;
#include "engine/Le2InputControl.inc"
}
}
using mevr::engine::le2_control::NativeInput;
namespace {
unsigned checks{};
void Check(bool condition, char const* label) {
    ++checks; if (!condition) throw std::runtime_error(label);
}
mevr::hook::Context hooks;
mevr::qa::control::PadPulse Pulse(std::string category = "default") {
    return {std::move(category),{0x1000,0,0,0,0,0,0},100};
}
bool Publish(NativeInput& input, std::string& reason, std::string category = "default", std::uint64_t sequence = 1) {
    return input.Publish(Pulse(std::move(category)),sequence,77,2000,[]{ fake::calls.push_back("clock"); return fake::utc; },reason);
}
void Admission() {
    fake::Reset();
    NativeInput input; std::string reason;
    input.Observe(77); input.Tick(77);
    Check(fake::installs == 0 && fake::begins == 0,"read-only observation does not install or admit");
    Check(!Publish(input,reason) && reason == "diagnostic-hook-service-unavailable","missing hook service refuses");
    input.hooks = &hooks; fake::current.available = false;
    Check(!Publish(input,reason) && fake::installs == 0,"unavailable context never installs");
    fake::current.available = true; fake::current.category = context::Category::PartySelect;
    Check(!Publish(input,reason) && fake::installs == 0,"default cannot publish in party selection");
    fake::current.category = context::Category::Default;
    Check(!Publish(input,reason,"party-select") && fake::installs == 0,"party cannot publish in default");
    fake::installOk = false;
    Check(!Publish(input,reason) && reason == "diagnostic-input-installation-unavailable" && fake::begins == 0,"install failure refuses before run");
    fake::installOk = true; fake::calls.clear();
    Check(Publish(input,reason),"default publication succeeds");
    Check(fake::lastOptIn && fake::lastThread == 77,"explicit opt-in and verified thread forwarded");
    Check(fake::calls == std::vector<std::string>{"capture","install","capture","clock","begin","publish"},"revalidation and fresh deadline precede admission");
    Check(reason == "published-not-yet-proven-consumed" && input.admitted.has_value(),"publication preserves uncertainty and context");
    Check(fake::source->Snapshot().pulsePolls == 0 && input.Active(),"publication alone is not a poll");
    unsigned const installs = fake::installs;
    Check(!Publish(input,reason) && reason == "previous-pulse-release-pending" && fake::installs == installs,"outstanding publication cannot reinstall or replace");
    auto observed = input.Observe(77);
    Check(!observed.Bool("gameplayEffectProven",true) && !observed.Bool("vrRoutingProven",true),"observation never promotes publication to gameplay or VR proof");
    Check(observed.Get("bridge").Get("source").String("phase") == "published","phase serialized from actual source");
}
void InstallationRechecks() {
    fake::Reset(); NativeInput input; input.hooks = &hooks; std::string reason;
    fake::onInstall = []{ ++fake::current.controller.index; };
    Check(!Publish(input,reason) && reason == "input-context-changed-before-publication","install-time context change refuses");
    Check(fake::begins == 0 && fake::publishes == 0 && !input.admitted,"changed context cannot begin run");
    fake::onInstall = []{ fake::utc = 2000; };
    Check(!Publish(input,reason) && reason == "input-request-expired-before-publication","deadline equality after installation refuses");
    Check(fake::begins == 0 && input.token == 0,"expired request consumes no source token");
    fake::utc = 1999; fake::onInstall = {};
    Check(Publish(input,reason),"one millisecond before admission deadline is accepted");
    // UTC is the admission deadline. The bounded hold uses an independent
    // monotonic clock; it is not silently shortened to the wire's UTC age.
    Check(fake::source->Snapshot().expiresMs == 0 && fake::source->Snapshot().activationDeadlineMs == 5100,
          "publication starts bounded monotonic activation wait, not the hold");
}
void ContextCancellation() {
    fake::Reset(); NativeInput input; input.hooks = &hooks; std::string reason;
    Check(Publish(input,reason),"context test admitted");
    auto original = fake::current;
    Check(fake::Poll().pad.buttons == 0x1000,"real source poll writes pulse");
    ++fake::current.pawn.index; input.Tick(77);
    Check(!input.admitted && input.cancellationReason == "engine-input-context-changed","identity transition cancels context");
    Check(fake::source->Snapshot().phase == pad::Phase::DrainPending && !fake::source->Snapshot().runOpen,"cancel closes run with neutral pending");
    auto const publishes = fake::publishes; fake::current = original; input.Tick(77);
    Check(fake::publishes == publishes && !input.admitted,"restoring identity does not rearm");
    Check(!Publish(input,reason) && input.Active(),"context cancellation cannot bypass release");
    auto neutral = fake::Poll();
    Check(neutral.replace && neutral.pad == pad::Pad{},"safe poll emits neutral after context cancellation");
    auto const oldToken = input.token;
    fake::current.category = context::Category::PartySelect;
    Check(Publish(input,reason,"party-select",2),"explicit new party request after drain accepted");
    Check(input.token > oldToken && fake::source->Snapshot().sequence == 2,"new request receives strictly newer process token and supplied sequence");
    fake::bridgeState.available = false; input.Tick(77);
    Check(!input.admitted && input.cancellationReason == "bridge-unavailable","bridge unavailable cancels pending pulse");
}
void ExpiryAndConflict() {
    fake::Reset(); NativeInput input; input.hooks = &hooks; std::string reason;
    Check(Publish(input,reason),"expiry test admitted");
    fake::monotonic = 5100; input.Tick(77);
    Check(input.Active() && !Publish(input,reason),"elapsed time without a poll does not pretend neutral drain");
    auto neutral = fake::Poll();
    Check(neutral.replace && neutral.pad == pad::Pad{} && fake::source->Snapshot().pulsePolls == 0,"first late poll emits only neutral");
    input.Tick(77);
    Check(!input.admitted && !input.Active() && fake::source->Snapshot().end == pad::End::StartTimedOut,"start timeout neutral clears context without automatic publication");
    Check(Publish(input,reason,"default",2),"explicit request after expired neutral accepted");
    pad::Physical physical{pad::PhysicalState::Connected,{0,0,0,1,0,0,0},11};
    Check(!fake::Poll(physical).replace,"physical non-neutral input is not masked");
    input.Tick(77);
    Check(!Publish(input,reason) && fake::source->Snapshot().end == pad::End::PhysicalConflict,"physical conflict keeps release busy");
    Check(fake::Poll().pad == pad::Pad{},"physical conflict resolves with neutral only");
    input.Tick(77);
    Check(!input.admitted && fake::publishes == 2,"resolved conflict does not rearm");
}
void NativePollDelay() {
    for (std::uint64_t delay : {312ULL,1422ULL}) {
        fake::Reset(); NativeInput input; input.hooks = &hooks; std::string reason;
        Check(Publish(input,reason),"native-delay publication admitted");
        fake::monotonic += delay; input.Tick(77);
        auto first = fake::Poll();
        Check(first.replace && first.pad.buttons == 0x1000,"native delayed first poll delivers press before hold expiry");
        auto observed = input.Observe(77).Get("bridge").Get("source");
        Check(observed.String("phase") == "output-written" && observed.Number("requestedDurationMs") == 100 &&
              observed.String("activationDeadlineMs") == "5100" &&
              observed.String("expiresMs") == std::to_string(fake::monotonic+100),"activation and actual hold deadline remain distinct");
        Check(!Publish(input,reason),"delivered press still cannot be replaced before release");
        fake::monotonic += 100;
        auto release = fake::Poll(); input.Tick(77);
        Check(release.replace && release.pad == pad::Pad{} && !input.Active() &&
              fake::source->Snapshot().end == pad::End::Expired,"hold expires relative to first eligible poll and drains once");
    }
}
void RefusedAndClock() {
    fake::Reset(); NativeInput input; input.hooks = &hooks; std::string reason;
    fake::beginOk = false;
    Check(!Publish(input,reason) && reason == "input-run-admission-refused" && fake::publishes == 0,"run refusal does not publish");
    fake::beginOk = true; fake::publishOk = false;
    Check(!Publish(input,reason) && reason == "input-publication-refused","publication refusal reported");
    Check(!input.admitted && !fake::source->Snapshot().runOpen && input.cancellationReason == "publication-refused","publication refusal closes admitted run");
    fake::publishOk = true; fake::monotonic = std::numeric_limits<std::uint64_t>::max();
    Check(!Publish(input,reason) && !input.admitted,"monotonic duration overflow cannot retain admission");
    fake::monotonic = 100;
    Check(!Publish(input,reason),"clock cannot rewind after overflow cancellation sampled maximum time");
}
void ClockRegression() {
    fake::Reset(); NativeInput input; input.hooks = &hooks; std::string reason;
    Check(Publish(input,reason),"clock test valid first publication");
    fake::monotonic = 99;
    auto neutral = fake::Poll(); input.Tick(77);
    Check(neutral.replace && neutral.pad == pad::Pad{} && fake::source->Snapshot().end == pad::End::ClockRegressed,"clock regression produces neutral not active pulse");
    Check(!Publish(input,reason) && reason == "input-publication-refused" && !input.admitted,"new token cannot bypass clock regression");
}
void DisableAndJson() {
    fake::Reset(); std::string reason;
    {
        NativeInput input; input.hooks = &hooks;
        Check(Publish(input,reason),"disable test admitted");
        fake::Poll(); input.Disable();
        Check(fake::bridgeState.disabled && !input.admitted && input.Active(),"disable retains pending drain");
        Check(!fake::source->Snapshot().runOpen,"disable closes source run");
        Check(fake::Poll().replace,"disabled hooks can still drain neutral");
        Check(!Publish(input,reason),"disable cannot reinstall or rearm");
        auto const observed = input.Observe(77);
        Check(observed.Get("bridge").Get("source").String("phase") == "neutral-written","neutral write reported distinctly");
        Check(observed.String("cancellationReason") == "endpoint-disabled-or-closed","disable reason preserved");
        Check(observed.Get("bridge").String("evidenceScope").find("not game-consumption") != std::string::npos,"telemetry evidence scope explicit");
    }
    Check(fake::disables >= 2,"destruction repeats fail-closed disable safely");
    fake::Reset();
    { NativeInput idle; }
    Check(fake::installs == 0 && fake::disables == 1,"unused endpoint destructor never installs");
}
void DestructorAndTokenLifetime() {
    fake::Reset(); std::string reason; std::uint64_t token{};
    {
        NativeInput first; first.hooks = &hooks;
        Check(Publish(first,reason),"destructor test admitted"); token = first.token;
    }
    Check(fake::bridgeState.disabled && fake::source->Snapshot().phase == pad::Phase::DrainPending &&
        !fake::source->Snapshot().runOpen,"destruction closes outstanding run without pretending release");
    Check(fake::Poll().replace && fake::source->Snapshot().neutralPolls == 1,"destruction preserves subsequent neutral drain");
    // Reset ONLY fake external services to inspect the orchestration's static
    // process-lifetime token counter. Production does not reenable a disabled
    // bridge or restart an endpoint in the same process.
    fake::Reset(); NativeInput second; second.hooks = &hooks;
    Check(Publish(second,reason) && second.token > token,"another owner object cannot reuse an old process token");
    fake::current.available = false; second.Tick(77);
    Check(!second.admitted && second.cancellationReason == "engine-input-context-changed","unavailable fresh context cancels admission");
}
}
int main() {
    try {
        Admission(); InstallationRechecks(); ContextCancellation(); ExpiryAndConflict(); NativePollDelay(); RefusedAndClock(); ClockRegression(); DisableAndJson(); DestructorAndTokenLifetime();
        std::cout << checks << " input orchestration checks passed\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "check " << checks << " failed: " << error.what() << '\n'; return 1;
    }
}
