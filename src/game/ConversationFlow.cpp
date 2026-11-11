// SPDX-License-Identifier: MIT
// LE1 conversations that flow: the chain starts' lines flagged ambient (game/ConversationFlow.hpp, research/59).
#include "game/ConversationFlow.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "engine/ConversationHooks.hpp"
#include "engine/SettingsFile.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::game::conversation_flow {

namespace {

namespace cw = vr::conversation_world;

constexpr std::uint8_t kReplyStandard = 0;  // EReplyTypes: REPLY_STANDARD shows the wheel; AUTOCONTINUE 1 and DIALOGEND 2 go on

// The level's script: each start action's conversation.
ue3::ClassRef gStartAction{"SFXGame.BioSeqAct_StartConversation"};
ue3::FieldRef gActionConv{"SFXGame.BioSeqAct_StartConversation", "Conv"};
ue3::FieldRef gActionRange{"SFXGame.BioSeqAct_StartConversation", "m_fInterruptRange"};
// The conversation's graph: entries (bAmbient; ReplyListNew's nIndex into m_ReplyList) and replies (ReplyType).
ue3::FieldRef gEntryList{"SFXGame.BioConversation", "m_EntryList"};
ue3::FieldRef gEntryAmbient{gEntryList, "bAmbient"};
ue3::FieldRef gEntryReplies{gEntryList, "ReplyListNew"};
ue3::FieldRef gEntryReplyIndex{gEntryReplies, "nIndex"};
ue3::FieldRef gStartingList{"SFXGame.BioConversation", "m_StartingList"};
ue3::FieldRef gReplyList{"SFXGame.BioConversation", "m_ReplyList"};
ue3::FieldRef gReplyType{gReplyList, "ReplyType"};
// The live conversation (as game/ConversationWorld reads it) and its interrupt range.
ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gCurrentConversation{"SFXGame.BioWorldInfo", "m_oCurrentConversation"};
ue3::FieldRef gConvAmbient{"SFXGame.BioConversation", "m_bAmbient"};
ue3::FieldRef gConvRange{"SFXGame.BioConversation", "m_fInterruptRange"};
// A newcomer during a flowed scene (research/69 item 1): the line the scene is on, the newcomer's start action, and firing it again.
ue3::FieldRef gConvCurrentEntry{"SFXGame.BioConversation", "m_nCurrentEntry"};
ue3::FieldRef gKismetStart{"SFXGame.BioConversation", "m_pKismetStart"};
ue3::FunctionRef gForceActivate{"Engine.SequenceOp.ForceActivateInput"};
ue3::FieldRef gConvSubtitle{"SFXGame.BioConversation", "m_sCurrentSubTitle"};      // the line's text, the speaker's name first
ue3::FieldRef gConvSubtitlesOption{"SFXGame.BioConversation", "m_nSubtitlesMode"};  // the player's subtitle option at its start
ue3::FieldRef gWorldSubtitles{"SFXGame.BioWorldInfo", "m_Subtitles"};               // what the conversation GUI draws
ue3::FunctionRef gDisplaySubtitle{"SFXGame.BioSubtitles.DisplaySubtitle"};
ue3::FieldRef gSubtitlesMode{"SFXGame.BioSubtitles", "m_CurrentRenderMode"};       // ESubtitlesRenderMode (3 bottom, 4 ambient)
ue3::FieldRef gWorldRange{"SFXGame.BioWorldInfo", "m_fConversationInterruptDistance"};  // the world's default (BIOGame.ini), used when the start's is 0
// The streaming levels (as `mevr stream chunks` reads them): a chunk loaded or shown is when new start actions appear.
ue3::FieldRef gStreamingLevels{"Engine.WorldInfo", "StreamingLevels"};
ue3::FieldRef gLevelLoaded{"Engine.LevelStreaming", "LoadedLevel"};
ue3::FieldRef gLevelVisible{"Engine.LevelStreaming", "bIsVisible"};

constexpr double kUnitsPerMeter = 100.0;

// ---- Settings ([Cinematics] in LE1MEVR.ini, read every second) ---------------------------------------------------------------
struct Settings {
    bool world = false;   // conversations = world (the flow belongs to the first-person mode)
    bool flow = true;     // worldFlow = all | off
    float leashM = 0.0f;  // worldFlowLeashM: 0 the game's interrupt range, -1 never interrupted, else metres from the speaker
    bool protect = true;  // worldFlowProtect: a flowed scene is protected from other conversations at all (research/69 item 1)
    cw::NewcomerRule newcomers = cw::NewcomerRule::Hold;  // worldFlowNewcomers = hold | refuse | through: another level scene during one
    bool subtitles = true;  // worldFlowSubtitles: a flowed line's subtitle through the game's display (the game draws none for barks)
    bool Active() const { return world && flow; }
};
Settings gSettings;
std::uint64_t gSettingsMs = 0;
std::string gSettingsText;

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsMs != 0 && now - gSettingsMs < 1000) {
        return;
    }
    gSettingsMs = now;
    ini::Loaded const loaded = ini::Read(engine::SettingsFile());
    Settings s;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "conversations")) {
        s.world = cw::ParseMode(text->c_str(), cw::Mode::Screen) == cw::Mode::World;
    }
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldFlow")) {
        std::string const w = ini::Lower(ini::Trim(*text));
        s.flow = !(w == "off" || w == "0" || w == "none");
    }
    if (auto const v = ini::GetNumber(loaded.file, "Cinematics", "worldFlowLeashM")) {
        s.leashM = static_cast<float>(std::clamp(*v, -1.0, 100.0));
    }
    if (auto const v = ini::GetNumber(loaded.file, "Cinematics", "worldFlowProtect")) {
        s.protect = *v != 0.0;
    }
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldFlowNewcomers")) {
        s.newcomers = cw::ParseNewcomerRule(*text, s.newcomers);
    }
    if (auto const v = ini::GetNumber(loaded.file, "Cinematics", "worldFlowSubtitles")) {
        s.subtitles = *v != 0.0;
    }
    std::string const text = std::format("worldFlow = {} ({}), worldFlowLeashM = {}, during a flowed scene barks are {} and other scenes {}",
        s.flow ? "all" : "off", s.world ? "conversations = world: active" : "not the world mode: inactive",
        s.leashM < 0.0f ? std::string("never interrupted") : s.leashM == 0.0f ? std::string("the game's") : std::format("{:.1f} m", s.leashM),
        s.protect ? "refused" : "let through", s.protect ? cw::Name(s.newcomers) : "through");
    if (text != gSettingsText) {
        gSettingsText = text;
        log::Info("convo flow: [Cinematics] {}", text);
    }
    gSettings = s;
}

