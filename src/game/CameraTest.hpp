#pragma once

// M1 test: prove the plugin controls the rendered view.
// Backslash toggles a slow ±30° yaw sweep applied to PlayerController.GetPlayerViewPoint's
// output. Also counts how often each camera-related UnrealScript event fires through
// ProcessEvent, so we learn which hook points ME1 actually uses.
namespace mevr::game::camera_test {

// Finds the watched functions and registers ProcessEvent handlers. Returns false, with nothing
// registered, while the script packages that define them aren't loaded yet; retry later.
// Call before installing the ProcessEvent hook.
bool TryStartup();
void Tick(float deltaSeconds);
void LogStats();
void Shutdown();

} // namespace mevr::game::camera_test
