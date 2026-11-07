// SPDX-License-Identifier: MIT
// The power wheel at the left hand (game/PowerWheel.hpp, research/55).

#include "game/PowerWheel.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/FootControls.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/SettingsFile.hpp"
#include "game/HeadTracking.hpp"
#include "game/OmniTool.hpp"
#include "game/Player.hpp"
#include "game/SquadWeapons.hpp"
#include "game/SupportGrip.hpp"
#include "game/Sights.hpp"
#include "game/Weapon.hpp"
#include "game/ViewAnchor.hpp"
#include "engine/XrToUnreal.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <map>
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
// `mevr powerwheel cast` (tests): SFXGameModeDefault.TryUseMappedPower, what the game's RB tap does (the right stick click's
// tap until 2026-11-05).
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
    "; Push your left hand to cast the equipped power (lock, below). While on = 1, X never throws a grenade on foot (the\n"
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
    "; coach: after you equip a power, a card where the wheel was shows how to cast it (its gesture drawn), until you cast it or\n"
    "; 8 s pass, for the first this many equips of each power each session (0: never).\n"
    "coach = 3\n"
    "; omniTool 1: your left forearm wears Shepard's omni-tool gauntlet (the game's own hologram) while the wheel is open.\n"
    "omniTool = 1\n"
    "; The lock and the push: with the wheel closed, raise your left arm (lockReachM ahead of your eyes) and point your hand: the\n"
    "; enemy nearest its ray (within lockAcquireDeg, kept until lockKeepDeg, in sight, within lockRangeM; lockGraceS of blocked\n"
    "; sight allowed) is locked and marked; push your left hand along its ray (pushSpeedMS fast, pushTravelM far) to cast the\n"
    "; equipped power at it, no sooner than pushCooldownS apart. lock 0: off (then only the game's power wheel casts).\n"
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
    "autoLockGazeDeg = 20\n"
    "; selfGestures 1: the powers that aim at nothing are cast by a gesture of the open left hand, not the push: the palm on\n"
    "; your chest (Barrier, Shield Boost, Immunity, Adrenaline Burst), slapped onto the gun (Marksman, Overkill, Carnage,\n"
    "; Assassination), or raised above your head (Unity). 0: the push casts them too.\n"
    "selfGestures = 1\n";

pw::Settings gSettings;
pw::LockSettings gLockSettings;
bool gSelfGestures = true;          // [PowerWheel] selfGestures: the self-cast powers by their gestures, not the push
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
    float coachTimes = static_cast<float>(s.coachTimes);
    number("coach", coachTimes, 0.0f, 100.0f);
    s.coachTimes = static_cast<int>(coachTimes);
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
    bool selfGestures = true;
    flag("selfGestures", selfGestures);
    gSelfGestures = selfGestures;
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
    ue3::Object* object = nullptr;  // the BioPower
    ue3::Name name{};
    std::string label;
    pw::PowerInfo const* info = nullptr;
    float cooldown = 0.0f;  // 0..1 of its cooldown still to run
    bool enabled = true;
    std::optional<bool> gameEnabled;
    std::optional<bool> requiredWeapon;
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

void ReadEnabled(Power& power, ue3::Object* object) {
    power.gameEnabled = gIsEnabledFn.Get() != nullptr ? std::optional<bool>(CallBool(object, gIsEnabledFn)) : std::nullopt;
    power.requiredWeapon = gIsRequiredWeaponFn.Get() != nullptr ? std::optional<bool>(CallBool(object, gIsRequiredWeaponFn)) : std::nullopt;
    power.enabled = power.enabledCount != 0 && power.gameEnabled.value_or(true) && power.requiredWeapon.value_or(true);
}

std::string EnabledChecks(Power const& power) {
    auto const flag = [](std::optional<bool> value) { return value ? (*value ? "true" : "false") : "function missing"; };
    return std::format("m_nEnabled {}, m_nSuppressedCount {}, IsEnabled {}, IsRequiredWeaponEquipped {}",
        power.enabledCount, power.suppressed, flag(power.gameEnabled), flag(power.requiredWeapon));
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
        p.object = power;
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
        ReadEnabled(p, power);
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
std::uint32_t gSourceLogged = 0;  // the opening whose light's source (the omni-tool's display) was logged
// The how-to-cast card after an equip (research/55 "How to cast it, on a card"): what, since when, the casts by then.
struct CoachState {
    pw::Coach kind = pw::Coach::None;
    std::string name;
    double since = 0.0;
    std::uint64_t casts = 0;
};
CoachState gCoach;
std::map<std::string, int> gCoachShown;  // equips coached, per power label, this session
unsigned gCoachCount = 0;
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
    pw::SelfGesture const gesture = gSelfGestures ? pw::SelfGestureFor(power.label.c_str()) : pw::SelfGesture::None;
    log::Info("power wheel: equipped {} ({}): the game's mapped power was {}; {} casts it", power.info->name, power.label,
        before.empty() ? "none" : before, gesture != pw::SelfGesture::None ? pw::Name(gesture) : "a push of the left hand");
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
int gPushMissedSeen = 0;      // gPush.heldMissed already logged
float gPushHeldReach = 0.0f;  // the farthest the hand got ahead of the eyes while a push was held
double gRaisedAt = -1e9;      // the arm last raised (the lock's wind-up)
constexpr double kWindUpS = 1.5;  // a lock is kept this long with the arm lowered: point, pull back to the chest, push
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
        p.object = power;
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
        ReadEnabled(p, power);
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
    log::Info("power wheel: cast refused: {}", why);
    if (link.haptic != nullptr && gSettings.haptic > 0.0f) {
        link.haptic(0, 0.12f, gSettings.haptic * 0.5f);  // a long soft buzz: no
    }
}

