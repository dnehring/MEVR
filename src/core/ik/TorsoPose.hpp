#pragma once

#include "core/ik/ArmPose.hpp"
#include "core/ik/Rigid.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The upper body under the headset: the spine bends and twists so the shoulders sit under the
// player's head and face the body's yaw, whatever the animation did with them (a combat stance
// leans and turns them into the gun). The controller decides where things are, never the
// animation (Overlord's rule; its weapon arms are rebased at the view anchor). Works on the same
// whole-skeleton pose as PoseArm (component space, parents first) and moves bones the same way:
// each spine joint turns about its own position and carries everything above it rigidly (neck,
// head, clavicles, arms, props in the hands), so the skinning stays rigid and nothing stretches.
// Our own implementation. Engine-agnostic; tested offline.
namespace mevr::ik {

struct TorsoChain {
    std::vector<int> spine;              // bottom to top, e.g. LowerBack, Chest, Chest1, Chest2
    std::array<int, 2> collars{-1, -1};  // clavicles (left, right): the upper arms' parents; -1 none
};

// From the arm chains: the top of the spine is the nearest common ancestor of both upper arms;
// below it, its ancestors named like a spine (spine, chest, back, torso, abdomen, ...; at most
// six). Each clavicle is the bone between the top of the spine and its upper arm, if there is one
// (named like a collar or clavicle, or not a helper). Nullopt without both arms or a spine.
std::optional<TorsoChain> FindTorsoChain(std::span<std::string const> names, std::span<int const> parents,
    ArmChain const& left, ArmChain const& right);

struct TorsoGoal {
    // Where the reference bones' midpoint should end up (the player's head or eyes), and which
    // bones those are (Shepard's eyes, or his head bone; -1 unused). The upper body turns rigidly
    // about the bottom spine joint, twist first, then the lean that points the reference at the
    // goal (its distance from that joint can't change: the arms take up the rest).
    Vec3 head;
    std::array<int, 2> reference{-1, -1};
    Vec3 forward;  // the wanted facing (made horizontal against the body's up)
    // Limits: how far the spine bends (whole chain, about its bottom joint) and twists.
    double maxBendDegrees = 35.0;
    double maxTwistDegrees = 60.0;
    // False: twist only, no lean toward `head`.
    bool lean = true;
    // True: `head` is an offset from the reference's animated position, not a point. First
    // person: the headset's move from where it was recentered, so the lean follows the player's
    // own lean; aiming the eyes at the view itself bent Shepard toward Halcyon's camera, which
    // sits about 44 units from his eyes, and put both shoulders under it.
    bool relative = false;
    // This bone and its subtree keep their animated transforms (-1: none). First person: the
    // (hidden) head, since Halcyon's camera follows the head bone as written; moving it moved the
    // view, a loop that juddered whenever the player moved.
    int keepBone = -1;
};

struct TorsoResult {
    bool ok = false;
    char const* error = nullptr;
    Vec3 oldShoulders;     // the shoulders' midpoint, animated
    Vec3 wantedShoulders;  // where the goal puts it
    Vec3 newShoulders;     // where it ended up (short of wanted when a limit held it)
    Vec3 facing;           // the new facing (horizontal, unit)
    double twistDegrees = 0.0;
    double bendDegrees = 0.0;
    bool limited = false;  // a limit held it back
    int bonesWritten = 0;
};

// Transactional: rewrites the spine, its subtree, or nothing (`error` says why). `left` and
// `right` are the arm chains (their upper arms are the shoulders).
TorsoResult PoseTorso(std::span<Transform> pose, std::span<int const> parents, TorsoChain const& chain,
    ArmChain const& left, ArmChain const& right, BodyFrame const& body, TorsoGoal const& goal);

// A clavicle shrug toward a far hand target: when `wrist` is beyond `reachFraction` of the arm's
// length from the shoulder, the clavicle turns the shoulder toward the target by `fraction` of the
// angle between them as seen from the clavicle, at most `maxDegrees`. Carries its subtree. Returns
// the degrees turned (0: not needed or not possible).
double PoseCollar(std::span<Transform> pose, std::span<int const> parents, int collar, ArmChain const& arm, Vec3 wrist,
    double reachFraction = 0.85, double fraction = 0.5, double maxDegrees = 15.0);

// A fraction of a rotation (the same axis, `fraction` of the angle), for spreading one turn over
// several joints.
Quat Fraction(Quat q, double fraction);

} // namespace mevr::ik
