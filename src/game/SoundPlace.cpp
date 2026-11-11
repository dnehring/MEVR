// SPDX-License-Identifier: MIT
// Where sounds come from on foot: people talking near the player and his own gun (game/SoundPlace.hpp, research/69 A1, A2).
#include "game/SoundPlace.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/ik/Rigid.hpp"
#include "core/vr/SoundPlace.hpp"
#include "engine/Skeleton.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/VoicePlace.hpp"
#include "game/ConversationWorld.hpp"
#include "game/Player.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mevr::game::sound_place {

namespace {

namespace sp = vr::sound_place;
namespace cw = vr::conversation_world;
namespace vp = engine::voice_place;

constexpr double kUnitsPerMeter = 100.0;  // research/30: life-size at 100 units per metre
constexpr std::uint8_t kModeOnFoot = 0;

ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gWorldPawns{"Engine.WorldInfo", "PawnList"};
ue3::FieldRef gNextPawnOf{"Engine.Pawn", "NextPawn"};
ue3::FieldRef gActorHidden{"Engine.Actor", "bHidden"};

// ---- Settings ([Audio] in LE1MEVR.ini, the mod's keys; read every second) -----------------------------------------------------
struct Settings {
    bool overheard = true;
    bool gun = true;
    float withinM = 8.0f;
    float keepM = 10.0f;
    float nearM = 2.0f;
    float falloffDb = 3.0f;
    float floorDb = 6.0f;
    float gain = 1.0f;
    int max = 16;
};
Settings gSettings;
std::uint64_t gSettingsMs = 0;
std::string gSettingsText;
std::optional<bool> gOverheardOverride;  // `mevr voices overheard on|off` (this session)
std::optional<bool> gGunOverride;

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsMs != 0 && now - gSettingsMs < 1000) {
        return;
    }
    gSettingsMs = now;
    ini::Loaded const loaded = ini::Read(engine::SettingsFile());
    Settings s;
    if (auto const text = ini::Get(loaded.file, "Audio", "overheard")) {
        s.overheard = sp::ParseOnOff(*text, s.overheard);
    }
    if (auto const text = ini::Get(loaded.file, "Audio", "gun")) {
        s.gun = sp::ParseOnOff(*text, s.gun);
    }
    auto number = [&](char const* key, float fallback, float lo, float hi) {
        auto const v = ini::GetNumber(loaded.file, "Audio", key);
        return v ? static_cast<float>(std::clamp(*v, static_cast<double>(lo), static_cast<double>(hi))) : fallback;
    };
    s.withinM = number("overheardWithinM", s.withinM, 0.0f, 100.0f);
    s.keepM = number("overheardKeepM", s.keepM, 0.0f, 100.0f);
    s.nearM = number("overheardNearM", s.nearM, 0.1f, 50.0f);
    s.falloffDb = number("overheardFalloffDb", s.falloffDb, 0.0f, 12.0f);
    s.floorDb = number("overheardFloorDb", s.floorDb, 0.0f, 40.0f);
    s.gain = number("overheardGain", s.gain, 0.0f, 4.0f);
    s.max = static_cast<int>(number("overheardMax", static_cast<float>(s.max), 1.0f, static_cast<float>(vp::kMaxPerPublisher)));
    if (gOverheardOverride) {
        s.overheard = *gOverheardOverride;
    }
    if (gGunOverride) {
        s.gun = *gGunOverride;
    }
    std::string const text = std::format("overheard {} (within {:.1f} m, kept to {:.1f}; full within {:.1f} m, {:.1f} dB a doubling, at most "
                                         "{:.1f} dB, gain {:.2f}, at most {} at once), gun {}",
        s.overheard ? "on" : "off", s.withinM, s.keepM, s.nearM, s.falloffDb, s.floorDb, s.gain, s.max, s.gun ? "on" : "off");
    if (text != gSettingsText) {
        gSettingsText = text;
        log::Info("sound place: [Audio] {}", text);
    }
    gSettings = s;
}

