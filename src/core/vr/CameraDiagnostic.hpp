// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>

// Volatile observations, never settings. The host owns the mailbox; the hot-reloaded mod publishes through
// HostServices. No file access or engine pointers. Time is supplied by the caller for deterministic tests.
namespace mevr::vr::camera_diagnostic {
struct Sample {
    double forward = 0.0, right = 0.0, up = 0.0;
    std::uint64_t timeMs = 0, sequence = 0;
    bool valid = false;
};
class Store {
public:
    void Publish(double forward, double right, double up, std::uint64_t nowMs) {
        std::lock_guard lock(mutex_);
        sample_ = {forward, right, up, nowMs, ++sequence_,
                   std::isfinite(forward) && std::isfinite(right) && std::isfinite(up)};
    }
    Sample Latest(std::uint64_t nowMs, std::uint64_t afterMs = 0) {
        std::lock_guard lock(mutex_);
        Sample result = sample_;
        // Samples arrive every five seconds. Reject clock reversal, stale data and pre-recenter readings.
        result.valid = result.valid && result.timeMs > afterMs && nowMs >= result.timeMs && nowMs - result.timeMs <= 15000;
        return result;
    }
    void Reset() {
        std::lock_guard lock(mutex_);
        sample_ = {};
    }
private:
    std::mutex mutex_;
    Sample sample_;
    std::uint64_t sequence_ = 0;
};
inline Store gCameraDiagnostic;
} // namespace mevr::vr::camera_diagnostic
