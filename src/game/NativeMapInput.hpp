// SPDX-License-Identifier: MIT
#pragma once
#include "game/GamepadMapping.hpp"

namespace mevr::game::gamepad {
// The shared native-map pad path keeps native ship/reticle steering. Only triggers are
// withheld across map/orbit, menu, tracking, session and recenter boundaries. The host
// supplies semantic contexts from GameAdapter and its existing input/session generation.
class NativeMapInput {
public:
    void Reset() { have_ = false; latch_ = {}; }
    void Apply(bool mapActive, bool orbital, std::uint64_t generation, bool inputAllowed, XINPUT_GAMEPAD& pad) {
        if (!mapActive) { Reset(); return; }
        if (!have_ || orbital_ != orbital || generation_ != generation || !inputAllowed) {
            latch_.leftTrigger = latch_.rightTrigger = true;
        }
        have_ = true;
        orbital_ = orbital;
        generation_ = generation;
        if (!inputAllowed) {
            pad.bLeftTrigger = pad.bRightTrigger = 0;
            return; // A menu's already-neutral pad is not evidence of a physical release.
        }
        ApplyCloseLatch(latch_, pad);
    }
private:
    CloseLatch latch_;
    bool have_ = false, orbital_ = false;
    std::uint64_t generation_ = 0;
};

// The XR owner publishes once per rendered frame; an XInput poll consumes the lease
// under the host's pad mutex, never querying XR off-thread. Revocation resets the release
// latch even when no pad poll occurred during the interruption. A host-filtered neutral
// pad must never count as release: Apply precedes the other owners' final pad filter.
class NativeMapInputLease {
public:
    static constexpr std::uint64_t kMaxAgeMs = 250;
    void Reset() { timeMs_ = 0; input_.Reset(); }
    void Publish(bool mapActive, bool orbital, std::uint64_t generation, bool allowed, std::uint64_t nowMs) {
        if (!mapActive || !allowed || nowMs == 0) { Reset(); return; }
        if (!Fresh(nowMs) || orbital != orbital_ || generation != generation_) input_.Reset();
        timeMs_ = nowMs;
        orbital_ = orbital;
        generation_ = generation;
    }
    void Apply(bool mapActive, bool orbital, std::uint64_t generation, bool ownersAllow,
        std::uint64_t nowMs, XINPUT_GAMEPAD& pad) {
        bool const allowed = ownersAllow && Fresh(nowMs) && orbital == orbital_ && generation == generation_;
        input_.Apply(mapActive, orbital, generation, allowed, pad);
        if (!mapActive || !allowed) timeMs_ = 0; // fresh publication required after loss/ownership change
    }
private:
    bool Fresh(std::uint64_t nowMs) const {
        return timeMs_ != 0 && nowMs >= timeMs_ && nowMs - timeMs_ <= kMaxAgeMs;
    }
    NativeMapInput input_;
    std::uint64_t timeMs_ = 0, generation_ = 0;
    bool orbital_ = false;
};
} // namespace mevr::game::gamepad
