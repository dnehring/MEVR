// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/GalaxyMap.hpp"

#include <string>

// The galaxy map as a hologram (research/51 stage B; what's shared with the host: core/vr/GalaxyMap.hpp). LE1, [Galaxy]
// hologram = 1 (the default). While the game is in its galaxy mode (9), each tick: the map's camera (the player camera's
// CameraCache.POV) against the map's centre (BioCameraBehaviorGalaxy.m_pCenterObject, else its m_vZoomLocation) gives the
// units per metre that show the centre [Galaxy] distanceM ahead; published to the host (HostServices v25, setGalaxyMap),
// which renders the map in stereo at that scale instead of on Halcyon's flat panel, and points the game's mouse from the
// controllers. Through our reflection layer; a missing piece stands it down with a `galaxy map:` line. Game thread.
namespace mevr::game::galaxy_map {

struct Link {
    // HostServices v25 setGalaxyMap; null: an older host (the map stays on Halcyon's panel).
    void (*publish)(vr::galaxy_map::View const* view) = nullptr;
};

void Tick(Link const& link);
// Snapshot JSON ("galaxyMap").
std::string StatusJson();
// Mod shutdown: the host forgets the hologram.
void Shutdown(Link const& link);

} // namespace mevr::game::galaxy_map
