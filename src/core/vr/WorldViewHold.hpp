// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace mevr::vr::world_view {

// A remembered room view belongs to one world/controller, not to the GUI that later uses it.
// Tokens are compared only; this policy never reads engine objects.
struct Context {
    std::uintptr_t world = 0;
    std::uintptr_t controller = 0;
    bool operator==(Context const&) const = default;
    bool Valid() const { return world != 0 && controller != 0; }
};

class Reference {
public:
    // True means the caller must discard its saved camera, tracking frame and use target.
    bool Observe(Context context) {
        if (context == context_) return false;
        context_ = context;
        remembered_ = false;
        return true;
    }
    // Mode 0 also occurs during camera transitions. Only the actual on-foot camera may teach a room view.
    bool Remember(int mode, bool footCamera, bool held) {
        if (!context_.Valid() || mode != 0 || !footCamera || held) return false;
        remembered_ = true;
        return true;
    }
    bool Available() const { return context_.Valid() && remembered_; }

private:
    Context context_;
    bool remembered_ = false;
};

} // namespace mevr::vr::world_view
