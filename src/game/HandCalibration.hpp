#pragma once

#include "core/ik/ArmLength.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/Rigid.hpp"
#include "game/GamepadMapping.hpp"

#include <optional>

// Arm IK settings, from <GAME>MEVR.ini next to the host ASI (written with defaults if missing; a
// key missing from an older file keeps its default; "key = value" and "key=value" both read, the
// latter as Halcyon's menu writes them). Re-read within a second whenever the file changes, so they
// can be tuned while the game runs. Game thread.
//
//   [Hands] handPitch, handYaw, handRoll  degrees: how the hands sit on the controllers (one set,
//                                         mirrored for the left: yaw and roll flip sign)
//           gunRoll                       degrees about a held gun's barrel (ik::GunUpInHand)
//           bodyFollowsHead               1: the spine puts the shoulders under the headset
//           armStretch                    1-1.25: how far the arms may stretch to reach
//           trackingScale                 Unreal units per meter for hands and head; 0: the view's
//   [Arms]  armLengthM                    the player's shoulder-to-grip length (meters); 0 or absent:
//                                         reach isn't scaled. Written by MeasureArmLength.
//           measureRequest, measure*      the VR hands tab's T-pose sample (src/plugin/MevrMenuTab.cpp,
//                                         contract at its top): each new measureRequest value is
//                                         measured (ik::ArmLengthFromTPose) and saved as armLengthM.
//   [Controls] supportGrip, supportAcquireM, supportReleaseM, sniperAims, leftTrigger: the
//                                         two-handed grip (game/support_grip) and whether the left
//                                         trigger reaches the game as LT.
//   [Holsters] body holsters (below, GetHolsters).
//   [Squad]  the squad weapon menu (below, GetSquad).
namespace mevr::game::hand_calibration {

// The trim for one hand (0 left, 1 right), in the grip's own axes (ik::GripTrim).
ik::Quat Trim(int side);
double GunRoll();
bool BodyFollowsHead();
double ArmStretch();
// 0: use the view's own scale.
double TrackingScale();
// The calibrated arm length (meters, shoulder to grip), 0 if not calibrated.
double ArmLength();
// [Controls] (appended to an older file that lacks it): the two-handed grip and the left trigger.
struct Controls {
    bool leftTrigger = false;     // leftTrigger = lt: the game's LT
    bool leftTriggerLB = true;    // leftTrigger = lb (the default): the weapon wheel
    bool gripLB = false;          // leftGripAway = lb: a left grip away from the gun is LB
    bool supportGrip = true;      // supportGrip
    double acquireMeters = 0.12;  // supportAcquireM
    double releaseMeters = 0.22;  // supportReleaseM
    bool sniperAims = false;      // sniperAims: the grip holds LT with a sniper rifle too
    bool aimAtShoulder = true;    // aimAtShoulder: aimed fire (LT) needs the gun at the shoulder too
};
Controls GetControls();
// [Holsters] (appended to an older file that lacks it; keys added later are appended to the section
// with their defaults): body holsters (game/holsters, research/19).
//   enabled          1: reach to a slot on the body and squeeze the right grip to draw its weapon
//   gripHolds        1 (default): the right grip holds the gun: squeeze at a slot to draw, keep it
//                    squeezed to carry, let go at the gun's slot to holster; RB moves to powerWheel.
//                    0: the old mapping (the grip is RB except a squeeze at a slot)
//   releaseGrace     seconds after letting go away from the gun's slot before it goes back (a new
//                    squeeze in time keeps it; never while the trigger is held); negative: it stays out
//   powerWheel       with gripHolds: the control that is RB (hold: power wheel, tap: mapped power):
//                    r3 (default) | y | x | b | none
//   weaponWheel      1: the game's weapon wheel stays on its button (LB) as a fallback; 0: no LB
//                    from the left trigger or grip while holsters are on
//   holster          0 off; 1 at the held gun's own slot; 2 also at an empty slot
//   eyeHeightFromFloor  1: scale by the standing eye height measured from the floor (host v10);
//                    eyeHeightM is the fallback. 0: always eyeHeightM
//   eyeHeightM       the player's standing eye height: scales the slot offsets (1.65 = as written)
//   hapticAmplitude  0..1, the strength of the contact haptics (0.5 as designed; 0: no vibration)
//   hipRight, shoulderRight, belly, shoulderLeft = weapon, right, up, forward, radius
//                    weapon: pistol | shotgun | rifle | sniper | none; meters from the eyes (held
//                    upright), in the body's frame, at a 1.65 m eye height
struct Holsters {
    bool enabled = true;
    bool gripHolds = true;
    double releaseGrace = 0.3;
    gamepad::RbSource powerWheel = gamepad::RbSource::RightStickClick;
    bool weaponWheel = true;
    ik::body_slots::Holster holster = ik::body_slots::Holster::OwnSlot;
    bool eyeHeightFromFloor = true;
    ik::body_slots::Layout layout = ik::body_slots::DefaultLayout();
    double hapticAmplitude = 0.5;
};
Holsters GetHolsters();
// [Squad] (appended to an older file that lacks it): the squad weapon menu (game/squad_weapons,
// docs/squad-weapons-design.md).
//   enabled           1: point the left hand at a squadmate and squeeze the left grip (away from the gun's
//                     foregrip) to open the menu at the hand
//   pointConeDeg      how close to the left hand's aim ray a squadmate must be to be "pointed at"
//   pointMaxM         ...and how far away at most (meters)
//   openHoldS         pointing at nobody: the grip held this long (seconds) opens it anyway
//   panelDistanceM    the menu's centre from the left hand when it opens (meters)
//   panelWidthM       its width (meters)
//   confirmOnRelease  1: letting go of the grip gives the chosen weapon (nothing chosen: cancels);
//                     0: it stays open after letting go (A gives it, B or a new squeeze cancels)
//   pause             1: the game pauses while it's open (as ME1's own wheels do); 0 (default): it runs
//   hapticAmplitude   0..1: its ticks and clicks (0.5 as designed; 0: none)
struct Squad {
    bool enabled = true;
    double pointConeDeg = 10.0;
    double pointMaxM = 40.0;
    double openHoldSeconds = 0.35;
    double panelDistanceM = 0.27;
    double panelWidthM = 0.40;
    bool confirmOnRelease = true;
    bool pause = true;
    double hapticAmplitude = 0.5;
};
Squad GetSquad();
// Where Halcyon's first-person camera sits from Shepard's head bone with the headset at the
// recenter point (units: forward, right, up in the view's yaw), for the menu's "Put the camera at
// Shepard's eyes" button (src/plugin/MevrMenuTab.cpp). Written as [Camera] measuredForward,
// measuredRight, measuredUp. Any thread; no-op when unchanged.
void WriteCameraOffset(double forward, double right, double up);

// Arm-length calibration API (for a menu, a key or a test; the VR hands tab reaches it through the
// ini instead): measures one T-pose sample (tracking space, meters), and if it is one, saves the
// result as [Arms] armLengthM and uses it from now on (logged). Nullopt: not a T pose, unchanged.
std::optional<ik::ArmLengths> MeasureArmLength(ik::ArmLengthSample const& sample);
// Forgets the calibration (writes armLengthM = 0).
void ClearArmLength();

// Checks the file at most once a second; logs when the values change; measures new samples.
void Refresh();

} // namespace mevr::game::hand_calibration
