// SPDX-License-Identifier: MIT
#include "game/Thespian.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/voice/ScriptMatch.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/Thespian.hpp"
#include "core/vr/Voice.hpp"
#include "engine/SettingsFile.hpp"
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

namespace sm = mevr::voice::script_match;
namespace vp = engine::voice_place;
namespace th = vr::thespian;

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
ue3::FunctionRef gGetEntryText{"SFXGame.BioConversation.GetEntryText"};                  // (optional bool bParse) -> string: the other person's line
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
// The game's wheel hidden while the panel stands (2026-11-13, the owner: "hide the game's dialogue display"): the wheel is the
// conversation movie's Flash; the handler shows it with UpdateConversationOptions (its setConversationOptions) when replies
// are needed and hides it with the movie's HideWheel when they no longer are. m_bDisplayingWheel is never touched, so the
// handler goes on as if the wheel were up, takes the stick as ever and hides it itself after a pick.
ue3::FieldRef gHandlerPanel{"Engine.BioSFHandler", "oPanel"};
ue3::FunctionRef gInvokeMethod{"Engine.BioSFPanel.InvokeMethod"};                                    // (string sMethodName)
ue3::FunctionRef gUpdateOptions{"SFXGame.BioSFHandler_Conversation.UpdateConversationOptions"};     // (BioConversation oConversation)
// `mevr thespian stick`: the wheel's own pick of a card's slot, for the plot comparison (research/70 step 2, (j)).
ue3::FieldRef gReplyLocations{"SFXGame.BioSFHandler_Conversation", "m_aReplyLocations"};  // int[6]: the category at each wheel location
ue3::FieldRef gSlotsUsed{"SFXGame.BioSFHandler_Conversation", "m_bSlotsUsed"};            // byte[6]
ue3::FunctionRef gSelectEntry{"SFXGame.BioSFHandler_Conversation.SelectConversationEntry"};  // (byte nWheelLocation) -> bool

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
    int skipTries = 0;
    bool playerSpoke = false;       // the conversation's speaker was the player since the commit
    bool muted = false;
    std::uint64_t atMs = 0;
    std::string trace;              // the last state logged (a line per change)
    int traceLines = 0;
};

Pending gPending;
Link gLink;
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

// The conversation movie's Flash wheel hidden (the movie's own HideWheel).
bool HideGameWheel(ue3::Object* handler) {
    ue3::Object* const panel = handler != nullptr ? gHandlerPanel.Read<ue3::Object*>(handler).value_or(nullptr) : nullptr;
    ue3::Function* const f = gInvokeMethod.Get();
    if (panel == nullptr || f == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), f);
    ue3::memory::OwnedString name(std::wstring(L"HideWheel"));
    return p.Valid() && p.Set("sMethodName", name.Get()) && ue3::runtime::Call(panel, f, p);
}

