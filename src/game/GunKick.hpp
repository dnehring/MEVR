// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// A visible kick of the gun in the hand on each of the player's shots (research/24 section 8; the math in core/ik/GunKick,
// [Hands] gunKick and gunKickScale). The view never kicks (game/view_level takes each shot's recoil out of ControlRotation and
// buzzes the hand instead); this gives the gun model the kick: muzzle up and back, scaled by the held weapon's own recoil (the
// shot's queued kick from SFXInventoryManager.SetWeaponRecoil, else BioWeaponRanged.GetRecoil through ProcessEvent, else the
// last one this weapon had), springing back within about 120 ms, stacking under a cap in automatic fire, half as much with
// both hands on the gun, its back motion faded out as the gun comes up to the eye. Arm IK puts it in the right hand's goal
// (engine::bone_pose::Request::gunKick); game/weapon takes it back out of every reading of the drawn gun, so the aim ray, the
// shots, ADS, the sight assist and the scope capture follow the controller exactly. LE1, VR on foot, the gun aimed by arm IK.
// Game thread.
namespace mevr::game::gun_kick {

// One of the player's shots (game/view_level's SetWeaponRecoil handler): the kick the game queued for the view, degrees (0
// or less: none measured, at the recoil cap or before the first PlayerTick).
void NoteShot(double queuedRecoilDeg);

// Each mod tick, before arm IK: the shots since the last tick kick (only while arm IK aims the gun in the right hand and
// [Hands] gunKick is on), and the spring steps on.
void Tick(double deltaSeconds);

// This tick's kick as arm IK draws it (degrees muzzle up, meters back; shaped for the hands and the eye). Not `active`:
// none (off, still, nothing aimed).
struct Kick {
    bool active = false;
    double angleDeg = 0.0;
    double backM = 0.0;
};
Kick Current();

// `mevr kick [recoilDeg] [shots] [ms]`: synthetic shots for a test without enemies or ammo (default one shot of 1 degree).
std::string Command(std::string const& args);

// The snapshot's "gunKick" and the heartbeat's line.
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::gun_kick
