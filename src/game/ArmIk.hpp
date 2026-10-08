#pragma once

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
void Tick(float deltaSeconds);
bool Toggle();
void SetEnabled(bool on);
bool Enabled();
void LogStats();
// Stops all writes and forgets everything.
void Shutdown();

} // namespace mevr::game::arm_ik
