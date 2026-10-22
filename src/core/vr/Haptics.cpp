#include "core/vr/Haptics.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::haptics {

void Channel::Pulse(float seconds, float amplitude, float frequency) {
    if (!(seconds > 0.0f) || !(amplitude > 0.0f)) {
        return;
    }
    pulse_.kind = Command::Kind::Play;
    pulse_.seconds = std::min(seconds, 1.0f);
    pulse_.amplitude = std::min(amplitude, 1.0f);
    pulse_.frequency = frequency > 0.0f ? frequency : 0.0f;
    pulsePending_ = true;
}

void Channel::Continuous(float amplitude, float frequency, float seconds, std::uint64_t nowMs) {
    if (!(amplitude > 0.0f) || !(seconds > 0.0f)) {
        amplitude_ = 0.0f;
        until_ = 0;
        return;
    }
    amplitude_ = std::min(amplitude, 1.0f);
    frequency_ = frequency > 0.0f ? frequency : 0.0f;
    until_ = nowMs + static_cast<std::uint64_t>(std::lround(std::min(seconds, kMaxContinuousSeconds) * 1000.0f));
}

Command Channel::Step(std::uint64_t nowMs) {
    if (pulsePending_) {
        pulsePending_ = false;
        busyUntil_ = nowMs + static_cast<std::uint64_t>(std::ceil(pulse_.seconds * 1000.0f));
        playingUntil_ = 0;  // the pulse replaced the buzz
        stopOwed_ = false;
        return pulse_;
    }
    if (nowMs < busyUntil_) {
        return {};
    }
    if (ContinuousActive(nowMs)) {
        stopOwed_ = false;
        // Re-sent when the chunk playing runs out before the request does, or the buzz changed.
        bool const refresh = playingUntil_ == 0 || (playingUntil_ < until_ && nowMs + kRefreshLeadMs >= playingUntil_) ||
            std::abs(amplitude_ - playingAmplitude_) > kAmplitudeStep || std::abs(frequency_ - playingFrequency_) > kFrequencyStep;
        if (!refresh) {
            return {};
        }
        std::uint64_t const chunk = std::max<std::uint64_t>(std::min(kChunkMs, until_ - nowMs), 1);
        playingUntil_ = nowMs + chunk;
        playingAmplitude_ = amplitude_;
        playingFrequency_ = frequency_;
        Command command;
        command.kind = Command::Kind::Play;
        command.seconds = static_cast<float>(chunk) / 1000.0f;
        command.amplitude = amplitude_;
        command.frequency = frequency_;
        return command;
    }
    bool const playing = playingUntil_ > nowMs || stopOwed_;
    playingUntil_ = 0;
    stopOwed_ = false;
    if (playing) {
        Command command;
        command.kind = Command::Kind::Stop;
        return command;
    }
    return {};
}

void Channel::Clear() {
    stopOwed_ = playingUntil_ != 0;
    pulsePending_ = false;
    busyUntil_ = 0;
    amplitude_ = 0.0f;
    until_ = 0;
    playingUntil_ = 0;
}

} // namespace mevr::vr::haptics
