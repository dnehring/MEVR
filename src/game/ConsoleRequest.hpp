#pragma once

// The test loop's console (research/22): %LOCALAPPDATA%\MEVR\console.request holds console commands, one per line
// (e.g. "UMBRA OCCLUSIONCULLING"); the mod runs each through the local player's PlayerController.ConsoleCommand, as the
// game's own console would, and writes what they print to console.result (tools/console.ps1). For A/B tests without
// the game's console or a restart; works flat and in VR, wherever there is a local player. Game thread.
namespace mevr::game::console_request {

// Looks for a request twice a second.
void Tick(float deltaSeconds);

} // namespace mevr::game::console_request
