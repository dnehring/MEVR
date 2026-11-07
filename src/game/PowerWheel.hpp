// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/PowerWheel.hpp"
#include "game/GamepadMapping.hpp"

#include <string>
#include <string_view>

// The power wheel at the left hand (research/55; LE1, on foot in VR; [PowerWheel] in ASI\LE1MEVR.ini). X held opens it
// at the left palm, fixed in the room, facing the eyes, with the player's own powers (their behaviour's m_powers, the
// mappable ones, biotics first); the left hand's travel toward a tile, or the left stick's direction, picks one; letting go
// of X equips it. X tapped keeps it open until a pinch of the left trigger, a squeeze of the left grip or X again
// (core/vr/PowerWheel's Machine). Equipping makes it the game's mapped power (BioPlayerInput.MappedPower; the right stick click's
// tap cast it until 2026-11-05, now only the push does: research/19 "Hold only"). The lock and the push (research/55
// section 4): with the wheel closed and the left arm raised, the hostile nearest the left hand's ray (in sight from the hand,
// in range) is locked, sticky, and marked (HostServices v29, setPowerLock); a push of the left hand along its ray casts the
// equipped power through BioPlayerInput.ActivatePower at the locked hostile (a place power with none: where the ray meets
// the world; a power without aim: at once).
// While it's on in the VR world, X never reaches the game on foot (handPowerWheel plus grenadeButton off, taps and holds),
// even if Throwables keeps the old grenade button or Holsters maps X to RB. Other modes keep X. While open the left stick, L3,
// X, the left trigger and grip are its own (gamepad::Options::powerWheel). The host draws it (HostServices v28,
// setPowerWheel; plugin/PowerWheel). Game thread.
namespace mevr::game::power_wheel {

struct Link {
    void (*publish)(vr::power_wheel::View const* view) = nullptr;  // v28 setPowerWheel; null: an older host (no wheel)
    void (*publishLock)(vr::power_wheel::LockView const* view) = nullptr;  // v29 setPowerLock; null: no marker (the lock still works)
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
};

// Before the ProcessEvent hook goes in (Mod.cpp): the handler that times the game's release against the bolt (research/55
// section 9).
bool TryStartup();
void Tick(double deltaSeconds, Link const& link);
// The pad options (before the support grip publishes them): X off on foot while it's on; the left hand its own while open.
void AdjustPad(gamepad::Options& options);
bool IsOpen();
// Open, or a left trigger or grip used in it still held: the squad's trigger and the left hand's other uses stand down.
bool OwnsLeftHand();
// `mevr powerwheel [open|close|pick <n>|hand <x> <y>|equip|roster]`: the wheel without tracked controllers (tests).
std::string Console(std::string_view args);
// Snapshot JSON ("powerWheel"), heartbeat line.
std::string StatusJson();
void LogStats();
// Mod shutdown: the host's wheel closed.
void Shutdown(Link const& link);

}  // namespace mevr::game::power_wheel