// ---- The flags we set --------------------------------------------------------------------------------------------------------
struct Flipped {
    ue3::Object* conversation = nullptr;
    std::string key;
    std::vector<int> entries;  // the entries we flagged (to put back)
};
std::vector<Flipped> gFlipped;
std::uint64_t gLevelsAtScan = 0;  // the streaming levels' signature at the last scan
std::uint64_t gScanMs = 0;
std::uint64_t gLastRescanMs = 0;
int gScans = 0;
std::map<std::string, int> gRefused;  // conversation key -> entries refused (the live data differs from the scan)
ue3::Object* gLive = nullptr;         // the live conversation last seen
std::uint32_t gPlayed = 0;            // chain plays seen as ambient
std::uint32_t gBarksRefused = 0;      // barks that wanted to start over a flowed scene and were refused (worldFlowProtect)
std::uint32_t gScenesHeld = 0;        // other scenes' starts held until the flowed scene was over (worldFlowNewcomers = hold)
std::uint32_t gScenesRefused = 0;     // ... or refused (= refuse)
std::uint32_t gHeldFired = 0;         // held starts fired again
std::vector<cw::HeldStart> gHeld;     // the start actions held (id: the action's address)
std::map<std::uint64_t, std::string> gHeldNames;
std::vector<std::string> gPendingLog; // what the hook decided, logged by Tick (the hook runs inside the game's code)
std::uint64_t gFreeSinceMs = 0;       // since when nothing that would refuse a held start has been playing

Flipped* Find(ue3::Object* conv) {
    for (Flipped& f : gFlipped) {
        if (f.conversation == conv) {
            return &f;
        }
    }
    return nullptr;
}

std::string Ranges(std::vector<int> const& indices) {
    std::string out;
    for (std::size_t i = 0; i < indices.size();) {
        std::size_t j = i;
        while (j + 1 < indices.size() && indices[j + 1] == indices[j] + 1) {
            ++j;
        }
        if (!out.empty()) out += ' ';
        out += j == i ? std::format("{}", indices[i]) : std::format("{}-{}", indices[i], indices[j]);
        i = j + 1;
    }
    return out;
}

