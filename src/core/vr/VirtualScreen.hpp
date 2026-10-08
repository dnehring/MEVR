#pragma once

struct IDXGISwapChain;

// M2: shows the game's final frame on a flat panel in the headset ("virtual screen").
// Also the fallback view for conversations and cutscenes later (docs/vr-features-plan.md).
namespace mevr::vr::virtual_screen {

// Render thread, once per game frame, before Present.
void OnPresent(IDXGISwapChain* swapChain);
// Releases the XR session and GPU resources. Not concurrent with OnPresent.
void Shutdown();

// Any thread. Disabling tears the XR session down on the next frame (the game keeps running
// on the monitor); enabling starts a new one. Quitting from the headset menu disables it.
void SetEnabled(bool enabled);
bool IsEnabled();

} // namespace mevr::vr::virtual_screen
