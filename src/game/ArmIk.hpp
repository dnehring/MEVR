#pragma once

// VRIK route 1 (research/14): drive the engine's own two-bone IK. If the player's AnimTree has a
// SkelControlLimb on a hand bone, its effector is set to that motion controller and its joint
// target to a natural elbow position (core/ik), so the engine bends the arm and blends it with
// the animation every frame. Hands are placed relative to Shepard's head bone, in the frame of
// the pawn's facing, so it works (and can be tested) in third person too. Never creates or
// removes controls; the original settings come back when it's turned off. Everything is read
// fresh from the live player pawn each tick (no object pointers kept). Off by default (`.`).
//
// If the probe (game/skeleton_probe) shows no such controls, route 2 (writing the arm bones
// after animation) is needed instead, and this stays inert.
namespace mevr::game::arm_ik {

// Game thread, after each engine tick: finds the controls for a new mesh, drives them while on.
void Tick(float deltaSeconds);
bool Toggle();
void LogStats();
// Restores the controls' settings if the player's body is present, and forgets them. (If the
// body is away at that moment, e.g. in the Mako, they keep the driven values until a level load.)
void Shutdown();

} // namespace mevr::game::arm_ik