// ---- The bolt from the hand (research/55 section 9, a prototype) ---------------------------------------------------------
// The player's powers fly nothing (section 8), but the enemies' versions of the same power do, each with its own projectile
// effect (TK_Throw_NPC's ThrowProjectile_VFX, SD_Warp_NPC's WarpProjectile_VFX...). At our push's cast that effect is
// spawned at the left hand (BioVisualEffect.CreateVFXOnPosition on the class's default object) and moved each tick to the
// target at the enemies' projectile speed (1300 units/s), then let go (SetLifeTime) on arrival: only a picture, the game's
// own release still decides the hit. The release's moment (BioPawn.ReleasePower on the player's pawn) is logged against it.
ue3::FieldRef gVfxAppearanceOf{"SFXGame.BioPower", "m_VFXAppearance"};
ue3::FieldRef gVfxProjectileOf{"SFXGame.BioPowerVFXAppearance", "vfxProjectileEffect"};
ue3::ClassRef gPowerClassForBolt{"SFXGame.BioPower"};
ue3::ClassRef gVisualEffectClass{"SFXGame.BioVisualEffect"};
ue3::FunctionRef gCreateVfxOnPositionFn{"SFXGame.BioVisualEffect.CreateVFXOnPosition"};
ue3::FunctionRef gVfxSetLifeTimeFn{"SFXGame.BioVisualEffect.SetLifeTime"};
ue3::FunctionRef gSetLocationFn{"Engine.Actor.SetLocation"};
ue3::FunctionRef gReleasePowerFn{"SFXGame.BioPawn.ReleasePower"};
// The flight takes as long as the game takes to release the power after the cast (learned per power from BioPawn.ReleasePower;
// Throw's measured 0.57 s at first), so the bolt lands as the game's effect does. `bolt speed <units/s>` (tests) flies at a
// fixed speed instead, slow enough to catch it in a snapshot.
constexpr double kDefaultReleaseS = 0.55;
double gBoltTestSpeed = 0.0;  // 0: timed to the release
std::map<std::string, double> gReleaseDelay;  // per power label, seconds from the cast to the release (smoothed)
constexpr std::uint64_t kClassDefaultFlag = 0x200;  // EObjectFlags: RF_ClassDefaultObject

struct Bolt {
    ue3::Object* effect = nullptr;
    std::string power;
    engine::UnrealVector from{};
    engine::UnrealVector to{};
    double startedAt = 0.0;
    double flightS = 0.0;
    bool arrived = false;
    std::string template_;
};
std::optional<Bolt> gBolt;
bool gBoltOn = true;           // `mevr powerwheel bolt on|off` (a prototype)
std::string gPushCastPower;    // its power's label
double gPushCastAt = -100.0;
bool gReleasedSinceCast = false;  // BioPawn.ReleasePower seen since our last cast
bool gReleaseByMod = false;       // ...sent by the mod right now (a cast with the weapon away)   // our push's cast (gClock), for the release's timing
std::string gLastBolt = "none";
unsigned gBolts = 0;

ue3::Object* EffectDefaults() {
    static ue3::Object* cached = nullptr;
    if (cached != nullptr && ue3::runtime::IsAlive(cached)) {
        return cached;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gVisualEffectClass.Get();
    for (std::int32_t i = 0, n = cls != nullptr ? r.ObjectCount() : 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.ClassOf(o) == cls && (r.FlagsOf(o) & kClassDefaultFlag) != 0) {
            cached = o;
            return o;
        }
    }
    return nullptr;
}

// The fallback where no enemy version is loaded (enemies' effects come with the level): the player's power's own particles, always
// loaded with it: the caster's charge (PlayerCrust: the biotics' Biotics_Charge_001) or else its release (the tech and self-cast
// powers' omni-tool prefabs). Only a template with a prefab (particle systems) can fly; `source` names it.
ue3::FieldRef gBoltPlayerCrust{"SFXGame.BioPowerVFXAppearance", "PlayerCrust"};
ue3::FieldRef gBoltRelease{"SFXGame.BioPowerVFXAppearance", "vfxReleaseEffect"};
ue3::FieldRef gBoltPrefab{"SFXGame.BioVFXTemplate", "oPrefab"};
ue3::Object* FallbackTemplate(ue3::Object* playerPower, std::string& source) {
    ue3::Object* const look = playerPower != nullptr ? gVfxAppearanceOf.Read<ue3::Object*>(playerPower).value_or(nullptr) : nullptr;
    if (look == nullptr) {
        return nullptr;
    }
    auto const withPrefab = [](ue3::Object* templ) {
        return templ != nullptr && gBoltPrefab.Read<ue3::Object*>(templ).value_or(nullptr) != nullptr ? templ : nullptr;
    };
    ue3::Object** const crust = gBoltPlayerCrust.In<ue3::Object*>(look);  // FPowerVFXData: the template first
    if (ue3::Object* const charge = withPrefab(crust != nullptr ? *crust : nullptr)) {
        source = "its own charge (no enemy version loaded)";
        return charge;
    }
    if (ue3::Object* const release = withPrefab(gBoltRelease.Read<ue3::Object*>(look).value_or(nullptr))) {
        source = "its own release effect (no enemy version loaded)";
        return release;
    }
    return nullptr;
}

