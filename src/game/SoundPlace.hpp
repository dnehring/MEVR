// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// Where sounds come from on foot (research/69 "What remains", A1 and A2; the rule in core/vr/SoundPlace, the hook
// engine/VoicePlace). Game mode 0, the player's view his own (not a conversation or a scripted moment held in the world):
//   - People talking near the player (`[Audio] overheard = 1`, the default): each tick the pawns within reach are named with
//     their head places under the conversation's own voice rule (within `overheardWithinM` 8 m, kept to `overheardKeepM` 10;
//     our loudness for the distance: full within `overheardNearM` 2, `overheardFalloffDb` 3 a doubling, at most
//     `overheardFloorDb` 6; `overheardGain` on top; `overheardMax` 16 at once, the speaking first). The hook places only the
//     sources the game left in the centre (a dialogue cue without an attenuation node), so the game's own placed sounds of
//     theirs (barks with a falloff, footsteps) keep its mix. Never the player's own pawn.
//   - The player's own gun (`[Audio] gun = 1`, the default): the held weapon actor (SFXGame.BioWeaponRanged) is named at its
//     drawn muzzle, so every source of the weapon's own audio component comes from the gun in the hand. Which actor owns the
//     gunfire's source (the weapon or Shepard's pawn) is [verify]: `mevr voices census` says (research/69 A2).
// Game thread.
namespace mevr::game::sound_place {

// The sound source hook (engine/VoicePlace) is wanted: either list is on in the settings file.
bool WantsHook();
void Tick(double deltaSeconds);
// `mevr voices [status | census [seconds] | overheard on|off | gun on|off]` (the last two for this session).
std::string Command(std::string const& rest);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::sound_place
