// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Controllers.hpp"
#include "engine/BonePose.hpp"
#include "engine/Skeleton.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

// Shepard's fingers follow the controllers (research/43): in first person on foot with tracked controllers, each
// hand's index finger curls with that controller's trigger and the other three with its grip (the thumb closes a
// little with the grip), on top of the animation's pose. With the weapon drawn the right hand keeps the animation's
// grip on the gun except the index finger, which rests along the frame off the trigger until the trigger is squeezed
// (then it is on the trigger as animated); the left hand does the same as a free hand only while it is NOT on the gun
// (held by the support grip, it keeps the animated hold of the foregrip). Controllers with touch sensors (Touch,
// Index; research/43 "Touch sensors", [Hands] fingerTouch) say more: a finger lifted off the trigger points (on the gun:
// lies along the frame), one resting on it curls a little; a thumb lifted off the stick, buttons and thumb rest is up
// (an open hand, a thumbs-up in a fist). The rules are core/ik/Fingers' FromController.
//
// Arm IK (game/arm_ik) calls Fill each tick it drives the arms from controllers, with its skeleton and the pose as
// the animation left it: the finger bones are found by name under each hand bone (once per skeleton asset, logged),
// their flexion axes measured from that pose (core/ik/Fingers), and the curls go into the writer's request
// (engine::bone_pose::Request::fingers), which turns the joints after the arms are solved. Settings: [Hands]
// fingers, fingerCurlDeg (game/hand_calibration). Logs `fingers:`; the snapshot's mod status "fingers". Game thread.
namespace mevr::game::fingers {

// The skeleton arm IK resolved (spans into its own storage, valid for the call).
struct Skeleton {
    void const* asset = nullptr;  // the SkeletalMesh asset, compared by address: the cache key
    std::span<std::string const> names;
    std::span<int const> parents;
    std::span<std::uint64_t const> keys;
    std::array<int, 2> hands{-1, -1};  // the hand bones (0 left, 1 right; -1 none)
};

struct Inputs {
    bool firstPerson = false;  // arm IK places the hands in the view's own frame
    bool weaponDrawn = false;  // a verified ranged weapon hangs in the right hand (or arm IK aims it this tick)
    bool leftOnGun = false;    // the support grip holds the foregrip
};

// Fills `request.fingers` for this tick (hands it can't drive stay disabled, the status says why).
void Fill(engine::bone_pose::Request& request, Skeleton const& skeleton, std::span<engine::skeleton::BoneAtom const> pose,
    vr::controllers::State const& controllers, Inputs const& in);
// Arm IK isn't driving this tick: nothing is written, the status says why.
void Stop(char const* why);
// The palm's normal (out of the palm, the side the fingers close toward) in the hand bone's frame, as measured for
// `asset`'s skeleton by Fill; nullopt before that or if it couldn't be told.
std::optional<ik::Vec3> PalmNormal(void const* asset, int side);

std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::fingers
