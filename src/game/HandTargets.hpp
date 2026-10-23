#pragma once

#include "core/ik/Rigid.hpp"
#include "engine/XrToUnreal.hpp"

#include <array>
#include <optional>

// Where Shepard's hands (and head) should be: the one place that knows the hand source, so
// swapping it (e.g. for Halcyon's renderer, which owns the camera) touches only this file. Arm IK
// (game/arm_ik) turns these into bone targets.
namespace mevr::game::hand_targets {

enum class Source {
    None,         // nothing to follow: the animation keeps the arms
    Controllers,  // tracked motion controllers
    DesktopTest,  // no headset: arm IK holds the right hand in front of the chest (no positions here)
};

struct Hand {
    bool valid = false;
    engine::UnrealVector position;  // world, Unreal units: where the hand bone (wrist joint) goes
    bool hasRotation = false;
    ik::Quat rotation;  // world, UE3 axes: the controller's grip orientation
    bool hasAim = false;
    ik::Quat aim;  // world, UE3 axes: the controller's aim orientation (X = where it points, Z = its up)
};

// How tracking space is placed on the body this tick.
enum class Anchor {
    None,
    View,      // first person: the view's own frame (the controllers where the player sees them)
    HeadBone,  // third person: tracking space's origin at Shepard's animated head bone
    Origin,    // no head bone: the origin where the view puts it
};

struct Targets {
    Source source = Source::None;
    std::array<Hand, 2> hands;  // 0 left, 1 right
    // Controllers only: the frame used, and the headset in it.
    Anchor anchor = Anchor::None;
    engine::TrackingFrame body;
    XrPosef head{};                 // the headset, tracking space
    engine::UnrealVector headWorld;  // the headset (the eyes' midpoint) in the world
    double viewToHeadBone = -1.0;    // units between the view and Shepard's head bone (-1: no bone)
    engine::UnrealVector viewFromHeadBone;  // the view minus Shepard's animated head bone (world)
};

// Game thread, once per tick: the motion controllers' grip poses, placed in the world through
// game/view_anchor (whichever renderer draws VR); or DesktopTest while the mod says there's no
// headset.
// - Scale: the view's own units per meter (Halcyon's: 50), or [Hands] trackingScale: the scale at
//   which the player sees the world, so a hand lands where the player sees the controller.
// - First person (the view within about 45 units of Shepard's animated head bone, leaving at 75):
//   the view's own frame. Third person: tracking space's origin (the head at the start or the
//   last recenter) at `animatedHead`, the head bone as the animation left it (not after our spine
//   writes, which move it), so the hands go around Shepard, not the camera, and leaning moves the
//   view, not the hands. Without a head bone, where the view puts the origin.
// - The wrist sits 7 units behind the grip, along the grip's -Z (in Shepard's units).
Targets Current(std::optional<engine::UnrealVector> const& animatedHead);

// Set by the mod every tick: true while there's no headset to follow (our renderer: no VR session;
// the host's: no headset session seen yet). Arm IK then holds the right hand in front of the chest.
void SetDesktopTest(bool on);
bool DesktopTest();

// A tracking-space orientation as a world rotation (its -Z, +X, +Y become UE's X, Y, Z), as the hands' are.
ik::Quat WorldRotation(engine::TrackingFrame const& frame, XrQuaternionf const& orientation);
// Where a hand bone (the wrist) goes for a grip pose (tracking space): the grip in the world, 7 units back.
engine::UnrealVector WristFromGrip(engine::TrackingFrame const& frame, XrPosef const& grip);

char const* Name(Source source);
char const* Name(Anchor anchor);

} // namespace mevr::game::hand_targets
