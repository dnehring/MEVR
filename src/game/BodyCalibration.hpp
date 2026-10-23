#pragma once

#include "core/ik/Posture.hpp"
#include "core/ik/TwoHand.hpp"
#include "core/vr/Calibration.hpp"

#include <optional>
#include <string>

// The guided calibration's results, as the mod uses them (research/25; the host runs the sequence,
// plugin/Calibration, and writes them). <GAME>MEVR.ini next to the host ASI:
//
//   [Calibration]           use = 1 (0: ignore the calibration: everything as before it existed)
//                           eye = auto | left | right (the next calibration's aiming eye), last, prompted
//   [Calibration.<key>]     one per controller type (the interaction profile and the headset,
//                           vr::calibration::ProfileKey), written whole by each save; the host says which key is
//                           current (HostServices v15), else [Calibration] last:
//     time                  when (a section without it isn't a calibration)
//     standEyeM, crouchEyeM the eyes above the floor standing tall and crouched (meters)
//     armLengthM, shoulderWidthM   from the T pose and both arms ahead
//     eye                   left | right: the eye the sight lines run from
//     pistolTrim, rifleTrim     x, y, z, w: the gun frame (-Z the sight line, +Y up) in the right GRIP frame
//     pistolLine, rifleLine     x, y, z: the sight line's point nearest the right grip, grip frame (meters)
//     pistolSpreadDeg, ...      how far the three aims disagreed; *EyeReliefM: the grip ahead of the eye
//     rifleForegrip         x, y, z: the left grip in the gun frame (origin the right grip; meters)
//     rifleLeftTrim         x, y, z, w: the left grip's orientation in the gun frame
//     rifleHandsLine        x, y, z: unit, right grip to left grip, gun frame
//     rifleStock            x, y, z: the right grip from the eyes, head's level frame (x right, y up, z back)
//
// Re-read within a second of the file changing, and whenever the host saves or the controllers change. Game
// thread.
namespace mevr::game::body_calibration {

struct Link {
    // HostServices v15 (sizes matching): the host's state; null with an older host.
    void (*state)(vr::calibration::HostState* out) = nullptr;
};

// Each mod tick, first: the host's state, and the settings file when it changed.
void Tick(Link const& link);
// The sequence runs: the mod stands down (the host hides the controllers too; this covers the physical crouch,
// which follows the head).
bool Calibrating();

// The stance for a weapon type (BioWeaponRanged's EBioItemWeaponRangedType: 0 pistol, 1 shotgun, 2 assault
// rifle, 3 sniper rifle): the pistol's for pistols, the rifle's for the rest. Nullopt: not calibrated (or
// [Calibration] use = 0): everything as before. Without a rifle stance (a calibration whose rifle step wasn't
// finished, or whose aims disagreed by more than 10 degrees), the long guns get the pistol's.
std::optional<vr::calibration::Stance> StanceFor(int weaponType);
// StanceFor gave a long gun the pistol's stance: its grip frame (where the barrel points) is used, its sight line (a
// pistol at arm's length) isn't, so arm IK doesn't move the gun onto it.
bool StanceBorrowed(int weaponType);
// The rifle stance with both hands measured (the two-handed grip), if any.
std::optional<vr::calibration::Stance> TwoHanded();

// Body measures (nullopt: not measured).
std::optional<double> StandingEyeHeight();
std::optional<double> ArmLength();
// Physical crouch thresholds between the measured standing and crouched eye heights
// (vr::calibration::CrouchFromHeights), with the dwell of `base`; nullopt: use `base` as it is.
std::optional<ik::posture::CrouchParams> CrouchParams(ik::posture::CrouchParams const& base);
// "At the shoulder" around the measured stock point (ik::ShoulderZoneAround); nullopt: the fixed zone.
std::optional<ik::ShoulderZone> ShoulderZone();

std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::body_calibration
