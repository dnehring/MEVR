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
//     into the speakers (ME1's worldMove: cw::StepPlayer, cw::KeepClear), only while the dialogue wheel is down (it steers
//     the wheel when it is up, latched after a pick: cw::StickWalks); the right stick turns about the drawn eyes ([Comfort]
//     turn). ME1's sticks, the owner's answer of 2026-11-19 (research/le3-conversations-in-world.md section 9).
//   - The host (HostServices v33 setConversationWorld, plugin/WorldUi's conversation hold) swaps that place into the player
//     camera's POV around the main view's build only, keeps the world in stereo instead of the cinematic presentation
//     (MevrSeams::ConversationInWorld in ME3's me2_xr.cpp) and hides Shepard's head (MevrSeams::WorldViewHeld 1).
//   - Arm IK and the body under the player keep running in the held frame (view_anchor::Hold).
//   - People kept where they stand (ME1's worldStay, worldStaySource): after the opening, the stage's placements of Shepard and
//     of the conversation's people are withheld at the source (engine/ConversationHooks: ME3's controller's SetPositionRotator;
//     ME2's placement function isn't pinned yet), the facing still applied, and so are a move track's moves of Shepard (ME1's
//     trackHeld; both sequels); someone else a track moves walks as authored. After the fact, as the fallback: a speaker moved
//     by more than 0.3 m and less than 6 m in one camera update by anything else is put back with Actor.SetLocation before the
//     frame is drawn (a placement the game repeats at once is its own, cw::Insists), and Shepard is kept on the player's place
//     (ME2's gesture walks' root motion is what remains there [verify]).
//   - Conversations with no place for the player (the generated presence table, cw::NoPlaceForPlayer) stay on the 3D screen.
//   - Moments (ME1's worldMoments = view): a level cutscene whose move track walks Shepard is held in first person from the
//     player's place (his pawn hidden while apart, the body the player's), joined at its end under a dip; the floor marker
//     (cw::StepMarker) where his walk ends (research/le2-conversations-in-world.md section 6).
//   - The eyes (ME1's opening and StepEyeHeight): a Stay or WalkIn keeps the player's eyes and facing on foot; at his mark the eyes
//     are where first person has them from his head bone (learned on foot, cw::NoteEyeFromHead); their height is the player's
//     over his floor (cw::EyeHeightStep).
// Not here yet (ME1 has them): looks at the player's eyes, people walking to marks, the circle, walls and floors for the stick's
// walk, the subtitle at the speaker and the wheel at the hand (the conversation's UI is on the HUD panel), walk-along, the game's
// black moving the player, rides, seats. Game thread.
namespace mevr::game::sequel_conversation_world {

// The camera-update handler; after ue3::runtime::Up.
void TryStartup();
void Tick(conversation_world::Link const& link);
void Shutdown(conversation_world::Link const& link);
// `mevr convo world [status | on | off | auto]`: the setting for this session, and what the last conversation did.
std::string Console(std::string_view args);
// A conversation is shown in the world now.
bool InWorld();
// The world mode is wanted: engine/ConversationHooks goes in (Mod.cpp), and this module answers its questions.
bool WantsHooks();
// A moment's first-person view is up (a level cutscene a matinee walks Shepard in; ME1's conversation_world::MomentViewOn).
bool MomentViewOn();
// The body under the player in a conversation shown in the world or a moment (ME1's conversation_world::BodyAway, which on the
// sequels answers with this: arm IK and the body follower).
conversation_world::BodyAwayState BodyAway();

} // namespace mevr::game::sequel_conversation_world
