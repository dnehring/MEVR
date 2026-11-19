// SPDX-License-Identifier: MIT
#include "game/VrCover.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/VrCover.hpp"
#include "engine/CallFunction.hpp"
#include "engine/SettingsFile.hpp"
#include "game/Player.hpp"
#include "game/Posture.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

// Shared geometric judgment, with native damage adapters per game.
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
#define MEVR_VR_COVER 1
#endif

namespace mevr::game::vr_cover {
namespace {

namespace vc = vr::cover;

#if defined(MEVR_VR_COVER)
// The damage call whose frame asks IsInCover, and its arguments read there. ME1: BioPawnBehavior.ProxyTakeDamage
// (IsInCover, then IsInCoverLeaning). ME2: SFXGame.ModifyDamage (IsInCoverLeaning, then IsInCover; research/23 section 7).
#if defined(MEVR_GAME_LE1)
ue3::FunctionRef gDamageFn{"SFXGame.BioPawnBehavior.ProxyTakeDamage", ue3::kEveryGame};
ue3::FieldRef gCauser{gDamageFn, "pDamageCausingActor"};
ue3::FunctionRef gIsDeadFn{"SFXGame.BioPawn.IsDead", ue3::kEveryGame};
#elif defined(MEVR_GAME_LE3)
ue3::FunctionRef gDamageFn{"SFXGame.SFXPawn_PlayerParty.ModifyDamage", ue3::kEveryGame};
ue3::FieldRef gCauser{gDamageFn, "DamageCauser"};
ue3::FieldRef gDamageType{gDamageFn, "DamageType"};
ue3::FunctionRef gIsDeadFn{"Engine.Pawn.IsDead", ue3::kEveryGame};
#else
ue3::FunctionRef gDamageFn{"SFXGame.SFXGame.ModifyDamage", ue3::kEveryGame};
ue3::FieldRef gCauser{gDamageFn, "DamageCauser"};
ue3::FieldRef gDamageType{gDamageFn, "DamageType"};
ue3::FunctionRef gIsDeadFn{"Engine.Pawn.IsDead", ue3::kEveryGame};
#endif
ue3::FieldRef gInstigatedBy{gDamageFn, "InstigatedBy"};
ue3::FieldRef gHitLocation{gDamageFn, "HitLocation"};
ue3::FunctionRef gIsInCoverFn{"SFXGame.BioPawn.IsInCover", ue3::kEveryGame};
ue3::FunctionRef gIsInCoverLeaningFn{"SFXGame.BioPawn.IsInCoverLeaning", ue3::kEveryGame};
ue3::FunctionRef gTraceFn{"Engine.Actor.Trace", ue3::kEveryGame};
ue3::FunctionRef gIsHostileFn{engine::adapter::kIsHostileFunction, ue3::kEveryGame};  // ME3: Engine.Pawn's
ue3::FieldRef gControllerPawn{"Engine.Controller", "Pawn", ue3::kEveryGame};
ue3::FieldRef gLocation{"Engine.Actor", "Location", ue3::kEveryGame};
ue3::FieldRef gEyeHeight{"Engine.Pawn", "EyeHeight", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList", ue3::kEveryGame};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn", ue3::kEveryGame};
ue3::FieldRef gHidden{"Engine.Actor", "bHidden", ue3::kEveryGame};
ue3::ClassRef gPawnClass{"Engine.Pawn", ue3::kEveryGame};
ue3::FieldRef gPawnWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
#if !defined(MEVR_GAME_LE1)
// The floor under the player's pawn (Location.z - CollisionHeight, the floor lock's floor), for the per-hit detail.
ue3::FieldRef gPawnCylinder{"Engine.Pawn", "CylinderComponent", ue3::kEveryGame};
ue3::FieldRef gCylinderHeight{"Engine.CylinderComponent", "CollisionHeight", ue3::kEveryGame};
#endif
#endif

#if defined(MEVR_GAME_LE1)
ue3::FieldRef gCoverNormal{"SFXGame.BioPawn", "CoverNormal"};
// `mevr cover hit`: the game's own damage path, from a pawn's place, with a weapon's damage object.
ue3::FunctionRef gTakeDamageFn{"SFXGame.BioActorBehavior.TakeDamage"};
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gWeaponDamage{"SFXGame.BioWeapon", "m_pDamage"};
ue3::ClassRef gBioWeaponClass{"SFXGame.BioWeapon"};
ue3::FieldRef gPawnAttributes{"SFXGame.BioEpicPawnBehavior", "m_PawnAttributes"};
ue3::FieldRef gHealthCurrent{"SFXGame.BioAttributesEpicPawn", "m_HealthCurrent"};
ue3::FieldRef gShield{"SFXGame.BioActorBehavior", "m_oShield"};
ue3::FieldRef gShieldAttributes{"SFXGame.BioShield", "m_pAttributes"};
ue3::FieldRef gShieldCapacity{"SFXGame.BioAttributesShield", "m_DamageCapacity"};
#endif

#if defined(MEVR_GAME_LE2)
// ME2's cone reads the game's cover slot: `Vector(TargetBP.CurrentLink.GetSlotRotation(TargetBP.CurrentSlotIdx))`. A VR
// player has no link (BioPawn.LeaveCover sets None), and a call on None never reaches CallFunction (the context is skipped,
// the rotator zeroed: the cone would face +X). So for the one damage call we answer "in cover", BioPawn.CurrentLink holds
// CoverLink's class default object, whose GetSlotRotation (0x490050) returns its own Rotation for any slot of its empty Slots
// array, and a frame post-handler on GetSlotRotation (inside ModifyDamage only) returns the hiding surface's facing instead:
// the game's own cone, distance, multipliers and its bIgnoresCoverDirection exception do the rest. The link is put back as
// soon as GetSlotRotation returns (and when ModifyDamage returns, and every tick).
ue3::FunctionRef gGetSlotRotationFn{"Engine.CoverLink.GetSlotRotation", ue3::kEveryGame};
ue3::FieldRef gCurrentLink{"SFXGame.BioPawn", "CurrentLink", ue3::kEveryGame};
ue3::ClassRef gCoverLinkClass{"Engine.CoverLink", ue3::kEveryGame};
ue3::ClassRef gSfxDamageTypeClass{"SFXGame.SFXDamageType", ue3::kEveryGame};
ue3::FieldRef gIgnoresCoverDirection{"SFXGame.SFXDamageType", "bIgnoresCoverDirection", ue3::kEveryGame};
ue3::FieldRef gDamagesFriends{"SFXGame.SFXDamageType", "bDamagesFriends", ue3::kEveryGame};
ue3::FieldRef gHealthDamage{"SFXGame.SFXDamageType", "bHealthDamage", ue3::kEveryGame};
// `mevr cover hit`: the player's damage module (SFXModule_DamagePlayer.SFXTakeDamage, which calls ModifyDamage), its health,
// and the shields in the pawn's inventory.
ue3::ClassRef gDamageModuleClass{"SFXGame.SFXModule_Damage", ue3::kEveryGame};
ue3::FieldRef gModules{"Engine.Actor", "Modules", ue3::kEveryGame};
ue3::FieldRef gCurrentHealth{"SFXGame.SFXModule_Damage", "CurrentHealth", ue3::kEveryGame};
ue3::FieldRef gInvManager{"Engine.Pawn", "InvManager", ue3::kEveryGame};
ue3::FieldRef gInventoryChain{"Engine.InventoryManager", "InventoryChain", ue3::kEveryGame};
ue3::FieldRef gInventoryNext{"Engine.Inventory", "Inventory", ue3::kEveryGame};
ue3::ClassRef gShieldClass{"SFXGame.SFXShield_Base", ue3::kEveryGame};
ue3::FieldRef gCurrentShields{"SFXGame.SFXShield_Base", "CurrentShields", ue3::kEveryGame};
ue3::FieldRef gPawnController{"Engine.Pawn", "Controller", ue3::kEveryGame};
ue3::FunctionRef gSetLocationFn{"Engine.Actor.SetLocation", ue3::kEveryGame};
// EObjectFlags: RF_ClassDefaultObject.
constexpr std::uint64_t kClassDefaultFlag = 0x200;
// The shields' faster recharge in cover (research/23 section 7.5): SFXShield_Base.Tick asks `!BioPawn(Owner).IsInCoverLeaning()
// && BioPawn(Owner).IsInCover()` while bFastCoverRegen is set and ShieldRegenTimer runs, and then takes an extra
// (PlayerCoverRegenTimerMultiplier - 1) * DeltaSeconds off the timer. Those two calls are answered for the VR player behind
// cover (judged against every awake hostile: no shot, no single shooter).
ue3::FunctionRef gShieldTickFn{"SFXGame.SFXShield_Base.Tick", ue3::kEveryGame};
ue3::FieldRef gShieldRegenTimer{"SFXGame.SFXShield_Base", "ShieldRegenTimer", ue3::kEveryGame};
ue3::FieldRef gFastCoverRegen{"SFXGame.SFXShield_Base", "bFastCoverRegen", ue3::kEveryGame};
#endif

// ---- Settings: [Cover] in the mod's settings file, read once a second, its defaults appended when it's missing --------
#if defined(MEVR_GAME_LE2)
constexpr char kBlock[] =
    "\n"
    "[Cover]\n"
    "; VR cover (research/23 sections 6 and 7): the game's own cover protection when you take cover with your body. The game\n"
    "; never snaps you into cover in VR ([Hands] noCover), so its rules for a player in cover (no damage from the cover's front\n"
    "; beyond 3 m, three quarters of it otherwise) never applied. With protect = 1, a hit counts as taken in cover when your head\n"
    "; is hidden from the shooter by something within reachM (metres) of your eyes: crouched behind a crate, beside a pillar.\n"
    "; Raise your head over it to shoot and you're in the open. The game's own rule does the rest. log = 1 logs every hit judged.\n"
    "; recharge = 1: the game's shorter wait before the shields recharge in cover, while your head is hidden from every awake\n"
    "; hostile, by cover within reachM from at least one of them.\n"
    "; judge: how your head counts as hidden. line = the line from your eyes to the shooter's eyes meets the cover; level = the\n"
    "; cover stands between you and the shooter at your eyes' height, whatever the shooter's height (as the game's own cover: a\n"
    "; drone above a crate doesn't see over it); either (the default) = by either. levelDropM: the level look that far below\n"
    "; the eyes (metres, 0 by default).\n"
    "protect = 1\n"
    "reachM = 1.2\n"
    "recharge = 1\n"
    "judge = either\n"
    "levelDropM = 0\n"
    "log = 0\n";
#elif defined(MEVR_GAME_LE3)
constexpr char kBlock[] =
    "\n[Cover]\n"
    "; VR cover (research/23 section 6): the game never snaps you into cover in VR ([Hands] noCover). With protect = 1, a hit\n"
    "; counts as taken in cover when your head is hidden from the shooter by something within reachM (metres) of your eyes and\n"
    "; an authored cover slot of the game's stands there: that slot's own damage rules apply. log = 1 logs every hit judged.\n"
    "; judge: line = the line from your eyes to the shooter's eyes meets the cover; level = the cover stands between you and\n"
    "; the shooter at your eyes' height, whatever the shooter's height; either (the default) = by either.\n"
    "protect = 1\nreachM = 1.2\njudge = either\nlevelDropM = 0\nlog = 0\n";
#else
constexpr char kBlock[] =
    "\n"
    "[Cover]\n"
    "; VR cover (research/23 section 6): the game's own cover protection when you take cover with your body. The game never\n"
    "; snaps you into cover in VR ([Hands] noCover), so its damage cut for a player in cover (all of a hit from the cover's\n"
    "; front on Casual and Normal, 90% of it above) never applied. With protect = 1, a hit counts as taken in cover when your\n"
    "; head is hidden from the shooter by something within reachM (metres) of your eyes: crouched behind a crate, beside a\n"
    "; pillar. Raise your head over it to shoot and you're in the open, as the game's Shepard is when leaning out. The game's\n"
    "; own rule does the rest: only shots from the cover's front, from more than 3 m. log = 1 logs every hit judged.\n"
    "protect = 1\n"
    "reachM = 1.2\n"
    "log = 0\n";
#endif

#if defined(MEVR_VR_COVER)
vc::Settings gSettings;
std::optional<bool> gOverride;  // `mevr cover on|off`
bool gRecharge = true;                  // [Cover] recharge (ME2: the shields' faster recharge behind cover)
std::optional<bool> gRechargeOverride;  // `mevr cover recharge on|off`
bool gLogHits = false;
#if !defined(MEVR_GAME_LE1)
vc::Rule gRule = vc::Rule::Either;      // [Cover] judge (the sequels; ME1 keeps the eye line)
std::optional<vc::Rule> gRuleOverride;  // `mevr cover judge line|level|either`, until the mod reloads
double gLevelDropM = 0.0;               // [Cover] levelDropM
std::uint64_t gHitDetailMs = 0;         // the last open hit's detail line (at most one a second)
#endif
std::uint64_t gSettingsAtMs = 0;
bool gTriedAppend = false;
std::string gSettingsText = "not read yet";

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsAtMs != 0 && now - gSettingsAtMs < 1000) {
        return;
    }
    gSettingsAtMs = now;
    std::filesystem::path const path = engine::SettingsFile();
    ini::Loaded const loaded = ini::Read(path);
    if (loaded.exists && loaded.file.find("Cover") == loaded.file.end() && !gTriedAppend) {
        gTriedAppend = true;
        if (ini::IsUtf16(loaded.encoding)) {
            log::Warn("vr cover: {} is saved as UTF-16: the [Cover] defaults aren't appended to it; they apply", ini::Utf8(path));
        } else {
            std::ofstream out(path, std::ios::app);
            out << kBlock;
            out.close();
            if (out) {
                log::Info("vr cover: added the [Cover] defaults to {}", ini::Utf8(path));
            } else {
                log::Warn("vr cover: couldn't add the [Cover] defaults to {} (read-only?); they apply", ini::Utf8(path));
            }
        }
    }
    vc::Settings s;
    if (auto const v = ini::GetNumber(loaded.file, "Cover", "protect")) {
        s.on = *v != 0.0;
    }
    if (auto const v = ini::GetNumber(loaded.file, "Cover", "reachM")) {
        s.reachM = std::clamp(*v, 0.2, 3.0);
    }
    bool logHits = false;
    if (auto const v = ini::GetNumber(loaded.file, "Cover", "log")) {
        logHits = *v != 0.0;
    }
    if (gOverride) {
        s.on = *gOverride;  // `mevr cover on|off` (tests), until the mod reloads
    }
    bool recharge = true;
    if (auto const v = ini::GetNumber(loaded.file, "Cover", "recharge")) {
        recharge = *v != 0.0;
    }
    if (gRechargeOverride) {
        recharge = *gRechargeOverride;
    }
#if defined(MEVR_GAME_LE2)
    if (recharge != gRecharge) {
        log::Info("vr cover: the shields' faster recharge behind cover {}", recharge ? "on ([Cover] recharge = 1)" : "off ([Cover] recharge = 0)");
    }
#endif
    gRecharge = recharge;
#if defined(MEVR_GAME_LE1)
    std::string const text = s.on ? std::format("on: a hit counts as taken in cover with the head hidden from the shooter by a surface "
                                                "within {:.2f} m of the eyes{}", s.reachM, logHits ? "; every judged hit logged" : "")
                                  : std::string("off ([Cover] protect = 0): a VR player takes every hit whole");
#else
    // The sequels: which look judges the head (research/23 section 7.7): the eye line, the level look, or either.
    vc::Rule rule = vc::Rule::Either;
    if (auto const v = ini::Get(loaded.file, "Cover", "judge")) {
        rule = vc::ParseRule(*v).value_or(rule);
    }
    if (gRuleOverride) {
        rule = *gRuleOverride;
    }
    double drop = 0.0;
    if (auto const v = ini::GetNumber(loaded.file, "Cover", "levelDropM")) {
        drop = std::clamp(*v, 0.0, 0.5);
    }
    gRule = rule;
    gLevelDropM = drop;
    char const* const how = rule == vc::Rule::Line ? "the line from the eyes to the shooter meets"
        : rule == vc::Rule::Level ? "the flat line from the eyes toward the shooter meets"
                                  : "the line from the eyes to the shooter, or the flat line toward it, meets";
    std::string const text = s.on ? std::format("on: a hit counts as taken in cover when {} a surface within {:.2f} m of the eyes ([Cover] "
                                                "judge = {}{}){}", how, s.reachM, vc::Name(rule),
                                        drop > 0.0 ? std::format(", the flat line {:.2f} m below the eyes", drop) : std::string(),
                                        logHits ? "; every judged hit logged" : "")
                                  : std::string("off ([Cover] protect = 0): a VR player takes every hit whole");
#endif
    if (text != gSettingsText) {
        log::Info("vr cover: {}", text);
        gSettingsText = text;
    }
    gSettings = s;
    gLogHits = logHits;
}
#else
vc::Settings gSettings;
#endif

