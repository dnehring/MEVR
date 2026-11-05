// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The game's own UI art for the omni-tool's rail (research/56; the owner, 2026-11-04: drawn with "ME1's screens, exactly"): once
// per process, on the game thread, the mod copies the Scaleform movies the rail is drawn from (the font library, the main wheel,
// the journal's, codex's, inventory's and character record's movies) out of the game's memory into
// %LOCALAPPDATA%\MEVR\gameart\<Package.Movie>.gfx, where the host reads them (core/gfx: their shapes drawn, their fonts made
// TrueType). Packages not loaded yet are loaded through the engine's own Object.DynamicLoadObject. The player's own game data on
// their own PC: never shipped, never committed. A file is rewritten only when its size changed.
namespace mevr::game::game_art {

// Every mod tick (game thread); does its work once, a second into a level.
void Tick();
void Shutdown();
// For snapshots: what was cached.
std::string StatusJson();

} // namespace mevr::game::game_art
