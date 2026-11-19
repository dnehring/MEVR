// SPDX-License-Identifier: MIT
#include "core/ik/HandContact.hpp"

#include "core/ik/BoneNames.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::ik::hand_contact {
namespace {

bool Has(std::vector<std::string> const& tokens, std::string_view token) {
    return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
}

bool HasAny(std::vector<std::string> const& tokens, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(), [&](std::string_view word) { return Has(tokens, word); });
}

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A bone the colliders never use: fingers, the face, props and attachment helpers, IK and camera helpers, twist and roll
// bones, ends and nubs, the skeleton's root.
bool Unused(std::vector<std::string> const& t) {
    return HasAny(t, {"finger", "thumb", "index", "middle", "ring", "pink", "pinky", "toe", "eye", "jaw", "lip", "brow", "cheek",
                      "tongue", "teeth", "nose", "ear", "mouth", "lid", "prop", "ik", "lhik", "rhik", "lfik", "rfik", "swivel",
                      "camera", "god", "root", "hair", "cloth", "cape", "gun", "weapon", "attach", "socket", "twist", "roll",
                      "end", "nub", "helper", "dummy", "pole", "target", "lookat"});
}

// A unit direction across an axis (out of a capsule whose axis the hand sits on): horizontal where the axis allows.
Vec3 Across(Vec3 axis) {
    Vec3 const dir = Normalize(axis, {0.0, 0.0, 1.0});
    Vec3 across = Cross(dir, Vec3{0.0, 0.0, 1.0});
    if (Length(across) < 1e-6) {
        across = Cross(dir, Vec3{1.0, 0.0, 0.0});
    }
    return Normalize(across, {1.0, 0.0, 0.0});
}

} // namespace

char const* PartName(Part part) {
    switch (part) {
    case Part::None: return "none";
    case Part::Spine: return "spine";
    case Part::Collar: return "collar";
    case Part::Neck: return "neck";
    case Part::Head: return "head";
    case Part::UpperArm: return "upper arm";
    case Part::Forearm: return "forearm";
    case Part::Hand: return "hand";
    case Part::Thigh: return "thigh";
    case Part::Shin: return "shin";
    case Part::Foot: return "foot";
    }
    return "?";
}

Part Classify(std::string_view boneName) {
    std::vector<std::string> const t = NameTokens(boneName);
    if (t.empty() || Unused(t)) {
        return Part::None;
    }
    if (IsHeadBone(boneName)) {
        return Part::Head;
    }
    if (Has(t, "neck")) {
        return Part::Neck;
    }
    if (HasAny(t, {"collar", "clavicle"})) {
        return Part::Collar;
    }
    if (HasAny(t, {"elbow", "forearm", "lowerarm"}) || (Has(t, "arm") && HasAny(t, {"fore", "lower"}))) {
        return Part::Forearm;
    }
    if (HasAny(t, {"wrist", "hand"})) {
        return Part::Hand;
    }
    if (HasAny(t, {"shoulder", "upperarm"}) || Has(t, "arm")) {
        return Part::UpperArm;
    }
    if (HasAny(t, {"knee", "calf", "shin", "lowerleg"}) || (Has(t, "leg") && HasAny(t, {"lower", "low"}))) {
        return Part::Shin;
    }
    if (HasAny(t, {"ankle", "foot", "heel"})) {
        return Part::Foot;
    }
    if (HasAny(t, {"hip", "thigh", "upleg", "upperleg"}) || Has(t, "leg")) {
        return Part::Thigh;
    }
    if (HasAny(t, {"spine", "back", "chest", "pelvis", "torso", "abdomen", "belly", "waist", "hips", "stomach", "spine1",
                   "spine2"})) {
        return Part::Spine;
    }
    return Part::None;
}

double RadiusMeters(Part part) {
    switch (part) {
    case Part::Spine: return 0.13;
    case Part::Collar: return 0.07;
    case Part::Neck: return 0.06;
    case Part::Head: return 0.10;
    case Part::UpperArm: return 0.06;
    case Part::Forearm: return 0.05;
    case Part::Hand: return 0.045;
    case Part::Thigh: return 0.085;
    case Part::Shin: return 0.06;
    case Part::None:
    case Part::Foot: break;
    }
    return 0.0;
}

