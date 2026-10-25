// SPDX-License-Identifier: MIT
#pragma once

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
