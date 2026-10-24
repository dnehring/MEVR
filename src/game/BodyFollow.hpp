// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"
#include "core/ik/TorsoPose.hpp"
#include "core/vr/Floor.hpp"
#include "game/HandTargets.hpp"

#include <array>
#include <optional>
#include <string>

namespace mevr::ue3 {
struct Object;
}

// Shepard's body under the player (research/29): where arm IK's torso goal points, every tick, from
// this tick's headset, controllers and view. Owns the one piece of state the body needs, kept in
// tracking space (core/ik/BodyFollow: the body's yaw and where the feet are), re-anchored on a
// recenter, a tracking jump and whenever the torso starts being driven again (a cutscene, a menu,
// a conversation or a vehicle stop it).
//
// First person with [Hands] bodyMove (the default): the whole body (the `Root` subtree; never `God` or
// `Camera`, research/18) turns by whatever the spine's twist can't take and slides under the view so
// the feet are under the player's head, and the spine leans Shepard's head bone to the head's pivot
// (not the eyes: turning the head doesn't move the shoulders). Without bodyMove: the spine alone, as
// before (Shepard's eyes moved as far as the headset moved from the recenter point).
//
// Logs `torso:` when the shoulders end up off the player's (rate-limited) and on each re-anchor; the
// snapshot JSON gets the same inputs (StatusJson).
namespace mevr::game::body_follow {

// The mod, every tick (host renderer, HostServices v10): the recenter counter. Nullopt: unknown
// (a tracking jump still re-anchors).
void NoteFloor(std::optional<vr::floor::State> const& floor);

struct Bones {
    int head = -1;                  // Shepard's head bone (the head's pivot)
    std::array<int, 2> eyes{-1, -1};
    int body = -1;                  // the whole body's bone (LE1: Root), -1: none
};

// Arm IK, on foot with the controllers followed: this tick's torso goal (component space). False:
// no goal (no head bone, a degenerate view); Reset() is up to the caller then.
bool Fill(hand_targets::Targets const& targets, ik::Affine const& worldToComponent, ik::BodyFrame const& frame,
    ue3::Object const* pawn, Bones const& bones, double deltaSeconds, ik::TorsoGoal& goal);
// The torso isn't driven this tick: the next Fill re-anchors.
void Reset();
// The body's yaw (tracking space, ik::BodyYaw's convention) while the torso is driven.
std::optional<double> Yaw();
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::body_follow
