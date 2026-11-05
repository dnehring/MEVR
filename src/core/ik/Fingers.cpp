// SPDX-License-Identifier: MIT
#include "core/ik/Fingers.hpp"

#include "core/ik/BoneNames.hpp"
#include "core/ik/PoseGraft.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace mevr::ik::fingers {
namespace {

// Which finger a bone name speaks of (-1: none).
int FingerOf(std::vector<std::string> const& tokens) {
    for (std::string const& t : tokens) {
        if (t == "thumb") {
            return kThumb;
        }
        if (t == "index") {
            return kIndex;
        }
        if (t == "middle") {
            return kMiddle;
        }
        if (t == "ring") {
            return kRing;
        }
        if (t == "pinky" || t == "pink" || t == "pinkie" || t == "little") {
            return kPinky;
        }
    }
    return -1;
}

bool IsEndBone(std::vector<std::string> const& tokens) {
    return std::any_of(tokens.begin(), tokens.end(), [](std::string const& t) { return t == "end" || t == "nub" || t == "tip"; });
}

// The joint a name's last digit token names: none -> 0 (the knuckle), "1"/"01" -> 1, "2"/"02" -> 2; anything else -1.
int JointOf(std::vector<std::string> const& tokens) {
    int joint = 0;
    for (std::string const& t : tokens) {
        if (std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            int value = 0;
            for (char c : t) {
                value = value * 10 + (c - '0');
                if (value > 99) {
                    return -1;
                }
            }
            joint = value;
        }
    }
    return joint >= 1 && joint < kJointsPerFinger ? joint : joint == 0 ? 0 : -1;
}

bool Descends(std::span<int const> parents, int bone, int ancestor) {
    int const count = static_cast<int>(parents.size());
    for (int steps = 0; bone >= 0 && bone < count && steps < count; ++steps) {
        bone = parents[static_cast<std::size_t>(bone)];
        if (bone == ancestor) {
            return true;
        }
    }
    return false;
}

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

int HandBones::Found() const {
    int found = 0;
    for (Chain const& chain : fingers) {
        found += chain.count > 0 ? 1 : 0;
    }
    return found;
}

int HandBones::All(std::array<int, kMaxBones>& out) const {
    int n = 0;
    out[static_cast<std::size_t>(n++)] = hand;
    for (Chain const& chain : fingers) {
        for (int k = 0; k < chain.count; ++k) {
            out[static_cast<std::size_t>(n++)] = chain.joints[static_cast<std::size_t>(k)];
        }
    }
    return n;
}

HandBones FindHand(std::span<std::string const> names, std::span<int const> parents, int hand) {
    HandBones out;
    int const count = static_cast<int>(std::min(names.size(), parents.size()));
    if (hand < 0 || hand >= count) {
        return out;
    }
    out.hand = hand;
    for (int i = 0; i < count; ++i) {
        if (i == hand || !Descends(parents, i, hand)) {
            continue;
        }
        std::vector<std::string> const tokens = NameTokens(names[static_cast<std::size_t>(i)]);
        int const finger = FingerOf(tokens);
        int const joint = finger >= 0 && !IsEndBone(tokens) ? JointOf(tokens) : -1;
        if (finger < 0 || joint < 0) {
            continue;
        }
        int& slot = out.fingers[static_cast<std::size_t>(finger)].joints[static_cast<std::size_t>(joint)];
        if (slot < 0) {
            slot = i;
        }
    }
    // A chain runs from the knuckle through joints that hang one from the other; anything after a gap is dropped.
    for (Chain& chain : out.fingers) {
        chain.count = 0;
        for (int k = 0; k < kJointsPerFinger; ++k) {
            int const joint = chain.joints[static_cast<std::size_t>(k)];
            bool const linked = joint >= 0 && (k == 0 || parents[static_cast<std::size_t>(joint)] == chain.joints[static_cast<std::size_t>(k - 1)]);
            if (!linked) {
                break;
            }
            chain.count = k + 1;
        }
        for (int k = chain.count; k < kJointsPerFinger; ++k) {
            chain.joints[static_cast<std::size_t>(k)] = -1;
        }
    }
    return out;
}

std::optional<HandAxes> MeasureAxes(std::span<Transform const> pose, HandBones const& bones) {
    int const count = static_cast<int>(pose.size());
    auto const inRange = [&](int bone) { return bone >= 0 && bone < count; };
    if (!inRange(bones.hand)) {
        return std::nullopt;
    }
    Transform const toHand = Inverse(pose[static_cast<std::size_t>(bones.hand)]);
    auto const local = [&](int bone) { return Apply(toHand, pose[static_cast<std::size_t>(bone)].translation); };
    Chain const& index = bones.fingers[kIndex];
    Chain const& middle = bones.fingers[kMiddle];
    Chain const& pinky = bones.fingers[kPinky];
    Chain const& thumb = bones.fingers[kThumb];
    if (index.count < 1 || middle.count < 1 || pinky.count < 1) {
        return std::nullopt;
    }
    // The palm plane: the knuckle line and a finger's direction.
    Chain const& reference = middle.count >= 2 ? middle : index;
    if (reference.count < 2) {
        return std::nullopt;
    }
    Vec3 const across = local(index.joints[0]) - local(pinky.joints[0]);
    Vec3 const direction = local(reference.joints[1]) - local(reference.joints[0]);
    double const acrossLength = Length(across);
    double const directionLength = Length(direction);
    if (!(acrossLength > 1e-6) || !(directionLength > 1e-6) || !Finite(across) || !Finite(direction)) {
        return std::nullopt;
    }
    Vec3 const normal = Cross(across, direction);
    if (Length(normal) < 0.2 * acrossLength * directionLength) {
        return std::nullopt;  // the knuckles lie along the fingers: no palm plane
    }
    Vec3 const n = Normalize(normal);
    HandAxes axes;
    // The rest pose's own curl: how each finger's second segment bends off its first.
    double restCurl = 0.0;
    auto const bendOf = [&](Chain const& chain) -> std::optional<Vec3> {
        if (chain.count < 3) {
            return std::nullopt;
        }
        Vec3 const d1 = local(chain.joints[1]) - local(chain.joints[0]);
        Vec3 const d2 = local(chain.joints[2]) - local(chain.joints[1]);
        if (!(Length(d1) > 1e-6) || !(Length(d2) > 1e-6)) {
            return std::nullopt;
        }
        return Normalize(d2) - Normalize(d1);
    };
    for (int f = kIndex; f <= kPinky; ++f) {
        if (auto const bend = bendOf(bones.fingers[static_cast<std::size_t>(f)])) {
            restCurl += Dot(*bend, n);
        }
    }
    // The palm side: where the thumb's base sits off the knuckle plane; else the way the rest pose curls.
    double palmSign = 0.0;
    if (thumb.count >= 1) {
        Vec3 const centre = (local(index.joints[0]) + local(middle.joints[0]) + local(pinky.joints[0])) * (1.0 / 3.0);
        double const off = Dot(local(thumb.joints[0]) - centre, n);
        if (std::abs(off) > 0.05 * acrossLength) {
            palmSign = off > 0.0 ? 1.0 : -1.0;
            axes.palmFromThumb = true;
        }
    }
    if (palmSign == 0.0) {
        if (!(std::abs(restCurl) > 1e-3)) {
            return std::nullopt;  // nothing says which side the palm is
        }
        palmSign = restCurl > 0.0 ? 1.0 : -1.0;
    }
    axes.palm = n * palmSign;
    for (int f = kIndex; f <= kPinky; ++f) {
        if (auto const bend = bendOf(bones.fingers[static_cast<std::size_t>(f)])) {
            double const along = Dot(*bend, axes.palm);
            if (std::abs(along) > 1e-3) {
                (along > 0.0 ? axes.restCurlAgree : axes.restCurlDisagree) += 1;
            }
        }
    }
    // Each finger bends toward the palm about the axis across it: its direction crossed with the palm normal.
    for (int f = kIndex; f <= kPinky; ++f) {
        Chain const& chain = bones.fingers[static_cast<std::size_t>(f)];
        if (chain.count < 1) {
            continue;
        }
        Vec3 const d = chain.count >= 2 ? Normalize(local(chain.joints[1]) - local(chain.joints[0])) : Normalize(direction);
        Vec3 const axis = Cross(d, axes.palm);
        if (Length(axis) > 0.2) {
            axes.axis[static_cast<std::size_t>(f)] = Normalize(axis);
            axes.valid[static_cast<std::size_t>(f)] = true;
        }
    }
    // The thumb closes toward the fingers: its direction turned toward the middle finger's first joint.
    if (thumb.count >= 1) {
        Vec3 const base = local(thumb.joints[0]);
        Vec3 const d = thumb.count >= 2 ? Normalize(local(thumb.joints[1]) - base) : Normalize(direction);
        Vec3 const toward = local(middle.joints[static_cast<std::size_t>(middle.count >= 2 ? 1 : 0)]) - base;
        Vec3 const perpendicular = toward - d * Dot(toward, d);
        if (Length(perpendicular) > 0.2 * Length(toward) && Length(toward) > 1e-6) {
            Vec3 const axis = Cross(d, Normalize(perpendicular));
            if (Length(axis) > 0.2) {
                axes.axis[kThumb] = Normalize(axis);
                axes.valid[kThumb] = true;
            }
        }
    }
    return axes;
}

Quat AboutAxis(Vec3 axis, double degrees) {
    double const half = degrees * std::numbers::pi / 360.0;
    double const s = std::sin(half);
    Vec3 const a = Normalize(axis, {1.0, 0.0, 0.0});
    return Normalize(Quat{a.x * s, a.y * s, a.z * s, std::cos(half)});
}

int Apply(std::span<Transform> pose, std::span<int const> parents, HandBones const& bones, HandAxes const& axes,
    Curl const& curl, double fullCurlDegrees) {
    int const count = static_cast<int>(pose.size());
    if (bones.hand < 0 || bones.hand >= count || static_cast<int>(parents.size()) != count || !std::isfinite(fullCurlDegrees)) {
        return 0;
    }
    int written = 0;
    Quat const hand = pose[static_cast<std::size_t>(bones.hand)].rotation;
    for (int f = 0; f < kFingerCount; ++f) {
        Chain const& chain = bones.fingers[static_cast<std::size_t>(f)];
        double const amount = curl.finger[static_cast<std::size_t>(f)];
        if (chain.count < 1 || !axes.valid[static_cast<std::size_t>(f)] || !std::isfinite(amount) || amount == 0.0) {
            continue;
        }
        double const degrees = std::clamp(amount, -1.0, 1.0) * fullCurlDegrees * (f == kThumb ? kThumbScale : 1.0);
        if (degrees == 0.0) {
            continue;
        }
        int const knuckle = chain.joints[0];
        if (knuckle < 0 || knuckle >= count) {
            continue;
        }
        Vec3 const axisInComponent = Normalize(Rotate(hand, axes.axis[static_cast<std::size_t>(f)]));
        // Everything under the knuckle, as local transforms, so turning a joint carries all of it.
        std::vector<int> const sub = Subtree(parents, knuckle);
        std::vector<int> const subParents = ParentsOf(parents, sub);
        std::vector<Transform> locals = CaptureRelative(pose, sub, subParents);
        if (locals.size() != sub.size() || sub.empty()) {
            continue;
        }
        for (int k = 0; k < chain.count; ++k) {
            int const joint = chain.joints[static_cast<std::size_t>(k)];
            if (joint < 0 || joint >= count || (k > 0 && parents[static_cast<std::size_t>(joint)] != chain.joints[static_cast<std::size_t>(k - 1)])) {
                break;
            }
            auto const at = std::find(sub.begin(), sub.end(), joint);
            if (at == sub.end()) {
                break;
            }
            // A turn about the component-space axis through the joint's origin, as the joint's own frame sees it:
            // R' = R * Q(axis in R's frame) == Q(axis) * R.
            Vec3 const axisLocal = Rotate(Conjugate(pose[static_cast<std::size_t>(joint)].rotation), axisInComponent);
            Transform& local = locals[static_cast<std::size_t>(at - sub.begin())];
            local.rotation = Normalize(local.rotation * AboutAxis(axisLocal, degrees));
        }
        if (ApplyRelative(pose, sub, subParents, locals)) {
            written += static_cast<int>(sub.size());
        }
    }
    return written;
}

Curl FromController(Rule rule, Controller const& c) {
    double const trigger = std::clamp(c.trigger, 0.0, 1.0);
    double const squeeze = std::clamp(c.squeeze, 0.0, 1.0);
    auto const lerp = [](double a, double b, double t) { return a + (b - a) * t; };
    bool const triggerSensor = c.triggerTouch >= 0.0;
    // How much the finger is on the trigger: its sensor's weight, and a pull is a touch whatever the sensor says.
    double const onTrigger = std::max(triggerSensor ? std::clamp(c.triggerTouch, 0.0, 1.0) : 0.0, std::clamp(trigger * 20.0, 0.0, 1.0));
    Curl curl;
    if (rule == Rule::Gun) {
        // Without a sensor the pull itself brings the finger onto the trigger (research/43's first rule).
        double const on = triggerSensor ? onTrigger : trigger;
        curl.finger[kIndex] = -kIndexOffTrigger * (1.0 - on);
        return curl;
    }
    curl.finger[kIndex] = triggerSensor ? lerp(kIndexPoint, lerp(kIndexOnTrigger, 1.0, trigger), onTrigger) : trigger;
    curl.finger[kMiddle] = squeeze;
    curl.finger[kRing] = squeeze;
    curl.finger[kPinky] = squeeze;
    curl.finger[kThumb] = c.thumbTouch >= 0.0 ? lerp(kThumbUp, std::max(squeeze, kThumbOnRest), std::clamp(c.thumbTouch, 0.0, 1.0)) : squeeze;
    return curl;
}

double EaseTouch(double weight, bool touching, double seconds) {
    if (!(weight >= 0.0)) {
        return touching ? 1.0 : 0.0;
    }
    double const step = std::max(seconds, 0.0) / kTouchBlendS;
    return touching ? std::min(1.0, weight + step) : std::max(0.0, weight - step);
}

} // namespace mevr::ik::fingers
