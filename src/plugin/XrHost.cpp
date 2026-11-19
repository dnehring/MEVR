// SPDX-License-Identifier: MIT
// The host's side of the renderer's OpenXR session (plugin/XrHost.hpp): moved out of plugin/HalcyonBridge.cpp unchanged so
// LE1's and ME2's bridges share it.

#include "plugin/XrHost.hpp"

#include "core/Log.hpp"
#include "core/vr/Haptics.hpp"
#include "core/vr/PoseRecording.hpp"
#include "core/xr/Floor.hpp"
#include "core/xr/Input.hpp"
#include "plugin/TestLoop.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <optional>

namespace mevr::host::xr_host {
namespace {

xr::Input gInput;
xr::Input::Target gTarget;  // set before the renderer's frames start, then its frame thread
bool gInputFailed = false;

// Each hand's vibration (pulses and the continuous contact buzz, core/vr/Haptics), stepped after the action sync on the
// frame thread.
std::mutex gHapticMutex;  // gHaptics
std::array<vr::haptics::Channel, 2> gHaptics;
bool gLoggedHaptic = false;            // frame thread
unsigned gHapticFailures = 0;          // frame thread: in a row
constexpr unsigned kHapticFailuresToGiveUp = 5;
bool gHapticGaveUp = false;            // frame thread: for this session

// The head above the floor (core/xr/Floor): measured each frame, read by the mod (HostServices::floor).
xr::FloorSpace gFloor;        // at creation, then frame thread
std::mutex gFloorMutex;       // gFloorState
vr::floor::State gFloorState;
std::atomic<std::uint32_t> gRecenters{0};
std::atomic<std::uint32_t> gSessions{0};
bool gLoggedFloor = false;  // frame thread

void ApplyPendingHaptics() {
    std::uint64_t const now = GetTickCount64();
    for (int hand : {0, 1}) {
        vr::haptics::Command command;
        {
            std::lock_guard lock(gHapticMutex);
            command = gHaptics[hand].Step(now);
        }
        using Kind = vr::haptics::Command::Kind;
        if (command.kind == Kind::None || gHapticGaveUp) {
            continue;
        }
        bool const ok = command.kind == Kind::Play
            ? gInput.Vibrate(gTarget, hand, command.seconds, command.amplitude, command.frequency)
            : gInput.StopVibration(gTarget, hand);
        if (command.kind == Kind::Play && !gLoggedHaptic) {
            gLoggedHaptic = true;
            log::Info("halcyon bridge: first controller vibration ({} hand, {:.0f} ms at {:.2f}, {}): {}",
                hand == 0 ? "left" : "right", command.seconds * 1000.0f, command.amplitude,
                command.frequency > 0.0f ? std::format("{:.0f} Hz", command.frequency) : std::string("the runtime's frequency"),
                ok ? "applied" : "refused (no vibration action, or the runtime said no)");
        }
        gHapticFailures = ok ? 0 : gHapticFailures + 1;
        if (gHapticFailures >= kHapticFailuresToGiveUp) {
            gHapticGaveUp = true;
            log::Warn("halcyon bridge: {} controller vibrations in a row failed: no more vibration this session",
                gHapticFailures);
        }
    }
}

// MeasureFloor while a replay stands in for the head (plugin/TestLoop): the floor sample follows the replayed head, so a
// recorded crouch, lean or reach lowers the eyes over the floor for every reader (posture, floor lock, body scale, holsters,
// VR cover), not the runtime's still head's (research/le2-vrik.md "The replay STAGE-eye gap"). The same publication as
// MeasureFloor's with the replay's sample; out of line, so MeasureFloor itself is as it was without a replay.
__declspec(noinline) void MeasureReplayedFloor(XrTime time) {
    std::optional<float> const measured = gFloor.EyeHeight(time);
    vr::recording::FloorSample live;
    live.valid = measured.has_value();
    live.eyeHeight = measured.value_or(0.0f);
    live.source = gFloor.Source();
    vr::recording::FloorSample sample = live;
    testloop::ReplayFloor(time, live, gFloor.HeadHeightIn(gTarget.base, time), sample);  // false: `sample` stays the live one
    {
        std::lock_guard lock(gFloorMutex);
        gFloorState.valid = sample.valid;
        if (sample.valid) {
            gFloorState.eyeHeight = sample.eyeHeight;
        }
        gFloorState.source = sample.source;
        gFloorState.recenters = gRecenters.load(std::memory_order_relaxed);
        gFloorState.sessions = gSessions.load(std::memory_order_relaxed);
        ++gFloorState.sequence;
        gFloorState.timeMs = GetTickCount64();
    }
}

// The floor height for this frame (frame thread, after the sync).
void MeasureFloor(XrTime time) {
    if (testloop::Replaying()) {
        MeasureReplayedFloor(time);
        return;
    }
    std::optional<float> const height = gFloor.EyeHeight(time);
    {
        std::lock_guard lock(gFloorMutex);
        gFloorState.valid = height.has_value();
        if (height) {
            gFloorState.eyeHeight = *height;
        }
        gFloorState.source = gFloor.Source();
        gFloorState.recenters = gRecenters.load(std::memory_order_relaxed);
        gFloorState.sessions = gSessions.load(std::memory_order_relaxed);
        ++gFloorState.sequence;
        gFloorState.timeMs = GetTickCount64();
    }
    if (height && !gLoggedFloor) {
        gLoggedFloor = true;
        log::Info("floor: first sample: the head is {:.2f} m above the floor ({})", *height, vr::floor::Name(gFloor.Source()));
    }
}

} // namespace

bool OnSessionCreated(XrInstance instance, XrSession session) noexcept {
    if (gInput.Matches(gTarget)) {
        gInput.Release(gTarget);  // a new session (the renderers make one per process, but be safe)
    }
    gTarget.instance = instance;
    gTarget.session = session;
    ++gTarget.instanceGeneration;
    ++gTarget.sessionGeneration;
    gInputFailed = !gInput.Create(gTarget);
    if (gInputFailed) {
        log::Error("halcyon bridge: controller actions failed; motion controllers unavailable");
    }
    // A new session: its own floor space, vibration from scratch.
    gFloor.Create(gTarget.session);
    gSessions.fetch_add(1, std::memory_order_relaxed);
    gLoggedFloor = false;
    gHapticGaveUp = false;
    gHapticFailures = 0;
    {
        std::lock_guard lock(gHapticMutex);
        for (auto& channel : gHaptics) {
            channel = {};
        }
    }
    return !gInputFailed;
}

void OnFrameBegun(XrSession session, XrSpace base, XrTime time, vr::controllers::State& state) noexcept {
    if (session == XR_NULL_HANDLE || session != gTarget.session) {
        return;
    }
    gTarget.base = base;  // recentering replaces it
    if (!gInputFailed) {
        gInput.Sync(gTarget, time, state);
        ApplyPendingHaptics();  // the mod's holster pulses and contact buzz (HostServices v9/v10)
    }
    MeasureFloor(time);
}

bool InputReady() noexcept {
    return !gInputFailed && gTarget.session != XR_NULL_HANDLE;
}

bool InputFocused() noexcept {
    return !gInputFailed && gInput.Focused();
}

std::string InteractionProfile(int hand) noexcept {
    if (gInputFailed) {
        return {};
    }
    return gInput.InteractionProfile(gTarget, hand);
}

void NoteRecentered() noexcept {
    gRecenters.fetch_add(1, std::memory_order_relaxed);
}

void RequestHaptic(int hand, float seconds, float amplitude, float frequencyHz) noexcept {
    if ((hand != 0 && hand != 1) || !(seconds > 0.0f) || !(amplitude > 0.0f)) {
        return;
    }
    std::lock_guard lock(gHapticMutex);
    gHaptics[hand].Pulse(seconds, amplitude, frequencyHz);
}

void RequestContinuousHaptic(int hand, float amplitude, float frequencyHz, float seconds) noexcept {
    if (hand != 0 && hand != 1) {
        return;
    }
    std::lock_guard lock(gHapticMutex);
    gHaptics[hand].Continuous(amplitude, frequencyHz, seconds, GetTickCount64());
}

void ForgetHaptics() noexcept {
    std::lock_guard lock(gHapticMutex);
    for (auto& channel : gHaptics) {
        channel.Clear();
    }
}

void Floor(vr::floor::State* out) noexcept {
    if (out == nullptr) {
        return;
    }
    std::lock_guard lock(gFloorMutex);
    *out = gFloorState;
}

} // namespace mevr::host::xr_host
