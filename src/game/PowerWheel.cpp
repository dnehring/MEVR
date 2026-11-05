// SPDX-License-Identifier: MIT
// The power wheel at the left hand (game/PowerWheel.hpp, research/55).

#include "game/PowerWheel.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/SettingsFile.hpp"
#include "game/HeadTracking.hpp"
#include "game/OmniTool.hpp"
#include "game/Player.hpp"
#include "game/SquadWeapons.hpp"
#include "game/SupportGrip.hpp"
#include "game/ViewAnchor.hpp"
#include "engine/XrToUnreal.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace mevr::game::power_wheel {
namespace {

namespace pw = vr::power_wheel;
namespace json = vr::test_request;

ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gPowers{"SFXGame.BioEpicPawnBehavior", "m_powers"};
ue3::FieldRef gPowerName{"SFXGame.BioPower", "m_nmPower"};
ue3::FieldRef gCooldownRemaining{"SFXGame.BioPower", "m_fCooldownRemaining"};
ue3::FieldRef gTotalCooldown{"SFXGame.BioPower", "m_fTotalCooldownTime"};
ue3::FieldRef gEnabledCount{"SFXGame.BioPower", "m_nEnabled"};
ue3::FieldRef gSuppressed{"SFXGame.BioPower", "m_nSuppressedCount"};
ue3::FieldRef gPlayerInput{"Engine.PlayerController", "PlayerInput"};
ue3::FieldRef gMappedPower{"SFXGame.BioPlayerInput", "MappedPower"};
ue3::FunctionRef gIsEnabledFn{"SFXGame.BioPower.IsEnabled"};
ue3::FunctionRef gIsCombatEnabledFn{"SFXGame.BioPlayerInput.IsCombatEnabled"};
ue3::FunctionRef gIsRequiredWeaponFn{"SFXGame.BioPower.IsRequiredWeaponEquipped"};
// `mevr powerwheel cast` (tests): what the right stick click's tap does, SFXGameModeDefault.TryUseMappedPower.
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes"};
ue3::ClassRef gGameModeDefault{"SFXGame.SFXGameModeDefault"};
ue3::FunctionRef gTryUseMappedPowerFn{"SFXGame.SFXGameModeDefault.TryUseMappedPower"};
bool gShowUnlearned = false;  // `mevr powerwheel unlearned on` (tests): untrained powers on the wheel too, dim

// ---- Settings: [PowerWheel] in the mod's settings file, read once a second, its defaults appended when it's missing --
constexpr char kBlock[] =
    "\n"
    "[PowerWheel]\n"
    "; The power wheel at your left hand (research/55): hold X on foot and it opens around your left palm with your powers;\n"
    "; move your left hand toward one (or push the left stick toward it: you don't walk while it's open) and let go of X to\n"
    "; equip it. Tap X instead and it stays open: a pinch of the left trigger, a squeeze of the left grip or X again equips.\n"
    "; The equipped power is the one the right stick click's tap casts. While on = 1, X never throws a grenade on foot (the\n"
    "; physical throw stays). widthM the wheel's width; towardM its centre from your palm toward your eyes; deadZoneM how far\n"
    "; the hand moves before it picks; keepDeg how much nearer another tile must be to take over; stickPick how far the left\n"
    "; stick must be pushed to pick; tapS how short a tap is; emblem 1: our N7 mark on the palm, 0: nothing; upperCase 1: the\n"
    "; names in capitals; hintOpenings: the explanation card shows the first this many openings of each session; haptic\n"
    "; 0..1: a tick when the pick changes, a pulse when a power is equipped (0: none).\n"
    "on = 1\n"
    "widthM = 0.42\n"
    "towardM = 0.04\n"
    "deadZoneM = 0.025\n"
    "keepDeg = 6\n"
    "stickPick = 0.5\n"
    "tapS = 0.30\n"
    "emblem = 1\n"
    "upperCase = 0\n"
    "hintOpenings = 3\n"
    "haptic = 0.35\n"
    "; omniTool 1: your left forearm wears Shepard's omni-tool gauntlet (the game's own hologram) while the wheel is open.\n"
    "omniTool = 1\n"
    "; The lock and the push: with the wheel closed, raise your left arm (lockReachM ahead of your eyes) and point your hand: the\n"
    "; enemy nearest its ray (within lockAcquireDeg, kept until lockKeepDeg, in sight, within lockRangeM; lockGraceS of blocked\n"
    "; sight allowed) is locked and marked; push your left hand along its ray (pushSpeedMS fast, pushTravelM far) to cast the\n"
    "; equipped power at it, no sooner than pushCooldownS apart. lock 0: off (the right stick click's tap still casts the game's way).\n"
    "lock = 1\n"
    "lockAcquireDeg = 9\n"
    "lockKeepDeg = 16\n"
    "lockRangeM = 35\n"
    "lockGraceS = 0.35\n"
    "lockReachM = 0.20\n"
    "pushSpeedMS = 1.6\n"
    "pushTravelM = 0.10\n"
    "pushCooldownS = 0.6\n"
    "; autoLock 1: nothing pointed at, the enemy within autoLockHandDeg of your hand's line or autoLockGazeDeg of where you look\n"
    "; is locked by itself (the one both agree on best); pointing at another takes over.\n"
    "autoLock = 1\n"
    "autoLockHandDeg = 30\n"
    "autoLockGazeDeg = 20\n";

pw::Settings gSettings;
pw::LockSettings gLockSettings;
std::uint64_t gSettingsAtMs = 0;
std::string gSettingsText;
bool gTriedAppend = false;

std::string Describe(pw::Settings const& s) {
    if (!s.on) {
        return "off ([PowerWheel] on = 0): X throws a grenade the game's way if [Throwables] grenadeButton = 1";
    }
    return std::format("X opens it at the left palm, {:.2f} m wide, {:.2f} m toward the eyes; picks past {:.3f} m of hand travel or "
                       "the stick past {:.2f}, {:.0f} deg to change; a tap under {:.2f} s keeps it open; emblem {}, {} names, hint {} "
                       "openings, haptic {:.2f}; the omni-tool gauntlet {}",
        s.widthM, s.towardM, s.deadZoneM, s.stickPick, s.keepDeg, s.tapS, s.emblem ? "N7" : "none", s.upperCase ? "capital" : "title-case",
        s.hintOpenings, s.haptic, s.omniTool ? "on the forearm while it's open" : "off");
}

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsAtMs != 0 && now - gSettingsAtMs < 1000) {
        return;
    }
    gSettingsAtMs = now;
    std::filesystem::path const path = engine::SettingsFile();
    ini::Loaded const loaded = ini::Read(path);
    if (loaded.exists && loaded.file.find("PowerWheel") == loaded.file.end() && !gTriedAppend) {
        gTriedAppend = true;
        if (ini::IsUtf16(loaded.encoding)) {
            log::Warn("power wheel: {} is saved as UTF-16: the [PowerWheel] defaults aren't appended to it; they apply", ini::Utf8(path));
        } else {
            std::ofstream out(path, std::ios::app);
            out << kBlock;
            out.close();
            if (out) {
                log::Info("power wheel: added the [PowerWheel] defaults to {}", ini::Utf8(path));
            } else {
                log::Warn("power wheel: couldn't add the [PowerWheel] defaults to {} (read-only?); they apply", ini::Utf8(path));
            }
        }
    }
    pw::Settings s;
    auto number = [&](char const* key, float& out, float lo, float hi) {
        if (auto const v = ini::GetNumber(loaded.file, "PowerWheel", key)) {
            out = static_cast<float>(std::clamp(*v, static_cast<double>(lo), static_cast<double>(hi)));
        }
    };
    auto flag = [&](char const* key, bool& out) {
        if (auto const v = ini::GetNumber(loaded.file, "PowerWheel", key)) {
            out = *v != 0.0;
        }
    };
    flag("on", s.on);
    number("widthM", s.widthM, 0.2f, 0.8f);
    number("towardM", s.towardM, -0.1f, 0.2f);
    number("deadZoneM", s.deadZoneM, 0.005f, 0.15f);
    number("keepDeg", s.keepDeg, 0.0f, 30.0f);
    number("stickPick", s.stickPick, 0.2f, 0.95f);
    number("tapS", s.tapS, 0.0f, 1.0f);
    flag("emblem", s.emblem);
    flag("upperCase", s.upperCase);
    float hint = static_cast<float>(s.hintOpenings);
    number("hintOpenings", hint, 0.0f, 100.0f);
    s.hintOpenings = static_cast<int>(hint);
    number("haptic", s.haptic, 0.0f, 1.0f);
    flag("omniTool", s.omniTool);
    pw::LockSettings l;
    flag("lock", l.on);
    number("lockAcquireDeg", l.acquireDeg, 1.0f, 30.0f);
    number("lockKeepDeg", l.keepDeg, 1.0f, 45.0f);
    l.keepDeg = std::max(l.keepDeg, l.acquireDeg);
    number("lockRangeM", l.rangeM, 2.0f, 100.0f);
    number("lockGraceS", l.graceS, 0.0f, 3.0f);
    number("lockReachM", l.reachM, 0.0f, 0.7f);
    number("pushSpeedMS", l.pushSpeedMS, 0.5f, 6.0f);
    number("pushTravelM", l.pushTravelM, 0.02f, 0.5f);
    number("pushCooldownS", l.cooldownS, 0.1f, 5.0f);
    flag("autoLock", l.autoLock);
    number("autoLockHandDeg", l.autoHandDeg, 0.0f, 60.0f);
    number("autoLockGazeDeg", l.autoGazeDeg, 0.0f, 60.0f);
    gLockSettings = l;
    gSettings = s;
    std::string const lockText = l.on ? std::format("; the lock within {:.0f} deg (kept to {:.0f}), {:.0f} m, the arm {:.2f} m out; a push "
                                                               "of {:.1f} m/s over {:.2f} m casts; automatic lock {}", l.acquireDeg, l.keepDeg, l.rangeM, l.reachM, l.pushSpeedMS, l.pushTravelM,
                                                    l.autoLock ? std::format("within {:.0f} deg of the hand or {:.0f} of the gaze", l.autoHandDeg, l.autoGazeDeg) : std::string("off"))
                                           : std::string("; no lock or push (lock = 0)");
    if (std::string const text = Describe(s) + lockText; text != gSettingsText) {
        gSettingsText = text;
        log::Info("power wheel: settings: {}", text);
    }
}

