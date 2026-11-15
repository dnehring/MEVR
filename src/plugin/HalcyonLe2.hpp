// SPDX-License-Identifier: MIT
#pragma once

// Halcyon's ME2 VR renderer (src/halcyon/le2, GPL-3.0, upstream jrush64/MassEffect2-VR 4784e90), running inside our LE2
// host ASI as LE1's runs inside the LE1 host (plugin/Halcyon). It owns the OpenXR session, the Present path, stereo, first
// person and its Insert-key menu. Its log is MELE2VR_Log.txt (its BaseDir) and its settings MELE2VR.ini next to
// MassEffect2.exe (upstream's places). docs/le2-from-le1.md section 3.1; research/le2-renderer-integration.md.
namespace mevr::host::halcyon_le2 {

// What upstream's DllMain and startup thread did, minus the dxgi.dll proxying and the diagnostic probes: the settings it
// writes before the engine reads them (its render resolution, depth of field), its display hooks, and hooks on the system
// dxgi.dll's factory functions so the game's swap chain is captured when it's made. Call once, early (before the game
// makes its D3D device). Also takes [Debug] openxrRuntime from ASI\LE2MEVR.ini (the fake headset) before anything opens
// an OpenXR loader or a device.
void Attach();

// Its Present-thread ProcessEvent calls are queued in plugin/GameThreadCalls (MevrBridge::QueueProcessEvent), as LE1's.

// Game thread, after each engine tick: the test loop's keys and request files (plugin/TestLoop), as LE1's
// halcyon::Tick.
void AfterTick(float deltaSeconds);

} // namespace mevr::host::halcyon_le2
