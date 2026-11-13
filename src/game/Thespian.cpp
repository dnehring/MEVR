// SPDX-License-Identifier: MIT
#include "game/Thespian.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/voice/ScriptMatch.hpp"
#include "engine/VoicePlace.hpp"
#include "game/ConversationWorld.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mevr::game::thespian {

namespace {

namespace sm = voice::script_match;
namespace vp = engine::voice_place;

// EReplyCategory and EConvGUIStyles values the wheel handler's script tests (research/70 section 8 step 1).
constexpr int kCategoryInvestigate = 5;
constexpr int kStyleIllegal = 4;  // a reply the player can't take (a charm or intimidate without the skill): the game refuses it
constexpr int kMaxCards = 32;

// The live conversation's replies. Every Get* function takes a position in m_lstCurrentReplyIndices.
ue3::FieldRef gCurrentReplies{"SFXGame.BioConversation", "m_lstCurrentReplyIndices"};  // TArray<int>: indices into m_ReplyList
ue3::FieldRef gReplyList{"SFXGame.BioConversation", "m_ReplyList"};
ue3::FieldRef gReplyUnskippable{gReplyList, "bUnskippable"};
ue3::FunctionRef gGetReplyText{"SFXGame.BioConversation.GetReplyText"};                  // (int nIndex, bool bParse) -> string
ue3::FunctionRef gGetReplyParaphrase{"SFXGame.BioConversation.GetReplyParaphraseText"};  // (int nIndex) -> string
ue3::FunctionRef gGetReplyCategory{"SFXGame.BioConversation.GetReplyCategory"};          // (int nIndex) -> int
ue3::FunctionRef gGetReplyGuiStyle{"SFXGame.BioConversation.GetReplyGUIStyle"};          // (int nIndex) -> byte
ue3::FunctionRef gQueueReply{"SFXGame.BioConversation.QueueReply"};                      // (int nReply) -> bool: what a wheel pick ends in
ue3::FunctionRef gSkipNode{"SFXGame.BioConversation.SkipNode"};                          // () -> bool: the player's skip of the current line
ue3::FunctionRef gEndCurrentConversation{"SFXGame.BioWorldInfo.EndCurrentConversation"}; // what the handler calls when QueueReply fails
// The conversation's state, for the skip and the trace after a commit.
ue3::FieldRef gSpeaker{"SFXGame.BioConversation", "m_pSpeaker"};
ue3::FieldRef gConvPlayer{"SFXGame.BioConversation", "m_pConversationPlayer"};
ue3::FieldRef gCurrentEntry{"SFXGame.BioConversation", "m_nCurrentEntry"};
ue3::FieldRef gSelectedReply{"SFXGame.BioConversation", "m_nSelectedReply"};
ue3::FieldRef gDelayedReply{"SFXGame.BioConversation", "m_nDelayedReplyChoice"};
ue3::FieldRef gRemainingVo{"SFXGame.BioConversation", "m_fRemainingVOLength"};
ue3::FieldRef gShowRepliesOffset{"SFXGame.BioConversation", "m_fShowRepliesOffset"};
// The wheel's handler.
ue3::FieldRef gDisplayingWheel{"SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel"};
ue3::FieldRef gInvestigateShown{"SFXGame.BioSFHandler_Conversation", "m_bDisplayInvestigateSubMenu"};
ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo"};

struct Card {
    int position = 0;   // in m_lstCurrentReplyIndices: what every reply function and QueueReply take
    int reply = -1;     // the element's value: the reply's index in m_ReplyList
    int category = 0;
    int style = 0;
    std::string label;
    std::string line;
    bool unskippable = false;
    bool candidate = true;   // may be picked: not unavailable, not shadowed by an earlier reply of its category
    char const* why = "";    // why not a candidate
};

struct Live {
    ue3::Object* conversation = nullptr;
    ue3::Object* handler = nullptr;
    bool wheelUp = false;
    std::vector<Card> cards;
};

// A commit under way: the reply was queued; its line is skipped when it starts, and Shepard's voice is silent meanwhile.
struct Pending {
    bool active = false;
    ue3::Object* conversation = nullptr;
    int position = -1;
    int reply = -1;
    bool unskippable = false;
    bool skipped = false;
    bool playerSpoke = false;       // the conversation's speaker was the player since the commit
    bool muted = false;
    std::uint64_t atMs = 0;
    std::string trace;              // the last state logged (a line per change)
    int traceLines = 0;
};

Pending gPending;
bool gWatch = false;
bool gWheelWasUp = false;
unsigned gWheels = 0;
unsigned gCommits = 0;
unsigned gSkips = 0;
unsigned gRefused = 0;
std::string gLast = "nothing yet";

std::string CallText(ue3::Object* conversation, ue3::FunctionRef& function, int position, bool withParse) {
    ue3::Function* const f = function.Get();
    if (f == nullptr) {
        return {};
    }
    ue3::Params parms(ue3::runtime::Get(), f);
    parms.Set("nIndex", static_cast<std::int32_t>(position));
    if (withParse) {
        parms.SetBool("bParse", true);  // tokens in the text resolved, as the subtitle shows it
    }
    if (!ue3::runtime::Call(conversation, f, parms)) {
        return {};
    }
    ue3::FString returned = parms.Return<ue3::FString>().value_or(ue3::FString{});
    std::string text = ue3::memory::Narrow(returned);
    ue3::memory::FreeString(returned);
    return text;
}

template <class T>
std::optional<T> CallNumber(ue3::Object* conversation, ue3::FunctionRef& function, int position) {
    ue3::Function* const f = function.Get();
    if (f == nullptr) {
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), f);
    parms.Set("nIndex", static_cast<std::int32_t>(position));
    if (!ue3::runtime::Call(conversation, f, parms)) {
        return std::nullopt;
    }
    return parms.Return<T>();
}