// True if entry `index` of `conv` offers a REPLY_STANDARD reply (a wheel): then it is no chain line and never flagged.
bool OffersWheel(ue3::Object* conv, void* entry) {
    auto const replies = gReplyList.ElementsOf(conv, 8192);
    auto const offered = gEntryReplies.ElementsOf(entry, 64);
    if (!replies || !offered) {
        return false;
    }
    for (std::int32_t j = 0; j < offered->count; ++j) {
        auto const idx = gEntryReplyIndex.Read<std::int32_t>(offered->At(j));
        if (!idx || *idx < 0 || *idx >= replies->count) {
            continue;
        }
        auto const type = gReplyType.Read<std::uint8_t>(replies->At(*idx));
        if (type && *type == kReplyStandard) {
            return true;
        }
    }
    return false;
}

bool WriteAmbient(void* entry, bool value) {
    ue3::PropertyInfo const* const info = gEntryAmbient.Get();
    if (info == nullptr || entry == nullptr || !gEntryAmbient.WriteBool(entry, value)) {
        return false;
    }
    write_journal::Wrote("ConversationFlow.entryAmbient", static_cast<std::uint8_t*>(entry) + info->offset, 4);
    return true;
}

// Flags the scan's chain entries of `conv` ambient (idempotent: a flag already set, by the game or by us, is left).
void Flip(ue3::Object* conv, ue3::Object* action) {
    std::string const name = ue3::runtime::FullName(conv);
    std::string const key = cw::ConversationKey(name);
    std::span<int const> const wanted = cw::FlowEntries(key);
    if (wanted.empty() || cw::IsSetPiece(key)) {
        return;
    }
    auto const entries = gEntryList.ElementsOf(conv, 4096);
    if (!entries) {
        return;
    }
    Flipped* record = Find(conv);
    std::vector<int> done, refused, outOfRange;
    for (int const index : wanted) {
        void* const entry = entries->At(index);
        if (entry == nullptr) {
            outOfRange.push_back(index);
            continue;
        }
        if (gEntryAmbient.ReadBool(entry).value_or(false)) {
            continue;  // already ambient (the game's own, or ours from an earlier pass)
        }
        if (OffersWheel(conv, entry)) {
            refused.push_back(index);  // the live data differs from the scan: a wheel here would be lost
            continue;
        }
        if (WriteAmbient(entry, true)) {
            done.push_back(index);
        }
    }
    if (!refused.empty() || !outOfRange.empty()) {
        int& count = gRefused[key];
        if (count == 0) {
            log::Warn("convo flow: {}: {} entries not flagged ({}{}{}): the game's data differs from the scan's table", key,
                refused.size() + outOfRange.size(), refused.empty() ? "" : "a wheel at " + Ranges(refused),
                !refused.empty() && !outOfRange.empty() ? "; " : "", outOfRange.empty() ? "" : "no entry " + Ranges(outOfRange));
        }
        count += static_cast<int>(refused.size() + outOfRange.size());
    }
    if (done.empty()) {
        return;
    }
    if (record == nullptr) {
        gFlipped.push_back({conv, key, {}});
        record = &gFlipped.back();
    }
    record->entries.insert(record->entries.end(), done.begin(), done.end());
    std::sort(record->entries.begin(), record->entries.end());
    auto const range = action != nullptr ? gActionRange.Read<float>(action) : std::nullopt;
    log::Info("convo flow: {}: {} lines made ambient ({}) for {}: the game will play that start as barks, the player free{}", key, done.size(),
        Ranges(done), action != nullptr ? ue3::runtime::FullName(action) : std::string("?"),
        range && *range > 0.0f ? std::format(" within {:.1f} m of the speaker", *range / kUnitsPerMeter) : std::string());
}

// The world and which of its streaming levels are loaded and visible, as one number that changes with any chunk load.
std::uint64_t LevelsSignature() {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const world = controller != nullptr ? gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    std::uint64_t h = reinterpret_cast<std::uintptr_t>(world) * 1469598103934665603ull;
    if (auto const levels = world != nullptr ? gStreamingLevels.ElementsOf(world, 1024) : std::nullopt) {
        for (std::int32_t i = 0; i < levels->count; ++i) {
            ue3::Object* const level = *static_cast<ue3::Object* const*>(levels->At(i));
            if (level == nullptr) continue;
            std::uint64_t const loaded = gLevelLoaded.Read<ue3::Object*>(level).value_or(nullptr) != nullptr ? 2u : 0u;
            std::uint64_t const visible = gLevelVisible.ReadBool(level).value_or(false) ? 1u : 0u;
            h = (h ^ (static_cast<std::uint64_t>(i) * 4u + loaded + visible + 1u)) * 1099511628211ull;
        }
    }
    return h;
}

