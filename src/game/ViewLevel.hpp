#pragma once

#include <string>

// The game camera kept level while the hand aims (research/24; the rules: core/vr/ViewLevel.hpp).
//
// Why: LE1 adds each shot's recoil to the player controller's rotation (BioPlayerInput.UpdateViewRotation,
// inside PlayerController.PlayerTick: SFXInventoryManager.UpdateKickback / UpdateKickbackFade), and the
// assault rifles' pitch kick never fades back (RecoilFadeSpeed = 0). With the stick's pitch off
// (Halcyon's decoupledPitch) and the hand aiming, nothing pulls it down again, so the camera Halcyon
// renders the head-tracked view from pitched up shot by shot: the world tilted (2026-10-22, about 30
// degrees after 204 shots). Aim assist's zoom snap (aimed fire turns on zoom) also turns the controller.
//
// What (LE1, [Hands] levelCamera = 1): a ProcessEvent post-handler on PlayerTick (every state's), the
// controller's tick that applies the input, the recoil and aim assist, puts ControlRotation's pitch and
// roll back to 0 and drops a pending zoom-snap target, before the cameras update (UWorld::Tick updates
// them after every actor has ticked), so the camera never shows the kick. Only on foot, with a head-
// tracked view, the hand aiming and the host's head aim off; never in cover, on a ladder, or in a camera
// mode with its own pitch (the sniper scope, cover, the Mako, cinematics). The mod's tick does the same
// if no PlayerTick was seen (one frame late).
//
// Recoil in the hand instead (LE1, [Hands] recoilHaptic, 0..1): a CallFunction post-handler on
// SFXInventoryManager.SetWeaponRecoil (each of the player's shots, from SFXPawn_Player.OnWeaponFired)
// pulses the right controller, and the left too while both hands hold the gun.
//
// Aim assist's friction and adhesion off while the hand aims (LE1, [Hands] aimAssist = 0, the default;
// research/24 section 7): with a gamepad, SFXModule_AimAssist.ProcessRotation scales the stick's turn by
// 1 - ViewFriction() (an enemy near the camera's centre slows it) and adds ViewAdhesion() (the view
// follows a target while a stick moves). The stick only turns the body in VR, so both turned the world.
// CallFunction post-handlers on the two script functions zero their results on foot, with a head-tracked
// view, the hand aiming and the host's head aim off, in a camera mode where the hand aims (not the sniper
// scope, the Mako, photo or cinematic modes). Nothing in the game is changed: once the conditions end (or
// the mod unloads) the game's values pass through again.
//
// Logs: the rotation as the game left it whenever its pitch or roll moved 0.5 degrees since the last line
// (`view level:`), the hold's and aim assist's state changes (`aim assist:`), and heartbeat summaries of the
// last minute (with how often friction and adhesion would have acted). Game thread.
namespace mevr::game::view_level {

// What the mod's tick knows that the handlers need (from the host, each tick).
struct Link {
    bool headTrackedView = false;  // the host shows a head-tracked world view
    bool headAim = false;          // the host's head aim writes ControlRotation
    bool handAim = false;          // the right controller aims and the host was told
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;  // HostServices v9; null: none
};

// Finds PlayerTick (every state's), SetWeaponRecoil and aim assist's ViewFriction and ViewAdhesion and
// registers the handlers. Call before the ProcessEvent and CallFunction hooks go in. False on LE2/LE3.
bool TryStartup();
// Each mod tick.
void Tick(Link const& link);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::view_level
