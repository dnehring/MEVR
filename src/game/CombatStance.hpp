// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The weapon drawn, looking and feeling like exploring (research/38; the rules in core/vr/CombatStance). LE1, VR on
// foot. Settings in [Hands] of <GAME>MEVR.ini (game/hand_calibration); B (the view) is the host's (plugin/HalcyonBridge).
//
// A. Body pose ([Hands] exploreStance = 1, exploreStanceStorm = 1): the player's AnimTree is walked once per tree
//    (Animations -> AnimNodeBlendBase.Children[].Anim, matching AnimNode.NodeName) for the CombatMode node(s) with
//    NonCombat and Combat children. Each tick the decision (drawn, standing, first person, not in cover, the scope or
//    the Mako) goes to engine/bone_pose's pre-stage, which composes the pose from NonCombat while the tree ticks as
//    combat (every notify fires). It waits until the hook has seen the tree tick before it (the [verify] of research/38:
//    `stance: [verify]` lines; if the tree ticks inside UpdateSkelPose it stands down and says so). The arms keep the
//    combat pose: both upper arms and everything below them (hands, fingers, Prop01 the gun hangs on, LHIK the
//    foregrip) are put back at the combat pose's place relative to their parents (engine/bone_pose's StanceHands),
//    learned once per skeleton asset and weapon type from calls composed as combat while the decision says
//    LearningHands (until it holds still: core/ik/PoseGraft's StillPose). Without a learned pose: no explore compose.
// C. Movement ([Hands] exploreRamp = 1): BioPawnBehavior's combat speed ramp (m_fMaxDesSpdTac{Accel,Decel}Rate) set to
//    the explore values (m_fMaxDesSpd{Accel,Decel}Rate, read, not hard-coded); the game's own values come back when
//    it's off, VR goes, the pawn changes or the mod unloads. Logged: the walking mode (BioPawn.GetWalkingSpeedMode) and
//    speed on each change (`movement:`), to measure the zoomed walk (the support grip's LT).
// D. Draw delay: `draw:` lines measure each draw, holster and switch (how long IsSwitchingWeapons stays true, when the
//    action state changed, when the gun reached the hand, per weapon type). [Hands] drawRate (default 2, 1 = the
//    game's) plays the WeaponEquip node's Draw and Holster sequences faster (AnimNodeSequence.Rate, written while they
//    play, put back when they stop): every notify still fires, in order. The notify functions are never called.
//
// Logs `stance:`, `movement:`, `draw:`; the snapshot's mod status "stance". Game thread.
namespace mevr::game::combat_stance {

struct Link {
    bool headTrackedView = false;  // the host shows a head-tracked world view (VR)
    bool supportHeld = false;      // the support grip holds the gun (LT reaches the game: aimed fire)
};

// Finds the script functions. False until the script packages have loaded (or not LE1).
bool TryStartup();
// Each mod tick, after arm IK and the holsters.
void Tick(double deltaSeconds, Link const& link);
std::string StatusJson();
void LogStats();
// Puts the game's speed ramp and animation rates back (where the objects are still the player's), and stops the
// pre-stage.
void Shutdown();

} // namespace mevr::game::combat_stance