// ... and shown again as the handler shows it when a wheel comes up (its UpdateConversationOptions). Only while it is up.
bool ShowGameWheel(ue3::Object* handler, ue3::Object* conversation) {
    ue3::Function* const f = gUpdateOptions.Get();
    if (handler == nullptr || conversation == nullptr || f == nullptr || !gDisplayingWheel.ReadBool(handler).value_or(false)) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), f);
    return p.Valid() && p.Set("oConversation", conversation) && ue3::runtime::Call(handler, f, p);
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
        // The reply's line plays once m_nSelectedReply names it (seen in the game 2026-11-12: a reply queued while the other
        // person's last words still played had the player as the speaker a tick before its line began, and a skip then is
        // refused). A refused skip is tried again for a few ticks, then it is the game's word (an unskippable moment).
        bool const lineStarted = gSelectedReply.Read<std::int32_t>(conversation).value_or(-1) >= 0;
        if (lineStarted && !gPending.skipped && !gPending.unskippable && gPending.skipTries < 6) {
            ++gPending.skipTries;
            bool skipped = false;
            if (ue3::Function* const skip = gSkipNode.Get()) {
                ue3::Params parms(ue3::runtime::Get(), skip);
                skipped = ue3::runtime::Call(conversation, skip, parms) && parms.ReturnBool().value_or(false);
            }
            gPending.skipped = skipped;
            gSkips += skipped ? 1 : 0;
            if (skipped || gPending.skipTries == 6) {
                log::Info("thespian: +{} ms: Shepard's line started: SkipNode {} (try {})", sinceMs, skipped ? "skipped it" : "was refused (the line plays)",
                    gPending.skipTries);
            }
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


// ---- The spoken turn (research/70 section 8 steps 3 to 6) -----------------------------------------------------------------

struct ModeSettings {
    bool on = false;          // [Thespian] on
    bool hideWheel = true;    // [Thespian] hideWheel: the game's wheel hidden while the panel stands (the stick brings it back)
    th::Settings turn;        // strictness, endMs, pauseMs
    bool handSilent = false;  // handPick = silent: a reply picked by hand is skipped and silenced too
    int keyterms = 12;        // the wheel's rare words given to the recognizer
};
ModeSettings gMode;
std::uint64_t gModeReadMs = 0;
std::optional<bool> gOnOverride;  // `mevr thespian on|off` (this session)
std::string gModeText;

void RefreshMode() {
    std::uint64_t const now = GetTickCount64();
    if (gModeReadMs != 0 && now - gModeReadMs < 1000) {
        return;
    }
    gModeReadMs = now;
    ini::Loaded const loaded = ini::Read(engine::SettingsFile());
    ModeSettings m;
    auto number = [&](char const* key, double fallback, double lo, double hi) {
        auto const v = ini::GetNumber(loaded.file, "Thespian", key);
        return v ? std::clamp(*v, lo, hi) : fallback;
    };
    m.on = number("on", 0.0, 0.0, 1.0) >= 0.5;
    if (auto const text = ini::Get(loaded.file, "Thespian", "strictness")) {
        m.turn.strictness = th::ParseStrictness(*text, m.turn.strictness);
    }
    m.turn.endMs = static_cast<int>(number("endMs", m.turn.endMs, 0.0, 2000.0));
    m.turn.pauseMs = static_cast<int>(number("pauseMs", m.turn.pauseMs, 200.0, 5000.0));
    m.turn.staleMs = static_cast<int>(number("staleMs", m.turn.staleMs, 1000.0, 20000.0));
    if (auto const text = ini::Get(loaded.file, "Thespian", "handPick")) {
        m.handSilent = ini::Lower(ini::Trim(*text)) == "silent";
    }
    m.keyterms = static_cast<int>(number("keyterms", m.keyterms, 0.0, 32.0));
    m.hideWheel = number("hideWheel", 1.0, 0.0, 1.0) >= 0.5;
    if (gOnOverride) {
        m.on = *gOnOverride;
    }
    std::string const text = std::format("{}{}, strictness {}, a line's end {} ms, a pause {} ms, a hand pick {}", m.on ? "on" : "off",
        gOnOverride ? " (the console's)" : "", th::Name(m.turn.strictness), m.turn.endMs, m.turn.pauseMs,
        m.handSilent ? "is silenced too" : "plays Shepard's recording");
    if (text != gModeText) {
        gModeText = text;
        log::Info("thespian: the spoken mode is {}", text);
    }
    gMode = m;
}

struct HeardLine {
    std::uint64_t id = 0;
    std::string text;
};

// One wheel's spoken turn.
struct Turn {
    bool active = false;             // the wheel is up and the recognizer was asked to listen
    bool blocked = false;            // this wheel is not for speaking (no cards, not English): the game's wheel alone
    ue3::Object* conversation = nullptr;
    std::vector<Card> all;           // every current reply
    std::vector<int> candidates;     // indices into `all`: the matcher's cards, in order
    std::optional<sm::Wheel> wheel;
    std::uint64_t signature = 0;     // of the current reply list (a change is a new wheel)
    std::vector<HeardLine> lines;    // the open listen's lines so far
    std::uint64_t listenStart = 0;
    std::size_t baseLine = 0;        // words before this line, and the first baseWords of it, were dropped (a restart)
    std::size_t baseWords = 0;
    std::string text;                // the words that count
    sm::Result now;                  // the matcher, while speaking
    sm::Result atEnd;                // the matcher, the player has stopped
    th::State state;
    th::Output out;
    std::uint64_t startedMs = 0;
    std::uint64_t firstWordMs = 0;
    std::string speaker;
    bool anchored = false;
    double speakerHead[3] = {};
    std::string heard;               // the other person's last line
    ue3::Object* handler = nullptr;  // the wheel's handler
    bool wheelHidden = false;        // the game's wheel is hidden (by us, this turn)
    bool wheelByHand = false;        // the stick brought it back
};
Turn gTurn;
std::uint32_t gPanelSerial = 0;
bool gPanelShown = false;
std::uint64_t gOurListens[4] = {};
unsigned gOurListenAt = 0;
unsigned gSpoken = 0, gSpokenCommits = 0, gUnsure = 0, gStoodDown = 0;
std::string gLastHeard;
bool gToldNoRecognizer = false;
vr::voice::State gVoice;

void NoteOurListen(std::uint64_t startMs) {
    if (startMs == 0) {
        return;
    }
    for (std::uint64_t const known : gOurListens) {
        if (known == startMs) {
            return;
        }
    }
    gOurListens[gOurListenAt++ % 4] = startMs;
}

std::uint64_t Signature(ue3::Object* conversation) {
    auto const* const current = gCurrentReplies.In<ue3::TArray<std::int32_t>>(conversation);
    if (current == nullptr || current->data == nullptr || current->count <= 0 || current->count > kMaxCards) {
        return 0;
    }
    std::uint64_t h = 1469598103934665603ull;
    for (std::int32_t i = 0; i < current->count; ++i) {
        h = (h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(current->data[i]))) * 1099511628211ull;
    }
    return h ^ static_cast<std::uint64_t>(current->count);
}

