// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Spectator.hpp"

#include <string>

// The spectator camera (research/49; what's shared with the host: core/vr/Spectator.hpp). LE1, [Spectator] enabled = 1
// (off by default; off, nothing is spawned or rendered).
//
// A SceneCapture2DActor (Actor.Spawn on the player controller, as game/ScopeCapture) with a 16:9 TextureRenderTarget2D of
// its own, the player's post-process chain, pointed each tick (SetView, then Actor.ForceUpdateComponents so the pose goes
// out before the next Draw) at the camera's pose:
//   head  the head pose the view was built for (view_anchor's frame), through a critically damped follow (smoothS);
//   free  a pose set from the console: `mevr spectator at X Y Z YAW PITCH` (world units, degrees) or `mevr spectator
//         follow RIGHT FORWARD UP [LOOKUP]` (metres from the pawn, in its heading; it looks at the pawn LOOKUP above
//         its origin), so a replayed session can be filmed from outside.
// Without a head-tracked view (menus, conversations, cutscenes) it films the game's own camera (CameraCache.POV), with
// no layers. The view actor (research/49): the capture actor is owned by the pawn (viewActor), and for the head camera
// the pawn's head meshes are hidden (hideHead: the eyes never see them anyway) since a capture's view doesn't hide the
// pawn's bOwnerNoSee parts. Everything through our reflection layer; a missing piece stands it down with a `spectator:`
// line. Game thread.
namespace mevr::game::spectator {

struct Link {
    // HostServices v24 setSpectator; null: an older host (the mod then never spawns anything).
    void (*publish)(vr::spectator::Frame const* frame) = nullptr;
};

void Tick(float deltaSeconds, Link const& link);
// `mevr spectator ...` (game/ConsoleRequest): what it did, for console.result.
std::string Command(std::string const& rest);
// Snapshot JSON ("spectator").
std::string StatusJson();
// Heartbeat line (while it exists).
void LogStats();
// Mod shutdown: puts the head back, disables and destroys the capture unless the process is exiting.
void Shutdown(bool exiting);

} // namespace mevr::game::spectator
