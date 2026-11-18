// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace mevr::hook { class Context; }
namespace mevr::ue3 { struct Object; }
namespace mevr::game::le3_galaxy_retention {
// A bounded diagnostic until the projector renderer can own the same lifecycle.
// Uses the engine's streaming requests, never plot states or streamlevelin.
void Startup(hook::Context& hooks);
bool Armed();
// The CIC's art level is loaded and visible in the player's world (the room the projector stands in). Game thread.
bool CicArtVisible(ue3::Object* controller);
// Arms the lease on foot (it waits 20 s for the map to open; empty: armed, else why not). `production`: the projector's own
// (game/Le3GalaxyCapture), which lasts as long as the map once it opened instead of the diagnostic's 120 s.
std::string Arm(ue3::Object* controller, bool production);
bool Active(); // current map, same live scope; game thread only
// The map has been seen and the game is in it or in one of its interludes (galaxy_projector::MapInterlude: the travel's movie,
// a call, a question, a load): the production lease keeps the CIC's art through them (research/51 section 8.4). Game thread.
bool Kept();
// The galaxy map's own level (BioA_Nor_203aGalaxyMap) is requested by the native streaming: the console's use has begun the
// map's opening (its cinematic before the map's mode). Game thread.
bool MapLevelRequested(ue3::Object* controller);
bool Shutdown(hook::Context& hooks, bool exiting);
std::string Command(ue3::Object* controller, std::string const& action);
}