sp::Limits LimitsFor(Settings const& s) {
    sp::Limits limits;
    limits.voice.withinUnits = s.withinM * kUnitsPerMeter;
    limits.voice.keepUnits = s.keepM * kUnitsPerMeter;
    limits.voice.nearUnits = s.nearM * kUnitsPerMeter;
    limits.voice.falloffDb = s.falloffDb;
    limits.voice.floorDb = s.floorDb;
    limits.max = s.max;
    return limits;
}

std::string NameOf(ue3::Object const* o) {
    if (o == nullptr || !ue3::runtime::Ready() || !ue3::runtime::IsAlive(o)) {
        return o == nullptr ? std::string("none") : std::string("(gone)");
    }
    return ue3::runtime::Get().NameText(o);
}

double Seconds() {
    return static_cast<double>(GetTickCount64()) / 1000.0;
}

// The head bone of a pawn's mesh in the world (cached per skeletal mesh asset), as game/ConversationWorld finds it.
std::map<ue3::Object const*, int> gHeadBones;

std::optional<ik::Vec3> HeadOf(ue3::Object* pawn) {
    ue3::Object* const mesh = player::Mesh(pawn);
    ue3::Object* const asset = player::SkeletalMesh(mesh);
    if (asset == nullptr) {
        return std::nullopt;
    }
    auto it = gHeadBones.find(asset);
    if (it == gHeadBones.end()) {
        int index = -1;
        if (auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh)) {
            for (std::size_t i = 0; i < skeleton->names.size() && index < 0; ++i) {
                if (ini::Lower(skeleton->names[i]) == "head") {
                    index = static_cast<int>(i);
                }
            }
        }
        it = gHeadBones.emplace(asset, index).first;
    }
    int const bone = it->second;
    auto const bases = engine::skeleton::SpaceBases(mesh);
    if (bone < 0 || static_cast<std::size_t>(bone) >= bases.size()) {
        return std::nullopt;
    }
    ik::Affine const toWorld = engine::skeleton::LocalToWorld(mesh);
    ik::Transform const t = engine::skeleton::ToTransform(bases[static_cast<std::size_t>(bone)]);
    return ik::TransformPoint(toWorld, t.translation);
}

// ---- State -----------------------------------------------------------------------------------------------------------------
sp::Overheard gOverheard;
bool gOverheardSet = false;
bool gGunSet = false;
std::string gStoodDown = "not started";  // why nothing is named now ("" while on foot)
ue3::Object* gGunActor = nullptr;
int gNamedNow = 0;                        // people named this tick
std::map<std::uint64_t, double> gLastLogged;  // a person's "speaks from where it stands" line, at most once in 10 s
unsigned gSpeakers = 0;                   // such lines since the start
unsigned long long gGunHeard = 0;          // ticks in which a source of the gun's was placed
bool gGunLogged = false;
bool gCensusPending = false;
vp::Totals gTotalsAtLog{};

void ClearOverheard() {
    if (gOverheardSet) {
        vp::Set(vp::Publisher::Overheard, nullptr, 0);
        gOverheardSet = false;
    }
    gOverheard.Clear();
    gNamedNow = 0;
}

void ClearGun() {
    if (gGunSet) {
        vp::Set(vp::Publisher::Gun, nullptr, 0);
        gGunSet = false;
    }
    gGunActor = nullptr;
}

void StandDown(std::string why) {
    ClearOverheard();
    ClearGun();
    if (why != gStoodDown) {
        if (!why.empty()) {
            log::Info("sound place: stands down ({}): the game's own mix", why);
        }
        gStoodDown = std::move(why);
    }
}

