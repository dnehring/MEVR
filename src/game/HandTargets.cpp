// SPDX-License-Identifier: MIT
#include "game/HandTargets.hpp"

#include "core/ik/BodyFollow.hpp"
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
// First person: the view within 45 units of Shepard's head bone (Unreal units); third person again beyond 75 (hysteresis):
// ik::AnchorSettings. Halcyon's first-person eye sits near the head; its third-person camera is a metre or more behind. With
// the view's height the player's own (the floor lock, a crouch on foot), only the horizontal distance counts
// (ik::FirstPersonAnchor; research/29 "The crouch that went third person").

bool gDesktopTest = false;
bool gFirstPerson = false;

double Distance(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

} // namespace

Targets Current(std::optional<engine::UnrealVector> const& animatedHead, bool inView, bool allowHeadOnly, bool viewHeightIsPlayers) {
    Targets targets;
    if (gDesktopTest) {
        targets.source = Source::DesktopTest;
        return targets;
    }
    // The controllers are tracked in the head's space; the view anchor says where that space is
    // in the world for the view being shown (none in menus, the flat screen or without tracking).
    auto const anchor = view_anchor::Current();
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!anchor) {
        ResetTracking();
        return targets;
    }
    bool const headOnly = allowHeadOnly &&
        (!controllers.active || (!controllers.left.gripValid && !controllers.right.gripValid));
    if ((!controllers.active && !headOnly) ||
        (headOnly && (view_anchor::Held() || !vr::host_view::Latest().valid))) return targets;
    targets.source = headOnly ? Source::HeadOnly : Source::Controllers;
    // The scale the player sees the world at (Halcyon: 50 units per meter, its lean and its eye
    // separation), so the hands land where the player sees the controllers. Mapping them at 100
    // while the view used 50 put them twice as far from the head as the controllers were
    // (research/14, "Reach").
    double const scale = hand_calibration::TrackingScale() > 0.0 ? hand_calibration::TrackingScale() : anchor->unitsPerMeter;
    targets.viewToHeadBone = animatedHead ? Distance(anchor->headWorld, *animatedHead) : -1.0;
    if (animatedHead) {
        targets.viewFromHeadBone = {anchor->headWorld.x - animatedHead->x, anchor->headWorld.y - animatedHead->y,
            anchor->headWorld.z - animatedHead->z};
    }
    ik::AnchorInput anchorIn;
    anchorIn.haveHead = animatedHead.has_value();
    anchorIn.viewFromHead = {targets.viewFromHeadBone.x, targets.viewFromHeadBone.y, targets.viewFromHeadBone.z};
    anchorIn.playerHeight = viewHeightIsPlayers;
    targets.viewHeightIsPlayers = viewHeightIsPlayers;
    targets.anchorGap = ik::AnchorGap(anchorIn);
    gFirstPerson = ik::FirstPersonAnchor(gFirstPerson, anchorIn);
    if (inView || gFirstPerson) {
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
    if (headOnly) {
        // Latest() marks stale controller samples inactive but retains their old
        // grip/button fields. Never promote those fields into new targets.
        return targets.anchor == Anchor::View ? targets : Targets{};
    }
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
        out.position = WristFromGrip(targets.body, hand.grip);
        // The aim pose (where the controller points: a held gun's barrel follows it).
        if (hand.aimValid) {
            out.hasAim = true;
            out.aim = WorldRotation(targets.body, hand.aim.orientation);
        }
    }
    return targets;
}

ik::Quat WorldRotation(engine::TrackingFrame const& frame, XrQuaternionf const& orientation) {
    engine::Basis const axes = engine::OrientationToWorld(frame, orientation);
    return ik::FromAxes({axes.forward.x, axes.forward.y, axes.forward.z}, {axes.right.x, axes.right.y, axes.right.z},
        {axes.up.x, axes.up.y, axes.up.z});
}

engine::UnrealVector WristFromGrip(engine::TrackingFrame const& frame, XrPosef const& grip) {
    engine::UnrealVector const at = engine::ToWorld(frame, grip.position);
    ik::Vec3 const back = ik::Rotate(WorldRotation(frame, grip.orientation), {-kWristBehindGripUnits, 0.0, 0.0});
    return {at.x + back.x, at.y + back.y, at.z + back.z};
}

void SetDesktopTest(bool on) {
    gDesktopTest = on;
}

void ResetTracking() { gFirstPerson = false; }

bool DesktopTest() {
    return gDesktopTest;
}

char const* Name(Source source) {
    switch (source) {
    case Source::Controllers: return "controllers";
    case Source::DesktopTest: return "desktop test";
    case Source::HeadOnly: return "head only";
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
