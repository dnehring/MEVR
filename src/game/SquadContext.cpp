// SPDX-License-Identifier: MIT
#include "game/SquadContext.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/PowerWheel.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/Interact.hpp"
#include "game/Player.hpp"
#include "game/Sights.hpp"
#include "game/SquadWeapons.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>

namespace mevr::game::squad_context {

namespace sc = vr::squad_context;
namespace json = vr::test_request;

namespace {

ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gPowers{"SFXGame.BioEpicPawnBehavior", "m_powers"};
ue3::FieldRef gPowerName{"SFXGame.BioPower", "m_nmPower"};
ue3::FieldRef gCooldownRemaining{"SFXGame.BioPower", "m_fCooldownRemaining"};
ue3::FieldRef gActorGameName{"SFXGame.BioActorBehavior", "ActorGameName"};
ue3::FieldRef gActorType{"SFXGame.BioActorBehavior", "m_oActorType"};
ue3::FieldRef gTypeGameName{"SFXGame.BioActorType", "ActorGameName"};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList"};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn"};
ue3::FieldRef gLastRenderTime{"Engine.Actor", "LastRenderTime"};
ue3::FieldRef gTimeSeconds{"Engine.WorldInfo", "TimeSeconds"};

ue3::FunctionRef gIsHostileFn{"SFXGame.BioPawn.IsHostile"};
ue3::FunctionRef gIsDeadFn{"SFXGame.BioPawn.IsDead"};
ue3::FunctionRef gGetPercentHealthFn{"SFXGame.BioEpicPawnBehavior.GetPercentHealth"};
ue3::FunctionRef gIsInCombatActionStateFn{"SFXGame.BioPawn.IsInCombatActionState"};
// The level's cover (research/54 "Landmarks"): CoverLink slots, each at the link's place plus its offset in the link's axes
// (the rows are game/SquadOrders' retreat's).
ue3::ClassRef gCoverLink{"Engine.CoverLink"};
ue3::FieldRef gCoverSlots{"Engine.CoverLink", "Slots"};
ue3::FieldRef gCoverSlotOffset{gCoverSlots, "LocationOffset"};
ue3::FieldRef gCoverDisabled{"Engine.CoverLink", "bDisabled"};

constexpr int kMaxPawns = 512;
constexpr double kCoverRefreshS = 2.0;  // the cover is static: a walk over the objects this often is enough

struct Known {  // a squadmate seen this session: the roster
    char letter = 0;
    std::string name;
    std::uint8_t weapons = 0;
    std::vector<std::pair<std::string, std::string>> powers;  // code, spoken
};

sc::Scene gScene;
sc::Grouper gGrouper;
sc::Landmarker gLandmarker;
std::vector<sc::Cover> gCover;  // the cover links within range at the last walk
double gSinceCover = 1e9;
unsigned gCoverWalks = 0, gCoverLinksSeen = 0;
sc::PoseHistory gPoses(4.0);
double gClock = 0.0;      // the mod's seconds, the history's clock
double gSinceRead = 1e9;
bool gOnFoot = false;
std::string gWhyNot;
std::map<char, Known> gRoster;
std::map<std::string, std::string> gPowerCodes;  // internal power name -> code
std::map<std::string, ue3::Name> gPowerNames;    // code -> the game's own name (BioPower.m_nmPower), for a power order
std::map<std::string, int> gLoggedTypes;        // type words seen (for the log)
unsigned gReads = 0, gSamples = 0;
bool gToldNoLayer = false;

ue3::Object* ObjectOf(ue3::FieldRef& field, ue3::Object const* object) {
    if (object == nullptr) return nullptr;
    auto const v = field.Read<ue3::Object*>(object);
    return v ? *v : nullptr;
}

std::string StringOf(ue3::FieldRef& field, ue3::Object const* object) {
    ue3::FString const* const text = object != nullptr ? field.In<ue3::FString>(object) : nullptr;
    return text != nullptr && text->count > 0 ? ue3::memory::Narrow(*text) : std::string();
}

bool CallBool(ue3::Object* object, ue3::Function* fn) {
    if (object == nullptr || fn == nullptr) return false;
    ue3::Params parms(ue3::runtime::Get(), fn);
    ue3::runtime::Call(object, fn, parms);
    return parms.ReturnBool().value_or(false);
}

char LetterOfClass(int classId) {
    switch (classId) {
    case 6: return 'L';
    case 7: return 'W';
    case 8: return 'G';
    case 9: return 'T';
    case 11: return 'A';
    case 12: return 'K';
    default: return 0;
    }
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The spoken name of a power: the power wheel's table where it knows the label ("TK_Throw" is "Throw"), else the internal
// name as words without its discipline prefix and its NPC/delivery suffix ("BW_Carnage_NPC" -> "carnage", "EL_EMP_Delivery" ->
// "emp"). Until 2026-11-09 the letters went in whole ("Ar Shield Boost", "Kro Pistol Whip"): 20 of the recognizer's key
// terms were words nobody says, and the parser's power words with them.
std::string Spoken(std::string const& internal) {
    if (vr::power_wheel::PowerInfo const* const info = vr::power_wheel::FindPower(internal.c_str()); info != nullptr && info->name != nullptr) {
        return Lower(info->name);
    }
    std::string trimmed = internal;
    if (auto const us = trimmed.find('_'); us != std::string::npos && us >= 2 && us <= 3 &&
        std::all_of(trimmed.begin(), trimmed.begin() + static_cast<std::ptrdiff_t>(us), [](char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; })) {
        trimmed = trimmed.substr(us + 1);
    }
    for (char const* suffix : {"_NPC", "_Npc", "_Delivery", "_Player"}) {
        if (trimmed.size() > std::string_view(suffix).size() && trimmed.ends_with(suffix)) trimmed = trimmed.substr(0, trimmed.size() - std::string_view(suffix).size());
    }
    std::string out;
    for (std::size_t i = 0; i < trimmed.size(); ++i) {
        char const c = trimmed[i];
        if (c == '_') {
            out += ' ';
        } else if (std::isupper(static_cast<unsigned char>(c)) && i > 0 && std::islower(static_cast<unsigned char>(trimmed[i - 1]))) {
            out += ' ';
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

// A three-letter code for a power, unique this session: the first letters of its words, else its consonants.
std::string CodeOf(std::string const& internal) {
    if (auto const it = gPowerCodes.find(internal); it != gPowerCodes.end()) return it->second;
    std::string letters;
    for (char c : internal) {
        if (std::isalpha(static_cast<unsigned char>(c))) letters += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    std::string code;
    for (char c : letters) {
        if (code.size() >= 3) break;
        if (code.empty() || (c != 'A' && c != 'E' && c != 'I' && c != 'O' && c != 'U')) code += c;
    }
    while (code.size() < 3 && code.size() < letters.size()) code += letters[code.size()];
    if (code.empty()) code = "PWR";
    std::string candidate = code;
    for (int n = 2; n < 10; ++n) {
        bool taken = false;
        for (auto const& [name, c] : gPowerCodes) taken = taken || c == candidate;
        if (!taken) break;
        candidate = code.substr(0, 2) + static_cast<char>('0' + n);
    }
    gPowerCodes[internal] = candidate;
    return candidate;
}

// The powers on a pawn's behaviour: the game's own list, with the cooldown left.
void ReadPowers(ue3::Object* behavior, sc::Member& member, Known& known) {
    member.powers.clear();
    auto const list = gPowers.ElementsOf(behavior);
    if (!list) return;
    for (std::int32_t i = 0; i < list->count && i < 64; ++i) {
        void* const slot = list->At(i);
        ue3::Object* const power = slot != nullptr ? *static_cast<ue3::Object**>(slot) : nullptr;
        if (power == nullptr || !ue3::runtime::IsAlive(power)) continue;
        ue3::Name const gameName = gPowerName.Read<ue3::Name>(power).value_or(ue3::Name{});
        std::string const internal = ue3::runtime::NameText(gameName);
        if (internal.empty() || internal == "None") continue;
        sc::Power p;
        p.code = CodeOf(internal);
        gPowerNames[p.code] = gameName;
        p.readyInS = std::max(0.0f, gCooldownRemaining.Read<float>(power).value_or(0.0f));
        member.powers.push_back(p);
        bool listed = false;
        for (auto const& [code, spoken] : known.powers) listed = listed || code == p.code;
        if (!listed) known.powers.emplace_back(p.code, Spoken(internal));
    }
}

sc::Vec3 Metres(ue3::Vector const& v, double upm) {
    return {static_cast<float>(v.x / upm), static_cast<float>(v.y / upm), static_cast<float>(v.z / upm)};
}
sc::Vec3 Metres(engine::UnrealVector const& v, double upm) {
    return {static_cast<float>(v.x / upm), static_cast<float>(v.y / upm), static_cast<float>(v.z / upm)};
}

// The cover links within rangeM of the player: a walk over the live objects (every kCoverRefreshS; the links don't move),
// each as a Cover with its slots in metres. The word for what the cover is stays "cover" until the mod learns the level's
// meshes (the [verify] in research/54 "Landmarks").
void ReadCover(sc::Vec3 const& player, double upm, float rangeM) {
    gCover.clear();
    ++gCoverWalks;
    gCoverLinksSeen = 0;
    ue3::Reflection& r = ue3::runtime::Get();
    std::int32_t const n = r.ObjectCount();
    for (std::int32_t i = 0; i < n && gCover.size() < 32; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !ue3::IsA(o, gCoverLink)) continue;
        if (gCoverDisabled.ReadBool(o).value_or(false)) continue;
        std::string const name = ue3::runtime::FullName(o);
        if (name.find("Default__") != std::string::npos) continue;  // the class default objects (a level without cover lists only those)
        ++gCoverLinksSeen;
        std::optional<ue3::Vector> const loc = player::Location(o);
        std::optional<ue3::Rotator> const rot = player::Rotation(o);
        if (!loc || !rot) continue;
        sc::Cover c;
        c.id = reinterpret_cast<std::uint64_t>(o);
        c.position = Metres(*loc, upm);
        if (gCoverLinksSeen <= 8 && gCoverWalks % 30 == 1) {  // the first walk and every minute: what the level has (the words to learn)
            log::Info("squad context: cover link {} at {:.0f} m, {} slots{}", ue3::runtime::FullName(o), sc::Distance(c.position, player),
                gCoverSlots.ElementsOf(o) ? gCoverSlots.ElementsOf(o)->count : 0, sc::Distance(c.position, player) > rangeM + 10.0f ? " (out of range)" : "");
        }
        if (sc::Distance(c.position, player) > rangeM + 10.0f) continue;
        engine::UnrealRotation const rotation{rot->pitch, rot->yaw, rot->roll};
        c.yawDeg = static_cast<float>(rotation.yaw * 360.0 / 65536.0);
        engine::Basis const axes = engine::ToBasis(rotation);
        if (auto const slots = gCoverSlots.ElementsOf(o)) {
            for (std::int32_t k = 0; k < slots->count && k < 16; ++k) {
                ue3::Vector const offset = gCoverSlotOffset.Read<ue3::Vector>(slots->At(k)).value_or(ue3::Vector{});
                engine::UnrealVector const local = engine::Apply(axes, {offset.x, offset.y, offset.z});
                c.slots.push_back(Metres(engine::UnrealVector{loc->x + local.x, loc->y + local.y, loc->z + local.z}, upm));
            }
        }
        gCover.push_back(std::move(c));
    }
}

void ReadScene(ue3::Object* controller, ue3::Object* pawn, engine::TrackingFrame const& frame, double upm) {
    sc::Scene scene;
    scene.time = gClock;
    scene.player = Metres(player::Location(pawn).value_or(ue3::Vector{}), upm);
    scene.head = Metres(frame.headWorld, upm);
    engine::UnrealVector const forward = engine::OrientationToWorld(frame, frame.head.orientation).forward;
    scene.headYawDeg = static_cast<float>(std::atan2(forward.y, forward.x) * 180.0 / 3.14159265358979);
    scene.combat = CallBool(pawn, gIsInCombatActionStateFn.Get());
    ue3::Object* const held = weapon::HeldObject();
    scene.shepardWeapon = held != nullptr ? squad_weapons::WeaponTypeOf(held) : -1;
    // The party, as the squad menu read it (names, classes, quick slots, health).
    for (squad_weapons::MemberView const& view : squad_weapons::Members()) {
        sc::Member m;
        m.letter = LetterOfClass(view.classId);
        if (m.letter == 0) m.letter = view.name.empty() ? '?' : view.name[0];
        m.present = true;
        m.alive = view.health != 0.0f;
        m.carried = view.carried;
        m.inHand = view.current;
        m.position = Metres(player::Location(view.pawn).value_or(ue3::Vector{}), upm);
        Known& known = gRoster[m.letter];
        known.letter = m.letter;
        known.name = view.name;
        known.weapons = static_cast<std::uint8_t>(known.weapons | view.carried);
        ReadPowers(ObjectOf(gBehavior, view.pawn), m, known);
        scene.party.push_back(m);
    }
    // The hostiles: the pawn list, the living ones hostile to Shepard.
    ue3::Object* const world = ObjectOf(gWorldInfo, pawn);
    float const now = world != nullptr ? gTimeSeconds.Read<float>(world).value_or(0.0f) : 0.0f;
    ue3::Function* const isHostile = gIsHostileFn.Get();
    ue3::Function* const isDead = gIsDeadFn.Get();
    ue3::Function* const percentHealth = gGetPercentHealthFn.Get();
    int guard = 0;
    for (ue3::Object* p = ObjectOf(gPawnList, world); p != nullptr && guard < kMaxPawns && isHostile != nullptr;
         p = ObjectOf(gNextPawn, p), ++guard) {
        if (p == pawn || !player::IsBioPawn(p) || CallBool(p, isDead)) continue;
        ue3::Params hostile(ue3::runtime::Get(), isHostile);
        hostile.Set("Other", p);
        ue3::runtime::Call(pawn, isHostile, hostile);
        if (!hostile.ReturnBool().value_or(false)) continue;
        sc::Hostile h;
        h.id = reinterpret_cast<sc::PawnId>(p);
        h.position = Metres(player::Location(p).value_or(ue3::Vector{}), upm);
        ue3::Object* const behavior = ObjectOf(gBehavior, p);
        std::string type = StringOf(gTypeGameName, ObjectOf(gActorType, behavior));
        if (type.empty()) type = StringOf(gActorGameName, behavior);
        h.type = Lower(type);
        if (!h.type.empty() && gLoggedTypes[h.type]++ == 0) {
            log::Info("squad context: a hostile of type \"{}\" ({})", h.type, ue3::runtime::FullName(p));
        }
        if (behavior != nullptr && percentHealth != nullptr) {
            ue3::Params parms(ue3::runtime::Get(), percentHealth);
            ue3::runtime::Call(behavior, percentHealth, parms);
            float health = parms.Return<float>().value_or(1.0f);
            if (health > 1.0f) health /= 100.0f;
            h.health = std::clamp(health, 0.0f, 1.0f);
            if (h.health <= 0.0f) h.health = 0.01f;  // alive (IsDead said so), however low
        }
        float const rendered = gLastRenderTime.Read<float>(p).value_or(-1e9f);
        h.visible = now - rendered < 1.0f;
        scene.hostiles.push_back(h);
    }
    scene.aimTarget = reinterpret_cast<sc::PawnId>(sights::ConvergeActor());
    bool combatTarget = false;
    scene.gazeTarget = reinterpret_cast<sc::PawnId>(interact::Target(combatTarget));
    (void)controller;
    hand_calibration::SquadContext const s = hand_calibration::GetSquadContext();
    if (s.maxLandmarks > 0 && s.landmarkRangeM > 0.0) {
        if (gClock - gSinceCover >= kCoverRefreshS || gSinceCover > gClock) {
            gSinceCover = gClock;
            ReadCover(scene.player, upm, static_cast<float>(s.landmarkRangeM));
        }
        scene.cover = gCover;
    }
    gScene = std::move(scene);
    sc::GroupSettings gs;
    gs.joinM = static_cast<float>(s.joinM);
    gs.splitAboveM = static_cast<float>(s.splitAboveM);
    gs.maxGroups = s.maxGroups;
    gs.nearM = static_cast<float>(s.nearM);
    gs.farM = static_cast<float>(s.farM);
    gGrouper.Update(gScene, gs);
    sc::LandmarkSettings ls;
    ls.rangeM = static_cast<float>(s.landmarkRangeM);
    ls.maxLandmarks = s.maxLandmarks;
    gLandmarker.Update(gScene, ls);
    ++gReads;
}

void Sample(engine::TrackingFrame const& frame, double upm) {
    sc::PoseSample s;
    s.time = gClock;
    engine::Basis const head = engine::OrientationToWorld(frame, frame.head.orientation);
    s.head = {Metres(frame.headWorld, upm), {static_cast<float>(head.forward.x), static_cast<float>(head.forward.y), static_cast<float>(head.forward.z)}};
    vr::controllers::State const c = vr::controllers::Latest();
    auto const ray = [&](vr::controllers::Hand const& hand, sc::Ray& out, bool& valid) {
        valid = c.active && hand.aimValid;
        if (!valid) return;
        engine::Basis const aim = engine::OrientationToWorld(frame, hand.aim.orientation);
        out = {Metres(engine::ToWorld(frame, hand.aim.position), upm),
            {static_cast<float>(aim.forward.x), static_cast<float>(aim.forward.y), static_cast<float>(aim.forward.z)}};
    };
    ray(c.left, s.leftAim, s.leftValid);
    ray(c.right, s.rightAim, s.rightValid);
    gPoses.Push(s);
    ++gSamples;
}

}  // namespace

void Tick(double deltaSeconds) {
    gClock += deltaSeconds;
    gSinceRead += deltaSeconds;
    hand_calibration::SquadContext const settings = hand_calibration::GetSquadContext();
    if (!settings.enabled) {
        gWhyNot = "off ([SquadContext] enabled = 0)";
        return;
    }
    if (!ue3::runtime::Ready()) {
        gWhyNot = "our reflection layer is off";
        if (!gToldNoLayer) {
            gToldNoLayer = true;
            log::Warn("squad context: our reflection layer is off: no scene");
        }
        return;
    }
    ue3::Object* const controller = player::Controller();
    std::optional<std::uint8_t> const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    gOnFoot = mode && *mode == 0 && pawn != nullptr && frame.has_value();
    if (!gOnFoot) {
        gWhyNot = pawn == nullptr ? "no player pawn (vehicle or cutscene?)" : !frame ? "no tracking frame" : "not on foot";
        if (gScene.hostiles.size() + gScene.party.size() + gScene.cover.size() > 0) {
            gScene = {};
            gGrouper.Reset();
            gLandmarker.Reset();  // a new level: the cover's objects are new ones
            gCover.clear();
            gSinceCover = 1e9;
        }
        return;
    }
    gWhyNot.clear();
    double const upm = frame->unitsPerMeter > 0.0 ? frame->unitsPerMeter : engine::kUnitsPerMeter;
    Sample(*frame, upm);
    if (gSinceRead >= settings.refreshS) {
        gSinceRead = 0.0;
        ReadScene(controller, pawn, *frame, upm);
    }
}

char LetterOf(int classId) { return LetterOfClass(classId); }

sc::Scene const& Scene() { return gScene; }
std::vector<sc::Group> const& Groups() { return gGrouper.Groups(); }
std::vector<sc::Landmark> const& Landmarks() { return gLandmarker.Landmarks(); }

std::vector<sc::Candidate> ResolveAgo(double secondsAgo) {
    sc::DeixisSettings ds;
    return sc::Resolve(gPoses, gClock - secondsAgo, gGrouper.Groups(), gLandmarker.Landmarks(), gScene, ds);
}

std::string Block(std::string_view said, std::string_view word, double secondsAgo) {
    std::vector<sc::Deictic> deictics;
    if (!word.empty()) {
        deictics.push_back({1, std::string(word), ResolveAgo(secondsAgo)});
    }
    return sc::Block(gScene, gGrouper.Groups(), gLandmarker.Landmarks(), deictics, {}, said);
}

std::optional<ue3::Name> PowerName(std::string_view code) {
    auto const it = gPowerNames.find(std::string(code));
    return it != gPowerNames.end() ? std::optional<ue3::Name>(it->second) : std::nullopt;
}

std::vector<sc::RosterEntry> Roster() {
    std::vector<sc::RosterEntry> roster;
    for (auto const& [letter, known] : gRoster) {
        sc::RosterEntry e;
        e.letter = letter;
        e.name = known.name;
        e.aliases = sc::KnownAliases(known.name);
        e.weapons = known.weapons;
        e.powers = known.powers;
        roster.push_back(e);
    }
    return roster;
}

std::string Prefix() {
    return sc::Prefix(Roster());
}

std::string StatusJson() {
    std::string groups;
    for (sc::Group const& g : gGrouper.Groups()) {
        groups += std::format("{}{{\"id\": {}, \"type\": {}, \"count\": {}, \"sector\": \"{}\", \"band\": \"{}\", \"elevation\": \"{}\", "
                              "\"distanceM\": {}, \"bearingDeg\": {}, \"visible\": {}}}",
            groups.empty() ? "" : ", ", g.id, json::JsonString(g.type), sc::Count(g), sc::Name(g.sector), sc::Name(g.band),
            sc::Name(g.elevation), json::JsonNumber(g.distanceM), json::JsonNumber(g.bearingDeg), g.visible ? "true" : "false");
    }
    std::string party;
    for (sc::Member const& m : gScene.party) {
        std::string powers;
        for (sc::Power const& p : m.powers) {
            powers += std::format("{}{{\"code\": {}, \"readyInS\": {}}}", powers.empty() ? "" : ", ", json::JsonString(p.code), json::JsonNumber(p.readyInS));
        }
        party += std::format("{}{{\"letter\": \"{}\", \"alive\": {}, \"carried\": {}, \"inHand\": {}, \"powers\": [{}]}}", party.empty() ? "" : ", ",
            m.letter, m.alive ? "true" : "false", m.carried, m.inHand, powers);
    }
    std::string landmarks;
    for (sc::Landmark const& l : gLandmarker.Landmarks()) {
        landmarks += std::format("{}{{\"id\": {}, \"type\": {}, \"sector\": \"{}\", \"band\": \"{}\", \"distanceM\": {}, \"slots\": {}}}", landmarks.empty() ? "" : ", ",
            l.id, json::JsonString(l.type), sc::Name(l.sector), sc::Name(l.band), json::JsonNumber(l.distanceM), l.slots.size());
    }
    return std::format("{{\"onFoot\": {}, \"why\": {}, \"combat\": {}, \"shepardWeapon\": {}, \"headYawDeg\": {}, \"party\": [{}], \"hostiles\": {}, "
                       "\"groups\": [{}], \"landmarks\": [{}], \"coverLinks\": {}, \"aimGroup\": {}, \"gazeGroup\": {}, \"poseSamples\": {}, \"reads\": {}}}",
        gOnFoot ? "true" : "false", json::JsonString(gWhyNot), gScene.combat ? "true" : "false", gScene.shepardWeapon,
        json::JsonNumber(gScene.headYawDeg), party, gScene.hostiles.size(), groups, landmarks, gCoverLinksSeen, sc::GroupOf(gGrouper.Groups(), gScene.aimTarget),
        sc::GroupOf(gGrouper.Groups(), gScene.gazeTarget), gPoses.Size(), gReads);
}

void LogStats() {
    if (!gOnFoot) {
        log::Info("squad context: {}", gWhyNot.empty() ? std::string("idle") : gWhyNot);
        return;
    }
    std::string groups;
    for (sc::Group const& g : gGrouper.Groups()) {
        groups += std::format("{}E{} {} x{} {} {} {}", groups.empty() ? "" : "; ", g.id, g.type.empty() ? "enemy" : g.type, sc::Count(g),
            sc::Name(g.sector), sc::Name(g.band), sc::Name(g.elevation));
    }
    std::string landmarks;
    for (sc::Landmark const& l : gLandmarker.Landmarks()) {
        landmarks += std::format("{}L{} {} {} {} {:.0f} m ({} slots)", landmarks.empty() ? "" : "; ", l.id, l.type, sc::Name(l.sector), sc::Name(l.band), l.distanceM,
            l.slots.size());
    }
    log::Info("squad context: {} squadmate(s), {} hostile(s) in {} group(s){}{}; {} landmark(s) of {} cover links in {} walks{}{}; {} reads, {} pose samples kept; "
              "aim E{}, gaze E{}",
        gScene.party.size(), gScene.hostiles.size(), gGrouper.Groups().size(), groups.empty() ? "" : ": ", groups, gLandmarker.Landmarks().size(), gCoverLinksSeen,
        gCoverWalks, landmarks.empty() ? "" : ": ", landmarks, gReads, gPoses.Size(), sc::GroupOf(gGrouper.Groups(), gScene.aimTarget),
        sc::GroupOf(gGrouper.Groups(), gScene.gazeTarget));
}

void Shutdown() {
    gScene = {};
    gGrouper.Reset();
    gLandmarker.Reset();
    gCover.clear();
    gPoses.Clear();
    gOnFoot = false;
}

}  // namespace mevr::game::squad_context