// The projectile effect for the player's power `label`: its own if it has one, else an enemy version's (TK_Throw ->
// TK_Throw_NPC or TK_Throw_PLC...) among the loaded powers, else FallbackTemplate's. Null: nothing with particles.
ue3::Object* BoltTemplate(ue3::Object* playerPower, std::string const& label, std::string& source) {
    auto projectileOf = [](ue3::Object* power) -> ue3::Object* {
        ue3::Object* const look = power != nullptr ? gVfxAppearanceOf.Read<ue3::Object*>(power).value_or(nullptr) : nullptr;
        return look != nullptr ? gVfxProjectileOf.Read<ue3::Object*>(look).value_or(nullptr) : nullptr;
    };
    if (ue3::Object* const own = projectileOf(playerPower)) {
        source = "its own";
        return own;
    }
    ue3::Object* const fallback = FallbackTemplate(playerPower, source);
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gPowerClassForBolt.Get();
    for (std::int32_t i = 0, n = cls != nullptr ? r.ObjectCount() : 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || o == playerPower || !r.IsAlive(o) || !r.IsA(o, cls)) {
            continue;
        }
        std::string const other = ue3::runtime::NameText(gPowerName.Read<ue3::Name>(o).value_or(ue3::Name{}));
        if (other.size() > label.size() && other.compare(0, label.size(), label) == 0 && other[label.size()] == '_') {
            if (ue3::Object* const templ = projectileOf(o)) {
                source = other + " on " + NameOf(r.OuterOf(o));
                return templ;
            }
        }
    }
    return fallback;  // null if the power has nothing with particles either
}

bool SetActorLocation(ue3::Object* actor, engine::UnrealVector const& where) {
    ue3::Function* const fn = gSetLocationFn.Get();
    if (actor == nullptr || fn == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    parms.Set("NewLocation", ToVector(where));
    ue3::runtime::Call(actor, fn, parms);  // an opcode native: through runtime::Call (gotchas/process-event-skips-opcode-natives)
    return parms.ReturnBool().value_or(false);
}

void EndBolt(float fadeS) {
    if (gBolt && gBolt->effect != nullptr && ue3::runtime::IsAlive(gBolt->effect) && gVfxSetLifeTimeFn.Get() != nullptr) {
        ue3::Params p(ue3::runtime::Get(), gVfxSetLifeTimeFn.Get());
        p.Set("fLife", fadeS);
        ue3::runtime::Call(gBolt->effect, gVfxSetLifeTimeFn.Get(), p);
    }
    gBolt.reset();
}

void StartBolt(Power const& power, engine::UnrealVector const& from, engine::UnrealVector const& to) {
    gPushCastAt = gClock;
    gPushCastPower = power.label;
    EndBolt(0.2f);
    std::string source;
    ue3::Object* const templ = BoltTemplate(power.object, power.label, source);
    ue3::Object* const defaults = EffectDefaults();
    if (templ == nullptr || defaults == nullptr || gCreateVfxOnPositionFn.Get() == nullptr) {
        gLastBolt = std::format("{}: no bolt ({})", power.label, templ == nullptr ? "no enemy version loaded and no particles of its own"
                                                                                  : "BioVisualEffect not found");
        log::Info("power wheel: {}", gLastBolt);
        return;
    }
    engine::UnrealVector const d = Minus(to, from);
    double const length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    constexpr double kToUnreal = 32768.0 / 3.14159265358979323846;
    ue3::Rotator const facing{static_cast<std::int32_t>(std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)) * kToUnreal),
        static_cast<std::int32_t>(std::atan2(d.y, d.x) * kToUnreal), 0};
    auto const learned = gReleaseDelay.find(power.label);
    double const flight = gBoltTestSpeed > 0.0 ? std::clamp(length / gBoltTestSpeed, 0.15, 10.0)
                                               : std::clamp(learned != gReleaseDelay.end() ? learned->second : kDefaultReleaseS, 0.15, 3.0);
    ue3::Params p(ue3::runtime::Get(), gCreateVfxOnPositionFn.Get());
    bool const set = p.Set("a_pTemplate", templ) && p.Set("vPosition", ToVector(from)) && p.Set("rOrientation", facing) &&
        p.Set("fLifeTime", static_cast<float>(flight + 2.0)) && p.SetBool("bUseVFXPool", false);
    if (!set || !ue3::runtime::Call(defaults, gCreateVfxOnPositionFn.Get(), p)) {
        gLastBolt = power.label + ": CreateVFXOnPosition failed";
        log::Warn("power wheel: {}", gLastBolt);
        return;
    }
    ue3::Object* const effect = p.Return<ue3::Object*>().value_or(nullptr);
    if (effect == nullptr) {
        gLastBolt = power.label + ": CreateVFXOnPosition gave no effect";
        log::Warn("power wheel: {}", gLastBolt);
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const full = r.FullName(templ);
    gBolt = Bolt{effect, power.label, from, to, gClock, flight, false, full.substr(full.find(' ') + 1)};
    ++gBolts;
    gLastBolt = std::format("{}: {} from {} ({}), {:.1f} m in {:.2f} s", power.label, gBolt->template_, source, NameOf(effect),
        length / engine::kUnitsPerMeter, flight);
    log::Info("power wheel: bolt {}", gLastBolt);
}

void TickBolt() {
    if (!gBolt) {
        return;
    }
    if (gBolt->effect == nullptr || !ue3::runtime::IsAlive(gBolt->effect)) {
        gBolt.reset();
        return;
    }
    double const t = std::min(1.0, (gClock - gBolt->startedAt) / gBolt->flightS);
    engine::UnrealVector const at = Plus(gBolt->from, Scaled(Minus(gBolt->to, gBolt->from), t));
    SetActorLocation(gBolt->effect, at);
    if (t >= 1.0 && !gBolt->arrived) {
        gBolt->arrived = true;
        log::Info("power wheel: the {} bolt arrived {:.2f} s after the cast", gBolt->power, gClock - gBolt->startedAt);
        EndBolt(0.25f);
    }
}

// BioPawn.ReleasePower (the casting animation's notify, ProcessEvent from native code): when the game's power actually goes off.
void OnReleasePower(ue3::Object* pawn, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr || pawn != player::BioPawn(controller) || gClock - gPushCastAt > 6.0) {
        return;
    }
    if (!gPushCastPower.empty()) {
        double& d = gReleaseDelay[gPushCastPower];
        d = d > 0.0 ? 0.5 * d + 0.5 * (gClock - gPushCastAt) : gClock - gPushCastAt;
        gPushCastPower.clear();  // only the first release after a cast
    }
    gReleasedSinceCast = true;
    log::Info("power wheel: {} released the power {:.2f} s after our cast{}", gReleaseByMod ? "the mod (the weapon is away)" : "the game",
        gClock - gPushCastAt,
        gBolt ? std::format(" (the bolt is {:.0f}% of the way)", 100.0 * std::min(1.0, (gClock - gBolt->startedAt) / gBolt->flightS))
              : std::string());
}

