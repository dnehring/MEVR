// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/GalaxyHologram.hpp"
#include "game/GalaxyProjector.hpp"

#include <string>

namespace mevr::ue3 { struct Object; }

// The game's galaxy map picked with the right controller's ray on a sequel (lane GXR, 2026-11-18; the owner, 2026-11-17: "the ray
// picks on both sequels' maps (ME1's way; the stick stays as the game's), the ray aims ME2's scanner"). Part of the sequels' flow
// (game/GalaxyProjectorFlow): while the map is held on the CIC's projector, the ray's point on the map's UI panel (the game's
// whole screen, core/vr/GalaxyRay) is
//   - in the map's galaxy, cluster and system views (ME2 9, ME3 11): the map's object nearest it on the game's screen (put there
//     through the map camera) is hovered (a tick in the hand), and the right trigger picks it as the game's own controls would:
//     the map's cursor put on it (BioCameraBehaviorGalaxy.m_pCrossHairObject, ME1's pick) and, once the game has selected it,
//     the map's A (its GUI event: ME2 12, ME3 15), so the game zooms, travels or asks itself;
//   - in ME2's orbit (mode 10): where the ray meets the planet is where the scanner's reticle goes (SFXGameModeOrbital.ReticleRot,
//     in the reticle effect's owner's frame), the game's own LT scans and RT launches a probe there.
// The game's stick, A and X keep working as before. [Galaxy] rayPick, rayScan (1, 1). Game thread.
namespace mevr::game::galaxy_projector::ray {

struct Frame {
    ue3::Object* pc = nullptr;
    ue3::Object* camera = nullptr;  // the galaxy mode's BioCameraBehaviorGalaxy
    int gameMode = -1;
    bool orbit = false;             // ME2's orbit (mode 10)
    vr::galaxy_holo::Scene const* scene = nullptr;  // the scene the flow publishes this tick (the disc, the window)
    MapPov pov;                     // the map camera's
    int clientW = 0, clientH = 0;   // the game window's client size (the screen's aspect)
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
};

// While the map is held on the projector, after the window's step.
void Tick(Frame const& frame);
// The map's hold ended (closed, an interlude, the flow off): a pick in progress dropped.
void Stop(char const* why);
// `mevr galaxy ray [state | pick on|off | scan on|off | mouse on|off | orbit]`.
std::string Command(Frame const& frame, std::string const& args);
std::string StatusJson();

} // namespace mevr::game::galaxy_projector::ray
