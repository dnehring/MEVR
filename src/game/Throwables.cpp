// SPDX-License-Identifier: MIT
#include "game/Throwables.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/Throwing.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Engine.hpp"
#include "engine/Objects.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ArmIk.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Holsters.hpp"
#include "game/SupportGrip.hpp"
#include "game/ViewAnchor.hpp"

#include <LESDK/Headers.hpp>

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

#if defined(SDK_TARGET_LE1)

static_assert(offsetof(ABioGrenadeProjectileFrisbee, m_vIntendedTargetLocation) == 0x354);
static_assert(offsetof(ABioGrenadeProjectileFrisbee, m_oHeatSeekTarget) == 0x36C);
static_assert(offsetof(ABioGrenadeProjectileFrisbee, m_oIntendedTarget) == 0x374);
static_assert(offsetof(ABioGrenade, m_clsProjectile) == 0x3C8);
static_assert(offsetof(AWorldInfo, PawnList) == 0x69C);
static_assert(offsetof(APawn, NextPawn) == 0x408);

UFunction* gUseGrenade = nullptr;
UFunction* gTryMelee = nullptr;
UFunction* gGetGrenades = nullptr;
UFunction* gIsInCombatActionState = nullptr;
UFunction* gIsSwitchingWeapons = nullptr;
UFunction* gIsThrowingGrenade = nullptr;
UFunction* gIsUsingPower = nullptr;
UFunction* gIsHostile = nullptr;
UFunction* gIsDead = nullptr;
UFunction* gGetPercentHealth = nullptr;
UFunction* gIsCombatEnabled = nullptr;
UFunction* gTrace = nullptr;
UFunction* gSetLocation = nullptr;
UFunction* gDestroy = nullptr;
UFunction* gSetVelocity = nullptr;
UFunction* gLaunchGrenade = nullptr;
UFunction* gLaunchAtPoint = nullptr;
UFunction* gLaunchAtActor = nullptr;

