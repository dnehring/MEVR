// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/HandContact.hpp"
#include "core/ik/Rigid.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A body's surface for the hair simulation (tools/rig/README.md "Body spheres"): spheres just under the skin of the neck, upper
// torso, shoulders and breasts, fitted by the rig tool to the mesh's own vertices (`mevr-rig colliders`, written with every body
// it builds: %LOCALAPPDATA%\MEVR\rig\<GAME>\colliders\<mesh>.json), each carried rigidly by one bone. The hair is pushed out of
// them by its whole radius (ik::hair::Input::surface), so it lies on this body. Engine-agnostic; tests/BodySpheresTests.cpp.
namespace mevr::ik::body_spheres {

// As the file has it: the bone's name, the centre in the mesh's bind pose (component space, game units), the radius (game units).
struct Fitted {
    std::string bone;
    Vec3 centre;
    double radius = 0.0;
    Vec3 normal;  // the skin's outward side where the sphere touches it (unit; zero in a file without one)
};
// The `mevr-body-spheres` version 1 file. Nullopt (and `error`) if it isn't one.
std::optional<std::vector<Fitted>> Parse(std::string_view text, std::string* error = nullptr);

// Bound to a skeleton: the bone's index and the centre in that bone's own frame (game units).
struct Sphere {
    int bone = -1;
    Vec3 local;
    double radius = 0.0;
    Vec3 localNormal;  // the outward side in the bone's frame
};
// `names` and `bindWorld` the skeleton's bones and their bind pose in component space (W_child = W_parent * L composed from the
// reference skeleton's locals, game units). Spheres on a bone the skeleton lacks are left out (`missing` counts them).
std::vector<Sphere> Bind(std::span<Fitted const> fitted, std::span<std::string const> names, std::span<Transform const> bindWorld,
    int* missing = nullptr);

// This pose's spheres as capsules with both ends at the centre: `world` each bone's transform now (the space the hair simulates
// in), `metresPerUnit` from game units to that space's (1 / units per metre for metres).
// `normals` (if given) gets each placed sphere's outward side, in the same order.
void Place(std::span<Sphere const> spheres, std::span<Transform const> world, double metresPerUnit,
    std::vector<hand_contact::Capsule>& out, std::vector<Vec3>* normals = nullptr);

} // namespace mevr::ik::body_spheres