std::vector<std::string> SpaceWords(std::string const& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string w; in >> w;) {
        out.push_back(w);
    }
    return out;
}

// The words that count: the listen's lines from the restart's baseline on.
std::string Counted() {
    std::string out;
    for (std::size_t i = gTurn.baseLine; i < gTurn.lines.size(); ++i) {
        std::vector<std::string> const words = SpaceWords(gTurn.lines[i].text);
        for (std::size_t w = i == gTurn.baseLine ? std::min(gTurn.baseWords, words.size()) : 0; w < words.size(); ++w) {
            out += (out.empty() ? "" : " ") + words[w];
        }
    }
    return out;
}

void Rebase() {
    if (gTurn.lines.empty()) {
        gTurn.baseLine = 0;
        gTurn.baseWords = 0;
    } else {
        gTurn.baseLine = gTurn.lines.size() - 1;
        gTurn.baseWords = SpaceWords(gTurn.lines.back().text).size();
    }
    gTurn.text.clear();
    gTurn.now = {};
    gTurn.atEnd = {};
    gTurn.firstWordMs = 0;
}

th::Match Reduce(sm::Result const& r, bool words) {
    th::Match m;
    m.words = words;
    m.leader = r.leader;
    m.decided = r.decided;
    if (r.leader >= 0 && static_cast<std::size_t>(r.leader) < r.cards.size()) {
        sm::CardState const& c = r.cards[static_cast<std::size_t>(r.leader)];
        m.reachedEnd = c.reachedEnd;
        m.coverage = c.coverage;
        m.byLabel = c.byLabel;
    }
    return m;
}

// English enough to read aloud to an English recognizer: the cards' words hold some of the language's commonest.
bool LooksEnglish(std::vector<Card> const& all, std::vector<int> const& candidates) {
    int words = 0, stops = 0;
    for (int const i : candidates) {
        for (std::string const& w : sm::Tokens(all[static_cast<std::size_t>(i)].line + " " + all[static_cast<std::size_t>(i)].label)) {
            ++words;
            stops += sm::IsStopWord(w) ? 1 : 0;
        }
    }
    return words < 12 || stops * 12 >= words;
}

