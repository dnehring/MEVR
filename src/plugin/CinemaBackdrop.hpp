// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The cinema screen's floor and horizon in the headset (research/52 "A floor and a horizon"; the owner, 2026-11-06: "we may need a
// background of some type to add a better sense of location so I don't get tunnel visioned and partially motion sick"; the
// floor and horizon, ideally from the game's own art): while the 3D cinema screen is up, a projection layer of its own under the
// screen's quads (MevrBridge::CinemaBackdropLayer, the seam in xr_session.cpp's screen branch), drawn by
// plugin/CinemaBackdropDraw: the floor at the player's real floor (the host's STAGE floor, else 1.6 m under the eyes where the
// sequence began), fixed in the room, the horizon all around, fading in over 0.6 s. Both eyes side by side in ONE swapchain
// (the runtime's swapchain budget, plugin/Swapchains). [Cinematics] backdrop (1; 0 off), backdropBrightness (1). Log `cinema
// backdrop:`; snapshots its "backdrop" layer and "cinemaBackdrop".
namespace mevr::host::cinema_backdrop {

// Snapshot JSON ("cinemaBackdrop").
std::string StatusJson();

} // namespace mevr::host::cinema_backdrop
