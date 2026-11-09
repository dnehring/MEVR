// SPDX-License-Identifier: MIT
// LE1 conversations that flow: the chain starts' lines flagged ambient (game/ConversationFlow.hpp, research/59).
#include "game/ConversationFlow.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "engine/SettingsFile.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <map>
#include <span>
#include <string>
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
ue3::FieldRef gReplyList{"SFXGame.BioConversation", "m_ReplyList"};
ue3::FieldRef gReplyType{gReplyList, "ReplyType"};
// The live conversation (as game/ConversationWorld reads it) and its interrupt range.
ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gCurrentConversation{"SFXGame.BioWorldInfo", "m_oCurrentConversation"};
ue3::FieldRef gConvAmbient{"SFXGame.BioConversation", "m_bAmbient"};
ue3::FieldRef gConvRange{"SFXGame.BioConversation", "m_fInterruptRange"};
ue3::FieldRef gWorldRange{"SFXGame.BioWorldInfo", "m_fConversationInterruptDistance"};  // the world's default (BIOGame.ini), used when the start's is 0

constexpr double kUnitsPerMeter = 100.0;

// ---- Settings ([Cinematics] in LE1MEVR.ini, read every second) ---------------------------------------------------------------
struct Settings {
    bool world = false;   // conversations = world (the flow belongs to the first-person mode)
    bool flow = true;     // worldFlow = all | off
    float leashM = 0.0f;  // worldFlowLeashM: 0 the game's interrupt range, -1 never interrupted, else metres from the speaker
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
    std::string const text = std::format("worldFlow = {} ({}), worldFlowLeashM = {}", s.flow ? "all" : "off",
        s.world ? "conversations = world: active" : "not the world mode: inactive",
        s.leashM < 0.0f ? std::string("never interrupted") : s.leashM == 0.0f ? std::string("the game's") : std::format("{:.1f} m", s.leashM));
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
std::int32_t gObjectsAtScan = 0;
std::uint64_t gScanMs = 0;
std::uint64_t gLastRescanMs = 0;
int gScans = 0;
std::map<std::string, int> gRefused;  // conversation key -> entries refused (the live data differs from the scan)
ue3::Object* gLive = nullptr;         // the live conversation last seen
std::uint32_t gPlayed = 0;            // chain plays seen as ambient

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
    gObjectsAtScan = n;
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

} // namespace

void Tick() {
    RefreshSettings();
    if (!ue3::runtime::Ready()) {
        return;
    }
    if (!gSettings.Active()) {
        Restore("the setting is off");
        return;
    }
    std::uint64_t const now = GetTickCount64();
    std::int32_t const objects = ue3::runtime::Get().ObjectCount();
    // A level or chunk load creates thousands of objects: scan 2 s after the table grew (its actions and conversations are
    // there by then), else every 10 s; the first scan at once.
    bool const grew = std::abs(objects - gObjectsAtScan) >= 1000;
    if (gScans == 0 || (grew && now - gLastRescanMs >= 2000) || now - gScanMs >= 10000) {
        gLastRescanMs = now;
        Rescan(gScans == 0 ? "first" : grew ? "the object table changed" : "periodic");
    }
    WatchLive();
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
        log::Info("convo flow: {} scans, {} conversations carry our flags, {} chain plays seen as barks", gScans, gFlipped.size(), gPlayed);
    }
}

void Shutdown() {
    Restore("the mod unloads");
}

} // namespace mevr::game::conversation_flow
