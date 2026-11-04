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

// The Flash map's calls into the game, for learning its protocol: post-handlers on BioSFHandler.HandleEvent and
// HandleExternalEvent (registered before the ProcessEvent hook goes in; inert until `mevr galaxy trace on`), logged as
// `galaxy trace:` with their command and string arguments.
void TryStartup();
std::string Trace(bool on);
// `mevr galaxy input <event> <value>`: the map handler's own HandleInputEvent (2/3 the left stick, 6/7 mouse deltas, 50 select
// the highlighted galaxy object), as the game's input would call it.
std::string Input(int event, float value);
// `mevr galaxy movie`: the open map's Flash movie (its panel's MovieRef, a GFxMovieInfo) RawData to
// %LOCALAPPDATA%\MEVR\galaxy\movie.gfx, for listing its ActionScript identifiers offline (local game data).
std::string Movie();
// `mevr galaxy invoke <method>`: the map panel's InvokeMethodReturn (an ActionScript function on the movie); `mevr galaxy var
// <path>`: its GetVariableString.
std::string Invoke(std::string const& method);
std::string Variable(std::string const& path);
// `mevr galaxy aim <i>`: the map's cross-hair actor moved onto m_pScaleformObjects[i] (Actor.SetLocation), so the camera
// behaviour's own tick selects the object nearest it (within m_nScanRange) and flies the camera there.
std::string Aim(int index);

} // namespace mevr::game::galaxy_dump