void SendPanel(Link const& link, bool shown) {
    if (link.setPanel == nullptr || (!shown && !gPanelShown)) {
        return;
    }
    th::Panel panel;
    panel.shown = shown ? 1 : 0;
    if (shown) {
        panel.mic = static_cast<std::uint8_t>(gTurn.out.mic);
        panel.level = gVoice.level;
        panel.serial = gPanelSerial;
        panel.anchored = gTurn.anchored ? 1 : 0;
        panel.gameWheel = gTurn.wheelHidden ? 0 : 1;
        for (int i = 0; i < 3; ++i) {
            panel.speakerHead[i] = static_cast<float>(gTurn.speakerHead[i]);
        }
        strncpy_s(panel.speaker, gTurn.speaker.c_str(), _TRUNCATE);
        strncpy_s(panel.heard, gTurn.heard.c_str(), _TRUNCATE);
        int const leader = gTurn.out.leader;
        bool const committed = gTurn.out.phase == th::Phase::Committed;
        // Rows: the wheel's replies first, then the investigate topics; unavailable replies shown greyed; the rest left out.
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t a = 0; a < gTurn.all.size() && panel.cardCount < th::kMaxCards; ++a) {
                Card const& c = gTurn.all[a];
                bool const investigate = c.category == kCategoryInvestigate;
                if (investigate != (pass == 1) || (!c.candidate && c.style != kStyleIllegal)) {
                    continue;
                }
                th::PanelCard& row = panel.cards[panel.cardCount++];
                panel.mainCount += pass == 0 ? 1 : 0;
                strncpy_s(row.label, c.label.c_str(), _TRUNCATE);
                strncpy_s(row.line, c.line.c_str(), _TRUNCATE);
                row.tone = static_cast<std::uint8_t>(th::ToneOf(c.category, c.style));
                auto const it = std::find(gTurn.candidates.begin(), gTurn.candidates.end(), static_cast<int>(a));
                if (it == gTurn.candidates.end()) {
                    row.look = static_cast<std::uint8_t>(th::Look::Unavailable);
                    continue;
                }
                int const card = static_cast<int>(it - gTurn.candidates.begin());
                row.look = static_cast<std::uint8_t>(card == leader ? (committed ? th::Look::Committed : th::Look::Leading) : leader >= 0 ? th::Look::Dim : th::Look::Idle);
                if (card == leader && static_cast<std::size_t>(card) < gTurn.now.cards.size() && !c.line.empty()) {
                    sm::CardState const& st = gTurn.now.cards[static_cast<std::size_t>(card)];
                    for (std::size_t w = 0; w < st.lit.size() && w < 64; ++w) {
                        row.lit |= st.lit[w] != 0 ? (1ull << w) : 0ull;
                    }
                    if (st.frontier + 1 < static_cast<int>(st.lit.size())) {
                        row.next = static_cast<std::int16_t>(st.frontier + 1);
                    }
                }
            }
        }
    }
    link.setPanel(&panel);
    gPanelShown = shown;
}

void EndTurn(Link const& link, char const* why) {
    if (!gTurn.active) {
        return;
    }
    if (link.voice.setListen != nullptr) {
        link.voice.setListen(0);
    }
    // A turn that ends with the wheel still up and nothing taken (the mode off, the recognizer gone, the mod unloading):
    // the game's wheel back, so the player is never left without one. After a commit the handler hides it itself.
    if (gTurn.wheelHidden && gTurn.state.phase != th::Phase::Committed &&
        ShowGameWheel(gTurn.handler, conversation_world::LiveConversation())) {
        log::Info("thespian: the game's wheel is shown again ({})", why);
    }
    log::Info("thespian: the turn is over ({}; {:.1f} s, heard \"{}\")", why, static_cast<double>(GetTickCount64() - gTurn.startedMs) / 1000.0, gTurn.text);
    bool const blocked = gTurn.blocked;
    gTurn = {};
    gTurn.blocked = blocked;
}

