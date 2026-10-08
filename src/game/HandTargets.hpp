#pragma once

#include "core/ik/Rigid.hpp"
#include "engine/XrToUnreal.hpp"

#include <array>
#include <optional>

// Where Shepard's hands should be: the one place that knows the hand source, so swapping it
// (e.g. for Halcyon's renderer, which owns the camera) touches only this file. Arm IK
// (game/arm_ik) turns these into bone targets.
namespace mevr::game::hand_targets {

enum class Source {
    None,         // nothing to follow: the animation keeps the arms
    Controllers,  // tracked motion controllers
    DesktopTest,  // no VR session: arm IK holds the right hand in front of the chest (no positions here)
};

struct Hand {
    bool valid = false;
    engine::UnrealVector position;  // world, Unreal units: where the hand bone (wrist joint) goes
    bool hasRotation = false;
    ik::Quat rotation;  // world, UE3 axes: the controller's grip orientation
};

struct Targets {
    Source source = Source::None;
    std::array<Hand, 2> hands;  // 0 left, 1 right
};

// Game thread, once per tick. Today: OpenXR grip poses (LOCAL space) placed in the world through
// the last head-tracked view (head_tracking::Anchor: the head pose and body yaw), with the wrist
// a few centimetres behind the grip; DesktopTest whenever no VR session runs.
// `headBone`: the world location of Shepard's head bone. Given, the controllers are placed around
// it rather than around the view (the camera): that's what works in third person, and in first
// person the two nearly coincide. Without it, around the view's head location.
Targets Current(std::optional<engine::UnrealVector> const& headBone);

char const* Name(Source source);

} // namespace mevr::game::hand_targets
