#pragma once

#include "engine/XrToUnreal.hpp"

#include <optional>

// Where tracking space (the head's and the controllers' OpenXR space) sits in the world for the
// view the player is seeing: the one place features that put the player's hands into the world
// (hand aim, arm IK) get that mapping, whichever renderer draws VR.
//   - Our renderer (LE2/LE3, or LE1 without Halcyon): the render path's anchor from
//     game/head_tracking (level, turned by the body yaw; head-tracked VR only).
//   - The host's renderer (LE1: Halcyon's): its published view (core/vr/HostView), placed on the
//     game camera (CameraCache.POV) it was built from: engine::FromCameraView.
// Fails closed: no value without a head-tracked 3D view (menus, flat screen, lost tracking, no
// player camera, a stale host view). The first valid frame is logged once, with a check of the
// game camera against the host renderer's own view.
namespace mevr::game::view_anchor {

// Mod startup: the host renders VR and publishes its view (HostServices v3).
void UseHostView(bool on);
// Game thread.
std::optional<engine::TrackingFrame> Current();
// The mod's own turn (game/comfort, research/31) moved the player controller's yaw by `units` (and the pawn's with
// `pawnTurned`, about its location (pivotX, pivotY)) after this tick's camera update. Until the camera updates again,
// Current() maps the view as it will be then (the game camera turned too), so arm IK poses the hands and the body
// for the frame that shows the turn, and a shot fired before the camera update aims from the turned view. Game
// thread.
void NoteTurn(int units, bool pawnTurned, double pivotX, double pivotY);
// Heartbeat (host renderer only): what the host's latest view says.
void LogStats();
void Shutdown();

} // namespace mevr::game::view_anchor
