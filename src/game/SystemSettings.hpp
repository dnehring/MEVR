// SPDX-License-Identifier: MIT
#pragma once

// The engine's SystemSettings the VR renderer needs, applied by the mod itself (research/35, 2026-10-23): BIOEngine.ini's
// [SystemSettings] wins over GamerSettings', the game rewrites that file (after every `scale set`, and a launch put the
// defaults back once), so a file edit can't be trusted. The mod runs `scale set <key> <value>` through the local
// player's console (game/ConsoleRequest) once a local player exists, and again whenever the section changes:
//   [SystemSettings] in ASI\<game>MEVR.ini, any key = value; apply = 0 turns it off.
// Built in unless the file says otherwise: DynamicShadows False (the scene renders twice a frame), MotionBlur False
// (the two eyes share one "previous frame", so the blur smeared one eye and not the other, every few seconds),
// FoliageDrawRadiusMultiplier 0.5 (the grass costs per instance, per eye; 0.25 pops in too close). Log `system settings:`.
namespace mevr::game::system_settings {

void Tick(float deltaSeconds);

} // namespace mevr::game::system_settings
