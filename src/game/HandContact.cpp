// SPDX-License-Identifier: MIT
#include "game/HandContact.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/ik/HandContact.hpp"
#include "core/ik/Rigid.hpp"
#include "engine/BonePose.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mevr::game::hand_contact {
namespace {

namespace hc = ik::hand_contact;

ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList", ue3::kEveryGame};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn", ue3::kEveryGame};
ue3::FieldRef gHidden{"Engine.Actor", "bHidden", ue3::kEveryGame};

constexpr char kBlock[] =
    "\n"
    "[HandContact]\n"
    "; Hand contact with other characters (research/73): Shepard's hand stops at a person's body (squadmates, NPCs, enemies)\n"
    "; and slides along it instead of passing through, while your controller goes on; the hand buzzes on contact. Nobody is\n"
    "; pushed. on = 0 turns it off. handRadiusM: the hand's size (metres); rangeM: how far from Shepard people are checked;\n"
    "; maxPeople: at most this many, nearest first; haptic = 0: no buzz.\n"
    "on = 1\n"
    "handRadiusM = 0.045\n"
    "rangeM = 2.5\n"
    "maxPeople = 8\n"
    "haptic = 1\n";

struct Settings {
    bool on = true;
    double handRadiusM = 0.045;
    double rangeM = 2.5;
    int maxPeople = 8;
    bool haptic = true;
};
Settings gSettings;
std::optional<bool> gOverride;  // `mevr contact on|off`
std::uint64_t gSettingsAtMs = 0;
bool gTriedAppend = false;

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsAtMs != 0 && now - gSettingsAtMs < 1000) {
        return;
    }
    gSettingsAtMs = now;
    std::filesystem::path const path = engine::SettingsFile();
    ini::Loaded const loaded = vr::settings_file::Load(path);
    if (loaded.exists && loaded.file.find("HandContact") == loaded.file.end() && !gTriedAppend) {
        gTriedAppend = true;
        // core/vr/SettingsFile Append: in the file's own encoding, one move; a per-game key to this game's section.
        if (std::string const error = vr::settings_file::Append(path, kBlock); error.empty()) {
            log::Info("hand contact: added the [HandContact] defaults to {}", ini::Utf8(path));
        } else {
            log::Warn("hand contact: couldn't add the [HandContact] defaults to {} ({}); they apply", ini::Utf8(path), error);
        }
    }
    Settings s;
    if (auto const v = ini::GetNumber(loaded.file, "HandContact", "on")) {
        s.on = *v != 0.0;
    }
    if (auto const v = ini::GetNumber(loaded.file, "HandContact", "handRadiusM")) {
        s.handRadiusM = std::clamp(*v, 0.01, 0.15);
    }
    if (auto const v = ini::GetNumber(loaded.file, "HandContact", "rangeM")) {
        s.rangeM = std::clamp(*v, 0.5, 10.0);
    }
    if (auto const v = ini::GetNumber(loaded.file, "HandContact", "maxPeople")) {
        s.maxPeople = static_cast<int>(std::clamp(*v, 1.0, 32.0));
    }
    if (auto const v = ini::GetNumber(loaded.file, "HandContact", "haptic")) {
        s.haptic = *v != 0.0;
    }
    if (gOverride) {
        s.on = *gOverride;
    }
    static bool logged = false;
    bool const changed = s.on != gSettings.on || s.handRadiusM != gSettings.handRadiusM || s.rangeM != gSettings.rangeM ||
                         s.maxPeople != gSettings.maxPeople || s.haptic != gSettings.haptic;
    if (!logged || changed) {
        logged = true;
        log::Info("hand contact: {} (hand {:.1f} cm, people within {:.1f} m, at most {}, buzz {})", s.on ? "on" : "off",
            s.handRadiusM * 100.0, s.rangeM, s.maxPeople, s.haptic ? "on" : "off");
    }
    gSettings = s;
}