// ---- State --------------------------------------------------------------------------------------------------------------
bool gStarted = false;
bool gHandlersReady = false;
ue3::Object* gPlayerPawn = nullptr;  // this tick's player pawn, while it can be judged
std::string gWhyNot = "not started";
std::uint8_t const* gAnsweredLocals = nullptr;  // the damage call's frame we said "in cover" to
#if defined(MEVR_GAME_LE1)
// The cover normal held during one damage call.
ue3::Vector* gHeldNormal = nullptr;
ue3::Vector gSavedNormal{};
#endif
#if defined(MEVR_GAME_LE2)
vc::Vec3 gAnsweredNormal;          // the hiding surface's facing for the answered call (from the cover toward the player)
bool gFacingPending = false;       // GetSlotRotation's answer still to give in that call
ue3::Object** gHeldLink = nullptr; // BioPawn.CurrentLink while it holds CoverLink's defaults
ue3::Object* gLinkDefaults = nullptr;
std::unordered_map<ue3::Class const*, ue3::Object*> gDamageDefaults;  // each SFXDamageType class's default object
std::uint64_t gDefaultsScanMs = 0;
bool gToldNoDefaults = false;
std::uint64_t gSlotAnswers = 0;
// The shields' faster recharge behind cover (research/23 section 7.5).
bool gRechargeReady = false;                 // SFXShield_Base.Tick and its fields found
std::uint8_t const* gShelterLocals = nullptr;  // the shield tick's frame we said "not leaning" to
struct Shelter {
    std::uint64_t atMs = 0;                  // judged then (again at most every kShelterEveryMs)
    vc::ShelterWhy why = vc::ShelterWhy::NoHostile;
    vc::ShelterCount count;
    std::string text = "not judged yet";
};
Shelter gShelter;
constexpr std::uint64_t kShelterEveryMs = 250;
constexpr double kShelterRangeM = 40.0;      // hostiles further than this aren't judged
constexpr int kShelterMaxHostiles = 16;      // the nearest this many
ue3::Object* gShelterAs = nullptr;           // `mevr cover regen as <n>`: this pawn stands in for the hostiles (tests)
std::uint64_t gShelterAnswerMs = 0;          // the last tick the shield's two questions were answered
struct Recharge {
    std::uint64_t asked = 0, answered = 0, exposed = 0, noHostile = 0, noCloseCover = 0, gameCover = 0, waits = 0;
};
Recharge gRechargeCounts, gLoggedRechargeCounts;
vc::RegenWait gWait;
std::string gLastWait = "none yet";
std::optional<bool> gFastRegen;  // the player's shield's bFastCoverRegen, as last read
vc::ShelterWhy gLoggedShelter = vc::ShelterWhy::NoHostile;
std::uint64_t gShelterLogMs = 0;
bool ShelteredNow(ue3::Object* pawn);  // below, after the pawn list
void TickRecharge(ue3::Object* pawn);
std::string RechargeJson();
#endif

struct Counts {
    std::uint64_t judged = 0, covered = 0, coneCut = 0, exposed = 0, tooFar = 0, overhead = 0, noShooter = 0, noView = 0,
                  gameCover = 0;
};
Counts gCounts;
Counts gLoggedCounts;
std::string gLast = "none yet";
std::uint64_t gLastLogMs = 0;

