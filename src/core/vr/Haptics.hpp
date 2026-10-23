// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

// One controller's vibration, as the host plays it (plugin/HalcyonBridge, the mod's requests through
// HostServices hapticPulse / hapticPulseHz / hapticContinuous): one-shot pulses, and a continuous
// buzz the mod keeps alive by asking again each tick. Pure (no OpenXR): Step says what to send.
//
// OpenXR has no "vibrate until told otherwise" a mod could forget to stop: a vibration has a
// duration, and a new xrApplyHapticFeedback on the same action and hand interrupts and replaces the
// one playing (the spec's "must"). So the continuous buzz is played as short chunks (kChunkMs),
// re-sent before each runs out or when its amplitude or frequency changes, and only until the
// mod's request expires: a mod that stalls or unloads goes quiet within its expiry plus a chunk.
// A pulse interrupts the buzz, which resumes when the pulse is over.
namespace mevr::vr::haptics {

inline constexpr std::uint64_t kChunkMs = 100;       // one continuous chunk at most
inline constexpr std::uint64_t kRefreshLeadMs = 40;  // re-sent when this little of a chunk is left
inline constexpr float kMaxContinuousSeconds = 0.5f; // the longest a request may keep it going
inline constexpr float kAmplitudeStep = 0.02f;       // smaller changes wait for the next chunk
inline constexpr float kFrequencyStep = 5.0f;        // Hz

struct Command {
    enum class Kind { None, Play, Stop };
    Kind kind = Kind::None;
    float seconds = 0.0f;
    float amplitude = 0.0f;
    float frequency = 0.0f;  // Hz; 0 = the runtime's choice
};

class Channel {
public:
    // A one-shot pulse; a newer one replaces one not yet played.
    void Pulse(float seconds, float amplitude, float frequency);
    // Keep buzzing at `amplitude` / `frequency` for `seconds` from `nowMs` (clamped to
    // kMaxContinuousSeconds); amplitude 0 stops it.
    void Continuous(float amplitude, float frequency, float seconds, std::uint64_t nowMs);
    // Once per frame: what to send to the runtime now.
    Command Step(std::uint64_t nowMs);
    // Forget everything (the mod unloaded, a new session); the next Step stops a running buzz.
    void Clear();
    bool ContinuousActive(std::uint64_t nowMs) const { return amplitude_ > 0.0f && nowMs < until_; }

private:
    bool pulsePending_ = false;
    Command pulse_;
    std::uint64_t busyUntil_ = 0;   // a pulse plays until then
    float amplitude_ = 0.0f;        // the continuous request
    float frequency_ = 0.0f;
    std::uint64_t until_ = 0;
    std::uint64_t playingUntil_ = 0;  // the continuous chunk sent last runs until then (0: none)
    float playingAmplitude_ = 0.0f;
    float playingFrequency_ = 0.0f;
    bool stopOwed_ = false;  // Clear() while a chunk played
};

} // namespace mevr::vr::haptics
