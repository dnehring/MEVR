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
//
// The visible rack (research/45 section 6, [Vent] animate): while the left hand holds the receiver of a hot gun, one
// part of the gun's own model (its slide, heat sink or receiver cover: a bone of the held SkeletalMeshComponent and the
// bones below it, chosen per model by core/ik/Vent ChoosePart) follows the hand's pull back along the gun, clamped to
// the part's travel, and snaps forward on a fast spring at the release, vented or not (ik::vent::Rack). It is written
// into the gun's pose right after its UpdateSkelPose (engine/bone_pose's weapon-part stage, journalled); a gun whose pose
// isn't updated by itself gets SkeletalMeshComponent.ForceSkelUpdate (ProcessEvent, the mod's tick) while the part
// moves. A model without a suitable bone stands down, its bone list logged once. Logs `vent: rack:`.
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
// Before engine/bone_pose's hook goes (the mod unloads): the rack's part back at its rest place and the stage cleared.
// `exiting`: the process is going (nothing is called into the game then).
void PutPartBack(bool exiting);
void Shutdown();

// `mevr vent ...` (game/ConsoleRequest, the game thread): tests without the controllers.
//   heat <0..1> [hold] | heat off   the held gun's heat as that fraction of ventMaxHeat (hold: kept each tick until off)
//   rack [fraction] [seconds]       the part pulled back that fraction of its travel (default 1) for that long (0.5 s),
//                                   then let go: what a stroke shows, without a vent
//   part                            the held model, its part and its bones
std::string Command(std::string const& args);

} // namespace mevr::game::vent
