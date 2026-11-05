// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The squad holograms' experiment (research/54 section 4, the owner 2026-11-04: the concept art's squad as holograms on a disc):
// console verbs that make a miniature copy of a party member in the level and turn it into a hologram with the game's own effect
// (BioPawn.SetEffectsMaterialType(EMT_HoloWipe), every character material's `CombinedEffect_EMT_HoloWipe` variant). A dev tool
// (names resolved by text, as game/GalaxyDump's): the omni-tool's hologram module is built on what these verbs show.
//   mevr holo spawn [self|n] [scale]   a copy of Shepard (self) or the n-th pawn nearest (1 = nearest squadmate), Actor.Spawn with
//                                      the pawn as its ActorTemplate, 1 m ahead of the player, collision and physics off
//   mevr holo effect <type> [fraction] SetEffectsMaterialType(type) and SetFractionOfEffectsMaterialEnabled (30 = EMT_HoloWipe)
//   mevr holo scale <s> | move <x> <y> <z> (units from the player) | info | clear
namespace mevr::game::holo_probe {

std::string Command(std::string const& rest);  // game thread (the console's tick)
void Shutdown();

} // namespace mevr::game::holo_probe
