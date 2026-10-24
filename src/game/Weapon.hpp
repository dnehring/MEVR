// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"
#include "engine/XrToUnreal.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mevr::ue3 {
struct Object;
}

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
//     weapon mesh's own X axis. Its up: the same frame's Z, for diagnostics only: model axes don't
//     agree between weapons (the pistol's and the rifle's ups were 90 degrees apart in the hand), so
//     arm IK takes the gun's roll from the hand, not from this (ik::GunUpInHand).
//   - Held in the right hand: the weapon mesh hangs (SkeletalMeshComponent.Attachments) on a bone
//     of the body's right-hand subtree (e.g. Prop01 under RightWrist), which is how
//     BioWeapon.AttachToMesh (0xC3EC70) attaches it, through a socket of the body.
//   - The grip: muzzle, barrel (and the model's up) in the right hand bone's own axes. Measured from the world
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
    engine::UnrealVector up;        // world, unit, perpendicular to the barrel (model axes: diagnostics only)
    engine::UnrealVector origin;    // world: the weapon mesh's origin (its root, where the hand holds it)
};

// How the muzzle sits on the right hand bone, in the bone's own axes.
struct Grip {
    ik::Vec3 barrel;  // unit
    ik::Vec3 up;      // unit, perpendicular to the barrel: the model's up (diagnostics; not the roll)
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
// The verified weapon in the right hand, as an identity to compare (0: none).
std::uintptr_t Key();
// Its ranged type (BioWeaponRanged.GetRangedWeaponType, EBioItemWeaponRangedType), read once per
// weapon.
enum class RangedType { Pistol = 0, Shotgun = 1, AssaultRifle = 2, Sniper = 3, Unknown = -1 };
RangedType Type();
// The player's ranged weapon's bIsZoomed (BioWeaponRanged +0x548 bit 0x4), live: the game's aimed
// fire (research/18: it picks the smaller aim error and recoil, and the TightAim camera follows
// it; set from BioPlayerInput.bWantsToZoom, i.e. LT). Nullopt without a ranged weapon.
std::optional<bool> Zoomed();
// The held ranged weapon's aim error for `accuracy` as if it were zoomed (aimed fire): the native
// GetPlayerAimError called through ProcessEvent with bIsZoomed set for that call only, so nothing else
// (the camera, which picks the game's TightAim and sniper scope modes from it) ever sees the flag.
// `weapon` must be the player's held ranged weapon; nullopt if not, if it's already zoomed, or LE2/LE3.
// Game thread.
std::optional<float> ZoomedAimError(ue3::Object* weapon, float accuracy);
// The same native's answer as the weapon is now (not zoomed): for undoing the lerp it applies.
std::optional<float> AimError(ue3::Object* weapon, float accuracy);
// A socket of the weapon's mesh named for the off hand (logged once per weapon with all its
// sockets), live, in the right hand bone's axes (component units). Nullopt if it has none.
std::optional<ik::Vec3> OffHandSocketInHand();
// The held gun's model against its bore (research/21 section 11): the corners of its mesh's bounds
// (USkeletalMesh.Bounds, the reference pose in the mesh's space) and its bones and sockets, live, in the gun's
// own frame: `back` behind the muzzle along the barrel, `up` above the bore toward `top` (world, the gun's
// top as the sight uses it), `side` to the right. Nullopt without a held, verified weapon.
struct ModelPoint {
    std::string name;
    double back = 0.0;
    double up = 0.0;
    double side = 0.0;
};
struct Model {
    double backMin = 0.0;  // the bounds' extent along each axis
    double backMax = 0.0;
    double upMin = 0.0;
    double upMax = 0.0;   // the model's top above the bore
    double sideMin = 0.0;
    double sideMax = 0.0;
    std::vector<ModelPoint> points;  // bones, then sockets ("socket " before the name)
};
std::optional<Model> ModelInGunFrame(engine::UnrealVector const& top);
// One line for status logs: which weapon and how far along it is.
std::string const& Status();
// Heartbeat.
void LogStats();
void Shutdown();

} // namespace mevr::game::weapon