// The live wheel as cards, or nullopt without a conversation. The reply functions are only called for positions the
// current list has (a call for a reply that isn't there read an empty list inside the script and ended the game once,
// research/59).
std::optional<Live> ReadLive() {
    Live live;
    live.conversation = conversation_world::LiveConversation();
    if (live.conversation == nullptr) {
        return std::nullopt;
    }
    live.handler = conversation_world::LiveWheelHandler();
    live.wheelUp = live.handler != nullptr && gDisplayingWheel.ReadBool(live.handler).value_or(false);
    auto const* const current = gCurrentReplies.In<ue3::TArray<std::int32_t>>(live.conversation);
    if (current == nullptr || current->data == nullptr || current->count <= 0 || current->count > kMaxCards) {
        return live;
    }
    auto const replies = gReplyList.ElementsOf(live.conversation);
    bool categorySeen[16] = {};
    for (std::int32_t i = 0; i < current->count; ++i) {
        Card card;
        card.position = i;
        card.reply = current->data[i];
        card.category = CallNumber<std::int32_t>(live.conversation, gGetReplyCategory, i).value_or(0);
        card.style = CallNumber<std::uint8_t>(live.conversation, gGetReplyGuiStyle, i).value_or(0);
        card.label = CallText(live.conversation, gGetReplyParaphrase, i, false);
        card.line = CallText(live.conversation, gGetReplyText, i, true);
        if (replies) {
            if (void* const node = replies->At(card.reply)) {
                card.unskippable = gReplyUnskippable.ReadBool(node).value_or(false);
            }
        }
        if (card.style == kStyleIllegal) {
            card.candidate = false;
            card.why = "unavailable (the game refuses it)";
        } else if (card.category != kCategoryInvestigate && card.category >= 0 && card.category < 16) {
            // The wheel shows one reply per place and takes the first of a category: a later one can't be picked there.
            if (categorySeen[card.category]) {
                card.candidate = false;
                card.why = "shadowed (an earlier reply has its place on the wheel)";
            }
            categorySeen[card.category] = true;
        }
        if (card.candidate && sm::Tokens(card.label).empty() && sm::Tokens(card.line).empty()) {
            card.candidate = false;
            card.why = "nothing to say (no label, no line)";
        }
        live.cards.push_back(std::move(card));
    }
    return live;
}

std::string Describe(Live const& live) {
    std::string out = std::format("thespian: wheel {}, {} replies", live.wheelUp ? "up" : "not up", live.cards.size());
    if (live.conversation != nullptr) {
        out += std::format("; show-replies offset {:.2f} s, the current line's recording has {:.2f} s left; investigate sub-menu {}",
            gShowRepliesOffset.Read<float>(live.conversation).value_or(-1.0f), gRemainingVo.Read<float>(live.conversation).value_or(-1.0f),
            live.handler != nullptr && gInvestigateShown.ReadBool(live.handler).value_or(false) ? "shown" : "not shown");
    }
    for (Card const& c : live.cards) {
        out += std::format("\n  [{}] reply {} category {} style {}{}{}: \"{}\" -> \"{}\"", c.position, c.reply, c.category, c.style,
            c.unskippable ? " unskippable" : "", c.candidate ? "" : std::format(" NOT A CARD: {}", c.why), c.label, c.line);
    }
    return out;
}

void ClearMute() {
    if (gPending.muted) {
        vp::Set(vp::Publisher::Thespian, nullptr, 0);
        gPending.muted = false;
    }
}