#if defined(MEVR_VR_COVER)
vc::Vec3 ToVec(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}
vc::Vec3 ToVec(engine::UnrealVector const& v) {
    return {v.x, v.y, v.z};
}
ue3::Vector ToUe(vc::Vec3 const& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

std::string NameOf(ue3::Object* object) {
    return object != nullptr ? ue3::runtime::Get().NameText(object) : std::string("nothing");
}

bool CallBool(ue3::Object* object, ue3::FunctionRef& ref) {
    ue3::Function* const function = ref.Get();
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

// The first surface from `from` toward `to` that isn't a pawn (a squadmate or the shooter in the line is looked past, at
// most twice), traced with the tracer (the player's pawn, which the trace ignores). Actors count (a dynamic crate).
std::optional<vc::Block> FirstSurface(ue3::Object* tracer, vc::Vec3 const& from, vc::Vec3 const& to, std::string* hitName = nullptr) {
    ue3::Function* const fn = gTraceFn.Get();
    if (tracer == nullptr || fn == nullptr) {
        return std::nullopt;
    }
    vc::Vec3 start = from;
    for (int attempt = 0; attempt < 3; ++attempt) {
        ue3::Params parms(ue3::runtime::Get(), fn);
        parms.Set("TraceEnd", ToUe(to));
        parms.Set("TraceStart", ToUe(start));
        parms.SetBool("bTraceActors", true);
        ue3::runtime::Call(tracer, fn, parms);
        ue3::Object* const hit = parms.Return<ue3::Object*>().value_or(nullptr);
        if (hit == nullptr) {
            return std::nullopt;
        }
        vc::Vec3 const at = ToVec(parms.Get<ue3::Vector>("HitLocation").value_or(ue3::Vector{}));
        vc::Vec3 const normal = ToVec(parms.Get<ue3::Vector>("HitNormal").value_or(ue3::Vector{}));
        if (!ue3::IsA(hit, gPawnClass)) {
            if (hitName != nullptr) {
                *hitName = NameOf(hit);
            }
            return vc::Block{at, normal};
        }
        // A person in the way: look past them.
        double const dx = to.x - start.x, dy = to.y - start.y, dz = to.z - start.z;
        double const length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length < 1.0) {
            return std::nullopt;
        }
        vc::Vec3 const next{at.x + dx / length * 5.0, at.y + dy / length * 5.0, at.z + dz / length * 5.0};
        if ((to.x - next.x) * dx + (to.y - next.y) * dy + (to.z - next.z) * dz <= 0.0) {
            return std::nullopt;
        }
        start = next;
    }
    return std::nullopt;
}

// Where a shooter's eyes are (the instigator's pawn: its location plus its eye height), and the game's own instigator
// location (the pawn's location, or on ME1 the damage causer's when there's no instigator).
struct Shooter {
    std::optional<vc::Vec3> eyes;
    std::optional<vc::Vec3> gameLocation;
    std::string name = "nobody";
};
Shooter FromPawn(ue3::Object* pawn) {
    Shooter s;
    if (pawn == nullptr || !ue3::runtime::IsAlive(pawn)) {
        return s;
    }
    s.name = NameOf(pawn);
    if (auto const at = gLocation.Read<ue3::Vector>(pawn)) {
        vc::Vec3 const location = ToVec(*at);
        s.gameLocation = location;
        float const eye = gEyeHeight.Read<float>(pawn).value_or(0.0f);
        s.eyes = vc::Vec3{location.x, location.y, location.z + eye};
    }
    return s;
}
Shooter FromCall(std::uint8_t const* locals) {
    ue3::Object* const instigatedBy = gInstigatedBy.Read<ue3::Object*>(locals).value_or(nullptr);
    if (instigatedBy != nullptr && ue3::runtime::IsAlive(instigatedBy)) {
        return FromPawn(gControllerPawn.Read<ue3::Object*>(instigatedBy).value_or(nullptr));
    }
    Shooter s;
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE3)
    if (ue3::Object* const causer = gCauser.Read<ue3::Object*>(locals).value_or(nullptr); causer && ue3::runtime::IsAlive(causer)) {
        s.name = NameOf(causer);
        if (auto const at = gLocation.Read<ue3::Vector>(causer)) {
            s.eyes = s.gameLocation = ToVec(*at);
        }
    }
#else
    // ME2's cone takes `InstigatedBy.Pawn.Location`, None without an instigator: no shooter, nothing answered (the hit is
    // the game's for a player out of cover).
    if (ue3::Object* const causer = gCauser.Read<ue3::Object*>(locals).value_or(nullptr); causer && ue3::runtime::IsAlive(causer)) {
        s.name = NameOf(causer) + " (no instigator)";
    }
#endif
    return s;
}

void Count(vc::Why why) {
    switch (why) {
    case vc::Why::Covered: ++gCounts.covered; break;
    case vc::Why::Exposed: ++gCounts.exposed; break;
    case vc::Why::CoverTooFar: ++gCounts.tooFar; break;
    case vc::Why::Overhead: ++gCounts.overhead; break;
    case vc::Why::NoShooter: ++gCounts.noShooter; break;
    case vc::Why::Off: break;
    }
}

std::string Describe(vc::Verdict const& v, Shooter const& shooter, vc::Vec3 const& eyes, std::string const& surface,
    std::optional<bool> cone) {
    double distance = 0.0;
    if (shooter.eyes) {
        distance = std::hypot(shooter.eyes->x - eyes.x, shooter.eyes->y - eyes.y, shooter.eyes->z - eyes.z) / gSettings.unitsPerMeter;
    }
    std::string text = std::format("{} at {:.1f} m: {}", shooter.name, distance, vc::Name(v.why));
    if (v.why == vc::Why::Covered) {
        text += std::format(" by {} {:.2f} m from the eyes, facing ({:.2f}, {:.2f})", surface, v.coverM, v.coverNormal.x, v.coverNormal.y);
        if (cone) {
#if defined(MEVR_GAME_LE2)
            text += *cone ? "; the game's cone cuts it to nothing" : "; outside the game's cone or within 3 m: the in-cover three quarters";
#else
            text += *cone ? "; the game's cone cuts it" : "; outside the game's cone or within 3 m: whole";
#endif
        }
    } else if (v.why == vc::Why::CoverTooFar) {
        text += std::format(" ({} {:.2f} m from the eyes)", surface, v.coverM);
    }
    return text;
}

#if !defined(MEVR_GAME_LE1)
// Both looks at one shooter (the sequels, research/23 section 7.7): the eye line (the line from the eyes to the shooter's eyes)
// and the level look (the flat line from the eyes toward the shooter, reachM long), the rule's verdict ([Cover] judge), and what
// each met, for the log. ME1 keeps the eye line alone (its code unchanged).
struct Look {
    vc::Verdict verdict, line, level;
    std::string lineSurface, levelSurface;
    std::optional<vc::Vec3> lineHit, levelHit;
    bool levelAsked = false;
};
unsigned gLevelOnly = 0;  // hits covered by the level look where the eye line was open (since the start)

Look LookAt(ue3::Object* self, vc::Vec3 const& eyes, Shooter const& shooter, vc::Settings const& settings) {
    Look look;
    std::optional<vc::Block> const block = shooter.eyes ? FirstSurface(self, eyes, *shooter.eyes, &look.lineSurface) : std::nullopt;
    if (block) {
        look.lineHit = block->point;
    }
    look.line = vc::Judge(eyes, shooter.eyes, block, settings);
    look.level = look.line;
    if (gRule != vc::Rule::Line && settings.on && shooter.eyes) {
        if (std::optional<vc::LevelLine> const flat = vc::Level(eyes, shooter.eyes, settings, gLevelDropM * settings.unitsPerMeter)) {
            look.levelAsked = true;
            std::optional<vc::Block> const levelBlock = FirstSurface(self, flat->from, flat->to, &look.levelSurface);
            if (levelBlock) {
                look.levelHit = levelBlock->point;
            }
            look.level = vc::Judge(eyes, shooter.eyes, levelBlock, settings);
        }
    }
    look.verdict = vc::Choose(gRule, look.line, look.level);
    return look;
}

bool ByLevel(Look const& look) {
    return look.verdict.Covered() && !look.line.Covered();
}

// The floor under a pawn (Location.z - CollisionHeight: the floor lock's), or nullopt.
std::optional<double> FloorUnder(ue3::Object* pawn) {
    std::optional<ue3::Vector> const at = gLocation.Read<ue3::Vector>(pawn);
    ue3::Object* const cylinder = gPawnCylinder.Read<ue3::Object*>(pawn).value_or(nullptr);
    std::optional<float> const height = cylinder != nullptr ? gCylinderHeight.Read<float>(cylinder) : std::nullopt;
    if (!at || !height) {
        return std::nullopt;
    }
    return static_cast<double>(at->z) - static_cast<double>(*height);
}

// What the two looks met, for the per-hit line: the eyes over the floor, the shooter above or below them, and each line's first
// surface (its distance, and where on the eye line it was against the eyes' height).
std::string HitDetail(Look const& look, ue3::Object* pawn, vc::Vec3 const& eyes, Shooter const& shooter, double upm) {
    std::string text;
    if (std::optional<double> const floor = FloorUnder(pawn)) {
        text += std::format("the eyes {:.2f} m over the floor", (eyes.z - *floor) / upm);
    } else {
        text += "the eyes' height unread";
    }
    if (shooter.eyes) {
        double const dz = shooter.eyes->z - eyes.z;
        double const flat = std::hypot(shooter.eyes->x - eyes.x, shooter.eyes->y - eyes.y);
        text += std::format(", the shooter's eyes {:+.2f} m from theirs ({:+.0f} deg)", dz / upm, std::atan2(dz, flat) * 180.0 / std::numbers::pi);
    }
    auto const met = [&](std::optional<vc::Vec3> const& at, std::string const& what) {
        if (!at) {
            return std::string("nothing");
        }
        double const d = std::hypot(at->x - eyes.x, at->y - eyes.y, at->z - eyes.z) / upm;
        return std::format("{} {:.2f} m out, {:+.2f} m from the eyes' height", what, d, (at->z - eyes.z) / upm);
    };
    text += "; the eye line met " + met(look.lineHit, look.lineSurface) + std::format(" ({})", vc::Name(look.line.why));
    if (look.levelAsked) {
        text += "; the flat line met " + (look.levelHit ? met(look.levelHit, look.levelSurface) : std::string("nothing within reach")) +
            std::format(" ({})", vc::Name(look.level.why));
    }
    return text;
}

