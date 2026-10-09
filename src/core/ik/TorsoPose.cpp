#include "core/ik/TorsoPose.hpp"

#include "core/ik/BoneNames.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <string_view>

namespace mevr::ik {
namespace {

constexpr double kDegrees = 180.0 / std::numbers::pi;
constexpr int kMaxSpine = 6;
// Passes over the spine: each recomputes what's left from the current pose, so the chain's
// curvature (each joint turns about its own position) doesn't leave the shoulders short.
constexpr int kPasses = 3;

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool Finite(Quat q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

bool Has(std::vector<std::string> const& tokens, std::initializer_list<std::string_view> words) {
    for (auto const& token : tokens) {
        for (std::string_view const word : words) {
            if (token == word) {
                return true;
            }
        }
    }
    return false;
}

bool IsSpine(std::vector<std::string> const& tokens) {
    return Has(tokens, {"spine", "chest", "back", "torso", "abdomen", "lumbar", "thorax", "waist", "spn"});
}

bool IsHelperName(std::vector<std::string> const& tokens) {
    return Has(tokens, {"roll", "twist", "swivel", "ik", "end", "nub", "helper", "dummy", "target", "pole"});
}

bool IsAncestor(std::span<int const> parents, int ancestor, int bone) {
    for (int current = bone, guard = 0; current >= 0 && guard <= static_cast<int>(parents.size()); current = parents[current], ++guard) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

Vec3 Horizontal(Vec3 v, Vec3 up) {
    return v - up * Dot(v, up);
}

// The signed angle (radians) about `up` from `from` to `to`, both horizontal.
double SignedAngle(Vec3 from, Vec3 to, Vec3 up) {
    return std::atan2(Dot(Cross(from, to), up), Dot(from, to));
}

Quat About(Vec3 axis, double radians) {
    Vec3 const a = Normalize(axis, {0.0, 0.0, 1.0});
    double const s = std::sin(radians * 0.5);
    return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5)};
}

double Angle(Quat q) {
    Quat const n = Normalize(q);
    return 2.0 * std::acos(std::clamp(std::abs(n.w), 0.0, 1.0));
}

// Turns `bone` and everything below it about the bone's own position.
void TurnSubtree(std::span<Transform> pose, std::span<int const> parents, int bone, Quat delta, std::vector<unsigned char>& in) {
    int const count = static_cast<int>(pose.size());
    in.assign(pose.size(), 0);
    in[bone] = 1;
    for (int i = bone + 1; i < count; ++i) {
        in[i] = parents[i] >= 0 ? in[parents[i]] : 0;
    }
    Vec3 const pivot = pose[bone].translation;
    for (int i = bone; i < count; ++i) {
        if (in[i]) {
            pose[i].rotation = Normalize(delta * Normalize(pose[i].rotation));
            pose[i].translation = pivot + Rotate(delta, pose[i].translation - pivot);
        }
    }
}

Vec3 Midpoint(std::span<Transform const> pose, int a, int b) {
    return (pose[a].translation + pose[b].translation) * 0.5;
}

// Facing from the shoulder line (left to right shoulder): forward = right x up, horizontal.
Vec3 Facing(std::span<Transform const> pose, ArmChain const& left, ArmChain const& right, Vec3 up, Vec3 fallback) {
    Vec3 const across = pose[right.upper].translation - pose[left.upper].translation;
    return Normalize(Horizontal(Cross(across, up), up), fallback);
}

} // namespace

Quat Fraction(Quat q, double fraction) {
    Quat n = Normalize(q);
    if (n.w < 0.0) {
        n = {-n.x, -n.y, -n.z, -n.w};  // the short way round
    }
    double const angle = 2.0 * std::acos(std::clamp(n.w, -1.0, 1.0));
    double const s = std::sqrt(std::max(0.0, 1.0 - n.w * n.w));
    if (s < 1e-12) {
        return {};
    }
    return About({n.x / s, n.y / s, n.z / s}, angle * fraction);
}

std::optional<TorsoChain> FindTorsoChain(std::span<std::string const> names, std::span<int const> parents,
    ArmChain const& left, ArmChain const& right) {
    int const count = static_cast<int>(names.size());
    if (count == 0 || static_cast<int>(parents.size()) != count || left.upper < 0 || right.upper < 0 || left.upper >= count ||
        right.upper >= count) {
        return std::nullopt;
    }
    for (int i = 0; i < count; ++i) {
        if (parents[i] < -1 || parents[i] >= i) {
            return std::nullopt;
        }
    }
    // The nearest common ancestor of both upper arms, excluding the upper arms themselves.
    int top = -1;
    for (int a = parents[left.upper]; a >= 0; a = parents[a]) {
        if (IsAncestor(parents, a, right.upper)) {
            top = a;
            break;
        }
    }
    if (top < 0 || parents[top] < 0) {
        return std::nullopt;  // the root can't be the spine: the legs hang from it
    }
    TorsoChain chain;
    std::vector<int> down{top};
    for (int b = parents[top]; b >= 0 && parents[b] >= 0 && static_cast<int>(down.size()) < kMaxSpine; b = parents[b]) {
        if (!IsSpine(NameTokens(names[b]))) {
            break;
        }
        down.push_back(b);
    }
    chain.spine.assign(down.rbegin(), down.rend());
    // The clavicles: the child of the top on the path to each upper arm, when that isn't the arm.
    for (int side : {0, 1}) {
        int const upper = side == 0 ? left.upper : right.upper;
        int b = upper;
        while (b >= 0 && parents[b] != top) {
            b = parents[b];
        }
        if (b >= 0 && b != upper && !IsHelperName(NameTokens(names[b]))) {
            chain.collars[side] = b;
        }
    }
    return chain;
}

TorsoResult PoseTorso(std::span<Transform> pose, std::span<int const> parents, TorsoChain const& chain,
    ArmChain const& left, ArmChain const& right, BodyFrame const& body, TorsoGoal const& goal) {
    TorsoResult result;
    int const count = static_cast<int>(pose.size());
    if (static_cast<int>(parents.size()) != count) {
        result.error = "pose and parents differ in size";
        return result;
    }
    if (chain.spine.empty() || left.upper < 0 || right.upper < 0 || left.upper >= count || right.upper >= count) {
        result.error = "no spine or shoulders";
        return result;
    }
    for (int i = 0; i < count; ++i) {
        if (parents[i] < -1 || parents[i] >= i) {
            result.error = "skeleton not parents-first";
            return result;
        }
    }
    for (std::size_t k = 0; k < chain.spine.size(); ++k) {
        int const bone = chain.spine[k];
        if (bone < 0 || bone >= count || (k > 0 && !IsAncestor(parents, chain.spine[k - 1], bone))) {
            result.error = "spine chain out of range or not one branch";
            return result;
        }
    }
    int const top = chain.spine.back();
    if (!IsAncestor(parents, top, left.upper) || !IsAncestor(parents, top, right.upper)) {
        result.error = "the shoulders don't hang from the spine";
        return result;
    }
    int references = 0;
    Vec3 referenceSum;
    for (int bone : goal.reference) {
        if (bone >= 0) {
            if (bone >= count || !IsAncestor(parents, top, bone)) {
                result.error = "reference bone not above the spine";
                return result;
            }
            referenceSum = referenceSum + pose[bone].translation;
            ++references;
        }
    }
    if (references == 0) {
        result.error = "no reference bone";
        return result;
    }
    if (!Finite(goal.head) || !Finite(goal.forward)) {
        result.error = "goal not finite";
        return result;
    }
    Vec3 const up = Normalize(body.up, {0.0, 0.0, 1.0});
    Vec3 const wantedFacing = Normalize(Horizontal(goal.forward, up), Normalize(Horizontal(body.forward, up), {1.0, 0.0, 0.0}));

    // The animated pose: shoulders, facing, and the reference point (eyes or head). The upper body
    // turns rigidly about the bottom spine joint: first the twist about the body's up, then the
    // lean that carries the reference point toward the goal; the shoulders go along.
    Vec3 const oldMid = Midpoint(pose, left.upper, right.upper);
    Vec3 const oldFacing = Facing(pose, left, right, up, wantedFacing);
    Vec3 const reference = referenceSum * (1.0 / references);
    Vec3 const pivot = pose[chain.spine.front()].translation;

    // The twist, limited.
    double const maxTwist = std::max(0.0, goal.maxTwistDegrees) / kDegrees;
    double twist = SignedAngle(oldFacing, wantedFacing, up);
    if (std::abs(twist) > maxTwist) {
        twist = std::copysign(maxTwist, twist);
        result.limited = true;
    }
    Quat const twistTurn = About(up, twist);
    Vec3 const facing = Rotate(twistTurn, oldFacing);

    // The lean, limited: the reference point keeps its distance from the pivot, so a goal nearer
    // or farther (crouching, standing taller) is met only in direction; the arms take the rest.
    Vec3 const twisted = Rotate(twistTurn, oldMid - pivot);
    Vec3 const twistedReference = Rotate(twistTurn, reference - pivot);
    Vec3 const goalHead = goal.relative ? reference + goal.head : goal.head;
    Quat const fullSwing = FromTo(twistedReference, goal.lean ? goalHead - pivot : twistedReference);
    result.wantedShoulders = pivot + Rotate(fullSwing, twisted);
    double const maxBend = std::max(0.0, goal.maxBendDegrees) / kDegrees;
    Quat swing = fullSwing;
    double const bend = Angle(swing);
    if (bend > maxBend) {
        swing = Fraction(swing, maxBend / bend);
        result.limited = true;
    }
    Vec3 const reachable = pivot + Rotate(swing, twisted);

    // Spread over the spine joints, bottom to top: each takes its share of what's left, measured
    // from where it is now.
    thread_local std::vector<Transform> work;
    thread_local std::vector<unsigned char> in;
    work.assign(pose.begin(), pose.end());
    int const joints = static_cast<int>(chain.spine.size());
    for (int pass = 0; pass < kPasses; ++pass) {
        for (int k = 0; k < joints; ++k) {
            int const bone = chain.spine[k];
            Vec3 const at = work[bone].translation;
            Vec3 const nowFacing = Facing(work, left, right, up, facing);
            Quat const turn = About(up, SignedAngle(nowFacing, facing, up));
            Vec3 const mid = Midpoint(work, left.upper, right.upper);
            Quat const lean = FromTo(Rotate(turn, mid - at), reachable - at);
            double const share = 1.0 / static_cast<double>(joints - k);
            TurnSubtree(work, parents, bone, Fraction(Normalize(lean * turn), share), in);
        }
    }
    if (goal.keepBone >= 0 && goal.keepBone < count) {
        for (int i = goal.keepBone; i < count; ++i) {
            if (IsAncestor(parents, goal.keepBone, i)) {
                work[i] = pose[i];
            }
        }
    }
    Vec3 const newMid = Midpoint(work, left.upper, right.upper);
    for (int i = 0; i < count; ++i) {
        if (!Finite(work[i].translation) || !Finite(work[i].rotation)) {
            result.error = "solution not finite";
            return result;
        }
    }
    int bones = 0;
    for (int i = chain.spine.front(); i < count; ++i) {
        if (IsAncestor(parents, chain.spine.front(), i)) {
            pose[i] = work[i];
            ++bones;
        }
    }
    result.ok = true;
    result.oldShoulders = oldMid;
    result.newShoulders = newMid;
    result.facing = Facing(pose, left, right, up, facing);
    result.twistDegrees = twist * kDegrees;
    result.bendDegrees = Angle(swing) * kDegrees;
    result.bonesWritten = bones;
    return result;
}

double PoseCollar(std::span<Transform> pose, std::span<int const> parents, int collar, ArmChain const& arm, Vec3 wrist,
    double reachFraction, double fraction, double maxDegrees) {
    int const count = static_cast<int>(pose.size());
    if (collar < 0 || collar >= count || arm.upper <= collar || arm.fore < 0 || arm.hand < 0 || arm.fore >= count ||
        arm.hand >= count || static_cast<int>(parents.size()) != count || !IsAncestor(parents, collar, arm.upper) ||
        !Finite(wrist)) {
        return 0.0;
    }
    Vec3 const pivot = pose[collar].translation;
    Vec3 const shoulder = pose[arm.upper].translation;
    double const length = Length(pose[arm.fore].translation - shoulder) + Length(pose[arm.hand].translation - pose[arm.fore].translation);
    if (!(length > 1e-6) || Length(wrist - shoulder) <= reachFraction * length) {
        return 0.0;
    }
    Quat const full = FromTo(shoulder - pivot, wrist - pivot);
    double const angle = Angle(full) * kDegrees;
    double const turned = std::min(angle * fraction, std::max(0.0, maxDegrees));
    if (!(turned > 1e-6) || !(angle > 1e-6)) {
        return 0.0;
    }
    thread_local std::vector<unsigned char> in;
    thread_local std::vector<Transform> work;
    work.assign(pose.begin(), pose.end());
    TurnSubtree(work, parents, collar, Fraction(full, turned / angle), in);
    for (int i = collar; i < count; ++i) {
        if (!Finite(work[i].translation) || !Finite(work[i].rotation)) {
            return 0.0;
        }
    }
    for (int i = collar; i < count; ++i) {
        pose[i] = work[i];
    }
    return turned;
}

} // namespace mevr::ik
