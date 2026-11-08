// SPDX-License-Identifier: MIT
#pragma once

#include "ue3/Types.hpp"

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>

// Shepard's omni-tool, the game's own: BIOG_V_T_Omni_Tool_Z.Prefab.OmniTool_2PieceLoop, the orange holographic gauntlet
// (OmniArm_Loop, the OmniArm mesh) and the hand's display (OmniHand_Loop) with its sound, on the player's pawn while a game
// screen is projected from the wrist (the bypass minigame, research/53 section 6: Mass Effect's look). Made by the game's own
// BioVisualEffect.CreateCrustEffect, as the equipment screen's BioUIWorld.AttachOmnitool puts it on its model. LE1, game thread.
namespace mevr::game::omni_tool {

// Each tick: on while `wanted` (or Request was called since the last Tick), gone (faded by its own lifetime) when not.
void Tick(bool wanted);
// Another module wants it this tick (the power wheel at the left hand, research/55): the next Tick shows it whatever its
// `wanted` says. Call it each tick it's wanted, before the world screens' tick (which ticks this one).
void Request();
std::uint64_t OnMs();
// `mevr omnitool on [lifetime s] | off [fade s] | state`: by hand, for tests.
std::string Console(std::string_view args);
// Where the omni-tool's display (the hand piece, OmniHand_Loop) is in the world as Shepard's arm shows it (its particle
// system's transform); nullopt while it isn't on the arm. The host projects the wrist screens from it.
std::optional<ue3::Vector> DisplayWorld();
// Snapshot JSON ("omniTool").
std::string StatusJson();
// Mod shutdown: the effect fades.
void Shutdown();

} // namespace mevr::game::omni_tool