// One hit on the player, judged in the damage call's frame: the verdict, counted and logged (the sequels: both looks, and a
// line for an open hit too, at most one a second, so a firefight says which test each hit failed).
vc::Verdict JudgeHit(ue3::Object* pawn, std::uint8_t const* locals) {
    ++gCounts.judged;
    auto const frame = view_anchor::Current();
    if (!frame) {
        ++gCounts.noView;
        return {};
    }
    vc::Vec3 const eyes = ToVec(frame->headWorld);
    vc::Settings settings = gSettings;
    settings.unitsPerMeter = frame->unitsPerMeter;
    Shooter const shooter = FromCall(locals);
    Look const look = LookAt(pawn, eyes, shooter, settings);
    vc::Verdict const verdict = look.verdict;
    Count(verdict.why);
    if (ByLevel(look)) {
        ++gLevelOnly;
    }
    std::optional<bool> cone;
    if (verdict.Covered() && shooter.gameLocation) {
#if defined(MEVR_GAME_LE2)
        // ME2's HitLocation is an out parameter: the frame's own copy of it isn't the hit (measured 2026-11-14: the cone
        // computed from it never cut, while the game's own cut every covered hit), so the player's place stands in for it,
        // as `mevr cover pawns` takes it.
        std::optional<ue3::Vector> const hit = gLocation.Read<ue3::Vector>(pawn);
#else
        std::optional<ue3::Vector> const hit = gHitLocation.Read<ue3::Vector>(locals);
#endif
        if (hit) {
            cone = vc::GameCone(verdict.coverNormal, ToVec(*hit), *shooter.gameLocation);
            if (*cone) {
                ++gCounts.coneCut;
            }
        }
    }
    gLast = Describe(verdict, shooter, eyes, ByLevel(look) ? look.levelSurface : look.lineSurface, cone) +
        (ByLevel(look) ? " (by the level look)" : "");
    std::uint64_t const now = GetTickCount64();
    bool const covered = verdict.Covered();
    if (gLogHits || (covered && now - gLastLogMs > 2000) || (!covered && verdict.why != vc::Why::NoShooter && now - gHitDetailMs > 1000)) {
        (covered ? gLastLogMs : gHitDetailMs) = now;
        log::Info("vr cover: a hit from {} | {}", gLast, HitDetail(look, pawn, eyes, shooter, settings.unitsPerMeter));
    }
    return verdict;
}
#else
// One hit on the player, judged in the damage call's frame: the verdict, counted and logged.
vc::Verdict JudgeHit(ue3::Object* pawn, std::uint8_t const* locals) {
    ++gCounts.judged;
    auto const frame = view_anchor::Current();
    if (!frame) {
        ++gCounts.noView;
        return {};
    }
    vc::Vec3 const eyes = ToVec(frame->headWorld);
    vc::Settings settings = gSettings;
    settings.unitsPerMeter = frame->unitsPerMeter;
    Shooter const shooter = FromCall(locals);
    std::string surface;
    std::optional<vc::Block> const block = shooter.eyes ? FirstSurface(pawn, eyes, *shooter.eyes, &surface) : std::nullopt;
    vc::Verdict const verdict = vc::Judge(eyes, shooter.eyes, block, settings);
    Count(verdict.why);
    std::optional<bool> cone;
    if (verdict.Covered() && shooter.gameLocation) {
#if defined(MEVR_GAME_LE2)
        // ME2's HitLocation is an out parameter: the frame's own copy of it isn't the hit (measured 2026-11-14: the cone
        // computed from it never cut, while the game's own cut every covered hit), so the player's place stands in for it,
        // as `mevr cover pawns` takes it.
        std::optional<ue3::Vector> const hit = gLocation.Read<ue3::Vector>(pawn);
#else
        std::optional<ue3::Vector> const hit = gHitLocation.Read<ue3::Vector>(locals);
#endif
        if (hit) {
            cone = vc::GameCone(verdict.coverNormal, ToVec(*hit), *shooter.gameLocation);
            if (*cone) {
                ++gCounts.coneCut;
            }
        }
    }
    gLast = Describe(verdict, shooter, eyes, surface, cone);
    std::uint64_t const now = GetTickCount64();
    if (gLogHits || (verdict.Covered() && now - gLastLogMs > 2000)) {
        gLastLogMs = now;
        log::Info("vr cover: a hit from {}", gLast);
    }
    return verdict;
}
#endif  // the sequels' JudgeHit or ME1's
#endif

#if defined(MEVR_GAME_LE1)
void PutBack() {
    if (gHeldNormal != nullptr) {
        *gHeldNormal = gSavedNormal;
        write_journal::Wrote("VrCover.coverNormal", gHeldNormal, sizeof(*gHeldNormal));
        gHeldNormal = nullptr;
    }
    gAnsweredLocals = nullptr;
}

// BioPawn.IsInCover, asked by BioPawnBehavior.ProxyTakeDamage for the player's pawn: true when the head is hidden from this
// shot's shooter, with the cover's facing in CoverNormal for the game's cone.
bool OnIsInCover(ue3::Object* context, ue3::Function* /*function*/, ue3::Function* caller, std::uint8_t* locals, void* result) {
    if (caller == nullptr || caller != gDamageFn.Get() || locals == nullptr || result == nullptr || context == nullptr ||
        context != gPlayerPawn || !gSettings.on) {
        return false;
    }
    vc::Verdict const verdict = JudgeHit(context, locals);
    if (!verdict.Covered()) {
        return false;
    }
    ue3::Vector* const normal = gCoverNormal.In<ue3::Vector>(context);
    if (normal == nullptr) {
        return false;
    }
    if (gHeldNormal == nullptr) {
        gSavedNormal = *normal;
    }
    gHeldNormal = normal;
    *normal = ToUe(verdict.coverNormal);
    write_journal::Wrote("VrCover.coverNormal", normal, sizeof(*normal));
    gAnsweredLocals = locals;
    *static_cast<std::uint32_t*>(result) = 1;
    return true;
}

// BioPawn.IsInCoverLeaning, right after: not leaning out (the head was hidden).
bool OnIsInCoverLeaning(ue3::Object* context, ue3::Function* /*function*/, ue3::Function* caller, std::uint8_t* locals, void* result) {
    if (caller == nullptr || caller != gDamageFn.Get() || locals == nullptr || locals != gAnsweredLocals || result == nullptr ||
        context != gPlayerPawn) {
        return false;
    }
    *static_cast<std::uint32_t*>(result) = 0;
    return true;
}
#endif

#if defined(MEVR_GAME_LE2)
void PutBackLink() {
    if (gHeldLink != nullptr) {
        *gHeldLink = nullptr;  // held only while it was None
        write_journal::Wrote("VrCover.currentLink", gHeldLink, sizeof(*gHeldLink));
        gHeldLink = nullptr;
    }
}

void PutBack() {
    PutBackLink();
    gFacingPending = false;
    gAnsweredLocals = nullptr;
}

// The default objects of every loaded SFXDamageType class, by class (one pass over the objects; again at most every 2 s
// when a class isn't there yet).
ue3::Object* DamageDefaults(ue3::Class const* cls) {
    if (cls == nullptr) {
        return nullptr;
    }
    if (auto const found = gDamageDefaults.find(cls); found != gDamageDefaults.end()) {
        return found->second;
    }
    std::uint64_t const now = GetTickCount64();
    ue3::Class* const base = gSfxDamageTypeClass.Get();
    if (base == nullptr || (gDefaultsScanMs != 0 && now - gDefaultsScanMs < 2000)) {
        return nullptr;
    }
    gDefaultsScanMs = now;
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && (r.FlagsOf(o) & kClassDefaultFlag) != 0 && r.IsA(o, base)) {
            gDamageDefaults[r.ClassOf(o)] = o;
        }
    }
    auto const found = gDamageDefaults.find(cls);
    return found != gDamageDefaults.end() ? found->second : nullptr;
}

// CoverLink's class default object (never in a level; its Slots array is empty).
ue3::Object* LinkDefaults() {
    if (gLinkDefaults != nullptr) {
        return gLinkDefaults;
    }
    ue3::Class* const cls = gCoverLinkClass.Get();
    if (cls == nullptr) {
        return nullptr;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.ClassOf(o) == cls && (r.FlagsOf(o) & kClassDefaultFlag) != 0) {
            gLinkDefaults = o;
            break;
        }
    }
    return gLinkDefaults;
}

// The shields' tick asks `!IsInCoverLeaning() && IsInCover()` for its owner while the wait before recharge runs (bFastCoverRegen
// set): for the VR player behind cover, not leaning (here) and in cover (OnShieldInCover), so the game takes its own extra
// decrement. The game's own cover keeps its own answers.
bool OnShieldLeaning(ue3::Object* context, std::uint8_t* locals, void* result) {
    if (!gRechargeReady || !gRecharge || !gSettings.on || context == nullptr || context != gPlayerPawn || locals == nullptr ||
        result == nullptr) {
        return false;
    }
    gShelterLocals = nullptr;
    ++gRechargeCounts.asked;
    if (CallBool(context, gIsInCoverFn)) {
        ++gRechargeCounts.gameCover;
        return false;
    }
    if (!ShelteredNow(context)) {
        return false;
    }
    gShelterLocals = locals;
    *static_cast<std::uint32_t*>(result) = 0;
    return true;
}
bool OnShieldInCover(ue3::Object* context, std::uint8_t* locals, void* result) {
    if (locals == nullptr || locals != gShelterLocals || context == nullptr || context != gPlayerPawn || result == nullptr) {
        return false;
    }
    gShelterLocals = nullptr;
    ++gRechargeCounts.answered;
    gShelterAnswerMs = GetTickCount64();
    *static_cast<std::uint32_t*>(result) = 1;
    return true;
}

// BioPawn.IsInCoverLeaning, asked first by SFXGame.ModifyDamage for the player's pawn: judged here; false (not leaning out)
// when the head is hidden from this shot's shooter. The game's own cover (taken by a script, or allowed: [Hands] noCover =
// 0) keeps its own rules: not answered.
bool OnIsInCoverLeaning(ue3::Object* context, ue3::Function* /*function*/, ue3::Function* caller, std::uint8_t* locals, void* result) {
    if (caller != nullptr && caller == gShieldTickFn.Get()) {
        return OnShieldLeaning(context, locals, result);
    }
    if (caller == nullptr || caller != gDamageFn.Get() || locals == nullptr || result == nullptr || context == nullptr ||
        context != gPlayerPawn || !gSettings.on) {
        return false;
    }
    PutBack();  // a call that left without its post-handler
    if (CallBool(context, gIsInCoverFn)) {
        ++gCounts.gameCover;
        return false;
    }
    if (gLinkDefaults == nullptr) {
        return false;  // the startup said why
    }
    vc::Verdict const verdict = JudgeHit(context, locals);
    if (!verdict.Covered()) {
        return false;
    }
    gAnsweredLocals = locals;
    gAnsweredNormal = verdict.coverNormal;
    *static_cast<std::uint32_t*>(result) = 0;
    return true;
}

