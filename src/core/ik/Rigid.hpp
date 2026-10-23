// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"

#include <optional>

// Rigid transforms for skeleton poses: unit quaternions (x, y, z, w; Hamilton product, active
// rotations) and bone atoms in UE3's FBoneAtom convention (scale, then rotate, then translate).
// The algebra doesn't depend on handedness, so it serves UE3's left-handed axes as is.
// Engine-agnostic; tested offline.
namespace mevr::ik {

struct Quat {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

// a * b applies b first, then a.
Quat operator*(Quat a, Quat b);
Quat Conjugate(Quat q);
double Norm(Quat q);
// Unit length; identity for a (near) zero quaternion.
Quat Normalize(Quat q);
Vec3 Rotate(Quat q, Vec3 v);
// The shortest rotation taking direction `from` to direction `to`.
Quat FromTo(Vec3 from, Vec3 to);
// The rotation taking the unit X, Y, Z axes to `x`, `y`, `z` (orthonormal, z = x cross y).
Quat FromAxes(Vec3 x, Vec3 y, Vec3 z);
// The angle between two rotations, radians (0..pi).
double AngleBetween(Quat a, Quat b);

// p -> rotation(scale * p) + translation, like UE3's FBoneAtom.
struct Transform {
    Quat rotation;
    Vec3 translation;
    double scale = 1.0;
};

Vec3 Apply(Transform const& t, Vec3 p);
// `local` then `parent` (UE3: LocalAtom * ParentSpaceBase): Apply(Compose(a, b), p) == Apply(b, Apply(a, p)).
Transform Compose(Transform const& local, Transform const& parent);
Transform Inverse(Transform const& t);
// `child` in `parent`'s frame: Compose(Relative(child, parent), parent) == child.
Transform Relative(Transform const& child, Transform const& parent);

// A 4x4 affine matrix in UE3's FMatrix layout: points are row vectors, rows 0-2 are the images
// of the local X, Y, Z axes and row 3 is the origin (p' = p.x*M[0] + p.y*M[1] + p.z*M[2] + M[3]).
struct Affine {
    double m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
};

Vec3 TransformPoint(Affine const& a, Vec3 p);
Vec3 TransformDirection(Affine const& a, Vec3 v);
// Nullopt if the 3x3 part is singular.
std::optional<Affine> InverseAffine(Affine const& a);
double Determinant3(Affine const& a);
// The rotation of an affine whose 3x3 part is a rotation times a uniform positive scale; nullopt
// if it shears, mirrors, scales unevenly or degenerates (beyond `tolerance`, relative).
std::optional<Quat> RotationOf(Affine const& a, double tolerance = 1e-3);

} // namespace mevr::ik
