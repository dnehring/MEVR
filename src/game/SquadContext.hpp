// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadContext.hpp"

#include <string>
#include <string_view>
#include <vector>

// The squad's world fed from the game (research/54 "The representation"): every `refreshS` on foot, the Scene for
// `core/vr/SquadContext` is read on the game thread (the squadmates as the squad menu knows them, their powers and
// cooldowns from the pawn behaviour's power list, the living hostiles from the pawn list with a type word from their
// actor type, the gun's sight-line hit and the gaze selection), the enemy groups are updated, and every tick a pose
// sample (the head's line of sight, both controllers' aim rays, in the world) goes into the 4 s history, so a spoken
// "those" or a pointed order can be resolved at its own time. `[SquadContext]` in ASI\LE1MEVR.ini. Nothing here acts
// on the game: the block is what a rule parser or a model reads, and `mevr context [word]` prints it.
namespace mevr::game::squad_context {

void Tick(double deltaSeconds);

// The latest scene and groups (empty while off foot or unreadable).
vr::squad_context::Scene const& Scene();
std::vector<vr::squad_context::Group> const& Groups();
// What the head and the hands pointed at `secondsAgo` (the word's time), best first.
std::vector<vr::squad_context::Candidate> ResolveAgo(double secondsAgo);
// The text block for now: `said` as the SAY line, and with `word` a PT1 line resolved `secondsAgo`.
std::string Block(std::string_view said, std::string_view word = {}, double secondsAgo = 0.0);
// The roster the game gave so far: the squadmates seen this session with their names, weapons and powers as named.
std::vector<vr::squad_context::RosterEntry> Roster();
// The prefix from that roster.
std::string Prefix();

// The roster letter of a squadmate's class id (the squad menu's ids: 6 Liara L, 7 Wrex W, 8 Garrus G, 9 Tali T, 11 Ashley A,
// 12 Kaidan K); 0 for any other.
char LetterOf(int classId);

std::string StatusJson();
void LogStats();
void Shutdown();

}  // namespace mevr::game::squad_context