void TickOverheard(ue3::Object* pawn, ik::Vec3 const& ear) {
    if (!gSettings.overheard) {
        ClearOverheard();
        return;
    }
    sp::Limits const limits = LimitsFor(gSettings);
    double const reach = sp::CandidateReachUnits(limits);
    std::vector<sp::Candidate> nearby;
    ue3::Object* const world = gActorWorldInfo.Read<ue3::Object*>(pawn).value_or(nullptr);
    int guard = 0;
    for (ue3::Object* p = world != nullptr ? gWorldPawns.Read<ue3::Object*>(world).value_or(nullptr) : nullptr; p != nullptr && guard < 512;
         p = gNextPawnOf.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        if (p == pawn || !player::IsBioPawn(p) || !ue3::runtime::IsAlive(p) || gActorHidden.ReadBool(p).value_or(true)) {
            continue;  // never the player's own pawn
        }
        auto const at = player::Location(p);
        if (!at) {
            continue;
        }
        ik::Vec3 const place{at->x, at->y, at->z};
        if (ik::Length(place - ear) > reach) {
            continue;
        }
        ik::Vec3 const head = HeadOf(p).value_or(place + ik::Vec3{0.0, 0.0, 70.0});
        nearby.push_back({reinterpret_cast<std::uint64_t>(p), {head.x, head.y, head.z}});
    }
    double const now = Seconds();
    // Someone near who isn't on the list (a crowd past its cap) and was heard in the centre: speaking, so named first next time
    // (else only those already named could ever count as heard; the Codex review, 2026-11-11).
    void const* unnamed[vp::kMaxPerPublisher] = {};
    int const nUnnamed = vp::TakeHeardUnnamed(unnamed, vp::kMaxPerPublisher);
    for (int i = 0; i < nUnnamed; ++i) {
        auto const id = reinterpret_cast<std::uint64_t>(unnamed[i]);
        if (std::any_of(nearby.begin(), nearby.end(), [&](sp::Candidate const& c) { return c.id == id; })) {
            gOverheard.Heard(id, Seconds());
        }
    }
    // Who spoke since the last tick (the hook placed one of their sources): they are named first in a crowd.
    void const* heard[vp::kMaxPerPublisher] = {};
    int const nHeard = vp::TakeHeard(vp::Publisher::Overheard, heard, vp::kMaxPerPublisher);
    for (int i = 0; i < nHeard; ++i) {
        auto const id = reinterpret_cast<std::uint64_t>(heard[i]);
        gOverheard.Heard(id, now);
        auto const last = gLastLogged.find(id);
        if (last == gLastLogged.end() || now - last->second >= 10.0) {
            gLastLogged[id] = now;
            ++gSpeakers;
            auto* const o = reinterpret_cast<ue3::Object*>(id);
            double distance = 0.0;
            for (sp::Candidate const& c : nearby) {
                if (c.id == id) {
                    distance = std::hypot(c.head.x - ear.x, c.head.y - ear.y, c.head.z - ear.z);
                }
            }
            log::Info("sound place: {} speaks from where it stands ({:.1f} m from the player; its sources the game had left in the centre)",
                NameOf(o), distance / kUnitsPerMeter);
        }
    }
    std::erase_if(gLastLogged, [&](auto const& entry) { return now - entry.second > 60.0; });
    std::vector<sp::Pick> const picks = gOverheard.Step({ear.x, ear.y, ear.z}, nearby, now, limits);
    vp::Voice voices[vp::kMaxPerPublisher];
    int count = 0;
    for (sp::Pick const& pick : picks) {
        if (count >= vp::kMaxPerPublisher) {
            break;
        }
        vp::Voice& v = voices[count++];
        v.actor = reinterpret_cast<void const*>(pick.id);
        v.at[0] = static_cast<float>(pick.at.x);
        v.at[1] = static_cast<float>(pick.at.y);
        v.at[2] = static_cast<float>(pick.at.z);
        v.gain = static_cast<float>(pick.gain) * gSettings.gain;
        v.centredOnly = true;  // the game's own placed sounds of theirs keep its mix
    }
    vp::Set(vp::Publisher::Overheard, voices, count);
    gOverheardSet = true;
    gNamedNow = count;
}

