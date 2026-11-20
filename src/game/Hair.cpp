// SPDX-License-Identifier: MIT
#include "game/Hair.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/BodySpheres.hpp"
#include "core/ik/BodyStandIn.hpp"
#include "core/ik/HairChain.hpp"
#include "core/ik/HandContact.hpp"
#include "core/vr/SettingsFile.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/HairPose.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include "game/HairLook.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mevr::game::hair {
namespace {

namespace hc = ik::hand_contact;
namespace hp = engine::hair_pose;

// Our reflection layer (research/34); engine/ReflectCheck has a row for each.
ue3::FieldRef gHairMesh{"SFXGame.BioPawn", "m_oHairMesh", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList", ue3::kEveryGame};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn", ue3::kEveryGame};
ue3::FieldRef gHidden{"Engine.Actor", "bHidden", ue3::kEveryGame};
ue3::FieldRef gHiddenGame{"Engine.PrimitiveComponent", "HiddenGame", ue3::kEveryGame};
ue3::ClassRef gSkeletalMeshClass{"Engine.SkeletalMesh", ue3::kEveryGame};
ue3::FunctionRef gSetSkeletalMesh{"Engine.SkeletalMeshComponent.SetSkeletalMesh", ue3::kEveryGame};
ue3::FunctionRef gDynamicLoadObject{"Core.Object.DynamicLoadObject", ue3::kEveryGame};
ue3::FunctionRef gSetParentAnim{"Engine.SkeletalMeshComponent.SetParentAnimComponent", ue3::kEveryGame};
ue3::FieldRef gHeadMesh{"SFXGame.BioPawn", engine::adapter::kBioPawnHeadMesh, ue3::kEveryGame};
// The components attached to an actor (a body's other followers: ME1's head gear, visor and face plate, accessories).
ue3::FieldRef gAllComponents{"Engine.Actor", "AllComponents", ue3::kEveryGame};
ue3::ClassRef gSkeletalMeshComponentClass{"Engine.SkeletalMeshComponent", ue3::kEveryGame};

constexpr char kBlock[] =
    "\n"
    "[Hair]\n"
    "; Hair that moves (docs/trilogy-hair-plan.md): hairstyles with hair bones sway, swing and settle with the head, stay off\n"
    "; the person's shoulders and are pushed by your hands. ME3's Miranda wears her older rigged hair for it (standIns = 0:\n"
    "; the game's own, which can't move). on = 0 turns it off. rangeM: how far from you people's hair moves; maxPeople: at\n"
    "; most this many, nearest first. stiffness (0..1): how firmly hair returns to its styled shape; damping (0..1): how soon\n"
    "; a swing dies down; inertia (0..1): how much of the head's motion the hair feels; maxBendDeg: how far each joint bends;\n"
    "; gravity (m/s2, on top of the styled shape, which already hangs; 0: the style is the rest); radiusM: a strand's thickness for collisions; wind (m/s, along the world's X); hands = 0: your hands\n"
    "; don't touch hair.\n"
    "on = 1\n"
    "rangeM = 6\n"
    "maxPeople = 6\n"
    "stiffness = 0.06\n"
    "damping = 0.10\n"
    "inertia = 1.0\n"
    "maxBendDeg = 50\n"
    "gravity = 0\n"
    "radiusM = 0.012\n"
    "wind = 0\n"
    "hands = 1\n"
    "standIns = 1\n"
    "; Soft tissue (research/trilogy-jiggle.md): bodies rigged by tools/rig (ME3's Miranda) have breast and glute bones that\n"
    "; lag, bounce and settle with the body's motion. jiggle = 0 holds them to the animation; jiggleIntensity 0..1 scales how\n"
    "; far they move (never how fast they settle). 1 since the owner's headset check (2026-11-19: full is a little better).\n"
    "jiggle = 1\n"
    "jiggleIntensity = 1\n"
    "; Armour holds the soft tissue (the owner, 2026-11-19: reduced in full gear, full in a bodysuit, casual clothes or\n"
    "; underwear, every female character of the three games): a body whose mesh name has a heavy, medium or light armour type\n"
    "; (_HVY, _MED, _LGT), or holds one of armourBodies, keeps jiggleArmour of the intensity; one holding a fullBodies entry\n"
    "; (a character outfit named like armour but a bodysuit: ME2 Miranda's HMF_MIR_LGTa) keeps all of it. Comma lists of name\n"
    "; parts, any case.\n"
    "jiggleArmour = 0.25\n"
    "armourBodies =\n"
    "fullBodies = hmf_mir_\n"
    "; A character's own share, before the rules above (the owner, 2026-11-19): name part:share, comma-separated, the first\n"
    "; that matches wins. 0 leaves the body as the game made it (no rigged body, no jiggle: EDI's robot body); Samara's armour\n"
    "; keeps half; Kasumi's suit, Ashley's and Liara's combat outfits and Cerberus female armour a quarter; Jack all of it;\n"
    "; Ashley's hospital and romance clothes (ASHc) and Liara's romance body (LIAd) all of it.\n"
    "bodyJiggle = hmf_arm_ashc:1, asa_arm_liad:1, _edi:0, asa_spr_:0.5, asa_arm_sam:0.5, _ksm:0.25, hmf_jck_:1, hmf_arm_ash:0.25, "
    "asa_arm_lia:0.25, hmf_arm_crb:0.25\n"
    "; followBody (0..1): how far a long hair chain's lower joints rest with the shoulders rather than the head (hair lying\n"
    "; on the shoulders stays there when the head turns); single-bone strands are never affected.\n"
    "followBody = 0.7\n";

struct Settings {
    bool on = true;
    double rangeM = 6.0;
    int maxPeople = 6;
    ik::hair::Settings sim;
    double wind = 0.0;
    bool hands = true;
    bool standIns = true;
    bool jiggle = true;
    double jiggleIntensity = 1.0;
    // Armour holds the soft tissue (OutfitJiggle): the share it keeps, and name parts that make a body armour or full.
    double jiggleArmour = 0.25;
    std::vector<std::string> armourBodies;
    std::vector<std::string> fullBodies{"hmf_mir_"};
    // A character's own share (name part, share; the first match wins), before the armour rules (the owner, 2026-11-19).
    std::vector<std::pair<std::string, double>> bodyJiggle{{"hmf_arm_ashc", 1.0}, {"asa_arm_liad", 1.0}, {"_edi", 0.0},
        {"asa_spr_", 0.5}, {"asa_arm_sam", 0.5}, {"_ksm", 0.25}, {"hmf_jck_", 1.0}, {"hmf_arm_ash", 0.25},
        {"asa_arm_lia", 0.25}, {"hmf_arm_crb", 0.25}};
    std::optional<std::string> look;  // [Hair] look: material parameters for simulated hair (empty: the game's)
};

// A comma list of name parts, lower case, trimmed.
std::vector<std::string> NameParts(std::string const& text) {
    std::vector<std::string> out;
    std::string item;
    std::stringstream in(text);
    while (std::getline(in, item, ',')) {
        std::size_t const a = item.find_first_not_of(" \t");
        std::size_t const b = item.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        std::string part = item.substr(a, b - a + 1);
        std::transform(part.begin(), part.end(), part.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        out.push_back(part);
    }
    return out;
}
Settings gSettings;
std::optional<double> gJiggleOverride;  // `mevr hair jiggle <0..1|off>`
std::optional<double> gFollowOverride;  // `mevr hair follow <0..1|auto>`
std::optional<bool> gOverride;          // `mevr hair on|off`
std::optional<bool> gStandInOverride;   // `mevr hair standins on|off`
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
    if (loaded.exists && loaded.file.find("Hair") == loaded.file.end() && !gTriedAppend) {
        gTriedAppend = true;
        if (std::string const error = vr::settings_file::Append(path, kBlock); error.empty()) {
            log::Info("hair: added the [Hair] defaults to {}", ini::Utf8(path));
        } else {
            log::Warn("hair: couldn't add the [Hair] defaults to {} ({}); they apply", ini::Utf8(path), error);
        }
    }
    auto num = [&](char const* key) { return ini::GetNumber(loaded.file, "Hair", key); };
    Settings s;
    if (auto v = num("on")) s.on = *v != 0.0;
    if (auto v = num("rangeM")) s.rangeM = std::clamp(*v, 0.5, 30.0);
    if (auto v = num("maxPeople")) s.maxPeople = static_cast<int>(std::clamp(*v, 1.0, static_cast<double>(hp::kMaxTargets)));
    if (auto v = num("stiffness")) s.sim.stiffness = std::clamp(*v, 0.0, 1.0);
    if (auto v = num("damping")) s.sim.damping = std::clamp(*v, 0.0, 1.0);
    if (auto v = num("inertia")) s.sim.inertia = std::clamp(*v, 0.0, 1.0);
    if (auto v = num("maxBendDeg")) s.sim.maxBendDeg = std::clamp(*v, 0.0, 170.0);
    s.sim.followBody = 0.7;
    if (auto v = num("followBody")) s.sim.followBody = std::clamp(*v, 0.0, 1.0);
    if (gFollowOverride) s.sim.followBody = *gFollowOverride;
    if (auto v = num("gravity")) s.sim.gravity = std::clamp(*v, 0.0, 30.0);
    if (auto v = num("radiusM")) s.sim.radiusM = std::clamp(*v, 0.0, 0.1);
    if (auto v = num("wind")) s.wind = std::clamp(*v, -20.0, 20.0);
    if (auto v = num("hands")) s.hands = *v != 0.0;
    if (auto v = num("standIns")) s.standIns = *v != 0.0;
    if (auto v = num("jiggle")) s.jiggle = *v != 0.0;
    if (auto v = num("jiggleIntensity")) s.jiggleIntensity = std::clamp(*v, 0.0, 1.0);
    if (auto v = num("jiggleArmour")) s.jiggleArmour = std::clamp(*v, 0.0, 1.0);
    if (auto v = ini::Get(loaded.file, "Hair", "armourBodies")) s.armourBodies = NameParts(*v);
    if (auto v = ini::Get(loaded.file, "Hair", "fullBodies")) s.fullBodies = NameParts(*v);
    if (auto v = ini::Get(loaded.file, "Hair", "bodyJiggle")) {
        s.bodyJiggle.clear();
        for (std::string const& item : NameParts(*v)) {
            std::size_t const colon = item.rfind(':');
            if (colon == std::string::npos || colon == 0) continue;
            std::string part = item.substr(0, colon);
            while (!part.empty() && part.back() == ' ') part.pop_back();
            if (auto share = ini::Number(item.substr(colon + 1))) s.bodyJiggle.emplace_back(part, std::clamp(*share, 0.0, 1.0));
        }
    }
    s.look = ini::Get(loaded.file, "Hair", "look");
    s.sim.windMS = {s.wind, 0.0, 0.0};
    if (gOverride) s.on = *gOverride;
    if (gStandInOverride) s.standIns = *gStandInOverride;
    if (gJiggleOverride) {
        s.jiggle = *gJiggleOverride > 0.0;
        s.jiggleIntensity = (std::max)(*gJiggleOverride, 0.0);
    }
    static bool logged = false;
    bool const changed = s.on != gSettings.on || s.standIns != gSettings.standIns || s.hands != gSettings.hands ||
                         s.jiggle != gSettings.jiggle || s.jiggleIntensity != gSettings.jiggleIntensity ||
                         s.sim.followBody != gSettings.sim.followBody ||
                         s.sim.stiffness != gSettings.sim.stiffness || s.sim.damping != gSettings.sim.damping ||
                         s.sim.inertia != gSettings.sim.inertia || s.sim.maxBendDeg != gSettings.sim.maxBendDeg ||
                         s.rangeM != gSettings.rangeM || s.maxPeople != gSettings.maxPeople || s.wind != gSettings.wind;
    if (!logged || changed) {
        logged = true;
        log::Info("hair: {} (people within {:.1f} m, at most {}; stiffness {:.2f}, damping {:.2f}, inertia {:.2f}, bend {:.0f} deg, "
                  "wind {:.1f} m/s; hands {}; stand-ins {})",
            s.on ? "on" : "off", s.rangeM, s.maxPeople, s.sim.stiffness, s.sim.damping, s.sim.inertia, s.sim.maxBendDeg, s.wind,
            s.hands ? "on" : "off", s.standIns ? "on" : "off");
    }
    gSettings = s;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "SkeletalMesh BIOG_X.Y.Z" -> "biog_x.y.z".
std::string PathOf(ue3::Object const* object) {
    if (object == nullptr) {
        return {};
    }
    std::string const full = ue3::runtime::FullName(object);
    std::size_t const space = full.find(' ');
    return Lower(space == std::string::npos ? full : full.substr(space + 1));
}

// Body parts kept off the hair (everything above the hips) and the player's parts that push it (hands and arms), per mesh
// asset.
struct Parts {
    int boneCount = -1;
    std::vector<hc::BonePart> body;
    std::vector<hc::BonePart> arms;
    std::vector<ik::body_spheres::Sphere> spheres;  // the body's surface fitted by the rig tool, if it built this mesh
};

// The rig tool's spheres for a body it built (tools/rig/README.md "Body spheres"): MEVR/rig/<GAME>/colliders/<mesh>.json
// under LOCALAPPDATA, bound to skel. Empty without a file (a body of the game's own: its capsules alone).
std::vector<ik::body_spheres::Sphere> SpheresFor(ue3::Object const* asset, engine::skeleton::RefSkeleton const& skel) {
    std::string name = ue3::runtime::FullName(asset);
    if (auto const dot = name.find_last_of(". "); dot != std::string::npos) name = name.substr(dot + 1);
    wchar_t local[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) == 0) return {};
    std::filesystem::path const file = std::filesystem::path(local) / L"MEVR" / L"rig" / MEVR_GAME_NAME_W / L"colliders" /
        (std::wstring(name.begin(), name.end()) + L".json");
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto const fitted = ik::body_spheres::Parse(text, &error);
    if (!fitted) {
        log::Warn("hair: {}'s body spheres unreadable ({}): its capsules alone", name, error);
        return {};
    }
    std::vector<ik::Transform> bind(skel.local.size());
    for (std::size_t i = 0; i < bind.size(); ++i) {
        int const p = skel.parents[i];
        bind[i] = p >= 0 ? ik::Compose(skel.local[i], bind[static_cast<std::size_t>(p)]) : skel.local[i];
    }
    int missing = 0;
    std::vector<ik::body_spheres::Sphere> spheres = ik::body_spheres::Bind(*fitted, skel.names, bind, &missing);
    log::Info("hair: {}'s surface: {} spheres fitted from its mesh{} (the hair lies on it; its spine, neck and collar capsules "
              "stand down)", name, spheres.size(), missing > 0 ? std::format(", {} on bones it lacks left out", missing) : "");
    return spheres;
}
std::unordered_map<ue3::Object const*, Parts> gParts;
ue3::Object const* gWorld = nullptr;

Parts const* PartsOf(ue3::Object* mesh) {
    ue3::Object const* const asset = player::SkeletalMesh(mesh);
    int const count = engine::skeleton::RefBoneCount(mesh);
    if (asset == nullptr || count <= 0) {
        return nullptr;
    }
    auto it = gParts.find(asset);
    if (it != gParts.end() && it->second.boneCount == count) {
        return &it->second;
    }
    Parts parts;
    parts.boneCount = count;
    if (auto const skel = engine::skeleton::ReadRefSkeleton(mesh)) {
        parts.spheres = SpheresFor(asset, *skel);
        for (hc::BonePart const& p : hc::PartsFor(skel->names, skel->parents)) {
            bool const leg = p.part == hc::Part::Thigh || p.part == hc::Part::Shin || p.part == hc::Part::Foot;
            // With the surface fitted, the round guesses it replaces stand down (they held the nape's hair inside the spine's).
            bool const replaced = !parts.spheres.empty() &&
                (p.part == hc::Part::Spine || p.part == hc::Part::Neck || p.part == hc::Part::Collar);
            if (!leg && !replaced) {
                parts.body.push_back(p);
            }
            if (p.part == hc::Part::Hand || p.part == hc::Part::Forearm || p.part == hc::Part::UpperArm) {
                parts.arms.push_back(p);
            }
        }
    }
    return &gParts.insert_or_assign(asset, std::move(parts)).first->second;
}

// ---- Rigged stand-ins (ME3's Miranda) --------------------------------------------------------------------------------

struct Swapped {
    ue3::Object* component = nullptr;
    ue3::Object* original = nullptr;  // the mesh it had
    ue3::Object* standIn = nullptr;
    bool byName = false;              // a rigged body from MEVR_Bodies_<GAME>'s index, not the hand table
};
std::vector<Swapped> gSwapped;
std::unordered_map<ue3::Object*, ue3::Object*> gBodyMaterial;  // `mevr hair body`: a body component's material before
std::unordered_map<std::string, ue3::Object*> gStandInObjects;  // rigged path -> object (this level)
std::unordered_map<std::string, bool> gStandInMissing;          // logged once per level

ue3::Object* FindLoaded(std::string const& path) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gSkeletalMeshClass.Get();
    if (cls == nullptr) {
        return nullptr;
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls)) {
            continue;
        }
        if (PathOf(o) == path) {
            return o;
        }
    }
    return nullptr;
}

