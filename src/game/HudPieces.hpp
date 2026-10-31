// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/AimRay.hpp"
#include "core/vr/HudPieces.hpp"

#include <string>

// HUD pieces on the body (research/41; LE1, on foot in VR; [Hud] in ASI\LE1MEVR.ini): the mod's part is the gun. Each
// tick, from the gun's aim ray (game/hand_aim: the muzzle and barrel in tracking space) and the gun's top (game/sights),
// where the weapon panel's centre goes on the held gun (gunBackM behind the muzzle, gunUpM above the bore) goes to the
// host (HostServices v22, setHudPieces), which cuts the panel's part out of the HUD and shows it there
// (plugin/HalcyonOverlays). The wrist piece needs nothing from the mod (the host knows the left grip pose). Game thread.
namespace mevr::game::hud_pieces {

struct Link {
    // HostServices v22 setHudPieces; null: an older host (the pieces stay on the HUD panel).
    void (*publish)(vr::hud_pieces::Pieces const* pieces) = nullptr;
};

// After hand aim's ray (PublishAimRay), once a tick. `ray`: this tick's aim ray (invalid: no gun aimed along its barrel).
void Tick(vr::aim_ray::Ray const& ray, Link const& link);
// Snapshot JSON ("hudPieces").
std::string StatusJson();
// Heartbeat line.
void LogStats();
// Mod shutdown: the host's gun piece hidden.
void Shutdown(Link const& link);

} // namespace mevr::game::hud_pieces