void TickGun() {
    ue3::Object* const weapon = gSettings.gun ? weapon::HeldObject() : nullptr;
    auto const muzzle = weapon != nullptr ? weapon::CurrentAsDrawn() : std::nullopt;
    if (weapon == nullptr || !muzzle) {
        ClearGun();
        return;
    }
    if (weapon != gGunActor) {
        gGunActor = weapon;
        log::Info("sound place: the sounds of the player's {} ({}) come from its muzzle", NameOf(weapon), weapon::Status());
    }
    vp::Voice v;
    v.actor = weapon;
    v.at[0] = static_cast<float>(muzzle->location.x);
    v.at[1] = static_cast<float>(muzzle->location.y);
    v.at[2] = static_cast<float>(muzzle->location.z);
    vp::Set(vp::Publisher::Gun, &v, 1);
    gGunSet = true;
    void const* heard[1] = {};
    if (vp::TakeHeard(vp::Publisher::Gun, heard, 1) > 0) {
        ++gGunHeard;
        if (!gGunLogged) {
            gGunLogged = true;
            log::Info("sound place: a sound of the player's gun was placed at its muzzle: its audio component belongs to the weapon actor "
                      "({})", NameOf(weapon));
        }
    }
}

void ReportCensus() {
    bool full = false;
    std::vector<vp::CensusEntry> const entries = vp::TakeCensus(&full);
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = player::Pawn(controller);
    ue3::Object* const weapon = weapon::HeldObject();
    log::Info("voice census: {} sources{} (owner, component, cue; whether the game placed it; updates; placed by us):", entries.size(),
        full ? std::format(" (full at {}: more were not counted)", vp::kCensusEntries) : std::string());
    for (vp::CensusEntry const& e : entries) {
        auto const* const owner = static_cast<ue3::Object const*>(e.owner);
        std::string who = owner == nullptr ? std::string("no owner") : ue3::runtime::IsAlive(owner) ? ue3::runtime::FullName(owner) : "(gone)";
        if (owner != nullptr && owner == pawn) {
            who += " [the player's pawn]";
        } else if (owner != nullptr && owner == weapon) {
            who += " [the player's weapon]";
        }
        auto const* const component = static_cast<ue3::Object const*>(e.component);
        auto const* const cue = static_cast<ue3::Object const*>(e.cue);
        log::Info("voice census:   {} | {} | {} | {} (component spatialization {}) | {} updates | {}", who,
            component != nullptr && ue3::runtime::IsAlive(component) ? NameOf(component) : std::string("?"),
            cue != nullptr && ue3::runtime::IsAlive(cue) ? ue3::runtime::FullName(cue) : std::string("no cue"),
            e.spatialized ? "placed by the game" : "in the centre", e.componentSpatialization, e.updates,
            e.placed != 0 ? std::format("{} placed for {}", e.placed, vp::Name(static_cast<vp::Publisher>(e.placedBy))) : std::string("left alone"));
    }
}

} // namespace

bool WantsHook() {
    RefreshSettings();
    return gSettings.overheard || gSettings.gun;
}

