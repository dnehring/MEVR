// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// Hair on simulated bone chains, every game (docs/trilogy-hair-plan.md section 4; research/trilogy-hair.md; the owner,
// 2026-11-19: on by default, Miranda first, hands and our VRIK arms move it). Each tick: the visible people within
// [Hair] rangeM of the player (nearest first, at most maxPeople; never the player's own pawn), their hair component
// (BioPawn.m_oHairMesh); a hairstyle with a rigged stand-in (engine/GameAdapter's kHairStandIns: ME3's Miranda) gets it by
// SetSkeletalMesh, put back when the feature goes off; the hair stage (engine/HairPose) simulates every hairstyle whose
// skeleton has hair bones, kept off the person's own body, pushed by the player's drawn hands and arms. A hairstyle without
// hair bones (ME2's Miranda until her re-rigged mesh exists) stays as the game has it. Settings `[Hair]` in the shared
// settings file (appended once): on, rangeM, maxPeople, gravity, stiffness, damping, inertia, maxBendDeg, radiusM, wind,
// hands, standIns. Logs `hair:`; snapshot `"mod"."hair"`; console `mevr hair [on|off|auto | people | trace <s> | kick <n>
// <x> <y> <z> | mesh <n> <path> | standins on|off]`. Game thread.
namespace mevr::game::hair {

// [Hair] on (or `mevr hair on`): the pose hook must be in.
bool Wanted();
void Tick(double deltaSeconds);
std::string Command(std::string const& args);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::hair