// A cast with the weapon away (measured 2026-11-05, research/55 section 9): the game accepts it and starts the cooldown, but
// its release is the casting animation's notify (BioPawn.ReleasePower), which the weapon-away animation never sends: the power
// was spent for nothing. ME1 itself never casts with the weapon away (the mapped power and the wheel draw it first). So after
// such a cast the mod sends that event itself, at the delay a release takes with the weapon out, unless the game did.
ue3::FunctionRef gInCombatStateFn{"SFXGame.BioPawn.IsInCombatActionState"};
bool gUnarmedRelease = true;   // `mevr powerwheel unarmed on|off` (tests)
double gUnarmedReleaseAt = -1.0;
ue3::Object* gUnarmedPawn = nullptr;

bool WeaponOut(ue3::Object* pawn) {
    ue3::Function* const fn = gInCombatStateFn.Get();
    if (pawn == nullptr || fn == nullptr) {
        return true;  // unknown: leave it to the game
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    ue3::runtime::Call(pawn, fn, parms);
    return parms.ReturnBool().value_or(true);
}

void TickUnarmedRelease() {
    if (gUnarmedReleaseAt < 0.0 || gClock < gUnarmedReleaseAt) {
        return;
    }
    gUnarmedReleaseAt = -1.0;
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    if (gReleasedSinceCast || pawn == nullptr || pawn != gUnarmedPawn || gReleasePowerFn.Get() == nullptr) {
        return;
    }
    ue3::Params parms(ue3::runtime::Get(), gReleasePowerFn.Get());
    gReleaseByMod = true;
    ue3::runtime::Call(pawn, gReleasePowerFn.Get(), parms);  // the casting animation's notify, as the game sends it
    gReleaseByMod = false;
}

// ---- Self-cast gestures and the omni-tool (research/55 section 10) --------------------------------------------------------
pw::SelfCastSettings gSelfSettings;
pw::SelfCastState gSelfCast;
pw::SelfView gSelfView;
pw::ConfirmationState gConfirmation;
pw::SelfView gConfirmed;
std::string gSelfPower;
float gSelfProgress = 0.0f;         // the gesture's dwell, 0..1 (the hand is in place)
bool gConsoleSelfCast = false;      // `mevr powerwheel selfcast` (tests)
std::string gLastCastPower;
bool gLastCastUsesOmniTool = false;
bool gOmniAiming = false;           // this tick: a power that uses the omni-tool is being aimed or gestured
std::map<std::string, bool> gUsesOmniTool;
ue3::FieldRef gOmniLook{"SFXGame.BioPower", "m_VFXAppearance"};
ue3::FieldRef gOmniCrust{"SFXGame.BioPowerVFXAppearance", "PlayerCrust"};
ue3::FieldRef gOmniRelease{"SFXGame.BioPowerVFXAppearance", "vfxReleaseEffect"};

// A power that shows the omni-tool: a tech power, or one whose own effects are the omni-tool's (the game's appearance: Shield
// Boost, Unity, Immunity, Adrenaline Burst carry BIOG_V_T_Omni_Tool_Z's prefabs). Left-hand casts wear the gauntlet then.
bool UsesOmniTool(Power const& power) {
    if (auto const known = gUsesOmniTool.find(power.label); known != gUsesOmniTool.end()) {
        return known->second;
    }
    bool uses = power.info != nullptr && power.info->discipline == 1;
    ue3::Object* const look = power.object != nullptr ? gOmniLook.Read<ue3::Object*>(power.object).value_or(nullptr) : nullptr;
    if (!uses && look != nullptr) {
        ue3::Reflection& r = ue3::runtime::Get();
        ue3::Object** const crust = gOmniCrust.In<ue3::Object*>(look);
        for (ue3::Object* const templ : {crust != nullptr ? *crust : nullptr, gOmniRelease.Read<ue3::Object*>(look).value_or(nullptr)}) {
            if (templ != nullptr && r.FullName(templ).find("Omni_Tool") != std::string::npos) {
                uses = true;
            }
        }
    }
    gUsesOmniTool[power.label] = uses;
    return uses;
}

// After a cast of ours (the push or a self-cast gesture): its time for the release, and the release itself with the weapon away.
void AfterCast(Power const& power, ue3::Object* pawn) {
    gPushCastAt = gClock;
    gReleasedSinceCast = false;
    gPushCastPower = power.label;
    gLastCastPower = power.label;
    gLastCastUsesOmniTool = UsesOmniTool(power);
    if (gUnarmedRelease && !WeaponOut(pawn)) {
        auto const learned = gReleaseDelay.find(power.label);
        gUnarmedReleaseAt = gClock + (learned != gReleaseDelay.end() ? learned->second : kDefaultReleaseS);
        gUnarmedPawn = pawn;
    }
}

void CastFeedback(Link const& link, bool ok) {
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

void LogGesture(pw::GestureAttempt const& attempt, std::string const& power, pw::SelfGesture gesture, std::string const& outcome) {
    pw::PowerInfo const* const info = pw::FindPower(power.c_str());
    log::Info("power wheel: gesture attempt: {} ({}) closest {:.3f} m, entry {:.2f} m/s, open {}, gun drawn {}: {}",
        info != nullptr ? std::format("{} [{}]", info->name, power) : power,
        pw::Name(gesture), attempt.closestM, attempt.entrySpeedMS, attempt.open, attempt.gunDrawn, outcome);
}

void ConfirmGesture(Link const& link, bool ok, std::string const& reason) {
    pw::BeginConfirmation(gConfirmation, ok ? pw::Confirmation::Success : pw::Confirmation::Refused, gClock);
    gConfirmed = gSelfView;
    gConfirmed.confirmation = gConfirmation.kind;
    std::snprintf(gConfirmed.reason, sizeof(gConfirmed.reason), "%s", reason.c_str());
    pw::ConfirmationStep const beat = pw::StepConfirmation(gConfirmation, gClock);
    if (link.haptic != nullptr && gSettings.haptic > 0.0f) {
        for (int hand : {0, 1}) { // Host/HalcyonBridge: 0 left, 1 right
            link.haptic(hand, beat.seconds, std::min(1.0f, beat.strength * gSettings.haptic));
        }
    }
}

void TickConfirmation(Link const& link, vr::controllers::State const& controllers) {
    pw::ConfirmationStep const confirmation = pw::StepConfirmation(gConfirmation, gClock);
    if (confirmation.visible) {
        gSelfView.confirmation = gConfirmed.confirmation;
        gSelfView.confirmationFor = confirmation.age;
        gSelfView.target = gConfirmed.target;
        gSelfView.hand = controllers.active && controllers.left.gripValid ? controllers.left.grip.position : gConfirmed.hand;
        std::snprintf(gSelfView.name, sizeof(gSelfView.name), "%s", gConfirmed.name);
        std::snprintf(gSelfView.reason, sizeof(gSelfView.reason), "%s", gConfirmed.reason);
    }
    if (confirmation.beat && link.haptic != nullptr && gSettings.haptic > 0.0f) {
        for (int hand : {0, 1}) {
            link.haptic(hand, confirmation.seconds, std::min(1.0f, confirmation.strength * gSettings.haptic));
        }
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
    gOmniAiming = false;
    gSelfView = {};
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
    } else if (support_grip::Held() && (!gSelfGestures || pw::SelfGestureFor(ready->label.c_str()) == pw::SelfGesture::None)) {
        why = "the left hand holds the gun";
    } else if (squad_weapons::IsOpen()) {
        why = "the squad's weapon wheel is open";
    }
    if (why != nullptr) {
        if (gSelfCast.reaching && !gSelfCast.reported) {
            LogGesture(gSelfCast.attempt, gSelfPower, pw::SelfGestureFor(gSelfPower.c_str()), std::format("missed ({})", why));
        }
        gSelfCast = {};
        if (!onFoot || !view.valid || !s.on || gMachine.open) {
            gConfirmation = {};
        }
        gLockWhy = why;
        gLock = {};
        gTarget = nullptr;
        publish();
        return;
    }
    gLockWhy = "ready";
    pw::SelfGesture const gesture = gSelfGestures ? pw::SelfGestureFor(ready->label.c_str()) : pw::SelfGesture::None;
    if (gSelfPower != ready->label) {
        if (gSelfCast.reaching && !gSelfCast.reported) {
            LogGesture(gSelfCast.attempt, gSelfPower, pw::SelfGestureFor(gSelfPower.c_str()), "missed (power changed)");
        }
        gSelfCast = {};
        gConfirmation = {};
        gSelfPower = ready->label;
    }
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
    // The wind-up: with the arm lowered (pulled back to push) the lock stays on what was locked for kWindUpS; the scan
    // doesn't run then, so it would let go at once.
    if (raised) {
        gRaisedAt = gClock;
    }
    bool const windUp = hand && !raised && gTarget != nullptr && ue3::runtime::IsAlive(gTarget) && gClock - gRaisedAt <= kWindUpS;
    std::uint64_t const id = windUp ? reinterpret_cast<std::uint64_t>(gTarget) : pw::UpdateLock(gLock, candidates, gClock, s);
    ue3::Object* target = windUp ? gTarget : nullptr;
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
        pushed = pw::StepPush(gPush, gClock, controllers.left.aim.position, along, s, raised) || pushed;
        if (gPush.held) {
            gPushHeldReach = std::max(gPushHeldReach, reach);
        }
        if (gPush.heldMissed != gPushMissedSeen) {
            gPushMissedSeen = gPush.heldMissed;
            log::Info("power wheel: a push of {} ended with the arm in (the hand {:.2f} m ahead of the eyes at most; it casts once "
                      "{:.2f} m ahead): not cast", ready->info->name, gPushHeldReach, s.reachM);
            gPushHeldReach = 0.0f;
        } else if (!gPush.held) {
            gPushHeldReach = 0.0f;
        }
    }
    if (pushed && gesture == pw::SelfGesture::None) {
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
                if (gSelfGestures && pw::SelfGestureFor(ready->label.c_str()) != pw::SelfGesture::None) {
                    tried = false;
                    Refuse(link, std::format("{} isn't pushed: {} casts it", ready->info->name,
                        pw::Name(pw::SelfGestureFor(ready->label.c_str()))));
                } else {
                    ok = Cast(controller, *ready, nullptr, std::nullopt);
                }
                break;
            }
            if (tried) {
                log::Info("power wheel: push: {}", gLastCast);
                if (ok) {
                    AfterCast(*ready, pawn);
                }
                if (ok && ready->info->aim != pw::Aim::None && gBoltOn) {
                    // The bolt leaves 6 cm ahead of the hand (the test hand with `aimhead`: held out low and left).
                    engine::UnrealVector from = Plus(origin, Scaled(direction, 0.06 * upm));
                    if (!hand) {
                        XrVector3f const offset = RotateXr(view.head.orientation, {-0.15f, -0.20f, -0.40f});
                        from = engine::ToWorld(*frame, XrVector3f{view.head.position.x + offset.x, view.head.position.y + offset.y,
                                                                  view.head.position.z + offset.z});
                    }
                    StartBolt(*ready, from, target != nullptr ? Plus(gTargetAt, {0.0, 0.0, kChestUnits}) : *gPlace);
                }
                CastFeedback(link, ok);
            }
        }
    }

    // The self-cast gestures (research/55 section 10): a power that aims at nothing is cast by its own gesture of the open left
    // hand, not the push (the chest, the gun, the raise); `mevr powerwheel selfcast` stands in for it without tracked hands.
    pw::SelfCastInput self;
    self.time = gClock;
    self.tracked = hand && controllers.left.gripValid;
    self.open = !support_grip::Held() && controllers.left.squeeze < 0.3f && controllers.left.trigger < 0.3f;
    self.hand = controllers.left.grip.position;
    self.head = view.head;
    self.gunDrawn = WeaponOut(pawn);
    self.gun = controllers.right.aim;
    self.gunProbe = controllers.right.aimValid && self.gun.position.y >= view.head.position.y - 0.45f;
    if (gesture == pw::SelfGesture::Gun && self.gunDrawn) {
        // Use the live, kicked model so both the target and the contact volume stay on the drawn receiver/barrel.
        if (auto const gun = weapon::CurrentAsDrawn()) {
            vr::aim_ray::Ray ray;
            ray.valid = true;
            ray.origin = engine::ToTracking(*frame, gun->location);
            double const back = Dot3(Minus(gun->origin, gun->location), gun->barrel);
            XrVector3f const base = engine::ToTracking(*frame, Plus(gun->location, Scaled(gun->barrel, back)));
            XrVector3f const along = engine::ToTracking(*frame, Plus(gun->location, Scaled(gun->barrel, upm)));
            ray.direction = {along.x - ray.origin.x, along.y - ray.origin.y, along.z - ray.origin.z};
            if (auto const top = sights::GunTop(ray)) {
                self.gunZone = pw::GunZone(base, ray.origin, *top, gSelfSettings);
                self.gunValid = true;
            }
        }
    }
    pw::SelfZone const zone = pw::ZoneFor(gesture, self, gSelfSettings);
    gSelfView.look = pw::LookFor(gesture, self, zone);
    // A stable centre while distant, then the point on the capsule nearest the approaching palm.
    XrVector3f const middle{(zone.start.x + zone.end.x) * 0.5f, (zone.start.y + zone.end.y) * 0.5f,
        (zone.start.z + zone.end.z) * 0.5f};
    gSelfView.target = gSelfView.look == pw::SelfLook::Faint ? middle : pw::ClosestPoint(zone, self.hand);
    gSelfView.hand = self.hand;
    std::snprintf(gSelfView.name, sizeof(gSelfView.name), "%s", ready->info->name);
    pw::SelfCastStep const selfStep = pw::StepSelfCast(gSelfCast, gesture, self, gSelfSettings);
    gSelfProgress = selfStep.progress;
    gSelfView.progress = selfStep.progress;
    if (selfStep.missed) {
        LogGesture(selfStep.attempt, ready->label, gesture, "missed");
    }
    if (selfStep.entered && !selfStep.cast && !selfStep.weaponMissing && link.haptic != nullptr && gSettings.haptic > 0.0f) {
        link.haptic(0, 0.03f, gSettings.haptic * 0.6f);  // a tick: the hand is in place
    }
    bool const selfAsked = gConsoleSelfCast;
    gConsoleSelfCast = false;
    if (gesture != pw::SelfGesture::None && (selfStep.cast || selfStep.weaponMissing || selfAsked)) {
        bool ok = false;
        std::string reason;
        pw::SelfRefusal const refusal = pw::RefusalFor(gesture, WeaponOut(pawn), ready->enabled && !selfStep.weaponMissing, ready->cooldown);
        if (refusal == pw::SelfRefusal::DrawWeapon) {
            reason = "Draw your weapon";
        } else if (refusal == pw::SelfRefusal::CoolingDown) {
            reason = std::format("Cooling down {:.0f}%", ready->cooldown * 100.0f);
        } else if (refusal == pw::SelfRefusal::Unavailable) {
            reason = "Not available now";
        } else {
            ok = Cast(controller, *ready, nullptr, std::nullopt);
            if (ok) {
                AfterCast(*ready, pawn);
                ++gCastCount;
                ++gCasts;
            } else {
                reason = "Not available now";
            }
        }
        if (!ok) {
            ++gRefused;
            gLastCast = reason;
        }
        ConfirmGesture(link, ok, reason);
        LogGesture(selfStep.attempt, ready->label, gesture, ok ? "cast" : "refused: " + reason);
    }
    gOmniAiming = UsesOmniTool(*ready) && (gSelfProgress > 0.0f || (ready->info->aim != pw::Aim::None && raised && (target != nullptr || gPlace)));

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

