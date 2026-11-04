// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/GalaxyMap.hpp"

#include <cstdint>
#include <string>

// The galaxy map as a hologram we draw (research/51 section 8; the shared part: core/vr/GalaxyHologram). LE1, [Galaxy]
// hologram = 1. While the game is in its galaxy mode (9), each tick: the map's model from the newest live
// BioCameraBehaviorGalaxy (the level's selectable actors in m_pDynamicObjects, the centre, the selection, the player camera's
// view) and the GalaxyMap tables (relay routes, read once per opening), labels through BioSPGame.GetSimpleString (cached),
// sent to the host (HostServices v26 setGalaxyHologram), which draws it on a table. A pick from the host (takeGalaxyPick)
// moves the map's cross-hair actor onto the object (Actor.SetLocation): the camera behaviour's own tick selects the object
// nearest its cross-hair; once it has, a click (posted to the game's window) activates the selection as the game's own
// click does (zooms in, or asks to travel). Through our reflection layer; a missing piece stands it down with a
// `galaxy hologram:` line. Game thread.
namespace mevr::game::galaxy_map {

struct Link {
    // HostServices v26; null: an older host (the map stays on Halcyon's panel).
    void (*setScene)(vr::galaxy_holo::Scene const* scene) = nullptr;
    bool (*takePick)(std::uint32_t* id) = nullptr;
};

// Before the ProcessEvent hook goes in: the camera-update handler that holds the view in the CIC while the map is open in-world.
void TryStartup();
void Tick(Link const& link);
// Snapshot JSON ("galaxyMap").
std::string StatusJson();
// Mod shutdown: the host forgets the hologram; the window's captures are destroyed (unless the process is exiting).
void Shutdown(Link const& link, bool exiting);

// Research (`mevr galaxy view ...`, `mevr galaxy flash ...`): with the map open, the player camera orbits the map's centre
// object (yaw and pitch in degrees, the distance in units; "off" gives the view back), and the map's Flash movie shown or
// hidden, to see the map's 3D scene alone from the angles a table hologram shows it at.
std::string ViewOverride(bool on, float yawDeg, float pitchDeg, float distanceUnits);
std::string ShowFlash(bool visible);
std::string HoldWrites(bool on);

} // namespace mevr::game::galaxy_map