FVector ToF(engine::UnrealVector const& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

engine::UnrealVector FromF(FVector const& v) {
    return {v.X, v.Y, v.Z};
}

bool CallBool(UObject* object, UFunction* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    struct {
        unsigned long ReturnValue;
        unsigned char pad[60];
    } parms{};
    object->ProcessEvent(function, &parms, nullptr);  // directly: never the SDK's wrapper
    return parms.ReturnValue != 0;
}

struct Game {
    ABioPlayerController* controller = nullptr;
    ABioPawn* pawn = nullptr;
    UBioPawnBehavior* behavior = nullptr;
    USFXGameModeDefault* mode = nullptr;
};

std::optional<Game> ReadGame(std::string& why) {
    APlayerController* const pc = engine::LocalPlayerController();
    if (pc == nullptr || !pc->IsA(ABioPlayerController::StaticClass())) {
        why = "no player controller";
        return std::nullopt;
    }
    Game game;
    game.controller = static_cast<ABioPlayerController*>(pc);
    if (pc->Pawn == nullptr || !pc->Pawn->IsA(ABioPawn::StaticClass())) {
        why = "no player pawn (vehicle or cutscene?)";
        return std::nullopt;
    }
    game.pawn = static_cast<ABioPawn*>(pc->Pawn);
    game.behavior = game.pawn->m_oBehavior;
    if (game.behavior == nullptr) {
        why = "the player pawn has no behavior";
        return std::nullopt;
    }
    if (USFXGameModeManager* const manager = game.controller->GameModeManager) {
        for (int i = 0; i < static_cast<int>(manager->GameModes.Count()); ++i) {
            USFXGameModeBase* const mode = manager->GameModes.GetData()[i];
            if (mode != nullptr && mode->IsA(USFXGameModeDefault::StaticClass())) {
                game.mode = static_cast<USFXGameModeDefault*>(mode);
                break;
            }
        }
    }
    if (game.mode == nullptr) {
        why = "no SFXGameModeDefault in the player controller's game modes";
        return std::nullopt;
    }
    return game;
}

bool CombatEnabled(Game const& game) {
    UPlayerInput* const input = game.controller->PlayerInput;
    return input != nullptr && input->IsA(UBioPlayerInput::StaticClass()) && CallBool(input, gIsCombatEnabled);
}

bool Drawn(Game const& game) {
    return CallBool(game.pawn, gIsInCombatActionState);
}

bool UsingPower(Game const& game) {
    if (gIsUsingPower == nullptr) {
        return false;
    }
    ABioPawn_execIsUsingPower_Parms parms;
    parms.bIgnoreWeaponPowers = 0;
    game.pawn->ProcessEvent(gIsUsingPower, &parms, nullptr);
    return parms.ReturnValue != 0;
}

int GrenadesLeft(Game const& game) {
    if (gGetGrenades == nullptr) {
        return -1;
    }
    UBioPawnBehavior_eventGetGrenades_Parms parms;
    game.behavior->ProcessEvent(gGetGrenades, &parms, nullptr);
    return parms.ReturnValue;
}

double Health(ABioPawn* pawn) {
    if (pawn == nullptr || pawn->m_oBehavior == nullptr || gGetPercentHealth == nullptr) {
        return -1.0;
    }
    UBioEpicPawnBehavior_execGetPercentHealth_Parms parms;
    pawn->m_oBehavior->ProcessEvent(gGetPercentHealth, &parms, nullptr);
    return parms.ReturnValue;
}

// Why the game would refuse a grenade now (empty: it wouldn't).
std::string GrenadeRefusal(Game const& game) {
    if (!CombatEnabled(game)) {
        return "combat is disabled here (a hub or a scripted scene)";
    }
    if (game.controller->bStorming) {
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
// it came back. Directly through ProcessEvent, never the SDK's wrapper.
AActor* TraceOnce(Game const& game, engine::UnrealVector const& start, engine::UnrealVector const& end, engine::UnrealVector* hit) {
    AActor_execTrace_Parms parms;
    parms.TraceEnd = ToF(end);
    parms.TraceStart = ToF(start);
    parms.bTraceActors = 1;
    game.pawn->ProcessEvent(gTrace, &parms, nullptr);
    if (hit != nullptr) {
        *hit = FromF(parms.HitLocation);
    }
    return parms.ReturnValue;
}

bool gLoggedTracePath = false;

// A line trace from `start` to `end`: the hit point and actor. A native's struct out parameter called
// through ProcessEvent may not come back (research/08 H17): then the hit point is found by bisecting
// the segment with the hit/no-hit answer alone (to 1/64 of it).
bool Trace(Game const& game, engine::UnrealVector const& start, engine::UnrealVector const& end, engine::UnrealVector& hit,
    AActor*& actor) {
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
            if (AActor* const a = TraceOnce(game, start, at(mid), nullptr)) {
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

bool SetLocation(AActor* actor, engine::UnrealVector const& where) {
    if (actor == nullptr || gSetLocation == nullptr) {
        return false;
    }
    AActor_execSetLocation_Parms parms;
    parms.NewLocation = ToF(where);
    actor->ProcessEvent(gSetLocation, &parms, nullptr);
    return parms.ReturnValue != 0;
}

void SetVelocity(ABioGrenadeProjectile* projectile, engine::UnrealVector const& velocity) {
    ABioGrenadeProjectile_execSetVelocity_Parms parms;
    parms.vVelocity = ToF(velocity);
    projectile->ProcessEvent(gSetVelocity, &parms, nullptr);  // native: the class's own SetVelocity
}

// The disc: from the hand, toward where the throw lands, colliding there (no homing).
std::string AimDisc(ABioGrenadeProjectile* projectile) {
    if (!projectile->IsA(ABioGrenadeProjectileFrisbee::StaticClass())) {
        return std::format("a {} (not the disc): left as the game aimed it", engine::FullName(projectile));
    }
    auto* const disc = static_cast<ABioGrenadeProjectileFrisbee*>(projectile);
    engine::UnrealVector const spawned = FromF(disc->Location);
    std::string moved;
    if (gPending.fromHand) {
        bool const ok = SetLocation(disc, gPending.origin);
        moved = ok ? std::format(", moved {:.0f} units from where the game spawned it to the hand", Len(Sub(gPending.origin, spawned)))
                   : std::string(", couldn't be moved to the hand (blocked): from the game's spot");
    }
    engine::UnrealVector const from = FromF(disc->Location);
    SetVelocity(disc, Sub(gPending.landing, from));  // the disc keeps its own speed along it
    disc->m_vIntendedTargetLocation = ToF(gPending.landing);
    write_journal::Wrote("Throw.disc target location", &disc->m_vIntendedTargetLocation);
    AActor* target = nullptr;
    if (gPending.landingActor != 0) {
        auto* const candidate = reinterpret_cast<AActor*>(gPending.landingActor);
        target = engine::IsAlive(candidate) ? candidate : nullptr;
    }
    disc->m_oIntendedTarget = target;
    write_journal::Wrote("Throw.disc target", &disc->m_oIntendedTarget);
    disc->m_oHeatSeekTarget = nullptr;
    write_journal::Wrote("Throw.disc heat seek target", &disc->m_oHeatSeekTarget);
    disc->m_vStartLocation = disc->Location;
    write_journal::Wrote("Throw.disc start location", &disc->m_vStartLocation);
    double const meters = Len(Sub(gPending.landing, from)) / std::max(1.0, gPending.unitsPerMeter);
    return std::format("the disc re-aimed{} to {} ({:.1f} m, {}, speed {:.0f} units/s)", moved, U(gPending.landing), meters,
        target != nullptr ? engine::FullName(target) : std::string(gPending.landingHit ? "a surface" : "nothing hit: open air"),
        disc->Speed);
}

// The ballistic grenade with the hand's velocity, in place of the disc. False: it didn't spawn.
bool LaunchArc(ABioGrenade* grenade, ABioGrenadeProjectile*& projectile, std::string& what) {
    if (gLaunchGrenade == nullptr || gDestroy == nullptr) {
        what = "the arc needs BioGrenade.LaunchGrenade and Actor.Destroy (not found)";
        return false;
    }
    UClass* const saved = grenade->m_clsProjectile;
    grenade->m_clsProjectile = ABioGrenadeProjectileBallistic::StaticClass();
    write_journal::Wrote("Throw.projectile class", &grenade->m_clsProjectile);
    ABioGrenade_eventLaunchGrenade_Parms parms;
    parms.vVelocity = ToF(gPending.velocity);
    gInLaunch = true;
    grenade->ProcessEvent(gLaunchGrenade, &parms, nullptr);  // the game's own spawn: MakeProjectile, then SetVelocity
    gInLaunch = false;
    grenade->m_clsProjectile = saved;
    write_journal::Wrote("Throw.projectile class back", &grenade->m_clsProjectile);
    ABioGrenadeProjectile* const ball = parms.ReturnValue;
    if (ball == nullptr) {
        what = "the ballistic grenade didn't spawn";
        return false;
    }
    std::string moved;
    if (gPending.fromHand) {
        moved = SetLocation(ball, gPending.origin) ? ", from the hand" : ", from the game's spot (the hand's was blocked)";
        SetVelocity(ball, gPending.velocity);
    }
    AActor_execDestroy_Parms destroy;
    projectile->ProcessEvent(gDestroy, &destroy, nullptr);
    projectile = ball;
    what = std::format("the ballistic grenade launched{} at {} ({:.0f} units/s; the disc removed{})", moved, U(gPending.velocity),
        Len(gPending.velocity), destroy.ReturnValue != 0 ? "" : ": NOT destroyed");
    return true;
}

// BioGrenade.LaunchGrenadeAtPoint / LaunchGrenadeAtActor / LaunchGrenade, after the game's own: the
// projectile it returned is ours to re-aim if the player's physical throw is waiting for it.
void OnLaunch(UObject* context, UFunction* function, void* parms, void* /*result*/) {
    if (gInLaunch || context == nullptr || !context->IsA(ABioGrenade::StaticClass())) {
        return;
    }
    APlayerController* const pc = engine::LocalPlayerController();
    auto* const grenade = static_cast<ABioGrenade*>(context);
    if (pc == nullptr || pc->Pawn == nullptr || (grenade->Owner != pc->Pawn && grenade->Instigator != pc->Pawn)) {
        return;  // a squadmate's or an enemy's
    }
    // The return value's slot in the parms (the SDK's structs are packed: copied, never referenced).
    std::byte* slot = nullptr;
    std::string how;
    if (function == gLaunchAtPoint) {
        auto* const p = static_cast<ABioGrenade_eventLaunchGrenadeAtPoint_Parms*>(parms);
        slot = static_cast<std::byte*>(parms) + offsetof(ABioGrenade_eventLaunchGrenadeAtPoint_Parms, ReturnValue);
        how = std::format("LaunchGrenadeAtPoint{}", U(FromF(p->vTarget)));
    } else if (function == gLaunchAtActor) {
        auto* const p = static_cast<ABioGrenade_eventLaunchGrenadeAtActor_Parms*>(parms);
        slot = static_cast<std::byte*>(parms) + offsetof(ABioGrenade_eventLaunchGrenadeAtActor_Parms, ReturnValue);
        AActor* const target = p->oTarget;
        how = std::format("LaunchGrenadeAtActor({})", target != nullptr ? engine::FullName(target) : std::string("None"));
    } else if (function == gLaunchGrenade) {
        auto* const p = static_cast<ABioGrenade_eventLaunchGrenade_Parms*>(parms);
        slot = static_cast<std::byte*>(parms) + offsetof(ABioGrenade_eventLaunchGrenade_Parms, ReturnValue);
        how = std::format("LaunchGrenade{}", U(FromF(p->vVelocity)));
    } else {
        return;
    }
    ABioGrenadeProjectile* projectile = nullptr;
    std::memcpy(&projectile, slot, sizeof projectile);
    ++gLaunches;
    if (!gPending.active || !gPending.called) {
        if (!gLoggedGameThrow) {
            gLoggedGameThrow = true;
            log::Info("throwables: a grenade thrown the game's way (the X button?): {} -> {}, left as the game aimed it (logged once)",
                how, projectile != nullptr ? engine::FullName(projectile) : std::string("no projectile"));
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
        projectile != nullptr ? engine::FullName(projectile) : std::string("none"), what);
    gLastThrow.launch = what;
    gLastThrow.delay = delay;
    gPending = {};
}

// The enemy a melee would hit: the nearest living hostile in reach and in front of the body.
struct Enemy {
    ABioPawn* pawn = nullptr;
    double meters = 1e9;
    double degrees = 0.0;
};

Enemy FindEnemy(Game const& game, engine::TrackingFrame const& frame, ik::Vec3 headTracking, double reachMeters, double coneDegrees,
    Enemy& nearestHostile) {
    Enemy best;
    AWorldInfo* const world = game.pawn->WorldInfo;
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
    engine::UnrealVector const me = FromF(game.pawn->Location);
    int guard = 0;
    for (APawn* p = world->PawnList; p != nullptr && guard < 512; p = p->NextPawn, ++guard) {
        if (p == game.pawn || !p->IsA(ABioPawn::StaticClass())) {
            continue;
        }
        auto* const other = static_cast<ABioPawn*>(p);
        if (CallBool(other, gIsDead)) {
            continue;
        }
        ABioPawn_execIsHostile_Parms hostile;
        hostile.Other = other;
        game.pawn->ProcessEvent(gIsHostile, &hostile, nullptr);
        if (hostile.ReturnValue == 0) {
            continue;
        }
        engine::UnrealVector const d = Sub(FromF(other->Location), me);
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
    if (game.controller->bStorming) {
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
                  settings.meleeReach, settings.meleeConeDeg, engine::FullName(nearest.pawn), nearest.meters, nearest.degrees)
            : std::string("no hostile around"));
    }
    USFXGameModeDefault_execTryMelee_Parms parms;
    game.mode->ProcessEvent(gTryMelee, &parms, nullptr);  // the B button's exec: directly, never the SDK's wrapper
    gMeleeReadyAt = gTime + settings.meleeCooldown;
    ++gStrikes;
    Pulse(link, hand, thr::StrikeTick(settings.hapticAmplitude));
    std::string const target = enemy.pawn != nullptr
        ? std::format("{} at {:.1f} m, {:.0f} degrees, health {:.0f}%", engine::FullName(enemy.pawn), enemy.meters, enemy.degrees,
              100.0 * Health(enemy.pawn))
        : std::string("no enemy in reach (meleeNeedsEnemy = 0)");
    gLastMelee = std::format("{} -> TryMelee; {}", what, target);
    log::Info("throwables: melee: {} -> SFXGameModeDefault.TryMelee ({}); target {}", what,
        game.behavior->m_oEquipment != nullptr && game.behavior->m_oEquipment->m_eQuickSlotCurrent == 0 ? "pistol whip" : "rifle butt",
        target);
    if (enemy.pawn != nullptr) {
        gHitWatch = {true, hand, 0.0, reinterpret_cast<std::uintptr_t>(enemy.pawn), Health(enemy.pawn), engine::FullName(enemy.pawn)};
    }
}

void WatchHit(Link const& link, double deltaSeconds, double strength) {
    if (!gHitWatch.active) {
        return;
    }
    gHitWatch.age += deltaSeconds;
    auto* const pawn = reinterpret_cast<ABioPawn*>(gHitWatch.target);
    bool const alive = engine::IsAlive(pawn) && pawn->IsA(ABioPawn::StaticClass());
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
    USFXGameModeDefault_execUseGrenade_Parms parms;
    game.mode->ProcessEvent(gUseGrenade, &parms, nullptr);  // the X button's exec: directly, never the SDK's wrapper
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
    if (settings.arc && game.pawn->WorldInfo != nullptr && game.pawn->WorldInfo->WorldGravityZ < 0.0f) {
        gravityZ = game.pawn->WorldInfo->WorldGravityZ;
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
        AActor* actor = nullptr;
        if (Trace(game, {arc[i - 1].x, arc[i - 1].y, arc[i - 1].z}, {arc[i].x, arc[i].y, arc[i].z}, hit, actor)) {
            pending.landing = hit;
            pending.landingHit = true;
            pending.landingActor = reinterpret_cast<std::uintptr_t>(actor);
            pending.landingName = engine::FullName(actor);
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

#endif  // SDK_TARGET_LE1

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
#if defined(SDK_TARGET_LE1)
    struct Want {
        UFunction** slot;
        wchar_t const* name;
        char const* full;
        bool needed;
    };
    Want const wants[] = {
        {&gUseGrenade, L"UseGrenade", "Function SFXGame.SFXGameModeDefault.UseGrenade", true},
        {&gTryMelee, L"TryMelee", "Function SFXGame.SFXGameModeDefault.TryMelee", true},
        {&gIsInCombatActionState, L"IsInCombatActionState", "Function SFXGame.BioPawn.IsInCombatActionState", true},
        {&gIsSwitchingWeapons, L"IsSwitchingWeapons", "Function SFXGame.BioPawn.IsSwitchingWeapons", true},
        {&gIsThrowingGrenade, L"IsThrowingGrenade", "Function SFXGame.BioPawn.IsThrowingGrenade", true},
        {&gIsCombatEnabled, L"IsCombatEnabled", "Function SFXGame.BioPlayerInput.IsCombatEnabled", true},
        {&gSetVelocity, L"SetVelocity", "Function SFXGame.BioGrenadeProjectile.SetVelocity", true},
        {&gLaunchAtPoint, L"LaunchGrenadeAtPoint", "Function SFXGame.BioGrenade.LaunchGrenadeAtPoint", true},
        {&gTrace, L"Trace", "Function Engine.Actor.Trace", true},
        {&gLaunchAtActor, L"LaunchGrenadeAtActor", "Function SFXGame.BioGrenade.LaunchGrenadeAtActor", false},
        {&gLaunchGrenade, L"LaunchGrenade", "Function SFXGame.BioGrenade.LaunchGrenade", false},
        {&gIsUsingPower, L"IsUsingPower", "Function SFXGame.BioPawn.IsUsingPower", false},
        {&gGetGrenades, L"GetGrenades", "Function SFXGame.BioPawnBehavior.GetGrenades", false},
        {&gIsHostile, L"IsHostile", "Function SFXGame.BioPawn.IsHostile", false},
        {&gIsDead, L"IsDead", "Function SFXGame.BioPawn.IsDead", false},
        {&gGetPercentHealth, L"GetPercentHealth", "Function SFXGame.BioEpicPawnBehavior.GetPercentHealth", false},
        {&gSetLocation, L"SetLocation", "Function Engine.Actor.SetLocation", false},
        {&gDestroy, L"Destroy", "Function Engine.Actor.Destroy", false},
    };
    std::string missing;
    bool ok = true;
    for (Want const& w : wants) {
        *w.slot = engine::FindFunction(w.name, w.full);
        if (*w.slot == nullptr) {
            missing += std::format("{}{}{}", missing.empty() ? "" : ", ", w.full, w.needed ? " (needed)" : "");
            ok = ok && !w.needed;
        }
    }
    if (ok) {
        for (UFunction* launch : {gLaunchAtPoint, gLaunchAtActor, gLaunchGrenade}) {
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
#if defined(SDK_TARGET_LE1)
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
                USFXGameModeDefault_execUseGrenade_Parms parms;
                game->mode->ProcessEvent(gUseGrenade, &parms, nullptr);  // with the weapon away it only draws it
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
#if defined(SDK_TARGET_LE1)
    gUseGrenade = gTryMelee = gGetGrenades = gIsInCombatActionState = gIsSwitchingWeapons = gIsThrowingGrenade = gIsUsingPower = nullptr;
    gIsHostile = gIsDead = gGetPercentHealth = gIsCombatEnabled = gTrace = gSetLocation = gDestroy = gSetVelocity = nullptr;
    gLaunchGrenade = gLaunchAtPoint = gLaunchAtActor = nullptr;
    gLoggedTracePath = false;
#endif
}

} // namespace mevr::game::throwables