std::vector<BonePart> PartsFor(std::vector<std::string> const& names, std::vector<int> const& parents) {
    std::size_t const count = (std::min)(names.size(), parents.size());
    std::vector<Part> kinds(count, Part::None);
    for (std::size_t i = 0; i < count; ++i) {
        kinds[i] = Classify(names[i]);
    }
    // The nearest ancestor the colliders use (twist bones and helpers passed over).
    auto usedParent = [&](std::size_t i) {
        int p = parents[i];
        for (int guard = 0; p >= 0 && static_cast<std::size_t>(p) < count && kinds[static_cast<std::size_t>(p)] == Part::None &&
                            guard < 8;
             ++guard) {
            p = parents[static_cast<std::size_t>(p)];
        }
        return p >= 0 && static_cast<std::size_t>(p) < count ? p : -1;
    };
    std::vector<BonePart> out;
    bool head = false;
    for (std::size_t i = 0; i < count; ++i) {
        Part const child = kinds[i];
        if (child == Part::None) {
            continue;
        }
        int const p = usedParent(i);
        if (p < 0) {
            continue;
        }
        Part const parent = kinds[static_cast<std::size_t>(p)];
        int const c = static_cast<int>(i);
        if (parent == Part::Spine && (child == Part::Spine || child == Part::Neck)) {
            out.push_back({Part::Spine, p, c});
        } else if ((parent == Part::Neck || parent == Part::Spine) && child == Part::Head && !head) {
            // The neck to the head bone, then the skull above it (the head bone sits at the skull's base).
            out.push_back({Part::Neck, p, c});
            out.push_back({Part::Head, p, c, 0.12});
            head = true;
        } else if (parent == Part::Collar && child == Part::UpperArm) {
            out.push_back({Part::Collar, p, c});
        } else if (parent == Part::UpperArm && child == Part::Forearm) {
            out.push_back({Part::UpperArm, p, c});
        } else if (parent == Part::Forearm && child == Part::Hand) {
            out.push_back({Part::Forearm, p, c});
            out.push_back({Part::Hand, p, c, 0.09});
        } else if (parent == Part::Thigh && child == Part::Shin) {
            out.push_back({Part::Thigh, p, c});
        } else if (parent == Part::Shin && child == Part::Foot) {
            out.push_back({Part::Shin, p, c});
        }
    }
    return out;
}

std::optional<Capsule> CapsuleOf(BonePart const& part, std::span<Vec3 const> positions, double unitsPerMeter) {
    if (part.from < 0 || part.to < 0 || static_cast<std::size_t>(part.from) >= positions.size() ||
        static_cast<std::size_t>(part.to) >= positions.size() || !(unitsPerMeter > 0.0)) {
        return std::nullopt;
    }
    Vec3 const from = positions[static_cast<std::size_t>(part.from)];
    Vec3 const to = positions[static_cast<std::size_t>(part.to)];
    if (!Finite(from) || !Finite(to)) {
        return std::nullopt;
    }
    double const length = Length(to - from);
    if (length < 0.01 * unitsPerMeter || length > 1.0 * unitsPerMeter) {
        return std::nullopt;
    }
    Capsule capsule;
    capsule.part = part.part;
    capsule.radius = RadiusMeters(part.part) * unitsPerMeter;
    if (part.extendMeters > 0.0) {
        capsule.a = to;
        capsule.b = to + (to - from) * (part.extendMeters * unitsPerMeter / length);
    } else {
        capsule.a = from;
        capsule.b = to;
    }
    return capsule;
}

Vec3 ClosestOnSegment(Vec3 p, Vec3 a, Vec3 b) {
    Vec3 const ab = b - a;
    double const lengthSquared = Dot(ab, ab);
    if (lengthSquared <= 1e-12) {
        return a;
    }
    double const t = std::clamp(Dot(p - a, ab) / lengthSquared, 0.0, 1.0);
    return a + ab * t;
}

Result PushOut(Vec3 point, double handRadius, std::span<Capsule const> capsules, std::optional<Vec3> previous) {
    Result result;
    result.point = point;
    if (!Finite(point) || capsules.empty()) {
        return result;
    }
    handRadius = std::isfinite(handRadius) && handRadius > 0.0 ? handRadius : 0.0;
    // How deep the controller's own point is in each capsule (the haptics' measure).
    for (std::size_t k = 0; k < capsules.size(); ++k) {
        Capsule const& c = capsules[k];
        double const need = c.radius + handRadius;
        double const overlap = need - Length(point - ClosestOnSegment(point, c.a, c.b));
        if (overlap > result.depth) {
            result.depth = overlap;
            result.capsule = static_cast<int>(k);
        }
    }
    if (result.capsule < 0) {
        return result;
    }
    result.touching = true;
    bool const usePrevious = previous.has_value() && Finite(*previous);
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        for (std::size_t k = 0; k < capsules.size(); ++k) {
            Capsule const& c = capsules[k];
            double const need = c.radius + handRadius;
            Vec3 const onAxis = ClosestOnSegment(result.point, c.a, c.b);
            Vec3 const away = result.point - onAxis;
            double const distance = Length(away);
            if (distance >= need - 1e-9) {
                continue;
            }
            Vec3 normal = distance > 1e-9 ? away * (1.0 / distance) : Across(c.b - c.a);
            if (usePrevious) {
                // The side the hand was on last time: past the axis it would otherwise pop through to the far side.
                Vec3 const before = *previous - ClosestOnSegment(*previous, c.a, c.b);
                double const beforeLength = Length(before);
                if (beforeLength > 1e-9) {
                    Vec3 const side = before * (1.0 / beforeLength);
                    if (Dot(normal, side) < 0.0 || distance <= 1e-9) {
                        normal = side;
                    }
                }
            }
            result.point = onAxis + normal * need;
            if (static_cast<int>(k) == result.capsule) {
                result.normal = normal;
            }
            moved = true;
        }
        if (!moved) {
            break;
        }
    }
    if (Length(result.normal) < 0.5) {
        Vec3 const away = result.point - point;
        result.normal = Normalize(away, {0.0, 0.0, 1.0});
    }
    return result;
}

double PressAmplitude(double depth, double handRadius, double minAmplitude, double maxAmplitude) {
    if (!(depth > 0.0) || !(handRadius > 0.0)) {
        return 0.0;
    }
    double const share = std::clamp(depth / (2.0 * handRadius), 0.0, 1.0);
    return minAmplitude + (maxAmplitude - minAmplitude) * share;
}

} // namespace mevr::ik::hand_contact
