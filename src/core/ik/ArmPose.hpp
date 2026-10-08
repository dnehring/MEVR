#pragma once

#include "core/ik/Rigid.hpp"

#include <optional>
#include <span>
#include <string>

// Arm IK on a whole skeleton pose: component-space bone transforms indexed like the skeleton,
// parents before children (UE3's RefSkeleton order). The upper arm stays where the animation put
// it; the elbow and wrist move to reach the target; every bone below the upper arm (forearm twist
// bones, the hand, fingers, props on the hand) keeps its transform relative to the arm segment it
// hangs from. Engine-agnostic; tested offline.
//
// Adapted from Overlord by Aeka0 (https://github.com/Aeka0/Overlord, GPL-3.0),
// src/client/component/vr/gameplay/hands/pose_solver.hpp: the bend-plane arm orientation
// (orient_arm, arm_plane_normal, arm_segment_frame), the swing-transported elbow pole
// (elbow_pole) and the rigid subtree transfer (transformed). See NOTICE.md.
namespace mevr::ik {

// Bone indices of one arm; -1 = not found.
struct ArmChain {
    int upper = -1;  // upper arm: the shoulder joint
    int fore = -1;   // forearm: the elbow joint
    int hand = -1;   // hand: the wrist joint
};

// The arm on `side` (0 left, 1 right): the hand by name (HandSide), then its nearest ancestors
// named like a forearm (elbow, forearm, lowerarm) and an upper arm (shoulder, upperarm, arm),
// skipping twist/roll/swivel helpers. Without such names, the nearest non-helper ancestors.
// `parents[i]` is bone i's parent (-1 for the root).
std::optional<ArmChain> FindArmChain(std::span<std::string const> names, std::span<int const> parents, int side);

// Body axes in the pose's space (unit, orthogonal).
struct BodyFrame {
    Vec3 forward{1.0, 0.0, 0.0};
    Vec3 right{0.0, 1.0, 0.0};
    Vec3 up{0.0, 0.0, 1.0};
};

struct ArmGoal {
    Vec3 wrist;  // where the hand bone goes
    bool hasRotation = false;
    Quat rotation;  // the hand bone's rotation; without one the hand keeps its pose on the forearm
};

struct ArmResult {
    bool ok = false;
    char const* error = nullptr;  // why nothing was written
    bool reached = false;         // false: out of reach, the straight arm points at the target
    Vec3 shoulder;
    Vec3 oldElbow;
    Vec3 oldWrist;
    Vec3 newElbow;
    Vec3 newWrist;
    double upperLength = 0.0;
    double lowerLength = 0.0;
    int bonesWritten = 0;
};

// The direction (unit, perpendicular to `reach`) the elbow bends toward for a reach direction:
// out to the side and down at rest, swung along with the reach.
Vec3 ElbowPoleDirection(Vec3 reach, BodyFrame const& body, int side);

// Transactional: rewrites the arm chain and every bone below the upper arm, or nothing (`error`
// says why). Bone lengths are kept; a target out of reach gets a straight arm pointing at it.
ArmResult PoseArm(std::span<Transform> pose, std::span<int const> parents, ArmChain const& chain, int side,
    BodyFrame const& body, ArmGoal const& goal);

} // namespace mevr::ik
