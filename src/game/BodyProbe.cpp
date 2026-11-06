// SPDX-License-Identifier: MIT
#include "game/BodyProbe.hpp"

#include "core/Log.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ArmIk.hpp"
#include "game/BodyCalibration.hpp"
#include "game/BodyScale.hpp"
#include "game/HandCalibration.hpp"
#include "game/HandTargets.hpp"
#include "game/Player.hpp"
#include "game/Weapon.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::game::body_probe {
namespace {

namespace sk = engine::skeleton;

double Length(XrVector3f const& a, XrVector3f const& b) {
    return std::sqrt(static_cast<double>(a.x - b.x) * (a.x - b.x) + static_cast<double>(a.y - b.y) * (a.y - b.y) +
                     static_cast<double>(a.z - b.z) * (a.z - b.z));
}

} // namespace

std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string sub;
    words >> sub;
    if (sub == "scale") {
        // `mevr body scale <s> | auto | off`: the body at the player's size (research/58, game/body_scale).
        std::string rest;
        std::getline(words, rest);
        return body_scale::Command(rest);
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    ue3::Object* const mesh = pawn != nullptr ? player::Mesh(pawn) : nullptr;
    if (mesh == nullptr) {
        return "body: no player pawn or mesh (on foot?)";
    }
    auto const skeleton = sk::ReadRefSkeleton(mesh);
    if (!skeleton) {
        return "body: the mesh has no readable skeleton";
    }
    hand_targets::Targets const targets = hand_targets::Current(std::nullopt, true);
    if (targets.source != hand_targets::Source::Controllers) {
        return "body: no tracked view (needs the VR view with controllers, or a replay)";
    }
    engine::TrackingFrame const& frame = targets.body;
    auto bone = [&](std::string_view name) -> int {
        for (std::size_t i = 0; i < skeleton->names.size(); ++i) {
            if (skeleton->names[i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    auto tracking = [&](int index) -> std::optional<XrVector3f> {
        if (index < 0) {
            return std::nullopt;
        }
        auto const world = sk::BoneLocation(mesh, index, sk::Space::World);
        return world ? std::optional<XrVector3f>(engine::ToTracking(frame, *world)) : std::nullopt;
    };
    auto component = [&](int index) -> std::optional<engine::UnrealVector> {
        return index >= 0 ? sk::BoneLocation(mesh, index, sk::Space::Component) : std::nullopt;
    };
    // The floor: Shepard's lowest bone (a toe), in tracking space.
    double lowest = std::numeric_limits<double>::infinity();
    std::string lowestName;
    for (std::size_t i = 0; i < skeleton->names.size(); ++i) {
        if (auto const p = tracking(static_cast<int>(i)); p && p->y < lowest) {
            lowest = p->y;
            lowestName = skeleton->names[i];
        }
    }
    std::string out = std::format("body: {} bones, first person {}, {:.1f} units per meter; head (tracking) y {:.3f} m\n",
        skeleton->names.size(), arm_ik::FirstPerson() ? "yes" : "no", frame.unitsPerMeter, targets.head.position.y);
    out += std::format("  floor (lowest bone, {}): tracking y {:.3f} m; the headset {:.3f} m above it\n", lowestName, lowest,
        targets.head.position.y - lowest);
    constexpr std::string_view kNames[] = {"LeftToe", "RightToe", "LeftAnkle", "RightAnkle", "LeftKnee", "RightKnee", "LeftHip",
        "RightHip", "Pelvis", "LowerBack", "Neck", "Head", "Eye_Left", "Eye_Right", "LeftShoulder", "RightShoulder", "LeftElbow",
        "RightElbow", "LeftWrist", "RightWrist", "RightIndexFinger2", "Camera", "God", "Root"};
    for (std::string_view name : kNames) {
        int const i = bone(name);
        auto const t = tracking(i);
        auto const c = component(i);
        if (!t || !c) {
            out += std::format("  {:<18} -\n", name);
            continue;
        }
        out += std::format("  {:<18} {:>3}  above the floor {:6.3f} m   tracking ({:+.3f}, {:+.3f}, {:+.3f})   component ({:+.1f}, {:+.1f}, {:+.1f})\n",
            name, i, t->y - lowest, t->x, t->y, t->z, c->x, c->y, c->z);
    }
    auto seg = [&](std::string_view a, std::string_view b) -> double {
        auto const p = tracking(bone(a));
        auto const q = tracking(bone(b));
        return p && q ? Length(*p, *q) : -1.0;
    };
    double const upperL = seg("LeftShoulder", "LeftElbow");
    double const lowerL = seg("LeftElbow", "LeftWrist");
    double const upperR = seg("RightShoulder", "RightElbow");
    double const lowerR = seg("RightElbow", "RightWrist");
    double const shoulders = seg("LeftShoulder", "RightShoulder");
    out += std::format("  arms (drawn, m): left upper {:.3f} lower {:.3f} = {:.3f}; right upper {:.3f} lower {:.3f} = {:.3f}; "
                       "shoulder joints {:.3f} apart\n",
        upperL, lowerL, upperL + lowerL, upperR, lowerR, upperR + lowerR, shoulders);
    // The held gun as drawn: its origin (the grip) to its muzzle, which grows with the hand only if the attachment takes the
    // bone's scale.
    if (auto const gun = weapon::CurrentAsDrawn()) {
        double const dx = gun->location.x - gun->origin.x;
        double const dy = gun->location.y - gun->origin.y;
        double const dz = gun->location.z - gun->origin.z;
        out += std::format("  gun: muzzle {:.1f} units from its origin (as drawn)\n", std::sqrt(dx * dx + dy * dy + dz * dz));
    } else {
        out += "  gun: none in the right hand\n";
    }
    // The player, as calibrated (research/25).
    double const playerArm = body_calibration::ArmLength().value_or(hand_calibration::ArmLength());
    out += std::format("  player: arm (shoulder to grip) {:.3f} m{}; reach near the face {:.2f} m\n", playerArm,
        body_calibration::ArmLength() ? " (calibrated)" : " ([Hands] armLengthM)", hand_calibration::ReachNearFace());
    log::Info("{}", out);
    return out;
}

} // namespace mevr::game::body_probe