// ---- The powers' own visual effects (research/55 section 9) --------------------------------------------------------------
// Every BioPower has a BioPowerVFXAppearance: nine slots (a template and a flag each) for the caster's body, the screen, the
// target and the world, the casting beam, the projectile's attach point, and the projectile's and the release's own effects.
// `mevr powerwheel vfx [all]` lists the player's; `all` adds every other loaded power's projectile and release effects (the
// enemies' versions of the same power, which fly).
ue3::FieldRef gVfxAppearance{"SFXGame.BioPower", "m_VFXAppearance"};
ue3::ClassRef gBioPowerClass{"SFXGame.BioPower"};
struct VfxSlot {
    char const* name;
    ue3::FieldRef field;
};
std::array<VfxSlot, 9> gVfxSlots{{
    {"PlayerCrust", {"SFXGame.BioPowerVFXAppearance", "PlayerCrust"}},
    {"PlayerMaterial", {"SFXGame.BioPowerVFXAppearance", "PlayerMaterial"}},
    {"UsingEffect", {"SFXGame.BioPowerVFXAppearance", "UsingEffect"}},
    {"FramebufferEffect", {"SFXGame.BioPowerVFXAppearance", "FramebufferEffect"}},
    {"TargetCrust", {"SFXGame.BioPowerVFXAppearance", "TargetCrust"}},
    {"TargetMaterial", {"SFXGame.BioPowerVFXAppearance", "TargetMaterial"}},
    {"ImpactVisualEffect", {"SFXGame.BioPowerVFXAppearance", "ImpactVisualEffect"}},
    {"WorldImpactVisualEffect", {"SFXGame.BioPowerVFXAppearance", "WorldImpactVisualEffect"}},
    {"CastingBeam", {"SFXGame.BioPowerVFXAppearance", "CastingBeam"}},
}};
ue3::FieldRef gVfxAttachPoint{"SFXGame.BioPowerVFXAppearance", "nmProjectileAttachPoint"};
ue3::FieldRef gVfxProjectile{"SFXGame.BioPowerVFXAppearance", "vfxProjectileEffect"};
ue3::FieldRef gVfxRelease{"SFXGame.BioPowerVFXAppearance", "vfxReleaseEffect"};
ue3::FieldRef gTemplatePrefab{"SFXGame.BioVFXTemplate", "oPrefab"};
ue3::FieldRef gTemplateLifeTime{"SFXGame.BioVFXTemplate", "fLifeTime"};