// ---- The player's powers -----------------------------------------------------------------------------------------------
struct Power {
    ue3::Name name{};
    std::string label;
    pw::PowerInfo const* info = nullptr;
    float cooldown = 0.0f;  // 0..1 of its cooldown still to run
    bool enabled = true;
    std::int32_t enabledCount = 0;  // m_nEnabled
    std::int32_t suppressed = 0;    // m_nSuppressedCount
};

bool CallBool(ue3::Object* object, ue3::FunctionRef& ref) {
    ue3::Function* const function = ref.Get();
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

// The pawn's mappable powers it has trained (m_nEnabled > 0: 0 for every talent without a rank, measured on a save
// 2026-11-04, research/55 section 6), biotics first, then tech, then combat (each in the game's order). Dim: the game won't
// cast it now (IsEnabled false: suppressed; or its weapon isn't the one held).
std::vector<Power> ReadRoster(ue3::Object* pawn, bool includeUnlearned) {
    std::vector<Power> out;
    ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
    if (behavior == nullptr) {
        return out;
    }
    auto const list = gPowers.ElementsOf(behavior);
    if (!list) {
        return out;
    }
    for (std::int32_t i = 0; i < list->count && i < 64; ++i) {
        void* const slot = list->At(i);
        ue3::Object* const power = slot != nullptr ? *static_cast<ue3::Object**>(slot) : nullptr;
        if (power == nullptr || !ue3::runtime::IsAlive(power)) {
            continue;
        }
        Power p;
        p.name = gPowerName.Read<ue3::Name>(power).value_or(ue3::Name{});
        p.label = ue3::runtime::NameText(p.name);
        p.info = pw::FindPower(p.label.c_str());
        if (p.info == nullptr) {
            continue;  // melee, healing, the armour emitter...: not on the wheel
        }
        float const remaining = std::max(0.0f, gCooldownRemaining.Read<float>(power).value_or(0.0f));
        float const total = gTotalCooldown.Read<float>(power).value_or(0.0f);
        p.cooldown = total > 0.01f ? std::clamp(remaining / total, 0.0f, 1.0f) : (remaining > 0.0f ? 1.0f : 0.0f);
        p.enabledCount = gEnabledCount.Read<std::int32_t>(power).value_or(-1);
        p.suppressed = gSuppressed.Read<std::int32_t>(power).value_or(-1);
        if (p.enabledCount == 0 && !includeUnlearned) {
            continue;  // not trained
        }
        p.enabled = p.enabledCount != 0 && (gIsEnabledFn.Get() == nullptr || CallBool(power, gIsEnabledFn)) &&
                    (gIsRequiredWeaponFn.Get() == nullptr || CallBool(power, gIsRequiredWeaponFn));
        out.push_back(std::move(p));
        if (out.size() >= static_cast<std::size_t>(pw::kMaxEntries) * 2) {
            break;
        }
    }
    std::stable_sort(out.begin(), out.end(), [](Power const& a, Power const& b) { return a.info->discipline < b.info->discipline; });
    if (out.size() > static_cast<std::size_t>(pw::kMaxEntries)) {
        out.resize(static_cast<std::size_t>(pw::kMaxEntries));
    }
    return out;
}

ue3::Object* PlayerInput(ue3::Object* controller) {
    return controller != nullptr ? gPlayerInput.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
}

std::string MappedPower(ue3::Object* controller) {
    ue3::Object* const input = PlayerInput(controller);
    auto const name = input != nullptr ? gMappedPower.Read<ue3::Name>(input) : std::nullopt;
    return name ? ue3::runtime::NameText(*name) : std::string();
}

// ---- State --------------------------------------------------------------------------------------------------------------
pw::Machine gMachine;
double gClock = 0.0;
std::vector<Power> gRoster;
pw::View gView;
bool gPadOn = false;          // on foot with the wheel on: X is the wheel's
bool gHeldAfter = false;      // a left trigger, grip or X used in it is still held after it closed
std::string gWhy = "not started";
std::string gEquipped;        // the label last equipped
std::string gLastEvent;
int gHintShown = 0;
std::uint64_t gOpened = 0, gEquips = 0, gCancels = 0, gPicks = 0;
// The console's stand-ins (tests without tracked controllers).
bool gConsole = false;        // allowed without controllers or a world view
bool gConsoleHold = false;    // X held
bool gConsoleCancel = false;  // close unequipped on the next tick
std::optional<pw::Vec2> gConsoleHand;
std::optional<int> gConsolePick;

XrVector3f HeadForwardFlat(XrQuaternionf const& q) {
    XrVector3f f{-2.0f * (q.x * q.z + q.w * q.y), 0.0f, -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))};
    float const l = std::sqrt(f.x * f.x + f.z * f.z);
    return l > 1e-4f ? XrVector3f{f.x / l, 0.0f, f.z / l} : XrVector3f{0.0f, 0.0f, -1.0f};
}

