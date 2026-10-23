#pragma once

#include "core/vr/AimRay.hpp"
#include "core/vr/Sights.hpp"

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
// The host's report for the latest frame (vr::sights::View bits), each tick.
void SetHostView(std::uint32_t view);
// For snapshots and the heartbeat.
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::sights