// A template as "Package.Name (prefab P, 1.5 s)", or "-".
std::string TemplateText(ue3::Object* templ) {
    if (templ == nullptr) {
        return "-";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const full = r.FullName(templ);
    std::string const name = full.substr(full.find(' ') + 1);
    ue3::Object* const prefab = gTemplatePrefab.Read<ue3::Object*>(templ).value_or(nullptr);
    return std::format("{} (prefab {}, {:.2f} s)", name, prefab != nullptr ? r.NameText(prefab) : std::string("none"),
        gTemplateLifeTime.Read<float>(templ).value_or(0.0f));
}

std::string PowerLabel(ue3::Object* power) {
    return ue3::runtime::NameText(gPowerName.Read<ue3::Name>(power).value_or(ue3::Name{}));
}

std::string VfxMap(bool all) {
    ue3::Object* const controller = ue3::runtime::Ready() ? player::Controller() : nullptr;
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    ue3::Object* const behavior = pawn != nullptr ? gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    auto const list = behavior != nullptr ? gPowers.ElementsOf(behavior) : std::nullopt;
    if (!list) {
        return "power wheel: vfx: no player pawn with powers";
    }
    std::string out = std::format("power wheel: vfx: the player's {} power(s)", list->count);
    std::vector<ue3::Object*> mine;
    for (std::int32_t i = 0; i < list->count && i < 64; ++i) {
        void* const slot = list->At(i);
        ue3::Object* const power = slot != nullptr ? *static_cast<ue3::Object**>(slot) : nullptr;
        if (power == nullptr || !ue3::runtime::IsAlive(power)) {
            continue;
        }
        mine.push_back(power);
        ue3::Object* const look = gVfxAppearance.Read<ue3::Object*>(power).value_or(nullptr);
        out += std::format("\n  {} ({}): appearance {}", PowerLabel(power), NameOf(power), NameOf(look));
        if (look == nullptr) {
            continue;
        }
        for (VfxSlot& s : gVfxSlots) {
            ue3::Object** const templ = s.field.In<ue3::Object*>(look);  // FPowerVFXData: the template first
            if (templ != nullptr && *templ != nullptr) {
                out += std::format("\n    {}: {}", s.name, TemplateText(*templ));
            }
        }
        out += std::format("\n    attach point: {}; projectile: {}; release: {}",
            ue3::runtime::NameText(gVfxAttachPoint.Read<ue3::Name>(look).value_or(ue3::Name{})),
            TemplateText(gVfxProjectile.Read<ue3::Object*>(look).value_or(nullptr)),
            TemplateText(gVfxRelease.Read<ue3::Object*>(look).value_or(nullptr)));
    }
    if (!all) {
        return out;
    }
    // Every other loaded power with a projectile or release effect: the enemies' (and the squad's) versions.
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gBioPowerClass.Get();
    int shown = 0;
    out += "\n  other loaded powers with a projectile or release effect:";
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n && cls != nullptr && shown < 60; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls) || std::find(mine.begin(), mine.end(), o) != mine.end()) {
            continue;
        }
        ue3::Object* const look = gVfxAppearance.Read<ue3::Object*>(o).value_or(nullptr);
        ue3::Object* const projectile = look != nullptr ? gVfxProjectile.Read<ue3::Object*>(look).value_or(nullptr) : nullptr;
        ue3::Object* const release = look != nullptr ? gVfxRelease.Read<ue3::Object*>(look).value_or(nullptr) : nullptr;
        if (projectile == nullptr && release == nullptr) {
            continue;
        }
        ++shown;
        out += std::format("\n    {} on {} (appearance {}): projectile {}; release {}", PowerLabel(o), NameOf(r.OuterOf(o)), NameOf(look),
            TemplateText(projectile), TemplateText(release));
    }
    return out;
}

}  // namespace