ue3::Object* LoadMesh(std::string const& path) {
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr || gDynamicLoadObject.Get() == nullptr || gSkeletalMeshClass.Get() == nullptr) {
        return nullptr;
    }
    ue3::memory::OwnedString text(ue3::memory::Widen(path));
    if (!text.Valid()) {
        return nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), gDynamicLoadObject.Get());
    if (!p.Set("ObjectName", text.Get()) || !p.Set("ObjectClass", gSkeletalMeshClass.Get()) || !p.SetBool("MayFail", true)) {
        return nullptr;
    }
    ue3::runtime::Call(controller, gDynamicLoadObject.Get(), p);  // the engine's own loader, on the game thread
    return p.Return<ue3::Object*>().value_or(nullptr);
}

ue3::Object* StandInFor(std::string const& riggedPath) {
    auto it = gStandInObjects.find(riggedPath);
    if (it != gStandInObjects.end() && ue3::runtime::IsAlive(it->second)) {
        return it->second;
    }
    if (gStandInMissing.contains(riggedPath)) {
        return nullptr;
    }
    ue3::Object* found = FindLoaded(riggedPath);
    char const* how = "loaded with the level";
    if (found == nullptr) {
        found = LoadMesh(riggedPath);
        how = "loaded by DynamicLoadObject";
    }
    if (found == nullptr) {
        gStandInMissing[riggedPath] = true;
        log::Warn("hair: the rigged stand-in {} isn't loaded and couldn't be loaded: the hairstyle stays the game's", riggedPath);
        return nullptr;
    }
    gStandInObjects[riggedPath] = found;
    log::Info("hair: the rigged stand-in {} found ({})", riggedPath, how);
    return found;
}

// ---- Rigged bodies by path (docs/trilogy-hair-plan.md section 12 step 3) -----------------------------------------------
// tools/rig's MEVR_Bodies_<GAME>.pcc holds every female body it rigged; its index beside it in %LOCALAPPDATA%\MEVR\rig\<GAME>,
// MEVR_Bodies_<GAME>.json, maps each game mesh's object path to its rig (core/ik/BodyStandIn): any body whose mesh it lists
// wears the rig, after the hand table (Miranda's hand rigs carry her hair chains).
namespace bsi = ik::body_stand_in;
bsi::Cache gBodies;
bool gBodiesIndexRead = false;  // read once a level (a rebuild between levels is picked up)

std::filesystem::path RigFolder() {
    wchar_t local[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) == 0) return {};
    return std::filesystem::path(local) / L"MEVR" / L"rig" / MEVR_GAME_NAME_W;
}

