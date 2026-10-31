// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <string>

// The physical vent (research/45): ME1's guns overheat instead of reloading; docs/trilogy-design.md's "Ammo" row
// adds an optional vent that ejects the heat at once. In VR on foot with a weapon drawn, the LEFT hand squeezes
// its grip on the gun's receiver (over and just ahead of the right hand, behind the foregrip's support-grip zone)
// and pulls back and away in one fast stroke, letting go, like racking a slide (core/ik/Vent classifies it in the
// gun's frame the pose writer measures: engine/bone_pose SupportSample). Then the held BioWeaponRanged's
// m_fCurrentHeat goes to 0 and m_bOverheated off (journalled writes; the game has no function that cools), the
// game's own cool-down effect plays ([Vent] effect), both hands pulse. Once per ventCooldownS, only with heat
// above ventMinHeat of ventMaxHeat.
//
// Arbitration of a new LEFT squeeze (game thread, in Mod.cpp's order):
//   1. the support grip (the host's pad mapping, from the distance game/support_grip publishes): a squeeze in the
//      grab zone along the gun is the two-handed grip. While the left hand is in the RECEIVER zone and nothing
//      holds the support grip, support_grip hides its zone from the mapping (MasksSupportZone), so that squeeze
//      is counted "away from the gun" and comes to the mod;
//   2. game/throwables::Claim: a squeeze at the grenade slot takes a grenade (hands = left | both only);
//   3. Claim here: a squeeze not claimed by the grenades, with the hand in the receiver zone, is the vent's;
//   4. anything else: the squad menu and holsters never take the left grip (since 2026-10-22).
// Logged once per event; a heartbeat line; "vent" in the snapshot's mod status. LE2/LE3: off.
namespace mevr::game::vent {

// What the host offers this tick (Mod.cpp fills it from HostServices).
struct Link {
    // HostServices::leftGripAway (v11): nullopt with an older host (our own latch of the grip stands in).
    std::optional<std::uint32_t> leftGrip;
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
    void (*hapticHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr;
};

// Finds the weapon's heat fields and the cool-down event. False until the script packages have loaded (or not LE1).
bool TryStartup();
// Each tick right after throwables::Claim: whether this tick's new left squeeze is the vent's.
void Claim(Link const& link);
bool ClaimedLeft();
// Each tick after throwables (before game/support_grip's tick, which reads MasksSupportZone).
void Tick(double deltaSeconds, Link const& link);
// The left hand is in the receiver zone of a drawn gun: support_grip hides its grab zone from the pad mapping
// while nothing holds the support grip, so the squeeze reaches the mod.
bool MasksSupportZone();
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::vent
