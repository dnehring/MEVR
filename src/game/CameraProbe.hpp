// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The player camera's hook, logged on each change (research/18, proposal 3). LE1's active camera
// mode (SFXCameraNativeBase.CurrentCameraMode) places the camera at the bone of Shepard's body
// mesh named by its HookName (`Camera` for exploration, combat, crouch and tight aim), plus its
// HookOffset turned by the view's yaw, minus its Offset turned by the view (the Offset is what
// Halcyon's first person writes). Each line gives the mode's class and name, HookName,
// HookOffset, Offset and LastHookPos, the hook bone's world position against the game camera,
// and the camera GetCameraLocation's formula predicts from that bone against where the mode and
// the game camera actually put it: a small gap confirms the bone is the hook in game.
// Read-only, game thread; LE1 only (does nothing elsewhere).
namespace mevr::game::camera_probe {

// Each mod tick, on foot: logs when the mode, its hook name, HookOffset or Offset changed (at
// most four lines a second).
void Tick();
void Shutdown();

// `mevr camera trace <seconds> [label]` (an idle sway report, 2026-11-14; the skeleton audit, research/72): every mod tick
// for that long, a CSV row in %LOCALAPPDATA%\MEVR\camera-trace-<stamp>[-label].csv: the pawn's place and yaw, its velocity,
// the game camera's place and rotation, and 14 bones (Camera, God, Root, Pelvis, spine, neck, head, collars, hips, ankles)
// in the mesh component's space (what the
// animation does, whatever the pawn does). Standing still with a still head, every change of the camera against the pawn
// is sway the game puts into the view. Returns the file's path, or why not.
std::string Command(std::string const& args);

} // namespace mevr::game::camera_probe