// BioPawn.IsInCover, right after: true. Unless the damage type ignores the cover's direction (then the cone isn't asked),
// the cone's GetSlotRotation is made to reach us: BioPawn.CurrentLink holds CoverLink's defaults until it returns.
bool OnIsInCover(ue3::Object* context, ue3::Function* /*function*/, ue3::Function* caller, std::uint8_t* locals, void* result) {
    if (caller != nullptr && caller == gShieldTickFn.Get()) {
        return OnShieldInCover(context, locals, result);
    }
    if (caller == nullptr || caller != gDamageFn.Get() || locals == nullptr || locals != gAnsweredLocals || result == nullptr ||
        context != gPlayerPawn) {
        return false;
    }
    ue3::Class const* const damageType = gDamageType.Read<ue3::Class*>(locals).value_or(nullptr);
    ue3::Object* const defaults = DamageDefaults(damageType);
    std::optional<bool> const ignores = defaults != nullptr ? gIgnoresCoverDirection.ReadBool(defaults) : std::nullopt;
    if (!ignores) {
        if (!gToldNoDefaults) {
            gToldNoDefaults = true;
            log::Warn("vr cover: the damage type {} has no default object we can read: such a hit isn't answered (taken whole)",
                damageType != nullptr ? ue3::runtime::Get().NameText(reinterpret_cast<ue3::Object const*>(damageType)) : "None");
        }
        gAnsweredLocals = nullptr;  // IsInCoverLeaning said false, as the game's own answer is for a VR player
        return false;
    }
    if (!*ignores) {
        ue3::Object** const link = gCurrentLink.In<ue3::Object*>(context);
        if (link == nullptr) {
            gAnsweredLocals = nullptr;
            return false;
        }
        if (*link == nullptr) {
            *link = gLinkDefaults;
            write_journal::Wrote("VrCover.currentLink", link, sizeof(*link));
            gHeldLink = link;
        }
        gFacingPending = true;
    }
    *static_cast<std::uint32_t*>(result) = 1;
    return true;
}

// CoverLink.GetSlotRotation returned inside the answered ModifyDamage: the hiding surface's facing. The cone takes
// Vector(rotation) against the shot (`dot < -0.3`), so the rotation points from the player into the cover: the reversed
// normal, as ME1's `-CoverNormal`.
void OnGetSlotRotation(ue3::Object* /*context*/, ue3::Function* /*function*/, ue3::Function* caller, std::uint8_t* locals, void* result) {
    if (!gFacingPending || caller == nullptr || caller != gDamageFn.Get() || locals == nullptr || locals != gAnsweredLocals ||
        result == nullptr) {
        return;
    }
    double const yaw = std::atan2(-gAnsweredNormal.y, -gAnsweredNormal.x);  // radians
    std::int32_t rotation[3] = {0, static_cast<std::int32_t>(std::lround(yaw * 32768.0 / std::numbers::pi)), 0};  // pitch, yaw, roll
    std::memcpy(result, rotation, sizeof(rotation));
    write_journal::Wrote("VrCover.slotRotation", result, sizeof(rotation));
    ++gSlotAnswers;
    gFacingPending = false;
    PutBackLink();
}
#endif

#if defined(MEVR_GAME_LE3)
#include "game/Le3Cover.inc"
#include "engine/GameAdapter.hpp"
#endif

#if defined(MEVR_VR_COVER) && !defined(MEVR_GAME_LE3)
// The damage call returned: everything held, back.
void OnDamageDone(ue3::Object* /*context*/, ue3::Function* /*function*/, void* /*result*/) {
    PutBack();
}
#endif

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE3)
    return Le3Startup();
#elif defined(MEVR_VR_COVER)
    if (gStarted) {
        return true;
    }
    if (!ue3::runtime::Up()) {
        return false;
    }
    ue3::Function* const damage = gDamageFn.Get();
    ue3::Function* const inCover = gIsInCoverFn.Get();
    ue3::Function* const leaning = gIsInCoverLeaningFn.Get();
    if (damage == nullptr || inCover == nullptr || leaning == nullptr) {
        return false;
    }
    gStarted = true;
    ue3::Reflection& reflection = ue3::runtime::Get();
    auto noParameters = [&](ue3::Function* f) {
        return reflection.Parameters(f).size() == 1 && reflection.ParmsSize(f) == 4;  // only the bool return value
    };
#if defined(MEVR_GAME_LE1)
    bool const fieldsOk = gInstigatedBy.Get() != nullptr && gHitLocation.Get() != nullptr && gCauser.Get() != nullptr &&
        gCoverNormal.Get() != nullptr && gTraceFn.Get() != nullptr && gPawnClass.Get() != nullptr;
#else
    bool const fieldsOk = gInstigatedBy.Get() != nullptr && gHitLocation.Get() != nullptr && gCauser.Get() != nullptr &&
        gDamageType.Get() != nullptr && gCurrentLink.Get() != nullptr && gIgnoresCoverDirection.Get() != nullptr &&
        gGetSlotRotationFn.Get() != nullptr && gTraceFn.Get() != nullptr && gPawnClass.Get() != nullptr;
#endif
    if (!noParameters(inCover) || !noParameters(leaning) || !fieldsOk) {
        log::Warn("vr cover: off: {}", fieldsOk ? "IsInCover or IsInCoverLeaning has parameters (not this game's build?)"
                                               : "a field of the damage call or the pawn wasn't found");
        gWhyNot = "unavailable (see the log)";
        return true;
    }
#if defined(MEVR_GAME_LE2)
    if (LinkDefaults() == nullptr) {
        log::Warn("vr cover: off: CoverLink's class default object wasn't found (the cone's cover facing has nowhere to come from)");
        gWhyNot = "unavailable (see the log)";
        return true;
    }
    engine::call_function::AddFramePostHandler(gGetSlotRotationFn.Get(), &OnGetSlotRotation);
    // The shields' faster recharge behind cover: the same two answer handlers, for SFXShield_Base.Tick's frame.
    gRechargeReady = gShieldTickFn.Get() != nullptr && gShieldRegenTimer.Get() != nullptr && gFastCoverRegen.Get() != nullptr &&
        gShieldClass.Get() != nullptr && gInvManager.Get() != nullptr && gInventoryChain.Get() != nullptr &&
        gInventoryNext.Get() != nullptr && gIsHostileFn.Get() != nullptr && gIsDeadFn.Get() != nullptr;
    if (gRechargeReady) {
        log::Info("vr cover: SFXShield_Base.Tick's IsInCoverLeaning and IsInCover are answered for the player behind cover: the "
                  "game's shorter wait before the shields recharge ([Cover] recharge, research/23 section 7.5)");
    } else {
        log::Warn("vr cover: SFXShield_Base.Tick, its ShieldRegenTimer or bFastCoverRegen, or the pawn list's functions weren't "
                  "found: no faster recharge behind cover");
    }
#endif
    engine::call_function::AddAnswerHandler(inCover, &OnIsInCover);
    engine::call_function::AddAnswerHandler(leaning, &OnIsInCoverLeaning);
    engine::call_function::AddPostHandler(damage, &OnDamageDone);
    gHandlersReady = true;
#if defined(MEVR_GAME_LE2)
    log::Info("vr cover: SFXGame.ModifyDamage's IsInCoverLeaning and IsInCover are answered for the player in VR, and its cone's "
              "CoverLink.GetSlotRotation with the hiding surface's facing (research/23 section 7)");
#else
    log::Info("vr cover: the damage call's IsInCover and IsInCoverLeaning are answered for the player in VR (research/23 section 6)");
#endif
    return true;
#else
    return false;
#endif
}

bool CandidateReady() {
#if defined(MEVR_GAME_LE3)
    return gHandlersReady;
#else
    return false;
#endif
}

void Tick() {
#if defined(MEVR_VR_COVER)
    RefreshSettings();
    PutBack();  // never held between damage calls (a call that left without its post-handler)
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    if (!gHandlersReady) {
        gPlayerPawn = nullptr;
        gWhyNot = gStarted ? "unavailable (see the log)" : "the script packages aren't loaded";
    } else if (!engine::call_function::Installed()) {
        gPlayerPawn = nullptr;
        gWhyNot = "the CallFunction hook isn't in yet";
    } else if (!gSettings.on) {
        gPlayerPawn = nullptr;
        gWhyNot = "off ([Cover] protect = 0)";
    } else if (pawn == nullptr) {
        gPlayerPawn = nullptr;
        gWhyNot = "no player pawn";
    } else if (!posture::RefusingCover()) {
        gPlayerPawn = nullptr;
        gWhyNot = "the game's own cover applies (not VR on foot, or [Hands] noCover = 0)";
    } else {
        gPlayerPawn = pawn;
        gWhyNot = "active";
    }
#endif
#if defined(MEVR_GAME_LE2)
    gShelterLocals = nullptr;  // never held between ticks
    if (gShelterAs != nullptr && !ue3::runtime::IsAlive(gShelterAs)) {
        gShelterAs = nullptr;
    }
    TickRecharge(pawn);
#endif
}

std::string StatusJson() {
    return std::format("{{\"active\": {}, \"why\": \"{}\", \"protect\": {}, \"reachM\": {:.2f}, \"judged\": {}, \"covered\": {}, "
                       "\"coneCut\": {}, \"exposed\": {}, \"coverTooFar\": {}, \"overhead\": {}, \"noShooter\": {}, \"noView\": {}, "
                       "\"gameCover\": {}, \"last\": \"{}\", \"recharge\": {}}}",
        gPlayerPawn != nullptr, gWhyNot, gSettings.on, gSettings.reachM, gCounts.judged, gCounts.covered, gCounts.coneCut,
        gCounts.exposed, gCounts.tooFar, gCounts.overhead, gCounts.noShooter, gCounts.noView, gCounts.gameCover, [] {
            std::string s;
            for (char c : gLast) {
                s += (c == '"' || c == '\\') ? '\'' : c;
            }
            return s;
        }(),
#if defined(MEVR_GAME_LE2)
        RechargeJson()
#else
        std::string("{\"applies\": false}")  // ME1 and ME3 have no faster recharge in cover (research/23 section 7.5)
#endif
    );
}