void Tick(double /*deltaSeconds*/) {
    RefreshSettings();
    if (gCensusPending && !vp::CensusRunning()) {
        gCensusPending = false;
        ReportCensus();
    }
    if (!vp::Installed()) {
        StandDown("the sound source hook is not in");
        return;
    }
    if (!gSettings.overheard && !gSettings.gun) {
        StandDown("[Audio] overheard = 0 and gun = 0");
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = player::BioPawn(controller);
    auto const mode = player::GameMode(controller);
    if (pawn == nullptr || !mode || *mode != kModeOnFoot) {
        StandDown(pawn == nullptr ? "no player on foot" : std::format("game mode {}", mode ? static_cast<int>(*mode) : -1));
        return;
    }
    // The view must be the player's own: a first-person conversation names its own voices, a scripted moment holds the view
    // elsewhere.
    if (conversation_world::InWorld() || conversation_world::MomentViewOn() || conversation_world::BodyAway().on) {
        StandDown("a conversation or a moment holds the view");
        return;
    }
    auto const pov = player::CameraPov(player::Camera(controller));
    if (!pov || pov->location == nullptr) {
        StandDown("no camera");
        return;
    }
    if (!gStoodDown.empty()) {
        log::Info("sound place: on foot: {}{}", gSettings.overheard ? "people near the player heard from where they stand" : "",
            gSettings.gun ? (gSettings.overheard ? ", the gun from its muzzle" : "the gun from its muzzle") : "");
        gStoodDown.clear();
    }
    ik::Vec3 const ear{pov->location->x, pov->location->y, pov->location->z};
    TickOverheard(pawn, ear);
    TickGun();
}

std::string Command(std::string const& rest) {
    std::istringstream words(rest);
    std::string verb;
    words >> verb;
    verb = ini::Lower(verb);
    if (verb == "census") {
        double seconds = 10.0;
        std::string value;
        if (words >> value) {
            seconds = ini::Number(value).value_or(seconds);
        }
        if (!vp::Installed()) {
            return "voices: the sound source hook is not in ([Audio] overheard and gun both off, and no first-person conversation asked for)";
        }
        vp::StartCensus(seconds);
        gCensusPending = true;
        return std::format("voices: counting every sound source for {:.1f} s; the log's `voice census:` lines list them (fire the gun now "
                           "to see who owns its sound)", std::clamp(seconds, 0.5, 120.0));
    }
    if (verb == "overheard" || verb == "gun") {
        std::string value;
        words >> value;
        bool const on = sp::ParseOnOff(value, true);
        (verb == "gun" ? gGunOverride : gOverheardOverride) = on;
        gSettingsMs = 0;
        RefreshSettings();
        return std::format("voices: {} {} for this session", verb, on ? "on" : "off");
    }
    vp::Totals const t = vp::SinceInstall();
    return std::format("voices: hook {}; {}; named now: {} people, the gun {}; since the hook went in: {} source updates, placed {} for the "
                       "conversation, {} for people near, {} for the gun, {} of the people near left to the game (placed by it); "
                       "{} speakers heard; [Audio] {}",
        vp::Installed() ? "in" : "not in", gStoodDown.empty() ? std::string("on foot") : "standing down (" + gStoodDown + ")", gNamedNow,
        gGunSet ? NameOf(gGunActor) : std::string("not named"), t.updates, t.placedBy[0], t.placedBy[1], t.placedBy[2], t.leftPlaced,
        gSpeakers, gSettingsText);
}

std::string StatusJson() {
    vp::Totals const t = vp::SinceInstall();
    return std::format("{{\"hook\": {}, \"onFoot\": {}, \"overheard\": {}, \"gun\": {}, \"named\": {}, \"gunNamed\": {}, \"speakers\": {}, "
                       "\"placedOverheard\": {}, \"placedGun\": {}, \"leftPlaced\": {}, \"gunHeardTicks\": {}}}",
        vp::Installed() ? "true" : "false", gStoodDown.empty() ? "true" : "false", gSettings.overheard ? "true" : "false",
        gSettings.gun ? "true" : "false", gNamedNow, gGunSet ? "true" : "false", gSpeakers, t.placedBy[1], t.placedBy[2], t.leftPlaced,
        gGunHeard);
}

void LogStats() {
    if (!vp::Installed()) {
        return;
    }
    vp::Totals const t = vp::SinceInstall();
    unsigned long long const overheard = t.placedBy[1] - gTotalsAtLog.placedBy[1];
    unsigned long long const gun = t.placedBy[2] - gTotalsAtLog.placedBy[2];
    unsigned long long const left = t.leftPlaced - gTotalsAtLog.leftPlaced;
    if (overheard != 0 || gun != 0 || left != 0 || gNamedNow != 0) {
        log::Info("sound place: {} people near named now; this period {} source updates placed for them ({} of theirs the game placed "
                  "itself, left alone), {} for the gun{}", gNamedNow, overheard, left, gun,
            gGunSet ? std::format(" ({})", NameOf(gGunActor)) : std::string());
    }
    gTotalsAtLog = t;
}

void Shutdown() {
    vp::Set(vp::Publisher::Overheard, nullptr, 0);
    vp::Set(vp::Publisher::Gun, nullptr, 0);
    gOverheardSet = gGunSet = false;
    gOverheard.Clear();
    gGunActor = nullptr;
    gHeadBones.clear();
    gLastLogged.clear();
    gStoodDown = "not started";
    gCensusPending = false;
}

} // namespace mevr::game::sound_place
