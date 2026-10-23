#pragma once

#include "core/ik/ArmLength.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/Posture.hpp"
#include "core/ik/Rigid.hpp"
#include "core/vr/Sights.hpp"
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
//           noCover, physicalCrouch, crouchBelow, standAbove, crouchDwellS: physical cover and
//                                         crouch (below, GetPosture)
//   [Arms]  armLengthM                   the player's shoulder-to-grip length (meters); 0 or absent:
//                                         reach isn't scaled. Written by MeasureArmLength.
//           measureRequest, measure*      the VR hands tab's T-pose sample (src/plugin/MevrMenuTab.cpp,
//                                         contract at its top): each new measureRequest value is
//                                         measured (ik::ArmLengthFromTPose) and saved as armLengthM.
//   [Controls] supportGrip, supportAcquireM, supportReleaseM, sniperAims, leftTrigger: the
//                                         two-handed grip (game/support_grip) and whether the left
//                                         trigger reaches the game as LT.
//   [Holsters] body holsters (below, GetHolsters).
//   [Squad]  the squad weapon menu (below, GetSquad).
//   [Throwables] physical grenades and melee (below, GetThrowables).
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
// [Squad] (appended to an older file that lacks it; keys added later are appended with their defaults):
// squad weapons (game/squad_weapons, docs/squad-weapons-design.md).
//   enabled           1: point the left hand at a squadmate and squeeze the left grip (away from the gun's
//                     foregrip) to open the menu at the hand
//   style             native (default): the game's own weapon wheel (ME1's look, pause and rules), shown on
//                     a panel at the hand; custom: our drawn menu (the fallback)
//   pointConeDeg      how close to the left hand's aim ray a squadmate must be to be "pointed at"
//   pointMaxM         ...and how far away at most (meters)
//   openHoldS         pointing at nobody: the grip held this long (seconds) opens it anyway
//   panelDistanceM    the menu's (or wheel's) centre from the hand when it opens (meters)
//   panelWidthM       its width (meters)
//   confirmOnRelease  1: letting go of the grip gives the chosen weapon (nothing chosen: cancels);
//                     0: custom: it stays open after letting go (A gives it, B or a new squeeze cancels);
//                     native: letting go closes it, only A (the game's own) queues a weapon
//   pause             custom only: 1 (default) the game pauses while it's open, as ME1's wheels do; 0 it runs.
//                     (The native wheel always pauses: it's the game's.)
//   hapticAmplitude   0..1: its ticks and clicks (0.5 as designed; 0: none)
//   wheelStick        native: the stick that steers the wheel: right (the game's) | left | either
//   wheelPreselect    native: 1 = pointing at a squadmate puts the wheel on their weapon in hand
//   wheelPanel        1: the game's weapon and power wheels show on a panel of their own (any way they open)
//   weaponWheelAt     where that panel goes for the weapon wheel: left (hand) | right | front | hud
//   powerWheelAt      ...for the power wheel (R3 held): right | left | front | hud (stays on the HUD panel)
//   wheelCrop         the wheel's part of the UI frame: auto (measured the first time) or x, y, width, height
//                     as fractions of the frame
struct Squad {
    bool enabled = true;
    bool native = true;
    double pointConeDeg = 10.0;
    double pointMaxM = 40.0;
    double openHoldSeconds = 0.35;
    double panelDistanceM = 0.27;
    double panelWidthM = 0.40;
    bool confirmOnRelease = true;
    bool pause = true;
    double hapticAmplitude = 0.5;
    gamepad::WheelStick wheelStick = gamepad::WheelStick::Right;
    bool wheelPreselect = true;
    bool wheelPanel = true;
    int weaponWheelAt = 0;  // 0 left hand, 1 right hand, 2 in front, -1 the HUD panel
    int powerWheelAt = -1;  // hud (user, 2026-10-22: the right hand is busy with the gun)
    double wheelCrop[4] = {0.0, 0.0, 0.0, 0.0};  // width 0: auto
};
Squad GetSquad();
// [Throwables] (appended to an older file that lacks it): physical grenades and melee (game/throwables,
// research/20).
//   enabled          1: take a grenade from a slot on the body with a grip, throw it by letting go
//   hands            right (default) | left | both: which hands may take grenades
//   slot             right, up, forward, radius: the grenade slot, meters from the eyes as the holsters'
//                    slots (default the left side of the chest)
//   throwSpeedMinMS  a release slower than this (m/s) is no throw: the grenade goes back
//   throwGain        the hand's speed is multiplied by this for the throw (Overlord: 2.5)
//   throwSpeedMaxMS  the hand's speed is capped at this (m/s) before the gain
//   flight           disc (default): the game's own grenade (a disc that flies straight and sticks), sent
//                    where the throw would land; arc: the game's unused ballistic grenade, thrown with the
//                    hand's velocity (experimental)
//   fromHand         1: the grenade starts at the hand that threw it
//   drawForThrow     1: taking a grenade with the weapon away draws it (ME1 only throws with a weapon out)
//   earlyRelease     the analog grip lets go this far below its peak while the hand moves fast (0: off)
//   grenadeButton    1: X still throws a grenade the game's way; 0: only the physical throw (on foot)
//   melee            1: a fast stroke of a hand toward an enemy in reach is the game's melee
//   meleeSpeedMS     the free hand's speed for a strike (m/s, relative to the head)
//   meleeGunSpeedMS  the gun hand's
//   meleeTravelM     the stroke's travel at that speed (m)
//   meleeCooldownS   seconds between strikes (the game adds its own: 0.96 s pistol, 1.38 s rifle)
//   meleeNeedsEnemy  1: only with a living hostile within meleeReachM and meleeConeDeg of the body's front
//   meleeReachM, meleeConeDeg
//   meleeButton      1: B still melees; 0: only the physical stroke (on foot)
//   hapticAmplitude  0..1 (0.5 as designed; 0: none); holdBuzz: 1 = a faint buzz while a grenade is held
struct Throwables {
    bool enabled = true;
    bool leftHand = false;  // right hand only by default (user, 2026-10-22)
    bool rightHand = true;
    ik::body_slots::Slot slot{"grenade", ik::body_slots::Weapon::None, {-0.13, -0.33, 0.0}, 0.10};
    double throwSpeedMin = 1.5;
    double throwGain = 2.5;
    double throwSpeedMax = 15.0;
    bool arc = false;
    bool fromHand = true;
    bool drawForThrow = true;
    double earlyRelease = 0.3;
    bool grenadeButton = true;
    bool melee = true;
    double meleeSpeed = 3.0;
    double meleeGunSpeed = 3.5;
    double meleeTravel = 0.12;
    double meleeCooldown = 0.8;
    bool meleeNeedsEnemy = true;
    double meleeReach = 2.0;
    double meleeConeDeg = 75.0;
    bool meleeButton = true;
    double hapticAmplitude = 0.5;
    bool holdBuzz = true;
};
Throwables GetThrowables();
// [Hands], physical cover and crouch (game/posture, research/23; keys appended to an older file with
// their defaults). Both act only in VR on foot (not in the Mako, conversations, menus or cutscenes).
//   noCover          1: the game's cover snapping is off (take cover by crouching or leaning behind things);
//                    a cover the player is already in is left the A button's way
//   physicalCrouch   1: crouching for real crouches Shepard (the game's crouch: a smaller target, ducked
//                    behind low cover); standing up stands him. The crouch button (L3) still toggles it, and
//                    stays until the next physical crouch or stand
//   crouchBelow      crouch when the eyes drop below this fraction of the standing eye height (0.75)
//   standAbove       stand again above this fraction (0.85)
//   crouchDwellS     ...once the head has stayed past it this long (seconds, 0.2)
struct Posture {
    bool noCover = true;
    bool physicalCrouch = true;
    ik::posture::CrouchParams crouch;
};
Posture GetPosture();
// [Sights] (appended to an older file that lacks it): gun sights (game/sights, research/21; the keys and
// their meaning are in a new file's block and core/vr/Sights.hpp's Settings).
vr::sights::Settings GetSights();
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
