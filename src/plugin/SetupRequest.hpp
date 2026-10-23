// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The in-game side of MEVR-Setup's optional content (research/33): the main menu's Display page lists the packs the
// setup installed (ASI\MEVR-install.json) and offers "Install after I quit", which writes
// %LOCALAPPDATA%\MEVR\setup.request; when the game exits (the host's ExitProcess hook) the setup copy in the ASI
// folder is started with --request --wait-pid <our pid>, and opens on Optional content once the game has gone.
namespace mevr::host::setup_request {

// "ALOT for ME1, ..." or "None yet" (re-read at most every 2 s).
std::string InstalledPacksText();
// Writes the request. The message for the menu either way.
bool Request(std::string& message);
// At exit: starts the setup if a request is waiting and the setup is in the ASI folder. Never blocks.
void LaunchIfRequested();

} // namespace mevr::host::setup_request
