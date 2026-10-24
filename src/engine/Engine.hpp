// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

// The engine's world and object count, on our reflection layer (research/34, stage 3). LE2/LE3 (no
// layout yet) answer from engine/Legacy.
namespace mevr::engine {

// Game thread, once ue3::runtime::Init is ready (Mod.cpp's startup gate): finds the path to the world
// (the GameEngine, GamePlayers, Player.Actor, Actor.WorldInfo, Engine.World) and freezes it, so World()
// never resolves anything itself. False (logged once) if a piece is missing; World() is then null.
bool ResolveWorld();
// Drops it (the mod unloads, before ue3::runtime::Shutdown).
void ForgetWorld();

// The current world, as an identity to compare: a new one means a level load. The local player
// controller's WorldInfo's outer World (its level's world: what GWorld holds; the self-check's `global
// World` row compares the two), or that WorldInfo itself if its outers aren't a World; null while there's
// no player controller (a level loading) or before ResolveWorld. Only reads frozen offsets: any thread
// (BonePose's UpdateSkelPose hook calls it).
void const* World();

// How World() found it, for the self-check: "WorldInfo's outer World", "WorldInfo (no World outer)",
// "no player controller" or "not resolved".
char const* WorldHow();

// GObjects' count (0 while the layer is off), for the heartbeat.
std::int32_t ObjectCount();

} // namespace mevr::engine