bool TryStartup() {
    ue3::Function* const release = gReleasePowerFn.Get();
    if (release == nullptr) {
        log::Warn("power wheel: BioPawn.ReleasePower not found: the bolt's timing against the game's release isn't logged");
        return false;
    }
    engine::process_event::AddPostHandler(release, &OnReleasePower);
    return true;
}

void Tick(double deltaSeconds, Link const& link) {
    gClock += std::max(deltaSeconds, 0.0);
    RefreshSettings();
    TickBolt();
    TickUnarmedRelease();
    pw::Settings const& s = gSettings;

    ue3::Object* const controller = ue3::runtime::Ready() ? player::Controller() : nullptr;
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    auto const mode = head_tracking::GameMode();
    bool const onFoot = mode && *mode == 0 && pawn != nullptr;
    vr::host_view::State const view = vr::host_view::Latest();
    vr::controllers::State const controllers = vr::controllers::Latest();
    vr::controllers::Hand const& left = controllers.left;
    gPadOn = vr::foot_controls::HandWheelOwnsX(mode ? static_cast<int>(*mode) : -1, s.on,
        link.publish != nullptr, view.valid, pawn != nullptr);

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
                     std::format(" [{}]", EnabledChecks(p)) +
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
            Power const& equipped = gRoster[static_cast<std::size_t>(gMachine.equipped)];
            Equip(controller, equipped);
            gLastEvent = std::string("equipped ") + equipped.info->name;
            // How to cast it, on a card where the wheel was: the first coachTimes equips of each power this session.
            pw::Coach const kind =
                pw::CoachFor(equipped.info->aim, gSelfGestures ? pw::SelfGestureFor(equipped.info->label) : pw::SelfGesture::None);
            int& shown = gCoachShown[equipped.info->label];
            if (s.coachTimes > 0 && shown < s.coachTimes && kind != pw::Coach::None) {
                ++shown;
                ++gCoachCount;
                gCoach = {kind, equipped.info->name, gClock, gCastCount};
                log::Info("power wheel: how to cast {} shown ({}; {} of {} this session)", equipped.info->name, pw::Name(kind), shown,
                    s.coachTimes);
            }
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

    // The how-to-cast card goes at the cast, after kCoachS, at the next opening, or off foot.
    if (gCoach.kind != pw::Coach::None) {
        char const* gone = gCastCount > gCoach.casts       ? "cast"
                        : gClock - gCoach.since > pw::kCoachS ? "time"
                        : gMachine.open                    ? "the wheel opened"
                        : !onFoot                          ? "not on foot"
                                                           : nullptr;
        if (gone != nullptr) {
            log::Info("power wheel: how to cast {} gone ({}, after {:.1f} s)", gCoach.name, gone, gClock - gCoach.since);
            gCoach = {};
            if (link.publish != nullptr && !gMachine.open) {
                pw::View closed = gView;
                closed.open = 0;
                closed.coach = 0;
                link.publish(&closed);
            }
        }
    }
    TickLock(link, view, controllers, controller, pawn, onFoot);
    TickConfirmation(link, controllers);
    gView.self = gSelfView;
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
            // Where the light comes from: the omni-tool's display as Shepard's hand shows it, in the left grip's frame
            // (plugin/PowerWheel puts it on the grip it has that frame, so it moves with the hand at once). None while no display
            // is on the arm, or while it's still far from the hand (its first transforms are the animation's arm).
            v.sourceValid = 0;
            if (controllers.active && left.gripValid) {
                std::optional<ue3::Vector> const display = omni_tool::DisplayWorld();
                std::optional<engine::TrackingFrame> const tracking = display ? view_anchor::Current() : std::nullopt;
                if (display && tracking) {
                    XrVector3f const local =
                        pw::ToFrame(left.grip, engine::ToTracking(*tracking, engine::UnrealVector{display->x, display->y, display->z}));
                    if (std::sqrt(local.x * local.x + local.y * local.y + local.z * local.z) < 0.30f) {
                        v.source = local;
                        v.sourceValid = 1;
                        if (gSourceLogged != gMachine.serial) {
                            gSourceLogged = gMachine.serial;
                            log::Info("power wheel: #{} its light comes from the omni-tool's display, ({:.1f}, {:.1f}, {:.1f}) cm from the "
                                      "left grip in its frame", gMachine.serial, local.x * 100.0f, local.y * 100.0f, local.z * 100.0f);
                        }
                    }
                }
            }
            v.emblem = s.emblem ? 1 : 0;
            v.upperCase = s.upperCase ? 1 : 0;
            v.hint = gHintShown <= s.hintOpenings ? 1 : 0;
            link.publish(&v);
        } else if (gCoach.kind != pw::Coach::None) {
            pw::View coached = gView;
            coached.open = 0;
            coached.coach = static_cast<std::uint8_t>(gCoach.kind);
            coached.coachFor = static_cast<float>(gClock - gCoach.since);
            std::snprintf(coached.coachName, sizeof(coached.coachName), "%s", gCoach.name.c_str());
            link.publish(&coached);
        } else {
            pw::View closed = gView;
            closed.open = 0;
            closed.coach = 0;
            link.publish(&closed);
        }
    }
    // The omni-tool gauntlet on the forearm for a power that uses it: while it's aimed or gestured, and 2.5 s after its cast.
    if (onFoot && (gOmniAiming || (gLastCastUsesOmniTool && gClock - gPushCastAt < 2.5))) {
        omni_tool::Request();
    }
}

