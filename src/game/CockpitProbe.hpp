// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// An experiment, not a feature (research/47): can the mod, at run time and with nothing in the game's install changed, put
// a static mesh the game has already loaded into the world, move it every tick with the player or the Mako, and show a
// live SceneCapture2D render target on it, drawn with correct depth in both eyes? The answer decides whether the Mako
// cockpit is a real mesh in the world or something the host draws.
//
// Completely inert until a `mevr cockpit ...` console line (tools/console.ps1) asks for something: one early return per
// tick and no resolved handle before that. LE1 only (a message elsewhere). Everything through our reflection layer on the
// game thread; a failed lookup answers with a readable line and changes nothing. A new level forgets what it spawned (the
// old world's objects are gone), `mevr cockpit clear` and the mod's shutdown destroy it. Log prefix `cockpit probe:`.
namespace mevr::game::cockpit_probe {

// One `mevr cockpit ...` line without the two first words; what it answers goes to console.result. Game thread.
std::string Command(std::string const& line);
// `mevr vehicle [enter]`: game mode, map, the pawn, the nearest vehicle (what a console `open <map>` produced); `enter`
// calls BioEnterVehicle on the closest one. Game thread.
std::string VehicleCommand(std::string const& line);
// Each mod tick, late (the view anchor current).
void Tick();
// Snapshot JSON ("cockpitProbe").
std::string StatusJson();
// Heartbeat line (while something is up).
void LogStats();
// Mod shutdown: destroys what it spawned unless the process is exiting (then nothing is called).
void Shutdown(bool exiting);

} // namespace mevr::game::cockpit_probe
