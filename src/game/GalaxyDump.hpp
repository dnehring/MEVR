// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// `mevr galaxy dump [tag]` (research/51 section 8): the open galaxy map's model, for building the hologram offline. Writes
// %LOCALAPPDATA%\MEVR\galaxy\<time>-<tag>.json: the player camera's view, the galaxy camera behaviour's own fields, every
// actor its lists hold (its class's own properties, read generically, and its place), the Flash handler's fields, and the six
// tables (GalaxyMap_Cluster, _System, _Planet, _Map, _PlotPlanet, _Relay) read through Bio2DA's own getters, with text
// references resolved through BioSPGame.GetSimpleString. Game data: it stays local, never in the repository. Game thread
// (the console request runs there); looks everything up by name (a research tool, no handles).
namespace mevr::game::galaxy_dump {

// Returns a summary, or why nothing was written.
std::string Dump(std::string const& tag);

// `mevr galaxy click <u> <v>`: the cursor to the game window's (u, v) (0..1, y down) and a left click there, as posted window
// messages plus SetCursorPos (the game pointer's two paths, research/42). Tests whether the map takes the pick as a click.
std::string Click(float u, float v);
// `mevr galaxy pick <i>`: where the game's camera shows the map's object i (m_pScaleformObjects[i]; core/vr/GalaxyHologram
// ToGameScreen with the player camera's view and the window's aspect), clicked; `mevr galaxy where` lists every object's place.
std::string Pick(int index, bool click);

} // namespace mevr::game::galaxy_dump
