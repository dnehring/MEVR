// Adapted from Overlord by Aeka0 (https://github.com/Aeka0/Overlord), GPL-3.0:
// src/client/component/vr/gameplay/hands/pose_solver.hpp (orient_arm, arm_plane_normal,
// arm_segment_frame, elbow_pole, transformed, and solve_limb's stretch: both segments scale by
// clamp((distance + .001) / (upper + lower), 1, 1.25)). Changes: double precision on our own vector
// and quaternion types, one arm at a time on a UE3 component-space pose, the stretch limit per
// goal (default none), no weapon bones, and a per-bone segment tag instead of repeated ancestry
// walks.

#include "core/ik/ArmPose.hpp"

#include "core/ik/BoneNames.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace mevr::ik {
namespace {

using Tokens = std::vector<std::string>;

bool Has(Tokens const& tokens, std::string_view word) {
    for (std::string const& token : tokens) {
        if (token == word) {
            return true;
        }
    }
    return false;
}

bool HasAny(Tokens const& tokens, std::initializer_list<std::string_view> words) {
    for (std::string_view const word : words) {
        if (Has(tokens, word)) {
            return true;
        }
    }
    return false;
}

// -1: no side (or both); 0: left; 1: right.
int SideOf(Tokens const& tokens) {
    bool const right = HasAny(tokens, {"r", "right", "rt"});
    bool const left = HasAny(tokens, {"l", "left", "lf", "lt"});
    if (right == left) {
        return -1;
    }
    return right ? 1 : 0;
}

// Twist and roll bones, IK targets and swivel helpers sit in or beside the chain but aren't joints
// of it (ME's rig has e.g. "LeftShoulder-Swivel").
bool IsHelper(Tokens const& tokens) {
    return HasAny(tokens, {"roll", "twist", "swivel", "ik", "end", "nub", "helper", "dummy", "target", "pole", "bend"});
}

bool IsForearm(Tokens const& tokens) {
    return HasAny(tokens, {"forearm", "elbow", "lowerarm"}) || (Has(tokens, "arm") && HasAny(tokens, {"fore", "lower"}));
}

bool IsUpperArm(Tokens const& tokens) {
    return !IsForearm(tokens) && (HasAny(tokens, {"upperarm", "shoulder", "arm", "humerus"}));
}

// The nearest ancestor of `bone` (at most `maxSteps` up) that `accept` takes, or -1.
int NearestAncestor(std::span<int const> parents, int bone, int maxSteps, std::function<bool(int)> const& accept) {
    int current = parents[bone];
    for (int step = 0; step < maxSteps && current >= 0; ++step, current = parents[current]) {
        if (accept(current)) {
            return current;
        }
    }
    return -1;
}

bool IsAncestor(std::span<int const> parents, int ancestor, int bone) {
    for (int current = bone, guard = 0; current >= 0 && guard <= static_cast<int>(parents.size()); current = parents[current], ++guard) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool Finite(Quat q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

bool Sane(Transform const& t) {
    double const norm = Norm(t.rotation);
    return Finite(t.translation) && Finite(t.rotation) && std::isfinite(t.scale) && t.scale > 1e-6 && norm > 0.9 && norm < 1.1;
}

// The plane the arm bends in, as its normal. A straight arm has none: use the upper arm's own
// authored axes instead of a world axis that would change with the pose.
Vec3 ArmPlaneNormal(Vec3 upper, Vec3 lower, Quat shoulderRotation) {
    Vec3 const axis = Normalize(upper);
    Vec3 normal = Cross(axis, Normalize(lower));
    if (Length(normal) < 1e-4) {
        normal = Rotate(Normalize(shoulderRotation), {0.0, 1.0, 0.0});
        normal = normal - axis * Dot(normal, axis);
        if (Length(normal) < 1e-4) {
            normal = Rotate(Normalize(shoulderRotation), {0.0, 0.0, 1.0});
            normal = normal - axis * Dot(normal, axis);
        }
    }
    return Normalize(normal);
}

// A frame with X along the segment and Z along the bend-plane normal.
Quat SegmentFrame(Vec3 direction, Vec3 normal) {
    Vec3 const forward = Normalize(direction);
    Vec3 const side = Normalize(Cross(normal, forward));
    return FromAxes(forward, side, Cross(forward, side));
}

struct ArmRotations {
    Quat upper;
    Quat lower;
};

// Carries both segments through the same oriented bend plane: direction alone leaves the roll
// free, and independent shortest-arc swings can spin the elbow when a segment passes opposite its
// animated direction.
ArmRotations OrientArm(Vec3 oldUpper, Vec3 oldLower, Vec3 upper, Vec3 lower, Quat oldShoulder) {
    Vec3 const oldNormal = ArmPlaneNormal(oldUpper, oldLower, oldShoulder);
    Vec3 normal = Cross(Normalize(upper), Normalize(lower));
    if (Length(normal) < 1e-4) {
        // A straight new arm (target out of reach): keep the animated plane as far as the new
        // direction allows.
        normal = oldNormal - Normalize(upper) * Dot(oldNormal, Normalize(upper));
        if (Length(normal) < 1e-4) {
            normal = ArmPlaneNormal(upper, lower, oldShoulder);
        }
    }
    normal = Normalize(normal);
    return {
        Normalize(SegmentFrame(upper, normal) * Conjugate(SegmentFrame(oldUpper, oldNormal))),
        Normalize(SegmentFrame(lower, normal) * Conjugate(SegmentFrame(oldLower, oldNormal))),
    };
}

// A bone carried rigidly with its segment: the segment's pivot moves from `oldPivot` to
// `newPivot` and turns by `delta`.
Transform Carried(Transform const& old, Vec3 oldPivot, Vec3 newPivot, Quat delta) {
    return {Normalize(delta * Normalize(old.rotation)), newPivot + Rotate(delta, old.translation - oldPivot), old.scale};
}

} // namespace

std::optional<ArmChain> FindArmChain(std::span<std::string const> names, std::span<int const> parents, int side) {
    int const count = static_cast<int>(names.size());
    if (count == 0 || parents.size() != names.size()) {
        return std::nullopt;
    }
    for (int i = 0; i < count; ++i) {
        if (parents[i] < -1 || parents[i] >= i) {
            return std::nullopt;  // not parents-first: the walks below could loop
        }
    }
    std::vector<Tokens> tokens(names.size());
    for (int i = 0; i < count; ++i) {
        tokens[i] = NameTokens(names[i]);
    }
    constexpr int kMaxSteps = 4;
    // Hand candidates in index order (parents first: the wrist before props or helpers below it).
    std::vector<int> hands;
    for (int i = 0; i < count; ++i) {
        if (HandSide(names[i]) == side && !IsHelper(tokens[i])) {
            hands.push_back(i);
        }
    }
    // First any hand whose arm bones are named as such...
    for (int hand : hands) {
        int const fore = NearestAncestor(parents, hand, kMaxSteps, [&](int i) {
            return !IsHelper(tokens[i]) && IsForearm(tokens[i]) && SideOf(tokens[i]) == side;
        });
        int const upper = fore < 0 ? -1 : NearestAncestor(parents, fore, kMaxSteps, [&](int i) {
            return !IsHelper(tokens[i]) && IsUpperArm(tokens[i]) && SideOf(tokens[i]) == side;
        });
        if (fore >= 0 && upper >= 0) {
            return ArmChain{upper, fore, hand};
        }
    }
    // ...else, for unfamiliar names, the first hand's nearest real joints above it (never the root).
    auto const joint = [&](int i) { return !IsHelper(tokens[i]) && parents[i] >= 0; };
    for (int hand : hands) {
        int const parentJoint = NearestAncestor(parents, hand, kMaxSteps, joint);
        int const grandparentJoint = parentJoint < 0 ? -1 : NearestAncestor(parents, parentJoint, kMaxSteps, joint);
        if (parentJoint >= 0 && grandparentJoint >= 0) {
            return ArmChain{grandparentJoint, parentJoint, hand};
        }
    }
    return std::nullopt;
}

Vec3 ElbowPoleDirection(Vec3 reach, BodyFrame const& body, int side) {
    Vec3 const direction = Normalize(reach, body.forward);
    Vec3 const outward = side == 1 ? body.right : body.right * -1.0;
    Vec3 const rest = Normalize(outward * 0.6 - body.up);
    // Straight behind the body is an ambiguous half turn: turn about the bend itself.
    Quat const swing = Dot(body.forward, direction) < -0.9999 ? Quat{rest.x, rest.y, rest.z, 0.0} : FromTo(body.forward, direction);
    Vec3 const transported = Rotate(swing, rest);
    Vec3 const pole = transported - direction * Dot(transported, direction);
    if (Length(pole) < 1e-6) {
        Vec3 const down = body.up * -1.0;
        return Normalize(down - direction * Dot(down, direction), outward);
    }
    return Normalize(pole);
}

ArmResult PoseArm(std::span<Transform> pose, std::span<int const> parents, ArmChain const& chain, int side,
    BodyFrame const& body, ArmGoal const& goal) {
    ArmResult result;
    int const count = static_cast<int>(pose.size());
    if (parents.size() != pose.size()) {
        result.error = "pose and parents differ in size";
        return result;
    }
    if (chain.upper < 0 || chain.fore <= chain.upper || chain.hand <= chain.fore || chain.hand >= count) {
        result.error = "arm chain indices out of order or range";
        return result;
    }
    for (int i = chain.upper; i < count; ++i) {
        if (parents[i] < -1 || parents[i] >= i) {
            result.error = "skeleton not parents-first";
            return result;
        }
    }
    if (!IsAncestor(parents, chain.upper, chain.fore) || !IsAncestor(parents, chain.fore, chain.hand)) {
        result.error = "arm chain is not one branch of the skeleton";
        return result;
    }
    Transform const& upperBone = pose[chain.upper];
    Transform const& foreBone = pose[chain.fore];
    Transform const& handBone = pose[chain.hand];
    if (!Sane(upperBone) || !Sane(foreBone) || !Sane(handBone)) {
        result.error = "arm bone transform not finite or not a rotation";
        return result;
    }
    if (!Finite(goal.wrist) || (goal.hasRotation && (!Finite(goal.rotation) || Norm(goal.rotation) < 0.5))) {
        result.error = "target not finite";
        return result;
    }
    Vec3 const shoulder = upperBone.translation;
    Vec3 const elbow = foreBone.translation;
    Vec3 const wrist = handBone.translation;
    double const upperLength = Length(elbow - shoulder);
    double const lowerLength = Length(wrist - elbow);
    result.shoulder = shoulder;
    result.oldElbow = elbow;
    result.oldWrist = wrist;
    result.upperLength = upperLength;
    result.lowerLength = lowerLength;
    if (!(upperLength > 1e-3) || !(lowerLength > 1e-3)) {
        result.error = "arm bones have no length";
        return result;
    }

    Vec3 const reach = goal.wrist - shoulder;
    Vec3 const pole = ElbowPoleDirection(Length(reach) > 1e-6 ? reach : body.forward, body, side);
    // Overlord's stretch: out of reach, both segments lengthen together, up to the limit.
    double const limit = std::isfinite(goal.maxStretch) ? std::clamp(goal.maxStretch, 1.0, kMaxArmStretch) : 1.0;
    double const stretch = std::clamp((Length(reach) + 1e-3) / (upperLength + lowerLength), 1.0, limit);
    TwoBoneResult const solved = SolveTwoBone(shoulder, goal.wrist, upperLength * stretch, lowerLength * stretch,
        shoulder + pole * upperLength);
    ArmRotations const rotations = OrientArm(elbow - shoulder, wrist - elbow, solved.mid - shoulder, solved.end - solved.mid,
        upperBone.rotation);
    Quat const handDelta = goal.hasRotation ? Normalize(Normalize(goal.rotation) * Conjugate(Normalize(handBone.rotation)))
                                            : rotations.lower;

    // Which segment each bone below the upper arm hangs from: 1 upper arm, 2 forearm, 3 hand.
    thread_local std::vector<unsigned char> segment;
    thread_local std::vector<Transform> written;
    segment.assign(pose.size(), 0);
    segment[chain.upper] = 1;
    segment[chain.fore] = 2;
    segment[chain.hand] = 3;
    for (int i = chain.upper + 1; i < count; ++i) {
        if (segment[i] == 0 && parents[i] >= chain.upper) {
            segment[i] = segment[parents[i]];
        }
    }
    written.assign(pose.begin(), pose.end());
    int bones = 0;
    for (int i = chain.upper; i < count; ++i) {
        switch (segment[i]) {
        case 1: written[i] = Carried(pose[i], shoulder, shoulder, rotations.upper); break;
        case 2: written[i] = Carried(pose[i], elbow, solved.mid, rotations.lower); break;
        case 3: written[i] = Carried(pose[i], wrist, solved.end, handDelta); break;
        default: continue;
        }
        if (!Finite(written[i].translation) || !Finite(written[i].rotation)) {
            result.error = "solution not finite";
            return result;
        }
        ++bones;
    }
    for (int i = chain.upper; i < count; ++i) {
        if (segment[i] != 0) {
            pose[i] = written[i];
        }
    }
    result.ok = true;
    result.reached = solved.reached;
    result.stretch = stretch;
    result.newElbow = solved.mid;
    result.newWrist = solved.end;
    result.bonesWritten = bones;
    return result;
}

} // namespace mevr::ik
