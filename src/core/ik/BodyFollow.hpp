// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"
#include "core/ik/Rigid.hpp"

#include <cstdint>

// Where the player's body is, under the headset (research/29): the head's pivot (the top of the
// neck, which turning or nodding the head doesn't move), the feet (a point on the floor plan that
// steps after the head once it leans too far, and settles back under it), and the body's yaw
// (ik::BodyYaw). All in tracking space (OpenXR LOCAL: meters, +X right, +Y up, -Z forward), so
// stick turns and stick walking (which move the view, not the room) never touch it; a recenter
// (which re-origins the room) or a tracking jump re-anchors it. Engine-agnostic; tested offline.
namespace mevr::ik {

// The head's pivot from the eyes' midpoint: `down` below and `back` behind it, in the head's own
// axes. Turning or nodding the head swings the eyes around it, so the shoulders hang from it, not
// from the eyes (a 90 degree head turn moves the eyes about 13 cm; the pivot stays).
struct NeckModel {
    double down = 0.10;
    double back = 0.09;
};
Vec3 HeadPivot(Vec3 eyes, Quat orientation, NeckModel const& neck = {});

struct FollowSettings {
    // The head's pivot may lean this far (meters, horizontally) from the feet before they step.
    double leanRadius = 0.20;
    // Within that, the feet settle under the pivot by this fraction per second (a held lean slowly
    // straightens up; a step's leftover lean goes away).
    double settlePerSecond = 0.3;
    // The pivot moving this far (meters) or the head's yaw this far (radians) in one update is a
    // tracking jump (a recenter we weren't told about, lost tracking): everything re-anchors.
    double jumpMeters = 0.5;
    double jumpRadians = 1.2;
};

enum class FollowAnchor : std::uint8_t {
    None,      // followed on from the last update
    Start,     // the first update (after Reset: the torso wasn't driven)
    Recenter,  // the recenter counter moved
    Jump,      // the head jumped (FollowSettings::jumpMeters / jumpRadians)
};
char const* Name(FollowAnchor anchor);

struct FollowInput {
    Vec3 pivot;               // HeadPivot of this update's head
    double headYaw = 0.0;     // ik::YawOf the head
    double facingYaw = 0.0;   // the yaw the body should turn to (the head's, or blended toward the hands)
    std::uint32_t recenters = 0;
    bool haveRecenters = false;  // false: the counter is unknown (jumps still re-anchor)
    double deltaSeconds = 0.0;
};

struct FollowState {
    Vec3 pivot;   // the head's pivot
    Vec3 feet;    // where the feet are: horizontal, at the pivot's height
    Vec3 lean;    // pivot - feet (horizontal): what the spine leans
    double yaw = 0.0;  // the body's yaw (ik::BodyYaw's convention)
    FollowAnchor anchored = FollowAnchor::None;  // this update re-anchored, and why
};

class BodyFollow {
public:
    explicit BodyFollow(FollowSettings const& settings = {}) : settings_(settings) {}
    FollowState Update(FollowInput const& in);
    // The next update re-anchors (feet under the head, body yaw at the head's).
    void Reset() { active_ = false; }
    void SetSettings(FollowSettings const& settings) { settings_ = settings; }
    bool Active() const { return active_; }
    FollowState const& Last() const { return last_; }

private:
    FollowSettings settings_;
    bool active_ = false;
    bool haveRecenters_ = false;
    std::uint32_t recenters_ = 0;
    double lastHeadYaw_ = 0.0;
    Vec3 lastPivot_;
    Vec3 feet_;
    BodyYaw yaw_;
    FollowState last_;
};

} // namespace mevr::ik
