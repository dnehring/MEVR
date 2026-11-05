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
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace mevr::game::vr_cover {
namespace {

namespace vc = vr::cover;

#if defined(MEVR_GAME_LE1)
ue3::FunctionRef gProxyTakeDamageFn{"SFXGame.BioPawnBehavior.ProxyTakeDamage"};
ue3::FunctionRef gIsInCoverFn{"SFXGame.BioPawn.IsInCover"};
ue3::FunctionRef gIsInCoverLeaningFn{"SFXGame.BioPawn.IsInCoverLeaning"};
ue3::FunctionRef gTraceFn{"Engine.Actor.Trace"};
ue3::FunctionRef gIsHostileFn{"SFXGame.BioPawn.IsHostile"};
ue3::FunctionRef gIsDeadFn{"SFXGame.BioPawn.IsDead"};
// The damage call's arguments, read in its frame while it asks IsInCover.
ue3::FieldRef gInstigatedBy{gProxyTakeDamageFn, "InstigatedBy"};
ue3::FieldRef gHitLocation{gProxyTakeDamageFn, "HitLocation"};
ue3::FieldRef gCauser{gProxyTakeDamageFn, "pDamageCausingActor"};
ue3::FieldRef gCoverNormal{"SFXGame.BioPawn", "CoverNormal"};
ue3::FieldRef gControllerPawn{"Engine.Controller", "Pawn"};
ue3::FieldRef gLocation{"Engine.Actor", "Location"};
ue3::FieldRef gEyeHeight{"Engine.Pawn", "EyeHeight"};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList"};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn"};
ue3::FieldRef gHidden{"Engine.Actor", "bHidden"};
ue3::ClassRef gPawnClass{"Engine.Pawn"};
// `mevr cover hit`: the game's own damage path, from a pawn's place, with a weapon's damage object.
ue3::FunctionRef gTakeDamageFn{"SFXGame.BioActorBehavior.TakeDamage"};
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gPawnWeapon{"Engine.Pawn", "Weapon"};
ue3::FieldRef gWeaponDamage{"SFXGame.BioWeapon", "m_pDamage"};
ue3::ClassRef gBioWeaponClass{"SFXGame.BioWeapon"};
ue3::FieldRef gPawnAttributes{"SFXGame.BioEpicPawnBehavior", "m_PawnAttributes"};
ue3::FieldRef gHealthCurrent{"SFXGame.BioAttributesEpicPawn", "m_HealthCurrent"};
ue3::FieldRef gShield{"SFXGame.BioActorBehavior", "m_oShield"};
ue3::FieldRef gShieldAttributes{"SFXGame.BioShield", "m_pAttributes"};
ue3::FieldRef gShieldCapacity{"SFXGame.BioAttributesShield", "m_DamageCapacity"};
#endif

// ---- Settings: [Cover] in the mod's settings file, read once a second, its defaults appended when it's missing --------
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

#if defined(MEVR_GAME_LE1)
vc::Settings gSettings;
std::optional<bool> gOverride;  // `mevr cover on|off`
bool gLogHits = false;
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
    std::string const text = s.on ? std::format("on: a hit counts as taken in cover with the head hidden from the shooter by a surface "
                                                "within {:.2f} m of the eyes{}", s.reachM, logHits ? "; every judged hit logged" : "")
                                  : std::string("off ([Cover] protect = 0): a VR player takes every hit whole");
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
// The cover normal held during one damage call.
ue3::Vector* gHeldNormal = nullptr;
ue3::Vector gSavedNormal{};
std::uint8_t const* gAnsweredLocals = nullptr;  // the damage call's frame we said "in cover" to

struct Counts {
    std::uint64_t judged = 0, covered = 0, coneCut = 0, exposed = 0, tooFar = 0, overhead = 0, noShooter = 0, noView = 0;
};
Counts gCounts;
Counts gLoggedCounts;
std::string gLast = "none yet";
std::uint64_t gLastLogMs = 0;

#if defined(MEVR_GAME_LE1)
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
// location (the pawn's location, or the damage causer's when there's no instigator).
struct Shooter {
    std::optional<vc::Vec3> eyes;
    std::optional<vc::Vec3> gameLocation;
    std::string name = "nobody";
};
Shooter FromPawn(ue3::Object* pawn) {
    Shooter s;
    if (pawn == nullptr) {
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
    if (instigatedBy != nullptr) {
        return FromPawn(gControllerPawn.Read<ue3::Object*>(instigatedBy).value_or(nullptr));
    }
    Shooter s;
    if (ue3::Object* const causer = gCauser.Read<ue3::Object*>(locals).value_or(nullptr)) {
        s.name = NameOf(causer);
        if (auto const at = gLocation.Read<ue3::Vector>(causer)) {
            s.eyes = s.gameLocation = ToVec(*at);
        }
    }
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
            text += *cone ? "; the game's cone cuts it" : "; outside the game's cone or within 3 m: whole";
        }
    } else if (v.why == vc::Why::CoverTooFar) {
        text += std::format(" ({} {:.2f} m from the eyes)", surface, v.coverM);
    }
    return text;
}

