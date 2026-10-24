// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The test loop's console (research/22): %LOCALAPPDATA%\MEVR\console.request holds console commands, one per line
// (e.g. "UMBRA OCCLUSIONCULLING"); the mod runs each through the local player's PlayerController.ConsoleCommand, as the
// game's own console would, and writes what they print to console.result (tools/console.ps1). For A/B tests without
// the game's console or a restart; works flat and in VR, wherever there is a local player. Game thread.
namespace mevr::game::console_request {

// Looks for a request twice a second.
void Tick(float deltaSeconds);

// One command through the local player's ConsoleCommand, now (game thread). False when there is no local player yet or
// the function isn't found; `output` gets what the command printed.
bool RunCommand(std::string const& command, std::string* output);

} // namespace mevr::game::console_request