// A mesh asset's body parts (by its bone names), kept per asset.
struct Body {
    int boneCount = -1;
    std::vector<hc::BonePart> parts;
    bool logged = false;
};
std::unordered_map<ue3::Object const*, Body> gBodies;
ue3::Object const* gBodiesWorld = nullptr;

Body const* BodyOf(ue3::Object* mesh) {
    ue3::Object const* const asset = player::SkeletalMesh(mesh);
    if (asset == nullptr) {
        return nullptr;
    }
    int const count = engine::skeleton::RefBoneCount(mesh);
    if (count <= 0) {
        return nullptr;
    }
    auto it = gBodies.find(asset);
    if (it != gBodies.end() && it->second.boneCount == count) {
        return &it->second;
    }
    Body body;
    body.boneCount = count;
    if (auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh)) {
        body.parts = hc::PartsFor(skeleton->names, skeleton->parents);
    }
    it = gBodies.insert_or_assign(asset, std::move(body)).first;
    return &it->second;
}

// The people found on the last fill (the console and the heartbeat).
struct Person {
    ue3::Object* pawn = nullptr;
    double distanceM = 0.0;
    int parts = 0;
    int capsules = 0;
};
std::vector<Person> gPeople;
int gLastCapsules = 0;
std::string gStatus = "not run yet";

// The heartbeat's period, per hand.
struct Period {
    int touches = 0;
    double deepestM = 0.0;
    double mostMovedM = 0.0;
    hc::Part part = hc::Part::None;
};
std::array<Period, 2> gPeriod;
std::array<bool, 2> gTouching{false, false};
std::array<std::uint64_t, 2> gLastPulseMs{0, 0};
std::uint64_t gTotalTouches = 0;
bool gContinuousOn[2] = {false, false};

double Distance(ue3::Vector a, ue3::Vector b) {
    return std::hypot(static_cast<double>(a.x - b.x), static_cast<double>(a.y - b.y), static_cast<double>(a.z - b.z));
}

} // namespace