void Equip(ue3::Object* controller, Power const& power) {
    ue3::Object* const input = PlayerInput(controller);
    ue3::Name* const slot = input != nullptr ? gMappedPower.In<ue3::Name>(input) : nullptr;
    if (slot == nullptr) {
        log::Warn("power wheel: {} can't be equipped: no BioPlayerInput.MappedPower to write", power.info->name);
        return;
    }
    std::string const before = ue3::runtime::NameText(*slot);
    *slot = power.name;
    write_journal::Wrote("PowerWheel.mappedPower", slot);
    gEquipped = power.label;
    log::Info("power wheel: equipped {} ({}): the game's mapped power was {}; the right stick click's tap casts it", power.info->name,
        power.label, before.empty() ? "none" : before);
}

// ---- The lock and the push (research/55 section 4) ----------------------------------------------------------------------
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList"};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn"};
ue3::FieldRef gHidden{"Engine.Actor", "bHidden"};
ue3::FieldRef gBehaviorActive{"SFXGame.BioActorBehavior", "bActive"};  // the encounter's switch (dormant: false)
ue3::FunctionRef gIsHostileFn{"SFXGame.BioPawn.IsHostile"};
ue3::FunctionRef gIsDeadFn{"SFXGame.BioPawn.IsDead"};
ue3::FunctionRef gFastTraceFn{"Engine.Actor.FastTrace"};
ue3::FunctionRef gTraceFn{"Engine.Actor.Trace"};
ue3::FunctionRef gActivatePowerFn{"SFXGame.BioPlayerInput.ActivatePower"};

constexpr double kChestUnits = 20.0;  // the marker a little above the pawn's centre (its location)

struct Seen {
    ue3::Object* pawn = nullptr;
    engine::UnrealVector at{};
    float angleDeg = 180.0f;  // from the hand's ray
    float gazeDeg = 180.0f;   // from the eyes' forward
    float distanceM = 0.0f;
    bool visible = false;
};
pw::LockState gLock;
pw::Push gPush;
std::vector<Seen> gSeen;
ue3::Object* gTarget = nullptr;
engine::UnrealVector gTargetAt{};
std::optional<engine::UnrealVector> gPlace;  // where a place power would land (no target)
std::uint8_t gCasts = 0;
std::uint64_t gCastCount = 0, gRefused = 0, gLocks = 0, gPushes = 0;
std::string gLastCast = "none";
std::string gLockWhy = "not started";
bool gConsolePush = false;  // `mevr powerwheel push`: a push now
bool gAimHead = false;      // `mevr powerwheel aimhead on` (tests): the head's ray stands in for the left hand's
double gGripAt = -1e9;      // the left grip last squeezed (a grenade's throw, a squad order: no push for 0.4 s after)
// The last scan's counts (`mevr powerwheel lock`): pawns walked, alive and awake, within range and a cone, hostile.
int gScanPawns = 0, gScanAwake = 0, gScanNear = 0, gScanHostile = 0;
std::string gScanNote;