void AdjustPad(gamepad::Options& options) {
    options.handPowerWheel = gPadOn;  // reserved independently of Throwables and of the open-wheel controls
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
    if (verb == "selfcast") {
        gConsoleSelfCast = true;
        return "power wheel: the equipped self-cast power's gesture on the next tick (tests)";
    }
    if (verb == "unarmed") {
        std::string state;
        words >> state;
        gUnarmedRelease = state != "off";
        return std::format("power wheel: a cast with the weapon away {}", gUnarmedRelease ? "gets the release from the mod" : "is left to the game");
    }
    if (verb == "bolt") {
        std::string state;
        words >> state;
        if (state == "speed") {
            words >> gBoltTestSpeed;
        } else if (!state.empty()) {
            gBoltOn = state != "off";
        }
        return std::format("power wheel: the bolt from the hand is {} (a prototype); {} bolts; last: {}", gBoltOn ? "on" : "off", gBolts,
            gLastBolt);
    }
    if (verb == "vfx") {
        std::string scope;
        words >> scope;
        return VfxMap(scope == "all");
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
            out += std::format("\n  {} ({}): {} ({}), {:.0f}% of its cooldown to go", p.info->name,
                p.label, p.enabled ? "enabled" : "disabled", EnabledChecks(p), p.cooldown * 100.0f);
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