// Every loaded start action's conversation (as ConsoleRequest's Instances): the conversations a level's script can start.
void Rescan(char const* why) {
    ue3::Reflection& r = ue3::runtime::Get();
    std::int32_t const n = r.ObjectCount();
    int actions = 0;
    for (std::int32_t i = 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !ue3::IsA(o, gStartAction) || r.NameText(o).rfind("Default__", 0) == 0) {
            continue;
        }
        ++actions;
        ue3::Object* const conv = gActionConv.Read<ue3::Object*>(o).value_or(nullptr);
        if (conv != nullptr && r.IsAlive(conv)) {
            Flip(conv, o);
        }
    }
    ++gScans;
    gScanMs = GetTickCount64();
    log::Info("convo flow: scan {} ({}): {} start actions among {} objects; {} conversations carry our flags", gScans, why, actions, n, gFlipped.size());
}

// Puts every flag we set back (the mod unloads, the setting goes off).
void Restore(char const* why) {
    if (gFlipped.empty()) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    int put = 0, gone = 0;
    for (Flipped const& f : gFlipped) {
        if (!r.IsAlive(f.conversation) || cw::ConversationKey(ue3::runtime::FullName(f.conversation)) != f.key) {
            ++gone;  // the object was freed (a level change): nothing to put back
            continue;
        }
        auto const entries = gEntryList.ElementsOf(f.conversation, 4096);
        for (int const index : f.entries) {
            if (entries && WriteAmbient(entries->At(index), false)) {
                ++put;
            }
        }
    }
    log::Info("convo flow: {} flags put back on {} conversations ({} of them gone with their level): {}", put, gFlipped.size(), gone, why);
    gFlipped.clear();
    gLive = nullptr;
}

// True if every start of `conv` is ambient by the game's own data (none of them by our flags): a bark.
bool IsGameBark(ue3::Object* conv) {
    auto const entries = gEntryList.ElementsOf(conv, 4096);
    auto const startList = gStartingList.ElementsOf(conv, 512);
    if (!entries || !startList || startList->count <= 0) {
        return false;
    }
    Flipped const* const ours = Find(conv);
    for (std::int32_t i = 0; i < startList->count; ++i) {
        auto const* const index = static_cast<std::int32_t const*>(startList->At(i));
        void* const entry = index != nullptr ? entries->At(*index) : nullptr;
        if (entry == nullptr || !gEntryAmbient.ReadBool(entry).value_or(false)) {
            return false;  // an ordinary start: a conversation
        }
        if (ours != nullptr && std::find(ours->entries.begin(), ours->entries.end(), *index) != ours->entries.end()) {
            return false;  // ambient by our flag: a scene of the level, not a bark
        }
    }
    return true;
}

// BioWorldInfo's interruption of the current ambient conversation asks (engine/ConversationHooks; research/69 item 1). The
// game ends an ambient conversation for any conversation that wants to start, and refuses every newcomer while a
// conversation that is not ambient runs. A chain of ours was such a conversation before our flags, so the game's own barks
// (a bystander's line, a squadmate's remark) could never start over it; flowed, they would cut the scene off. They are
// refused, as the game refuses them during the conversation this chain was authored as. Another scene of the level (one of
// ours, or a conversation) is let through: refusing it could lose a scene's start, and with it its plot effects. The distance
// and owner tests (no newcomer) are the game's. Called on the game thread inside the game's code: reads only.
// True if `conv` plays as barks because of our flags: it is ambient and the line it is on is one we flagged. (Whether every
// start of the conversation was ordinary is the wrong question: sta70_garrus has an ambient start of the game's own beside the
// two we flag, and on 2026-11-09 that left its argument unprotected.)
Flipped const* OurScene(ue3::Object* conv) {
    Flipped const* const ours = conv != nullptr ? Find(conv) : nullptr;
    if (ours == nullptr || !gConvAmbient.ReadBool(conv).value_or(false)) {
        return nullptr;
    }
    auto const entry = gConvCurrentEntry.Read<std::int32_t>(conv);
    return entry && std::find(ours->entries.begin(), ours->entries.end(), *entry) != ours->entries.end() ? ours : nullptr;
}

