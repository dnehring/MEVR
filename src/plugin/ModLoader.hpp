#pragma once

class ISharedProxyInterface;

namespace mevr::host {

// Monolithic build: calls the linked-in mod directly.
// Hot-reload build: loads <GAME>MEVR_mod.dll through a shadow copy and reloads it whenever
// the file changes. The DLL's path comes from <ASI dir>/<GAME>MEVR.modpath if present
// (tools/deploy-asi.ps1 points it at build output), else <ASI dir>/<GAME>MEVR_mod.dll.
void Start(ISharedProxyInterface* spi);
// Call every engine tick (game thread): checks for a new build, then ticks the mod.
void Tick(float deltaSeconds);
void Stop();

} // namespace mevr::host
