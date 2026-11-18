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
// ME1's rules around it (research/51 section 8.4, game/GalaxyMap): on foot at the projector the table is reserved, so the map's
// first frame is held; the map's opening and closing cinematics are held at Shepard's view; an interlude inside the map (a
// travel movie, a call, a question, a load) keeps the table reserved and re-opens it from the disc measured on foot; the
// GalaxyMap sequence's opening wait (its longest delay, 2.5 s in all three games) is shortened while the map opens in the CIC.
// [Galaxy] hologram, inWorld, window, windowScale, windowDepth, windowRound, windowBackdrop (ME1's keys and defaults). Each
// game's facts come from its adapter (game/Le2GalaxyCapture, game/Le3GalaxyCapture). Game thread.
namespace mevr::game::galaxy_projector::flow {

using Publish = void (*)(vr::galaxy_holo::Scene const*);

struct Adapter {
    char const* name = "galaxy projector";  // the log lines' prefix ("LE2 galaxy projector")
    // The CIC's art level loaded and visible in the player's world: the projector is measured only then. Null: always.
    bool (*roomVisible)(ue3::Object* pc) = nullptr;
    // The room kept loaded through the map (ME3: its art unloads on the map's entry; game/Le3GalaxyRetention): armed on foot near
    // the projector (empty: armed, else why not), then armed and active while the map is open, and kept through the map's
    // interludes (galaxy_projector::MapInterlude). Null: the map keeps the room (ME2).
    std::string (*armRetention)(ue3::Object* pc) = nullptr;
    bool (*retentionArmed)() = nullptr;
    bool (*retentionActive)() = nullptr;
    bool (*retentionKept)() = nullptr;
    // An armed retention still waiting for the map waits again from now (on foot at the projector, and through the map's
    // opening cinematic). Null: nothing to refresh.
    void (*refreshRetention)() = nullptr;
    // A reload with the map open went straight into the map's mode (ME3 loads a save written there, research/le3-galaxy.md):
    // the room's retention armed for a map already open (empty: armed, else why not). Null: no retention to arm.
    std::string (*armRetentionInMap)(ue3::Object* pc) = nullptr;
    // The map's own level, lower case (ME2 bioa_nor_103agalaxymap, ME3 bioa_nor_203agalaxymap): the console's use requests it,
    // so a cinematic then is the map's opening; the opening's probe times its load.
    char const* mapLevel = nullptr;
    // The engine's depth of field off near the projector (on foot, 3 s into gameplay, never during a load) and back away from
    // it (game/DepthOfFieldHold): ME2's map entry with it on faulted the rendering thread in the first held frame
    // (MassEffect2.exe+0x98C25D, gotchas/me2-scale-set-during-load's signature; research/le2-galaxy-table.md). The owner
    // accepted it (2026-11-17).
    bool depthOfFieldOff = false;
    // Not built (the owner, 2026-11-17: ray picking on both maps and ray aiming of ME2's scanner are the next lane's): a
    // controller ray's hit on the map's window, as a place on the game's map (u, v across the window's picture, 0..1 from its
    // top left, the captures' crop already taken out) and whether the trigger selects there. The flow will find the hit from
    // the scene it publishes (the disc's pose and the window's camera) and call this on the game thread; each game moves its
    // own cursor (ME2's and ME3's BioCameraBehaviorGalaxy.m_pCrossHairObject) and selects with its own call. False: refused.
    bool (*pointAt)(ue3::Object* pc, float u, float v, bool select) = nullptr;
};

// The Camera.UpdateCamera post-handler (before the ProcessEvent hook goes in). The adapter is copied.
void TryStartup(Adapter const& adapter);
void Tick(Publish publish, std::optional<vr::floor::State> const& floor);
void Shutdown(bool exiting);
// `mevr galaxy capture <action>`: on (or on-raw) the captures without a post-process chain (the default: ME3's chains leave
// them black, ME2 the same), on-post with the player's chain, on-default the engine's DefaultPostProcess, on-ui its
// DefaultUIScenePostProcess (diagnostics of the captures' post-process path); hold-only the hold and the panel without
// captures; off stops the flow until `on`; opening-keep / opening-cut the map's opening wait as the game has it / shortened
// (the default); ship the map's ship, camera, relay and selectable objects (read only, for galaxy-steps.ps1's steer); flares
// the lens flares nearest the projector; state.
std::string Command(ue3::Object* pc, std::string const& action);
std::string StatusJson();
// `mevr galaxy projector`: the projector as the flow measures it now, and the world's streaming levels (read only).
std::string ProjectorJson(ue3::Object* pc);
// The game's galaxy map mode is open: the map camera's centre object (BioCameraBehaviorGalaxy.m_pCenterObject of the galaxy
// game mode's GalaxyCam, ME2's and ME3's), for the window's convergence.
std::optional<ue3::Vector> MapCentre(ue3::Object* pc);

} // namespace mevr::game::galaxy_projector::flow
