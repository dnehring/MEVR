// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Floor.hpp"

#include <openxr/openxr.h>

#include <optional>
#include <string>

namespace mevr::xr {

// The head's height above the floor, on someone else's session (Halcyon's): a floor reference space
// and a VIEW space of our own, located against each other each frame. Halcyon's app space is LOCAL
// (origin at the head when recentered), so it knows no floor.
//
// Which floor: LOCAL_FLOOR if the runtime lists it (core in OpenXR 1.1, or XR_EXT_local_floor;
// Halcyon makes a 1.0 instance without that extension, so normally not), else STAGE (core since
// 1.0, optional per runtime). Neither depends on Halcyon's recenter, which only re-origins its own
// LOCAL-based app space. Render thread only, like the session.
class FloorSpace {
public:
    // After the session exists (before its first frame). Logs what it chose. False: no floor space.
    bool Create(XrSession session);
    // A new session: the old spaces died with the old session (Halcyon destroys a session before it
    // makes another), so their handles are only forgotten here, never destroyed.
    void Forget();
    // The VIEW origin's height above the floor at `time` (meters), if located.
    std::optional<float> EyeHeight(XrTime time) const;
    vr::floor::Source Source() const { return source_; }

private:
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace floor_ = XR_NULL_HANDLE;
    XrSpace view_ = XR_NULL_HANDLE;
    vr::floor::Source source_ = vr::floor::Source::None;
};

} // namespace mevr::xr
