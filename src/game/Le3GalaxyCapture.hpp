// SPDX-License-Identifier: MIT
#pragma once
#include "core/vr/Floor.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include <optional>
#include <string>
namespace mevr::ue3 { struct Object; }
// ME3's galaxy map over the CIC's projector (ME1's design, research/51 section 8.1; docs/galaxy-projector-handoff.md): ME3's
// adapter of the shared flow (game/GalaxyProjector). On foot in the CIC it measures the projector (game/Le3GalaxyProjector)
// and arms the room's retention (game/Le3GalaxyRetention: the CIC's art stays loaded through the map); with the map open
// (modes 11/12) it holds the view at Shepard's (the host's render-time swap), shows the game's map as a stereo window on the
// projector's disc and its Flash UI on the panel there. [Galaxy] hologram, inWorld and window (ME1's keys and defaults).
namespace mevr::game::le3_galaxy_capture {
using Publish = void (*)(vr::galaxy_holo::Scene const*);
// The Camera.UpdateCamera post-handler (before the ProcessEvent hook goes in).
void TryStartup();
void Tick(Publish publish, std::optional<vr::floor::State> const& floor);
void Shutdown(bool exiting);
// `mevr galaxy capture on|on-post|on-default|on-ui|off|state`: on (or on-raw) the captures without a post-process chain (the
// default: ME3's chains leave them black), on-post with the player's chain, on-default with the engine's DefaultPostProcess,
// on-ui with its DefaultUIScenePostProcess (diagnostics of the captures' post-process path); off stops the flow until `on`.
std::string Command(ue3::Object* pc, std::string const& action);
std::string StatusJson();
}
