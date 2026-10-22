#pragma once

#include "game/GamepadMapping.hpp"

#include <cstdint>
#include <optional>
#include <string>

// Body holsters (research/19): the right hand reaches to a slot on the body and squeezes the grip
// to draw that slot's weapon; squeezing and releasing at the held gun's own slot holsters it.
// Slots, sizes and options: [Holsters] in <GAME>MEVR.ini (game/hand_calibration).
//
// Each tick, on foot with Halcyon's world view and a tracked right controller:
//   - the body frame: arm IK's torso yaw (game/arm_ik), or our own lagging yaw from the head, held
//     while the hand is in a slot; the anchor is the eye held upright over the neck pivot, its height
//     smoothed (ik::body_slots);
//   - the hand against each slot (ik::body_slots::Machine): enter/leave, a pulse on entry;
//   - near a slot the pad mapping keeps a NEW right-grip squeeze off RB (AdjustPad: the flag the
//     host reads); the host counts those squeezes (HostServices::rightGripFree), and each new one in
//     a slot is a grab;
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
};

// Finds the script functions. False until the script packages have loaded (or not LE1).
bool TryStartup();
void Tick(double deltaSeconds, Link const& link);
// The pad options as holsters want them: RB kept off a new right-grip squeeze near a slot, and the
// weapon wheel's LB off if [Holsters] weaponWheel = 0. Called by game/support_grip before it
// publishes the flags (so after Tick).
void AdjustPad(gamepad::Options& options);
// For the test loop's mod status: a JSON object.
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::holsters
