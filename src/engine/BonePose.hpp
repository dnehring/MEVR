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

// The support grip (game/support_grip): the right hand holds a gun that has a place for the left
// hand. Both arms must target Component and the right hand have a rotation (the gun's aim). The
// writer then measures how far the left hand's goal (after reach scaling, as it will be written)
// is from that place (SupportSample), and with `blend` > 0 holds the gun with both hands: the
// barrel swings toward the left hand (ik::TwoHandRotation, `twoHandAim`) and the left wrist goes
// to the foregrip on the gun as written, not where the animation had the gun.
struct Support {
    bool enabled = false;
    ik::Transform foregrip;    // the left hand bone in the right hand bone's axes (translation unscaled)
    bool hasRotation = false;  // foregrip.rotation is the left hand's on the gun; else the controller's
    // The span from the right hand to the foregrip runs along the gun (a rifle's foregrip), so the
    // two hands define the barrel. False (a pistol cupped from below): the right hand keeps aiming.
    bool twoHandAim = true;
    double blend = 0.0;          // 0 one-handed .. 1 both hands
    double unitsPerMeter = 50.0;  // written units per meter of left-controller motion (distance in meters)
    double minSpanMeters = 0.08;  // hands closer than this define no axis: no hold
    // The grab zone (ik::MakeSupportZone, 2026-10-22): the gun's barrel and muzzle in the right hand
    // bone's axes (game/weapon's grip), and how far the capsule reaches ahead of the right hand and past
    // the muzzle (meters of left-controller motion). Without a barrel: the foregrip alone, as before.
    bool hasGun = false;
    ik::Vec3 barrel;
    ik::Vec3 muzzle;
    double frontMeters = 0.10;
    double pastMeters = 0.10;
    // + 1 for each new hold (0: not held). At a hold's first write the line from the right hand to where
    // the left hand took hold is captured as the span two-hand aim swings (ik::CapturedSpan), so taking
    // hold anywhere in the zone doesn't swing the gun; the left hand still goes to the foregrip.
    std::uint32_t hold = 0;
    // The guided calibration (game/body_calibration, research/25): the player's own rifle stance, the left
    // wrist from the right in the right hand bone's axes (component units). With `useCalibratedSpan` (the left
    // hand took hold near where it sat when calibrating) a hold's first write takes it instead of capturing the
    // grab's line, so the barrel keeps the calibrated relation to the line between the hands.
    bool useCalibratedSpan = false;
    ik::Vec3 calibratedSpan;
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
    Support support;
    // The sight alignment assist (game/sights, research/21): the held gun moves by this (component
    // space units), added to the right wrist's goal after reach scaling, and to the left's while the
    // support grip holds (fading with its blend after a release): a hand on the gun moves with it, so the
    // two-hand line doesn't turn; a free left hand stays. Zero: none.
    ik::Vec3 gunShift;
    // Bones the writer never writes, whatever the torso and arms did: their SpaceBases entries
    // stay the animation's (LE1: the camera hook bones `Camera` and `God`, ik::FindCameraHookBones;
    // the camera reads its hook from SpaceBases, so moving one moves the view, research/18). With
    // their RefSkeleton names, checked like the chains'. A future whole-body move must keep them too.
    std::vector<int> keep;
    std::vector<std::uint64_t> keepKeys;
};

// What the writer did with Request::support on its last write.
struct SupportSample {
    // The left hand's goal from the grab zone (the capsule along the gun, or the foregrip; -1: not
    // measured; 99: hands crossed).
    double distanceMeters = -1.0;
    double foregripMeters = -1.0;  // ...from the foregrip itself (diagnostics)
    double alongMeters = 0.0;      // how far along the barrel from the right hand the left goal is
    bool captured = false;         // two-hand aim swings the span captured when this hold began
    bool calibrated = false;       // ...which is the guided calibration's (Support::useCalibratedSpan)
    double blend = 0.0;
    bool twoHand = false;          // the barrel swung toward the left hand
    double swingDegrees = 0.0;     // how far the right hand turned for it
    double leftErrorUnits = 0.0;   // the written left wrist from its goal (out of reach)
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
// Heartbeat: calls, writes, rejections, the latest before/after per arm, and how often a solve
// moved a kept bone (Request::keep; kept animated anyway).
void LogStats();

// The published mesh's head bone (Request::head) as the animation left it, before our writes
// (component space), from the last write within half a second. Moving the spine moves the head,
// so whatever is anchored to the head must use this, or it would chase its own writes.
std::optional<ik::Vec3> AnimatedHead();

// Both hand bones (Request::arms[side].chain.hand) as the animation and the AnimTree's controls left
// them, before our writes (component space), from the last write within half a second.
struct Hands {
    ik::Transform left;
    ik::Transform right;
};
std::optional<Hands> AnimatedHands();

// The torso's last result (Request::torso enabled), within half a second (component space): what the
// body turn, the slide and the spine did (research/29).
std::optional<ik::TorsoResult> LatestTorso();

// The last write's support grip result (Request::support enabled, both arm goals there), within
// half a second.
std::optional<SupportSample> LatestSupport();

} // namespace mevr::engine::bone_pose
