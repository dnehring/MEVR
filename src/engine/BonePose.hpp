#pragma once

#include "core/ik/ArmPose.hpp"
#include "core/ik/TorsoPose.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

class USkeletalMeshComponent;

namespace LESDK {
class Initializer;
}

// VRIK route 2 (research/14): arm IK written into the player's pose after the animation, before
// anything shows it. USkeletalMeshComponent::UpdateSkelPose (LE1 0x592440) blends the AnimTree into
// LocalAtoms, composes them into SpaceBases (component space) and applies the skeletal controls
// (ComposeSkeleton, 0x580A60). Everything that shows the pose reads SpaceBases afterwards: the
// render data update and attachments in UpdateTransform, GetBoneMatrix, sockets. A post-hook on it
// rewrites the arm chains in SpaceBases (core/ik/ArmPose), carrying every bone below each upper
// arm along rigidly, so fingers and anything on the hand follow.
//
// The game thread publishes what to write (Publish); the hook only reads it, and writes nothing
// unless the mesh, its owner, the world, its bone count and the chain's bone names all still
// match and the request is recent (a level load runs no mod tick in between).
namespace mevr::engine::bone_pose {

enum class Target {
    Off,        // the animation keeps this arm
    Component,  // `wrist` is a component-space point
    Chest,      // `wrist` is (forward, outward, up) from the shoulders' midpoint, in arm lengths (desktop test)
};

struct Arm {
    Target target = Target::Off;
    ik::ArmChain chain;          // set for every arm found, even when Off (Chest uses both shoulders)
    std::uint64_t keys[3] = {};  // RefSkeleton names of the upper arm, forearm and hand
    ik::Vec3 wrist;
    bool hasRotation = false;
    ik::Quat rotation;  // the hand bone's component-space rotation
    // The wrist target's distance from the shoulder (after the torso and clavicle moved it) is
    // scaled by this: arm-length calibration (the player's full reach is Shepard's). 1: none.
    double reachScale = 1.0;
};

// The upper body under the headset (core/ik/TorsoPose), solved before the arms so they reach
// from where the shoulders end up.
struct Torso {
    bool enabled = false;
    ik::TorsoChain chain;
    std::vector<std::uint64_t> keys;  // RefSkeleton names: the spine bones, then both clavicles (0: none)
    ik::TorsoGoal goal;               // component space
    bool collars = true;              // clavicles shrug toward far hand targets (ik::PoseCollar)
};

struct Request {
    USkeletalMeshComponent const* mesh = nullptr;  // compared by address only, never dereferenced
    int boneCount = 0;
    std::vector<int> parents;
    ik::BodyFrame body;  // component space
    std::array<Arm, 2> arms;  // 0 left, 1 right
    Torso torso;
    double maxStretch = 1.0;  // arms may stretch this much to reach (ik::ArmGoal::maxStretch)
    int head = -1;            // the head bone: its animated position is kept for AnimatedHead()
};

// LE1 only: false (logged) for other games or if the address isn't a function start.
bool Install(::LESDK::Initializer& init);
// Removes the hook and waits (up to 200 ms, if `waitForCalls`) for calls still inside it. False
// if the hook couldn't be removed or a call is still inside: this DLL must stay loaded.
bool Uninstall(::LESDK::Initializer& init, bool waitForCalls);
bool Installed();

// Game thread. Replaces what the hook writes (also noting the mesh's owner, the world and the
// time, which the hook checks); Clear stops all writes.
void Publish(Request request);
void Clear();

// Game thread: logs the first write for the published mesh in detail (once per mesh).
void LogFirstWrite();
// Poses written since the hook went in (any thread; never reset by the heartbeat): a caller
// compares it across ticks to know the pose it asked for is being written.
std::uint64_t TotalWrites();
// Heartbeat: calls, writes, rejections and the latest before/after per arm.
void LogStats();

// The published mesh's head bone (Request::head) as the animation left it, before our writes
// (component space), from the last write within half a second. Moving the spine moves the head,
// so whatever is anchored to the head must use this, or it would chase its own writes.
std::optional<ik::Vec3> AnimatedHead();

} // namespace mevr::engine::bone_pose
