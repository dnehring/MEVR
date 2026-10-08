#pragma once

#include "core/ik/Rigid.hpp"
#include "engine/XrToUnreal.hpp"

#include <optional>
#include <string>

// The ranged weapon the player holds, as the game draws it: where its muzzle is and which way its
// barrel points (LE1; research/14, "Weapon and muzzle").
//   - Which weapon: Pawn.Weapon, an SFXGame.BioWeaponRanged. Its mesh is BioWeapon.SkelMeshComp
//     (+0x3E8) and its muzzle the socket named BioWeaponRanged.m_MuzzleSocketName (+0x490): what the
//     game's own GetMuzzleLoc (0xC59870) reads.
//   - The muzzle: that socket's world transform, computed the engine's way (socket offset on its
//     bone in the weapon's pose, then the weapon mesh's LocalToWorld). Verified once per weapon
//     against the native GetMuzzleLoc through ProcessEvent; a mismatch means no weapon (fail
//     closed, logged).
//   - The barrel: the muzzle socket's X axis (the engine points muzzle effects along it), checked
//     against the direction from the weapon's origin (its grip) to the muzzle; failing that, the
//     weapon mesh's own X axis. Its up: the same frame's Z.
//   - Held in the right hand: the weapon mesh hangs (SkeletalMeshComponent.Attachments) on a bone
//     of the body's right-hand subtree (e.g. Prop01 under RightWrist), which is how
//     BioWeapon.AttachToMesh (0xC3EC70) attaches it, through a socket of the body.
//   - The grip: muzzle, barrel and up in the right hand bone's own axes. Measured from the world
//     while the hand holds still, so it doesn't matter whether the weapon updates before or after
//     the hand is written; kept per weapon and re-measured when it settles somewhere else.
//   - Following: each tick it compares the weapon's muzzle with where the written hand and the
//     attachment put it, and logs once per weapon whether the weapon follows the written hand in
//     the same frame (research/14 open risk).
// Game thread only. LE2/LE3: nothing (layouts not verified there).
namespace mevr::game::weapon {

struct Muzzle {
    engine::UnrealVector location;  // world
    engine::UnrealVector barrel;    // world, unit
    engine::UnrealVector up;        // world, unit, perpendicular to the barrel
};

// How the muzzle sits on the right hand bone, in the bone's own axes.
struct Grip {
    ik::Vec3 barrel;  // unit
    ik::Vec3 up;      // unit, perpendicular to the barrel
    ik::Vec3 muzzle;  // from the hand bone (component-space units)
};

// Finds the natives used for the cross-checks. False until the script packages have loaded.
bool TryStartup();
// Each mod tick, after the engine tick. `wanted`: hand aim or arm IK is on; otherwise it forgets
// the weapon and does nothing.
void Tick(bool wanted);
// The verified weapon's muzzle now, read live (also from inside a shot's aim queries). Nullopt
// unless a verified ranged weapon hangs in the player's right hand.
std::optional<Muzzle> Current();
// The grip, once measured for the weapon held in the right hand.
std::optional<Grip> RightHandGrip();
// One line for status logs: which weapon and how far along it is.
std::string const& Status();
// Heartbeat.
void LogStats();
void Shutdown();

} // namespace mevr::game::weapon
