#pragma once

#include "core/ik/ArmLength.hpp"
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

// Arm-length calibration API (for a menu, a key or a test; the VR hands tab reaches it through the
// ini instead): measures one T-pose sample (tracking space, meters), and if it is one, saves the
// result as [Arms] armLengthM and uses it from now on (logged). Nullopt: not a T pose, unchanged.
std::optional<ik::ArmLengths> MeasureArmLength(ik::ArmLengthSample const& sample);
// Forgets the calibration (writes armLengthM = 0).
void ClearArmLength();

// Checks the file at most once a second; logs when the values change; measures new samples.
void Refresh();

} // namespace mevr::game::hand_calibration