void BeginTurn(Link const& link, Live const& live) {
    gTurn = {};
    gTurn.conversation = live.conversation;
    gTurn.signature = Signature(live.conversation);
    gTurn.all = live.cards;
    std::vector<sm::Card> cards;
    for (std::size_t i = 0; i < gTurn.all.size(); ++i) {
        if (gTurn.all[i].candidate) {
            gTurn.candidates.push_back(static_cast<int>(i));
            cards.push_back({gTurn.all[i].label, gTurn.all[i].line});
        }
    }
    if (cards.empty() || !LooksEnglish(gTurn.all, gTurn.candidates)) {
        gTurn.blocked = true;
        ++gStoodDown;
        log::Info("thespian: this wheel is not for speaking ({}): the game's wheel alone", cards.empty() ? "no reply to read" : "its text isn't English");
        return;
    }
    gTurn.wheel.emplace(cards);
    gTurn.active = true;
    gTurn.startedMs = GetTickCount64();
    ++gSpoken;
    ++gPanelSerial;
    // The other person: whoever spoke last (the wheel's line), for the panel's header and place.
    ue3::Object* const speaker = gSpeaker.Read<ue3::Object*>(live.conversation).value_or(nullptr);
    ue3::Object* const playerActor = gConvPlayer.Read<ue3::Object*>(live.conversation).value_or(nullptr);
    if (speaker != nullptr && speaker != playerActor && conversation_world::InWorld()) {
        gTurn.anchored = conversation_world::PersonHead(speaker, gTurn.speakerHead, &gTurn.speaker);
    }
    if (ue3::Function* const f = gGetEntryText.Get()) {
        ue3::Params parms(ue3::runtime::Get(), f);
        parms.SetBool("bParse", true);
        if (ue3::runtime::Call(live.conversation, f, parms)) {
            ue3::FString returned = parms.Return<ue3::FString>().value_or(ue3::FString{});
            gTurn.heard = ue3::memory::Narrow(returned);
            ue3::memory::FreeString(returned);
        }
    }
    std::string terms;
    for (std::string const& t : gTurn.wheel->Keyterms(gMode.keyterms)) {
        terms += (terms.empty() ? "" : ",") + t;
    }
    if (link.voice.setKeyterms != nullptr && !terms.empty()) {
        link.voice.setKeyterms(terms.c_str());
    }
    if (link.voice.setListen != nullptr) {
        link.voice.setListen(2);  // the open listen: no ducking, until we end it
    }
    gTurn.handler = live.handler;
    if (gMode.hideWheel) {
        gTurn.wheelHidden = HideGameWheel(live.handler);
        log::Info("thespian: the game's wheel {} (the left stick brings it back)", gTurn.wheelHidden ? "is hidden" : "couldn't be hidden");
    }
    log::Info("thespian: a wheel to speak: {} cards of {} replies; key terms \"{}\"; the other's line has {:.2f} s left", cards.size(), gTurn.all.size(), terms,
        gRemainingVo.Read<float>(live.conversation).value_or(-1.0f));
}

void TakeLines(Link const& link) {
    if (link.voice.takeLine == nullptr) {
        return;
    }
    vr::voice::Line line;
    bool changed = false;
    while (link.voice.takeLine(&line)) {
        if (line.listenStartMs != gTurn.listenStart) {
            // A listen that isn't this turn's (the radio's last words, an earlier wheel's): not ours to read.
            if (gVoice.open == 0 || line.listenStartMs != gVoice.listenStartMs) {
                continue;
            }
            gTurn.listenStart = line.listenStartMs;
            gTurn.lines.clear();
            gTurn.baseLine = 0;
            gTurn.baseWords = 0;
        }
        NoteOurListen(line.listenStartMs);
        if (line.endOfListen != 0) {
            continue;
        }
        auto it = std::find_if(gTurn.lines.begin(), gTurn.lines.end(), [&](HeardLine const& h) { return h.id == line.id; });
        if (it == gTurn.lines.end()) {
            gTurn.lines.push_back({line.id, line.text});
        } else {
            it->text = line.text;
        }
        changed = true;
    }
    if (!changed) {
        return;
    }
    std::string const text = Counted();
    if (text == gTurn.text) {
        return;
    }
    gTurn.text = text;
    gLastHeard = text;
    if (gTurn.firstWordMs == 0 && !text.empty()) {
        gTurn.firstWordMs = GetTickCount64();
    }
    gTurn.now = gTurn.wheel->Match(text, false);
    gTurn.atEnd = gTurn.wheel->Match(text, true);
}