bool RefuseInterrupt(void const* worldInfo, void const* newcomer) {
    if (newcomer == nullptr || !gSettings.protect || !gSettings.Active()) {
        return false;
    }
    ue3::Object* const world = const_cast<ue3::Object*>(static_cast<ue3::Object const*>(worldInfo));
    ue3::Object* const current = gCurrentConversation.Read<ue3::Object*>(world).value_or(nullptr);
    Flipped const* const ours = OurScene(current);
    if (ours == nullptr) {
        return false;
    }
    ue3::Object* const comer = const_cast<ue3::Object*>(static_cast<ue3::Object const*>(newcomer));
    ue3::Object* const action = gKismetStart.Read<ue3::Object*>(comer).value_or(nullptr);
    cw::NewcomerFacts facts;
    facts.sceneOurs = true;
    facts.same = current == comer;
    facts.gameBark = IsGameBark(comer);
    facts.hasStartAction = action != nullptr;
    cw::NewcomerRule const rule = cw::DecideNewcomer(facts, gSettings.newcomers);
    if (rule == cw::NewcomerRule::Through) {
        if (!facts.same) {
            gPendingLog.push_back(std::format("{} starts over the flowed scene {} and ends it: let through ({})",
                cw::ConversationKey(ue3::runtime::FullName(comer)), ours->key,
                gSettings.newcomers == cw::NewcomerRule::Through ? "worldFlowNewcomers = through" : "no start action to fire again later"));
        }
        return false;
    }
    std::string const name = cw::ConversationKey(ue3::runtime::FullName(comer));
    if (rule == cw::NewcomerRule::Hold) {
        auto const id = reinterpret_cast<std::uint64_t>(action);
        if (cw::HoldStart(gHeld, id, GetTickCount64())) {
            ++gScenesHeld;
            gHeldNames[id] = std::format("{} ({})", name, ue3::runtime::FullName(action));
            gPendingLog.push_back(std::format("{} wanted to start over the flowed scene {}: held, its start fires again when the scene and "
                                              "what follows it are over", name, ours->key));
        }
        return true;
    }
    if (facts.gameBark) {
        ++gBarksRefused;
        gPendingLog.push_back(std::format("the bark {} wanted to start over the flowed scene {}: refused, as the game refuses every "
                                          "conversation during the conversation that scene was authored as", name, ours->key));
    } else {
        ++gScenesRefused;
        gPendingLog.push_back(std::format("{} wanted to start over the flowed scene {}: refused (worldFlowNewcomers = refuse)", name, ours->key));
    }
    return true;
}

