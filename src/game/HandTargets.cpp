#include "game/HandTargets.hpp"

#include "core/vr/Controllers.hpp"
#include "game/HandCalibration.hpp"
#include "game/ViewAnchor.hpp"

#include <cmath>

namespace mevr::game::hand_targets {
namespace {

// The hand bone is the wrist joint; the grip pose sits in the palm. OpenXR grip space: -Z points
// along a held object's barrel, so the wrist is behind it along +Z (UE-style grip axes: -X).
// Shepard's units, not the player's: it's his hand. [verify in headset]
constexpr double kWristBehindGripUnits = 7.0;
// First person: the view this close to Shepard's head bone (Unreal units); third person again
// beyond the second (hysteresis). Halcyon's first-person eye sits near the head; its third-person
// camera is a metre or more behind. [verify: the log's "body anchor" line gives the distance]
constexpr double kFirstPersonEnterUnits = 45.0;
constexpr double kFirstPersonLeaveUnits = 75.0;

bool gDesktopTest = false;
bool gFirstPerson = false;

// A tracking-space orientation as a world rotation: its -Z, +X, +Y become UE's X, Y, Z.
ik::Quat WorldRotation(engine::TrackingFrame const& frame, XrQuaternionf const& orientation) {
    engine::Basis const axes = engine::OrientationToWorld(frame, orientation);
    return ik::FromAxes({axes.forward.x, axes.forward.y, axes.forward.z}, {axes.right.x, axes.right.y, axes.right.z},
        {axes.up.x, axes.up.y, axes.up.z});
}

double Distance(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

} // namespace

Targets Current(std::optional<engine::UnrealVector> const& animatedHead) {
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
    // The scale the player sees the world at (Halcyon: 50 units per meter, its lean and its eye
    // separation), so the hands land where the player sees the controllers. Mapping them at 100
    // while the view used 50 put them twice as far from the head as the controllers were
    // (research/14, "Reach").
    double const scale = hand_calibration::TrackingScale() > 0.0 ? hand_calibration::TrackingScale() : anchor->unitsPerMeter;
    targets.viewToHeadBone = animatedHead ? Distance(anchor->headWorld, *animatedHead) : -1.0;
    if (animatedHead) {
        gFirstPerson = gFirstPerson ? targets.viewToHeadBone <= kFirstPersonLeaveUnits
                                    : targets.viewToHeadBone <= kFirstPersonEnterUnits;
    } else {
        gFirstPerson = false;
    }
    if (gFirstPerson) {
        // The view's own frame, scaled about the live head: the controllers exactly where the
        // player sees them.
        targets.anchor = Anchor::View;
        targets.body = {anchor->head, anchor->headWorld, anchor->rotation, scale};
    } else if (animatedHead) {
        // Measured from tracking space's origin, placed at Shepard's animated head bone: hands
        // around Shepard, and leaning or 6DOF moves only the view. The anchor gives the turn.
        targets.anchor = Anchor::HeadBone;
        targets.body = engine::AtOrigin(*anchor, *animatedHead, scale);
    } else {
        targets.anchor = Anchor::Origin;
        targets.body = engine::AtOrigin(*anchor, engine::OriginWorld(*anchor), scale);
    }
    targets.head = anchor->head;
    targets.headWorld = engine::ToWorld(targets.body, anchor->head.position);
    for (int side : {0, 1}) {
        vr::controllers::Hand const& hand = side == 0 ? controllers.left : controllers.right;
        if (!hand.gripValid) {
            continue;
        }
        Hand& out = targets.hands[side];
        out.valid = true;
        // The grip's -Z, +X, +Y in the world are the images of UE's X, Y, Z.
        out.hasRotation = true;
        out.rotation = WorldRotation(targets.body, hand.grip.orientation);
        engine::UnrealVector const grip = engine::ToWorld(targets.body, hand.grip.position);
        ik::Vec3 const back = ik::Rotate(out.rotation, {-kWristBehindGripUnits, 0.0, 0.0});
        out.position = {grip.x + back.x, grip.y + back.y, grip.z + back.z};
        // The aim pose (where the controller points: a held gun's barrel follows it).
        if (hand.aimValid) {
            out.hasAim = true;
            out.aim = WorldRotation(targets.body, hand.aim.orientation);
        }
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

char const* Name(Anchor anchor) {
    switch (anchor) {
    case Anchor::View: return "first person (the view's frame)";
    case Anchor::HeadBone: return "third person (tracking origin at Shepard's animated head)";
    case Anchor::Origin: return "the view's origin (no head bone)";
    default: return "none";
    }
}

} // namespace mevr::game::hand_targets
