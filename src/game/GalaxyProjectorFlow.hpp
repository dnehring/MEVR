// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Floor.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "ue3/Types.hpp"

#include <optional>
#include <string>

namespace mevr::ue3 { struct Object; }

// The game's own galaxy map open over the CIC's projector on a sequel (ME1's design, research/51 section 8.1;
// docs/galaxy-projector-handoff.md), one flow for every sequel. On foot in the CIC it measures the projector (the InterpActor
// tagged GalaxyMapProxy with BIOA_GalaxyMap_S.GalaxyMapProxy, the same actor in ME2's BioA_Nor_110 and ME3's BioA_Nor_210CIC)
// every 2 s and remembers Shepard's view; with the map open (the game's map modes: ME2 9/10, ME3 11/12) it holds the view at
// Shepard's (the host's render-time swap, plugin/GalaxyHologram), shows the game's map as a stereo window on the projector's
// disc (game/GalaxyProjector's StereoWindow) and its Flash UI on the panel there, and hides the room's own hologram meanwhile.
// [Galaxy] hologram, inWorld, window, windowScale, windowDepth, windowRound, windowBackdrop (ME1's keys and defaults). Each
// game's facts come from its adapter (game/Le2GalaxyCapture; ME3's game/Le3GalaxyCapture moves onto this flow). Game thread.
namespace mevr::game::galaxy_projector::flow {

using Publish = void (*)(vr::galaxy_holo::Scene const*);

struct Adapter {
    char const* name = "galaxy projector";  // the log lines' prefix ("LE2 galaxy projector")
    // The CIC's art level loaded and visible in the player's world: the projector is measured only then. Null: always.
    bool (*roomVisible)(ue3::Object* pc) = nullptr;
    // The room kept loaded through the map (ME3: its art unloads on the map's entry; game/Le3GalaxyRetention): armed on foot near
    // the projector (empty: armed, else why not), then armed and active while the map is open. Null: the map keeps the room.
    std::string (*armRetention)(ue3::Object* pc) = nullptr;
    bool (*retentionArmed)() = nullptr;
    bool (*retentionActive)() = nullptr;
};

// The Camera.UpdateCamera post-handler (before the ProcessEvent hook goes in). The adapter is copied.
void TryStartup(Adapter const& adapter);
void Tick(Publish publish, std::optional<vr::floor::State> const& floor);
void Shutdown(bool exiting);
// `mevr galaxy capture on|on-post|on-default|on-ui|off|state`: on (or on-raw) the captures without a post-process chain (the
// default, as ME3's), on-post with the player's chain, on-default the engine's DefaultPostProcess, on-ui its
// DefaultUIScenePostProcess (diagnostics of the captures' post-process path); off stops the flow until `on`.
std::string Command(ue3::Object* pc, std::string const& action);
std::string StatusJson();
// `mevr galaxy projector`: the projector as the flow measures it now, and the world's streaming levels (read only).
std::string ProjectorJson(ue3::Object* pc);
// The game's galaxy map mode is open: the map camera's centre object (BioCameraBehaviorGalaxy.m_pCenterObject of the galaxy
// game mode's GalaxyCam, ME2's and ME3's), for the window's convergence.
std::optional<ue3::Vector> MapCentre(ue3::Object* pc);

} // namespace mevr::game::galaxy_projector::flow
