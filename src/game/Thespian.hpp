// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Thespian.hpp"
#include "game/Voice.hpp"

#include <cstdint>
#include <string>

// Thespian mode (docs/thespian-mode-plan.md, research/70): the player speaks Shepard's lines. This module reads the live
// dialogue wheel as script cards (each reply's label and full line), matches what was said against them
// (core/voice/ScriptMatch), takes the reply through the game's own queue and skips Shepard's recording of it.
//
// The spoken turn (research/70 section 8 steps 3 to 6; `[Thespian]` in ASI\LE1MEVR.ini: on (default 0), strictness =
// relaxed | performer | method, endMs, pauseMs, handPick = voice | silent, keyterms, hideWheel): while a wheel is up the host's
// recognizer listens openly (no button, the game's audio not ducked), the wheel's rare words are its key terms, every
// update of the transcript is matched against the cards, the turn's rules (core/vr/Thespian) say when a reply is taken,
// and the panel (the rows, the lit words, the microphone's mark) goes to the host each tick; the game's own wheel is
// hidden meanwhile (its Flash's HideWheel) until the left stick moves, which brings it back for a pick by hand and folds the
// panel away (the microphone still listens). Off, or without a
// recognizer, or on a wheel whose text isn't English: nothing changes, the game's wheel works as ever. The console
// commands work whatever the setting.
//
// How the game indexes replies (read from the wheel handler's script, research/70 section 8 step 1): every reply function of
// BioConversation takes a POSITION in m_lstCurrentReplyIndices (0 .. count-1), not the reply's index in m_ReplyList. The
// investigate topics are in that list with category 5. A wheel pick ends in BioConversation.QueueReply(position), which is
// what this module calls.
namespace mevr::game::thespian {

struct Link {
    voice::Link voice;  // the host's recognizer (as game/Voice's)
    void (*setPanel)(vr::thespian::Panel const* panel) = nullptr;  // HostServices setThespian; null: an older host, no panel
};

// Once per game tick, after game/ConversationWorld's.
void Tick(double deltaSeconds, Link const& link);
// A wheel is up and Thespian mode listens: game/Voice leaves the recognizer's lines and key terms alone.
bool OwnsMicrophone();
// A listen Thespian mode opened (by its start on the host's clock): its late lines are never a squad order.
bool OwnsListen(std::uint64_t listenStartMs);
// `mevr thespian [cards | watch on|off | say <words> | pick <position> | on | off | auto | status | feed <wav>]`:
//   cards            the live wheel: every current reply's position, category, style, label and line
//   watch on|off     log the cards whenever a wheel comes up (for unattended runs)
//   say <words>      what a player would say: matched against the cards; a decided card is committed
//   pick <position>  commit that position without matching (the queue and the skip alone)
//   on | off | auto  the spoken mode for this session whatever the file says (auto: the file's again)
//   status           the mode, the recognizer, the turn's state and what was heard
//   feed <wav>       a WAV heard in the microphone's place by the open listen (a test without a voice)
//   stick <position> the card taken as the stick takes it (the handler's SelectConversationEntry)
//   wheel show|hide  the game's own wheel shown (as the left stick does while the panel stands) or hidden again
std::string Command(std::string const& rest);
std::string StatusJson();
// Lets go of what it holds (the silenced voice); the module may be unloaded after.
void Shutdown();

} // namespace mevr::game::thespian
