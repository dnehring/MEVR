// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"
#include "core/ik/Rigid.hpp"

#include <cstdint>
#include <numbers>

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

// The legs under a moving body (research/29 "The legs keep the run", 2026-11-04). The game's run animations point
// where the pawn faces: an explore-mode Shepard faces where he runs, a combat one strafes and backpedals against his
// own facing. Turning the hips with the body toward the view while the pawn moves shows the legs running one way while
// the body goes another, the feet skating (a moonwalk). So while the pawn moves the legs keep the pawn's facing and only
// the spine twists toward the view, up to `twistLimitRadians` from the legs; past that the whole body (the legs too)
// turns by the rest, so the shoulders still face the player's way (the arms hang from them). Standing, the old rule
// holds (TorsoGoal: the hips take whatever the spine's 30 degrees can't); `weight` blends between the two. Running
// backward (the facing near 180 degrees from the legs) the twist keeps the side it reached first until the facing is
// `sideBandRadians` past 180, so noise around 180 never flips the legs from one side to the other.
struct LegsSettings {
    bool on = true;                                // [Hands] legsFollowMotion
    double twistLimitRadians = 60.0 * std::numbers::pi / 180.0;  // [Hands] legsTwistDeg
    double moveSpeed = 60.0;       // units a second: the pawn counts as moving above this (LE1's walk is 130, its run 400)...
    double stopSpeed = 30.0;       // ...and as standing again below this
    double blendPerSecond = 4.0;   // the weight's change a second (a quarter second from standing to moving and back)
    double sideBandRadians = 30.0 * std::numbers::pi / 180.0;
};

struct LegsInput {
    // From the legs' facing (the pawn's: where its run animation points) to the shoulders' wanted facing, radians about
    // the body's up, in TorsoPose's sense (YawBetween).
    double facing = 0.0;
    double speed = 0.0;         // the pawn's horizontal speed, units a second (not finite: unknown, counted as standing)
    double deltaSeconds = 0.0;
};

struct LegsState {
    bool moving = false;
    bool started = false;        // this update began a move (standing -> moving)
    double weight = 0.0;         // 0: the standing rule; 1: the legs keep the pawn's facing
    double facing = 0.0;         // LegsInput::facing unwrapped: the side kept (radians, |facing| <= pi + the side band)
    double legsTurn = 0.0;       // the whole body's turn that leaves the spine at most the limit from the legs (radians)
    double spineFromLegs = 0.0;  // what the spine twists from the legs then (facing - legsTurn, radians)
};

// The rule alone: the body's turn for shoulders `facing` radians from the legs with the spine twisting at most `limit`
// (0 within the limit, the rest beyond it, the sign of `facing`).
double LegsTurn(double facing, double limit);

class LegsFollow {
public:
    explicit LegsFollow(LegsSettings const& settings = {}) : settings_(settings) {}
    LegsState Update(LegsInput const& in);
    // The next update starts over: no side kept, and the weight goes straight to where it belongs (a re-anchor, or the
    // torso wasn't driven).
    void Reset() { active_ = false; }
    void SetSettings(LegsSettings const& settings) { settings_ = settings; }
    LegsSettings const& Settings() const { return settings_; }
    LegsState const& Last() const { return last_; }

private:
    LegsSettings settings_;
    bool active_ = false;
    bool moving_ = false;
    double weight_ = 0.0;
    double facing_ = 0.0;
    LegsState last_;
};

} // namespace mevr::ik