// Takes a reply as a wheel pick does (BioSFHandler_Conversation.SelectConversationEntry, read 2026-11-12): only while
// the wheel is up, never an unavailable reply; QueueReply(position); the investigate sub-menu closed; the conversation
// ended if the queue refuses, as the handler does.
std::string Commit(Live const& live, int position, std::string_view how) {
    if (!live.wheelUp) {
        return "thespian: no commit: the wheel isn't up";
    }
    auto const it = std::find_if(live.cards.begin(), live.cards.end(), [&](Card const& c) { return c.position == position; });
    if (it == live.cards.end()) {
        return std::format("thespian: no commit: no reply at position {}", position);
    }
    if (!it->candidate) {
        return std::format("thespian: no commit: position {} is {}", position, it->why);
    }
    ue3::Function* const queue = gQueueReply.Get();
    if (queue == nullptr) {
        return "thespian: no commit: BioConversation.QueueReply wasn't found";
    }
    ue3::Params parms(ue3::runtime::Get(), queue);
    parms.Set("nReply", static_cast<std::int32_t>(position));
    bool const called = ue3::runtime::Call(live.conversation, queue, parms);
    bool const queued = called && parms.ReturnBool().value_or(false);
    if (live.handler != nullptr && gInvestigateShown.ReadBool(live.handler).value_or(false)) {
        if (ue3::PropertyInfo const* const info = gInvestigateShown.Get(); info != nullptr && gInvestigateShown.WriteBool(live.handler, false)) {
            write_journal::Wrote("Thespian.investigateSubMenu", reinterpret_cast<std::uint8_t*>(live.handler) + info->offset, 4);
        }
    }
    ++gCommits;
    if (!queued) {
        // The handler's own answer to a refused queue.
        ue3::Object* const controller = player::Controller();
        ue3::Object* const info = controller != nullptr ? gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
        ue3::Function* const end = gEndCurrentConversation.Get();
        bool ended = false;
        if (info != nullptr && end != nullptr) {
            ue3::Params none(ue3::runtime::Get(), end);
            ended = ue3::runtime::Call(info, end, none);
        }
        gLast = std::format("thespian: {} -> position {} \"{}\": QueueReply {} (the conversation {})", how, position, it->label,
            called ? "returned false" : "couldn't be called", ended ? "was ended, as the wheel does" : "could not be ended");
        log::Warn("{}", gLast);
        return gLast;
    }
    gPending = {};
    gPending.active = true;
    gPending.conversation = live.conversation;
    gPending.position = position;
    gPending.reply = it->reply;
    gPending.unskippable = it->unskippable;
    gPending.atMs = GetTickCount64();
    gLast = std::format("thespian: {} -> position {} (reply {}) \"{}\" queued{}", how, position, it->reply, it->label,
        it->unskippable ? "; its line is unskippable and plays" : "; its line is skipped when it starts");
    log::Info("{}", gLast);
    return gLast;
}

std::string Say(Live const& live, std::string const& words) {
    std::vector<sm::Card> cards;
    std::vector<int> positions;
    for (Card const& c : live.cards) {
        if (c.candidate) {
            cards.push_back({c.label, c.line});
            positions.push_back(c.position);
        }
    }
    if (cards.empty()) {
        return "thespian: nothing to match: no cards";
    }
    sm::Wheel const wheel(cards);
    sm::Result const r = wheel.Match(words, true);
    std::string scores;
    for (std::size_t i = 0; i < r.cards.size(); ++i) {
        scores += std::format("{}[{}] {:.2f}{}", scores.empty() ? "" : ", ", positions[i], r.cards[i].score, r.cards[i].byLabel ? " (label)" : "");
    }
    if (!r.decided) {
        ++gRefused;
        gLast = std::format("thespian: \"{}\" decides no card (leader {}, margin {:.2f}; scores {})", words,
            r.leader >= 0 ? std::format("[{}] \"{}\"", positions[static_cast<std::size_t>(r.leader)], cards[static_cast<std::size_t>(r.leader)].label) : std::string("none"),
            r.margin, scores);
        log::Info("{}", gLast);
        return gLast;
    }
    sm::CardState const& lead = r.cards[static_cast<std::size_t>(r.leader)];
    std::string const how = std::format("\"{}\" matched by its {} (score {:.2f}, margin {:.2f}, {:.0f}% of the line; scores {})", words,
        lead.byLabel ? "label" : "line", lead.score, r.margin, lead.coverage * 100.0f, scores);
    return Commit(live, positions[static_cast<std::size_t>(r.leader)], how);
}

