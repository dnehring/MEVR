#pragma once

#include "core/ik/ArmLength.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/Rigid.hpp"

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
// [Holsters] (appended to an older file that lacks it): body holsters (game/holsters, research/19).
//   enabled          1: reach to a slot on the body and squeeze the right grip to draw its weapon
//   weaponWheel      1: the game's weapon wheel stays on its button (LB) as a fallback; 0: no LB
//                    from the left trigger or grip while holsters are on
//   holster          0 off; 1 squeeze and release at the held gun's own slot; 2 also at an empty slot
//   eyeHeightM       the player's standing eye height: scales the slot offsets (1.65 = as written)
//   hapticAmplitude  0..1, the pulse on entering a slot (0: no vibration)
//   hipRight, shoulderRight, belly, shoulderLeft = weapon, right, up, forward, radius
//                    weapon: pistol | shotgun | rifle | sniper | none; meters from the eyes (held
//                    upright), in the body's frame, at a 1.65 m eye height
struct Holsters {
    bool enabled = true;
    bool weaponWheel = true;
    ik::body_slots::Holster holster = ik::body_slots::Holster::OwnSlot;
    ik::body_slots::Layout layout = ik::body_slots::DefaultLayout();
    double hapticAmplitude = 0.5;
};
Holsters GetHolsters();
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