void PutBackNormal() {
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
    if (caller == nullptr || caller != gProxyTakeDamageFn.Get() || locals == nullptr || result == nullptr || context == nullptr ||
        context != gPlayerPawn || !gSettings.on) {
        return false;
    }
    ++gCounts.judged;
    auto const frame = view_anchor::Current();
    if (!frame) {
        ++gCounts.noView;
        return false;
    }
    vc::Vec3 const eyes = ToVec(frame->headWorld);
    vc::Settings settings = gSettings;
    settings.unitsPerMeter = frame->unitsPerMeter;
    Shooter const shooter = FromCall(locals);
    std::string surface;
    std::optional<vc::Block> const block = shooter.eyes ? FirstSurface(context, eyes, *shooter.eyes, &surface) : std::nullopt;
    vc::Verdict const verdict = vc::Judge(eyes, shooter.eyes, block, settings);
    Count(verdict.why);
    std::optional<bool> cone;
    if (verdict.Covered() && shooter.gameLocation) {
        if (auto const hit = gHitLocation.Read<ue3::Vector>(locals)) {
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
    if (caller == nullptr || caller != gProxyTakeDamageFn.Get() || locals == nullptr || locals != gAnsweredLocals || result == nullptr ||
        context != gPlayerPawn) {
        return false;
    }
    *static_cast<std::uint32_t*>(result) = 0;
    return true;
}

// The damage call returned: the cover normal back.
void OnProxyTakeDamageDone(ue3::Object* /*context*/, ue3::Function* /*function*/, void* /*result*/) {
    PutBackNormal();
}
#endif

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    if (gStarted) {
        return true;
    }
    ue3::Function* const proxy = gProxyTakeDamageFn.Get();
    ue3::Function* const inCover = gIsInCoverFn.Get();
    ue3::Function* const leaning = gIsInCoverLeaningFn.Get();
    if (proxy == nullptr || inCover == nullptr || leaning == nullptr) {
        return false;
    }
    gStarted = true;
    ue3::Reflection& reflection = ue3::runtime::Get();
    auto noParameters = [&](ue3::Function* f) {
        return reflection.Parameters(f).size() == 1 && reflection.ParmsSize(f) == 4;  // only the bool return value
    };
    bool const fieldsOk = gInstigatedBy.Get() != nullptr && gHitLocation.Get() != nullptr && gCauser.Get() != nullptr &&
        gCoverNormal.Get() != nullptr && gTraceFn.Get() != nullptr && gPawnClass.Get() != nullptr;
    if (!noParameters(inCover) || !noParameters(leaning) || !fieldsOk) {
        log::Warn("vr cover: off: {}", fieldsOk ? "IsInCover or IsInCoverLeaning has parameters (not this game's build?)"
                                               : "a field of the damage call or the pawn wasn't found");
        gWhyNot = "unavailable (see the log)";
        return true;
    }
    engine::call_function::AddAnswerHandler(inCover, &OnIsInCover);
    engine::call_function::AddAnswerHandler(leaning, &OnIsInCoverLeaning);
    engine::call_function::AddPostHandler(proxy, &OnProxyTakeDamageDone);
    gHandlersReady = true;
    log::Info("vr cover: the damage call's IsInCover and IsInCoverLeaning are answered for the player in VR (research/23 section 6)");
    return true;
#else
    return false;
#endif
}

void Tick() {
#if defined(MEVR_GAME_LE1)
    RefreshSettings();
    PutBackNormal();  // never held between damage calls (a call that left without its post-handler)
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
}

std::string StatusJson() {
    return std::format("{{\"active\": {}, \"why\": \"{}\", \"protect\": {}, \"reachM\": {:.2f}, \"judged\": {}, \"covered\": {}, "
                       "\"coneCut\": {}, \"exposed\": {}, \"coverTooFar\": {}, \"overhead\": {}, \"noShooter\": {}, \"noView\": {}, "
                       "\"last\": \"{}\"}}",
        gPlayerPawn != nullptr, gWhyNot, gSettings.on, gSettings.reachM, gCounts.judged, gCounts.covered, gCounts.coneCut,
        gCounts.exposed, gCounts.tooFar, gCounts.overhead, gCounts.noShooter, gCounts.noView, [] {
            std::string s;
            for (char c : gLast) {
                s += (c == '"' || c == '\\') ? '\'' : c;
            }
            return s;
        }());
}

void LogStats() {
    if (gCounts.judged == gLoggedCounts.judged) {
        return;
    }
    log::Info("vr cover: {} hits judged since the start: {} in cover ({} cut by the game's cone), {} in the open, {} with cover too far, "
              "{} from overhead, {} without a shooter, {} without a view; last: {}",
        gCounts.judged, gCounts.covered, gCounts.coneCut, gCounts.exposed, gCounts.tooFar, gCounts.overhead, gCounts.noShooter,
        gCounts.noView, gLast);
    gLoggedCounts = gCounts;
}

#if defined(MEVR_GAME_LE1)
namespace {

// The awake pawns other than Shepard, nearest first (`mevr cover pawns`, the shooters `hit` can use).
std::vector<ue3::Object*> Pawns(ue3::Object* self) {
    std::vector<std::pair<double, ue3::Object*>> found;
    std::optional<ue3::Vector> const at = gLocation.Read<ue3::Vector>(self);
    ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(self).value_or(nullptr);
    int guard = 0;
    for (ue3::Object* p = world != nullptr ? gPawnList.Read<ue3::Object*>(world).value_or(nullptr) : nullptr; p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
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

std::string Judged(ue3::Object* self, vc::Vec3 const& eyes, std::optional<vc::Vec3> const& selfAt, Shooter const& shooter,
    vc::Settings const& settings) {
    std::string surface;
    std::optional<vc::Block> const block = shooter.eyes ? FirstSurface(self, eyes, *shooter.eyes, &surface) : std::nullopt;
    vc::Verdict const verdict = vc::Judge(eyes, shooter.eyes, block, settings);
    std::optional<bool> cone;
    if (verdict.Covered() && shooter.gameLocation && selfAt) {
        cone = vc::GameCone(verdict.coverNormal, *selfAt, *shooter.gameLocation);
    }
    return Describe(verdict, shooter, eyes, surface, cone);
}

} // namespace
#endif

std::string Console(std::string_view args) {
#if defined(MEVR_GAME_LE1)
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
    if (sub != "test" && sub != "pawns" && sub != "hit" && sub != "probe") {
        return "usage: mevr cover [on|off | test | pawns | hit <damage> <n> | probe <yawDeg> [rangeM]]";
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
    if (sub == "pawns") {
        // Each awake pawn as a shooter would be judged from its location (`hit` has no instigator, so the game and VR cover
        // both take the damage causer's place).
        std::string out = std::format("vr cover ({}): the eyes at ({:.0f}, {:.0f}, {:.0f}); {} pawns", gWhyNot, eyes.x, eyes.y, eyes.z,
            pawns.size());
        for (std::size_t i = 0; i < pawns.size() && i < 24; ++i) {
            Shooter shooter = FromPawn(pawns[i]);
            shooter.eyes = shooter.gameLocation;
            out += std::format("\n    [{}] {}{}", i, Hostile(pawn, pawns[i]) ? "hostile " : "", Judged(pawn, eyes, selfAt, shooter, settings));
        }
        return out;
    }
    if (sub == "hit") {
        // The game's own damage path (BioActorBehavior.TakeDamage on Shepard's behaviour) from pawn n's place, no instigator
        // (no friendly-fire refusal; the game then takes the causer's location, as VR cover does), with a weapon's damage
        // object. Health and shields before and after, and what VR cover judged.
        float damage = 0.0f;
        std::size_t index = 0;
        words >> damage >> index;
        if (damage <= 0.0f || index >= pawns.size()) {
            return std::format("usage: mevr cover hit <damage> <n> (n of the {} pawns `mevr cover pawns` lists)", pawns.size());
        }
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
    return "mevr cover: LE1 only";
#endif
}

void Shutdown() {
#if defined(MEVR_GAME_LE1)
    PutBackNormal();
    gPlayerPawn = nullptr;
#endif
}

} // namespace mevr::game::vr_cover
