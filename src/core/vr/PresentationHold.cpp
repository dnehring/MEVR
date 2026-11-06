// SPDX-License-Identifier: MIT
#include "core/vr/PresentationHold.hpp"

namespace mevr::vr::presentation {

bool FrontEnd::Update(int gameMode) {
    if (gameMode >= 0) {
        gameplayFrames_ = gameMode <= 4 ? gameplayFrames_ + 1 : 0;
        if (gameplayFrames_ >= kGameplayFrames) {
            starting_ = false;  // gameplay (not the title's one-frame 0 between its camera flight and the main menu)
        }
        last_ = gameMode;
        if (gameMode == 6 || gameMode == 7) {
            titled_ = true;
        }
        if (gameMode != 8) {
            lastOther_ = gameMode;
        }
        // The start-up's front end: everything but gameplay. After the menus: the load's movie mode.
        return (starting_ && gameMode >= 5) || (gameMode == 8 && lastOther_ == 7);
    }
    if (lastOther_ == 7) {
        starting_ = false;  // a load from the menus (held like the menu): its cinematics are the game's
    }
    return starting_ || lastOther_ == 7;
}

bool VrEntry::Hold(bool plainVr, std::uint64_t nowMs, std::uint64_t holdMs) {
    if (!plainVr) {
        if (waiting_) {
            ++covered_;
        }
        waiting_ = false;
        have_ = true;
        shownPlain_ = false;
        return false;
    }
    if (!have_ || shownPlain_) {
        have_ = true;
        shownPlain_ = true;
        return false;
    }
    if (!waiting_) {
        waiting_ = true;
        sinceMs_ = nowMs;
    }
    if (nowMs < sinceMs_ || nowMs - sinceMs_ >= holdMs) {
        waiting_ = false;
        shownPlain_ = true;
        return false;
    }
    return true;
}

} // namespace mevr::vr::presentation