void ReadBodiesIndex() {
    if (gBodiesIndexRead) return;
    gBodiesIndexRead = true;
    std::filesystem::path const file = RigFolder() / (L"MEVR_Bodies_" + std::wstring(MEVR_GAME_NAME_W) + L".json");
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        gBodies.SetIndex(std::nullopt);
        return;  // not built: bodies keep the game's meshes (said by `mevr hair bodies`)
    }
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    std::optional<bsi::Index> index = bsi::Index::Parse(text, &error);
    if (index) {
        log::Info("hair: the rigged bodies' index lists {} game meshes for {}", index->Size(), index->Package());
    } else {
        log::Warn("hair: the rigged bodies' index {} unreadable ({}): bodies keep the game's meshes", ini::Utf8(file), error);
    }
    gBodies.SetIndex(std::move(index));
}

// The bodies' package is in memory (an outermost object of that name).
bool BodiesPackageLoaded() {
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const want = bsi::PackageName(MEVR_GAME_NAME_A);
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.IsAlive(o) && r.OuterOf(o) == nullptr && Lower(r.NameText(o)) == want) {
            return true;
        }
    }
    return false;
}

bsi::Engine BodyEngine() {
    bsi::Engine e;
    e.findLoaded = [](std::string const& path) -> void* { return FindLoaded(path); };
    e.load = [](std::string const& path) -> void* { return LoadMesh(path); };
    e.packageLoaded = [] { return BodiesPackageLoaded(); };
    // Still a mesh of ours: IsAlive alone says yes for another object at the same address (CLAUDE.md, ue3::Kept).
    e.alive = [](void* o) {
        auto* const object = static_cast<ue3::Object*>(o);
        return ue3::runtime::IsAlive(object) && ue3::IsA(object, gSkeletalMeshClass) &&
               bsi::IsStandIn(ue3::runtime::Get().NameText(object));
    };
    return e;
}

// The rigged stand-in for a body mesh by its path, or nullptr; each mesh asked once a level, the outcome logged then.
// The soft tissue's share for a body mesh's lowered object path (OutfitJiggle's rules; a character's bodyJiggle entry first).
double ShareForPath(std::string const& name) {
    for (auto const& [part, share] : gSettings.bodyJiggle) {
        if (!part.empty() && name.find(part) != std::string::npos) return share;
    }
    for (std::string const& part : gSettings.fullBodies) {
        if (!part.empty() && name.find(part) != std::string::npos) return 1.0;
    }
    for (std::string const& part : gSettings.armourBodies) {
        if (!part.empty() && name.find(part) != std::string::npos) return gSettings.jiggleArmour;
    }
    for (char const* type : {"_hvy", "_med", "_lgt"}) {
        if (name.find(type) != std::string::npos) return gSettings.jiggleArmour;
    }
    return 1.0;
}

ue3::Object* BodyStandInFor(ue3::Object const* asset) {
    if (asset == nullptr || !gSettings.standIns) {
        return nullptr;
    }
    ReadBodiesIndex();
    std::string const path = PathOf(asset);
    bool const known = gBodies.Entries().contains(path);
    bool const absentBefore = gBodies.PackageAbsent();
    auto* const found = static_cast<ue3::Object*>(gBodies.Find(path, BodyEngine()));
    if (!known) {
        bsi::Entry const& e = gBodies.Entries().at(path);
        if (e.outcome == bsi::Outcome::Found) {
            log::Info("hair: {}'s rigged body {} found ({})", path, e.rig, e.loaded ? "loaded by DynamicLoadObject" : "already in memory");
        } else if (e.outcome == bsi::Outcome::NoPackage && !absentBefore && gBodies.GetIndex() != nullptr) {
            log::Info("hair: the rigged bodies' package {} isn't installed (tools/rig/install.ps1): bodies keep the game's meshes this level",
                gBodies.GetIndex()->Package());
        } else if (e.outcome == bsi::Outcome::NotLoaded) {
            log::Warn("hair: the index lists {} for {}, but the package has no such rig (an older build?)", e.rig, path);
        }
    }
    return found;
}

bool SetMesh(ue3::Object* component, ue3::Object* mesh) {
    if (component == nullptr || mesh == nullptr || gSetSkeletalMesh.Get() == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), gSetSkeletalMesh.Get());
    if (!p.Valid() || !p.Set("NewMesh", mesh)) {
        return false;
    }
    p.SetBool("bKeepSpaceBases", false);
    return ue3::runtime::Call(component, gSetSkeletalMesh.Get(), p);
}

// Puts `standIn` on the component in place of `asset`, kept to put back. False if SetSkeletalMesh refused it.
bool Wear(ue3::Object* component, ue3::Object* asset, ue3::Object* standIn, bool byName) {
    if (!SetMesh(component, standIn)) {
        log::Warn("hair: SetSkeletalMesh refused {} on {:#x}", PathOf(standIn), reinterpret_cast<std::uintptr_t>(component));
        return false;
    }
    // A stand-in replaced by a later one keeps the first original, so putting back restores the game's.
    ue3::Object* original = asset;
    for (auto it = gSwapped.begin(); it != gSwapped.end(); ++it) {
        if (it->component == component) {
            original = it->original;
            gSwapped.erase(it);
            break;
        }
    }
    gSwapped.push_back({component, original, standIn, byName});
    log::Info("hair: {:#x} wears {} instead of {} (rigged{}: its hair or body can move)", reinterpret_cast<std::uintptr_t>(component),
        PathOf(standIn), PathOf(asset), byName ? ", from the rigged bodies' index" : "");
    return true;
}

// The stand-in for this component's mesh, if one is wanted: the hand table's first entry for it that loads, else for a body
// the rigged body named after its mesh (BodyStandInFor). True when a mesh was put on this call.
bool ApplyStandIn(ue3::Object* component, ue3::Object* asset, bool body) {
    if (!gSettings.standIns || asset == nullptr || component == nullptr) {
        return false;
    }
    for (Swapped const& s : gSwapped) {
        if (s.component == component && s.standIn == asset) {
            return false;  // already ours
        }
    }
    std::string const path = PathOf(asset);
    for (engine::adapter::HairStandIn const& entry : engine::adapter::kHairStandIns) {
        if (entry.used == nullptr || Lower(entry.used) != path) {
            continue;
        }
        if (entry.needs != nullptr && StandInFor(Lower(entry.needs)) == nullptr) {
            continue;  // its materials' package isn't there
        }
        ue3::Object* const standIn = StandInFor(Lower(entry.rigged));
        if (standIn == nullptr) {
            continue;  // the next entry for this mesh, if any
        }
        return Wear(component, asset, standIn, false);
    }
    if (!body) {
        return false;
    }
    if (ShareForPath(Lower(PathOf(asset))) <= 0.0) {
        return false;  // a body that keeps no jiggle (EDI's robot body) stays the game's
    }
    ue3::Object* const standIn = BodyStandInFor(asset);
    return standIn != nullptr && Wear(component, asset, standIn, true);
}

// After a body's mesh changes, the components that follow it (head, hair) map their bones to it again.
void Reattach(ue3::Object* body, std::vector<ue3::Object*> const& followers) {
    if (body == nullptr || gSetParentAnim.Get() == nullptr) {
        return;
    }
    for (ue3::Object* f : followers) {
        if (f == nullptr || !ue3::runtime::IsAlive(f)) {
            continue;
        }
        ue3::Params p(ue3::runtime::Get(), gSetParentAnim.Get());
        if (p.Valid() && p.Set("NewParentAnimComp", body)) {
            ue3::runtime::Call(f, gSetParentAnim.Get(), p);
        }
    }
}

void ReattachEveryone();

void PutBackStandIns(char const* why) {
    int put = 0;
    for (Swapped const& s : gSwapped) {
        if (ue3::runtime::IsAlive(s.component)) {
            hair_look::PutBack(s.component);  // its own material instance dropped: the mesh's material again
            auto kept = gBodyMaterial.find(s.component);
            if (kept != gBodyMaterial.end() && ue3::runtime::IsAlive(kept->second)) {
                hair_look::SetMaterial(s.component, kept->second);  // the outfit's own material back (`mevr hair body`)
            }
        }
        if (ue3::runtime::IsAlive(s.component) && ue3::runtime::IsAlive(s.original) && player::SkeletalMesh(s.component) == s.standIn) {
            put += SetMesh(s.component, s.original) ? 1 : 0;
        }
    }
    ReattachEveryone();  // a body put back: its followers map to it again
    if (!gSwapped.empty()) {
        log::Info("hair: {} of {} rigged stand-in(s) put back ({})", put, gSwapped.size(), why);
    }
    gSwapped.clear(); gBodyMaterial.clear();
}

// ---- The people near ---------------------------------------------------------------------------------------------------

struct Person {
    ue3::Object* pawn = nullptr;
    ue3::Object* hair = nullptr;
    ue3::Object* body = nullptr;
    double distanceM = 0.0;
    ue3::Object* head = nullptr;
};
std::vector<Person> gPeople;
std::string gStatus = "not run yet";

// What follows a person's body: the head and hair, and any other skeletal mesh attached to the actor whose pose parent is the
// body (ME1's head gear, visor and face plate; accessories).
std::vector<ue3::Object*> FollowersOf(Person const& p) {
    std::vector<ue3::Object*> out{p.head, p.hair};
    if (p.pawn == nullptr || p.body == nullptr) {
        return out;
    }
    if (auto const components = gAllComponents.ElementsOf(p.pawn)) {
        for (std::int32_t i = 0; i < components->count; ++i) {
            ue3::Object* const c = *static_cast<ue3::Object**>(components->At(i));
            if (c == nullptr || c == p.body || !ue3::runtime::IsAlive(c) || !ue3::IsA(c, gSkeletalMeshComponentClass) ||
                engine::skeleton::PoseParent(c) != p.body || std::find(out.begin(), out.end(), c) != out.end()) {
                continue;
            }
            out.push_back(c);
        }
    }
    return out;
}

