#pragma once

#include "core/vr/Controllers.hpp"
#include "core/xr/Session.hpp"

#include <array>
#include <cstdint>

namespace mevr::xr {

// OpenXR actions for two motion controllers: buttons, sticks, triggers, grips and the grip/aim
// poses. Suggested bindings: Oculus Touch (Quest, Rift, Steam Link), Valve Index, and the KHR
// simple controller. Render thread only, like Session.
class Input {
public:
    // Creates the action set and attaches it to the session. Call once per session.
    bool Create(Session& session);
    // Destroys what still exists: spaces die with their session and the action set with its
    // instance, so this only calls OpenXR for objects whose parent is still alive.
    void Release(Session& session);
    // True if created for this session (and so usable).
    bool Matches(Session const& session) const;

    // Syncs the actions and fills `out` (poses located at `time` in LOCAL space). `out.active`
    // is false when the session doesn't have input focus.
    void Sync(Session& session, XrTime time, vr::controllers::State& out);

private:
    enum Hand { kLeft = 0, kRight = 1 };

    XrActionSet set_ = XR_NULL_HANDLE;
    XrAction trigger_ = XR_NULL_HANDLE;
    XrAction squeeze_ = XR_NULL_HANDLE;
    XrAction stick_ = XR_NULL_HANDLE;
    XrAction stickClick_ = XR_NULL_HANDLE;
    XrAction primary_ = XR_NULL_HANDLE;
    XrAction secondary_ = XR_NULL_HANDLE;
    XrAction menu_ = XR_NULL_HANDLE;
    XrAction gripPose_ = XR_NULL_HANDLE;
    XrAction aimPose_ = XR_NULL_HANDLE;
    std::array<XrPath, 2> hands_{};
    std::array<XrSpace, 2> gripSpaces_{};
    std::array<XrSpace, 2> aimSpaces_{};
    std::uint32_t sessionGeneration_ = 0;
    std::uint32_t instanceGeneration_ = 0;
    bool created_ = false;
    bool loggedFocus_ = false;
};

} // namespace mevr::xr
