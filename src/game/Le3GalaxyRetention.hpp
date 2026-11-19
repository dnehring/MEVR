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
// The projector's lease for a map already open (a reload with the map open loads straight into its mode): armed as if the map
// had been seen, if the CIC's art is loaded and visible in the map's mode; the native requests are taken over at once if they
// would unload it (journalled; put back as Release does). Empty: armed, else why not. Game thread.
std::string ArmInMap(ue3::Object* controller);
// The projector's lease, armed and still waiting for the map, waits 20 s from now: the player still stands at the projector,
// or the map's opening has begun. Its 20 s ran out in the opening's cinematic once the opening was 0.1 s long (lane GX4: the
// lease armed 20.0 s before the console's use was released in the 0.1 s cinematic; the map opened without the room).
void Refresh();
bool Shutdown(hook::Context& hooks, bool exiting);
std::string Command(ue3::Object* controller, std::string const& action);
}
