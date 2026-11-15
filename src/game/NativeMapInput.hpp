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
} // namespace mevr::game::gamepad