// Fires the oldest held start again once nothing that would refuse it has been playing for a second: no conversation, or only
// an ambient one that is no scene of ours (the level's follow-up to a scene, the Garrus wheel conversation, starts in the
// tick the scene ends and is waited out too). Held past three minutes, a start is dropped.
void ReleaseHeld() {
    if (gHeld.empty()) {
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const world = controller != nullptr ? gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    if (world == nullptr) {
        return;
    }
    ue3::Object* const current = gCurrentConversation.Read<ue3::Object*>(world).value_or(nullptr);
    bool const clear = current == nullptr || (gConvAmbient.ReadBool(current).value_or(false) && OurScene(current) == nullptr);
    std::uint64_t const now = GetTickCount64();
    if (!clear) {
        gFreeSinceMs = 0;
    } else if (gFreeSinceMs == 0) {
        gFreeSinceMs = now;
    }
    std::vector<std::uint64_t> expired;
    auto const id = cw::ReleaseStart(gHeld, clear && now - gFreeSinceMs >= 1000, now, 180000, &expired);
    for (std::uint64_t const e : expired) {
        log::Warn("convo flow: the held start of {} waited three minutes for the scene to end: dropped", gHeldNames[e]);
        gHeldNames.erase(e);
    }
    if (!id) {
        return;
    }
    std::string const name = gHeldNames[*id];
    gHeldNames.erase(*id);
    gFreeSinceMs = 0;  // the next held start waits for this one in turn
    auto* const action = reinterpret_cast<ue3::Object*>(*id);
    if (!ue3::runtime::IsAlive(action) || gForceActivate.Get() == nullptr) {
        log::Info("convo flow: the held start of {} is gone with its level: not fired", name);
        return;
    }
    ue3::Params p(ue3::runtime::Get(), gForceActivate.Get());
    bool const called = p.Valid() && p.Set("InputIdx", static_cast<std::int32_t>(0)) && ue3::runtime::Call(action, gForceActivate.Get(), p);
    ++gHeldFired;
    log::Info("convo flow: the flowed scene is over: the held start of {} fired again (ForceActivateInput(0) called {}); {} still held", name,
        called, gHeld.size());
}

// The live conversation: a chain of ours playing as barks (logged once), its interrupt range set as asked.
void WatchLive() {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const world = controller != nullptr ? gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    ue3::Object* const conv = world != nullptr ? gCurrentConversation.Read<ue3::Object*>(world).value_or(nullptr) : nullptr;
    if (conv == gLive) {
        return;
    }
    gLive = conv;
    if (conv == nullptr) {
        return;
    }
    Flipped const* const ours = Find(conv);
    bool const ambient = gConvAmbient.ReadBool(conv).value_or(false);
    if (ours == nullptr || !ambient) {
        return;
    }
    ++gPlayed;
    auto range = gConvRange.Read<float>(conv);
    std::string leash;
    if (gSettings.leashM != 0.0f) {
        float const want = gSettings.leashM < 0.0f ? -1.0f : gSettings.leashM * static_cast<float>(kUnitsPerMeter);
        if (float* const p = gConvRange.In<float>(conv)) {
            *p = want;
            write_journal::Wrote("ConversationFlow.interruptRange", p, sizeof(float));
            range = want;
            leash = " (set by worldFlowLeashM)";
        }
    }
    auto const fallback = gWorldRange.Read<float>(world);
    std::string bounds;
    if (range && *range < 0.0f) {
        bounds = "never interrupted by distance";
    } else if (range && *range > 0.0f) {
        bounds = std::format("interrupted beyond {:.1f} m of the speaker", *range / kUnitsPerMeter);
    } else if (fallback) {
        bounds = std::format("interrupted beyond the world's default {:.1f} m of the speaker", *fallback / kUnitsPerMeter);
    } else {
        bounds = "interrupt range unknown";
    }
    log::Info("convo flow: {} plays as barks (ambient): the player stays on foot; {}{}", ours->key, bounds, leash);
}

// A flowed scene's run as one line of numbers, for the sweep's judge (research/69 "The sweep: plan"): a chain plays in game
// mode 0, where conversation_world sees no conversation and writes no summary. The entries it passed through, in order,
// what came after it (the level's follow-up: often the same conversation again, as a conversation), and what was refused or
// held over it. Whether the entries are a whole play of the chain is the judge's to say: it has the conversation's graph.
void RestoreMode();

struct ChainRun {
    ue3::Object* conv = nullptr;
    std::string key;
    std::uint64_t startMs = 0;
    std::size_t flagged = 0;
    std::vector<std::int32_t> entries;
    std::uint32_t barks = 0;
    std::uint32_t held = 0;
    std::uint32_t refused = 0;
};
ChainRun gChain;

void EndChain(ue3::Object* next, char const* why) {
    std::string entries;
    for (std::int32_t const e : gChain.entries) {
        entries += std::format("{}{}", entries.empty() ? "" : ", ", e);
    }
    bool const follows = next != nullptr && ue3::runtime::IsAlive(next);
    log::Info("convo flow: summary {{\"name\": \"{}\", \"why\": \"{}\", \"seconds\": {:.1f}, \"flagged\": {}, \"entries\": [{}], "
              "\"next\": \"{}\", \"nextAmbient\": {}, \"barksRefused\": {}, \"scenesHeld\": {}, \"scenesRefused\": {}}}",
        gChain.key, why, static_cast<double>(GetTickCount64() - gChain.startMs) / 1000.0, gChain.flagged, entries,
        follows ? cw::ConversationKey(ue3::runtime::FullName(next)) : std::string(),
        follows && gConvAmbient.ReadBool(next).value_or(false) ? "true" : "false", gBarksRefused - gChain.barks, gScenesHeld - gChain.held,
        gScenesRefused - gChain.refused);
    gChain = {};
    RestoreMode();
}

// A flowed line's subtitle, through the game's own display (BioSubtitles, which the conversation GUI shows as it shows a
// conversation's): handed over every 0.25 s with a 0.6 s life while the line plays, so it goes with the line (research/69 "The
// subtitle of a line played as a bark").
cw::BarkSubtitleState gBarkSubtitle;
std::uint32_t gSubtitleLines = 0;
bool gToldNoSubtitles = false;
// The render mode: the game sets ambient (4) for a bark, which its GUI draws in the middle of the screen, where in VR it stayed
// in the eye images as head-locked text 6 pixels tall; bottom (3) is drawn in the GUI's movie, so it reaches the HUD panel with
// the rest of the game's UI (2026-11-11, snapshots 06-22-14 and 06-24-19). Set for the scene, the game's put back after it.
ue3::Object* gModeObject = nullptr;
std::uint8_t gModeWas = 0;
void BottomMode(ue3::Object* subtitles) {
    std::uint8_t* const mode = gSubtitlesMode.In<std::uint8_t>(subtitles);
    if (mode == nullptr || *mode == 3) {
        return;
    }
    if (gModeObject != subtitles) {
        gModeObject = subtitles;
        gModeWas = *mode;
    }
    *mode = 3;
    write_journal::Wrote("ConversationFlow.subtitleMode", mode, sizeof(*mode));
}
void RestoreMode() {
    if (gModeObject != nullptr && ue3::runtime::IsAlive(gModeObject)) {
        if (std::uint8_t* const mode = gSubtitlesMode.In<std::uint8_t>(gModeObject); mode != nullptr && *mode == 3) {
            *mode = gModeWas;  // (not ours any more if the game has set another since)
            write_journal::Wrote("ConversationFlow.subtitleMode", mode, sizeof(*mode));
        }
    }
    gModeObject = nullptr;
    gBarkSubtitle = {};
}
void ShowSubtitle(ue3::Object* conv, ue3::Object* world) {
    ue3::FString const* const text = conv != nullptr ? gConvSubtitle.In<ue3::FString>(conv) : nullptr;
    std::wstring_view const view = text != nullptr ? std::wstring_view(text->Chars()) : std::wstring_view();
    std::uint64_t const hash = view.empty() ? 0 : static_cast<std::uint64_t>(std::hash<std::wstring_view>{}(view)) | 1u;
    int const option = conv != nullptr ? gConvSubtitlesOption.Read<std::int32_t>(conv).value_or(1) : 1;
    std::uint64_t const before = gBarkSubtitle.text;
    double const nowS = static_cast<double>(GetTickCount64()) / 1000.0;
    if (!cw::ShowBarkSubtitle(gBarkSubtitle, gSettings.subtitles, option, hash, nowS, 0.25)) {
        return;
    }
    ue3::Object* const subtitles = world != nullptr ? gWorldSubtitles.Read<ue3::Object*>(world).value_or(nullptr) : nullptr;
    ue3::Function* const fn = gDisplaySubtitle.Get();
    if (subtitles == nullptr || !ue3::runtime::IsAlive(subtitles) || fn == nullptr || !ue3::memory::Ready()) {
        if (!gToldNoSubtitles) {
            gToldNoSubtitles = true;
            log::Warn("convo flow: a flowed line's subtitle can't be shown (no subtitle display, DisplaySubtitle or allocator)");
        }
        return;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    ue3::memory::OwnedString owned{std::wstring(view)};
    if (!owned.Valid() || !p.Valid() || !p.Set("sSubtitle", owned.Get()) || !p.Set("fDuration", 0.6f)) {
        return;
    }
    BottomMode(subtitles);
    ue3::runtime::Call(subtitles, fn, p);
    if (gBarkSubtitle.text != before) {
        if (gSubtitleLines++ == 0 || gSubtitleLines % 50 == 0) {
            log::Info("convo flow: {}'s line shown as a subtitle (the game draws none for barks): '{}' ({} lines so far)", gChain.key,
                ue3::memory::Narrow(owned.Get()), gSubtitleLines);
        }
    }
}

void WatchChain() {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const world = controller != nullptr ? gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    ue3::Object* const conv = world != nullptr ? gCurrentConversation.Read<ue3::Object*>(world).value_or(nullptr) : nullptr;
    if (gChain.conv != nullptr) {
        // Over when the world's conversation is another one, none, or this one started again as a conversation.
        if (conv != gChain.conv || !gConvAmbient.ReadBool(conv).value_or(false)) {
            EndChain(conv, conv == nullptr ? "it ended" : conv == gChain.conv ? "it began again as a conversation" : "another conversation began");
        } else {
            auto const entry = gConvCurrentEntry.Read<std::int32_t>(conv);
            if (entry && *entry >= 0 && (gChain.entries.empty() || gChain.entries.back() != *entry)) {
                gChain.entries.push_back(*entry);
            }
            ShowSubtitle(OurScene(conv) != nullptr ? conv : nullptr, world);  // (a line the game made ambient itself: its own way)
            return;
        }
    }
    if (Flipped const* const ours = OurScene(conv)) {
        gChain.conv = conv;
        gChain.key = ours->key;
        gChain.startMs = GetTickCount64();
        gChain.flagged = ours->entries.size();
        gChain.barks = gBarksRefused;
        gChain.held = gScenesHeld;
        gChain.refused = gScenesRefused;
        if (auto const entry = gConvCurrentEntry.Read<std::int32_t>(conv); entry && *entry >= 0) {
            gChain.entries.push_back(*entry);
        }
    }
}

} // namespace

void Tick() {
    RefreshSettings();
    if (!ue3::runtime::Ready()) {
        return;
    }
    engine::conversation_hooks::SetInterruptProvider(gSettings.Active() && gSettings.protect ? &RefuseInterrupt : nullptr);
    if (!gPendingLog.empty()) {
        std::vector<std::string> lines;
        lines.swap(gPendingLog);
        for (std::string const& line : lines) {
            log::Info("convo flow: {}", line);
        }
    }
    if (!gSettings.Active()) {
        Restore("the setting is off");
        return;
    }
    std::uint64_t const now = GetTickCount64();
    // A chunk streamed in (or a level loaded) brings its start actions: scan as soon as the streaming levels' loaded and
    // visible set changes (at most twice a second while loads go on: a chunk's trigger can fire within a second of its load,
    // and on 2026-11-09 the Executor scene began 3 s before the periodic scan flagged it; the object table's count never
    // moved for that chunk, its objects took freed slots), else every 10 s; the first scan at once.
    std::uint64_t const levels = LevelsSignature();
    bool const changed = levels != gLevelsAtScan;
    if (gScans == 0 || (changed && now - gLastRescanMs >= 500) || now - gScanMs >= 10000) {
        gLastRescanMs = now;
        gLevelsAtScan = levels;
        Rescan(gScans == 0 ? "first" : changed ? "the streaming levels changed" : "periodic");
    }
    WatchLive();
    WatchChain();
    ReleaseHeld();
}

std::string Command(std::string const& rest) {
    std::string const word = ini::Lower(ini::Trim(rest));
    if (word == "rescan") {
        Rescan("mevr convo flow rescan");
        return std::format("convo flow: rescanned; {} conversations carry our flags", gFlipped.size());
    }
    if (word == "restore") {
        std::size_t const n = gFlipped.size();
        Restore("mevr convo flow restore");
        return std::format("convo flow: flags put back on {} conversations (the next scan sets them again while the setting is on)", n);
    }
    std::string out = std::format("convo flow: {}; {} scans, {} conversations carry our flags, {} chain plays seen as barks{}", gSettingsText,
        gScans, gFlipped.size(), gPlayed, gRefused.empty() ? "" : std::format(", {} conversations with entries refused", gRefused.size()));
    if (word == "list" || word.empty()) {
        for (Flipped const& f : gFlipped) {
            out += std::format("\n  {}: {}", f.key, Ranges(f.entries));
        }
        for (auto const& [key, count] : gRefused) {
            out += std::format("\n  {}: {} entries refused (the data differs from the scan)", key, count);
        }
    }
    return out;
}

std::string StatusJson() {
    return std::format("{{\"active\": {}, \"scans\": {}, \"conversations\": {}, \"played\": {}}}", gSettings.Active() ? "true" : "false", gScans,
        gFlipped.size(), gPlayed);
}

void LogStats() {
    if (gSettings.Active() || !gFlipped.empty()) {
        log::Info("convo flow: {} scans, {} conversations carry our flags, {} chain plays seen as barks; over them {} barks refused, {} scenes "
                  "held ({} fired again, {} waiting), {} refused", gScans, gFlipped.size(), gPlayed, gBarksRefused, gScenesHeld, gHeldFired,
            gHeld.size(), gScenesRefused);
    }
}

void Shutdown() {
    engine::conversation_hooks::SetInterruptProvider(nullptr);
    if (!gHeld.empty()) {
        log::Warn("convo flow: {} held starts dropped (the mod unloads)", gHeld.size());
    }
    gHeld.clear();
    gHeldNames.clear();
    gPendingLog.clear();
    if (gChain.conv != nullptr) {
        EndChain(nullptr, "the mod unloads");
    }
    Restore("the mod unloads");
}

} // namespace mevr::game::conversation_flow