void ReattachEveryone() {
    for (Person const& p : gPeople) {
        Reattach(p.body, FollowersOf(p));
    }
}
int gPublished = 0;

double Distance(ue3::Vector a, ue3::Vector b) {
    return std::hypot(static_cast<double>(a.x - b.x), static_cast<double>(a.y - b.y), static_cast<double>(a.z - b.z));
}

// ME3's conversation characters (Miranda on the Citadel and in London among them) are SFXStuntActors, not pawns: their own
// Body, Head and Hair components, and not on the world's pawn list. Found by a scan of the objects once a second.
ue3::ClassRef gStuntClass{"SFXGame.SFXStuntActor", ue3::kEveryGame};
ue3::FieldRef gStuntBody{"SFXGame.SFXStuntActor", "BodyMesh", ue3::kEveryGame};
ue3::FieldRef gStuntHair{"SFXGame.SFXStuntActor", "HairMesh", ue3::kEveryGame};
ue3::FieldRef gStuntHead{"SFXGame.SFXStuntActor", "HeadMesh", ue3::kEveryGame};
std::vector<ue3::Object*> gStunts;
std::uint64_t gStuntScanAt = 0;
ue3::Object const* gStuntWorld = nullptr;

std::vector<ue3::Object*> const& StuntActors(ue3::Object* world) {
    std::uint64_t const now = GetTickCount64();
    if (world == gStuntWorld && now < gStuntScanAt) {
        return gStunts;
    }
    gStuntWorld = world;
    gStuntScanAt = now + 1000;
    gStunts.clear();
    ue3::Class* const cls = gStuntClass.Get();
    if (cls == nullptr) {
        return gStunts;  // not ME3's SFXGame (or not up yet)
    }
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls) || gWorldInfo.Read<ue3::Object*>(o).value_or(nullptr) != world) {
            continue;  // archetypes and class defaults have no WorldInfo
        }
        gStunts.push_back(o);
    }
    return gStunts;
}

bool HasSoftTissueBones(ue3::Object* mesh);

// A body we can make jiggle: rigged already, or with a rigged body named after its mesh (asked once a level).
bool SoftTissueOrRigged(ue3::Object* body) {
    if (body == nullptr || !ue3::runtime::IsAlive(body) || ShareForPath(Lower(PathOf(player::SkeletalMesh(body)))) <= 0.0) {
        return false;
    }
    return HasSoftTissueBones(body) || BodyStandInFor(player::SkeletalMesh(body)) != nullptr;
}

// The soft tissue's share for a body's outfit (the owner, 2026-11-19): a character's bodyJiggle entry first (EDI none, Samara
// half, Kasumi a quarter); then armour keeps jiggleArmour (a heavy, medium or light armour type in the mesh's name, or an
// armourBodies part), everything else (a bodysuit, casual clothes, underwear) all of it; a fullBodies part (a character outfit
// named like armour but a bodysuit) wins over the armour rules. The rigged body keeps its source mesh's name.
double OutfitJiggle(ue3::Object* body) {
    if (body == nullptr) return 1.0;
    return ShareForPath(Lower(PathOf(player::SkeletalMesh(body))));
}

std::vector<Person> FindPeople(ue3::Object* playerPawn, ue3::Vector at, ue3::Object* world) {
    std::vector<Person> found;
    double const range = gSettings.rangeM * engine::kUnitsPerMeter;
    int guard = 0;
    for (ue3::Object* p = gPawnList.Read<ue3::Object*>(world).value_or(nullptr); p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        if (!ue3::runtime::Get().IsAlive(p)) {
            break;  // a level torn down under us leaves freed pawns in the list (CLAUDE.md, LivePawns)
        }
        if (p == playerPawn || !player::IsBioPawn(p) || gHidden.ReadBool(p).value_or(true)) {
            continue;
        }
        ue3::Object* hair = gHairMesh.Read<ue3::Object*>(p).value_or(nullptr);
        if (hair != nullptr && player::SkeletalMesh(hair) == nullptr) hair = nullptr;  // an empty hair component (asari): no hair
        if (hair != nullptr && (!ue3::runtime::IsAlive(hair) || gHiddenGame.ReadBool(hair).value_or(false))) {
            continue;
        }
        std::optional<ue3::Vector> const there = player::Location(p);
        if (!there) {
            continue;
        }
        double const d = Distance(*there, at);
        if (d <= range && hair == nullptr && !SoftTissueOrRigged(player::Mesh(p))) {
            continue;  // no hair and no jiggle: nothing of ours (a hairless body rigged by tools/rig has soft tissue: Liara)
        }
        if (d <= range) {
            ue3::Object* const head = gHeadMesh.Read<ue3::Object*>(p).value_or(nullptr);
            found.push_back({p, hair, player::Mesh(p), d / engine::kUnitsPerMeter, head != nullptr && ue3::runtime::IsAlive(head) ? head : nullptr});
        }
    }
    for (ue3::Object* const s : StuntActors(world)) {
        if (!ue3::runtime::IsAlive(s) || gHidden.ReadBool(s).value_or(true)) {
            continue;
        }
        ue3::Object* hair = gStuntHair.Read<ue3::Object*>(s).value_or(nullptr);
        if (hair != nullptr && player::SkeletalMesh(hair) == nullptr) hair = nullptr;  // an empty hair component (asari): no hair
        if (hair != nullptr && (!ue3::runtime::IsAlive(hair) || gHiddenGame.ReadBool(hair).value_or(false))) {
            continue;
        }
        std::optional<ue3::Vector> const there = player::Location(s);
        if (!there) {
            continue;
        }
        double const d = Distance(*there, at);
        if (d <= range && hair == nullptr && !SoftTissueOrRigged(gStuntBody.Read<ue3::Object*>(s).value_or(nullptr))) {
            continue;
        }
        if (d <= range) {
            ue3::Object* const body = gStuntBody.Read<ue3::Object*>(s).value_or(nullptr);
            ue3::Object* const head = gStuntHead.Read<ue3::Object*>(s).value_or(nullptr);
            found.push_back({s, hair, body != nullptr && ue3::runtime::IsAlive(body) ? body : nullptr, d / engine::kUnitsPerMeter,
                             head != nullptr && ue3::runtime::IsAlive(head) ? head : nullptr});
        }
    }
    std::sort(found.begin(), found.end(), [](Person const& a, Person const& b) { return a.distanceM < b.distanceM; });
    return found;
}

