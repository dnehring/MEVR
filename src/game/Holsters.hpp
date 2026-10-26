// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Floor.hpp"
#include "game/GamepadMapping.hpp"

#include <cstdint>
#include <optional>
#include <string>

// Body holsters (research/19): the right hand reaches to a slot on the body and squeezes the grip
// to draw that slot's weapon. With [Holsters] gripHolds = 1 (the default) the right grip then holds
// the gun: keep squeezing to carry it, let go at its own slot to holster it, let go anywhere else and
// it goes back after releaseGrace unless squeezed again; RB moves to [Holsters] powerWheel. With 0,
// squeezing and releasing at the held gun's own slot holsters it, and the grip is RB elsewhere.
// Slots, sizes and options: [Holsters] in <GAME>MEVR.ini (game/hand_calibration).
//
// Each tick, on foot with Halcyon's world view and a tracked right controller:
//   - the body frame: arm IK's torso yaw (game/arm_ik), or our own lagging yaw from the head, held
//     while the hand is in a slot; the anchor is the eye held upright over the neck pivot, its height
//     smoothed; the offsets scale with the standing eye height measured from the floor (host v10,
//     ik::body_slots::StandingHeight), else [Holsters] eyeHeightM;
//   - the hand against each slot (ik::body_slots::Machine): enter/leave, grabs, holsters, returns;
//   - contact haptics: a continuous buzz while the hand is in a slot, deeper = stronger, an edge when
//     it crosses in, a click on a draw or holster (ik::body_slots Contact/EnterEdge/..., played by the
//     host: HostServices hapticContinuous / hapticPulseHz, v10; v9: one pulse on entry);
//   - the pad (AdjustPad): with gripHolds a right-grip squeeze is never RB and RB comes from
//     powerWheel; without it, near a slot a NEW right-grip squeeze is kept off RB. The host counts
//     the squeezes kept from the game (HostServices::rightGripFree); each new one is a grab;
//   - the game (LE1): SFXGameModeDefault.SwitchToWeapon(type) to draw (the animated switch the PC
//     number keys use), SFXGameModeDefault.TryHolster() to holster (only while drawn: it toggles),
//     the player's quick slots (BioPawn -> BioPawnBehavior -> BioEquipment) for what they carry.
//     All through ProcessEvent, never the SDK's wrappers.
// Logged once per change; a heartbeat line; the snapshot's mod status carries the slots.
// Game thread. LE2/LE3: off (their weapon classes differ).
namespace mevr::game::holsters {

// What the host offers this tick (Mod.cpp fills it from HostServices).
struct Link {
    // HostServices::rightGripFree (v9): nullopt with an older host, which can't keep a grab's
    // squeeze off RB; the grab then follows the mod's own reading of the grip.
    std::optional<std::uint32_t> rightGrip;
    // HostServices::hapticPulse (v9), or null.
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
    // v10: a pulse at a frequency, and the continuous contact buzz; null with an older host.
    void (*hapticHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr;
    void (*hapticContinuous)(int hand, float amplitude, float frequencyHz, float seconds) = nullptr;
    // The pad mapping knows gamepad::kFlagRightGripHolds (host v10, or our own mapping): without it
    // gripHolds can't work (the grip would stay RB), so the old mapping is used.
    bool gripHoldsSupported = false;
    // v10: the head's height above the floor (nullopt: an older host, or sizes differ).
    std::optional<vr::floor::State> floor;
};

// Finds the script functions. False until the script packages have loaded (or not LE1).
bool TryStartup();
void Tick(double deltaSeconds, Link const& link);
// The pad options as holsters want them: the right grip holding the gun and RB from powerWheel
// (gripHolds), or RB kept off a new right-grip squeeze near a slot; and the weapon wheel's LB off if
// [Holsters] weaponWheel = 0. Called by game/support_grip before it publishes the flags (so after
// Tick).
void AdjustPad(gamepad::Options& options);
// For game/throwables (research/20): the standing eye height that scales the slots (meters), whether
// the right hand is in a weapon slot now, and the seconds since our last draw request (a draw's fast
// reach mustn't count as a melee stroke).
double EyeHeight();
bool RightHandInSlot();
double SecondsSinceDraw();
// [Holsters] onlyHolstersDraw (default 1): only a grab at a holster draws. The game's own draws with the weapon away
// (SFXPawn_Player.DrawPrimaryWeapon, from melee, grenade and power buttons, the wheels, the holster key's toggle) are
// skipped on foot in VR. A draw we want from the game (a grenade thrown with the weapon away: UseGrenade draws) goes
// between AllowGameDraw(true) and AllowGameDraw(false). Game thread.
void AllowGameDraw(bool begin);
// For the test loop's mod status: a JSON object.
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::holsters
