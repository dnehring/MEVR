// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace mevr::vr::floor {
struct State;
}

// Halcyon's VR renderer (src/halcyon/le1, GPL-3.0), running inside our host ASI: it owns the
// OpenXR session, the Present path, stereo, first person, the HUD and its Insert-key menu. Its log
// is %LOCALAPPDATA%\MELEVR\MELEVR_Log.txt; its settings are MELEVR.ini next to our ASI.
namespace mevr::host::halcyon {

// What upstream's DllMain and startup thread did, minus the dxgi.dll proxying: early settings
// writes, input and display hooks, and hooks on the system dxgi.dll's factory functions so the
// game's swap chain is captured when it's created. Call once, early (before the game makes its
// D3D device).
void Attach();
// Game thread, after each engine tick.
void Tick(float deltaSeconds);
// Process exit: asks the frame loop to stop.
void Shutdown();

// The mod aims weapons with the right controller (HostServices::setHandAim), or stopped (also
// when it unloads). Combined with the right controller being tracked, this turns Halcyon's
// on-foot combat head aim off (plugin/HalcyonBridge.cpp). Any thread.
void SetModHandAim(bool on);

// The mod's two-handed grip input (HostServices::setSupportGrip), for the pad mapping in the XInput
// hook; stale after 250 ms. Forget drops it now (the mod unloads). Any thread.
void SetSupportGrip(float distanceMeters, float acquireMeters, float releaseMeters, unsigned flags);
void ForgetSupportGrip();
// The pad mapping holds the support grip (HostServices::supportGripHeld).
bool SupportGripHeld();

// The body holsters (HostServices v9). A controller vibration, applied on the next frame after the
// action sync (Halcyon's frame thread); a newer request for a hand replaces a pending one. Any thread.
// `frequencyHz` 0: the runtime's choice (v10 adds the frequency).
void RequestHaptic(int hand, float seconds, float amplitude, float frequencyHz = 0.0f);
// v10: a continuous vibration for `seconds` from now unless asked again (core/vr/Haptics); amplitude
// 0 stops it. Any thread.
void RequestContinuousHaptic(int hand, float amplitude, float frequencyHz, float seconds);
// The mod unloaded: no more vibration from it.
void ForgetHaptics();
// The pad mapping's right grip report (gamepad::RightGripReport). Any thread.
std::uint32_t RightGripFree();
// v11: the pad mapping's left grip report (gamepad::LeftGripReport): squeezes that began away from the
// gun, for the squad weapon menu. Any thread.
std::uint32_t LeftGripAway();

// v10: the head's height above the floor, the latest frame's (core/xr/Floor). Any thread.
void Floor(vr::floor::State* out);
// Halcyon recentered (MevrBridge::OnRecentered): counted in the floor state, since the mod takes the
// standing eye height there.
void NoteRecentered();

} // namespace mevr::host::halcyon
