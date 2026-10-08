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
    // The session the actions belong to, whoever owns it (our Session, or Halcyon's renderer).
    struct Target {
        XrInstance instance = XR_NULL_HANDLE;
        XrSession session = XR_NULL_HANDLE;
        XrSpace base = XR_NULL_HANDLE;  // poses are located in this space
        std::uint32_t instanceGeneration = 0;
        std::uint32_t sessionGeneration = 0;
    };
    static Target From(Session const& session);

    // Creates the action set and attaches it to the session. Call once per session.
    bool Create(Target const& target);
    bool Create(Session& session) { return Create(From(session)); }
    // Destroys what still exists: spaces die with their session and the action set with its
    // instance, so this only calls OpenXR for objects whose parent is still alive.
    void Release(Target const& target);
    void Release(Session& session) { Release(From(session)); }
    // True if created for this session (and so usable).
    bool Matches(Target const& target) const;
    bool Matches(Session const& session) const { return Matches(From(session)); }

    // Syncs the actions and fills `out` (poses located at `time` in the target's base space).
    // `out.active` is false when the session doesn't have input focus.
    void Sync(Target const& target, XrTime time, vr::controllers::State& out);
    void Sync(Session& session, XrTime time, vr::controllers::State& out) { Sync(From(session), time, out); }

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
