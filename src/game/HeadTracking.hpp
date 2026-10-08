#pragma once

#include "engine/XrToUnreal.hpp"

#include <openxr/openxr.h>

#include <optional>

// The game view follows the headset. Rendered yaw = the game's own yaw (mouse / stick turning)
// + head yaw; rendered pitch and roll come from the head.
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
// Render path extras (off by default, so A/B tests stay clean):
//   6DOF (`/`): head position moves the rendered view too (LOCAL space, 1 m = 100 units).
//   Alternate-eye stereo (`;`): frames alternate between the left and right eye, each rendered
//     half an IPD to its side and tagged with that eye's pose; the output keeps each eye's latest
//     image, so the headset always shows both (each refreshed at half the frame rate).
//
// Head source: the headset when VR runs head-tracked, else the desktop simulator (head_sim),
// so the camera path can be checked without VR. Stereo needs the headset.
namespace mevr::game::head_tracking {

enum class Path { Render, Camera, Off };
enum class Stereo { Mono, AlternateEye };

// Finds the functions and registers ProcessEvent handlers. False (nothing registered) until the
// script packages have loaded; retry later. Call before installing the ProcessEvent hook.
// `renderPathAvailable` is false if the CalcSceneView hook couldn't be installed.
bool TryStartup(bool renderPathAvailable);
// The ProcessEvent hook is live: start counting views for the render-path self-checks.
void SetReady();
void LogStats();
void Shutdown();

// Game thread, from engine/scene_view: the frame's player view is about to be built.
void OnBeforePrimaryView();
// Game thread, after each engine tick (before the lockstep wait): gives a tick that drew no
// player view a fallback pose, so VR always has an image.
void OnTickEnd();

Path CyclePath();
char const* Name(Path path);
Stereo CycleStereo();
char const* Name(Stereo stereo);
bool TogglePositional();

// Head yaw (Unreal units) the rendered view is turned by beyond the game camera: non-zero only on
// the render path during on-foot gameplay (not conversations, menus, wheels, galaxy map or
// the Mako). Input uses it so "forward" on the stick means where the player looks. Any thread.
int RenderYawOffset();

// The game's own (body) yaw, in Unreal units, before the head is added: the world direction of
// LOCAL space's forward. Valid once a view has been rotated. Game thread.
std::optional<int> GameYaw();

// Where the last head-rotated player view put the head: its world location (the camera plus any
// 6DOF offset, without the stereo eye offset), the body yaw, and the head pose (LOCAL) it was
// built from. Maps LOCAL-space points such as controllers into the world (engine::ToWorld).
// Render path only. Game thread.
struct ViewAnchor {
    engine::UnrealVector headWorld;
    int gameYaw = 0;
    XrPosef head{};
};
std::optional<ViewAnchor> Anchor();
// On-foot gameplay (SFXGameModeManager mode 0) as of this frame's camera update. Game thread.
bool OnFoot();

} // namespace mevr::game::head_tracking
