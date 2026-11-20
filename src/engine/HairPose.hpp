// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/HairChain.hpp"
#include "core/ik/HandContact.hpp"
#include "core/ik/Jiggle.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mevr::ue3 {
struct Object;
}

// The hair stage (docs/trilogy-hair-plan.md section 4.3): hair components whose hairstyle has hair bones, simulated right
// after the engine composed their pose (engine/BonePose's UpdateSkelPose hook calls AfterPose for a published component),
// and their chain bones written back into the component's SpaceBases before anything draws them. Each component's own
// fresh pose is the animated input, its owner's body (the person's skull, neck, collars, spine, upper arms: capsules from
// that body mesh's bones, read in the same call) keeps the hair off the person, and the player's drawn arms and hands (our
// VRIK body, the same way) push it. Checked on every call: the component by address, its mesh asset and bone count, the
// chain bones by name, the game thread, the request's age; inside SEH; journalled ("HairPose.chains"). Every game. Mod side.
//
// The same call drives the body's soft-tissue bones (research/trilogy-jiggle.md; tools/rig adds them): every bone named
// Breast* or Glute* is a core/ik/Jiggle spring in its own animated frame (tools/rig gives it the body's axes at rest: X out
// of the body, Y right, Z up), its offset and turn written over the animation's pose of it.
namespace mevr::engine::hair_pose {

inline constexpr int kMaxTargets = 8;

// One hair component to simulate, published by game/Hair each tick.
struct Target {
    ue3::Object* hair = nullptr;          // the hair SkeletalMeshComponent
    ue3::Object const* asset = nullptr;   // its SkeletalMesh when published (another mesh: refused, re-discovered)
    ue3::Object* body = nullptr;          // the owner's body SkeletalMeshComponent (its capsules), or null
    std::vector<ik::hand_contact::BonePart> bodyParts;  // that body's parts kept off (skull, neck, collars, spine, upper arms)
};
struct Request {
    std::vector<Target> targets;  // at most kMaxTargets
    ik::hair::Settings settings;
    double unitsPerMeter = 100.0;
    ue3::Object* player = nullptr;                          // the player's body mesh (its drawn arms and hands push hair)
    std::vector<ik::hand_contact::BonePart> playerParts;    // its hand, forearm and upper-arm parts
    bool playerPushes = true;
    bool jiggle = true;               // the soft-tissue bones move
    double jiggleIntensity = 0.5;     // 0..1 (ik::jiggle::Settings::intensity)
};
// Game thread: from the next call on; Clear stops the stage (the hair is the animation's alone again).
void Publish(Request request);
void Clear();
// Whether the hook should call AfterPose for this component (cheap: an atomic list).
bool Wants(ue3::Object const* component);
// engine/BonePose's hook, right after the engine's UpdateSkelPose for a component Wants() named.
void AfterPose(ue3::Object* component, float deltaTime);

struct ChainInfo {
    int anchor = -1;
    std::vector<std::string> bones;
    double tipOffsetM = 0.0;
    int resets = 0;
};
struct SoftInfo {
    std::string bone;
    double offsetM = 0.0;    // the shown offset now
    double peakM = 0.0;      // since the last LogStats
    int resets = 0;
    int clampHits = 0;
};
struct TargetInfo {
    ue3::Object* hair = nullptr;
    std::vector<ChainInfo> chains;
    std::vector<SoftInfo> soft;
    std::uint64_t calls = 0;       // AfterPose calls for it
    std::uint64_t written = 0;
    std::uint64_t collisions = 0;
    double peakTipM = 0.0;         // since the last LogStats
    char const* lastReject = nullptr;
};
struct Stats {
    std::vector<TargetInfo> targets;
    std::uint64_t rejected = 0;
    bool broken = false;          // an access violation: the stage stopped for good
};
Stats GetStats();
// The per-tick trace: `seconds` > 0 writes every chain point and its animated place each call to `path` (CSV).
void Trace(double seconds, std::string const& path);
void ResetPeriod();
// Slow motion for footage (`mevr anim slowmo`): every step's time multiplied by `scale` (1 is real time).
void SetTimeScale(double scale);
// A test flick (`mevr hair kick`): every simulated point of that component's chains given `velocityMS` (world, m/s) on its
// next step. False if the component isn't simulated.
bool Kick(ue3::Object const* component, ik::Vec3 velocityMS);

} // namespace mevr::engine::hair_pose
