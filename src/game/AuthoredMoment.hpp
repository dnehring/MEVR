// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

// LE1 authored moments (research/69 "Authored scenes: a treatment per kind of track"): the level's own script walks Shepard
// (a matinee's move track on the player's pawn: the walk to the council with Anderson), teleports him, shows a black screen
// or switches the cinematic mode on. This module watches those four things for the player and writes what each did, so a
// run of the sweep's moments (tools/conversation-sweep.py moments) says, per moment, how far a track moved him, how far a
// teleport took him and whether the place it led to is the same space (in sight, on a floor, within a walk), which game
// modes it passed through and whether his pawn was hidden: the facts the rules for those moments are decided by.
// `[Cinematics] worldMoments = log` (the default with the world mode) only measures; `off` does nothing. Game thread, except
// the move track's question, which the engine asks from inside its physics step and which reads two words of ours.
namespace mevr::game::authored_moment {

void Tick();
// A matinee's move track moved this actor within the last `withinMs` (an authored walk: Wrex pacing on Virmire).
bool TrackMoves(void const* actor, std::uint64_t withinMs = 300);
// When a move track last moved (or would have moved) the player's pawn, in GetTickCount64 milliseconds; 0 never.
std::uint64_t LastPlayerTrackMs();
// `mevr moment`: what is being watched and the last moment's numbers.
std::string Command(std::string const& rest);
void LogStats();
void Shutdown();

} // namespace mevr::game::authored_moment
