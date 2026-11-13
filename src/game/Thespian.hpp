// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// Thespian mode (docs/thespian-mode-plan.md, research/70): the player speaks Shepard's lines. This module reads the live
// dialogue wheel as script cards (each reply's label and full line), matches what was said against them
// (core/voice/ScriptMatch), takes the reply through the game's own queue and skips Shepard's recording of it.
//
// Built so far (research/70 section 8, steps 1 and 2): the cards, a commit by typed words or by position, the skip and the
// silence. No microphone and no panel yet: nothing happens unless a console command asks.
//
// How the game indexes replies (read from the wheel handler's script, research/70 section 8 step 1): every reply function of
// BioConversation takes a POSITION in m_lstCurrentReplyIndices (0 .. count-1), not the reply's index in m_ReplyList. The
// investigate topics are in that list with category 5. A wheel pick ends in BioConversation.QueueReply(position), which is
// what this module calls.
namespace mevr::game::thespian {

// Once per game tick, after game/ConversationWorld's.
void Tick(double deltaSeconds);
// `mevr thespian [cards | watch on|off | say <words> | pick <position>]`:
//   cards            the live wheel: every current reply's position, category, style, label and line
//   watch on|off     log the cards whenever a wheel comes up (for unattended runs)
//   say <words>      what a player would say: matched against the cards; a decided card is committed
//   pick <position>  commit that position without matching (the queue and the skip alone)
std::string Command(std::string const& rest);
std::string StatusJson();
// Lets go of what it holds (the silenced voice); the module may be unloaded after.
void Shutdown();

} // namespace mevr::game::thespian
