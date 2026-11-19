// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Hand contact with other characters (research/73, the owner 2026-11-18: "I can't have my hand go through other characters";
// the design they chose: the drawn hand stops at the person's body and slides along it while the controller goes on, with a
// haptic pulse; nobody is pushed). Each person near the player is a set of capsules built from their own skeleton's bones
// (PartsFor: by bone name tokens, any rig's naming), and each hand's final arm-IK goal is a sphere pushed out of them
// (PushOut) right before the arm is solved (engine/BonePose). Engine-agnostic; tested offline (tests/HandContactTests.cpp).
namespace mevr::ik::hand_contact {

// A body part: what it is decides its thickness.
enum class Part : std::uint8_t { None, Spine, Collar, Neck, Head, UpperArm, Forearm, Hand, Thigh, Shin, Foot };
char const* PartName(Part part);
// The part a bone's name says it starts (LeftShoulder starts the upper arm, LeftElbow the forearm), or None: a bone the
// colliders never use (fingers, face, props, IK and camera helpers, twist bones, the root).
Part Classify(std::string_view boneName);
// A part's radius in metres (an adult human's; the krogan's and the elcor's are [verify]).
double RadiusMeters(Part part);

// One capsule of a skeleton: the segment from bone `from` to bone `to`, or (extendMeters > 0) from `to` on along
// `from` -> `to` by that much (the head above the neck bone, the hand past the wrist).
struct BonePart {
    Part part = Part::None;
    int from = -1;
    int to = -1;
    double extendMeters = 0.0;
};
// The capsules a skeleton has (names and parents as its RefSkeleton has them). A bone the colliders don't use (Classify's
// None) is passed over to its parent, so a twist bone between the shoulder and the elbow doesn't break the upper arm.
std::vector<BonePart> PartsFor(std::vector<std::string> const& names, std::vector<int> const& parents);

struct Capsule {
    Vec3 a;
    Vec3 b;
    double radius = 0.0;
    Part part = Part::None;
};
// The capsule of `part` with the bones at `positions` (any one space; `unitsPerMeter` of it). Nullopt if a bone is out of
// range or not finite, or the segment is implausible (under 1 cm or over 1 m).
std::optional<Capsule> CapsuleOf(BonePart const& part, std::span<Vec3 const> positions, double unitsPerMeter);

// The closest point to `p` on the segment a-b.
Vec3 ClosestOnSegment(Vec3 p, Vec3 a, Vec3 b);

struct Result {
    Vec3 point;              // the hand's point, outside every capsule (by `handRadius`)
    bool touching = false;   // the hand reached a capsule
    double depth = 0.0;      // how deep the hand went in (the largest overlap at the controller's point; same units)
    int capsule = -1;        // the capsule it went deepest into
    Vec3 normal;             // the way that capsule pushed it (unit), when touching
};
// The hand, a sphere of `handRadius` at `point`, pushed out of the capsules, sliding along them (each overlap removed along
// the line from the capsule's axis; a few passes for capsules that overlap each other). `previous` is the point this
// returned last time (same space) if the hand was in contact then: a hand pushed past a capsule's axis stays on the side
// it came from instead of popping through to the far side.
Result PushOut(Vec3 point, double handRadius, std::span<Capsule const> capsules, std::optional<Vec3> previous = std::nullopt);

// The pulse while touching (core of the haptics; game/HandContact plays it): the overlap as a share of the hand's radius
// to an amplitude between `minAmplitude` and `maxAmplitude`.
double PressAmplitude(double depth, double handRadius, double minAmplitude = 0.15, double maxAmplitude = 0.6);

} // namespace mevr::ik::hand_contact