void LogStats() {
#if defined(MEVR_GAME_LE2)
    Recharge const& r = gRechargeCounts;
    if (r.asked != gLoggedRechargeCounts.asked || r.waits != gLoggedRechargeCounts.waits) {
        log::Info("vr cover: the shields' recharge: {} ticks of the wait asked, {} answered behind cover ({} in the game's own cover); "
                  "judged: {} in the open, {} without a hostile, {} hidden only by far things; {} waits measured, last: {}; now: {}",
            r.asked, r.answered, r.gameCover, r.exposed, r.noHostile, r.noCloseCover, r.waits, gLastWait, gShelter.text);
        gLoggedRechargeCounts = r;
    }
#endif
#if defined(MEVR_GAME_LE3)
    if (gLe3Borrowed != gLe3LoggedBorrowed || gLe3NoSlot != gLe3LoggedNoSlot) {
        log::Info("vr cover: LE3 since the start: {} covered hits answered through a real cover slot (the game's cover rules), {} "
                  "covered hits with no qualified slot (the game's full damage)", gLe3Borrowed, gLe3NoSlot);
        gLe3LoggedBorrowed = gLe3Borrowed;
        gLe3LoggedNoSlot = gLe3NoSlot;
    }
#endif
    if (gCounts.judged == gLoggedCounts.judged && gCounts.gameCover == gLoggedCounts.gameCover) {
        return;
    }
#if defined(MEVR_GAME_LE1)
    log::Info("vr cover: {} hits judged since the start: {} in cover ({} cut by the game's cone), {} in the open, {} with cover too far, "
              "{} from overhead, {} without a shooter, {} without a view{}; last: {}",
        gCounts.judged, gCounts.covered, gCounts.coneCut, gCounts.exposed, gCounts.tooFar, gCounts.overhead, gCounts.noShooter,
        gCounts.noView, gCounts.gameCover > 0 ? std::format("; {} in the game's own cover (its rules)", gCounts.gameCover) : std::string(),
        gLast);
#else
    log::Info("vr cover: {} hits judged since the start ([Cover] judge = {}): {} in cover ({} by the level look alone, {} cut by the "
              "game's cone), {} in the open, {} with cover too far, {} from overhead, {} without a shooter, {} without a view{}; last: {}",
        gCounts.judged, vc::Name(gRule), gCounts.covered, gLevelOnly, gCounts.coneCut, gCounts.exposed, gCounts.tooFar, gCounts.overhead,
        gCounts.noShooter, gCounts.noView,
        gCounts.gameCover > 0 ? std::format("; {} in the game's own cover (its rules)", gCounts.gameCover) : std::string(), gLast);
#endif
    gLoggedCounts = gCounts;
}

#if defined(MEVR_VR_COVER)
namespace {

// The awake pawns other than Shepard, nearest first (`mevr cover pawns`, the shooters `hit` can use).
std::vector<ue3::Object*> Pawns(ue3::Object* self) {
    std::vector<std::pair<double, ue3::Object*>> found;
    std::optional<ue3::Vector> const at = gLocation.Read<ue3::Vector>(self);
    ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(self).value_or(nullptr);
    int guard = 0;
    for (ue3::Object* p = world != nullptr ? gPawnList.Read<ue3::Object*>(world).value_or(nullptr) : nullptr; p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        if (!ue3::runtime::Get().IsAlive(p)) {
            break;  // a level torn down under us leaves freed pawns in the list (CLAUDE.md, LivePawns)
        }
        if (p == self || !player::IsBioPawn(p) || gHidden.ReadBool(p).value_or(false) || CallBool(p, gIsDeadFn)) {
            continue;
        }
        std::optional<ue3::Vector> const there = gLocation.Read<ue3::Vector>(p);
        double const d = at && there ? std::hypot(static_cast<double>(there->x - at->x), static_cast<double>(there->y - at->y),
                                           static_cast<double>(there->z - at->z))
                                     : 1e12;
        found.emplace_back(d, p);
    }
    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::vector<ue3::Object*> out;
    for (auto const& entry : found) {
        out.push_back(entry.second);
    }
    return out;
}

bool Hostile(ue3::Object* self, ue3::Object* other) {
    ue3::Params hostile(ue3::runtime::Get(), gIsHostileFn.Get());
    hostile.Set("Other", other);
    ue3::runtime::Call(self, gIsHostileFn.Get(), hostile);
    return hostile.ReturnBool().value_or(false);
}

#if defined(MEVR_GAME_LE1)
// A weapon's damage object: the shooter's own weapon's, else any live weapon's.
ue3::Object* WeaponDamage(ue3::Object* shooter) {
    if (ue3::Object* const weapon = gPawnWeapon.Read<ue3::Object*>(shooter).value_or(nullptr)) {
        if (ue3::IsA(weapon, gBioWeaponClass)) {
            if (ue3::Object* const damage = gWeaponDamage.Read<ue3::Object*>(weapon).value_or(nullptr)) {
                return damage;
            }
        }
    }
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.IsAlive(o) && ue3::IsA(o, gBioWeaponClass)) {
            if (ue3::Object* const damage = gWeaponDamage.Read<ue3::Object*>(o).value_or(nullptr)) {
                return damage;
            }
        }
    }
    return nullptr;
}

// Shepard's health and shields.
std::pair<float, float> HealthAndShields(ue3::Object* pawn) {
    ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
    ue3::Object* const attributes = behavior != nullptr ? gPawnAttributes.Read<ue3::Object*>(behavior).value_or(nullptr) : nullptr;
    ue3::Object* const shield = behavior != nullptr ? gShield.Read<ue3::Object*>(behavior).value_or(nullptr) : nullptr;
    ue3::Object* const shieldAttributes = shield != nullptr ? gShieldAttributes.Read<ue3::Object*>(shield).value_or(nullptr) : nullptr;
    return {attributes != nullptr ? gHealthCurrent.Read<float>(attributes).value_or(-1.0f) : -1.0f,
        shieldAttributes != nullptr ? gShieldCapacity.Read<float>(shieldAttributes).value_or(-1.0f) : -1.0f};
}
#elif defined(MEVR_GAME_LE2)
// Shepard's damage module (Actor.Modules: SFXModule_DamagePlayer on the player).
ue3::Object* DamageModule(ue3::Object* pawn) {
    ue3::TArray<ue3::Object*> const* const modules = gModules.In<ue3::TArray<ue3::Object*>>(pawn);
    for (int i = 0; modules != nullptr && i < modules->Count(); ++i) {
        ue3::Object* const module = modules->data[i];
        if (module != nullptr && ue3::IsA(module, gDamageModuleClass)) {
            return module;
        }
    }
    return nullptr;
}

// Shepard's health (the damage module's CurrentHealth) and shields (every SFXShield_Base in the inventory, as
// SFXModule_Damage.SFXTakeDamage adds them up).
std::pair<float, float> HealthAndShields(ue3::Object* pawn) {
    ue3::Object* const module = DamageModule(pawn);
    float const health = module != nullptr ? gCurrentHealth.Read<float>(module).value_or(-1.0f) : -1.0f;
    float shields = -1.0f;
    ue3::Object* const manager = gInvManager.Read<ue3::Object*>(pawn).value_or(nullptr);
    int guard = 0;
    for (ue3::Object* item = manager != nullptr ? gInventoryChain.Read<ue3::Object*>(manager).value_or(nullptr) : nullptr;
         item != nullptr && guard < 64; item = gInventoryNext.Read<ue3::Object*>(item).value_or(nullptr), ++guard) {
        if (ue3::IsA(item, gShieldClass)) {
            shields = std::max(shields, 0.0f) + gCurrentShields.Read<float>(item).value_or(0.0f);
        }
    }
    return {health, shields};
}

// A damage type for `mevr cover hit` from a squadmate: the game refuses a hit from a friend unless its damage type
// bDamagesFriends (SFXGame.ModifyDamage). One that also takes health and doesn't ignore the cover's direction, by name.
ue3::Class* FriendlyFireType() {
    DamageDefaults(gSfxDamageTypeClass.Get());  // fills the table
    ue3::Class* best = nullptr;
    std::string bestName;
    for (auto const& [cls, defaults] : gDamageDefaults) {
        if (defaults == nullptr || !gDamagesFriends.ReadBool(defaults).value_or(false) || !gHealthDamage.ReadBool(defaults).value_or(false) ||
            gIgnoresCoverDirection.ReadBool(defaults).value_or(true)) {
            continue;
        }
        std::string const name = ue3::runtime::Get().NameText(reinterpret_cast<ue3::Object const*>(cls));
        if (best == nullptr || name < bestName) {
            best = const_cast<ue3::Class*>(cls);
            bestName = name;
        }
    }
    return best;
}

// The player's shield (the first SFXShield_Base in the pawn's inventory: the one whose tick asks about cover).
ue3::Object* PlayerShield(ue3::Object* pawn) {
    ue3::Object* const manager = gInvManager.Read<ue3::Object*>(pawn).value_or(nullptr);
    int guard = 0;
    for (ue3::Object* item = manager != nullptr ? gInventoryChain.Read<ue3::Object*>(manager).value_or(nullptr) : nullptr;
         item != nullptr && guard < 64; item = gInventoryNext.Read<ue3::Object*>(item).value_or(nullptr), ++guard) {
        if (!ue3::runtime::IsAlive(item)) {
            return nullptr;
        }
        if (ue3::IsA(item, gShieldClass)) {
            return item;
        }
    }
    return nullptr;
}

// Behind cover outside a shot: every awake hostile within kShelterRangeM (the nearest kShelterMaxHostiles) judged as a shot
// from it would be (from its eyes; `mevr cover regen as <n>` puts one pawn in their place for tests), tallied by the shared rule.
void JudgeShelter(ue3::Object* pawn, Shelter& out, std::string* detail = nullptr) {
    out.count = {};
    auto const frame = view_anchor::Current();
    if (!frame) {
        out.why = vc::ShelterWhy::NoHostile;
        out.text = "no head-tracked view";
        return;
    }
    vc::Vec3 const eyes = ToVec(frame->headWorld);
    vc::Settings settings = gSettings;
    settings.on = true;
    settings.unitsPerMeter = frame->unitsPerMeter;
    int judged = 0;
    for (ue3::Object* p : Pawns(pawn)) {
        if (gShelterAs != nullptr ? p != gShelterAs : !Hostile(pawn, p)) {
            continue;
        }
        Shooter const shooter = FromPawn(p);
        if (!shooter.eyes) {
            continue;
        }
        double const metres = std::hypot(shooter.eyes->x - eyes.x, shooter.eyes->y - eyes.y, shooter.eyes->z - eyes.z) /
            settings.unitsPerMeter;
        if (metres > kShelterRangeM) {
            break;  // nearest first
        }
        Look const look = LookAt(pawn, eyes, shooter, settings);  // the same looks as a shot from it (section 7.7)
        vc::Add(out.count, look.verdict.why);
        if (detail != nullptr) {
            *detail += "\n    " + Describe(look.verdict, shooter, eyes, ByLevel(look) ? look.levelSurface : look.lineSurface, std::nullopt) +
                (ByLevel(look) ? " (by the level look)" : "");
        }
        if (++judged >= kShelterMaxHostiles) {
            break;
        }
    }
    out.why = vc::Shelter(out.count);
    out.text = std::format("{} ({} hostile{} judged{}: {} hidden by cover within {:.2f} m, {} in the open, {} beyond far walls, {} "
                           "overhead)",
        vc::Name(out.why), out.count.hostiles, out.count.hostiles == 1 ? "" : "s", gShelterAs != nullptr ? ", a test stand-in" : "",
        out.count.covered, gSettings.reachM, out.count.exposed, out.count.blockedFar, out.count.overhead);
}

