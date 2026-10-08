#pragma once

// VRIK route 1 (research/14): drive the engine's own two-bone IK. If the player's AnimTree has a
// SkelControlLimb on a hand bone, its effector is set to that motion controller (world space)
// and its joint target to a natural elbow position (core/ik), so the engine bends the arm and
// blends it with the animation every frame. Never creates or removes controls; the original
// settings come back when it's turned off. Off by default (`.` toggles).
//
// If the probe (game/skeleton_probe) shows no such controls, route 2 (writing the arm bones
// after animation) is needed instead, and this stays inert.
namespace mevr::game::arm_ik {

// Game thread, after each engine tick: finds the controls for a new mesh, drives them while on.
void Tick(float deltaSeconds);
bool Toggle();
void LogStats();
// Restores the controls' settings (if the mesh is still the player's) and forgets them.
void Shutdown();

} // namespace mevr::game::arm_ik
