// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"

#include <array>
#include <optional>
#include <string>

// VRIK arms, route 2 (research/14): Shepard's arms follow the hand targets (game/hand_targets)
// through arm bones written after the animation (engine/bone_pose). Route 1, driving the AnimTree's
// own SkelControlLimb, is out: the player's tree has limb controls only on the ankles and on the
// gun's off-hand (LeftWrist), none on the right arm.
//
// Each tick, on foot: finds the player's body mesh; resolves both arm chains by bone name (once
// per skeleton, logged); checks the bone layouts against the engine's own GetBoneLocation (once per
// mesh, logged); turns the targets into component space and hands them to the writer. Anything
// that doesn't resolve stops all writes, and the status says why. Nothing is kept across ticks
// but values and pointers compared by address.
//
// Desktop test: with no VR session, arm IK on (`.`) holds the right hand at a fixed point in
// front of the chest, visible in the normal third-person camera.
namespace mevr::game::arm_ik {

// Game thread, after each engine tick (needs the engine::bone_pose hook installed to write).
// Holding a gun (game/weapon has its grip), the right hand turns so the gun's barrel points along
// the right controller's aim ray instead of following the grip pose (core/ik HandForBarrel).
void Tick(float deltaSeconds);
// This tick's request points the held gun along the right controller's aim, and the writer is
// writing the pose: the gun model follows the controller, so shots can follow the gun.
bool RightHandAimsGun();
// This tick's request asks the writer for the two-handed grip (a settled foregrip on the aimed gun,
// the left controller followed): game/support_grip's distance and blend are about this gun.
bool SupportRequested();
// The torso's heading this tick (OpenXR yaw in tracking space, ik::BodyYaw's convention), while arm
// IK turns the torso to it (game/holsters hangs the body slots off it). Nullopt otherwise.
std::optional<double> TorsoYaw();
// Arm IK placed the controllers in the view's own frame (first person: the view near Shepard's head bone) within the
// last half second (game/combat_stance, research/38).
bool FirstPerson();
// Shepard's head bone as the animation left it, scaled as the writer draws the body, in the mesh's component space: without
// our writes (the body follower's slide, the spine). Within half a second of the last write; nullopt when nothing is written
// (the drawn pose is the animation's then). Whatever places the player by Shepard's head (a scene's opening, its follower)
// takes this one: his drawn head is where our body follower put it, under the player (research/69 "The body that stayed at
// the mark", 2026-11-13).
std::optional<ik::Vec3> AnimatedHeadComponent();
// Where Shepard's drawn hands are against the player's controllers (research/69 "Seeing the body in the audit", 2026-11-13):
// each drawn wrist (the last posed pose) taken into tracking space through the view's frame (view_anchor), its distance from
// that controller's grip, and whether it lies inside the head's view (35 degrees up, down and to each side of its forward:
// clear of the edge of the fake headset's 45-degree field). In first person the wrist sits 7 units behind the grip; a body left behind (arm IK's third
// person, the council hearing's 3.85 m) puts it metres away. The audit's placement measure, independent of arm IK's own
// verdict. Game thread.
struct HandsPlaced {
    std::array<bool, 2> valid{};        // left, right: a tracked controller and a drawn wrist read
    std::array<double, 2> fromGripM{};  // the drawn wrist from its controller's grip, metres
    std::array<bool, 2> inView{};       // the drawn wrist inside the head's view
};
// Nullopt without tracked controllers, a view frame or a posed player mesh.
std::optional<HandsPlaced> MeasureHands();
// The snapshot's "hands": MeasureHands as JSON, or null.
std::string HandsJson();
bool Toggle();
void SetEnabled(bool on);
bool Enabled();
// One line: what arm IK does now ("off", "driving: ...", or why it can't), as LogStats prints it.
std::string const& Status();
// Where the hand targets come from (game/hand_targets' name for it).
char const* HandSource();
void LogStats();
// Stops all writes and forgets everything.
void Shutdown();

} // namespace mevr::game::arm_ik
