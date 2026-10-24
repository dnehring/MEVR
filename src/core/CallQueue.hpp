// SPDX-License-Identifier: MIT
#pragma once

// Calls made off the game thread, kept for it (research/36): UnrealScript's VM keeps its state in process-wide
// globals (GPropAddr, GProperty, GRuntimeUCFlags), so a ProcessEvent on another thread races the game thread's
// script and can hand it another thread's stack address (the 2026-10-23 crash). Halcyon's Present-thread setters
// (hides, the Mako turret) queue their call here; the host runs them at the start of the next engine tick.
//
// Bounded: when full, the oldest call goes (counted). Any thread pushes; one thread takes. Header-only and
// game-independent (the offline tests use it as is).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace mevr::call_queue {

inline constexpr std::size_t kMaxParms = 64;

struct Call {
    void* object = nullptr;
    void* function = nullptr;
    void const* objectClass = nullptr;  // the object's class when queued: a different one at the drain means the
                                        // object died and its memory went to another
    std::uint32_t size = 0;             // bytes of `parms` used (the function's ParmsSize)
    std::uint32_t pad = 0;              // keeps `parms` 8-byte aligned
    std::uint8_t parms[kMaxParms]{};
};

template <std::size_t N>
class Queue {
    static_assert(N > 0);

public:
    // False (nothing queued) if the parameter block doesn't fit.
    bool Push(void* object, void* function, void const* objectClass, void const* parms, std::size_t size) {
        if (size > kMaxParms || (size > 0 && parms == nullptr)) {
            std::lock_guard lock(mutex_);
            ++refused_;
            return false;
        }
        std::lock_guard lock(mutex_);
        if (count_ == N) {
            head_ = (head_ + 1) % N;
            --count_;
            ++dropped_;
        }
        Call& c = items_[(head_ + count_) % N];
        c.object = object;
        c.function = function;
        c.objectClass = objectClass;
        c.size = static_cast<std::uint32_t>(size);
        std::memset(c.parms, 0, sizeof(c.parms));
        if (size > 0) {
            std::memcpy(c.parms, parms, size);
        }
        ++count_;
        ++pushed_;
        return true;
    }

    // Everything queued, oldest first, into `out` (room for N); the queue is then empty.
    std::size_t TakeAll(Call* out) {
        std::lock_guard lock(mutex_);
        std::size_t const n = count_;
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = items_[(head_ + i) % N];
        }
        head_ = 0;
        count_ = 0;
        return n;
    }

    std::uint64_t Pushed() const {
        std::lock_guard lock(mutex_);
        return pushed_;
    }
    // Pushed out by newer calls while full.
    std::uint64_t Dropped() const {
        std::lock_guard lock(mutex_);
        return dropped_;
    }
    // Refused: a parameter block too big.
    std::uint64_t Refused() const {
        std::lock_guard lock(mutex_);
        return refused_;
    }
    std::size_t Size() const {
        std::lock_guard lock(mutex_);
        return count_;
    }

private:
    mutable std::mutex mutex_;
    Call items_[N];
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::uint64_t pushed_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t refused_ = 0;
};

} // namespace mevr::call_queue
