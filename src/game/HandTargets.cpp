#include "game/HandTargets.hpp"

#include "core/vr/Controllers.hpp"
#include "core/vr/Output.hpp"
#include "game/HeadTracking.hpp"

namespace mevr::game::hand_targets {
namespace {

// The hand bone is the wrist joint; the grip pose sits in the palm. OpenXR grip space: -Z points
// along a held object's barrel, so the wrist is behind it along +Z. [verify in headset]
constexpr float kWristBehindGripMeters = 0.07f;

XrVector3f Wrist(XrPosef const& grip) {
    XrQuaternionf const& q = grip.orientation;
    ik::Vec3 const back = ik::Rotate(ik::Quat{q.x, q.y, q.z, q.w}, {0.0, 0.0, kWristBehindGripMeters});
    return {grip.position.x + static_cast<float>(back.x), grip.position.y + static_cast<float>(back.y),
        grip.position.z + static_cast<float>(back.z)};
}

} // namespace

Targets Current(std::optional<engine::UnrealVector> const& headBone) {
    Targets targets;
    if (!vr::output::Running()) {
        targets.source = Source::DesktopTest;
        return targets;
    }
    // Controllers are tracked in LOCAL space; the last head-tracked view says where LOCAL space
    // is in the world (the head's world location and the body yaw LOCAL forward maps to).
    auto const anchor = head_tracking::Anchor();
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!anchor || !controllers.active || !vr::output::WantsHeadTracking()) {
        return targets;
    }
    targets.source = Source::Controllers;
    engine::UnrealVector const center = headBone ? *headBone : anchor->headWorld;
    for (int side : {0, 1}) {
        vr::controllers::Hand const& hand = side == 0 ? controllers.left : controllers.right;
        if (!hand.gripValid) {
            continue;
        }
        Hand& out = targets.hands[side];
        out.valid = true;
        out.position = engine::ToWorld(Wrist(hand.grip), anchor->head.position, center, anchor->gameYaw);
        out.hasRotation = true;
        out.rotation = engine::ToUnrealOrientation(hand.grip.orientation, anchor->gameYaw);
    }
    return targets;
}

char const* Name(Source source) {
    switch (source) {
    case Source::Controllers: return "controllers";
    case Source::DesktopTest: return "desktop test";
    default: return "none";
    }
}

} // namespace mevr::game::hand_targets
