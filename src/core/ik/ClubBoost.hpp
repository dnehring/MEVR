// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core/ik/Rigid.hpp"

// The club dancers' boost (the owner, 2026-11-19/20; docs/trilogy-hair-plan.md section 12): every female NPC in a club level
// (Chora's Den, Afterlife, Purgatory) gets more jiggle and a natural size boost on the soft-tissue bones tools/rig added:
// breasts up to a cap (10% larger than the LE3 Liara romance mesh, per body from the rig index's "breastMax"), glutes about a
// tenth, each person her own amounts, the same on every visit (a stable hash of her actor's object path and her body mesh).
// Engine-agnostic; game/Hair decides who, engine/HairPose grows the bones. tests/ClubBoostTests.cpp.
//
// The growth: the game's skinning takes each bone as an FBoneAtom (rotation, translation and ONE uniform scale), so a bone
// can only grow a vertex uniformly about a point. The point is chosen per region (`Grow`'s pivot, in the body's bind frame):
// at the top of the breast or glute, so the region grows forward (backward) and down, never up, a larger copy of its own
// shape hanging from its upper edge: a vertex's share on the bone goes to Q + (1 + a) (v - Q).
namespace mevr::ik::club {

// FNV-1a 64 of the text, mixed (splitmix64): the same on every run, platform and build.
std::uint64_t StableHash(std::string_view text);

// True when the lower-case object path holds one of the lower-case name parts (an empty part never matches).
bool InClub(std::string_view lowerPath, std::span<std::string const> parts);

// A female body (lower-case mesh path): the human female and asari skeletons' meshes (hmf_, asa_).
bool FemaleBody(std::string_view lowerMeshPath);

struct SizeSettings {
    double breastMin = 0.4;        // [Hair] clubBreastMin: the least share of the way to the cap a person gets
    double breastMaxDefault = 1.15;  // a body whose index entry has no breastMax
    double gluteMid = 0.12;        // [Hair] clubGlute: the middle dancer's glute amount
    double gluteLow = 0.10;        // [Hair] clubGluteRange: the least and most
    double gluteHigh = 0.16;
};

// Each region's growth amount a (the bone grows by 1 + a); 0 leaves it as the animation has it.
struct Sizes {
    double breast = 0.0;
    double glute = 0.0;
};

// Each person's amounts from her key ("<actor path>|<body mesh path>", lower case): breast a = u (breastMax - 1) with u in
// [breastMin, 1] (breastMax clamped to [1, 1.8], the indexes holding up to 1.772; a body already the cap's size is left as it
// is; breastMax <= 0: the default); glute a from the low end through gluteMid (u = 0.5) to the high end, clamped to [0, 0.4].
Sizes SizesFor(std::string_view key, double breastMax, SizeSettings const& settings = {});

// On a club copy of her body (the approved shape baked in by tools/rig, the owner's option 1, 2026-11-20) a person keeps only
// a small variation of her own, from the same hash: uniform growth a in [-breastVary, +breastVary] for the breasts and
// [-gluteVary, +gluteVary] for the glutes (a may be negative: smaller). The breasts' is held so the baked amount and the
// variation together stay within the cap: (1 + clubBreastA) (1 + a) <= breastMax (breastMax <= 0: the default; clubBreastA
// < 0: none baked, 0).
Sizes VariationFor(std::string_view key, double breastMax, double clubBreastA, double breastVary, double gluteVary,
    SizeSettings const& settings = {});

// Without a club copy the uniform growth stands in, gentler than SizesFor's (the uniform shape at full size was rejected):
// SizesFor's amounts scaled so the breasts reach at most `breastLimit` (still within the cap) and the glutes `gluteLimit`.
Sizes FallbackSizes(std::string_view key, double breastMax, SizeSettings const& settings = {}, double breastLimit = 0.15,
    double gluteLimit = 0.08);

// The pivot in the bone's own frame: `offset` given in the body's bind frame (mesh units: x forward, y left, z up) from the
// bone's bind origin, turned into the bone's bind axes.
Vec3 PivotLocal(Transform const& bind, Vec3 offset);

// A bone's component-space transform grown by 1 + a about `pivotLocal` (in the bone's own frame, scaled as the bone): a vertex
// skinned to it goes to Q + (1 + a) (v - Q), Q the pivot where the bone is now drawn; the rotation kept, the pivot moving with
// the bone.
Transform Grow(Transform const& bone, double a, Vec3 pivotLocal);

} // namespace mevr::ik::club
