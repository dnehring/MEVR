#include "game/HandTargets.hpp"

#include "core/vr/Controllers.hpp"
#include "game/ViewAnchor.hpp"

namespace mevr::game::hand_targets {
namespace {

// The hand bone is the wrist joint; the grip pose sits in the palm. OpenXR grip space: -Z points
// along a held object's barrel, so the wrist is behind it along +Z. [verify in headset]
constexpr float kWristBehindGripMeters = 0.07f;

bool gDesktopTest = false;

XrVector3f Wrist(XrPosef const& grip) {
    XrQuaternionf const& q = grip.orientation;
    ik::Vec3 const back = ik::Rotate(ik::Quat{q.x, q.y, q.z, q.w}, {0.0, 0.0, kWristBehindGripMeters});
    return {grip.position.x + static_cast<float>(back.x), grip.position.y + static_cast<float>(back.y),
        grip.position.z + static_cast<float>(back.z)};
}

} // namespace

Targets Current(std::optional<engine::UnrealVector> const& headBone) {
    Targets targets;
    if (gDesktopTest) {
        targets.source = Source::DesktopTest;
        return targets;
    }
    // The controllers are tracked in the head's space; the view anchor says where that space is
    // in the world for the view being shown (none in menus, the flat screen or without tracking).
    auto const anchor = view_anchor::Current();
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!anchor || !controllers.active) {
        return targets;
    }
    targets.source = Source::Controllers;
    engine::TrackingFrame body = *anchor;
    if (headBone) {
        body.headWorld = *headBone;  // hands around Shepard's head, not the camera
    }
    // Hands relative to the body at the body's own scale; a renderer may scale head motion
    // differently (Halcyon's lean). [verify in headset: reach matches Shepard's arms]
    body.unitsPerMeter = engine::kUnitsPerMeter;
    for (int side : {0, 1}) {
        vr::controllers::Hand const& hand = side == 0 ? controllers.left : controllers.right;
        if (!hand.gripValid) {
            continue;
        }
        Hand& out = targets.hands[side];
        out.valid = true;
        out.position = engine::ToWorld(body, Wrist(hand.grip));
        // The grip's -Z, +X, +Y in the world are the images of UE's X, Y, Z.
        engine::Basis const axes = engine::OrientationToWorld(body, hand.grip.orientation);
        out.hasRotation = true;
        out.rotation = ik::FromAxes({axes.forward.x, axes.forward.y, axes.forward.z},
            {axes.right.x, axes.right.y, axes.right.z}, {axes.up.x, axes.up.y, axes.up.z});
    }
    return targets;
}

void SetDesktopTest(bool on) {
    gDesktopTest = on;
}

bool DesktopTest() {
    return gDesktopTest;
}

char const* Name(Source source) {
    switch (source) {
    case Source::Controllers: return "controllers";
    case Source::DesktopTest: return "desktop test";
    default: return "none";
    }
}

} // namespace mevr::game::hand_targets