void Fill(engine::bone_pose::Request& request, ue3::Object* playerPawn, ik::Affine const& worldToComponent, bool firstPerson) {
    RefreshSettings();
    request.contact = {};
    gPeople.clear();
    gLastCapsules = 0;
    if (!gSettings.on) {
        gStatus = gOverride ? "off (mevr contact off)" : "off ([HandContact] on = 0)";
        return;
    }
    if (!firstPerson) {
        gStatus = "standing by: the hands aren't first person";
        return;
    }
    if (playerPawn == nullptr || !ue3::runtime::Up()) {
        gStatus = "standing by: no player pawn";
        return;
    }
    std::optional<ue3::Vector> const at = player::Location(playerPawn);
    ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(playerPawn).value_or(nullptr);
    if (!at || world == nullptr) {
        gStatus = "standing by: the player's place or world unreadable";
        return;
    }
    if (world != gBodiesWorld) {
        gBodies.clear();  // a new level: its assets may sit where freed ones were
        gBodiesWorld = world;
    }
    double const rangeUnits = gSettings.rangeM * engine::kUnitsPerMeter;
    std::vector<std::pair<double, ue3::Object*>> nearby;
    int guard = 0;
    for (ue3::Object* p = gPawnList.Read<ue3::Object*>(world).value_or(nullptr); p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        if (!ue3::runtime::Get().IsAlive(p)) {
            break;  // a level torn down under us leaves freed pawns in the list (CLAUDE.md, LivePawns)
        }
        if (p == playerPawn || !player::IsBioPawn(p) || gHidden.ReadBool(p).value_or(true)) {
            continue;
        }
        std::optional<ue3::Vector> const there = player::Location(p);
        if (!there) {
            continue;
        }
        double const d = Distance(*there, *at);
        if (d <= rangeUnits) {
            nearby.emplace_back(d, p);
        }
    }
    std::sort(nearby.begin(), nearby.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    if (nearby.size() > static_cast<std::size_t>(gSettings.maxPeople)) {
        nearby.resize(static_cast<std::size_t>(gSettings.maxPeople));
    }
    // World units to this request's component units (the player's mesh may be scaled).
    double const perWorld = ik::Length(ik::TransformDirection(worldToComponent, {1.0, 0.0, 0.0}));
    double const unitsPerMeterHere = engine::kUnitsPerMeter * (perWorld > 1e-6 && std::isfinite(perWorld) ? perWorld : 1.0);
    std::vector<ik::Vec3> positions;
    for (auto const& [distance, pawn] : nearby) {
        Person person;
        person.pawn = pawn;
        person.distanceM = distance / engine::kUnitsPerMeter;
        ue3::Object* const mesh = player::Mesh(pawn);
        Body const* const body = mesh != nullptr ? BodyOf(mesh) : nullptr;
        if (body == nullptr || body->parts.empty()) {
            gPeople.push_back(person);
            continue;
        }
        person.parts = static_cast<int>(body->parts.size());
        std::span<engine::skeleton::BoneAtom> const bases = engine::skeleton::SpaceBases(mesh);
        if (static_cast<int>(bases.size()) != body->boneCount) {
            gPeople.push_back(person);  // not posed yet
            continue;
        }
        ik::Affine const toWorld = engine::skeleton::LocalToWorld(mesh);
        positions.resize(bases.size());
        for (std::size_t i = 0; i < bases.size(); ++i) {
            ik::Vec3 const local{bases[i].tx, bases[i].ty, bases[i].tz};
            positions[i] = ik::TransformPoint(worldToComponent, ik::TransformPoint(toWorld, local));
        }
        for (hc::BonePart const& part : body->parts) {
            if (auto const capsule = hc::CapsuleOf(part, positions, unitsPerMeterHere)) {
                request.contact.capsules.push_back(*capsule);
                ++person.capsules;
            }
        }
        gPeople.push_back(person);
    }
    gLastCapsules = static_cast<int>(request.contact.capsules.size());
    request.contact.handRadius = gSettings.handRadiusM * unitsPerMeterHere;
    request.contact.enabled = !request.contact.capsules.empty();
    gStatus = request.contact.enabled ? std::format("on: {} people near, {} capsules", gPeople.size(), gLastCapsules)
                                      : std::format("on: nobody within {:.1f} m", gSettings.rangeM);
}

void Tick(double /*deltaSeconds*/, Haptics const& haptics) {
    std::optional<engine::bone_pose::ContactSample> const sample = engine::bone_pose::LatestContact();
    std::uint64_t const now = GetTickCount64();
    for (int side : {0, 1}) {
        engine::bone_pose::ContactHand const hand = sample ? sample->hands[side] : engine::bone_pose::ContactHand{};
        bool const touching = hand.touching && gSettings.on;
        double const toMeters = hand.handRadius > 0.0 ? gSettings.handRadiusM / hand.handRadius : 0.0;
        if (touching) {
            Period& period = gPeriod[side];
            if (!gTouching[side]) {
                ++period.touches;
                ++gTotalTouches;
            }
            period.deepestM = (std::max)(period.deepestM, hand.depth * toMeters);
            if (hand.moved * toMeters >= period.mostMovedM) {
                period.mostMovedM = hand.moved * toMeters;
                period.part = hand.part;
            }
        }
        if (gSettings.haptic) {
            double const amplitude = hc::PressAmplitude(hand.depth, hand.handRadius);
            if (touching && !gTouching[side]) {
                // The touch: a short click.
                if (haptics.pulseHz != nullptr) {
                    haptics.pulseHz(side, 0.025f, 0.55f, 180.0f);
                } else if (haptics.pulse != nullptr) {
                    haptics.pulse(side, 0.025f, 0.55f);
                }
                gLastPulseMs[side] = now;
            } else if (touching) {
                // Pressed: a hum, stronger the deeper the controller is in.
                if (haptics.continuous != nullptr) {
                    haptics.continuous(side, static_cast<float>(amplitude), 90.0f, 0.15f);
                    gContinuousOn[side] = true;
                } else if (haptics.pulse != nullptr && now - gLastPulseMs[side] >= 100) {
                    haptics.pulse(side, 0.04f, static_cast<float>(amplitude));
                    gLastPulseMs[side] = now;
                }
            } else if (gContinuousOn[side] && haptics.continuous != nullptr) {
                haptics.continuous(side, 0.0f, 90.0f, 0.0f);
                gContinuousOn[side] = false;
            }
        }
        gTouching[side] = touching;
    }
}

std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string verb;
    words >> verb;
    if (verb == "on" || verb == "off") {
        gOverride = verb == "on";
        gSettingsAtMs = 0;
        RefreshSettings();
        return std::format("hand contact {} (until auto or a reload)", verb);
    }
    if (verb == "auto") {
        gOverride.reset();
        gSettingsAtMs = 0;
        RefreshSettings();
        return std::format("hand contact as [HandContact] says: {}", gSettings.on ? "on" : "off");
    }
    if (verb.empty() || verb == "people" || verb == "status") {
        std::string out = "hand contact: " + gStatus;
        for (Person const& p : gPeople) {
            out += std::format("\n  {:#x} at {:.2f} m: {} parts, {} capsules", reinterpret_cast<std::uintptr_t>(p.pawn), p.distanceM,
                p.parts, p.capsules);
        }
        if (auto const sample = engine::bone_pose::LatestContact()) {
            for (int side : {0, 1}) {
                auto const& h = sample->hands[side];
                double const toMeters = h.handRadius > 0.0 ? gSettings.handRadiusM / h.handRadius : 0.0;
                out += std::format("\n  {} hand: {}", side == 0 ? "left" : "right",
                    h.touching ? std::format("touching the {} ({:.1f} cm in, the hand kept {:.1f} cm out)", hc::PartName(h.part),
                                     h.depth * toMeters * 100.0, h.moved * toMeters * 100.0)
                               : std::string("free"));
            }
        } else {
            out += "\n  the writer reported no contact step (arm IK not writing, or nobody nearby)";
        }
        return out;
    }
    return "mevr contact [on|off|auto|people]";
}

