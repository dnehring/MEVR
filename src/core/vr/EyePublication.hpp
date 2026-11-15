// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>

namespace mevr::vr {
// Present-thread bookkeeping for the image actually released to the runtime, independent
// of newer source/history contents. No GPU access or pose synthesis lives in this policy.
template<class Tag>
class EyePublication {
public:
    struct Entry {
        bool valid = false;
        bool tagged = false;
        std::uint64_t frame = 0;
        Tag tag{};
    };
    void BeginFrame() { if (++frame_ == 0) Reset(); }
    void Reset() { eyes_ = {}; ++frame_; }
    bool Complete(int eye, bool acquired, bool copied, bool released, Tag const* tag = nullptr) {
        if (eye < 0 || eye > 1 || !acquired) return false; // untouched previous image may still be used
        auto& entry = eyes_[static_cast<unsigned>(eye)];
        if (!copied || !released) {
            entry = {}; // attempted acquired image is no longer proof of a valid last release
            return false;
        }
        entry = {true, tag != nullptr, frame_, tag ? *tag : Tag{}};
        return true;
    }
    Entry const* Get(int eye) const {
        return eye >= 0 && eye < 2 && eyes_[static_cast<unsigned>(eye)].valid ? &eyes_[static_cast<unsigned>(eye)] : nullptr;
    }
    bool TaggedPair() const { return eyes_[0].valid && eyes_[1].valid && eyes_[0].tagged && eyes_[1].tagged; }
    bool CurrentPair() const {
        return eyes_[0].valid && eyes_[1].valid && eyes_[0].frame == frame_ && eyes_[1].frame == frame_;
    }
private:
    std::array<Entry, 2> eyes_{};
    std::uint64_t frame_ = 1;
};
// Ownership is independent of content/history validity. A failed wait or release
// leaves the acquired-image queue uncertain: never acquire another image from
// that handle until its owner actually creates a new swapchain. Recenter and
// source-history resets must not call Recreated().
class SwapchainOwnership {
public:
    bool MayAcquire() const { return state_ == State::Idle; }
    bool MayRelease() const { return state_ == State::Waited; }
    void Acquired() { state_ = MayAcquire() ? State::Acquired : State::Quarantined; }
    bool WaitCompleted(bool success) {
        state_ = success && state_ == State::Acquired ? State::Waited : State::Quarantined;
        return MayRelease();
    }
    bool ReleaseCompleted(bool success) {
        state_ = success && MayRelease() ? State::Idle : State::Quarantined;
        return MayAcquire();
    }
    void Recreated() { state_ = State::Idle; }
private:
    enum class State { Idle, Acquired, Waited, Quarantined };
    State state_ = State::Idle;
};
} // namespace mevr::vr