double Dot3(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
engine::UnrealVector Minus(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
engine::UnrealVector Plus(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
engine::UnrealVector Scaled(engine::UnrealVector const& a, double k) {
    return {a.x * k, a.y * k, a.z * k};
}
ue3::Vector ToVector(engine::UnrealVector const& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}
XrVector3f RotateXr(XrQuaternionf q, XrVector3f v) {
    XrVector3f const u{q.x, q.y, q.z};
    auto cross = [](XrVector3f a, XrVector3f b) { return XrVector3f{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    XrVector3f const t = cross(u, v);
    XrVector3f const t2{2.0f * t.x, 2.0f * t.y, 2.0f * t.z};
    XrVector3f const c = cross(u, t2);
    return {v.x + q.w * t2.x + c.x, v.y + q.w * t2.y + c.y, v.z + q.w * t2.z + c.z};
}

// The power the game has mapped (the wheel's equip, or the game's own wheel), if it's one of the player's.
std::optional<Power> ReadyPower(ue3::Object* controller, ue3::Object* pawn) {
    std::string const mapped = MappedPower(controller);
    if (mapped.empty() || mapped == "None") {
        return std::nullopt;
    }
    ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
    auto const list = behavior != nullptr ? gPowers.ElementsOf(behavior) : std::nullopt;
    if (!list) {
        return std::nullopt;
    }
    for (std::int32_t i = 0; i < list->count && i < 64; ++i) {
        void* const slot = list->At(i);
        ue3::Object* const power = slot != nullptr ? *static_cast<ue3::Object**>(slot) : nullptr;
        if (power == nullptr || !ue3::runtime::IsAlive(power)) {
            continue;
        }
        Power p;
        p.name = gPowerName.Read<ue3::Name>(power).value_or(ue3::Name{});
        p.label = ue3::runtime::NameText(p.name);
        if (p.label != mapped) {
            continue;
        }
        p.info = pw::FindPower(p.label.c_str());
        if (p.info == nullptr) {
            return std::nullopt;
        }
        float const remaining = std::max(0.0f, gCooldownRemaining.Read<float>(power).value_or(0.0f));
        float const total = gTotalCooldown.Read<float>(power).value_or(0.0f);
        p.cooldown = total > 0.01f ? std::clamp(remaining / total, 0.0f, 1.0f) : (remaining > 0.0f ? 1.0f : 0.0f);
        p.enabledCount = gEnabledCount.Read<std::int32_t>(power).value_or(-1);
        p.suppressed = gSuppressed.Read<std::int32_t>(power).value_or(-1);
        p.enabled = p.enabledCount != 0 && (gIsEnabledFn.Get() == nullptr || CallBool(power, gIsEnabledFn)) &&
                    (gIsRequiredWeaponFn.Get() == nullptr || CallBool(power, gIsRequiredWeaponFn));
        return p;
    }
    return std::nullopt;
}

// The angle between `direction` (a unit vector) and the line from `from` to `point`, degrees.
float AngleTo(engine::UnrealVector const& from, engine::UnrealVector const& direction, engine::UnrealVector const& point) {
    engine::UnrealVector const to = Minus(point, from);
    double const length = std::sqrt(Dot3(to, to));
    if (length < 1.0) {
        return 0.0f;
    }
    return static_cast<float>(std::acos(std::clamp(Dot3(to, direction) / length, -1.0, 1.0)) * 180.0 / 3.14159265358979);
}

// The living hostiles near the hand's ray (within keepDeg) or, with the automatic resolver, within its cones of the hand's
// ray or the eyes' forward, and in range; each with its line of sight from the hand.
void Scan(ue3::Object* pawn, engine::UnrealVector const& origin, engine::UnrealVector const& direction, engine::UnrealVector const& eye,
    engine::UnrealVector const& gaze, double upm, pw::LockSettings const& s) {
    float const handLimit = std::max(s.keepDeg, s.autoLock ? s.autoHandDeg + pw::kAutoKeepMarginDeg : 0.0f);
    float const gazeLimit = s.autoLock ? s.autoGazeDeg + pw::kAutoKeepMarginDeg : -1.0f;
    gSeen.clear();
    gScanPawns = gScanAwake = gScanNear = gScanHostile = 0;
    gScanNote = std::format("\n    the ray from ({:.0f}, {:.0f}, {:.0f}) along ({:.2f}, {:.2f}, {:.2f}); the eyes at ({:.0f}, {:.0f}, {:.0f}) looking along "
                            "({:.2f}, {:.2f}, {:.2f})",
        origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, eye.x, eye.y, eye.z, gaze.x, gaze.y, gaze.z);
    ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(pawn).value_or(nullptr);
    if (world == nullptr || gIsHostileFn.Get() == nullptr) {
        return;
    }
    int guard = 0;
    for (ue3::Object* p = gPawnList.Read<ue3::Object*>(world).value_or(nullptr); p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        ++gScanPawns;
        ue3::Object* const behavior = player::IsBioPawn(p) ? gBehavior.Read<ue3::Object*>(p).value_or(nullptr) : nullptr;
        if (p == pawn || !player::IsBioPawn(p) || gHidden.ReadBool(p).value_or(false) ||
            (behavior != nullptr && !gBehaviorActive.ReadBool(behavior).value_or(true)) || CallBool(p, gIsDeadFn)) {
            // Dead, or dormant: hidden, or its behaviour switched off (bActive false: a base's crew before its encounter's
            // script runs BioSeqAct_ModifyPropertyPawn's SetActive, measured 2026-11-05). Not the collision: a thrown,
            // ragdolled hostile has none for a moment and would lose the lock.
            continue;
        }
        ++gScanAwake;
        std::optional<ue3::Vector> const at = player::Location(p);
        if (!at) {
            continue;
        }
        engine::UnrealVector const point{at->x, at->y, at->z + kChestUnits};
        engine::UnrealVector const to = Minus(point, origin);
        double const length = std::sqrt(Dot3(to, to));
        if (length < 1.0) {
            continue;
        }
        float const distanceM = static_cast<float>(length / upm);
        float const angle = AngleTo(origin, direction, point);
        float const gazeAngle = AngleTo(eye, gaze, point);
        if (distanceM <= s.rangeM && gScanNote.size() < 900) {  // `mevr powerwheel lock`: the awake pawns in range
            gScanNote += std::format("\n    {} at ({:.0f}, {:.0f}, {:.0f}): {:.1f} m, {:.1f} deg from the ray, {:.1f} from the gaze",
                ue3::runtime::Get().NameText(p), at->x, at->y, at->z, distanceM, angle, gazeAngle);
        }
        if (distanceM > s.rangeM || (angle > handLimit && gazeAngle > gazeLimit)) {
            continue;  // the cheap tests first
        }
        ++gScanNear;
        ue3::Params hostile(ue3::runtime::Get(), gIsHostileFn.Get());
        hostile.Set("Other", p);
        ue3::runtime::Call(pawn, gIsHostileFn.Get(), hostile);
        if (!hostile.ReturnBool().value_or(false)) {
            continue;
        }
        ++gScanHostile;
        Seen seen{p, {at->x, at->y, at->z}, angle, gazeAngle, distanceM, true};
        if (ue3::Function* const trace = gFastTraceFn.Get()) {
            ue3::Params parms(ue3::runtime::Get(), trace);
            parms.Set("TraceEnd", ToVector(point));
            parms.Set("TraceStart", ToVector(origin));
            ue3::runtime::Call(pawn, trace, parms);
            seen.visible = parms.ReturnBool().value_or(false);
        }
        gSeen.push_back(seen);
    }
}

// Where the ray meets the world within range (a place power's landing point), or nullopt.
std::optional<engine::UnrealVector> Land(ue3::Object* pawn, engine::UnrealVector const& origin, engine::UnrealVector const& direction,
    double range) {
    ue3::Function* const fn = gTraceFn.Get();
    if (pawn == nullptr || fn == nullptr) {
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    parms.Set("TraceEnd", ToVector(Plus(origin, Scaled(direction, range))));
    parms.Set("TraceStart", ToVector(origin));
    parms.SetBool("bTraceActors", true);
    ue3::runtime::Call(pawn, fn, parms);
    if (parms.Return<ue3::Object*>().value_or(nullptr) == nullptr) {
        return std::nullopt;
    }
    ue3::Vector const at = parms.Get<ue3::Vector>("HitLocation").value_or(ue3::Vector{});
    engine::UnrealVector const point{at.x, at.y, at.z};
    double const along = Dot3(Minus(point, origin), direction);
    if ((at.x == 0.0f && at.y == 0.0f && at.z == 0.0f) || along < 0.0 || along > range + 1.0) {
        return std::nullopt;
    }
    return point;
}

std::string NameOf(ue3::Object* object) {
    return object != nullptr ? ue3::runtime::Get().NameText(object) : std::string("nothing");
}

// The game's own "cast this at that" (research/55 section 2): SetCurrentPower, AcquireActive(BioActivePower), then the
// running power's SetTargetActor and SetTargetLocation.
bool Cast(ue3::Object* controller, Power const& power, ue3::Object* target, std::optional<engine::UnrealVector> const& at) {
    ue3::Object* const input = PlayerInput(controller);
    ue3::Function* const fn = gActivatePowerFn.Get();
    if (input == nullptr || fn == nullptr) {
        gLastCast = std::string(power.info->name) + ": no BioPlayerInput.ActivatePower";
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    parms.Set("nmPower", power.name);
    if (target != nullptr) {
        parms.Set("oTarget", target);
    }
    if (at) {
        parms.Set("vTargetLocation", ToVector(*at));
    }
    ue3::runtime::Call(input, fn, parms);
    bool const ok = parms.ReturnBool().value_or(false);
    gLastCast = std::format("{} at {}{}: {}", power.info->name, target != nullptr ? NameOf(target) : std::string("no actor"),
        at ? std::format(" ({:.0f}, {:.0f}, {:.0f})", at->x, at->y, at->z) : std::string(), ok ? "started" : "the game refused it");
    return ok;
}

void Refuse(Link const& link, std::string const& why) {
    ++gRefused;
    gLastCast = why;
    log::Info("power wheel: push not cast: {}", why);
    if (link.haptic != nullptr && gSettings.haptic > 0.0f) {
        link.haptic(0, 0.12f, gSettings.haptic * 0.5f);  // a long soft buzz: no
    }
}

void TickLock(Link const& link, vr::host_view::State const& view, vr::controllers::State const& controllers, ue3::Object* controller,
    ue3::Object* pawn, bool onFoot) {
    pw::LockSettings const& s = gLockSettings;
    pw::LockView marker;
    auto publish = [&]() {
        if (link.publishLock != nullptr) {
            link.publishLock(&marker);
        }
    };
    gSeen.clear();
    gPlace.reset();
    bool const pushAsked = gConsolePush;
    gConsolePush = false;
    std::optional<Power> const ready = s.on && onFoot && controller != nullptr && pawn != nullptr ? ReadyPower(controller, pawn) : std::nullopt;
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    bool const hand = controllers.active && controllers.left.aimValid;
    char const* why = nullptr;
    if (!s.on) {
        why = "off (lock = 0)";
    } else if (!onFoot) {
        why = "not on foot";
    } else if (!ready) {
        why = "no equipped power of the player's (the game's mapped power)";
    } else if (!frame || !view.valid) {
        why = "no head-tracked world view";
    } else if (!hand && !gAimHead) {
        why = "the left controller isn't tracked";
    } else if (gMachine.open || gHeldAfter) {
        why = "the wheel has the left hand";
    } else if (support_grip::Held()) {
        why = "the left hand holds the gun";
    } else if (squad_weapons::IsOpen()) {
        why = "the squad's weapon wheel is open";
    }
    if (why != nullptr) {
        gLockWhy = why;
        gLock = {};
        gTarget = nullptr;
        publish();
        return;
    }
    gLockWhy = "ready";
    XrPosef const aim = hand ? controllers.left.aim : view.head;
    // The arm raised: the hand ahead of the eyes (as seen flat).
    XrVector3f const forwardFlat = HeadForwardFlat(view.head.orientation);
    float const reach = (aim.position.x - view.head.position.x) * forwardFlat.x + (aim.position.z - view.head.position.z) * forwardFlat.z;
    bool const raised = !hand || reach >= s.reachM;
    double const upm = frame->unitsPerMeter > 0.0 ? frame->unitsPerMeter : engine::kUnitsPerMeter;
    engine::UnrealVector const origin = engine::ToWorld(*frame, aim.position);
    engine::UnrealVector const direction = engine::OrientationToWorld(*frame, aim.orientation).forward;
    engine::UnrealVector const eye = engine::ToWorld(*frame, view.head.position);
    engine::UnrealVector const gaze = engine::OrientationToWorld(*frame, view.head.orientation).forward;
    if (ready->info->aim != pw::Aim::None && raised) {
        Scan(pawn, origin, direction, eye, gaze, upm, s);
    }
    std::vector<pw::LockCandidate> candidates;
    for (Seen const& seen : gSeen) {
        candidates.push_back({reinterpret_cast<std::uint64_t>(seen.pawn), seen.angleDeg, seen.gazeDeg, seen.distanceM, seen.visible});
    }
    std::uint64_t const id = pw::UpdateLock(gLock, candidates, gClock, s);
    ue3::Object* target = nullptr;
    for (Seen const& seen : gSeen) {
        if (reinterpret_cast<std::uint64_t>(seen.pawn) == id) {
            target = seen.pawn;
            gTargetAt = seen.at;
            if (target != gTarget) {
                ++gLocks;
                log::Info("power wheel: {} locked on {} ({:.1f} m, {:.1f} deg from the left hand's ray, {:.1f} from the gaze; {})",
                    ready->info->name, NameOf(target), seen.distanceM, seen.angleDeg, seen.gazeDeg,
                    gLock.automatic ? "resolved automatically: nothing pointed at" : "pointed at");
                if (link.haptic != nullptr && gSettings.haptic > 0.0f) {
                    link.haptic(0, 0.02f, gSettings.haptic);
                }
            }
        }
    }
    gTarget = target;
    if (target == nullptr && ready->info->aim == pw::Aim::Place && raised) {
        gPlace = Land(pawn, origin, direction, s.rangeM * upm);
    }

    // The push: an open left hand (no grip: a grenade's throw or a squad order's point isn't a cast), the arm raised.
    bool pushed = pushAsked;
    if (controllers.active && controllers.left.squeeze > 0.5f) {
        gGripAt = gClock;
    }
    if (hand && gClock - gGripAt >= 0.4) {
        // Every sample counts (a push may start low), but only one that ends with the arm raised casts: a fast reach
        // elsewhere (walking, grabbing) is spent silently.
        XrVector3f const along = RotateXr(controllers.left.aim.orientation, {0.0f, 0.0f, -1.0f});
        pushed = (pw::StepPush(gPush, gClock, controllers.left.aim.position, along, s) && raised) || pushed;
    }
    if (pushed) {
        ++gPushes;
        if (!ready->enabled) {
            Refuse(link, std::format("{} can't be cast now (the game says it's unavailable: suppressed, or not with the weapon in hand)",
                ready->info->name));
        } else if (ready->cooldown > 0.0f) {
            Refuse(link, std::format("{} is still cooling down ({:.0f}% to go)", ready->info->name, ready->cooldown * 100.0f));
        } else {
            bool ok = false;
            bool tried = true;
            switch (ready->info->aim) {
            case pw::Aim::Target:
                if (target != nullptr) {
                    ok = Cast(controller, *ready, target, Plus(gTargetAt, {0.0, 0.0, kChestUnits}));
                } else {
                    tried = false;
                    Refuse(link, std::format("{} needs a locked enemy", ready->info->name));
                }
                break;
            case pw::Aim::Place:
                if (target != nullptr) {
                    ok = Cast(controller, *ready, target, Plus(gTargetAt, {0.0, 0.0, kChestUnits}));
                } else if (gPlace) {
                    ok = Cast(controller, *ready, nullptr, gPlace);
                } else {
                    tried = false;
                    Refuse(link, std::format("{}: no enemy locked and the hand's ray meets nothing in range", ready->info->name));
                }
                break;
            case pw::Aim::None:
                ok = Cast(controller, *ready, nullptr, std::nullopt);
                break;
            }
            if (tried) {
                log::Info("power wheel: push: {}", gLastCast);
                if (ok) {
                    ++gCastCount;
                    ++gCasts;
                    if (link.haptic != nullptr && gSettings.haptic > 0.0f) {
                        link.haptic(0, 0.08f, std::min(1.0f, gSettings.haptic * 2.0f));
                    }
                } else if (link.haptic != nullptr && gSettings.haptic > 0.0f) {
                    link.haptic(0, 0.12f, gSettings.haptic * 0.5f);
                }
            }
        }
    }

    // The marker: on the locked enemy, or where a place power would land.
    if (target != nullptr || gPlace) {
        marker.active = 1;
        marker.ready = ready->enabled && ready->cooldown <= 0.0f ? 1 : 0;
        marker.discipline = ready->info->discipline;
        marker.casts = gCasts;
        marker.onActor = target != nullptr ? 1 : 0;
        marker.icon = ready->info->icon;
        engine::UnrealVector const at = target != nullptr ? Plus(gTargetAt, {0.0, 0.0, kChestUnits}) : *gPlace;
        marker.target = engine::ToTracking(*frame, at);
        marker.sizeDeg = target != nullptr ? 6.0f : 4.0f;
        marker.lockedFor = target != nullptr ? static_cast<float>(gClock - gLock.since) : 0.0f;
        marker.cooldown = ready->cooldown;
    }
    publish();
}

}  // namespace

void Tick(double deltaSeconds, Link const& link) {
    gClock += std::max(deltaSeconds, 0.0);
    RefreshSettings();
    pw::Settings const& s = gSettings;

    ue3::Object* const controller = ue3::runtime::Ready() ? player::Controller() : nullptr;
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    auto const mode = head_tracking::GameMode();
    bool const onFoot = mode && *mode == 0 && pawn != nullptr;
    vr::host_view::State const view = vr::host_view::Latest();
    vr::controllers::State const controllers = vr::controllers::Latest();
    vr::controllers::Hand const& left = controllers.left;
    gPadOn = s.on && link.publish != nullptr && onFoot;

    std::string why;
    if (!s.on) {
        why = "off ([PowerWheel] on = 0)";
    } else if (link.publish == nullptr) {
        why = "the host is older than services v28 (close the game and redeploy the host)";
    } else if (!onFoot) {
        why = mode ? std::format("not on foot (game mode {})", static_cast<int>(*mode)) : std::string("no player");
    } else if (gConsoleCancel) {
        why = "closed from the console";
    } else if (!gConsole && !view.valid) {
        why = "no head-tracked world view (a menu, a flat screen or VR off)";
    } else if (!gConsole && !controllers.active) {
        why = "the controllers aren't active";
    } else if (!gConsole && !left.gripValid) {
        why = "the left controller isn't tracked";
    } else if (squad_weapons::IsOpen()) {
        why = "the squad's weapon wheel is open";
    } else if (!CallBool(PlayerInput(controller), gIsCombatEnabledFn)) {
        why = "powers can't be used here (combat is disabled)";
    }
    bool const allowed = why.empty();
    gConsoleCancel = false;
    gWhy = allowed ? std::string("ready") : why;

    bool const xHeld = (controllers.active && left.primary) || gConsoleHold;
    if (allowed && (gMachine.open || (xHeld && !gMachine.buttonWas))) {
        gRoster = ReadRoster(pawn, gShowUnlearned);
    }
    std::vector<float> const angles = pw::Angles(static_cast<int>(gRoster.size()));

    pw::MachineInput in;
    in.time = gClock;
    in.allowed = allowed && !gRoster.empty();
    in.button = xHeld;
    if (controllers.active) {
        in.stickX = left.stickX;
        in.stickY = left.stickY;
        in.trigger = left.trigger;
        in.grip = left.squeeze;
    }
    if (gMachine.open && left.gripValid && controllers.active) {
        in.handValid = true;
        in.hand = pw::InPlane(gView.pose, left.grip.position);
    }
    if (gConsoleHand) {
        in.handValid = true;
        in.hand = *gConsoleHand;
    }
    int const selectedBefore = gMachine.selected;
    pw::Event const event = pw::Step(gMachine, in, angles, s);
    if (gConsolePick && gMachine.open) {
        int const pick = std::clamp(*gConsolePick, -1, static_cast<int>(gRoster.size()) - 1);
        if (pick != gMachine.selected) {
            gMachine.selected = pick;
            gMachine.selectedAt = gClock;
        }
    }

    switch (event) {
    case pw::Event::Opened: {
        ++gOpened;
        gConsolePick.reset();
        XrVector3f eye{0.0f, 1.6f, 0.0f};
        XrVector3f forward{0.0f, 0.0f, -1.0f};
        if (view.valid) {
            eye = view.head.position;
            forward = HeadForwardFlat(view.head.orientation);
        }
        XrVector3f palm{eye.x + forward.x * 0.42f, eye.y - 0.28f, eye.z + forward.z * 0.42f};  // no tracked hand: in front, low
        char const* from = "in front of the head (no tracked left hand)";
        if (left.gripValid && controllers.active) {
            palm = left.grip.position;
            from = "the left palm";
        }
        gView = {};
        gView.pose = pw::Place(palm, eye, s.towardM);
        gView.serial = gMachine.serial;
        ++gHintShown;
        std::string names;
        for (Power const& p : gRoster) {
            names += (names.empty() ? "" : ", ") + std::string(p.info->name) + (p.enabled ? "" : " (disabled)") +
                     (p.cooldown > 0.0f ? std::format(" (cooling, {:.0f}% to go)", p.cooldown * 100.0f) : std::string());
        }
        log::Info("power wheel: opened #{} at {} ({:.2f}, {:.2f}, {:.2f}) m, {:.2f} m wide, with {} power(s): {}", gMachine.serial, from,
            gView.pose.position.x, gView.pose.position.y, gView.pose.position.z, s.widthM, gRoster.size(), names);
        if (link.haptic != nullptr && s.haptic > 0.0f) {
            link.haptic(0, 0.02f, s.haptic * 0.6f);
        }
        gLastEvent = "opened";
        break;
    }
    case pw::Event::Equipped:
        ++gEquips;
        if (gMachine.equipped >= 0 && gMachine.equipped < static_cast<int>(gRoster.size())) {
            Equip(controller, gRoster[static_cast<std::size_t>(gMachine.equipped)]);
            gLastEvent = std::string("equipped ") + gRoster[static_cast<std::size_t>(gMachine.equipped)].info->name;
        }
        if (link.haptic != nullptr && s.haptic > 0.0f) {
            link.haptic(0, 0.06f, std::min(1.0f, s.haptic * 1.6f));
        }
        gHeldAfter = true;
        gConsoleHold = false;
        gConsoleHand.reset();
        gConsolePick.reset();
        gConsole = false;
        break;
    case pw::Event::Cancelled:
        ++gCancels;
        log::Info("power wheel: closed without a choice ({})", allowed ? "nothing picked" : gWhy);
        gLastEvent = "cancelled";
        gHeldAfter = true;
        gConsoleHold = false;
        gConsoleHand.reset();
        gConsolePick.reset();
        gConsole = false;
        break;
    case pw::Event::None:
        break;
    }
    if (gMachine.open && gMachine.selected != selectedBefore && gMachine.selected >= 0) {
        ++gPicks;
        if (link.haptic != nullptr && s.haptic > 0.0f) {
            link.haptic(0, 0.015f, s.haptic);
        }
    }
    if (gMachine.open && s.omniTool) {
        omni_tool::Request();  // the forearm wears the game's omni-tool gauntlet while the wheel is open (the world screens tick it)
    }
    if (gHeldAfter && !gMachine.open) {
        bool const still = controllers.active && (left.trigger > 0.2f || left.squeeze > 0.2f || left.primary);
        gHeldAfter = still;
    }

    // What the host draws.
    if (link.publish != nullptr) {
        if (gMachine.open) {
            pw::View& v = gView;
            v.open = 1;
            v.serial = gMachine.serial;
            v.count = static_cast<std::int32_t>(std::min<std::size_t>(gRoster.size(), pw::kMaxEntries));
            for (std::int32_t i = 0; i < v.count; ++i) {
                Power const& p = gRoster[static_cast<std::size_t>(i)];
                pw::ViewEntry& e = v.entries[i];
                e = {};
                std::snprintf(e.label, sizeof(e.label), "%s", p.info->name);
                e.icon = p.info->icon;
                e.discipline = p.info->discipline;
                e.enabled = p.enabled ? 1 : 0;
                e.cooldown = p.cooldown;
            }
            v.selected = gMachine.selected;
            v.selectedFor = static_cast<float>(gClock - gMachine.selectedAt);
            v.openFor = static_cast<float>(gClock - gMachine.openedAt);
            v.widthM = s.widthM;
            v.hand = in.handValid ? in.hand : pw::Vec2{};
            v.emblem = s.emblem ? 1 : 0;
            v.upperCase = s.upperCase ? 1 : 0;
            v.hint = gHintShown <= s.hintOpenings ? 1 : 0;
            link.publish(&v);
        } else if (event == pw::Event::Equipped || event == pw::Event::Cancelled) {
            pw::View closed = gView;
            closed.open = 0;
            link.publish(&closed);
        }
    }
    TickLock(link, view, controllers, controller, pawn, onFoot);
}

void AdjustPad(gamepad::Options& options) {
    if (gPadOn) {
        options.grenadeButton = false;  // X is the wheel's on foot (the physical throw stays)
    }
    if (gMachine.open) {
        options.powerWheel = true;  // the left stick, L3, X, the left trigger and grip are its own
    }
}

bool IsOpen() {
    return gMachine.open;
}

bool OwnsLeftHand() {
    return gMachine.open || gHeldAfter;
}

std::string Console(std::string_view args) {
    std::istringstream words{std::string(args)};
    std::string verb;
    words >> verb;
    if (verb == "open") {
        gConsole = true;
        gConsoleHold = true;
        return "power wheel: X held from the console (no controllers needed); `mevr powerwheel hand x y`, `pick n`, `equip`, `close`";
    }
    if (verb == "hand") {
        float x = 0.0f;
        float y = 0.0f;
        words >> x >> y;
        gConsoleHand = pw::Vec2{x, y};
        return std::format("power wheel: the hand {:.3f}, {:.3f} m from the wheel's centre", x, y);
    }
    if (verb == "pick") {
        int n = -1;
        words >> n;
        gConsolePick = n;
        return std::format("power wheel: tile {} picked", n);
    }
    if (verb == "equip") {
        gConsoleHold = false;  // X let go: the pick is equipped (if the wheel was held open longer than a tap)
        return "power wheel: X let go";
    }
    if (verb == "close") {
        gConsoleCancel = true;
        gConsoleHold = false;
        return "power wheel: closing";
    }
    if (verb == "push") {
        gConsolePush = true;
        return "power wheel: a push on the next tick (casts the equipped power at the lock)";
    }
    if (verb == "aimhead") {
        std::string state;
        words >> state;
        gAimHead = state != "off";
        return std::format("power wheel: the head's ray {} the left hand's for the lock (tests)", gAimHead ? "stands in for" : "no longer stands in for");
    }
    if (verb == "lock") {
        std::string out = std::format("power wheel: lock {}; target {}{}; place {}; last cast: {}", gLockWhy, NameOf(gTarget), gTarget != nullptr && gLock.automatic ? " (automatic)" : "",
            gPlace ? std::format("({:.0f}, {:.0f}, {:.0f})", gPlace->x, gPlace->y, gPlace->z) : std::string("none"), gLastCast);
        out += std::format("\n  last scan: {} pawns, {} awake, {} near the ray or gaze and in range, {} hostile{}", gScanPawns, gScanAwake,
            gScanNear, gScanHostile, gScanNote);
        for (Seen const& seen : gSeen) {
            out += std::format("\n  {}: {:.1f} m, {:.1f} deg from the hand, {:.1f} from the gaze, {}", NameOf(seen.pawn), seen.distanceM, seen.angleDeg,
                seen.gazeDeg, seen.visible ? "in sight" : "blocked");
        }
        return out;
    }
    if (verb == "unlearned") {
        std::string state;
        words >> state;
        gShowUnlearned = state != "off";
        return std::format("power wheel: untrained powers {} the wheel (tests)", gShowUnlearned ? "on" : "off");
    }
    if (verb == "cast") {
        ue3::Object* const controller = ue3::runtime::Ready() ? player::Controller() : nullptr;
        ue3::Object* const manager = controller != nullptr ? gGameModeManager.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
        ue3::TArray<ue3::Object*> const* const modes = manager != nullptr ? gGameModes.In<ue3::TArray<ue3::Object*>>(manager) : nullptr;
        ue3::Object* mode = nullptr;
        for (int i = 0; modes != nullptr && i < modes->Count(); ++i) {
            if (modes->data[i] != nullptr && ue3::IsA(modes->data[i], gGameModeDefault)) {
                mode = modes->data[i];
            }
        }
        ue3::Function* const function = gTryUseMappedPowerFn.Get();
        if (mode == nullptr || function == nullptr) {
            return "power wheel: no SFXGameModeDefault (or its TryUseMappedPower) to cast through";
        }
        ue3::Params parms(ue3::runtime::Get(), function);
        ue3::runtime::Call(mode, function, parms);
        return std::format("power wheel: SFXGameModeDefault.TryUseMappedPower called (the mapped power is {})", MappedPower(controller));
    }
    if (verb == "roster") {
        ue3::Object* const controller = ue3::runtime::Ready() ? player::Controller() : nullptr;
        ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
        if (pawn == nullptr) {
            return "power wheel: no player pawn";
        }
        std::vector<Power> const roster = ReadRoster(pawn, true);
        std::string out = std::format("power wheel: {} power(s); mapped power {}", roster.size(), MappedPower(controller));
        for (Power const& p : roster) {
            out += std::format("\n  {} ({}): {} (m_nEnabled {}, m_nSuppressedCount {}), {:.0f}% of its cooldown to go", p.info->name,
                p.label, p.enabled ? "enabled" : "disabled", p.enabledCount, p.suppressed, p.cooldown * 100.0f);
        }
        return out;
    }
    return StatusJson();
}

std::string StatusJson() {
    std::string roster;
    for (Power const& p : gRoster) {
        roster += (roster.empty() ? "" : ", ") + json::JsonString(p.info->name);
    }
    ue3::Object* const controller = ue3::runtime::Ready() ? player::Controller() : nullptr;
    return std::format("{{\"on\": {}, \"why\": {}, \"open\": {}, \"toggled\": {}, \"serial\": {}, \"roster\": [{}], \"selected\": {}, "
                       "\"hand\": [{}, {}], \"pose\": [{}, {}, {}], \"equipped\": {}, \"mappedPower\": {}, \"last\": {}, \"opened\": {}, "
                       "\"equips\": {}, \"cancels\": {}, \"picks\": {}, \"lock\": {{\"why\": {}, \"target\": {}, \"automatic\": {}, \"seen\": {}, \"locks\": {}, "
        "\"pushes\": {}, \"casts\": {}, \"refused\": {}, \"lastCast\": {}}}}}",
        gSettings.on ? "true" : "false", json::JsonString(gWhy), gMachine.open ? "true" : "false", gMachine.toggled ? "true" : "false",
        gMachine.serial, roster, gMachine.selected, json::JsonNumber(gView.hand.x), json::JsonNumber(gView.hand.y),
        json::JsonNumber(gView.pose.position.x), json::JsonNumber(gView.pose.position.y), json::JsonNumber(gView.pose.position.z),
        json::JsonString(gEquipped), json::JsonString(MappedPower(controller)), json::JsonString(gLastEvent), gOpened, gEquips, gCancels,
        gPicks, json::JsonString(gLockWhy), json::JsonString(NameOf(gTarget)), gTarget != nullptr && gLock.automatic ? "true" : "false", gSeen.size(), gLocks, gPushes, gCastCount, gRefused,
        json::JsonString(gLastCast));
}

void LogStats() {
    log::Info("power wheel: {}; opened {}, equipped {}, closed unequipped {}, picks {}; last equipped {}; lock {}: {} locks, {} pushes, "
              "{} casts, {} refused; last: {}", gWhy, gOpened, gEquips, gCancels, gPicks, gEquipped.empty() ? "none" : gEquipped, gLockWhy,
        gLocks, gPushes, gCastCount, gRefused, gLastCast);
}

void Shutdown(Link const& link) {
    if (link.publishLock != nullptr) {
        pw::LockView none;
        link.publishLock(&none);
    }
    if (link.publish != nullptr && gMachine.open) {
        pw::View closed = gView;
        closed.open = 0;
        link.publish(&closed);
    }
    gMachine = {};
}

}  // namespace mevr::game::power_wheel