bool HasHairBones(ue3::Object* mesh) {
    if (auto const skel = engine::skeleton::ReadRefSkeleton(mesh)) {
        for (std::string const& name : skel->names) {
            if (Lower(name).find("hair") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

// A body rigged by tools/rig with soft-tissue bones (Breast*, Glute*).
bool HasSoftTissueBones(ue3::Object* mesh) {
    if (mesh == nullptr || !ue3::runtime::IsAlive(mesh)) {
        return false;
    }
    if (auto const skel = engine::skeleton::ReadRefSkeleton(mesh)) {
        for (std::string const& name : skel->names) {
            std::string const n = Lower(name);
            if (n.rfind("breast", 0) == 0 || n.rfind("glute", 0) == 0) {
                return true;
            }
        }
    }
    return false;
}

// The component whose pose draws the hair bones: the body the hair follows if that body has hair bones, else the hair; a
// hairless person (soft tissue only) is simulated on the body.
ue3::Object* SimulatedOn(Person const& person) {
    if (person.hair == nullptr) {
        return person.body;
    }
    ue3::Object* const parent = engine::skeleton::PoseParent(person.hair);
    if (parent != nullptr && ue3::runtime::IsAlive(parent) && HasHairBones(parent) && HasHairBones(person.hair)) {
        return parent;
    }
    return person.hair;
}

std::string HairBones(ue3::Object* mesh) {
    std::string out;
    if (auto const skel = engine::skeleton::ReadRefSkeleton(mesh)) {
        for (std::size_t i = 0; i < skel->names.size(); ++i) {
            if (Lower(skel->names[i]).find("hair") != std::string::npos) {
                int const parent = skel->parents[i];
                out += std::format("{}{}({}<-{})", out.empty() ? "" : " ", skel->names[i], i,
                    parent >= 0 && static_cast<std::size_t>(parent) < skel->names.size() ? skel->names[static_cast<std::size_t>(parent)] : "-");
            }
        }
    }
    return out.empty() ? "none" : out;
}

// ---- Bringing someone to the player: the in-game check of a character who isn't around (`mevr hair stream`, `bring`) -----
// ME3's Miranda stands only in a few sublevels (BioD_CitHub_Miranda, BioD_End001_436CRMiranda...), often unloaded or hidden
// by the plot. `stream` asks the engine's level streaming to load and show one, as a streaming volume would; `bring` finds
// a pawn by its name, its hair mesh or its body mesh, shows it and stands it 1.5 m in front of the player, facing them.

ue3::FieldRef gStreamingLevels{"Engine.WorldInfo", "StreamingLevels", ue3::kEveryGame};
ue3::FieldRef gLevelPackage{"Engine.LevelStreaming", "PackageName", ue3::kEveryGame};
ue3::FieldRef gLevelLoaded{"Engine.LevelStreaming", "LoadedLevel", ue3::kEveryGame};
ue3::FieldRef gLevelShouldLoad{"Engine.LevelStreaming", "bShouldBeLoaded", ue3::kEveryGame};
ue3::FieldRef gLevelShouldShow{"Engine.LevelStreaming", "bShouldBeVisible", ue3::kEveryGame};
ue3::FunctionRef gSetHidden{"Engine.Actor.SetHidden", ue3::kEveryGame};
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation", ue3::kEveryGame};
ue3::FunctionRef gSetRotation{"Engine.Actor.SetRotation", ue3::kEveryGame};

ue3::Object* PlayerWorld(ue3::Object** playerPawnOut = nullptr) {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const playerPawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    if (playerPawnOut != nullptr) {
        *playerPawnOut = playerPawn;
    }
    return playerPawn != nullptr ? gWorldInfo.Read<ue3::Object*>(playerPawn).value_or(nullptr) : nullptr;
}

std::string StreamLevel(std::string const& package, bool on) {
    ue3::Object* const world = PlayerWorld();
    auto const levels = world != nullptr ? gStreamingLevels.ElementsOf(world) : std::nullopt;
    if (!levels) {
        return "no world with streaming levels";
    }
    std::string names;
    for (std::int32_t i = 0; i < levels->count; ++i) {
        ue3::Object* const level = *static_cast<ue3::Object**>(levels->At(i));
        auto const name = level != nullptr && ue3::runtime::IsAlive(level) ? gLevelPackage.Read<ue3::Name>(level) : std::nullopt;
        if (!name) {
            continue;
        }
        std::string const text = ue3::runtime::NameText(*name);
        if (package == "?") {
            names += std::format("\n  {} (loaded {})", text, gLevelLoaded.Read<ue3::Object*>(level).value_or(nullptr) != nullptr ? 1 : 0);
            continue;
        }
        if (Lower(text).find(Lower(package)) == std::string::npos) {
            continue;
        }
        bool const loaded = gLevelLoaded.Read<ue3::Object*>(level).value_or(nullptr) != nullptr;
        bool const wrote = gLevelShouldLoad.WriteBool(level, on) && gLevelShouldShow.WriteBool(level, on);
        write_journal::Wrote("Hair.streamLevel", level, 0);
        names += std::format("\n  {}: {} (was {}loaded){}", text, on ? "load and show" : "hide and unload", loaded ? "" : "not ",
            wrote ? "" : " - NOT WRITTEN");
    }
    if (package == "?") {
        return std::format("{} streaming level(s):{}", levels->count, names);
    }
    if (names.empty()) {
        return "no streaming level whose name holds '" + package + "' in this world (`mevr hair stream ?` lists them)";
    }
    log::Info("hair: streaming {}{}", package, names);
    return "asked the level streaming to " + std::string(on ? "load" : "unload") + ":" + names +
           (on ? "\n  give it a few seconds, then `mevr hair bring <name>`" : "");
}

// `mevr hair spawn <text> [k]`: a pawn spawned from a loaded pawn object (an archetype in a henchman or level package, or a
// class default) whose path holds the text, 1.5 m in front of the player, facing them: the game's own spawn actions do the
// same (Actor.Spawn with the archetype as its template). The ME3 crew and Miranda are spawned by scripts, not placed.
ue3::FunctionRef gSpawn{"Engine.Actor.Spawn", ue3::kEveryGame};
std::vector<ue3::Object*> gSpawned;

std::string SpawnPawn(std::string const& what, int which) {
    ue3::Object* playerPawn = nullptr;
    ue3::Object* const world = PlayerWorld(&playerPawn);
    ue3::Object* const controller = player::Controller();
    std::optional<ue3::Vector> const at = playerPawn != nullptr ? player::Location(playerPawn) : std::nullopt;
    std::optional<ue3::Rotator> const facing = playerPawn != nullptr ? player::Rotation(playerPawn) : std::nullopt;
    ue3::Function* const spawn = gSpawn.Get();
    if (world == nullptr || controller == nullptr || !at || !facing || spawn == nullptr) {
        return "no player in a world (or Actor.Spawn not found)";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const key = Lower(what);
    std::vector<ue3::Object*> found;
    std::string list;
    if (key == "player") {
        // A copy of the player's pawn faulted in the engine's first UpdateSkelPose of it (ME3, 2026-11-19, exe+0x9C44D4).
        return "not the player's pawn: a copy of it crashed ME3 (its first pose update); spawn from an archetype instead";
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !player::IsBioPawn(o)) {
            continue;
        }
        std::string const path = Lower(r.FullName(o));
        if (path.find(key) == std::string::npos || path.find(".theworld.") != std::string::npos) {
            continue;  // live pawns in the world are `bring`'s
        }
        if (found.size() < 60) {
            list += std::format("\n  [{}] {}", found.size(), r.FullName(o));
        }
        found.push_back(o);
    }
    if (found.empty()) {
        return "no loaded pawn archetype holds '" + what + "' (`mevr hair stream <package>` loads a henchman or level package first)";
    }
    std::string out = std::format("{} pawn object(s):{}", found.size(), list);
    if (which < 0 || static_cast<std::size_t>(which) >= found.size()) {
        return out + "\n  none spawned: `mevr hair spawn <text> <k>`";
    }
    ue3::Object* const source = found[static_cast<std::size_t>(which)];
    double const yaw = static_cast<double>(facing->yaw) * (2.0 * 3.14159265358979323846 / 65536.0);
    ue3::Vector to = *at;
    to.x += static_cast<float>(std::cos(yaw) * 1.5 * engine::kUnitsPerMeter);
    to.y += static_cast<float>(std::sin(yaw) * 1.5 * engine::kUnitsPerMeter);
    ue3::Rotator face{};
    face.yaw = (facing->yaw + 32768) & 0xFFFF;
    ue3::Params p(r, spawn);
    bool const set = p.Set("SpawnClass", r.ClassOf(source)) && p.Set("SpawnLocation", to) && p.Set("SpawnRotation", face) &&
                     p.Set("ActorTemplate", source) && p.SetBool("bNoCollisionFail", true);
    if (!set) {
        return out + "\n  Spawn's parameters couldn't be set";
    }
    ue3::runtime::Call(controller, spawn, p);
    ue3::Object* const spawned = p.Return<ue3::Object*>().value_or(nullptr);
    write_journal::Wrote("Hair.spawn", spawned, 0);
    if (spawned == nullptr) {
        return out + std::format("\n  Spawn({}) gave no actor", r.FullName(source));
    }
    gSpawned.push_back(spawned);
    log::Info("hair: spawned {} from {} in front of the player", r.FullName(spawned), r.FullName(source));
    return out + std::format("\n  spawned {} at ({:.0f}, {:.0f}, {:.0f}); `mevr hair people` should list it", r.FullName(spawned), to.x, to.y, to.z);
}

// `mevr hair fire <text> [k] [input]`: a loaded Kismet op whose path holds the text, its input fired
// (Engine.SequenceOp.ForceActivateInput), as LE1's and LE2's tools start a scene: Miranda's
// `BioD_CitHub_Miranda...SFXSeqAct_StartConversation` starts her conversation, so her stunt actor is animated.
ue3::ClassRef gSequenceOpClass{"Engine.SequenceOp", ue3::kEveryGame};

std::string FireOp(std::string const& what, int which, int input) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gSequenceOpClass.Get();
    if (cls == nullptr) {
        return "Engine.SequenceOp not found";
    }
    std::string const key = Lower(what);
    std::vector<ue3::Object*> found;
    std::string list;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls)) {
            continue;
        }
        std::string const name = r.FullName(o);
        if (name.find("Default__") != std::string::npos || Lower(name).find(key) == std::string::npos) {
            continue;
        }
        if (found.size() < 40) {
            list += std::format("\n  [{}] {}", found.size(), name);
        }
        found.push_back(o);
    }
    if (found.empty()) {
        return "no loaded Kismet op holds '" + what + "'";
    }
    std::string out = std::format("{} op(s):{}", found.size(), list);
    if (which < 0 || static_cast<std::size_t>(which) >= found.size()) {
        return out + "\n  none fired: `mevr hair fire <text> <k> [input]`";
    }
    ue3::Object* const op = found[static_cast<std::size_t>(which)];
    ue3::Function* const f = r.FindFunction(r.ClassOf(op), "ForceActivateInput");
    if (f == nullptr) {
        return out + "\n  it has no ForceActivateInput";
    }
    ue3::Params p(r, f);
    p.Set("InputIdx", static_cast<std::int32_t>(input));
    bool const called = ue3::runtime::Call(op, f, p);
    log::Info("hair: fired {} input {} ({})", r.FullName(op), input, called ? "called" : "not called");
    return out + std::format("\n  fired [{}] input {}: {}", which, input, called ? "called" : "NOT called");
}

// `mevr hair call <text> <Function> [param=int]`: a script function called on the first live object whose full name holds the
// text (not a class default), at most one int parameter set: the test's way past a scene that owns the camera (ME3's docking
// taxi: `call bioconversationcontroller QueueReply nReply=10`, or `call sfxgamemodeconversation SkipConversation`).
std::string CallOn(std::string const& what, std::string const& function, std::string const& param) {
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const key = Lower(what);
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o)) {
            continue;
        }
        std::string const name = r.FullName(o);
        if (name.find("Default__") != std::string::npos || Lower(name).find(key) == std::string::npos) {
            continue;
        }
        ue3::Function* const f = r.FindFunction(r.ClassOf(o), function);
        if (f == nullptr) {
            continue;
        }
        ue3::Params p(r, f);
        if (std::size_t const eq = param.find('='); eq != std::string::npos) {
            int value = 0;
            try {
                value = std::stoi(param.substr(eq + 1));
            } catch (...) {
                return "the parameter's value isn't an int: " + param;
            }
            if (!p.Set(param.substr(0, eq), static_cast<std::int32_t>(value))) {
                return "no int parameter " + param.substr(0, eq) + " on " + function;
            }
        }
        bool const called = ue3::runtime::Call(o, f, p);
        log::Info("hair: called {}.{}({}): {}", name, function, param, called ? "called" : "not called");
        return std::format("{}.{}({}) {}; returned {}", name, function, param, called ? "called" : "NOT called",
            p.ReturnBool() ? (*p.ReturnBool() ? "true" : "false") : "nothing");
    }
    return "no live object holding '" + what + "' has " + function;
}

