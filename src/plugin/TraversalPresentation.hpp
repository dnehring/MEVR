// SPDX-License-Identifier: MIT
#pragma once
#include "core/vr/Traversal.hpp"
#include "game/GamepadMapping.hpp"

namespace mevr::host::traversal_presentation {
// Kept under the bridge mutex. Invalid or expired publications never replace a live
// compensation with zero: the view could still retain its last authored-yaw offset.
class State {
public:
    bool Set(vr::traversal::Presentation const* value, std::uint64_t now,
        std::uint32_t recenters, std::uint32_t sessions) {
        if (!value) { *this = {}; return true; }
        if (!vr::traversal::PresentationFresh(*value, now, recenters, sessions)) return false;
        value_ = *value;
        invalidated_ = false;
        return true;
    }
    bool Active() const { return (value_.flags & vr::traversal::kPresentationActive) != 0; }
    void Invalidate() { invalidated_ = true; }
    void Apply(SHORT& x, SHORT& y, std::uint64_t now, std::uint32_t recenters,
        std::uint32_t sessions, bool focused) const {
        if (!Active()) return;
        if (!focused || invalidated_ || !vr::traversal::PresentationFresh(value_, now, recenters, sessions)) {
            x = y = 0;
            return;
        }
        game::gamepad::RotateStick(x, y, -value_.yawOffsetUU);
    }
private:
    vr::traversal::Presentation value_{};
    bool invalidated_ = false;
};
} // namespace mevr::host::traversal_presentation
