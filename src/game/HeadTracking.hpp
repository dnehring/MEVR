#pragma once

// The game view follows the headset (rotation only, mono). Rendered yaw = the game's own yaw
// (mouse / stick turning) + head yaw; rendered pitch and roll come from the head.
//
// Paths (`\` cycles them, for A/B tests):
//   Render (default): only the frame's player view is rotated, inside CalcSceneView
//     (engine/scene_view). The game camera, aim and AI keep the unrotated view, so the
//     third-person camera can't swing or smooth against the head. Aim stays with the game camera
//     for now (no head aim).
//   Camera (M3): every GetPlayerViewPoint is rotated and the camera FOV overridden. The camera
//     boom and aim follow the head.
//   Off: frames are still tagged, so the view box follows the head but its picture doesn't.
//
// Head source: the headset when VR runs head-tracked, else the desktop simulator (head_sim),
// so the camera path can be checked without VR.
namespace mevr::game::head_tracking {

enum class Path { Render, Camera, Off };

// Finds the functions and registers ProcessEvent handlers. False (nothing registered) until the
// script packages have loaded; retry later. Call before installing the ProcessEvent hook.
// `renderPathAvailable` is false if the CalcSceneView hook couldn't be installed.
bool TryStartup(bool renderPathAvailable);
void LogStats();
void Shutdown();

// Game thread, from engine/scene_view: the frame's player view is about to be built.
void OnBeforePrimaryView();

Path CyclePath();
char const* Name(Path path);

} // namespace mevr::game::head_tracking