std::string Bring(std::string const& what, int which) {
    ue3::Object* playerPawn = nullptr;
    ue3::Object* const world = PlayerWorld(&playerPawn);
    std::optional<ue3::Vector> const at = playerPawn != nullptr ? player::Location(playerPawn) : std::nullopt;
    std::optional<ue3::Rotator> const facing = playerPawn != nullptr ? player::Rotation(playerPawn) : std::nullopt;
    if (world == nullptr || !at || !facing) {
        return "no player in a world";
    }
    std::string const key = Lower(what);
    std::vector<std::pair<ue3::Object*, std::string>> matches;
    int guard = 0;
    int walked = 0;
    std::string seen;
    for (ue3::Object* p = gPawnList.Read<ue3::Object*>(world).value_or(nullptr); p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        if (!ue3::runtime::IsAlive(p)) {
            break;
        }
        ++walked;
        if (p == playerPawn || !player::IsBioPawn(p)) {
            continue;
        }
        ue3::Object* const hair = gHairMesh.Read<ue3::Object*>(p).value_or(nullptr);
        ue3::Object* const body = player::Mesh(p);
        if (seen.size() < 4000) {
            seen += "\n  " + PathOf(p);
        }
        std::string const name = PathOf(p);
        std::string const hairPath = hair != nullptr && ue3::runtime::IsAlive(hair) ? PathOf(player::SkeletalMesh(hair)) : std::string();
        std::string const bodyPath = body != nullptr ? PathOf(player::SkeletalMesh(body)) : std::string();
        if (name.find(key) != std::string::npos || hairPath.find(key) != std::string::npos || bodyPath.find(key) != std::string::npos) {
            matches.emplace_back(p, std::format("{} (hair {}, body {}, hidden {})", name, hairPath.empty() ? "none" : hairPath,
                bodyPath.empty() ? "none" : bodyPath, gHidden.ReadBool(p).value_or(false) ? 1 : 0));
        }
    }
    std::vector<ue3::Object*> const stunts = StuntActors(world);
    for (ue3::Object* const s : stunts) {
        if (!ue3::runtime::IsAlive(s)) {
            continue;
        }
        ue3::Object* const hair = gStuntHair.Read<ue3::Object*>(s).value_or(nullptr);
        ue3::Object* const body = gStuntBody.Read<ue3::Object*>(s).value_or(nullptr);
        std::string const name = PathOf(s);
        std::string const hairPath = hair != nullptr && ue3::runtime::IsAlive(hair) ? PathOf(player::SkeletalMesh(hair)) : std::string();
        std::string const bodyPath = body != nullptr && ue3::runtime::IsAlive(body) ? PathOf(player::SkeletalMesh(body)) : std::string();
        if (seen.size() < 4000) {
            seen += "\n  (stunt actor) " + name + " hair " + (hairPath.empty() ? "none" : hairPath);
        }
        if (name.find(key) != std::string::npos || hairPath.find(key) != std::string::npos || bodyPath.find(key) != std::string::npos) {
            matches.emplace_back(s, std::format("stunt actor {} (hair {}, body {}, hidden {})", name, hairPath.empty() ? "none" : hairPath,
                bodyPath.empty() ? "none" : bodyPath, gHidden.ReadBool(s).value_or(false) ? 1 : 0));
        }
    }
    if (matches.empty()) {
        return std::format("no pawn or stunt actor whose name, hair or body mesh holds '{}' among the {} pawn(s) and {} stunt actor(s) "
                           "(`mevr hair stream <package>` loads a sublevel first):{}", what, walked, stunts.size(), seen);
    }
    std::string out = std::format("{} match(es):", matches.size());
    for (std::size_t i = 0; i < matches.size(); ++i) {
        out += std::format("\n  [{}] {}", i, matches[i].second);
    }
    if (which < 0 || static_cast<std::size_t>(which) >= matches.size()) {
        return out + "\n  none brought: `mevr hair bring <name> <k>`";
    }
    ue3::Object* const pawn = matches[static_cast<std::size_t>(which)].first;
    ue3::Function* const setHidden = gSetHidden.Get();
    ue3::Function* const setLocation = gSetLocation.Get();
    ue3::Function* const setRotation = gSetRotation.Get();
    if (setHidden == nullptr || setLocation == nullptr || setRotation == nullptr) {
        return out + "\n  Actor.SetHidden, SetLocation or SetRotation not found";
    }
    double const yaw = static_cast<double>(facing->yaw) * (2.0 * 3.14159265358979323846 / 65536.0);
    ue3::Vector to = *at;
    to.x += static_cast<float>(std::cos(yaw) * 1.5 * engine::kUnitsPerMeter);
    to.y += static_cast<float>(std::sin(yaw) * 1.5 * engine::kUnitsPerMeter);
    ue3::Rotator face{};
    face.yaw = (facing->yaw + 32768) & 0xFFFF;
    ue3::Params hide(ue3::runtime::Get(), setHidden);
    hide.SetBool("bNewHidden", false);
    ue3::runtime::Call(pawn, setHidden, hide);
    ue3::Params move(ue3::runtime::Get(), setLocation);
    move.Set("NewLocation", to);
    ue3::runtime::Call(pawn, setLocation, move);
    bool placed = move.Return<bool>().value_or(false);
    if (!placed) {
        // SetLocation refuses a far move into another part of the level (a conversation actor whose own sublevel stands 40 m
        // away: Liara at the Citadel docks, 2026-11-19): the place written directly, the components moved with it.
        static ue3::FieldRef location{"Engine.Actor", "Location", ue3::kEveryGame};
        static ue3::FunctionRef forceUpdate{"Engine.Actor.ForceUpdateComponents", ue3::kEveryGame};
        if (ue3::Vector* const place = location.In<ue3::Vector>(pawn)) {
            *place = to;
            write_journal::Wrote("Hair.bringLocation", pawn, sizeof(ue3::Vector));
            if (ue3::Function* const f = forceUpdate.Get()) {
                ue3::Params update(ue3::runtime::Get(), f);
                update.SetBool("bCollisionUpdate", false);
                update.SetBool("bTransformOnly", true);
                ue3::runtime::Call(pawn, f, update);
            }
            placed = true;
        }
    }
    ue3::Params turn(ue3::runtime::Get(), setRotation);
    turn.Set("NewRotation", face);
    ue3::runtime::Call(pawn, setRotation, turn);
    write_journal::Wrote("Hair.bring", pawn, 0);
    std::optional<ue3::Vector> const now = player::Location(pawn);
    log::Info("hair: brought {} in front of the player", matches[static_cast<std::size_t>(which)].second);
    return out + std::format("\n  brought [{}]: SetLocation returned {}, now ({:.0f}, {:.0f}, {:.0f})", which,
        move.Return<bool>().value_or(false) ? "true" : (placed ? "false (written directly)" : "false"), now ? now->x : 0.0f, now ? now->y : 0.0f, now ? now->z : 0.0f);
}

void Stop(char const* why) {
    if (gPublished > 0) {
        hp::Clear();
        gPublished = 0;
    }
    gStatus = why;
}

} // namespace

bool Wanted() {
    RefreshSettings();
    return gSettings.on;
}

