// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>

// VR cover (research/23 section 6; the rule in core/vr/VrCover): the game's own cover protection for a VR player, who takes
// cover with their body because the game's cover snapping is refused (game/posture's noCover). LE1's damage code
// (BioPawnBehavior.ProxyTakeDamage) cuts a hit on the player by the difficulty's CoverDamageReduction (all of it on Casual
// and Normal, 90% above) when `IsInCover() && !IsInCoverLeaning()` and the shot comes from the cover's front
// (CoverNormal's cone) from more than 300 units. A VR player is never in the game's cover, so that never happened.
//
// Now, inside that damage call only, the mod answers its two questions for the player's pawn (engine/call_function's
// answer handlers, which see the damage call's arguments in its frame): it traces from the headset's eyes toward the
// shooter (the instigator's eyes, or the damage causer), and if a surface stands within [Cover] reachM of the eyes
// (the head hidden behind cover from that shooter), IsInCover answers true, IsInCoverLeaning false, and BioPawn.CoverNormal
// holds that surface's facing for the call (put back when it returns: journalled). The game's own code then applies its
// own cone, distance and amount. The head is the game's "leaning out": raised over the cover to shoot, it's in the open
// and the hit is whole. Nothing changes outside the damage call (no cover state, no camera, no animation).
//
// Settings: [Cover] in the mod's settings file (protect, reachM, log), its defaults appended when missing. Logs `vr cover:`;
// snapshot "vrCover" in the mod's status. Console: `mevr cover [test]`. Game thread. LE1 (LE2 and LE3: research/23
// section 6.3).
namespace mevr::game::vr_cover {

// Finds the script functions and registers the answer handlers (before the CallFunction hook goes in). False until the
// script packages have loaded (or not LE1).
bool TryStartup();
// Each mod tick: the settings, whether the player can be judged (VR on foot, the game's cover refused), the pawn.
void Tick();
std::string StatusJson();
void LogStats();
// `mevr cover` (the status) | `mevr cover test`: every awake hostile judged now, as a hit from it would be.
std::string Console(std::string_view args);
// Puts back a cover normal still held (it isn't, outside a damage call).
void Shutdown();

} // namespace mevr::game::vr_cover
