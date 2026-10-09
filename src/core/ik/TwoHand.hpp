#pragma once

#include "core/ik/Rigid.hpp"

#include <optional>

// A gun held with both hands (the support grip, game/support_grip): where the off hand goes on the
// gun and how the gun turns when it does. Positions in the space of the caller's choice (component
// space in the writer); "in hand" means the rear (right) hand bone's own axes. Engine-agnostic;
// tested offline.
//
// TwoHandRotation is adapted from Overlord (https://github.com/Aeka0/Overlord, GPL-3.0, Aeka0),
// src/client/component/vr/gameplay/weapon_profile.hpp `aimed_rotation` (swing the authored
// grip-to-grip vector onto the hands' vector, keeping the rear hand's roll) and grip_presenter.hpp
// (hands too close together define no axis).
namespace mevr::ik {

// Spherical interpolation from `a` (t = 0) to `b` (t = 1), the short way round.
Quat Slerp(Quat a, Quat b, double t);

// The rear hand's rotation while the other hand holds the gun: `rear` turned by the shortest
// rotation that takes the off hand's place on the gun (`spanInHand`, in the rear hand bone's axes,
// so `Rotate(rear, spanInHand)` now) onto the direction from `rearPosition` to `supportPosition`.
// The barrel then points along the line between the hands, and the roll about it stays the rear
// hand's (no world-up singularity). Nullopt (keep `rear`) when the hands are closer together than
// `minDistance`, the span is (nearly) zero, or anything isn't finite.
std::optional<Quat> TwoHandRotation(Quat rear, Vec3 spanInHand, Vec3 rearPosition, Vec3 supportPosition,
    double minDistance);

// A candidate off-hand point on the gun, in the rear hand bone's axes (units): plausible if it
// isn't on the rear hand itself (`kMinSpan`), lies between `kBehindGrip` behind the hand and
// `kPastMuzzle` past the muzzle along the barrel, and within `kOffBarrel` of the barrel's line.
// `barrelInHand` (any length) and `muzzleInHand` come from game/weapon's grip measurement.
inline constexpr double kForegripMinSpan = 4.0;
inline constexpr double kForegripBehindGrip = 20.0;
inline constexpr double kForegripPastMuzzle = 5.0;
inline constexpr double kForegripOffBarrel = 20.0;
bool OnGun(Vec3 point, Vec3 barrelInHand, Vec3 muzzleInHand);

// Along the barrel from the rear hand, and how far off the barrel's line (units), for logs and for
// deciding whether two hands define the gun's axis.
struct GunOffsets {
    double along = 0.0;
    double off = 0.0;
};
GunOffsets OffsetsOnGun(Vec3 point, Vec3 barrelInHand, Vec3 muzzleInHand);

// Where the off hand cups a gun with no foregrip (pistols): under the rear hand's grip, `down`
// units below it (against the gun's up, the thumb side) and `forward` units along the barrel.
Vec3 UnderGrip(Vec3 barrelInHand, Vec3 upInHand, double down = 8.0, double forward = 2.0);

} // namespace mevr::ik
