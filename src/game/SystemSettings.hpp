// SPDX-License-Identifier: MIT
#pragma once

// The engine's SystemSettings the VR renderer needs, applied by the mod itself (research/35, 2026-10-23): BIOEngine.ini's
// [SystemSettings] wins over GamerSettings', the game rewrites that file (after every `scale set`, and a launch put the
// defaults back once), so a file edit can't be trusted. The mod runs `scale set <key> <value>` through the local
// player's console (game/ConsoleRequest) once a local player exists, and again whenever the section changes:
//   [SystemSettings] in ASI\<game>MEVR.ini, any key = value; apply = 0 turns it off.
// Built in unless the file says otherwise: DynamicShadows False (the scene renders twice a frame), MotionBlur False
// (the two eyes share one "previous frame", so the blur smeared one eye and not the other, every few seconds),
// FoliageDrawRadiusMultiplier 0.5 (the grass costs per instance, per eye; 0.25 pops in too close), and while the second
// eye is a replay ([Performance] sharePass 2, 3 or 5) LightEnvironmentShadows False (the character shadows: the right eye
// drew them where the left eye sees them, research/37, 2026-11-13). Log `system settings:`.
namespace mevr::game::system_settings {

// An installed, healthy native blur guard replaces menu/conversation-return and timed settling resets.
// Initial application, level applies and actual settings edits remain. Losing protection
// schedules the old workaround immediately (retried if the local player is not ready).
void Tick(float deltaSeconds, bool motionBlurProtected = false);

} // namespace mevr::game::system_settings