// After a commit: skip Shepard's line when it starts, keep the player's voice silent until the next speaker, and log the
// conversation's state at each change (how the game steps from the queue to the reply to the answer).
void TickPending() {
    ue3::Object* const conversation = conversation_world::LiveConversation();
    std::uint64_t const sinceMs = GetTickCount64() - gPending.atMs;
    if (conversation == nullptr || conversation != gPending.conversation || sinceMs > 15000) {
        log::Info("thespian: the commit of position {} is over ({}; {} ms)", gPending.position,
            conversation == nullptr ? "the conversation ended" : conversation != gPending.conversation ? "another conversation" : "timed out",
            sinceMs);
        ClearMute();
        gPending = {};
        return;
    }
    ue3::Object* const handler = conversation_world::LiveWheelHandler();
    bool const wheelUp = handler != nullptr && gDisplayingWheel.ReadBool(handler).value_or(false);
    ue3::Object* const speaker = gSpeaker.Read<ue3::Object*>(conversation).value_or(nullptr);
    ue3::Object* const playerActor = gConvPlayer.Read<ue3::Object*>(conversation).value_or(nullptr);
    bool const playerSpeaks = speaker != nullptr && speaker == playerActor;
    std::string const state = std::format("wheel {}, speaker {}, entry {}, selected reply {}, delayed reply {}", wheelUp ? "up" : "down",
        playerSpeaks ? "the player" : speaker == nullptr ? "none" : "someone else", gCurrentEntry.Read<std::int32_t>(conversation).value_or(-99),
        gSelectedReply.Read<std::int32_t>(conversation).value_or(-99), gDelayedReply.Read<std::int32_t>(conversation).value_or(-99));
    if (state != gPending.trace && gPending.traceLines < 24) {
        gPending.trace = state;
        ++gPending.traceLines;
        log::Info("thespian: +{} ms: {}; recording left {:.2f} s", sinceMs, state, gRemainingVo.Read<float>(conversation).value_or(-1.0f));
    }
    // Shepard's voice is silent from the commit on (the queued line may start in any tick).
    if (!gPending.muted && playerActor != nullptr && vp::Installed()) {
        vp::Voice voice;
        voice.actor = playerActor;
        voice.gain = 0.0f;
        vp::Set(vp::Publisher::Thespian, &voice, 1);
        gPending.muted = true;
    }
    if (playerSpeaks && !wheelUp) {
        gPending.playerSpoke = true;
        if (!gPending.skipped && !gPending.unskippable) {
            gPending.skipped = true;  // one try: a refused skip is the game's word (an unskippable moment)
            bool skipped = false;
            if (ue3::Function* const skip = gSkipNode.Get()) {
                ue3::Params parms(ue3::runtime::Get(), skip);
                skipped = ue3::runtime::Call(conversation, skip, parms) && parms.ReturnBool().value_or(false);
            }
            gSkips += skipped ? 1 : 0;
            log::Info("thespian: +{} ms: Shepard's line started: SkipNode {}", sinceMs, skipped ? "skipped it" : "was refused (the line plays)");
        }
        return;
    }
    if (gPending.playerSpoke && !playerSpeaks) {
        void const* heard[2] = {};
        int const silenced = gPending.muted ? vp::TakeHeard(vp::Publisher::Thespian, heard, 2) : 0;
        log::Info("thespian: +{} ms: the next line began; the commit of position {} is done ({}{})", sinceMs, gPending.position,
            gPending.skipped ? "skipped" : "played", silenced > 0 ? "; Shepard's recording was silenced" : "; no source of Shepard's was seen playing");
        ClearMute();
        gPending = {};
    }
}

}  // namespace

void Tick(double) {
    if (gPending.active) {
        TickPending();
    }
    // A wheel coming up: counted, and listed while watching.
    ue3::Object* const handler = conversation_world::LiveWheelHandler();
    bool const up = handler != nullptr && gDisplayingWheel.ReadBool(handler).value_or(false);
    if (up && !gWheelWasUp) {
        ++gWheels;
        if (gWatch) {
            if (auto const live = ReadLive()) {
                log::Info("{}", Describe(*live));
            }
        }
    }
    gWheelWasUp = up;
}

std::string Command(std::string const& rest) {
    std::istringstream words(rest);
    std::string verb;
    words >> verb;
    if (verb == "watch") {
        std::string value;
        words >> value;
        gWatch = value != "off" && value != "0";
        return std::format("thespian: watching {} (the cards are logged whenever a wheel comes up)", gWatch ? "on" : "off");
    }
    auto const live = ReadLive();
    if (!live) {
        return "thespian: no conversation";
    }
    if (verb.empty() || verb == "cards") {
        return Describe(*live);
    }
    if (verb == "pick") {
        int position = -1;
        words >> position;
        return Commit(*live, position, "pick");
    }
    if (verb == "say") {
        std::string said;
        std::getline(words, said);
        return Say(*live, said);
    }
    return "thespian: cards | watch on|off | say <words> | pick <position>";
}

std::string StatusJson() {
    return std::format("{{\"wheels\": {}, \"commits\": {}, \"skips\": {}, \"refused\": {}, \"pending\": {}, \"watch\": {}}}", gWheels, gCommits,
        gSkips, gRefused, gPending.active ? "true" : "false", gWatch ? "true" : "false");
}

void Shutdown() {
    ClearMute();
    gPending = {};
}

} // namespace mevr::game::thespian
