#pragma once

#include "core/ik/Rigid.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Sights.hpp"
#include "game/Weapon.hpp"

#include <optional>

#include <cstdint>
#include <string>

// Gun sights on the held gun (research/21; the geometry and what the host draws: core/vr/Sights.hpp).
// Each tick, from the gun's aim ray (game/hand_aim: the measured muzzle and barrel in tracking space) and
// [Sights] (game/hand_calibration): which sight the held weapon type gets, where it sits on the gun (its
// back and height in gun units, through the view's scale into tracking meters), the sight line (the
// barrel, or zeroed), and the gun's top (the right controller's up, as arm IK rolls the gun). The host
// (plugin/SightLayers) draws it and says which eye looks through it (HostServices::sightView).
//
// Aimed fire for scoped shots (LE1): while the host shows the scope's picture, the held weapon's aim
// error is the game's zoomed one (bIsZoomed's smaller spread, research/18), without the game's scope
// camera: a CallFunction post-handler on BioWeaponRanged.GetPlayerAimError (called from the weapon's
// GetAdjustedAim script for every shot) rewrites its answer, computing the zoomed one through ProcessEvent
// with bIsZoomed set for that call only (game/weapon ZoomedAimError). Game thread.
namespace mevr::game::sights {

// Registers the aim-error handler (before the CallFunction hook goes in). False on LE2/LE3 or when the
// function isn't found (sights still draw; only scoped aimed fire is off).
bool TryStartup();
// Each tick, after hand aim's ray: the sight to publish (kind None: none), and logs what changes. Also
// steps the alignment assist ([Sights] assist): with the gun raised to the face and the aiming eye near
// the sight line, the gun's goal shift that puts the line through the eye (vr::sights::AssistTarget,
// measured on the sight without the shift already applied, then eased by vr::sights::AssistSpring).
vr::sights::Sight Current(vr::aim_ray::Ray const& ray);
// Arm IK, once per tick while it drives the hands: how far to move the held gun (tracking meters; zero:
// none). The sight it then draws includes the move; Current takes it out again (two ticks later, when the
// engine has posed the gun with it) before measuring the eye against the line.
XrVector3f TakeGunShift();
// Where the held gun's sight centre sits in the right hand bone's axes (component units) for a gun held with its
// top along `upInHand`: its [Sights] place, or the weapon type's typical one (whatever the sight's kind, so the
// guided calibration's sight line has a place on every gun). Nullopt without a held ranged weapon of known type.
std::optional<ik::Vec3> SightInHand(weapon::Grip const& grip, ik::Vec3 const& upInHand);
// A shot that lands on the sight (research/21 section 10; [Sights] converge): each tick the world is traced along the
// held gun's sight line (Actor.Trace from the sight's centre), and a shot leaves the muzzle toward the point the line
// hit, rebuilt from the gun as it is when it fires (the place on the gun, the tick's top, the live barrel). Called from
// hand aim's GetAdjustedAimFor handler. Nullopt (the barrel's line, as before): convergence off, no sight on the held
// gun this tick, no trace.
struct ShotAim {
    engine::UnrealVector direction{};    // world, unit: the shot
    engine::UnrealVector sightLine{};    // world, unit: the sight line
    engine::UnrealVector sightCenter{};  // world: the sight's centre
    double distance = 0.0;               // game units from the sight's centre to what the line hit (the range if nothing)
    bool hit = false;
    std::string what;                    // the actor the line hit
    double toSightDeg = 0.0;             // the shot against the sight line
    double toBarrelDeg = 0.0;            // ...and against the barrel
};
std::optional<ShotAim> ShotAlongSight(weapon::Muzzle const& muzzle);
// The host's report for the latest frame (vr::sights::View bits), each tick.
void SetHostView(std::uint32_t view);
// For snapshots and the heartbeat.
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::sights