bool ShelteredNow(ue3::Object* pawn) {
    std::uint64_t const now = GetTickCount64();
    if (gShelter.atMs != 0 && now - gShelter.atMs < kShelterEveryMs) {
        return gShelter.why == vc::ShelterWhy::Sheltered;
    }
    gShelter.atMs = now;
    JudgeShelter(pawn, gShelter);
    switch (gShelter.why) {
    case vc::ShelterWhy::Exposed: ++gRechargeCounts.exposed; break;
    case vc::ShelterWhy::NoHostile: ++gRechargeCounts.noHostile; break;
    case vc::ShelterWhy::NoCloseCover: ++gRechargeCounts.noCloseCover; break;
    case vc::ShelterWhy::Sheltered: break;
    }
    if (gShelter.why != gLoggedShelter && (gLogHits || now - gShelterLogMs > 2000)) {
        gShelterLogMs = now;
        gLoggedShelter = gShelter.why;
        log::Info("vr cover: while the shields wait: {}{}", gShelter.text,
            gShelter.why == vc::ShelterWhy::Sheltered ? ": the game's faster recharge in cover applies" : "");
    }
    return gShelter.why == vc::ShelterWhy::Sheltered;
}

// Each mod tick: the wait before the shields recharge, from the game's own timer, and how much of it was behind cover.
void TickRecharge(ue3::Object* pawn) {
    if (!gRechargeReady || pawn == nullptr || !ue3::runtime::IsAlive(pawn)) {
        return;
    }
    ue3::Object* const shield = PlayerShield(pawn);
    if (shield == nullptr) {
        return;
    }
    gFastRegen = gFastCoverRegen.ReadBool(shield);
    std::optional<float> const timer = gShieldRegenTimer.Read<float>(shield);
    if (!timer) {
        return;
    }
    std::uint64_t const now = GetTickCount64();
    bool const sheltered = gShelterAnswerMs != 0 && now - gShelterAnswerMs <= 300;
    if (std::optional<vc::RegenWaitDone> const done = vc::Step(gWait, static_cast<double>(now) / 1000.0, *timer, sheltered)) {
        ++gRechargeCounts.waits;
        gLastWait = std::format("{:.2f} s after a hit that set {:.2f} s, {:.2f} s of it behind cover (the game's fast regen in "
                                "cover {})",
            done->waitS, done->startTimer, done->shelteredS, gFastRegen ? (*gFastRegen ? "on" : "off") : "unread");
        log::Info("vr cover: the shields' wait before recharging: {}", gLastWait);
    }
}

std::string RechargeJson() {
    std::string text;
    for (char c : gShelter.text + " | " + gLastWait) {
        text += (c == '"' || c == '\\') ? '\'' : c;
    }
    Recharge const& r = gRechargeCounts;
    return std::format("{{\"applies\": true, \"ready\": {}, \"on\": {}, \"fastRegen\": {}, \"waiting\": {}, \"asked\": {}, "
                       "\"answered\": {}, \"gameCover\": {}, \"waits\": {}, \"shelter\": \"{}\", \"text\": \"{}\"}}",
        gRechargeReady, gRecharge, gFastRegen ? (*gFastRegen ? "true" : "false") : "null", gWait.waiting, r.asked, r.answered,
        r.gameCover, r.waits, vc::Name(gShelter.why), text);
}
#endif

std::string Judged(ue3::Object* self, vc::Vec3 const& eyes, std::optional<vc::Vec3> const& selfAt, Shooter const& shooter,
    vc::Settings const& settings) {
#if defined(MEVR_GAME_LE1)
    std::string surface;
    std::optional<vc::Block> const block = shooter.eyes ? FirstSurface(self, eyes, *shooter.eyes, &surface) : std::nullopt;
    vc::Verdict const verdict = vc::Judge(eyes, shooter.eyes, block, settings);
    std::optional<bool> cone;
    if (verdict.Covered() && shooter.gameLocation && selfAt) {
        cone = vc::GameCone(verdict.coverNormal, *selfAt, *shooter.gameLocation);
    }
    return Describe(verdict, shooter, eyes, surface, cone);
#else
    Look const look = LookAt(self, eyes, shooter, settings);
    std::optional<bool> cone;
    if (look.verdict.Covered() && shooter.gameLocation && selfAt) {
        cone = vc::GameCone(look.verdict.coverNormal, *selfAt, *shooter.gameLocation);
    }
    return Describe(look.verdict, shooter, eyes, ByLevel(look) ? look.levelSurface : look.lineSurface, cone) +
        (ByLevel(look) ? " (by the level look)" : "") + " | " + HitDetail(look, self, eyes, shooter, settings.unitsPerMeter);
#endif
}

} // namespace
#endif

