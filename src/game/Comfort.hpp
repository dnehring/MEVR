#pragma once

#include "game/GamepadMapping.hpp"

#include <string>

// Comfort options on foot (research/31; the rules and numbers: core/vr/Comfort; settings: [Comfort], our VR menu's
// Comfort page). LE1, the host's renderer (HostServices v17).
//
// Turning: with [Comfort] turn = smooth (default) or snap, the mod turns the player itself and the game sees no
// right-stick X from the motion controllers (gamepad::kFlagModTurns, through support_grip's pad flags): smooth at
// turnSpeed degrees a second, or snapDeg per flick (re-armed near the centre, a cooldown between). turn = game leaves
// it to the game (its per-camera-mode SFXCameraInput: rate, acceleration, dead zone), as before.
// - Where: the player controller's rotation (ControlRotation) yaw, from the mod's tick, before arm IK. The pawn turns
//   with it when its facing follows the controller (combat: Actor.SetRotation through ProcessEvent, at once rather
//   than at the game's next UpdateRotation), and the view anchor is told (view_anchor::NoteTurn), so the hands and
//   the body are posed for the frame that shows the turn: no frame where the arms or shoulders swing by the snap.
//   Body follow's state is in tracking space, which a turn doesn't move: it neither re-anchors nor lags.
// - When: on foot (game mode 0) in a head-tracked view, a pawn, not in cover or on a ladder, a camera mode where the
//   hand aims and the stick only turns (Explore, Combat, TightAim and their Storm modes: not the sniper scope), the
//   squad menu and our main menu closed, the controllers tracked, and a v17 host (an older one would still pass the
//   stick to the game: a double turn). Anywhere else the game turns, as before.
// The vignette: how dark the view's edges should be, each tick (HostServices::setVignette): [Comfort] vignette,
// vignetteStrength, vignetteWhen (turning; or turning and moving with the left stick), and a 0.2 s pulse after each
// snap. The host fades and draws it (plugin/Vignette).
// Walking toward the left hand: [Comfort] walk = hand sets gamepad::kFlagHandWalk on foot outside menus and wheels;
// the host turns the controllers' left stick by the left controller's flat yaw (HalcyonBridge's WalkTowardHand).
//
// Logs `comfort:` (settings, who turns and why, each snap, the game's own turn rate per camera mode, the heartbeat);
// snapshots carry `"comfort"` in the mod's status. Game thread.
namespace mevr::game::comfort {

struct Link {
    bool supported = false;               // a v17 host: it knows kFlagModTurns / kFlagHandWalk and draws the vignette
    void (*setVignette)(float amount) = nullptr;
    bool headTrackedView = false;         // the host shows a head-tracked world view (not Halcyon's menu, not flat)
    bool mainMenuOpen = false;            // our VR main menu (the host also hides the controllers then)
};

// Each mod tick, before arm IK (the turn must be in the view anchor first) and before the support grip publishes
// the pad flags (AdjustPad).
void Tick(double deltaSeconds, Link const& link);
// The pad flags this tick (support_grip::Tick): kFlagModTurns while the mod turns, kFlagHandWalk while walking toward
// the left hand.
void AdjustPad(gamepad::Options& options);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::comfort
