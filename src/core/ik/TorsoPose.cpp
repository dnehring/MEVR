// SPDX-License-Identifier: MIT
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
// TorsoGoal::crouch: a gap within this fraction of the spine's length (LE1: 0.6 units, 6 mm) moves nothing.
constexpr double kCrouchDeadbandFraction = 0.01;

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

double Wrap(double a) {
    if (!std::isfinite(a)) {
        return 0.0;
    }
    a = std::remainder(a, 2.0 * std::numbers::pi);
    return a <= -std::numbers::pi ? a + 2.0 * std::numbers::pi : a;
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

double YawBetween(Vec3 from, Vec3 to, Vec3 up) {
    Vec3 const u = Normalize(up, {0.0, 0.0, 1.0});
    Vec3 const a = Horizontal(from, u);
    Vec3 const b = Horizontal(to, u);
    if (!(Length(a) > 1e-9) || !(Length(b) > 1e-9) || !Finite(a) || !Finite(b)) {
        return 0.0;
    }
    return SignedAngle(Normalize(a), Normalize(b), u);
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
    double const maxTwist = std::max(0.0, goal.maxTwistDegrees) / kDegrees;
    if (goal.body >= 0 && (goal.body >= count || parents[goal.body] < 0 || !IsAncestor(parents, goal.body, chain.spine.front()))) {
        result.error = "body bone not an ancestor of the spine, or the skeleton's root";
        return result;
    }
    if (goal.body >= 0 && goal.bodyMoves && (!Finite(goal.bodyAt) || !std::isfinite(goal.maxBodyShift))) {
        result.error = "body goal not finite";
        return result;
    }
    result.oldReference = referenceSum * (1.0 / references);
    Vec3 const animatedFacing = Facing(pose, left, right, up, wantedFacing);
    result.wantedTurnDegrees = SignedAngle(animatedFacing, wantedFacing, up) * kDegrees;
    // The legs (TorsoGoal::legsWeight): how far the animation turned the shoulders from the legs (the pawn's facing).
    double const legsWeight = goal.body >= 0 && goal.bodyTurns && std::isfinite(goal.legsWeight) && std::isfinite(goal.legsTurnDegrees)
        ? std::clamp(goal.legsWeight, 0.0, 1.0)
        : 0.0;
    double const shouldersFromLegs = YawBetween(body.forward, animatedFacing, up);
    result.shouldersFromLegsDegrees = shouldersFromLegs * kDegrees;
    // The spine's twist limit: the standing rule's, or (the legs keeping the run) the legs' limit from the legs, which
    // from the animated shoulders is that plus the animation's own turn. The body's turn meets it exactly when the
    // view is past the limit, hence the hair of slack.
    double twistLimit = maxTwist;
    if (legsWeight > 0.0) {
        double const legsTwist = std::isfinite(goal.legsTwistDegrees) ? std::max(0.0, goal.legsTwistDegrees) / kDegrees : maxTwist;
        twistLimit = std::max(maxTwist, legsTwist + std::abs(shouldersFromLegs) + 1e-6);
    }

    // The whole body first (TorsoGoal::body): the turn the spine can't take, then the slide under the
    // view. Rigid, about the body bone; everything below is measured on the moved pose.
    Vec3 const animatedMid = Midpoint(pose, left.upper, right.upper);
    thread_local std::vector<Transform> base;
    thread_local std::vector<unsigned char> in;
    base.assign(pose.begin(), pose.end());
    bool bodyMoved = false;
    Quat bodyTurn{};
    Vec3 const animatedBody = goal.body >= 0 && goal.body < count ? base[goal.body].translation : Vec3{};
    if (goal.body >= 0) {
        if (goal.bodyTurns) {
            double const total = result.wantedTurnDegrees / kDegrees;
            double const excess = std::abs(total) > maxTwist ? std::copysign(std::abs(total) - maxTwist, total) : 0.0;
            result.standingTurnDegrees = excess * kDegrees;
            // Moving, the legs keep the pawn's facing (legsTurnDegrees from it); blended the short way round.
            double const turn = legsWeight > 0.0 ? Wrap(excess + legsWeight * Wrap(goal.legsTurnDegrees / kDegrees - excess)) : excess;
            if (turn != 0.0) {
                TurnSubtree(base, parents, goal.body, About(up, turn), in);
                bodyTurn = About(up, turn);
                result.bodyTurnDegrees = turn * kDegrees;
                bodyMoved = true;
            }
        }
        if (goal.bodyMoves) {
            Vec3 const wanted = Horizontal(goal.bodyAt - base[goal.body].translation, up);
            result.wantedBodyShift = wanted;
            double const length = Length(wanted);
            double const max = std::max(0.0, goal.maxBodyShift);
            Vec3 shift = wanted;
            if (length > max) {
                shift = length > 1e-12 ? wanted * (max / length) : Vec3{};
                result.shiftLimited = true;
            }
            if (goal.bodyRise != 0.0) {
                shift = shift + up * goal.bodyRise;
            }
            if (Length(shift) > 1e-9) {
                for (int i = goal.body; i < count; ++i) {
                    if (IsAncestor(parents, goal.body, i)) {
                        base[i].translation = base[i].translation + shift;
                    }
                }
                result.bodyShift = shift;
                bodyMoved = true;
            }
            // Standing still, the feet planted (TorsoGoal::plantWeight): the slide put the body bone at bodyAt whatever
            // the animation did; the feet go back by its animated offset from rest, turned with the body.
            double const plantWeight = std::isfinite(goal.plantWeight) ? std::clamp(goal.plantWeight, 0.0, 1.0) : 0.0;
            if (plantWeight > 0.0 && Finite(goal.bodyRest)) {
                Vec3 const sway = Horizontal(animatedBody - goal.bodyRest, up);
                result.bodySway = Length(sway);
                Vec3 correction = Rotate(bodyTurn, sway) * plantWeight;
                double const plantMax = std::isfinite(goal.maxPlant) ? std::max(0.0, goal.maxPlant) : 0.0;
                if (Length(correction) > plantMax) {
                    correction = Length(correction) > 1e-12 ? correction * (plantMax / Length(correction)) : Vec3{};
                }
                result.feetCorrection = correction;
            }
        }
    }

    // The animated pose (the body moved, if it did): shoulders, facing, and the reference point (eyes or
    // head). The upper body turns rigidly about the bottom spine joint: first the twist about the body's
    // up, then the lean that carries the reference point toward the goal; the shoulders go along.
    auto const referenceIn = [&](std::vector<Transform> const& p) {
        Vec3 sum;
        for (int bone : goal.reference) {
            if (bone >= 0) {
                sum = sum + p[bone].translation;
            }
        }
        return sum * (1.0 / references);
    };
    // Relative: the offset from where the animation put the reference (before any body move).
    Vec3 const goalHead = goal.relative ? result.oldReference + goal.head : goal.head;
    Vec3 aim = goalHead;  // what the lean points the reference at

    // The body down (or up) to the player's head (TorsoGoal::crouch): the spine keeps its length, so the reference reaches
    // its target only from a body at the right height. Measured on the body as the slide left it; the twist below turns
    // about the bottom spine joint and changes neither the spine's length nor the target's height above that joint.
    if (goal.crouch && goal.body >= 0 && goal.bodyMoves && !goal.relative && std::isfinite(goal.neckDown)) {
        int eyeCount = 0;
        Vec3 eyeSum;
        for (int bone : goal.eyes) {
            if (bone >= 0 && bone < count && IsAncestor(parents, chain.spine.back(), bone)) {
                eyeSum = eyeSum + base[bone].translation;
                ++eyeCount;
            }
        }
        if (eyeCount > 0) {
            Vec3 const reference = referenceIn(base);
            Vec3 const bottom = base[chain.spine.front()].translation;
            // The reference's target: the goal raised by how far the reference sits above Shepard's own pivot (the neck
            // model's drop under his eyes): standing straight at the body's scale, exactly where it is.
            double const above = goal.neckDown - Dot(eyeSum * (1.0 / eyeCount) - reference, up);
            Vec3 const target = goalHead + up * above;
            double const spine = Length(reference - bottom);
            Vec3 const toTarget = target - bottom;
            double const bendLimit = std::clamp(goal.maxBendDegrees, 0.0, 89.0) / kDegrees;
            double const across = std::min(Length(Horizontal(toTarget, up)), spine * std::sin(bendLimit));
            // How far the reference would stand above its target with the spine leaning toward it (+: too high).
            double const gap = std::sqrt(std::max(0.0, spine * spine - across * across)) - Dot(toTarget, up);
            double const slack = std::isfinite(goal.crouchSlack) ? std::max(0.0, goal.crouchSlack) : 0.0;
            double const loose = std::isfinite(goal.crouchLoose) ? std::clamp(goal.crouchLoose, 0.0, 1.0) : 0.0;
            double wanted = gap - loose * std::clamp(gap, -slack, slack);
            // A hair of a gap is the skeleton's own (an eye a millimetre off its average): nothing moves for it, and the
            // move grows from zero past it (no step).
            double const deadband = spine * kCrouchDeadbandFraction;
            wanted = std::abs(wanted) > deadband ? std::copysign(std::abs(wanted) - deadband, wanted) : 0.0;
            double const maxDrop = std::isfinite(goal.maxDrop) ? std::max(0.0, goal.maxDrop) : 0.0;
            double const maxRise = std::isfinite(goal.maxRise) ? std::max(0.0, goal.maxRise) : 0.0;
            double const lift = std::clamp(-wanted, -maxDrop, maxRise);
            result.crouchGap = gap;
            result.liftLimited = lift != -wanted;
            if (std::isfinite(lift) && lift != 0.0) {
                for (int i = goal.body; i < count; ++i) {
                    if (IsAncestor(parents, goal.body, i)) {
                        base[i].translation = base[i].translation + up * lift;
                    }
                }
                result.bodyLift = lift;
                bodyMoved = true;
            }
            aim = target;
        }
    }
    Vec3 const oldMid = Midpoint(base, left.upper, right.upper);
    Vec3 const oldFacing = Facing(base, left, right, up, wantedFacing);
    Vec3 const reference = referenceIn(base);
    Vec3 const pivot = base[chain.spine.front()].translation;

    // The twist, limited.
    double twist = SignedAngle(oldFacing, wantedFacing, up);
    if (std::abs(twist) > twistLimit) {
        twist = std::copysign(twistLimit, twist);
        result.limited = true;
    }
    Quat const twistTurn = About(up, twist);
    Vec3 const facing = Rotate(twistTurn, oldFacing);

    // The lean, limited: the reference point keeps its distance from the pivot, so a goal nearer
    // or farther (crouching, standing taller) is met only in direction; the arms take the rest.
    Vec3 const twisted = Rotate(twistTurn, oldMid - pivot);
    Vec3 const twistedReference = Rotate(twistTurn, reference - pivot);
    result.goalReference = aim;
    Quat const fullSwing = FromTo(twistedReference, goal.lean ? aim - pivot : twistedReference);
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
    work.assign(base.begin(), base.end());
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
    int const written = bodyMoved ? goal.body : chain.spine.front();
    for (int i = written; i < count; ++i) {
        if (IsAncestor(parents, written, i)) {
            pose[i] = work[i];
            ++bones;
        }
    }
    Vec3 newReference;
    for (int bone : goal.reference) {
        if (bone >= 0) {
            newReference = newReference + pose[bone].translation;
        }
    }
    result.ok = true;
    result.firstBone = written;
    result.newReference = newReference * (1.0 / references);
    result.oldShoulders = animatedMid;
    result.newShoulders = newMid;
    result.facing = Facing(pose, left, right, up, facing);
    result.twistDegrees = twist * kDegrees;
    result.bendDegrees = Angle(swing) * kDegrees;
    result.bonesWritten = bones;
    return result;
}

LegRoom LegsRoom(std::span<Transform const> pose, std::span<ArmChain const> legs, Vec3 up, double foldFraction,
    double straightFraction) {
    int const count = static_cast<int>(pose.size());
    Vec3 const u = Normalize(up, {0.0, 0.0, 1.0});
    LegRoom room;
    bool any = false;
    for (ArmChain const& leg : legs) {
        if (leg.upper < 0 || leg.fore < 0 || leg.hand < 0 || leg.upper >= count || leg.fore >= count || leg.hand >= count) {
            return {};
        }
        Vec3 const hip = pose[leg.upper].translation;
        Vec3 const knee = pose[leg.fore].translation;
        Vec3 const ankle = pose[leg.hand].translation;
        if (!Finite(hip) || !Finite(knee) || !Finite(ankle)) {
            return {};
        }
        double const length = Length(knee - hip) + Length(ankle - knee);
        if (!(length > 1e-6)) {
            return {};
        }
        double const height = Dot(hip - ankle, u);
        double const aside = Length(Horizontal(hip - ankle, u));
        auto const reach = [&](double fraction) {
            double const r = std::clamp(fraction, 0.0, 1.0) * length;
            return std::sqrt(std::max(0.0, r * r - aside * aside));
        };
        double const drop = std::max(0.0, height - reach(foldFraction));
        double const rise = std::max(0.0, reach(straightFraction) - height);
        room.drop = any ? std::min(room.drop, drop) : drop;
        room.rise = any ? std::min(room.rise, rise) : rise;
        any = true;
    }
    return room;
}

std::optional<ArmGoal> LegUnderBody(std::span<Transform const> pose, ArmChain const& leg, Vec3 ankleMove, BodyFrame const& body) {
    int const count = static_cast<int>(pose.size());
    if (!Finite(ankleMove) || Length(ankleMove) < 0.01 || leg.upper < 0 || leg.fore < 0 || leg.hand < 0 || leg.upper >= count ||
        leg.fore >= count || leg.hand >= count) {
        return std::nullopt;
    }
    Vec3 const hip = pose[leg.upper].translation;
    Vec3 const knee = pose[leg.fore].translation;
    ArmGoal goal;
    goal.wrist = pose[leg.hand].translation + ankleMove;
    goal.hasRotation = true;
    goal.rotation = pose[leg.hand].rotation;
    // The knee bends the way it bends now (the solve takes the pole's part across the reach: with the ankle where it was
    // the knee lands where it was), turned toward the body's forward as the move grows, so a straight leg folds forward.
    Vec3 const along = Normalize(goal.wrist - hip, {0.0, 0.0, -1.0});
    Vec3 const bent = (knee - hip) - along * Dot(knee - hip, along);
    Vec3 const forward = Normalize(Horizontal(body.forward, Normalize(body.up, {0.0, 0.0, 1.0})), {1.0, 0.0, 0.0});
    goal.hasPole = true;
    goal.pole = bent + forward * (2.0 * Length(ankleMove));
    goal.maxStretch = 1.0;
    return goal;
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
