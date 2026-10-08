#pragma once

struct IDXGISwapChain;

// M2: shows the game's final frame on a flat panel in the headset ("virtual screen").
// Also the fallback view for conversations and cutscenes later (docs/vr-features-plan.md).
namespace mevr::vr::virtual_screen {

// Render thread, once per game frame, before Present.
void OnPresent(IDXGISwapChain* swapChain);
// Releases the XR session and GPU resources. Not concurrent with OnPresent.
void Shutdown();

} // namespace mevr::vr::virtual_screen