std::string StatusJson() {
    std::string out = std::format("{{\"on\": {}, \"status\": \"{}\", \"people\": {}, \"capsules\": {}, \"touching\": [{}, {}], \"touches\": {}",
        gSettings.on ? "true" : "false", gStatus, gPeople.size(), gLastCapsules, gTouching[0] ? "true" : "false",
        gTouching[1] ? "true" : "false", gTotalTouches);
    if (auto const sample = engine::bone_pose::LatestContact()) {
        out += ", \"hands\": [";
        for (int side : {0, 1}) {
            auto const& h = sample->hands[side];
            double const toMeters = h.handRadius > 0.0 ? gSettings.handRadiusM / h.handRadius : 0.0;
            out += std::format("{}{{\"touching\": {}, \"depthM\": {:.3f}, \"movedM\": {:.3f}, \"part\": \"{}\"}}", side == 0 ? "" : ", ",
                h.touching ? "true" : "false", h.depth * toMeters, h.moved * toMeters, hc::PartName(h.part));
        }
        out += "]";
    }
    out += "}";
    return out;
}

void LogStats() {
    std::string hands;
    for (int side : {0, 1}) {
        Period const& p = gPeriod[side];
        hands += std::format("; {} hand {} touch(es)", side == 0 ? "left" : "right", p.touches);
        if (p.touches > 0) {
            hands += std::format(" (deepest {:.1f} cm, held off up to {:.1f} cm at the {})", p.deepestM * 100.0, p.mostMovedM * 100.0,
                hc::PartName(p.part));
        }
    }
    log::Info("    hand contact: {}{}", gStatus, hands);
    gPeriod = {};
}

void Shutdown() {
    gBodies.clear();
    gBodiesWorld = nullptr;
    gPeople.clear();
}

} // namespace mevr::game::hand_contact
