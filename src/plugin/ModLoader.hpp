// SPDX-License-Identifier: MIT
#pragma once

namespace mevr::loader::spi {
class Interface;
}

namespace mevr::host {

// Monolithic build: calls the linked-in mod directly.
// Hot-reload build: loads <GAME>MEVR_mod.dll through a shadow copy and reloads it whenever
// the file changes. The DLL's path comes from <ASI dir>/<GAME>MEVR.modpath if present
// (tools/deploy-asi.ps1 points it at build output), else <ASI dir>/<GAME>MEVR_mod.dll.
//
// Unloading a mod generation frees its DLL only if nothing can still run its code: if a hook
// couldn't be removed, or a call was still inside one when the wait gave up (the mod reports
// these through HostServices::keepLoaded; the host's own ProcessEvent callback wait counts too),
// the DLL is pinned instead, logged as an error, and the next build loads beside it.
void Start(loader::spi::Interface* spi);
// Call every engine tick (game thread): checks for a new build, then ticks the mod.
void Tick(float deltaSeconds);
void Stop();

// The process is exiting (the ExitProcess hook, or LEBinkProxy detaching). From then on nothing
// waits for other threads (they may never run again), the mod is told so
// (HostServices::processExiting), and Stop leaves the mod DLL mapped for Windows to unload.
void SetProcessExiting();
bool ProcessExiting();
// The voice recognizer's and the model tier's threads stopped (they join and wait). Only where the DLLs will be
// unloaded with the game's threads already gone (LEBinkProxy's detach), never in the ExitProcess hook: there the
// joins kept the game's workers running ~20 ms past the game's teardown, into its deleted locks (research/36 "The
// fault at quit"), and the process is terminated with no DLL unload anyway.
void StopWorkersForUnload();

} // namespace mevr::host