void TickTurn(double deltaSeconds, Link const& link) {
    RefreshMode();
    bool ready = false;
    gVoice = {};
    if (link.voice.has && link.voice.state != nullptr) {
        link.voice.state(&gVoice);
        ready = gVoice.status == static_cast<std::uint8_t>(vr::voice::Status::Ready);
        if (gVoice.open != 0) {
            NoteOurListen(gVoice.listenStartMs);
        }
    }
    ue3::Object* const conversation = conversation_world::LiveConversation();
    ue3::Object* const handler = conversation != nullptr ? conversation_world::LiveWheelHandler() : nullptr;
    bool const up = handler != nullptr && gDisplayingWheel.ReadBool(handler).value_or(false);
    if (!up) {
        gTurn.blocked = false;
    }
    if (gMode.on && up && !ready && !gToldNoRecognizer) {
        gToldNoRecognizer = true;
        log::Info("thespian: the spoken mode is on but there is no recognizer ({}): the game's wheel alone",
            !link.voice.has ? "the host has no voice services" : gVoice.error[0] != '\0' ? gVoice.error : "not loaded");
    }
    bool const want = gMode.on && ready && up && !gPending.active;
    if (gTurn.active && (!want || conversation != gTurn.conversation || Signature(conversation) != gTurn.signature)) {
        // The wheel went down without us: a pick by hand. With handPick = silent its line is skipped and silenced too.
        bool const byHand = !up && gMode.on && !gPending.active && conversation == gTurn.conversation;
        EndTurn(link, !up ? "the wheel went down: a pick by hand" : !gMode.on ? "the mode went off" : !ready ? "the recognizer went away" : "another wheel");
        if (byHand && gMode.handSilent && conversation != nullptr) {
            gPending = {};
            gPending.active = true;
            gPending.conversation = conversation;
            gPending.atMs = GetTickCount64();
            auto const replies = gReplyList.ElementsOf(conversation);
            std::int32_t const selected = gSelectedReply.Read<std::int32_t>(conversation).value_or(-1);
            void* const node = replies && selected >= 0 ? replies->At(selected) : nullptr;
            gPending.reply = selected;
            gPending.unskippable = node == nullptr || gReplyUnskippable.ReadBool(node).value_or(true);  // unknown: silenced, not skipped
            log::Info("thespian: a pick by hand (reply {}): silenced{}", selected, gPending.unskippable ? "" : " and skipped when it starts");
        }
    }
    if (want && !gTurn.active && !gTurn.blocked) {
        if (auto const live = ReadLive(); live && live->wheelUp) {
            BeginTurn(link, *live);
        }
    }
    if (gTurn.active && gTurn.wheelHidden) {
        // The left stick (the wheel's) pushed: a choice by hand. The game's wheel comes back; the voice still listens.
        vr::controllers::State const c = vr::controllers::Latest();
        float const x = c.active ? c.left.stickX : 0.0f, y = c.active ? c.left.stickY : 0.0f;
        if (x * x + y * y > 0.25f && ShowGameWheel(handler, conversation)) {
            gTurn.wheelHidden = false;
            gTurn.wheelByHand = true;
            log::Info("thespian: the left stick moved: the game's wheel is shown again, the panel folds away");
        }
    }
    if (gTurn.active) {
        TakeLines(link);
        th::Input in;
        in.wheelUp = true;
        in.otherSpeaking = gRemainingVo.Read<float>(conversation).value_or(0.0f) > 0.05f;
        in.silentMs = gVoice.open != 0 ? gVoice.silentMs : 0;
        // No commit before the open listen runs (the quiet clock is its own).
        // Words: something other than the commonest words alone (the recognizer writes "you" for a quiet room now and
        // then, seen in the game 2026-11-13), unless they are a reply themselves.
        bool words = false;
        for (std::string const& w : sm::Tokens(gTurn.text)) {
            words = words || !sm::IsStopWord(w);
        }
        words = words || gTurn.now.decided || gTurn.atEnd.decided;
        in.now = Reduce(gTurn.now, words);
        in.atEnd = Reduce(gTurn.atEnd, words);
        in.dt = deltaSeconds;
        th::Phase const before = gTurn.state.phase;
        gTurn.out = th::Step(gTurn.state, gMode.turn, in);
        if (gTurn.out.restart) {
            if (!gTurn.text.empty()) {
                ++gUnsure;
                log::Info("thespian: \"{}\" was dropped ({})", gTurn.text, in.otherSpeaking || before == th::Phase::Off ? "a new turn" : "nobody could place it");
            }
            Rebase();
        }
        if (gTurn.out.phase != before && gTurn.out.phase == th::Phase::Leading && gTurn.out.leader >= 0) {
            Card const& c = gTurn.all[static_cast<std::size_t>(gTurn.candidates[static_cast<std::size_t>(gTurn.out.leader)])];
            log::Info("thespian: leading [{}] \"{}\" after \"{}\" ({} ms into the speech)", c.position, c.label, gTurn.text,
                gTurn.firstWordMs != 0 ? GetTickCount64() - gTurn.firstWordMs : 0);
        }
        if (gTurn.out.commit >= 0 && static_cast<std::size_t>(gTurn.out.commit) < gTurn.candidates.size()) {
            Card const& c = gTurn.all[static_cast<std::size_t>(gTurn.candidates[static_cast<std::size_t>(gTurn.out.commit)])];
            sm::Result const& r = in.now.decided && in.now.leader == gTurn.out.commit ? gTurn.now : gTurn.atEnd;
            sm::CardState const& st = r.cards[static_cast<std::size_t>(gTurn.out.commit)];
            std::string const how = std::format("spoken \"{}\" (by its {}, {:.0f}% of the line{}, quiet {} ms, {} ms after the first word)", gTurn.text,
                st.byLabel ? "label" : "line", st.coverage * 100.0f, st.reachedEnd ? ", read to its end" : "", in.silentMs,
                gTurn.firstWordMs != 0 ? GetTickCount64() - gTurn.firstWordMs : 0);
            Live live;
            live.conversation = conversation;
            live.handler = handler;
            live.wheelUp = true;
            live.cards = gTurn.all;
            ++gSpokenCommits;
            if (!gTurn.wheelByHand) {
                SendPanel(link, true);  // the committed row's flash
            }
            Commit(live, c.position, how);
            EndTurn(link, "a reply was taken");
        }
    }
    // The panel folds away once the stick brought the game's wheel back (a pick by hand: one set of replies at a time, and the
    // game's wheel quad stands where the panel would). The microphone still listens.
    SendPanel(link, gTurn.active && !gTurn.wheelByHand);
}

}  // namespace