std::string Console(std::string_view args) {
#if defined(MEVR_VR_COVER)
    std::istringstream words{std::string(args)};
    std::string sub;
    words >> sub;
    if (sub.empty()) {
        return StatusJson();
    }
    if (sub == "on" || sub == "off") {
        gOverride = sub == "on";
        gSettingsAtMs = 0;
        RefreshSettings();
        return std::format("vr cover: {} until the mod reloads ([Cover] protect stays as it is)", sub);
    }
    if (sub == "snap" || sub == "where" || sub == "acquire" || sub == "links") {
        return posture::CoverConsole(args);  // the cover snapping's refusal and its test (game/posture)
    }
#if defined(MEVR_GAME_LE2)
    if (sub == "recharge") {
        // `mevr cover recharge [on|off]`: the shields' faster recharge behind cover, overridden until the mod reloads.
        std::string value;
        words >> value;
        if (value == "on" || value == "off") {
            gRechargeOverride = value == "on";
            gSettingsAtMs = 0;
            RefreshSettings();
            return std::format("vr cover: the faster recharge behind cover {} until the mod reloads ([Cover] recharge stays as it is)", value);
        }
        return RechargeJson();
    }
#endif
#if !defined(MEVR_GAME_LE1)
    if (sub == "judge") {
        // `mevr cover judge line|level|either|auto`: which look judges the head (research/23 section 7.7), until the mod reloads.
        std::string value;
        words >> value;
        if (value == "auto") {
            gRuleOverride.reset();
        } else if (std::optional<vc::Rule> const rule = vc::ParseRule(value)) {
            gRuleOverride = *rule;
        } else {
            return std::format("usage: mevr cover judge line|level|either|auto (now {})", vc::Name(gRule));
        }
        gSettingsAtMs = 0;
        RefreshSettings();
        return std::format("vr cover: judged by the {} until the mod reloads ([Cover] judge stays as it is)",
            gRule == vc::Rule::Line ? "eye line" : gRule == vc::Rule::Level ? "level look" : "eye line or the level look");
    }
#endif
    if (sub != "test" && sub != "pawns" && sub != "hit" && sub != "probe" && sub != "put" && sub != "regen") {
        return "usage: mevr cover [on|off | test | pawns | hit <damage> <n> | probe <yawDeg> [rangeM] | put <me|n> <x> <y> <z> | "
               "regen [as <n>|as off] | recharge [on|off] | snap on|off|auto | where | acquire | links [n]]";
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    auto const frame = view_anchor::Current();
    if (pawn == nullptr || !frame) {
        return "mevr cover " + sub + ": needs the player on foot with a head-tracked view";
    }
    if (gIsHostileFn.Get() == nullptr || gIsDeadFn.Get() == nullptr || gTraceFn.Get() == nullptr) {
        return "mevr cover " + sub + ": BioPawn.IsHostile, IsDead or Actor.Trace wasn't found";
    }
    RefreshSettings();
    vc::Vec3 const eyes = ToVec(frame->headWorld);
    vc::Settings settings = gSettings;
    settings.on = true;
    settings.unitsPerMeter = frame->unitsPerMeter;
    std::optional<vc::Vec3> selfAt;
    if (auto const at = gLocation.Read<ue3::Vector>(pawn)) {
        selfAt = ToVec(*at);
    }
    if (sub == "probe") {
        // `mevr cover probe <yawDeg> [rangeM]`: the first surface level from the eyes along a world yaw (0 = +X, 90 = +Y), for
        // finding cover to test against.
        double yaw = 0.0, range = 20.0;
        words >> yaw >> range;
        double const r = yaw * 3.14159265358979 / 180.0;
        double const units = range * frame->unitsPerMeter;
        vc::Vec3 const end{eyes.x + std::cos(r) * units, eyes.y + std::sin(r) * units, eyes.z};
        std::string surface;
        std::optional<vc::Block> const block = FirstSurface(pawn, eyes, end, &surface);
        if (!block) {
            return std::format("mevr cover probe: nothing within {:.1f} m along yaw {:.0f} from ({:.0f}, {:.0f}, {:.0f})", range, yaw, eyes.x,
                eyes.y, eyes.z);
        }
        double const d = std::hypot(block->point.x - eyes.x, block->point.y - eyes.y, block->point.z - eyes.z) / frame->unitsPerMeter;
        return std::format("mevr cover probe: {} at {:.2f} m along yaw {:.0f}: ({:.0f}, {:.0f}, {:.0f}), normal ({:.2f}, {:.2f}, {:.2f})", surface,
            d, yaw, block->point.x, block->point.y, block->point.z, block->normal.x, block->normal.y, block->normal.z);
    }
    std::vector<ue3::Object*> const pawns = Pawns(pawn);
#if defined(MEVR_GAME_LE2)
    if (sub == "regen") {
        // `mevr cover regen [as <n> | as off]`: the player's shield (its wait before recharging, the game's fast regen in cover)
        // and the shelter judged now, hostile by hostile; `as <n>` judges against pawn n of `mevr cover pawns` instead of the
        // hostiles (a fixture without hostiles), until `as off` or the mod reloads.
        std::string word;
        if (words >> word && word == "as") {
            std::string who;
            words >> who;
            if (who == "off") {
                gShelterAs = nullptr;
            } else {
                std::size_t const index = static_cast<std::size_t>(std::strtoul(who.c_str(), nullptr, 10));
                if (index >= pawns.size()) {
                    return std::format("mevr cover regen as: no pawn {} ({} listed)", who, pawns.size());
                }
                gShelterAs = pawns[index];
            }
            gShelter.atMs = 0;
        }
        ue3::Object* const shield = PlayerShield(pawn);
        Shelter now;
        std::string detail;
        JudgeShelter(pawn, now, &detail);
        return std::format("vr cover regen ({}; recharge {}): shield {}, its wait {:.2f} s, the game's fast regen in cover {}, "
                           "shields {:.1f}; behind cover now: {}{}{}; last wait: {}",
            gWhyNot, gRecharge ? "on" : "off", NameOf(shield),
            shield != nullptr ? gShieldRegenTimer.Read<float>(shield).value_or(-1.0f) : -1.0f,
            shield != nullptr ? (gFastCoverRegen.ReadBool(shield).value_or(false) ? "on" : "off") : "?",
            HealthAndShields(pawn).second, now.text, gShelterAs != nullptr ? " (as " + NameOf(gShelterAs) + ")" : std::string(),
            detail, gLastWait);
    }
    if (sub == "put") {
        // `mevr cover put <me|n> <x> <y> <z>`: Shepard (me) or pawn n of `mevr cover pawns` moved there (Engine.Actor.SetLocation
        // through ProcessEvent, this tick), for placing the shooter or Shepard behind a surface `probe` found.
        std::string who;
        ue3::Vector to{};
        if (!(words >> who >> to.x >> to.y >> to.z)) {
            return "usage: mevr cover put <me|n> <x> <y> <z>";
        }
        ue3::Object* target = nullptr;
        if (who == "me") {
            target = pawn;
        } else {
            std::size_t const index = static_cast<std::size_t>(std::strtoul(who.c_str(), nullptr, 10));
            target = index < pawns.size() ? pawns[index] : nullptr;
        }
        ue3::Function* const setLocation = gSetLocationFn.Get();
        if (target == nullptr || setLocation == nullptr) {
            return target == nullptr ? std::format("mevr cover put: no pawn {} ({} listed)", who, pawns.size())
                                     : std::string("mevr cover put: Engine.Actor.SetLocation not found");
        }
        ue3::Params parms(ue3::runtime::Get(), setLocation);
        parms.Set("NewLocation", to);
        ue3::runtime::Call(target, setLocation, parms);
        write_journal::Wrote("VrCover.SetLocation", target, 0);
        std::optional<ue3::Vector> const now = gLocation.Read<ue3::Vector>(target);
        return std::format("mevr cover put: {} {} at ({:.1f}, {:.1f}, {:.1f})", NameOf(target), parms.ReturnBool().value_or(false) ? "now" : "REFUSED, still",
            now ? now->x : 0.0f, now ? now->y : 0.0f, now ? now->z : 0.0f);
    }
#endif
    if (sub == "pawns") {
        // Each awake pawn as a shooter would be judged from its location.
        std::string out = std::format("vr cover ({}): the eyes at ({:.0f}, {:.0f}, {:.0f}); {} pawns", gWhyNot, eyes.x, eyes.y, eyes.z,
            pawns.size());
        for (std::size_t i = 0; i < pawns.size() && i < 24; ++i) {
            Shooter shooter = FromPawn(pawns[i]);
#if defined(MEVR_GAME_LE1)
            shooter.eyes = shooter.gameLocation;  // `hit` has no instigator on ME1: the game and VR cover take the causer's place
#endif
            std::optional<ue3::Vector> const there = gLocation.Read<ue3::Vector>(pawns[i]);
            out += std::format("\n    [{}] {}{} (at {:.0f}, {:.0f}, {:.0f})", i, Hostile(pawn, pawns[i]) ? "hostile " : "",
                Judged(pawn, eyes, selfAt, shooter, settings), there ? there->x : 0.0f, there ? there->y : 0.0f, there ? there->z : 0.0f);
        }
        return out;
    }
    if (sub == "hit") {
#if defined(MEVR_GAME_LE3)
        return "mevr cover hit: LE3 candidate observes real native damage; synthetic damage replay is not implemented";
#else
        float damage = 0.0f;
        std::size_t index = 0;
        words >> damage >> index;
        if (damage <= 0.0f || index >= pawns.size()) {
            return std::format("usage: mevr cover hit <damage> <n> (n of the {} pawns `mevr cover pawns` lists)", pawns.size());
        }
#if defined(MEVR_GAME_LE1)
        // The game's own damage path (BioActorBehavior.TakeDamage on Shepard's behaviour) from pawn n's place, no instigator
        // (no friendly-fire refusal; the game then takes the causer's location, as VR cover does), with a weapon's damage
        // object. Health and shields before and after, and what VR cover judged.
        ue3::Function* const take = gTakeDamageFn.Get();
        ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
        ue3::Object* const kind = WeaponDamage(pawns[index]);
        if (take == nullptr || behavior == nullptr || kind == nullptr || !selfAt) {
            return std::format("mevr cover hit: missing {}", take == nullptr ? "BioActorBehavior.TakeDamage"
                                                              : behavior == nullptr ? "Shepard's behaviour"
                                                              : kind == nullptr ? "a weapon's damage object" : "Shepard's location");
        }
        auto const before = HealthAndShields(pawn);
        Counts const countsBefore = gCounts;
        ue3::Params parms(ue3::runtime::Get(), take);
        parms.Set("Damage", damage);
        parms.Set("HitLocation", ToUe(*selfAt));
        parms.Set("DamageType", reinterpret_cast<ue3::Object*>(ue3::runtime::Get().ClassOf(kind)));
        parms.Set("pDamageCausingActor", pawns[index]);
        parms.Set("pDamage", kind);
        ue3::runtime::Call(behavior, take, parms);
        auto const after = HealthAndShields(pawn);
        return std::format("vr cover hit: {:.0f} damage from [{}] {} ({}): health {:.1f} -> {:.1f}, shields {:.1f} -> {:.1f}; judged {}, "
                           "covered {}, the cone cut {}; {}",
            damage, index, NameOf(pawns[index]), NameOf(kind), before.first, after.first, before.second, after.second,
            gCounts.judged - countsBefore.judged, gCounts.covered - countsBefore.covered, gCounts.coneCut - countsBefore.coneCut,
            gCounts.judged > countsBefore.judged ? gLast : std::string("VR cover wasn't asked (") + gWhyNot + ")");
#elif defined(MEVR_GAME_LE2)
        // The game's own damage path on ME2: the player's damage module's SFXTakeDamage (SFXModule_DamagePlayer, which calls
        // SFXGame.ModifyDamage and then the health), from pawn n's place: its controller the instigator (ME2's cone is taken
        // from InstigatedBy.Pawn.Location), the pawn the causer. A hostile's hit takes the default damage type; a friend's a
        // type that bDamagesFriends (else ModifyDamage refuses a hit from a friend), named in the answer. Health and shields
        // before and after, and what VR cover judged.
        ue3::Object* const module = DamageModule(pawn);
        ue3::Function* const take = module != nullptr
            ? ue3::runtime::Get().FindFunction(ue3::runtime::Get().ClassOf(module), "SFXTakeDamage") : nullptr;
        ue3::Object* const instigator = gPawnController.Read<ue3::Object*>(pawns[index]).value_or(nullptr);
        if (take == nullptr || instigator == nullptr || !selfAt) {
            return std::format("mevr cover hit: missing {}", module == nullptr ? "Shepard's damage module"
                                                              : take == nullptr ? "SFXTakeDamage on it"
                                                              : instigator == nullptr ? "the shooter's controller" : "Shepard's location");
        }
        bool const hostile = Hostile(pawn, pawns[index]);
        ue3::Class* const kind = hostile ? nullptr : FriendlyFireType();
        if (!hostile && kind == nullptr) {
            return "mevr cover hit: the shooter is a friend and no loaded damage type bDamagesFriends (with health damage and the "
                   "cover's direction): pick a hostile pawn";
        }
        auto const before = HealthAndShields(pawn);
        Counts const countsBefore = gCounts;
        std::uint64_t const slotsBefore = gSlotAnswers;
        ue3::Params parms(ue3::runtime::Get(), take);
        parms.Set("Damage", damage);
        parms.Set("HitLocation", ToUe(*selfAt));
        parms.Set("DamageType", reinterpret_cast<ue3::Object*>(kind));
        parms.Set("InstigatedBy", instigator);
        parms.Set("DamageCauser", pawns[index]);
        ue3::runtime::Call(module, take, parms);
        auto const after = HealthAndShields(pawn);
        return std::format("vr cover hit: {:.0f} damage from [{}] {} ({}, {}): health {:.1f} -> {:.1f}, shields {:.1f} -> {:.1f}; judged {}, "
                           "covered {}, the cone cut {}, the cover's facing given {}; {}",
            damage, index, NameOf(pawns[index]), hostile ? "hostile" : "a friend",
            kind != nullptr ? ue3::runtime::Get().NameText(reinterpret_cast<ue3::Object const*>(kind)) : std::string("SFXDamageType_Default"),
            before.first, after.first, before.second, after.second, gCounts.judged - countsBefore.judged,
            gCounts.covered - countsBefore.covered, gCounts.coneCut - countsBefore.coneCut, gSlotAnswers - slotsBefore,
            gCounts.judged > countsBefore.judged ? gLast : std::string("VR cover wasn't asked (") + gWhyNot + ")");
#endif
#endif
    }
    if (sub == "put") {
        return "mevr cover put: ME2 only (ME1: mevr setloc)";
    }
    if (sub == "regen") {
        return "mevr cover regen: ME2 only (ME1 and ME3 have no faster recharge in cover: research/23 section 7.5)";
    }
    // `test`: every awake hostile judged now, as a hit on Shepard's location from it would be.
    std::string out = std::format("vr cover ({}): the eyes at ({:.0f}, {:.0f}, {:.0f})", gWhyNot, eyes.x, eyes.y, eyes.z);
    int judged = 0;
    for (ue3::Object* p : pawns) {
        if (!Hostile(pawn, p)) {
            continue;
        }
        out += "\n    " + Judged(pawn, eyes, selfAt, FromPawn(p), settings);
        if (++judged >= 24) {
            break;
        }
    }
    if (judged == 0) {
        out += "\n    no awake hostile in the level";
    }
    return out;
#else
    (void)args;
    return "mevr cover: LE1 and LE2 only";
#endif
}

void Shutdown() {
#if defined(MEVR_VR_COVER)
    PutBack();
    gPlayerPawn = nullptr;
#endif
}

} // namespace mevr::game::vr_cover
