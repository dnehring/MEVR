// SPDX-License-Identifier: MIT
#pragma once

#include "game/ConversationWorld.hpp"

#include <string>
#include <string_view>

// The sequels' first-person conversations, ME1's rules on ME2 and ME3 (docs/trilogy-plan.md T49; the owner chose ME1's
// 2026-11-19 over upstream Halcyon's ConvoFirstPerson, which only moves the camera to Shepard's eyes at every shot; ME2 since
// lane FPC, research/le2-conversations-in-world.md). One code path; each game's reader of the live conversation (ME2's
// BioConversation, ME3's BioConversationController) at the top of the .cpp. With `[Cinematics] conversations = world` (ME1's
// key and word), in the conversation mode (ME2 5, ME3 7):
//   - The director's near plane is kept off the player's view (ME1's worldNearPlane).
//   - The opening, ME1's rule (cw::DecideOpening, worldOpening auto): the stage's mark for Shepard against where the player
//     stood on foot. Near, or within a walk in the same space: Shepard is put back where the player stood and kept there for
//     the whole conversation (his walk to the mark withheld: the player is never moved by the game, and his head bone never
//     moves the view); in another space (a scripted start, a mark past 40 m or another deck): the place is his mark.
//   - The left stick walks Shepard and the view along the head's facing within 4 m of where the conversation began, never
//     into the speakers (ME1's worldMove: cw::StepPlayer, cw::KeepClear); the right stick steers the dialogue wheel then
//     (plugin/PadMerge on ME3).
//   - The host (HostServices v33 setConversationWorld, plugin/WorldUi's conversation hold) swaps that place into the player
//     camera's POV around the main view's build only, keeps the world in stereo instead of the cinematic presentation
//     (MevrSeams::ConversationInWorld in ME3's me2_xr.cpp) and hides Shepard's head (MevrSeams::WorldViewHeld 1).
//   - Arm IK and the body under the player keep running in the held frame (view_anchor::Hold).
//   - People kept where they stand (ME1's worldStay): after the opening, a speaker the stage moves by more than 0.3 m and less
//     than 6 m in one camera update is put back with Actor.SetLocation before the frame is drawn; a placement the game repeats
//     at once is its own (ME1's cw::Insists) and stays.
// Not here yet (ME1 has them): looks at the player's eyes, people walking to marks, the circle, walls and floors for the stick's walk, the
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