bool OwnsMicrophone() {
    return gTurn.active;
}

bool OwnsListen(std::uint64_t listenStartMs) {
    if (listenStartMs == 0) {
        return false;
    }
    for (std::uint64_t const known : gOurListens) {
        if (known == listenStartMs) {
            return true;
        }
    }
    return false;
}

void Tick(double deltaSeconds, Link const& link) {
    gLink = link;
    if (gPending.active) {
        TickPending();
    }
    TickTurn(deltaSeconds, link);
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
    if (verb == "on" || verb == "off" || verb == "auto") {
        gOnOverride = verb == "auto" ? std::nullopt : std::optional<bool>(verb == "on");
        gModeReadMs = 0;
        RefreshMode();
        return "thespian: the spoken mode is " + gModeText;
    }
    if (verb == "status") {
        return std::format("thespian: the spoken mode is {}; recognizer {} ({}), open listen {}, quiet {} ms, level {:.3f}; turn {} (phase {}), heard \"{}\"; "
                           "{} wheels, {} spoken turns, {} taken by voice, {} dropped, {} wheels not for speaking; last: {}",
            gModeText, gVoice.status, gVoice.model, gVoice.open != 0 ? "yes" : "no", gVoice.silentMs, gVoice.level, gTurn.active ? "active" : gTurn.blocked ? "blocked" : "none",
            static_cast<int>(gTurn.state.phase), gTurn.active ? gTurn.text : gLastHeard, gWheels, gSpoken, gSpokenCommits, gUnsure, gStoodDown, gLast);
    }
    if (verb == "wheel") {
        // Tests without controllers: the game's wheel shown (as the stick does) or hidden again.
        std::string what;
        words >> what;
        if (!gTurn.active) {
            return "thespian: no spoken turn (the mode off, or no wheel up)";
        }
        if (what == "show") {
            bool const shown = ShowGameWheel(gTurn.handler, conversation_world::LiveConversation());
            gTurn.wheelHidden = gTurn.wheelHidden && !shown;
            gTurn.wheelByHand = gTurn.wheelByHand || shown;
            return std::format("thespian: the game's wheel {}", shown ? "is shown again" : "couldn't be shown");
        }
        bool const hidden = HideGameWheel(gTurn.handler);
        gTurn.wheelHidden = gTurn.wheelHidden || hidden;
        return std::format("thespian: the game's wheel {}", hidden ? "is hidden" : "couldn't be hidden");
    }
    if (verb == "feed") {
        std::string path;
        std::getline(words, path);
        path = ini::Trim(path);
        if (gLink.voice.command == nullptr) {
            return "thespian: the host has no voice services";
        }
        gLink.voice.command(("wav " + path).c_str());
        return std::format("thespian: fed {} to the recognizer ({})", path, gVoice.open != 0 ? "into the open listen" : "no open listen: as a listen of its own");
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
    if (verb == "stick") {
        // The reply at a position taken as the stick and A take it: the handler's SelectConversationEntry on the wheel
        // location that holds the card's category. Shepard's recording plays. Not for investigate topics (a sub-menu).
        int position = -1;
        words >> position;
        auto const it = std::find_if(live->cards.begin(), live->cards.end(), [&](Card const& c) { return c.position == position; });
        ue3::Function* const select = gSelectEntry.Get();
        if (!live->wheelUp || it == live->cards.end() || !it->candidate || it->category == kCategoryInvestigate || select == nullptr || live->handler == nullptr) {
            return "thespian: no stick pick (the wheel isn't up, no such card, or an investigate topic)";
        }
        for (int location = 0; location < 6; ++location) {
            std::int32_t const* const category = gReplyLocations.At<std::int32_t>(live->handler, location);
            std::uint8_t const* const used = gSlotsUsed.At<std::uint8_t>(live->handler, location);
            if (category != nullptr && used != nullptr && *used != 0 && *category == it->category) {
                ue3::Params parms(ue3::runtime::Get(), select);
                parms.Set("nWheelLocation", static_cast<std::uint8_t>(location));
                bool const called = ue3::runtime::Call(live->handler, select, parms);
                return std::format("thespian: the stick's pick of position {} \"{}\": SelectConversationEntry({}) called {}", position, it->label, location, called);
            }
        }
        return std::format("thespian: no wheel location holds category {}", it->category);
    }
    if (verb == "say") {
        std::string said;
        std::getline(words, said);
        return Say(*live, ini::Trim(said));
    }
    return "thespian: cards | watch on|off | say <words> | pick <position> | stick <position> | on | off | auto | status | feed <wav> | wheel show|hide";
}

std::string StatusJson() {
    return std::format("{{\"on\": {}, \"wheels\": {}, \"commits\": {}, \"skips\": {}, \"refused\": {}, \"pending\": {}, \"watch\": {}, \"turn\": {}, "
                       "\"phase\": {}, \"spokenTurns\": {}, \"spokenCommits\": {}, \"dropped\": {}, \"notForSpeaking\": {}}}",
        gMode.on ? "true" : "false", gWheels, gCommits, gSkips, gRefused, gPending.active ? "true" : "false", gWatch ? "true" : "false",
        gTurn.active ? "true" : "false", static_cast<int>(gTurn.state.phase), gSpoken, gSpokenCommits, gUnsure, gStoodDown);
}

void Shutdown() {
    EndTurn(gLink, "the mod is unloading");
    SendPanel(gLink, false);
    ClearMute();
    gPending = {};
}

} // namespace mevr::game::thespian
