// SPDX-License-Identifier: MIT
#pragma once

#include "game/ConversationWorld.hpp"

#include <string>
#include <string_view>

// ME3's slice of ME1's first-person conversations (docs/trilogy-plan.md T49; the owner chose it 2026-11-19 over upstream
// Halcyon's ConvoFirstPerson, which only moves the camera to Shepard's eyes at every shot). With `[Cinematics] conversations =
// world` (ME1's key and word; ME3 used to take it as the 3D screen), in ME3's conversation mode (7):
//   - The player's place: Shepard's eyes as the stage puts him on his mark, taken once the opening has settled; held for the
//     whole conversation (no jump at the director's cuts), followed only when his head stays more than worldFollowM away for
//     three ticks (a cut that moves him to another mark: vr::conversation_world::FollowHeadSettled, ME1's rule).
//   - The host (HostServices v33 setConversationWorld, plugin/WorldUi's conversation hold) swaps that place into the player
//     camera's POV around the main view's build only, keeps the world in stereo instead of the cinematic presentation
//     (MevrSeams::ConversationInWorld in ME3's me2_xr.cpp) and hides Shepard's head (MevrSeams::WorldViewHeld 1).
//   - Arm IK and the body under the player keep running in the held frame (view_anchor::Hold).
//   - People kept where they stand (ME1's worldStay): after the opening, a speaker the stage moves by more than 0.3 m and less
//     than 6 m in one camera update is put back with Actor.SetLocation before the frame is drawn; a placement the game repeats
//     at once is its own (ME1's cw::Insists) and stays.
// Not here yet (ME1 has them): looks at the player's eyes, walking to marks, the circle, walking during the conversation, the
// subtitle at the speaker and the wheel at the hand (the conversation's UI is on the HUD panel), moments. Game thread.
namespace mevr::game::sequel_conversation_world {

// The camera-update handler; after ue3::runtime::Up.
void TryStartup();
void Tick(conversation_world::Link const& link);
void Shutdown(conversation_world::Link const& link);
// `mevr convo world [status | on | off | auto]`: the setting for this session, and what the last conversation did.
std::string Console(std::string_view args);
// A conversation is shown in the world now.
bool InWorld();

} // namespace mevr::game::sequel_conversation_world
