// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace mevr::vr {
// The saved pause bit belongs to one world. A failed restore retains that owner for retry;
// a caller must never substitute the new current world for the captured one.
class PauseOwner {
public:
    std::uintptr_t Owner() const { return owner_; }
    void Capture(std::uintptr_t owner, bool previous) { owner_ = owner; previous_ = previous; }
    // Capture only after the adapter observes the requested state. A refused native pause
    // owns nothing; an existing owner must be restored before another can replace it.
    template<class Pause>
    bool Acquire(std::uintptr_t owner, bool previous, Pause&& pause) {
        if (owner == 0 || owner_ != 0 || !pause(owner)) return false;
        Capture(owner, previous);
        return true;
    }
    void Forget() { owner_ = 0; }
    template<class Restore>
    bool Release(Restore&& restore) {
        if (owner_ == 0) return true;
        if (!restore(owner_, previous_)) return false;
        Forget();
        return true;
    }
private:
    std::uintptr_t owner_ = 0;
    bool previous_ = false;
};
} // namespace mevr::vr