void Tick(double /*deltaSeconds*/) {
    RefreshSettings();
    if (!gSettings.on) {
        PutBackStandIns("hair movement off");
        Stop(gOverride ? "off (mevr hair off)" : "off ([Hair] on = 0)");
        return;
    }
    if (!gSettings.standIns && !gSwapped.empty()) {
        PutBackStandIns("stand-ins off");
    }
    if (!ue3::runtime::Up()) {
        Stop("standing by: the reflection layer isn't up");
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const playerPawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    ue3::Object* const world = playerPawn != nullptr ? gWorldInfo.Read<ue3::Object*>(playerPawn).value_or(nullptr) : nullptr;
    std::optional<ue3::Vector> const at = playerPawn != nullptr ? player::Location(playerPawn) : std::nullopt;
    if (world == nullptr || !at) {
        Stop("standing by: no player in a world");
        return;
    }
    if (world != gWorld) {
        gWorld = world;  // a new level: its objects may sit where freed ones were
        gParts.clear();
        gSwapped.clear(); gBodyMaterial.clear();
        gStandInObjects.clear();
        gStandInMissing.clear();
        gBodies.Forget();
        gBodiesIndexRead = false;
        hair_look::Forget();
        hp::Clear();
        gPublished = 0;
    }
    std::vector<Person> people = FindPeople(playerPawn, *at, world);
    if (people.size() > static_cast<std::size_t>(gSettings.maxPeople)) {
        people.resize(static_cast<std::size_t>(gSettings.maxPeople));
    }
    hp::Request request;
    request.settings = gSettings.sim;
    request.unitsPerMeter = engine::kUnitsPerMeter;
    request.playerPushes = gSettings.hands;
    request.jiggle = gSettings.jiggle;
    request.jiggleIntensity = gSettings.jiggleIntensity;
    if (gSettings.hands) {
        if (ue3::Object* const mesh = player::Mesh(playerPawn); mesh != nullptr) {
            if (Parts const* parts = PartsOf(mesh)) {
                request.player = mesh;
                request.playerParts = parts->arms;
            }
        }
    }
    for (Person const& person : people) {
        // The body first (a rigged body carries the hair chains and soft-tissue bones the hair and jiggle stages drive), its
        // followers re-attached to it, then the hair.
        if (ApplyStandIn(person.body, player::SkeletalMesh(person.body), true)) {
            Reattach(person.body, FollowersOf(person));
        }
        ue3::Object* const asset = person.hair != nullptr ? player::SkeletalMesh(person.hair) : nullptr;
        if (person.hair != nullptr && ApplyStandIn(person.hair, asset, false)) {
            Reattach(person.body, {person.hair});
        }
        // The hair's look (game/HairLook): [Hair] look, else the rigged stand-in's own from the table; only on a hair we
        // simulate (a stand-in or a rigged hairstyle), once per component and look.
        if (!gSwapped.empty() && person.hair != nullptr) {
            std::string look = gSettings.look.value_or("");
            if (!gSettings.look) {
                std::string const worn = PathOf(player::SkeletalMesh(person.hair));
                for (engine::adapter::HairStandIn const& entry : engine::adapter::kHairStandIns) {
                    if (entry.rigged != nullptr && entry.look != nullptr && Lower(entry.rigged) == worn) {
                        look = entry.look;
                        break;
                    }
                }
            }
            for (Swapped const& s : gSwapped) {
                if (s.component == person.hair) {
                    hair_look::Apply(person.hair, look);
                }
            }
        }
        hp::Target t;
        // A hair that follows a body is drawn with the body's matrix for every bone the body has (UE3's follower rule; ME3's
        // UpdateSkelPose composes nothing for a component with a ParentAnimComponent, 0x796EB0): so when the body carries the
        // hair bones (Miranda's ME3 body has the old hair's 13), they are simulated and written in the body's pose.
        ue3::Object* const simulated = SimulatedOn(person);
        t.hair = simulated;
        t.asset = player::SkeletalMesh(simulated);
        t.body = person.body;
        if (person.body != nullptr) {
            if (Parts const* parts = PartsOf(person.body)) {
                t.bodyParts = parts->body;
                t.spheres = parts->spheres;
            }
            t.jiggleScale = OutfitJiggle(person.body);
        }
        request.targets.push_back(std::move(t));
    }
    gPeople = std::move(people);
    gPublished = static_cast<int>(request.targets.size());
    hp::Publish(std::move(request));
    gStatus = gPublished > 0 ? std::format("on: {} people's hair within {:.1f} m", gPublished, gSettings.rangeM)
                             : std::format("on: nobody within {:.1f} m", gSettings.rangeM);
}

std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string verb;
    words >> verb;
    if (verb == "on" || verb == "off") {
        gOverride = verb == "on";
        gSettingsAtMs = 0;
        RefreshSettings();
        return std::format("hair movement {} (until auto or a reload)", verb);
    }
    if (verb == "auto") {
        gOverride.reset();
        gStandInOverride.reset();
        gSettingsAtMs = 0;
        RefreshSettings();
        return std::format("hair movement as [Hair] says: {}", gSettings.on ? "on" : "off");
    }
    if (verb == "follow") {
        std::string v;
        words >> v;
        if (v == "auto") {
            gFollowOverride.reset();
        } else {
            gFollowOverride = std::clamp(ini::Number(v).value_or(0.7), 0.0, 1.0);
        }
        gSettingsAtMs = 0;
        return gFollowOverride ? std::format("long hair chains follow the shoulders by {:.2f} (until auto)", *gFollowOverride)
                               : "long hair chains follow the shoulders as the settings say";
    }
    if (verb == "surface") {
        // `mevr hair surface reload`: the bodies' sphere files read again at the next pose (a refit by `mevr-rig colliders`).
        gParts.clear();
        return "the bodies' surfaces (tools/rig's sphere files) are read again at the next pose";
    }
    if (verb == "jiggle") {
        std::string v;
        words >> v;
        if (v == "auto") {
            gJiggleOverride.reset();
        } else if (v == "off") {
            gJiggleOverride = 0.0;
        } else {
            gJiggleOverride = std::clamp(ini::Number(v).value_or(0.5), 0.0, 1.0);
        }
        gSettingsAtMs = 0;
        return gJiggleOverride ? std::format("soft tissue {} (until auto)", *gJiggleOverride > 0 ? std::format("at intensity {:.2f}", *gJiggleOverride) : std::string("off"))
                               : "soft tissue as the settings say";
    }
    if (verb == "standins") {
        std::string v;
        words >> v;
        if (v == "on" || v == "off") {
            gStandInOverride = v == "on";
            gSettingsAtMs = 0;
            RefreshSettings();
            return std::format("rigged stand-ins {}", v);
        }
        return "mevr hair standins on|off";
    }
    if (verb == "trace") {
        double seconds = 10.0;
        words >> seconds;
        wchar_t buffer[MAX_PATH]{};
        GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::filesystem::path const file = std::filesystem::path(buffer) / L"MEVR" /
            std::format(L"hair-trace-{:04}{:02}{:02}-{:02}{:02}{:02}.csv", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        hp::Trace(seconds, ini::Utf8(file));
        return std::format("hair trace for {:.0f} s: {}", seconds, ini::Utf8(file));
    }
    if (verb == "kick") {
        int n = -1;
        double x = 0.0, y = 0.0, z = 0.0;
        words >> n >> x >> y >> z;
        if (n < 0 || static_cast<std::size_t>(n) >= gPeople.size()) {
            return "mevr hair kick <person n> <x> <y> <z> (m/s, world)";
        }
        return hp::Kick(SimulatedOn(gPeople[static_cast<std::size_t>(n)]), {x, y, z}) ? std::format("kicked [{}]'s hair at ({}, {}, {}) m/s", n, x, y, z)
                                                                              : "that hair isn't simulated (no hair bones, or not posed yet)";
    }
    if (verb == "material" || verb == "param") {
        // mevr hair material [n]; mevr hair param [n] <name> <value | r g b [a]>
        std::vector<std::string> rest;
        for (std::string w; words >> w;) {
            rest.push_back(w);
        }
        int n = 0;
        if (!rest.empty() && !rest.front().empty() && std::isdigit(static_cast<unsigned char>(rest.front()[0])) && (verb == "material" || rest.size() >= 3)) {
            n = std::stoi(rest.front());
            rest.erase(rest.begin());
        }
        if (n < 0 || n >= static_cast<int>(gPeople.size())) {
            return std::format("no person [{}] (mevr hair people)", n);
        }
        ue3::Object* const component = gPeople[static_cast<std::size_t>(n)].hair;
        if (verb == "material") {
            return hair_look::Describe(component);
        }
        if (rest.size() < 2) {
            return "mevr hair param [n] <name> <value | r g b [a]>";
        }
        std::vector<double> values;
        for (std::size_t i = 1; i < rest.size(); ++i) {
            if (auto v = ini::Number(rest[i])) {
                values.push_back(*v);
            }
        }
        return hair_look::Set(component, rest[0], values);
    }
    if (verb == "mesh") {
        int n = -1;
        std::string path;
        words >> n >> path;
        if (n < 0 || static_cast<std::size_t>(n) >= gPeople.size() || path.empty()) {
            return "mevr hair mesh <person n from `mevr hair people`> <package.group.mesh>";
        }
        ue3::Object* mesh = FindLoaded(Lower(path));
        if (mesh == nullptr) {
            mesh = LoadMesh(path);
        }
        if (mesh == nullptr) {
            return "not loaded and couldn't be loaded: " + path;
        }
        return SetMesh(gPeople[static_cast<std::size_t>(n)].hair, mesh) ? "set " + PathOf(mesh) : "SetSkeletalMesh failed";
    }
    if (verb == "body") {
        // mevr hair body <n> <object path>: another body on person n (ME3: MEVR_Miranda.HMF_ARM_NKDc_MEVR, the underwear
        // body, to judge the soft tissue on the anatomy); her head and hair re-attached; `mevr hair standins off` puts the
        // game's back.
        std::string who;
        std::string path;
        words >> who >> path;
        int n = -1;
        Person named;
        if (!who.empty() && std::isdigit(static_cast<unsigned char>(who[0]))) {
            n = std::atoi(who.c_str());
        } else if (!who.empty()) {
            // By name: a pawn or conversation actor whose name or body mesh holds `who` (Liara's squad pawn, a stunt actor).
            ue3::Object* playerPawn = nullptr;
            ue3::Object* const world = PlayerWorld(&playerPawn);
            std::string const key = Lower(who);
            for (ue3::Object* q = world != nullptr ? gPawnList.Read<ue3::Object*>(world).value_or(nullptr) : nullptr; q != nullptr;
                 q = gNextPawn.Read<ue3::Object*>(q).value_or(nullptr)) {
                if (!ue3::runtime::IsAlive(q)) break;
                if (q == playerPawn || !player::IsBioPawn(q)) continue;
                ue3::Object* const body = player::Mesh(q);
                if (PathOf(q).find(key) != std::string::npos || (body != nullptr && PathOf(player::SkeletalMesh(body)).find(key) != std::string::npos)) {
                    ue3::Object* const head = gHeadMesh.Read<ue3::Object*>(q).value_or(nullptr);
                    named = {q, gHairMesh.Read<ue3::Object*>(q).value_or(nullptr), body, 0.0, head};
                    break;
                }
            }
            if (named.body == nullptr && world != nullptr) {
                for (ue3::Object* const s : StuntActors(world)) {
                    if (!ue3::runtime::IsAlive(s)) continue;
                    ue3::Object* const body = gStuntBody.Read<ue3::Object*>(s).value_or(nullptr);
                    if (PathOf(s).find(key) != std::string::npos || (body != nullptr && PathOf(player::SkeletalMesh(body)).find(key) != std::string::npos)) {
                        named = {s, gStuntHair.Read<ue3::Object*>(s).value_or(nullptr), body, 0.0, gStuntHead.Read<ue3::Object*>(s).value_or(nullptr)};
                        break;
                    }
                }
            }
        }
        if (path.empty() || (n < 0 && named.body == nullptr) || (n >= 0 && n >= static_cast<int>(gPeople.size()))) {
            return "mevr hair body <n | name> <object path> (mevr hair people; a name: a pawn's or conversation actor's, or its body mesh's)";
        }
        Person const& p = n >= 0 ? gPeople[static_cast<std::size_t>(n)] : named;
        if (p.body == nullptr) {
            return "that person has no body component";
        }
        ue3::Object* const mesh = StandInFor(Lower(path));
        if (mesh == nullptr) {
            return "not loaded and couldn't be loaded: " + path;
        }
        ue3::Object* const before = player::SkeletalMesh(p.body);
        // The character's own material override (her outfit's instance) would paint the new body: dropped for it, kept to
        // put back with the outfit.
        if (gBodyMaterial.find(p.body) == gBodyMaterial.end()) {
            gBodyMaterial[p.body] = hair_look::Material(p.body);
        }
        if (!SetMesh(p.body, mesh)) {
            return "SetSkeletalMesh failed";
        }
        hair_look::SetMaterial(p.body, nullptr);
        ue3::Object* original = before;
        for (auto it = gSwapped.begin(); it != gSwapped.end(); ++it) {
            if (it->component == p.body) {
                original = it->original;  // putting back restores the game's own
                gSwapped.erase(it);
                break;
            }
        }
        gSwapped.push_back({p.body, original, mesh});
        Reattach(p.body, FollowersOf(p));
        return std::format("[{}] wears {} (was {})", n >= 0 ? who : PathOf(p.pawn), PathOf(mesh), PathOf(before));
    }
    if (verb == "stream") {
        std::string package, onOff;
        words >> package >> onOff;
        if (package.empty()) {
            return "mevr hair stream <package or part of its name> [on|off]";
        }
        return StreamLevel(package, onOff != "off");
    }
    if (verb == "call") {
        std::string what, function, param;
        words >> what >> function >> param;
        if (what.empty() || function.empty()) {
            return "mevr hair call <part of an object's name> <Function> [param=int]";
        }
        return CallOn(what, function, param);
    }
    if (verb == "fire") {
        std::string what;
        int which = -1, input = 0;
        words >> what;
        if (!(words >> which)) {
            which = -1;
        }
        words >> input;
        if (what.empty()) {
            return "mevr hair fire <part of a Kismet op's path> [k] [input]";
        }
        return FireOp(what, which, input);
    }
    if (verb == "spawn") {
        std::string what;
        int which = -1;
        words >> what;
        if (!(words >> which)) {
            which = -1;
        }
        if (what.empty()) {
            return "mevr hair spawn <part of a loaded pawn archetype's path> [k]";
        }
        return SpawnPawn(what, which);
    }
    if (verb == "bring") {
        std::string what;
        int which = 0;
        words >> what;
        if (!(words >> which)) {
            which = 0;
        }
        if (what.empty()) {
            return "mevr hair bring <part of a pawn's name, hair or body mesh> [k]";
        }
        return Bring(what, which);
    }
    if (verb == "bodies") {
        // Which bodies near wear a rigged stand-in, and what the lookup by name found for each mesh this level.
        std::string reload;
        words >> reload;
        if (reload == "reload") {
            gBodies.Forget();
            gBodiesIndexRead = false;  // asked again from the next tick (a rebuilt package needs the game restarted: its TOC)
        }
        ReadBodiesIndex();
        bsi::Index const* const index = gBodies.GetIndex();
        std::string out = index != nullptr
            ? std::format("hair: rigged bodies: {}'s index lists {} game meshes; {} load(s) this level{}", index->Package(), index->Size(),
                  gBodies.Loads(), gBodies.PackageAbsent() ? ", the package isn't installed" : "")
            : std::string("hair: rigged bodies: no index (") + ini::Utf8(RigFolder()) + "\\MEVR_Bodies_" + MEVR_GAME_NAME_A +
                  ".json; tools/rig/build-bodies.ps1 builds it)";
        for (std::size_t i = 0; i < gPeople.size(); ++i) {
            Person const& p = gPeople[i];
            ue3::Object* const worn = player::SkeletalMesh(p.body);
            Swapped const* swapped = nullptr;
            for (Swapped const& s : gSwapped) {
                if (s.component == p.body && s.standIn == worn) swapped = &s;
            }
            if (swapped != nullptr) {
                out += std::format("\n  [{}] {} at {:.2f} m wears {} instead of {} ({}), jiggle x{:.2f}", i, PathOf(p.pawn), p.distanceM,
                    PathOf(worn), PathOf(swapped->original), swapped->byName ? "the bodies' index" : "the hand table", OutfitJiggle(p.body));
                continue;
            }
            auto const e = gBodies.Entries().find(PathOf(worn));
            out += std::format("\n  [{}] {} at {:.2f} m: {}: {}", i, PathOf(p.pawn), p.distanceM, PathOf(worn),
                HasSoftTissueBones(p.body) ? "rigged already"
                : e != gBodies.Entries().end() ? std::format("{}{}", bsi::Describe(e->second.outcome),
                      e->second.rig.empty() ? std::string() : " (" + e->second.rig + ")")
                : gSettings.standIns ? "not asked yet" : "stand-ins off");
        }
        for (auto const& [mesh, e] : gBodies.Entries()) {
            bool shown = false;
            for (Person const& p : gPeople) {
                shown = shown || PathOf(player::SkeletalMesh(p.body)) == mesh;
            }
            if (!shown) {
                out += std::format("\n  (asked) {}: {}", mesh, bsi::Describe(e.outcome));
            }
        }
        return out;
    }
    if (verb.empty() || verb == "people" || verb == "status") {
        std::string out = "hair: " + gStatus;
        hp::Stats const stats = hp::GetStats();
        for (std::size_t i = 0; i < gPeople.size(); ++i) {
            Person const& p = gPeople[i];
            if (p.hair == nullptr) {
                out += std::format("\n  [{}] pawn {:#x} at {:.2f} m: no hair; soft tissue on its body {}", i,
                    reinterpret_cast<std::uintptr_t>(p.pawn), p.distanceM, PathOf(player::SkeletalMesh(p.body)));
                continue;
            }
            ue3::Object* const asset = player::SkeletalMesh(p.hair);
            ue3::Object* const parent = engine::skeleton::PoseParent(p.hair);
            out += std::format("\n  [{}] pawn {:#x} at {:.2f} m: hair {:#x} {} ({} bones), pose parent {}", i,
                reinterpret_cast<std::uintptr_t>(p.pawn), p.distanceM, reinterpret_cast<std::uintptr_t>(p.hair), PathOf(asset),
                engine::skeleton::RefBoneCount(p.hair),
                parent != nullptr ? std::format("{:#x} {}", reinterpret_cast<std::uintptr_t>(parent), PathOf(player::SkeletalMesh(parent)))
                                  : std::string("none"));
            out += "\n      hair bones: " + HairBones(p.hair);
            ue3::Object* const simulated = SimulatedOn(p);
            if (simulated != p.hair) {
                out += std::format("\n      simulated on the body it follows ({:#x}): its hair bones draw the hair", reinterpret_cast<std::uintptr_t>(simulated));
            }
            for (hp::TargetInfo const& t : stats.targets) {
                if (t.hair != simulated) {
                    continue;
                }
                out += std::format("\n      stage: {} calls, {} written, {} collisions{}", t.calls, t.written, t.collisions,
                    t.lastReject != nullptr ? std::format(", last refusal: {}", t.lastReject) : std::string());
                for (hp::ChainInfo const& c : t.chains) {
                    std::string bones;
                    for (std::string const& b : c.bones) {
                        bones += (bones.empty() ? "" : " > ") + b;
                    }
                    out += std::format("\n      chain {}: tip {:.1f} cm off the animation, {} reset(s)", bones, c.tipOffsetM * 100.0, c.resets);
                }
                for (hp::SoftInfo const& s : t.soft) {
                    out += std::format("\n      soft tissue {}: {:.2f} cm off the animation (peak {:.2f}), {} reset(s), {} limit hit(s)", s.bone,
                        s.offsetM * 100.0, s.peakM * 100.0, s.resets, s.clampHits);
                }
            }
        }
        if (stats.broken) {
            out += "\n  the stage STOPPED after an access violation";
        }
        return out;
    }
    return "mevr hair [on|off|auto | people | bodies [reload] | trace <s> | kick <n> <x> <y> <z> | mesh <n> <path> | standins on|off | stream <package> [on|off] | bring <name> [k]]";
}

std::string StatusJson() {
    hp::Stats const stats = hp::GetStats();
    std::string out = std::format("{{\"on\": {}, \"status\": \"{}\", \"people\": {}, \"standIns\": {}, \"broken\": {}, \"targets\": [",
        gSettings.on ? "true" : "false", gStatus, gPeople.size(), gSwapped.size(), stats.broken ? "true" : "false");
    for (std::size_t i = 0; i < stats.targets.size(); ++i) {
        hp::TargetInfo const& t = stats.targets[i];
        out += std::format("{}{{\"calls\": {}, \"written\": {}, \"collisions\": {}, \"chains\": {}, \"peakTipM\": {:.3f}}}", i == 0 ? "" : ", ",
            t.calls, t.written, t.collisions, t.chains.size(), t.peakTipM);
    }
    out += "]}";
    return out;
}

void LogStats() {
    hp::Stats const stats = hp::GetStats();
    std::string detail;
    for (hp::TargetInfo const& t : stats.targets) {
        if (!t.chains.empty() || !t.soft.empty()) {
            double softPeak = 0.0;
            for (hp::SoftInfo const& s : t.soft) {
                softPeak = (std::max)(softPeak, s.peakM);
            }
            detail += std::format("; {:#x}: {} chains, {} soft-tissue bones, {} written of {} calls, tips up to {:.1f} cm, soft tissue up to {:.2f} cm, {} collisions{}",
                reinterpret_cast<std::uintptr_t>(t.hair), t.chains.size(), t.soft.size(), t.written, t.calls, t.peakTipM * 100.0, softPeak * 100.0,
                t.collisions, t.lastReject != nullptr ? std::format(" (last refusal: {})", t.lastReject) : std::string());
        }
    }
    log::Info("    hair: {}; {} stand-in(s){}{}", gStatus, gSwapped.size(), detail, stats.broken ? "; the stage STOPPED" : "");
    hp::ResetPeriod();
}

void Shutdown() {
    if (ue3::runtime::Up()) {
        PutBackStandIns("shutdown");
    }
    hp::Clear();
    gPeople.clear();
    gParts.clear();
    gStandInObjects.clear();
    gStandInMissing.clear();
    gBodies.Forget();
    gBodiesIndexRead = false;
    hair_look::Forget();
    gWorld = nullptr;
    gPublished = 0;
}

} // namespace mevr::game::hair
