// SPDX-License-Identifier: MIT
#include "game/Throwables.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/Throwing.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ArmIk.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Holsters.hpp"
#include "game/Player.hpp"
#include "game/SupportGrip.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <numbers>
#include <string>
#include <vector>

namespace mevr::game::throwables {
namespace {

namespace slots = ik::body_slots;
namespace thr = ik::throwing;

constexpr int kLeft = thr::kLeft;
constexpr int kRight = thr::kRight;
// Grip latch and trigger, as the pad mapping's (game/gamepad_mapping).
constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;
constexpr float kTriggerOn = 0.5f;
// The continuous buzzes are asked for again each tick and last this long unasked (the host stops them).
constexpr float kContinuousSeconds = 0.15f;
// A throw the game hasn't launched by then is given up (its animation takes about half a second).
constexpr double kPendingSeconds = 3.0;
// A throw waiting for the weapon to come out (ME1 throws only with a weapon out) waits this long.
constexpr double kDrawWaitSeconds = 2.0;
// A melee's damage is looked for this long after TryMelee.
constexpr double kHitWatchSeconds = 1.2;
// A hand that just let go of a grenade (a throw is a fast forward stroke) can't melee for this long.
constexpr double kNoMeleeAfterRelease = 0.5;
// ...nor the right hand after a draw from a holster (a fast reach).
constexpr double kNoMeleeAfterDraw = 0.5;
// The arc traced for where a throw lands: its step, how long, how far down.
constexpr double kArcStepSeconds = 0.05;
constexpr double kArcSeconds = 4.0;
constexpr double kArcDropMeters = 50.0;
// Real gravity, for where a hand's throw would land (meters per second squared).
constexpr double kGravity = 9.81;

[[maybe_unused]] char const* HandName(int hand) {
    return hand == kLeft ? "left" : "right";
}

[[maybe_unused]] std::string V(ik::Vec3 const& v) {
    return std::format("({:.2f}, {:.2f}, {:.2f})", v.x, v.y, v.z);
}

[[maybe_unused]] std::string U(engine::UnrealVector const& v) {
    return std::format("({:.0f}, {:.0f}, {:.0f})", v.x, v.y, v.z);
}

[[maybe_unused]] double SteadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

[[maybe_unused]] ik::Vec3 FromXr(XrVector3f const& v) {
    return {v.x, v.y, v.z};
}

[[maybe_unused]] engine::UnrealVector Sub(engine::UnrealVector a, engine::UnrealVector b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[maybe_unused]] double Len(engine::UnrealVector v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

bool gStarted = false;
bool gEnabled = false;
bool gActive = false;          // this tick's conditions held (on foot, view, controllers, the player)
std::string gWhy = "not started";
std::string gLoggedWhy;

thr::GrenadeMachine gMachine;
std::array<thr::SwingDetector, 2> gSwing;
thr::SampleClock gClock;
double gTime = 0.0;            // the newest controller sample's time
slots::Anchor gAnchor;
ik::BodyYaw gOwnYaw;
bool gOwnYawActive = false;
double gYaw = 0.0;
bool gYawValid = false;
slots::Frame gFrame;
bool gFrameValid = false;
ik::Vec3 gCenter;
double gScale = 1.0;
std::array<double, 2> gDistance{-1.0, -1.0};
bool gNearRight = false;       // the right hand near the grenade slot: a new right squeeze isn't RB

// Squeezes.
std::array<bool, 2> gClaimed{};      // this tick's new squeeze takes a grenade
std::array<bool, 2> gClaimQueue{};   // ...until the machine has seen it (it runs on new samples only)
std::array<bool, 2> gOwnGrip{};
std::array<bool, 2> gHaveCount{};
std::array<std::uint32_t, 2> gLastCount{};
bool gKeepGunOut = false;            // the right hand threw or put back a grenade: until its next squeeze
std::array<double, 2> gSinceRelease{1e9, 1e9};

// Haptics.
std::array<float, 2> gContinuous{};
bool gToldNoHaptics = false;
bool gToldNoContinuous = false;
bool gToldNoButtons = false;
bool gButtonFlags = false;
bool gToldOldGrip = false;

// A throw the game is about to make: what we computed at the release (world, units).
struct Pending {
    bool active = false;
    bool waitingForDraw = false;
    bool called = false;          // UseGrenade called
    int hand = -1;
    double age = 0.0;
    double handSpeed = 0.0;       // m/s, before the gain and cap
    engine::UnrealVector origin;  // the hand
    engine::UnrealVector velocity;  // units/s, after the gain
    engine::UnrealVector landing;
    bool landingHit = false;
    std::uintptr_t landingActor = 0;  // compared and checked before use
    std::string landingName;
    bool arc = false;
    bool fromHand = true;
    double unitsPerMeter = engine::kUnitsPerMeter;
};
Pending gPending;
bool gInLaunch = false;           // our own LaunchGrenade call: its handler is ours
bool gLoggedGameThrow = false;

// The last throw and launch, for the status.
struct LastThrow {
    bool valid = false;
    int hand = -1;
    double handSpeed = 0.0;
    ik::Vec3 direction;           // tracking space, unit
    double peakAgo = 0.0;
    std::string launch = "none";  // what happened at the launch
    double delay = -1.0;          // release to launch (s)
    engine::UnrealVector landing;
};
LastThrow gLastThrow;

// A melee's target, watched for damage.
struct HitWatch {
    bool active = false;
    int hand = -1;
    double age = 0.0;
    std::uintptr_t target = 0;
    double health = 0.0;
    std::string name;
};
HitWatch gHitWatch;
double gMeleeReadyAt = 0.0;
std::string gLastMelee = "none";

// Heartbeat counters.
unsigned gGrabs = 0;
unsigned gThrows = 0;
unsigned gCancels = 0;
unsigned gRefusals = 0;
unsigned gLaunches = 0;
unsigned gAimed = 0;
unsigned gStrikes = 0;
unsigned gHits = 0;
unsigned gStrokeRefusals = 0;
unsigned gMeleeRefusals = 0;
int gGrenadesLeft = -1;

void SetWhy(std::string why) {
    gWhy = std::move(why);
    if (gWhy != gLoggedWhy) {
        gLoggedWhy = gWhy;
        log::Info("throwables: {}", gWhy);
    }
}

[[maybe_unused]] void Pulse(Link const& link, int hand, float amplitude, float frequency, float seconds) {
    if (!(amplitude > 0.0f) || !(seconds > 0.0f)) {
        return;
    }
    if (link.hapticHz != nullptr) {
        link.hapticHz(hand, seconds, amplitude, frequency);
    } else if (link.haptic != nullptr) {
        link.haptic(hand, seconds, amplitude);
    } else if (!gToldNoHaptics) {
        gToldNoHaptics = true;
        log::Warn("throwables: the host can't vibrate the controllers (services older than v9): no haptics");
    }
}

[[maybe_unused]] void Pulse(Link const& link, int hand, thr::Buzz const& buzz) {
    Pulse(link, hand, buzz.amplitude, buzz.frequency, buzz.seconds);
}

[[maybe_unused]] void Pulse(Link const& link, int hand, slots::Buzz const& buzz) {
    Pulse(link, hand, buzz.amplitude, buzz.frequency, buzz.seconds);
}

void Continuous(Link const& link, int hand, float amplitude, float frequency) {
    amplitude = amplitude > 0.0f ? amplitude : 0.0f;
    if (link.hapticContinuous == nullptr) {
        if (amplitude > 0.0f && !gToldNoContinuous) {
            gToldNoContinuous = true;
            log::Warn("throwables: the host (services older than v10) has no continuous vibration: no buzz in the grenade "
                      "slot or while holding a grenade");
        }
        gContinuous[hand] = 0.0f;
        return;
    }
    if (amplitude <= 0.0f && gContinuous[hand] <= 0.0f) {
        return;  // already quiet
    }
    link.hapticContinuous(hand, amplitude, frequency, amplitude > 0.0f ? kContinuousSeconds : 0.0f);
    gContinuous[hand] = amplitude;
}

void Quiet(Link const& link) {
    Continuous(link, kLeft, 0.0f, 0.0f);
    Continuous(link, kRight, 0.0f, 0.0f);
}

#if defined(MEVR_GAME_LE1)

// Our reflection layer (research/34): the fields research/20 read (BioGrenadeProjectileFrisbee
// m_vIntendedTargetLocation +0x354, m_oHeatSeekTarget +0x36C, m_oIntendedTarget +0x374; BioGrenade.m_clsProjectile
// +0x3C8; WorldInfo.PawnList +0x69C; Pawn.NextPawn +0x408), found by name; engine/ReflectCheck compares each.
ue3::ClassRef gBioPlayerInput{"SFXGame.BioPlayerInput"};
ue3::ClassRef gGameModeDefault{"SFXGame.SFXGameModeDefault"};
ue3::ClassRef gBioGrenade{"SFXGame.BioGrenade"};
ue3::ClassRef gFrisbee{"SFXGame.BioGrenadeProjectileFrisbee"};
ue3::ClassRef gBallistic{"SFXGame.BioGrenadeProjectileBallistic"};
ue3::FieldRef gPlayerInput{"Engine.PlayerController", "PlayerInput"};
ue3::FieldRef gStorming{"SFXGame.BioPlayerController", "bStorming"};
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes"};
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gEquipment{"SFXGame.BioPawnBehavior", "m_oEquipment"};
ue3::FieldRef gQuickSlotCurrent{"SFXGame.BioEquipment", "m_eQuickSlotCurrent"};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gOwner{"Engine.Actor", "Owner"};
ue3::FieldRef gInstigator{"Engine.Actor", "Instigator"};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList"};
ue3::FieldRef gWorldGravityZ{"Engine.WorldInfo", "WorldGravityZ"};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn"};
ue3::FieldRef gProjectileSpeed{"Engine.Projectile", "Speed"};
ue3::FieldRef gClsProjectile{"SFXGame.BioGrenade", "m_clsProjectile"};
ue3::FieldRef gIntendedTargetLocation{"SFXGame.BioGrenadeProjectileFrisbee", "m_vIntendedTargetLocation"};
ue3::FieldRef gIntendedTarget{"SFXGame.BioGrenadeProjectileFrisbee", "m_oIntendedTarget"};
ue3::FieldRef gHeatSeekTarget{"SFXGame.BioGrenadeProjectileFrisbee", "m_oHeatSeekTarget"};
ue3::FieldRef gStartLocation{"SFXGame.BioGrenadeProjectileFrisbee", "m_vStartLocation"};

ue3::FunctionRef gUseGrenadeFn{"SFXGame.SFXGameModeDefault.UseGrenade"};
ue3::FunctionRef gTryMeleeFn{"SFXGame.SFXGameModeDefault.TryMelee"};
ue3::FunctionRef gIsInCombatActionStateFn{"SFXGame.BioPawn.IsInCombatActionState"};
ue3::FunctionRef gIsSwitchingWeaponsFn{"SFXGame.BioPawn.IsSwitchingWeapons"};
ue3::FunctionRef gIsThrowingGrenadeFn{"SFXGame.BioPawn.IsThrowingGrenade"};
ue3::FunctionRef gIsCombatEnabledFn{"SFXGame.BioPlayerInput.IsCombatEnabled"};
ue3::FunctionRef gSetVelocityFn{"SFXGame.BioGrenadeProjectile.SetVelocity"};
ue3::FunctionRef gLaunchAtPointFn{"SFXGame.BioGrenade.LaunchGrenadeAtPoint"};
ue3::FunctionRef gTraceFn{"Engine.Actor.Trace"};
ue3::FunctionRef gLaunchAtActorFn{"SFXGame.BioGrenade.LaunchGrenadeAtActor"};
ue3::FunctionRef gLaunchGrenadeFn{"SFXGame.BioGrenade.LaunchGrenade"};
ue3::FunctionRef gIsUsingPowerFn{"SFXGame.BioPawn.IsUsingPower"};
ue3::FunctionRef gGetGrenadesFn{"SFXGame.BioPawnBehavior.GetGrenades"};
ue3::FunctionRef gIsHostileFn{"SFXGame.BioPawn.IsHostile"};
ue3::FunctionRef gIsDeadFn{"SFXGame.BioPawn.IsDead"};
ue3::FunctionRef gGetPercentHealthFn{"SFXGame.BioEpicPawnBehavior.GetPercentHealth"};
ue3::FunctionRef gSetLocationFn{"Engine.Actor.SetLocation"};
ue3::FunctionRef gDestroyFn{"Engine.Actor.Destroy"};
// The launch post-handlers read their own blocks (the engine's): each function's parameters.
ue3::FieldRef gAtPointTarget{gLaunchAtPointFn, "vTarget"};
ue3::FieldRef gAtPointReturn{gLaunchAtPointFn, "ReturnValue"};
ue3::FieldRef gAtActorTarget{gLaunchAtActorFn, "oTarget"};
ue3::FieldRef gAtActorReturn{gLaunchAtActorFn, "ReturnValue"};
ue3::FieldRef gLaunchVelocity{gLaunchGrenadeFn, "vVelocity"};
ue3::FieldRef gLaunchReturn{gLaunchGrenadeFn, "ReturnValue"};

ue3::Function* gUseGrenade = nullptr;
ue3::Function* gTryMelee = nullptr;
ue3::Function* gGetGrenades = nullptr;
ue3::Function* gIsInCombatActionState = nullptr;
ue3::Function* gIsSwitchingWeapons = nullptr;
ue3::Function* gIsThrowingGrenade = nullptr;
ue3::Function* gIsUsingPower = nullptr;
ue3::Function* gIsHostile = nullptr;
ue3::Function* gIsDead = nullptr;
ue3::Function* gGetPercentHealth = nullptr;
ue3::Function* gIsCombatEnabled = nullptr;
ue3::Function* gTrace = nullptr;
ue3::Function* gSetLocation = nullptr;
ue3::Function* gDestroy = nullptr;
ue3::Function* gSetVelocity = nullptr;
ue3::Function* gLaunchGrenade = nullptr;
ue3::Function* gLaunchAtPoint = nullptr;
ue3::Function* gLaunchAtActor = nullptr;

ue3::Vector ToF(engine::UnrealVector const& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

engine::UnrealVector FromF(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}

std::string FullName(ue3::Object const* object) {
    return ue3::runtime::FullName(object);
}

ue3::Object* ObjectOf(ue3::FieldRef& field, ue3::Object const* object) {
    return object != nullptr ? field.Read<ue3::Object*>(object).value_or(nullptr) : nullptr;
}

engine::UnrealVector LocationOf(ue3::Object const* actor) {
    return FromF(player::Location(actor).value_or(ue3::Vector{}));
}

// Through ProcessEvent with the function's own parameter block (never a generated wrapper).
bool CallBool(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

void CallVoid(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
}

struct Game {
    ue3::Object* controller = nullptr;  // a BioPlayerController
    ue3::Object* pawn = nullptr;        // a BioPawn
    ue3::Object* behavior = nullptr;    // its BioPawnBehavior
    ue3::Object* mode = nullptr;        // the SFXGameModeDefault
};

std::optional<Game> ReadGame(std::string& why) {
    if (!ue3::runtime::Ready()) {
        why = "our reflection layer is off (see the 'reflect:' log lines)";
        return std::nullopt;
    }
    ue3::Object* const pc = player::Controller();
    if (pc == nullptr || !player::IsBioPlayerController(pc)) {
        why = "no player controller";
        return std::nullopt;
    }
    Game game;
    game.controller = pc;
    game.pawn = player::BioPawn(pc);
    if (game.pawn == nullptr) {
        why = "no player pawn (vehicle or cutscene?)";
        return std::nullopt;
    }
    game.behavior = ObjectOf(gBehavior, game.pawn);
    if (game.behavior == nullptr) {
        why = "the player pawn has no behavior";
        return std::nullopt;
    }
    if (ue3::Object* const manager = ObjectOf(gGameModeManager, pc)) {
        if (ue3::TArray<ue3::Object*> const* const modes = gGameModes.In<ue3::TArray<ue3::Object*>>(manager)) {
            for (int i = 0; i < modes->Count(); ++i) {
                ue3::Object* const mode = modes->data[i];
                if (mode != nullptr && ue3::IsA(mode, gGameModeDefault)) {
                    game.mode = mode;
                    break;
                }
            }
        }
    }
    if (game.mode == nullptr) {
        why = "no SFXGameModeDefault in the player controller's game modes";
        return std::nullopt;
    }
    return game;
}

bool Storming(Game const& game) {
    return gStorming.ReadBool(game.controller).value_or(false);
}

bool CombatEnabled(Game const& game) {
    ue3::Object* const input = ObjectOf(gPlayerInput, game.controller);
    return input != nullptr && ue3::IsA(input, gBioPlayerInput) && CallBool(input, gIsCombatEnabled);
}

bool Drawn(Game const& game) {
    return CallBool(game.pawn, gIsInCombatActionState);
}

bool UsingPower(Game const& game) {
    if (gIsUsingPower == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), gIsUsingPower);
    parms.SetBool("bIgnoreWeaponPowers", false);
    ue3::runtime::Call(game.pawn, gIsUsingPower, parms);
    return parms.ReturnBool().value_or(false);
}

int GrenadesLeft(Game const& game) {
    if (gGetGrenades == nullptr) {
        return -1;
    }
    ue3::Params parms(ue3::runtime::Get(), gGetGrenades);
    ue3::runtime::Call(game.behavior, gGetGrenades, parms);
    return parms.Return<std::int32_t>().value_or(-1);
}

double Health(ue3::Object* pawn) {
    ue3::Object* const behavior = ObjectOf(gBehavior, pawn);
    if (pawn == nullptr || behavior == nullptr || gGetPercentHealth == nullptr) {
        return -1.0;
    }
    ue3::Params parms(ue3::runtime::Get(), gGetPercentHealth);
    ue3::runtime::Call(behavior, gGetPercentHealth, parms);
    return parms.Return<float>().value_or(-1.0f);
}

// Why the game would refuse a grenade now (empty: it wouldn't).
std::string GrenadeRefusal(Game const& game) {
    if (!CombatEnabled(game)) {
        return "combat is disabled here (a hub or a scripted scene)";
    }
    if (Storming(game)) {
        return "sprinting";
    }
    gGrenadesLeft = GrenadesLeft(game);
    if (gGrenadesLeft == 0) {
        return "no grenades left";
    }
    if (CallBool(game.pawn, gIsThrowingGrenade)) {
        return "a grenade throw is still running";
    }
    if (gPending.active) {
        return "the last throw hasn't left the hand yet";
    }
    return {};
}

// Actor.Trace (native 277) on the player's pawn, which it ignores: the actor hit, and the hit point if
// it came back. Directly through ProcessEvent with the function's own block, never a generated wrapper.
ue3::Object* TraceOnce(Game const& game, engine::UnrealVector const& start, engine::UnrealVector const& end, engine::UnrealVector* hit) {
    ue3::Params parms(ue3::runtime::Get(), gTrace);
    parms.Set("TraceEnd", ToF(end));
    parms.Set("TraceStart", ToF(start));
    parms.SetBool("bTraceActors", true);
    ue3::runtime::Call(game.pawn, gTrace, parms);
    if (hit != nullptr) {
        *hit = FromF(parms.Get<ue3::Vector>("HitLocation").value_or(ue3::Vector{}));
    }
    return parms.Return<ue3::Object*>().value_or(nullptr);
}

bool gLoggedTracePath = false;

// A line trace from `start` to `end`: the hit point and actor. A native's struct out parameter called
// through ProcessEvent may not come back (research/08 H17): then the hit point is found by bisecting
// the segment with the hit/no-hit answer alone (to 1/64 of it).
bool Trace(Game const& game, engine::UnrealVector const& start, engine::UnrealVector const& end, engine::UnrealVector& hit,
    ue3::Object*& actor) {
    if (gTrace == nullptr) {
        return false;
    }
    actor = TraceOnce(game, start, end, &hit);
    if (actor == nullptr) {
        return false;
    }
    double const length = Len(Sub(end, start));
    bool const plausible = (hit.x != 0.0 || hit.y != 0.0 || hit.z != 0.0) && Len(Sub(hit, start)) <= length + 1.0 &&
        Len(Sub(hit, end)) <= length + 1.0;
    if (!plausible) {
        double lo = 0.0;
        double hi = 1.0;
        auto const at = [&](double f) {
            return engine::UnrealVector{start.x + (end.x - start.x) * f, start.y + (end.y - start.y) * f, start.z + (end.z - start.z) * f};
        };
        for (int i = 0; i < 6; ++i) {
            double const mid = 0.5 * (lo + hi);
            if (ue3::Object* const a = TraceOnce(game, start, at(mid), nullptr)) {
                hi = mid;
                actor = a;
            } else {
                lo = mid;
            }
        }
        hit = at(hi);
    }
    if (!gLoggedTracePath) {
        gLoggedTracePath = true;
        log::Info("throwables: Actor.Trace through ProcessEvent: the hit point {}", plausible
            ? "came back" : "didn't come back (a native's struct out parameter): found by bisecting with the hit/no-hit answer");
    }
    return true;
}

bool SetLocation(ue3::Object* actor, engine::UnrealVector const& where) {
    if (actor == nullptr || gSetLocation == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), gSetLocation);
    parms.Set("NewLocation", ToF(where));
    ue3::runtime::Call(actor, gSetLocation, parms);
    return parms.ReturnBool().value_or(false);
}

void SetVelocity(ue3::Object* projectile, engine::UnrealVector const& velocity) {
    ue3::Params parms(ue3::runtime::Get(), gSetVelocity);
    parms.Set("vVelocity", ToF(velocity));
    ue3::runtime::Call(projectile, gSetVelocity, parms);  // native: the class's own SetVelocity
}

// The disc: from the hand, toward where the throw lands, colliding there (no homing).
std::string AimDisc(ue3::Object* projectile) {
    if (!ue3::IsA(projectile, gFrisbee)) {
        return std::format("a {} (not the disc): left as the game aimed it", FullName(projectile));
    }
    ue3::Object* const disc = projectile;
    engine::UnrealVector const spawned = LocationOf(disc);
    std::string moved;
    if (gPending.fromHand) {
        bool const ok = SetLocation(disc, gPending.origin);
        moved = ok ? std::format(", moved {:.0f} units from where the game spawned it to the hand", Len(Sub(gPending.origin, spawned)))
                   : std::string(", couldn't be moved to the hand (blocked): from the game's spot");
    }
    engine::UnrealVector const from = LocationOf(disc);
    SetVelocity(disc, Sub(gPending.landing, from));  // the disc keeps its own speed along it
    if (ue3::Vector* const intended = gIntendedTargetLocation.In<ue3::Vector>(disc)) {
        *intended = ToF(gPending.landing);
        write_journal::Wrote("Throw.disc target location", intended);
    }
    ue3::Object* target = nullptr;
    if (gPending.landingActor != 0) {
        auto* const candidate = reinterpret_cast<ue3::Object*>(gPending.landingActor);
        target = ue3::runtime::IsAlive(candidate) ? candidate : nullptr;
    }
    if (ue3::Object** const intendedTarget = gIntendedTarget.In<ue3::Object*>(disc)) {
        *intendedTarget = target;
        write_journal::Wrote("Throw.disc target", intendedTarget);
    }
    if (ue3::Object** const heatSeek = gHeatSeekTarget.In<ue3::Object*>(disc)) {
        *heatSeek = nullptr;
        write_journal::Wrote("Throw.disc heat seek target", heatSeek);
    }
    if (ue3::Vector* const start = gStartLocation.In<ue3::Vector>(disc)) {
        *start = player::Location(disc).value_or(*start);
        write_journal::Wrote("Throw.disc start location", start);
    }
    double const meters = Len(Sub(gPending.landing, from)) / std::max(1.0, gPending.unitsPerMeter);
    return std::format("the disc re-aimed{} to {} ({:.1f} m, {}, speed {:.0f} units/s)", moved, U(gPending.landing), meters,
        target != nullptr ? FullName(target) : std::string(gPending.landingHit ? "a surface" : "nothing hit: open air"),
        gProjectileSpeed.Read<float>(disc).value_or(0.0f));
}

// The ballistic grenade with the hand's velocity, in place of the disc. False: it didn't spawn.
bool LaunchArc(ue3::Object* grenade, ue3::Object*& projectile, std::string& what) {
    if (gLaunchGrenade == nullptr || gDestroy == nullptr) {
        what = "the arc needs BioGrenade.LaunchGrenade and Actor.Destroy (not found)";
        return false;
    }
    ue3::Class** const projectileClass = gClsProjectile.In<ue3::Class*>(grenade);
    ue3::Class* const ballistic = gBallistic.Get();
    if (projectileClass == nullptr || ballistic == nullptr) {
        what = "BioGrenade.m_clsProjectile or BioGrenadeProjectileBallistic not found";
        return false;
    }
    ue3::Class* const saved = *projectileClass;
    *projectileClass = ballistic;
    write_journal::Wrote("Throw.projectile class", projectileClass);
    ue3::Params parms(ue3::runtime::Get(), gLaunchGrenade);
    parms.Set("vVelocity", ToF(gPending.velocity));
    gInLaunch = true;
    ue3::runtime::Call(grenade, gLaunchGrenade, parms);  // the game's own spawn: MakeProjectile, then SetVelocity
    gInLaunch = false;
    *projectileClass = saved;
    write_journal::Wrote("Throw.projectile class back", projectileClass);
    ue3::Object* const ball = parms.Return<ue3::Object*>().value_or(nullptr);
    if (ball == nullptr) {
        what = "the ballistic grenade didn't spawn";
        return false;
    }
    std::string moved;
    if (gPending.fromHand) {
        moved = SetLocation(ball, gPending.origin) ? ", from the hand" : ", from the game's spot (the hand's was blocked)";
        SetVelocity(ball, gPending.velocity);
    }
    ue3::Params destroy(ue3::runtime::Get(), gDestroy);
    ue3::runtime::Call(projectile, gDestroy, destroy);
    projectile = ball;
    what = std::format("the ballistic grenade launched{} at {} ({:.0f} units/s; the disc removed{})", moved, U(gPending.velocity),
        Len(gPending.velocity), destroy.ReturnBool().value_or(false) ? "" : ": NOT destroyed");
    return true;
}

// BioGrenade.LaunchGrenadeAtPoint / LaunchGrenadeAtActor / LaunchGrenade, after the game's own: the
// projectile it returned is ours to re-aim if the player's physical throw is waiting for it.
void OnLaunch(ue3::Object* context, ue3::Function* function, void* parms, void* /*result*/) {
    if (gInLaunch || context == nullptr || !ue3::IsA(context, gBioGrenade)) {
        return;
    }
    ue3::Object* const pawn = player::Pawn(player::Controller());
    ue3::Object* const grenade = context;
    if (pawn == nullptr || (ObjectOf(gOwner, grenade) != pawn && ObjectOf(gInstigator, grenade) != pawn)) {
        return;  // a squadmate's or an enemy's
    }
    // The return value's slot in the engine's block (unaligned in some: copied, never referenced).
    void* slot = nullptr;
    std::string how;
    if (function == gLaunchAtPoint) {
        slot = gAtPointReturn.Address(parms);
        how = std::format("LaunchGrenadeAtPoint{}", U(FromF(gAtPointTarget.Read<ue3::Vector>(parms).value_or(ue3::Vector{}))));
    } else if (function == gLaunchAtActor) {
        slot = gAtActorReturn.Address(parms);
        ue3::Object* target = nullptr;
        if (void const* const at = gAtActorTarget.Address(parms)) {
            std::memcpy(&target, at, sizeof target);
        }
        how = std::format("LaunchGrenadeAtActor({})", target != nullptr ? FullName(target) : std::string("None"));
    } else if (function == gLaunchGrenade) {
        slot = gLaunchReturn.Address(parms);
        how = std::format("LaunchGrenade{}", U(FromF(gLaunchVelocity.Read<ue3::Vector>(parms).value_or(ue3::Vector{}))));
    } else {
        return;
    }
    if (slot == nullptr) {
        return;  // the parameter didn't resolve (logged once)
    }
    ue3::Object* projectile = nullptr;
    std::memcpy(&projectile, slot, sizeof projectile);
    ++gLaunches;
    if (!gPending.active || !gPending.called) {
        if (!gLoggedGameThrow) {
            gLoggedGameThrow = true;
            log::Info("throwables: a grenade thrown the game's way (the X button?): {} -> {}, left as the game aimed it (logged once)",
                how, projectile != nullptr ? FullName(projectile) : std::string("no projectile"));
        }
        return;
    }
    double const delay = gPending.age;
    std::string what;
    if (projectile == nullptr) {
        what = "the game spawned no projectile";
    } else {
        bool arced = false;
        if (gPending.arc) {
            arced = LaunchArc(grenade, projectile, what);
            if (arced) {
                std::memcpy(slot, &projectile, sizeof projectile);  // the game takes the ballistic grenade for its own
                write_journal::Wrote("Throw.launch (PE return)", slot, sizeof projectile);
            } else {
                what += "; the disc instead: ";
            }
        }
        if (!arced) {
            what += AimDisc(projectile);
        }
        ++gAimed;
    }
    log::Info("throwables: grenade launched {:.2f} s after the {} hand's release ({} -> {}): {}", delay, HandName(gPending.hand), how,
        projectile != nullptr ? FullName(projectile) : std::string("none"), what);
    gLastThrow.launch = what;
    gLastThrow.delay = delay;
    gPending = {};
}

// The enemy a melee would hit: the nearest living hostile in reach and in front of the body.
struct Enemy {
    ue3::Object* pawn = nullptr;  // a BioPawn
    double meters = 1e9;
    double degrees = 0.0;
};

Enemy FindEnemy(Game const& game, engine::TrackingFrame const& frame, ik::Vec3 headTracking, double reachMeters, double coneDegrees,
    Enemy& nearestHostile) {
    Enemy best;
    ue3::Object* const world = ObjectOf(gWorldInfo, game.pawn);
    if (world == nullptr || gIsHostile == nullptr) {
        return best;
    }
    // The body's front in the world (horizontal).
    ik::Vec3 const fwd = slots::YawForward(gFrame.yaw);
    XrVector3f const a{static_cast<float>(headTracking.x), static_cast<float>(headTracking.y), static_cast<float>(headTracking.z)};
    XrVector3f const b{static_cast<float>(headTracking.x + fwd.x), static_cast<float>(headTracking.y + fwd.y),
        static_cast<float>(headTracking.z + fwd.z)};
    engine::UnrealVector front = Sub(engine::ToWorld(frame, b), engine::ToWorld(frame, a));
    front.z = 0.0;
    double const frontLength = std::max(Len(front), 1e-6);
    double const upm = frame.unitsPerMeter > 0.0 ? frame.unitsPerMeter : engine::kUnitsPerMeter;
    engine::UnrealVector const me = LocationOf(game.pawn);
    int guard = 0;
    for (ue3::Object* p = ObjectOf(gPawnList, world); p != nullptr && guard < 512; p = ObjectOf(gNextPawn, p), ++guard) {
        if (p == game.pawn || !player::IsBioPawn(p)) {
            continue;
        }
        ue3::Object* const other = p;
        if (CallBool(other, gIsDead)) {
            continue;
        }
        ue3::Params hostile(ue3::runtime::Get(), gIsHostile);
        hostile.Set("Other", other);
        ue3::runtime::Call(game.pawn, gIsHostile, hostile);
        if (!hostile.ReturnBool().value_or(false)) {
            continue;
        }
        engine::UnrealVector const d = Sub(LocationOf(other), me);
        double const flat = std::sqrt(d.x * d.x + d.y * d.y);
        double const meters = flat / upm;
        double const cosine = flat > 1e-6 ? (d.x * front.x + d.y * front.y) / (flat * frontLength) : 1.0;
        double const degrees = std::acos(std::clamp(cosine, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        if (meters < nearestHostile.meters) {
            nearestHostile = {other, meters, degrees};
        }
        if (meters <= reachMeters && degrees <= coneDegrees && std::abs(d.z) / upm < 2.0 && meters < best.meters) {
            best = {other, meters, degrees};
        }
    }
    return best;
}

// The quick slot in hand (BioEquipment.m_eQuickSlotCurrent; 0 the pistol), if there's equipment.
std::optional<std::uint8_t> CurrentQuickSlot(Game const& game) {
    ue3::Object* const equipment = ObjectOf(gEquipment, game.behavior);
    return equipment != nullptr ? gQuickSlotCurrent.Read<std::uint8_t>(equipment) : std::nullopt;
}

void Strike(Link const& link, Game const& game, int hand, thr::SwingStep const& stroke,
    hand_calibration::Throwables const& settings, engine::TrackingFrame const& frame, ik::Vec3 headTracking) {
    std::string const what = std::format("the {} hand's {:.1f} m/s stroke ({:.2f} m, {:.2f} toward the front)", HandName(hand), stroke.speed,
        stroke.travel, stroke.forward);
    auto const refuse = [&](std::string const& why) {
        ++gMeleeRefusals;
        gLastMelee = std::format("refused: {}", why);
        log::Info("throwables: melee: {}: no strike ({})", what, why);
    };
    if (!settings.melee) {
        return;
    }
    if (!CombatEnabled(game)) {
        return refuse("combat is disabled here");
    }
    if (!Drawn(game)) {
        return refuse("the weapon is away: ME1's melee would only draw it");
    }
    if (Storming(game)) {
        return refuse("sprinting");
    }
    if (CallBool(game.pawn, gIsSwitchingWeapons) || UsingPower(game) || CallBool(game.pawn, gIsThrowingGrenade)) {
        return refuse("switching weapons, using a power or throwing a grenade");
    }
    if (gTime < gMeleeReadyAt) {
        return refuse(std::format("cooldown, {:.2f} s left", gMeleeReadyAt - gTime));
    }
    Enemy nearest;
    Enemy const enemy = FindEnemy(game, frame, headTracking, settings.meleeReach, settings.meleeConeDeg, nearest);
    if (settings.meleeNeedsEnemy && enemy.pawn == nullptr) {
        return refuse(nearest.pawn != nullptr
            ? std::format("no enemy within {:.1f} m and {:.0f} degrees; the nearest hostile is {} at {:.1f} m, {:.0f} degrees",
                  settings.meleeReach, settings.meleeConeDeg, FullName(nearest.pawn), nearest.meters, nearest.degrees)
            : std::string("no hostile around"));
    }
    CallVoid(game.mode, gTryMelee);  // the B button's exec: directly, never a generated wrapper
    gMeleeReadyAt = gTime + settings.meleeCooldown;
    ++gStrikes;
    Pulse(link, hand, thr::StrikeTick(settings.hapticAmplitude));
    std::string const target = enemy.pawn != nullptr
        ? std::format("{} at {:.1f} m, {:.0f} degrees, health {:.0f}%", FullName(enemy.pawn), enemy.meters, enemy.degrees,
              100.0 * Health(enemy.pawn))
        : std::string("no enemy in reach (meleeNeedsEnemy = 0)");
    gLastMelee = std::format("{} -> TryMelee; {}", what, target);
    std::optional<std::uint8_t> const slot = CurrentQuickSlot(game);
    log::Info("throwables: melee: {} -> SFXGameModeDefault.TryMelee ({}); target {}", what,
        slot && *slot == 0 ? "pistol whip" : "rifle butt", target);
    if (enemy.pawn != nullptr) {
        gHitWatch = {true, hand, 0.0, reinterpret_cast<std::uintptr_t>(enemy.pawn), Health(enemy.pawn), FullName(enemy.pawn)};
    }
}

void WatchHit(Link const& link, double deltaSeconds, double strength) {
    if (!gHitWatch.active) {
        return;
    }
    gHitWatch.age += deltaSeconds;
    auto* const pawn = reinterpret_cast<ue3::Object*>(gHitWatch.target);
    bool const alive = ue3::runtime::IsAlive(pawn) && player::IsBioPawn(pawn);
    double const health = alive ? Health(pawn) : -1.0;
    bool const dead = alive && CallBool(pawn, gIsDead);
    if (alive && (dead || (health >= 0.0 && health < gHitWatch.health - 1e-4))) {
        ++gHits;
        Pulse(link, gHitWatch.hand, thr::HitThump(strength));
        log::Info("throwables: melee: hit {} after {:.2f} s (health {:.0f}% -> {:.0f}%{})", gHitWatch.name, gHitWatch.age,
            100.0 * gHitWatch.health, 100.0 * std::max(health, 0.0), dead ? ", dead" : "");
        gLastMelee += std::format("; hit ({:.0f}% -> {:.0f}%)", 100.0 * gHitWatch.health, 100.0 * std::max(health, 0.0));
        gHitWatch = {};
    } else if (!alive || gHitWatch.age > kHitWatchSeconds) {
        log::Info("throwables: melee: no damage seen on {} within {:.1f} s (a miss, out of the game's 60-degree cone, or blocked)",
            gHitWatch.name, kHitWatchSeconds);
        gLastMelee += "; no damage seen";
        gHitWatch = {};
    }
}

void CallUseGrenade(Game const& game) {
    CallVoid(game.mode, gUseGrenade);  // the X button's exec: directly, never a generated wrapper
    gPending.called = true;
    bool const started = CallBool(game.pawn, gIsThrowingGrenade);
    log::Info("throwables: SFXGameModeDefault.UseGrenade called {:.2f} s after the release: {}", gPending.age,
        started ? "Shepard is throwing" : "Shepard isn't throwing yet (the launch is awaited)");
}

void FollowPending(Game const* game, double deltaSeconds) {
    if (!gPending.active) {
        return;
    }
    gPending.age += deltaSeconds;
    if (gPending.waitingForDraw && game != nullptr) {
        if (Drawn(*game) && !CallBool(game->pawn, gIsSwitchingWeapons)) {
            gPending.waitingForDraw = false;
            CallUseGrenade(*game);
        } else if (gPending.age > kDrawWaitSeconds) {
            log::Warn("throwables: the weapon didn't come out within {:.1f} s: the {} hand's throw is dropped (ME1 throws only with "
                      "a weapon out)", kDrawWaitSeconds, HandName(gPending.hand));
            gLastThrow.launch = "dropped: the weapon didn't come out";
            gPending = {};
            return;
        }
    }
    if (gPending.age > kPendingSeconds) {
        log::Warn("throwables: the game didn't launch the {} hand's grenade within {:.0f} s (no grenades, a cooldown, the throw "
                  "animation interrupted, or the launch went another way)", HandName(gPending.hand), kPendingSeconds);
        gLastThrow.launch = "never launched";
        gPending = {};
    }
}

// The release: where it goes, and the game's throw.
void Throw(Link const& link, Game const& game, int hand, thr::Release const& release, hand_calibration::Throwables const& settings,
    vr::controllers::Hand const& controller) {
    auto const frame = view_anchor::Current();
    if (!frame) {
        log::Info("throwables: the {} hand let go at {:.1f} m/s but there's no view anchor: the grenade goes back", HandName(hand),
            release.speed);
        Pulse(link, hand, thr::StowClick(settings.hapticAmplitude));
        return;
    }
    double const upm = frame->unitsPerMeter > 0.0 ? frame->unitsPerMeter : engine::kUnitsPerMeter;
    ik::Vec3 v = release.velocity;
    double const raw = release.speed;
    if (raw > settings.throwSpeedMax && raw > 0.0) {
        v = v * (settings.throwSpeedMax / raw);
    }
    XrVector3f const p = controller.grip.position;
    XrVector3f const tip{static_cast<float>(p.x + v.x), static_cast<float>(p.y + v.y), static_cast<float>(p.z + v.z)};
    engine::UnrealVector const origin = engine::ToWorld(*frame, p);
    engine::UnrealVector velocity = Sub(engine::ToWorld(*frame, tip), origin);
    velocity = {velocity.x * settings.throwGain, velocity.y * settings.throwGain, velocity.z * settings.throwGain};
    // Gravity: real for the disc's landing (where a throw would land); the world's for the arc (it flies in it).
    double gravityZ = -kGravity * upm;
    if (settings.arc) {
        ue3::Object* const world = ObjectOf(gWorldInfo, game.pawn);
        std::optional<float> const worldGravityZ = world != nullptr ? gWorldGravityZ.Read<float>(world) : std::nullopt;
        if (worldGravityZ && *worldGravityZ < 0.0f) {
            gravityZ = *worldGravityZ;
        }
    }
    std::vector<ik::Vec3> const arc = thr::ArcPoints({origin.x, origin.y, origin.z}, {velocity.x, velocity.y, velocity.z}, gravityZ,
        kArcStepSeconds, kArcSeconds, kArcDropMeters * upm);
    Pending pending;
    pending.active = true;
    pending.hand = hand;
    pending.handSpeed = raw;
    pending.origin = origin;
    pending.velocity = velocity;
    pending.arc = settings.arc;
    pending.fromHand = settings.fromHand;
    pending.unitsPerMeter = upm;
    pending.landing = arc.empty() ? origin : engine::UnrealVector{arc.back().x, arc.back().y, arc.back().z};
    for (std::size_t i = 1; i < arc.size(); ++i) {
        engine::UnrealVector hit;
        ue3::Object* actor = nullptr;
        if (Trace(game, {arc[i - 1].x, arc[i - 1].y, arc[i - 1].z}, {arc[i].x, arc[i].y, arc[i].z}, hit, actor)) {
            pending.landing = hit;
            pending.landingHit = true;
            pending.landingActor = reinterpret_cast<std::uintptr_t>(actor);
            pending.landingName = FullName(actor);
            break;
        }
    }
    gPending = pending;
    ++gThrows;
    ik::Vec3 const dir = ik::Normalize(release.velocity);
    double const pitch = std::asin(std::clamp(dir.y, -1.0, 1.0)) * 180.0 / std::numbers::pi;
    gLastThrow = {true, hand, raw, dir, release.peakAgo, "awaited", -1.0, pending.landing};
    log::Info("throwables: grenade thrown with the {} hand at {:.1f} m/s{} (fastest {:.0f} ms before the release, {} samples), "
              "direction {} (tracking), {:.0f} degrees {}; world velocity {} units/s (x{:.1f}); {} {:.1f} m away{}", HandName(hand), raw,
        raw > settings.throwSpeedMax ? std::format(", capped at {:.0f}", settings.throwSpeedMax) : std::string(), release.peakAgo * 1000.0,
        release.samples, V(dir), std::abs(pitch), pitch >= 0.0 ? "up" : "down", U(velocity), settings.throwGain,
        pending.landingHit ? std::format("lands on {} at {},", pending.landingName, U(pending.landing))
                           : std::format("hits nothing within {:.0f} s; aimed at {},", kArcSeconds, U(pending.landing)),
        Len(Sub(pending.landing, origin)) / upm, settings.arc ? " (flight: arc)" : " (flight: disc)");
    Pulse(link, hand, thr::ThrowPulse(settings.hapticAmplitude));
    if (Drawn(game) && !CallBool(game.pawn, gIsSwitchingWeapons)) {
        CallUseGrenade(game);
    } else {
        gPending.waitingForDraw = true;
        log::Info("throwables: the weapon isn't out yet: the throw waits for it (up to {:.1f} s; ME1 throws only with a weapon out)",
            kDrawWaitSeconds);
    }
}

#endif  // MEVR_GAME_LE1

void Inactive(std::string why, Link const& link) {
    if (gMachine.Holder() >= 0) {
        log::Info("throwables: the grenade in the {} hand goes back ({})", HandName(gMachine.Holder()), why);
    }
    gMachine.Reset();
    for (thr::SwingDetector& s : gSwing) {
        s.Reset();
    }
    gAnchor.Reset();
    gOwnYawActive = false;
    gYawValid = false;
    gFrameValid = false;
    gActive = false;
    gNearRight = false;
    gDistance = {-1.0, -1.0};
    gClaimQueue = {false, false};
    Quiet(link);
    SetWhy(std::move(why));
}

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    struct Want {
        ue3::Function** slot;
        ue3::FunctionRef* ref;
        bool needed;
    };
    Want const wants[] = {
        {&gUseGrenade, &gUseGrenadeFn, true},
        {&gTryMelee, &gTryMeleeFn, true},
        {&gIsInCombatActionState, &gIsInCombatActionStateFn, true},
        {&gIsSwitchingWeapons, &gIsSwitchingWeaponsFn, true},
        {&gIsThrowingGrenade, &gIsThrowingGrenadeFn, true},
        {&gIsCombatEnabled, &gIsCombatEnabledFn, true},
        {&gSetVelocity, &gSetVelocityFn, true},
        {&gLaunchAtPoint, &gLaunchAtPointFn, true},
        {&gTrace, &gTraceFn, true},
        {&gLaunchAtActor, &gLaunchAtActorFn, false},
        {&gLaunchGrenade, &gLaunchGrenadeFn, false},
        {&gIsUsingPower, &gIsUsingPowerFn, false},
        {&gGetGrenades, &gGetGrenadesFn, false},
        {&gIsHostile, &gIsHostileFn, false},
        {&gIsDead, &gIsDeadFn, false},
        {&gGetPercentHealth, &gGetPercentHealthFn, false},
        {&gSetLocation, &gSetLocationFn, false},
        {&gDestroy, &gDestroyFn, false},
    };
    std::string missing;
    bool ok = true;
    for (Want const& w : wants) {
        *w.slot = w.ref->Get();
        if (*w.slot == nullptr) {
            missing += std::format("{}Function {}{}", missing.empty() ? "" : ", ", w.ref->Path(), w.needed ? " (needed)" : "");
            ok = ok && !w.needed;
        }
    }
    if (ok) {
        for (ue3::Function* launch : {gLaunchAtPoint, gLaunchAtActor, gLaunchGrenade}) {
            if (launch != nullptr) {
                engine::process_event::AddPostHandler(launch, &OnLaunch);
            }
        }
    }
    gStarted = ok;
    log::Info("throwables: script functions {}{}", ok ? "found" : "MISSING: physical grenades and melee stay off",
        missing.empty() ? std::string() : std::format(" (not found: {})", missing));
    return ok;
#else
    return false;
#endif
}

void Claim(Link const& link) {
    gClaimed = {false, false};
    hand_calibration::Refresh();  // at most once a second
    hand_calibration::Throwables const settings = hand_calibration::GetThrowables();
    vr::controllers::State const controllers = vr::controllers::Latest();
    std::array<vr::controllers::Hand const*, 2> const hands{&controllers.left, &controllers.right};
    std::array<std::optional<std::uint32_t>, 2> const reports{link.leftGrip, link.rightGrip};
    std::array<bool, 2> fresh{};
    for (int h = 0; h < 2; ++h) {
        bool const was = gOwnGrip[h];
        gOwnGrip[h] = controllers.active && (gOwnGrip[h] ? hands[h]->squeeze > kGripOff : hands[h]->squeeze >= kGripOn);
        if (reports[h]) {
            // The host's count of squeezes it kept from the game (the right grip) or that began away from
            // the gun (the left): each new one, while it's still the current squeeze.
            std::uint32_t const count = *reports[h] >> 1;
            fresh[h] = gHaveCount[h] && count != gLastCount[h] && (*reports[h] & 1u) != 0;
            gLastCount[h] = count;
            gHaveCount[h] = true;
        } else {
            fresh[h] = gOwnGrip[h] && !was && (h == kRight || !support_grip::Held());
            if (!gToldOldGrip && settings.enabled) {
                gToldOldGrip = true;
                log::Warn("throwables: the host doesn't report the grips (services older than v11): grabs follow our own reading of "
                          "the grip, and the game may see that squeeze too; close the game and redeploy the host");
            }
        }
        bool const allowed = h == kLeft ? settings.leftHand : settings.rightHand;
        if (!fresh[h] || !gEnabled || !gActive || !gFrameValid || !allowed || gMachine.Holder() >= 0 || !hands[h]->gripValid) {
            continue;
        }
        double const margin = gMachine.InSlot(h) ? 0.05 : 0.0;
        double const distance = ik::Length(FromXr(hands[h]->grip.position) - gCenter);
        if (distance <= settings.slot.radius + margin) {
            gClaimed[h] = true;
            gClaimQueue[h] = true;
        }
    }
    // A squeeze of the right grip that didn't take a grenade is the gun's again.
    if (fresh[kRight] && !gClaimed[kRight] && gKeepGunOut) {
        gKeepGunOut = false;
    }
}

bool ClaimedLeft() {
    return gClaimed[kLeft];
}

bool ClaimedRight() {
    return gClaimed[kRight];
}

bool RightHandBusy() {
    return gClaimed[kRight] || gClaimQueue[kRight] || gMachine.Holder() == kRight || gKeepGunOut;
}

void Tick(double deltaSeconds, Link const& link) {
    hand_calibration::Throwables const settings = hand_calibration::GetThrowables();
    gEnabled = settings.enabled;
    gButtonFlags = link.buttonFlags;
    for (double& s : gSinceRelease) {
        s = std::min(s + std::max(deltaSeconds, 0.0), 1e9);
    }
#if defined(MEVR_GAME_LE1)
    std::string why;
    auto const game = gStarted ? ReadGame(why) : std::nullopt;
    FollowPending(game ? &*game : nullptr, deltaSeconds);
    WatchHit(link, deltaSeconds, settings.hapticAmplitude);
    if (!settings.enabled) {
        Inactive("off ([Throwables] enabled = 0)", link);
        return;
    }
    if (!gStarted) {
        Inactive("waiting for the script functions", link);
        return;
    }
    auto const mode = head_tracking::GameMode();
    if (!mode || *mode != 0) {
        Inactive(std::format("not on foot (game mode {})", mode ? std::to_string(*mode) : std::string("unknown")), link);
        return;
    }
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid) {
        Inactive("no head-tracked world view (menu, flat screen or VR off)", link);
        return;
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!controllers.active) {
        Inactive("the controllers aren't active", link);
        return;
    }
    if (!game) {
        Inactive(why, link);
        return;
    }
    auto const frame = view_anchor::Current();
    if (!frame) {
        Inactive("no view anchor (the hands can't be placed in the world)", link);
        return;
    }
    gActive = true;
    SetWhy("active");

    // The body frame, as the holsters': arm IK's torso yaw (else our own lagging one), held while a hand
    // is in the slot or holds a grenade; the anchor is the eye held upright over the neck, its height
    // smoothed; offsets scaled by the standing eye height (the holsters' measurement).
    XrPosef const& head = view.head;
    ik::Quat const headRotation{head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w};
    ik::Vec3 const headPosition{head.position.x, head.position.y, head.position.z};
    double const headYaw = ik::YawOf(headRotation.x, headRotation.y, headRotation.z, headRotation.w);
    if (!gOwnYawActive) {
        gOwnYaw.Reset(headYaw);
        gOwnYawActive = true;
    }
    double const ownYaw = gOwnYaw.Update(headYaw, deltaSeconds);
    bool const busy = gMachine.Holder() >= 0 || gMachine.InSlot(kLeft) || gMachine.InSlot(kRight);
    if (!gYawValid || !busy) {
        std::optional<double> const torso = arm_ik::TorsoYaw();
        gYaw = torso ? *torso : ownYaw;
        gYawValid = true;
    }
    gFrame.yaw = gYaw;
    gFrame.anchor = gAnchor.Update(slots::NeutralEye(headPosition, headRotation), deltaSeconds);
    slots::Layout layout;
    layout.eyeHeight = holsters::EyeHeight();
    gScale = slots::Scale(layout);
    gCenter = slots::SlotCenter(gFrame, settings.slot, gScale);
    gFrameValid = true;
    std::array<vr::controllers::Hand const*, 2> const hands{&controllers.left, &controllers.right};
    for (int h = 0; h < 2; ++h) {
        gDistance[h] = hands[h]->gripValid ? ik::Length(FromXr(hands[h]->grip.position) - gCenter) : -1.0;
    }
    gNearRight = gDistance[kRight] >= 0.0 && gDistance[kRight] <= settings.slot.radius + 0.05;

    // The machines run on new controller samples only (their velocities need real time steps).
    auto const sampleTime = gClock.Update(controllers.sequence, SteadySeconds());
    if (sampleTime) {
        gTime = *sampleTime;
        thr::Input input;
        input.now = gTime;
        input.radius = settings.slot.radius;
        input.throwSpeedMin = settings.throwSpeedMin;
        input.grip.earlyDrop = static_cast<float>(settings.earlyRelease);
        bool const claimed = gClaimQueue[kLeft] || gClaimQueue[kRight];
        std::string refusal;
        if (claimed) {
            refusal = GrenadeRefusal(*game);
        }
        input.canGrab = refusal.empty();
        for (int h = 0; h < 2; ++h) {
            thr::HandInput& in = input.hands[h];
            in.tracked = hands[h]->gripValid;
            in.position = FromXr(hands[h]->grip.position);
            in.distance = gDistance[h];
            in.newSqueeze = gClaimQueue[h];
            in.squeeze = hands[h]->squeeze;
            in.allowed = h == kLeft ? settings.leftHand : settings.rightHand;
        }
        gClaimQueue = {false, false};
        thr::Step const step = gMachine.Update(input);
        for (int h = 0; h < 2; ++h) {
            if (step.entered[h] != 0) {
                log::Info("throwables: {} hand entered the grenade slot ({:.2f} m from its centre, radius {:.2f})", HandName(h), gDistance[h],
                    settings.slot.radius);
                Pulse(link, h, slots::EnterEdge(settings.hapticAmplitude));  // the edge, as a holster's
            }
            if (step.left[h] != 0) {
                log::Info("throwables: {} hand left the grenade slot ({:.2f} m)", HandName(h), gDistance[h]);
            }
        }
        if (claimed && step.event == thr::Event::None) {
            log::Info("throwables: a squeeze at the slot's edge took nothing (the hand just outside it)");
        }
        switch (step.event) {
        case thr::Event::Grabbed: {
            ++gGrabs;
            Pulse(link, step.hand, thr::GrabClick(settings.hapticAmplitude));
            bool const drawn = Drawn(*game);
            log::Info("throwables: grenade taken with the {} hand ({} left){}", HandName(step.hand),
                gGrenadesLeft >= 0 ? std::to_string(gGrenadesLeft) : std::string("?"),
                drawn ? "" : settings.drawForThrow ? "; the weapon is away: drawing it (SFXGameModeDefault.UseGrenade draws it), since "
                                                     "ME1 throws only with a weapon out"
                                                   : "; the weapon is away and drawForThrow = 0: the throw will wait for a weapon");
            if (!drawn && settings.drawForThrow) {
                CallVoid(game->mode, gUseGrenade);  // with the weapon away it only draws it
            }
            break;
        }
        case thr::Event::Refused:
            ++gRefusals;
            Pulse(link, step.hand, thr::RefusedBuzz(settings.hapticAmplitude));
            log::Info("throwables: a squeeze at the grenade slot with the {} hand: no grenade ({})", HandName(step.hand), refusal);
            break;
        case thr::Event::Thrown:
            gSinceRelease[step.hand] = 0.0;
            if (step.hand == kRight) {
                gKeepGunOut = true;
            }
            Throw(link, *game, step.hand, step.release, settings, *hands[step.hand]);
            break;
        case thr::Event::Cancelled:
            ++gCancels;
            gSinceRelease[step.hand] = 0.0;
            if (step.hand == kRight) {
                gKeepGunOut = true;
            }
            Pulse(link, step.hand, thr::StowClick(settings.hapticAmplitude));
            log::Info("throwables: the {} hand let go at {:.2f} m/s (under {:.1f}): the grenade goes back", HandName(step.hand),
                step.release.valid ? step.release.speed : 0.0, settings.throwSpeedMin);
            break;
        case thr::Event::Dropped:
            ++gCancels;
            if (step.hand == kRight) {
                gKeepGunOut = true;
            }
            log::Info("throwables: the {} hand lost tracking holding a grenade: it goes back", HandName(step.hand));
            break;
        case thr::Event::None:
            break;
        }

        // Melee: each hand's stroke relative to the head.
        bool const drawn = Drawn(*game);
        for (int h = 0; h < 2; ++h) {
            thr::SwingInput swing;
            swing.now = gTime;
            swing.tracked = hands[h]->gripValid;
            swing.position = FromXr(hands[h]->grip.position) - headPosition;
            swing.bodyForward = slots::YawForward(gFrame.yaw);
            // A throw moves the whole body: neither hand strikes just after one.
            bool suppressed = gMachine.Holder() == h || std::min(gSinceRelease[kLeft], gSinceRelease[kRight]) < kNoMeleeAfterRelease;
            if (h == kRight) {
                suppressed = suppressed || hands[h]->trigger >= kTriggerOn || holsters::RightHandInSlot() ||
                    holsters::SecondsSinceDraw() < kNoMeleeAfterDraw;
            } else {
                suppressed = suppressed || support_grip::Held();
            }
            swing.suppressed = suppressed;
            thr::SwingParams params;
            params.minSpeed = h == kRight && drawn ? settings.meleeGunSpeed : settings.meleeSpeed;
            params.minTravel = settings.meleeTravel;
            params.cooldown = settings.meleeCooldown;
            thr::SwingStep const stroke = gSwing[h].Update(swing, params);
            if (!settings.melee) {
                continue;
            }
            if (stroke.strike) {
                Strike(link, *game, h, stroke, settings, *frame, headPosition);
            } else if (stroke.refused != nullptr) {
                ++gStrokeRefusals;
                log::Info("throwables: melee: the {} hand's {:.1f} m/s stroke ({:.2f} m) isn't a strike: {}", HandName(h), stroke.speed,
                    stroke.travel, stroke.refused);
            }
        }
    }

    // Haptics each tick: the slot's contact buzz (as a holster's), or the held grenade's faint buzz.
    for (int h = 0; h < 2; ++h) {
        if (gMachine.Holder() == h) {
            thr::Buzz const hold = settings.holdBuzz ? thr::HoldBuzz(settings.hapticAmplitude) : thr::Buzz{};
            Continuous(link, h, hold.amplitude, hold.frequency);
        } else if (gMachine.InSlot(h) && gDistance[h] >= 0.0) {
            slots::Buzz const contact = slots::Contact(slots::Depth(gDistance[h], settings.slot.radius), settings.hapticAmplitude, false);
            Continuous(link, h, contact.amplitude, contact.frequency);
        } else {
            Continuous(link, h, 0.0f, 0.0f);
        }
    }
#else
    (void)deltaSeconds;
    Inactive("LE1 only", link);
#endif
}

void AdjustPad(gamepad::Options& options) {
    if (!gEnabled) {
        return;
    }
    hand_calibration::Throwables const settings = hand_calibration::GetThrowables();
    if (gActive && (!settings.meleeButton || !settings.grenadeButton)) {
        if (!gButtonFlags && !gToldNoButtons) {
            gToldNoButtons = true;
            log::Warn("throwables: meleeButton/grenadeButton = 0 needs the redeployed host (services v14): until then B and X still "
                      "reach the game; close the game and redeploy the host");
        }
        options.meleeButton = settings.meleeButton;
        options.grenadeButton = settings.grenadeButton;
    }
    if (gActive && gNearRight && settings.rightHand && gMachine.Holder() < 0) {
        options.rightGripFree = true;  // a new right squeeze at the grenade slot isn't RB (gripHolds 0)
    }
}

std::string StatusJson() {
    namespace json = vr::test_request;
    hand_calibration::Throwables const settings = hand_calibration::GetThrowables();
    std::string const last = gLastThrow.valid
        ? std::format("{{\"hand\": {}, \"handSpeed\": {}, \"direction\": [{}, {}, {}], \"peakAgo\": {}, \"launch\": {}, \"delay\": {}, "
                      "\"landing\": [{}, {}, {}]}}",
              json::JsonString(HandName(gLastThrow.hand)), json::JsonNumber(gLastThrow.handSpeed), json::JsonNumber(gLastThrow.direction.x),
              json::JsonNumber(gLastThrow.direction.y), json::JsonNumber(gLastThrow.direction.z), json::JsonNumber(gLastThrow.peakAgo),
              json::JsonString(gLastThrow.launch), json::JsonNumber(gLastThrow.delay), json::JsonNumber(gLastThrow.landing.x),
              json::JsonNumber(gLastThrow.landing.y), json::JsonNumber(gLastThrow.landing.z))
        : std::string("null");
    return std::format("{{\"enabled\": {}, \"active\": {}, \"why\": {}, \"slot\": {{\"center\": [{}, {}, {}], \"radius\": {}, \"scale\": {}}}, "
                       "\"distance\": [{}, {}], \"inSlot\": [{}, {}], \"holder\": {}, \"keepGunOut\": {}, \"grenadesLeft\": {}, "
                       "\"pending\": {}, \"lastThrow\": {}, \"melee\": {{\"enabled\": {}, \"peakSpeed\": [{}, {}], \"last\": {}, "
                       "\"cooldownLeft\": {}}}, \"flight\": {}}}",
        gEnabled ? "true" : "false", gActive ? "true" : "false", json::JsonString(gWhy), json::JsonNumber(gCenter.x),
        json::JsonNumber(gCenter.y), json::JsonNumber(gCenter.z), json::JsonNumber(settings.slot.radius), json::JsonNumber(gScale),
        json::JsonNumber(gDistance[kLeft]), json::JsonNumber(gDistance[kRight]), gMachine.InSlot(kLeft) ? "true" : "false",
        gMachine.InSlot(kRight) ? "true" : "false", gMachine.Holder() >= 0 ? json::JsonString(HandName(gMachine.Holder())) : std::string("null"),
        gKeepGunOut ? "true" : "false", gGrenadesLeft, gPending.active ? (gPending.waitingForDraw ? "\"waiting for the draw\"" : "\"awaiting the launch\"")
                                                                       : "null",
        last, settings.melee ? "true" : "false", json::JsonNumber(gSwing[kLeft].PeakSpeed()), json::JsonNumber(gSwing[kRight].PeakSpeed()),
        json::JsonString(gLastMelee), json::JsonNumber(std::max(gMeleeReadyAt - gTime, 0.0)), json::JsonString(settings.arc ? "arc" : "disc"));
}

void LogStats() {
    if (!gEnabled && gGrabs == 0 && gStrikes == 0) {
        return;
    }
    log::Info("    throwables: {}; grenade slot {} from the anchor (scale {:.2f}), hands {:.2f} / {:.2f} m from it; holder {}; this period: "
              "{} grabs, {} throws, {} put back, {} refused, {} launches ({} re-aimed); melee: {} strikes, {} hits, {} refused at the "
              "game, {} strokes refused; peak hand speeds (head-relative) {:.1f} / {:.1f} m/s; grenades left {}",
        gWhy, V(gCenter - gFrame.anchor), gScale, gDistance[kLeft], gDistance[kRight],
        gMachine.Holder() >= 0 ? HandName(gMachine.Holder()) : "none", gGrabs, gThrows, gCancels, gRefusals, gLaunches, gAimed, gStrikes,
        gHits, gMeleeRefusals, gStrokeRefusals, gSwing[kLeft].PeakSpeed(), gSwing[kRight].PeakSpeed(),
        gGrenadesLeft >= 0 ? std::to_string(gGrenadesLeft) : std::string("?"));
    gGrabs = gThrows = gCancels = gRefusals = gLaunches = gAimed = gStrikes = gHits = gStrokeRefusals = gMeleeRefusals = 0;
    gSwing[kLeft].ResetPeak();
    gSwing[kRight].ResetPeak();
}

void Shutdown() {
    gMachine.Reset();
    for (thr::SwingDetector& s : gSwing) {
        s.Reset();
        s.ResetPeak();
    }
    gClock.Reset();
    gAnchor.Reset();
    gOwnYawActive = false;
    gYawValid = false;
    gFrameValid = false;
    gActive = false;
    gEnabled = false;
    gNearRight = false;
    gClaimed = {false, false};
    gClaimQueue = {false, false};
    gOwnGrip = {false, false};
    gHaveCount = {false, false};
    gKeepGunOut = false;
    gSinceRelease = {1e9, 1e9};
    gContinuous = {0.0f, 0.0f};
    gToldNoHaptics = gToldNoContinuous = gToldNoButtons = gToldOldGrip = false;
    gPending = {};
    gInLaunch = false;
    gLoggedGameThrow = false;
    gLastThrow = {};
    gHitWatch = {};
    gMeleeReadyAt = 0.0;
    gLastMelee = "none";
    gGrenadesLeft = -1;
    gStarted = false;
    gWhy = "not started";
    gLoggedWhy.clear();
#if defined(MEVR_GAME_LE1)
    gUseGrenade = gTryMelee = gGetGrenades = gIsInCombatActionState = gIsSwitchingWeapons = gIsThrowingGrenade = gIsUsingPower = nullptr;
    gIsHostile = gIsDead = gGetPercentHealth = gIsCombatEnabled = gTrace = gSetLocation = gDestroy = gSetVelocity = nullptr;
    gLaunchGrenade = gLaunchAtPoint = gLaunchAtActor = nullptr;
    gLoggedTracePath = false;
#endif
}

} // namespace mevr::game::throwables
