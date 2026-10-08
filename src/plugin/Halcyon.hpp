#pragma once

// Halcyon's VR renderer (src/halcyon/le1, GPL-3.0), running inside our host ASI: it owns the
// OpenXR session, the Present path, stereo, first person, the HUD and its Insert-key menu. Its log
// is MELEVR_Log.txt and its settings MELEVR.ini, both next to our ASI.
namespace mevr::host::halcyon {

// What upstream's DllMain and startup thread did, minus the dxgi.dll proxying: early settings
// writes, input and display hooks, and hooks on the system dxgi.dll's factory functions so the
// game's swap chain is captured when it's created. Call once, early (before the game makes its
// D3D device).
void Attach();
// Process exit: asks the frame loop to stop.
void Shutdown();

// The mod aims weapons with the right controller (HostServices::setHandAim), or stopped (also
// when it unloads). Combined with the right controller being tracked, this turns Halcyon's
// on-foot combat head aim off (plugin/HalcyonBridge.cpp). Any thread.
void SetModHandAim(bool on);

} // namespace mevr::host::halcyon
