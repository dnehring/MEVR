#pragma once

// M3: the game camera follows the headset (rotation only, mono).
//
// Once per game frame, at Engine.Camera.UpdateCamera, the latest predicted head pose is read and
// queued (with the camera's FOV) for the render thread, which tags that frame's image with it.
// Every PlayerController.GetPlayerViewPoint that frame then returns:
//   yaw   = the game's own yaw (mouse / stick turning) + head yaw
//   pitch = head pitch, roll = head roll (the game's pitch is ignored)
// Aim follows the view for now, since weapons also ask GetPlayerViewPoint.
namespace mevr::game::head_tracking {

// Finds the functions and registers ProcessEvent handlers. False (nothing registered) until the
// script packages have loaded; retry later. Call before installing the ProcessEvent hook.
bool TryStartup();
void LogStats();
void Shutdown();

} // namespace mevr::game::head_tracking
