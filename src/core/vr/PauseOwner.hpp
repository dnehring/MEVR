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
