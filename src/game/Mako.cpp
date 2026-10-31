// SPDX-License-Identifier: MIT
#include "game/Mako.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Calibration.hpp"
#include "core/vr/Comfort.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/BonePose.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/BodyCalibration.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace mevr::game::mako {
namespace {

namespace json = vr::test_request;

constexpr unsigned char kGameModeVehicle = 1;  // SFXGameModeManager.EGameModes: the Mako
constexpr int kMaxTurrets = 4;
constexpr int kMaxControlsPerList = 32;
constexpr ULONGLONG kAimFreshMs = 500;    // an aim older than this aims nothing (the mod stopped ticking)
constexpr ULONGLONG kStartFreshMs = 200;  // the shot's start seen within this of the aim query belongs to it
constexpr int kHeldUnits = 182;           // a degree: DesiredBoneRotation moved less than this is "held"
constexpr double kWhyLogSeconds = 2.0;
constexpr unsigned kShotLines = 3;        // the first shots logged one by one, then one every two seconds

#if defined(MEVR_GAME_LE1)
// Our reflection layer (ue3/, research/34): each found once, by name, in the running game; every row is checked by
// engine/ReflectCheck.
ue3::ClassRef gVehicleBase{"SFXGame.BioVehicleBase"};
ue3::ClassRef gBehaviorBase{"SFXGame.BioVehicleBehaviorBase"};
ue3::ClassRef gTurretControl{"SFXGame.BioSkelControl_TurretConstrained"};
ue3::ClassRef gAnimTree{"Engine.AnimTree"};
ue3::FieldRef gBehavior{"SFXGame.BioVehicleBase", "m_oBehavior"};
ue3::FieldRef gWeaponInfo{"SFXGame.BioVehicleBehaviorBase", "m_aVehicleWeaponInfo"};
ue3::FieldRef gInfoWeapon{gWeaponInfo, "oVehicleWeaponPtr"};
ue3::FieldRef gInfoTurret{gWeaponInfo, "nTurretIndex"};
ue3::FieldRef gVelocity{"Engine.Actor", "Velocity"};
ue3::FieldRef gAnimations{"Engine.SkeletalMeshComponent", "Animations"};
ue3::FieldRef gSkelControlLists{"Engine.AnimTree", "SkelControlLists"};
ue3::FieldRef gControlHead{gSkelControlLists, "ControlHead"};
ue3::FieldRef gNextControl{"Engine.SkelControlBase", "NextControl"};
ue3::FieldRef gControlName{"Engine.SkelControlBase", "ControlName"};
ue3::FieldRef gDesiredBoneRotation{"SFXGame.BioSkelControl_TurretConstrained", "DesiredBoneRotation"};
ue3::FieldRef gConstrainYaw{"SFXGame.BioSkelControl_TurretConstrained", "bConstrainYaw"};
ue3::FieldRef gConstrainPitch{"SFXGame.BioSkelControl_TurretConstrained", "bConstrainPitch"};
ue3::FunctionRef gSetTurretAim{"SFXGame.BioVehicleBehaviorBase.SetDesiredTurretAimPoint"};
ue3::FunctionRef gVehicleTick{"SFXGame.BioVehicleWheeled.Tick"};
ue3::FunctionRef gVehicleStartTrace{"SFXGame.BioVehicleWheeled.GetWeaponStartTraceLocation"};
ue3::FieldRef gVehicleStartReturn{gVehicleStartTrace, "ReturnValue"};
ue3::FunctionRef gTrace{"Engine.Actor.Trace"};
#endif

enum class Why {
    NotInMako,      // on foot, a wheel, a menu
    Off,            // [Mako] handAim = 0
    NoView,         // no head-tracked world view
    MainMenu,       // our VR main menu is open
    NoControllers,  // the right controller isn't tracked
    NoAnchor,       // no view anchor (the host's view hasn't mapped tracking space yet)
    Aiming,
};

char const* Name(Why why) {
    switch (why) {
    case Why::NotInMako: return "not in the Mako";
    case Why::Off: return "the game aims the turret ([Mako] handAim = 0)";
    case Why::NoView: return "the game aims the turret (no head-tracked world view)";
    case Why::MainMenu: return "the game aims the turret (our menu is open)";
    case Why::NoControllers: return "the game aims the turret (the right controller isn't tracked)";
    case Why::NoAnchor: return "the game aims the turret (no view anchor yet)";
    case Why::Aiming: return "the right controller aims the turret";
    }
    return "?";
}

enum class Seam { ModTick = 0, PawnTick = 1 };

// This tick's aim: the controller's ray in the world and in tracking space, and what it hits.
struct Aim {
    bool valid = false;
    ULONGLONG atMs = 0;
    engine::UnrealVector origin;     // the controller (or the calibrated line's point) in the world
    engine::UnrealVector direction;  // unit
    engine::UnrealVector target;     // what the ray hits, or its end
    double distanceUnits = 0.0;
    bool hit = false;
    std::string what;
    XrVector3f trackOrigin{};
    XrVector3f trackDirection{};
    bool calibrated = false;
};

// The shot's start, from the vehicle's GetWeaponStartTraceLocation (seen just before each shot's aim query).
struct Start {
    bool valid = false;
    ULONGLONG atMs = 0;
    engine::UnrealVector at;
};

// One BioSkelControl_TurretConstrained of the vehicle's AnimTree: what SetDesiredTurretAimPoint wrote into it for our
// point, and whether the next look found it unchanged.
struct TurretControl {
    ue3::Object* control = nullptr;
    std::string name;
    bool yaw = false;
    bool pitch = false;
    bool oursValid = false;
    ue3::Rotator ours;
    Seam oursSeam = Seam::ModTick;
    ue3::Rotator seen;  // the latest read (diagnostics)
};

hand_calibration::Mako gSettings;
Why gWhy = Why::NotInMako;
Why gLoggedWhy = Why::NotInMako;
bool gWhyLogged = false;
double gWhyLoggedAt = -1e9;
ue3::Object* gVehicle = nullptr;
ue3::Object* gVehicleBehavior = nullptr;
std::string gVehicleName;
Aim gAim;
Start gStart;
vr::comfort::VehicleMotion gMotion;
Motion gMotionNow;
vr::comfort::VehicleMotionResult gMotionResult;
std::array<int, kMaxTurrets> gTurretIndices{};
int gTurretCount = 0;
std::array<TurretControl, kMaxTurrets> gControls{};
int gControlCount = 0;
bool gLoggedControls = false;
bool gLoggedFirstAim = false;
bool gLoggedFirstTurret = false;
bool gLoggedNoTurretFn = false;
bool gLoggedNoBehavior = false;
bool gLoggedNoInfos = false;
bool gLoggedNoTrace = false;
bool gLoggedTracePath = false;
bool gLoggedFirstShot = false;
bool gLoggedPawnTickMissing = false;
bool gTurretPublished = false;
bool gVehicleTickHandled = false;
bool gStartTraceHandled = false;
unsigned gShotsLogged = 0;
ULONGLONG gNextShotLogMs = 0;
ULONGLONG gNextOverwriteWarnMs = 0;
ULONGLONG gLastPawnTickMs = 0;
unsigned long long gShotsTotal = 0;
std::atomic<unsigned> gTurretCallsTick{0};
std::atomic<unsigned> gTurretCallsPawn{0};
std::atomic<unsigned> gTurretCallsRefused{0};
std::atomic<unsigned> gPawnTicks{0};
std::atomic<unsigned> gStartsSeen{0};

// Since the heartbeat.
struct Counts {
    unsigned aims = 0;
    unsigned traces = 0;
    unsigned hits = 0;
    unsigned shots = 0;
    unsigned shotsFromStart = 0;
    // Checked where the next seam runs: the mod tick's value against the world tick up to the vehicle's Tick (both seams
    // on; or the whole world tick with the vehicle's seam off), the pawn-tick value against the rest of the world tick.
    unsigned heldAfterTick = 0;     // our mod-tick value still there at the next check
    unsigned changedAfterTick = 0;  // ...or re-aimed by the game in between (expected if it aims every frame)
    unsigned heldAfterPawn = 0;     // our pawn-tick value still there at the end of the world tick
    unsigned changedAfterPawn = 0;  // ...or something after the vehicle's Tick re-aimed it (the pose seam then holds ours)
    unsigned motionTicks = 0;
    unsigned movingTicks = 0;
    unsigned turningTicks = 0;
} gCounts;

double Now() {
    static LARGE_INTEGER const frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

void LogWhy(Why why, double now) {
    gWhy = why;
    if (gWhyLogged && why == gLoggedWhy) {
        return;
    }
    if (gWhyLogged && now - gWhyLoggedAt < kWhyLogSeconds && why != Why::Aiming && gLoggedWhy != Why::Aiming) {
        return;  // flapping between reasons settles in a line
    }
    gWhyLogged = true;
    gLoggedWhy = why;
    gWhyLoggedAt = now;
    log::Info("mako: {}", Name(why));
}

#if defined(MEVR_GAME_LE1)

engine::UnrealVector Sub(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
engine::UnrealVector Add(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
engine::UnrealVector Times(engine::UnrealVector const& a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
double Dot(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double Len(engine::UnrealVector const& a) {
    return std::sqrt(Dot(a, a));
}
int SignedUnits(int units) {
    int const u = units & 0xFFFF;
    return u >= 0x8000 ? u - 0x10000 : u;
}

// Actor.Trace on the vehicle (which it ignores), directly through ProcessEvent with the function's own parameter
// block: the actor hit and, if the native's out parameter came back, where (as game/sights does).
ue3::Object* TraceOnce(ue3::Object* vehicle, ue3::Function* trace, engine::UnrealVector const& start, engine::UnrealVector const& end,
    engine::UnrealVector* hit) {
    ue3::Params parms(ue3::runtime::Get(), trace);
    parms.Set("TraceEnd", ue3::Vector{static_cast<float>(end.x), static_cast<float>(end.y), static_cast<float>(end.z)});
    parms.Set("TraceStart", ue3::Vector{static_cast<float>(start.x), static_cast<float>(start.y), static_cast<float>(start.z)});
    parms.SetBool("bTraceActors", true);
    ue3::runtime::Call(vehicle, trace, parms);
    if (hit != nullptr) {
        ue3::Vector const at = parms.Get<ue3::Vector>("HitLocation").value_or(ue3::Vector{});
        *hit = {at.x, at.y, at.z};
    }
    return parms.Return<ue3::Object*>().value_or(nullptr);
}

// The world along the controller's ray: the aim point is what it hits within the range, else the range's end.
void TraceAim(ue3::Object* vehicle, Aim& aim, double rangeUnits) {
    aim.target = Add(aim.origin, Times(aim.direction, rangeUnits));
    aim.distanceUnits = rangeUnits;
    aim.hit = false;
    aim.what.clear();
    ue3::Function* const trace = gTrace.Get();
    if (trace == nullptr) {
        if (!gLoggedNoTrace) {
            gLoggedNoTrace = true;
            log::Warn("mako: Actor.Trace not found: the turret aims at the ray's end ({:.0f} m out), not at what it hits",
                rangeUnits / engine::kUnitsPerMeter);
        }
        return;
    }
    ++gCounts.traces;
    engine::UnrealVector at{};
    ue3::Object* actor = TraceOnce(vehicle, trace, aim.origin, aim.target, &at);
    if (actor == nullptr) {
        return;
    }
    double const along = Dot(Sub(at, aim.origin), aim.direction);
    bool const plausible = (at.x != 0.0 || at.y != 0.0 || at.z != 0.0) && along >= 0.0 && along <= rangeUnits + 1.0 &&
        Len(Sub(at, Add(aim.origin, Times(aim.direction, along)))) < 10.0;
    double distance = along;
    if (!plausible) {
        // The hit point didn't come back (a native's out parameter): bisect with the hit/no-hit answer.
        double lo = 0.0;
        double hi = 1.0;
        for (int i = 0; i < 8; ++i) {
            double const mid = 0.5 * (lo + hi);
            if (ue3::Object* const a = TraceOnce(vehicle, trace, aim.origin, Add(aim.origin, Times(aim.direction, rangeUnits * mid)), nullptr)) {
                hi = mid;
                actor = a;
            } else {
                lo = mid;
            }
        }
        distance = rangeUnits * hi;
    }
    aim.hit = true;
    aim.distanceUnits = distance;
    aim.target = Add(aim.origin, Times(aim.direction, distance));
    aim.what = ue3::runtime::FullName(actor);
    ++gCounts.hits;
    if (!gLoggedTracePath) {
        gLoggedTracePath = true;
        log::Info("mako: the aim ray's trace (Actor.Trace on the vehicle through ProcessEvent): the hit point {}",
            plausible ? "came back" : "didn't come back (a native's out parameter): found by bisecting with the hit/no-hit answer");
    }
}

ue3::Object* Behavior(ue3::Object* vehicle) {
    ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(vehicle).value_or(nullptr);
    if (behavior == nullptr || !ue3::runtime::IsAlive(behavior) || !ue3::IsA(behavior, gBehaviorBase)) {
        return nullptr;
    }
    return behavior;
}

// The turrets the vehicle's weapons sit on (BioVehicleWeaponInfo.nTurretIndex of each weapon present), or turret 0.
void FindTurretIndices(ue3::Object* behavior) {
    gTurretCount = 0;
    if (auto const infos = gWeaponInfo.ElementsOf(behavior, 16)) {
        for (std::int32_t i = 0; i < infos->count && gTurretCount < kMaxTurrets; ++i) {
            void* const info = infos->At(i);
            ue3::Object* const weapon = gInfoWeapon.Read<ue3::Object*>(info).value_or(nullptr);
            std::optional<std::int32_t> const index = gInfoTurret.Read<std::int32_t>(info);
            if (weapon == nullptr || !index || *index < 0 || *index > 15) {
                continue;
            }
            bool known = false;
            for (int k = 0; k < gTurretCount; ++k) {
                known = known || gTurretIndices[static_cast<std::size_t>(k)] == *index;
            }
            if (!known) {
                gTurretIndices[static_cast<std::size_t>(gTurretCount++)] = *index;
            }
        }
    }
    if (gTurretCount == 0) {
        gTurretIndices[0] = 0;
        gTurretCount = 1;
        if (!gLoggedNoInfos) {
            gLoggedNoInfos = true;
            log::Info("mako: no vehicle weapon info with a weapon on the behavior yet: aiming turret 0");
        }
    }
}

// The turret's skeletal controls in the vehicle mesh's AnimTree (BioSkelControl_TurretConstrained), found each tick
// (cheap, and a control that went away is never read).
void FindTurretControls(ue3::Object* vehicle) {
    int count = 0;
    std::array<TurretControl, kMaxTurrets> found{};
    ue3::Object* const mesh = player::Mesh(vehicle);
    ue3::Object* const tree = mesh != nullptr ? gAnimations.Read<ue3::Object*>(mesh).value_or(nullptr) : nullptr;
    if (tree != nullptr && ue3::IsA(tree, gAnimTree)) {
        if (auto const lists = gSkelControlLists.ElementsOf(tree)) {
            for (std::int32_t i = 0; i < lists->count && count < kMaxTurrets; ++i) {
                int depth = 0;
                for (ue3::Object* control = gControlHead.Read<ue3::Object*>(lists->At(i)).value_or(nullptr);
                     control != nullptr && depth < kMaxControlsPerList && count < kMaxTurrets;
                     control = gNextControl.Read<ue3::Object*>(control).value_or(nullptr), ++depth) {
                    if (!ue3::IsA(control, gTurretControl)) {
                        continue;
                    }
                    TurretControl& t = found[static_cast<std::size_t>(count++)];
                    t.control = control;
                    t.name = ue3::runtime::NameText(gControlName.Read<ue3::Name>(control).value_or(ue3::Name{}));
                    t.yaw = gConstrainYaw.ReadBool(control).value_or(false);
                    t.pitch = gConstrainPitch.ReadBool(control).value_or(false);
                    t.seen = gDesiredBoneRotation.Read<ue3::Rotator>(control).value_or(ue3::Rotator{});
                }
            }
        }
    }
    // Keep what we wrote into controls still there (the held/changed check needs it).
    for (int i = 0; i < count; ++i) {
        for (int k = 0; k < gControlCount; ++k) {
            TurretControl const& old = gControls[static_cast<std::size_t>(k)];
            if (old.control == found[static_cast<std::size_t>(i)].control && old.oursValid) {
                found[static_cast<std::size_t>(i)].oursValid = true;
                found[static_cast<std::size_t>(i)].ours = old.ours;
                found[static_cast<std::size_t>(i)].oursSeam = old.oursSeam;
            }
        }
    }
    gControls = found;
    gControlCount = count;
    if (!gLoggedControls && count > 0) {
        gLoggedControls = true;
        std::string names;
        for (int i = 0; i < count; ++i) {
            TurretControl const& t = gControls[static_cast<std::size_t>(i)];
            names += std::format("{}'{}' ({}{}{}; desired pitch {} yaw {})", i > 0 ? ", " : "", t.name, t.yaw ? "yaw" : "",
                t.yaw && t.pitch ? " and " : "", t.pitch ? "pitch" : "", t.seen.pitch, t.seen.yaw);
        }
        log::Info("mako: the vehicle's turret controls (BioSkelControl_TurretConstrained in its AnimTree): {}", names);
    }
}

// Before this tick's calls: did what we last wrote into each control survive the world tick since?
void CheckTurretHeld() {
    for (int i = 0; i < gControlCount; ++i) {
        TurretControl& t = gControls[static_cast<std::size_t>(i)];
        if (!t.oursValid) {
            continue;
        }
        bool const held = std::abs(SignedUnits(t.seen.pitch - t.ours.pitch)) <= kHeldUnits &&
            std::abs(SignedUnits(t.seen.yaw - t.ours.yaw)) <= kHeldUnits;
        if (t.oursSeam == Seam::PawnTick) {
            (held ? gCounts.heldAfterPawn : gCounts.changedAfterPawn) += 1;
        } else {
            (held ? gCounts.heldAfterTick : gCounts.changedAfterTick) += 1;
        }
        t.oursValid = false;
    }
}

// The game's own turret aim, for our point, on every turret the weapons sit on; then what it wrote into the
// controls is remembered (the held check, and the pose seam re-asserts it before the vehicle's pose).
void AimTurrets(ue3::Object* behavior, Seam seam) {
    ue3::Function* const fn = gSetTurretAim.Get();
    if (fn == nullptr) {
        if (!gLoggedNoTurretFn) {
            gLoggedNoTurretFn = true;
            log::Warn("mako: BioVehicleBehaviorBase.SetDesiredTurretAimPoint not found: the turret model keeps the game's aim "
                      "(shots still follow the controller)");
        }
        return;
    }
    ue3::Vector const target{static_cast<float>(gAim.target.x), static_cast<float>(gAim.target.y), static_cast<float>(gAim.target.z)};
    for (int i = 0; i < gTurretCount; ++i) {
        ue3::Params parms(ue3::runtime::Get(), fn);
        parms.Set("nTurretIndex", gTurretIndices[static_cast<std::size_t>(i)]);
        parms.Set("vTargetLocation", target);
        bool const ok = ue3::runtime::Call(behavior, fn, parms);
        (seam == Seam::PawnTick ? gTurretCallsPawn : gTurretCallsTick).fetch_add(1, std::memory_order_relaxed);
        if (!ok) {
            gTurretCallsRefused.fetch_add(1, std::memory_order_relaxed);
        }
        if (!gLoggedFirstTurret) {
            gLoggedFirstTurret = true;
            ue3::Rotator const returned = parms.Return<ue3::Rotator>().value_or(ue3::Rotator{});
            log::Info("mako: first turret aim: SetDesiredTurretAimPoint(turret {}, ({:.0f}, {:.0f}, {:.0f})) {} and returned pitch {} yaw "
                      "{} roll {} ({} turret{}; from {})",
                gTurretIndices[static_cast<std::size_t>(i)], gAim.target.x, gAim.target.y, gAim.target.z, ok ? "ran" : "was refused",
                returned.pitch, returned.yaw, returned.roll, gTurretCount, gTurretCount == 1 ? "" : "s",
                seam == Seam::PawnTick ? "the vehicle's Tick" : "the mod's tick");
        }
    }
    // What the native wrote into the controls: ours to hold.
    engine::bone_pose::Turret publish;
    ue3::Object* const mesh = player::Mesh(gVehicle);
    for (int i = 0; i < gControlCount; ++i) {
        TurretControl& t = gControls[static_cast<std::size_t>(i)];
        if (auto const desired = gDesiredBoneRotation.Read<ue3::Rotator>(t.control)) {
            t.ours = *desired;
            t.seen = *desired;
            t.oursValid = true;
            t.oursSeam = seam;
            if (publish.count < static_cast<int>(publish.controls.size())) {
                publish.controls[static_cast<std::size_t>(publish.count++)] = {t.control, desired->pitch, desired->yaw, desired->roll};
            }
        }
    }
    // The pose seam (research/44): re-asserted right before UpdateSkelPose composes the vehicle's pose.
    bool const poseSeam = (gSettings.turretSeam & 4) != 0 && engine::bone_pose::Installed();
    if (poseSeam && publish.count > 0 && mesh != nullptr) {
        ue3::PropertyInfo const* const animations = gAnimations.Get();
        ue3::PropertyInfo const* const rotation = gDesiredBoneRotation.Get();
        if (animations != nullptr && rotation != nullptr) {
            publish.mesh = mesh;
            publish.tree = gAnimations.Read<void*>(mesh).value_or(nullptr);
            publish.animationsOffset = static_cast<std::uint32_t>(animations->offset);
            publish.rotationOffset = static_cast<std::uint32_t>(rotation->offset);
            engine::bone_pose::PublishTurret(publish);
            gTurretPublished = true;
        }
    } else if (gTurretPublished) {
        engine::bone_pose::ClearTurret();
        gTurretPublished = false;
    }
}

void ForgetVehicle() {
    if (gTurretPublished) {
        engine::bone_pose::ClearTurret();
        gTurretPublished = false;
    }
    gVehicle = nullptr;
    gVehicleBehavior = nullptr;
    gVehicleName.clear();
    gTurretCount = 0;
    gControlCount = 0;
    gControls = {};
    gLoggedControls = false;
    gLoggedNoInfos = false;
    gStart = {};
    gMotion.Reset();
    gMotionNow = {};
    gMotionResult = {};
}

// Inside the world tick, after the vehicle's own Tick: the same aim, so it lands after whatever the vehicle's tick did.
void OnVehicleTick(ue3::Object* context, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (context == nullptr || context != gVehicle) {
        return;
    }
    gPawnTicks.fetch_add(1, std::memory_order_relaxed);
    gLastPawnTickMs = GetTickCount64();
    if (!gSettings.turret || (gSettings.turretSeam & 2) == 0 || !gAim.valid || GetTickCount64() - gAim.atMs > kAimFreshMs ||
        gVehicleBehavior == nullptr || !ue3::runtime::IsAlive(gVehicleBehavior)) {
        return;
    }
    // Did the mod tick's value survive the world tick up to here (the vehicle's own tick included)?
    FindTurretControls(gVehicle);
    CheckTurretHeld();
    AimTurrets(gVehicleBehavior, Seam::PawnTick);
}

// The vehicle's answer for where a shot starts (its native, asked through ProcessEvent just before the shot's aim
// query): remembered, so the shot's direction can run from there toward the aim point. Not rewritten.
void OnVehicleStartTrace(ue3::Object* context, ue3::Function* /*function*/, void* parms, void* /*result*/) {
    if (context == nullptr || context != gVehicle) {
        return;
    }
    ue3::Vector const* const at = gVehicleStartReturn.In<ue3::Vector>(parms);
    if (at == nullptr) {
        return;
    }
    gStart.valid = true;
    gStart.atMs = GetTickCount64();
    gStart.at = {at->x, at->y, at->z};
    gStartsSeen.fetch_add(1, std::memory_order_relaxed);
}

void LogShot(engine::UnrealVector const& start, bool fromStart, double offDegrees) {
    ULONGLONG const now = GetTickCount64();
    if (gShotsLogged >= kShotLines && now < gNextShotLogMs) {
        return;
    }
    ++gShotsLogged;
    gNextShotLogMs = now + 2000;
    log::Info("mako: shot {}: from {} ({:.0f}, {:.0f}, {:.0f}) toward {} {:.1f} m out{}; {:.2f} degrees off the controller's ray",
        gShotsTotal, fromStart ? "the gun's start" : "the controller (no shot start seen)", start.x, start.y, start.z,
        gAim.hit ? gAim.what : std::string("the ray's end"), gAim.distanceUnits / engine::kUnitsPerMeter,
        gAim.hit ? "" : " (nothing hit within the range)", offDegrees);
}

#endif

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    if (!ue3::runtime::Ready()) {
        return false;
    }
    if (ue3::Function* const tick = gVehicleTick.Get()) {
        engine::process_event::AddPostHandler(tick, &OnVehicleTick);
        gVehicleTickHandled = true;
    } else {
        log::Warn("mako: BioVehicleWheeled.Tick not found: the turret is aimed from the mod's tick only");
    }
    if (ue3::Function* const start = gVehicleStartTrace.Get()) {
        engine::process_event::AddPostHandler(start, &OnVehicleStartTrace);
        gStartTraceHandled = true;
    } else {
        log::Warn("mako: BioVehicleWheeled.GetWeaponStartTraceLocation not found: shots in the Mako run parallel to the controller's "
                  "ray from the game's own start");
    }
    return true;
#else
    return false;
#endif
}

void Tick(double deltaSeconds, Link const& link) {
    double const now = Now();
    gSettings = hand_calibration::GetMako();
#if defined(MEVR_GAME_LE1)
    ue3::Object* const controller = player::Controller();
    std::optional<unsigned char> const mode = head_tracking::GameMode();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    bool const inVehicle = mode && *mode == kGameModeVehicle && pawn != nullptr && ue3::IsA(pawn, gVehicleBase);
    if (!inVehicle) {
        if (gVehicle != nullptr) {
            log::Info("mako: left {}", gVehicleName);
            ForgetVehicle();
        }
        gAim.valid = false;
        LogWhy(Why::NotInMako, now);
        return;
    }
    if (pawn != gVehicle) {
        ForgetVehicle();
        gVehicle = pawn;
        gVehicleName = ue3::runtime::FullName(pawn);
        gVehicleBehavior = Behavior(pawn);
        if (gVehicleBehavior != nullptr) {
            FindTurretIndices(gVehicleBehavior);
        } else if (!gLoggedNoBehavior) {
            gLoggedNoBehavior = true;
            log::Warn("mako: the vehicle has no BioVehicleBehaviorBase (m_oBehavior): the turret model keeps the game's aim");
        }
        FindTurretControls(pawn);
        log::Info("mako: driving {} (behavior {}, {} turret{} from the weapons' info, {} turret control{} in the AnimTree, the vehicle's "
                  "Tick {}, its shot start {})",
            gVehicleName, gVehicleBehavior != nullptr ? "found" : "missing", gTurretCount, gTurretCount == 1 ? "" : "s", gControlCount,
            gControlCount == 1 ? "" : "s", gVehicleTickHandled ? "hooked" : "not hooked", gStartTraceHandled ? "hooked" : "not hooked");
    }

    // The vehicle's motion, for the vignette (game/comfort reads CurrentMotion()).
    {
        ue3::Vector const velocity = gVelocity.Read<ue3::Vector>(pawn).value_or(ue3::Vector{});
        ue3::Rotator const rotation = player::Rotation(pawn).value_or(ue3::Rotator{});
        vr::comfort::VehicleThresholds thresholds;
        thresholds.moveSpeed = gSettings.moveSpeed;
        thresholds.turnRate = gSettings.turnRate;
        gMotionResult = gMotion.Step(velocity.x, velocity.y, velocity.z, rotation.yaw, deltaSeconds, thresholds);
        gMotionNow.inVehicle = true;
        gMotionNow.wanted = gSettings.vignette;
        gMotionNow.moving = gMotionResult.moving;
        gMotionNow.turning = gMotionResult.turning;
        ++gCounts.motionTicks;
        gCounts.movingTicks += gMotionResult.moving ? 1u : 0u;
        gCounts.turningTicks += gMotionResult.turning ? 1u : 0u;
    }

    // The turret controls as the world tick left them, against what we last wrote.
    FindTurretControls(pawn);
    CheckTurretHeld();
    if (gVehicleBehavior != nullptr && gTurretCount <= 1) {
        FindTurretIndices(gVehicleBehavior);  // the weapons arrive a moment after the vehicle
    }
    if (gVehicleTickHandled && !gLoggedPawnTickMissing && gLastPawnTickMs == 0 && gCounts.motionTicks > 300) {
        gLoggedPawnTickMissing = true;
        log::Warn("mako: the vehicle's Tick hasn't been seen in {} ticks (another class's Tick, or not an event here): the turret is "
                  "aimed from the mod's tick and the pose seam only",
            gCounts.motionTicks);
    }

    // The aim.
    Why why = Why::Aiming;
    vr::controllers::State const controllers = vr::controllers::Latest();
    std::optional<engine::TrackingFrame> frame;
    if (!gSettings.handAim) {
        why = Why::Off;
    } else if (!link.headTrackedView) {
        why = Why::NoView;
    } else if (link.mainMenuOpen) {
        why = Why::MainMenu;
    } else if (!controllers.active || !controllers.right.aimValid) {
        why = Why::NoControllers;
    } else if (!(frame = view_anchor::Current())) {
        why = Why::NoAnchor;
    }
    if (why != Why::Aiming) {
        gAim.valid = false;
        if (gTurretPublished) {
            engine::bone_pose::ClearTurret();
            gTurretPublished = false;
        }
        LogWhy(why, now);
        return;
    }
    XrPosef aim = controllers.right.aim;
    bool calibrated = false;
    if (gSettings.calibratedLine && controllers.right.gripValid) {
        // The guided calibration's pistol line (research/25): where the player's grip points, not the runtime's aim pose.
        if (auto const stance = body_calibration::StanceFor(static_cast<int>(weapon::RangedType::Pistol))) {
            aim.position = vr::calibration::SightLine(controllers.right.grip, *stance).point;
            aim.orientation = vr::calibration::GunOrientation(controllers.right.grip.orientation, *stance);
            calibrated = true;
        }
    }
    Aim next;
    next.valid = true;
    next.atMs = GetTickCount64();
    next.calibrated = calibrated;
    next.origin = engine::ToWorld(*frame, aim.position);
    next.direction = engine::OrientationToWorld(*frame, aim.orientation).forward;
    double const length = Len(next.direction);
    if (!(length > 0.0) || !std::isfinite(next.origin.x) || !std::isfinite(next.origin.y) || !std::isfinite(next.origin.z)) {
        gAim.valid = false;
        LogWhy(Why::NoAnchor, now);
        return;
    }
    next.direction = Times(next.direction, 1.0 / length);
    next.trackOrigin = aim.position;
    next.trackDirection = engine::DirectionToTracking(*frame, next.direction);
    TraceAim(pawn, next, static_cast<double>(gSettings.aimRangeM) * engine::kUnitsPerMeter);
    gAim = next;
    ++gCounts.aims;
    if (!gLoggedFirstAim) {
        gLoggedFirstAim = true;
        log::Info("mako: first aim: the right controller{} at ({:.0f}, {:.0f}, {:.0f}) points ({:.3f}, {:.3f}, {:.3f}); the ray {} {:.1f} m out",
            calibrated ? " (the pistol calibration's line)" : "", gAim.origin.x, gAim.origin.y, gAim.origin.z, gAim.direction.x,
            gAim.direction.y, gAim.direction.z, gAim.hit ? std::format("hits {}", gAim.what) : std::string("hits nothing; its end"),
            gAim.distanceUnits / engine::kUnitsPerMeter);
    }
    // The turret model, from the mod's tick (after the world tick; the pose seam carries it into the next frame's pose).
    if (gSettings.turret && (gSettings.turretSeam & 1) != 0 && gVehicleBehavior != nullptr && ue3::runtime::IsAlive(gVehicleBehavior)) {
        AimTurrets(gVehicleBehavior, Seam::ModTick);
    } else if (gTurretPublished && (!gSettings.turret || (gSettings.turretSeam & 4) == 0)) {
        engine::bone_pose::ClearTurret();
        gTurretPublished = false;
    }
    LogWhy(why, now);
#else
    (void)deltaSeconds;
    (void)link;
    LogWhy(Why::NotInMako, now);
#endif
}

Motion CurrentMotion() {
    return gMotionNow;
}

bool ShotRotation(ue3::Rotator const& game, ue3::Rotator& out) {
#if defined(MEVR_GAME_LE1)
    ULONGLONG const now = GetTickCount64();
    if (!gAim.valid || now - gAim.atMs > kAimFreshMs || gVehicle == nullptr) {
        return false;
    }
    bool const fromStart = gStart.valid && now - gStart.atMs <= kStartFreshMs;
    engine::UnrealVector const start = fromStart ? gStart.at : gAim.origin;
    engine::UnrealVector direction = Sub(gAim.target, start);
    double const length = Len(direction);
    if (!(length > 1.0)) {
        direction = gAim.direction;  // the aim point is at the start itself: along the ray
    } else {
        direction = Times(direction, 1.0 / length);
    }
    engine::UnrealRotation const rotation = engine::ToRotation(direction);
    out.pitch = engine::Unwind(rotation.pitch, game.pitch);
    out.yaw = engine::Unwind(rotation.yaw, game.yaw);
    out.roll = 0;
    ++gShotsTotal;
    ++gCounts.shots;
    gCounts.shotsFromStart += fromStart ? 1u : 0u;
    double const off = std::acos(std::clamp(Dot(direction, gAim.direction), -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
    if (!gLoggedFirstShot) {
        gLoggedFirstShot = true;
        log::Info("mako: first shot aimed by the right controller: game aim pitch {} yaw {} -> pitch {} yaw {} ({})", game.pitch, game.yaw,
            out.pitch, out.yaw, fromStart ? "from the gun's start toward the aim point" : "along the controller's ray");
    }
    LogShot(start, fromStart, off);
    return true;
#else
    (void)game;
    (void)out;
    return false;
#endif
}

std::optional<vr::aim_ray::Ray> AimRay() {
    if (!gAim.valid || !gSettings.reticle || GetTickCount64() - gAim.atMs > kAimFreshMs) {
        return std::nullopt;
    }
    vr::aim_ray::Ray ray;
    ray.valid = true;
    ray.origin = gAim.trackOrigin;
    ray.direction = gAim.trackDirection;
    return ray;
}

std::string StatusJson() {
    std::string controls = "[";
    for (int i = 0; i < gControlCount; ++i) {
        auto const& t = gControls[static_cast<std::size_t>(i)];
        controls += std::format("{}{{\"name\": {}, \"yaw\": {}, \"pitch\": {}, \"desired\": [{}, {}], \"ours\": {}}}", i > 0 ? ", " : "",
            json::JsonString(t.name), t.yaw ? "true" : "false", t.pitch ? "true" : "false", t.seen.pitch, t.seen.yaw,
            t.oursValid ? std::format("[{}, {}]", t.ours.pitch, t.ours.yaw) : std::string("null"));
    }
    controls += "]";
    std::string indices = "[";
    for (int i = 0; i < gTurretCount; ++i) {
        indices += std::format("{}{}", i > 0 ? ", " : "", gTurretIndices[static_cast<std::size_t>(i)]);
    }
    indices += "]";
    engine::bone_pose::TurretStats const pose = engine::bone_pose::GetTurretStats();
    return std::format(
        "{{\"inVehicle\": {}, \"vehicle\": {}, \"why\": {}, \"aim\": {{\"valid\": {}, \"calibrated\": {}, \"hit\": {}, \"what\": {}, "
        "\"distanceM\": {}, \"target\": [{}, {}, {}], \"direction\": [{}, {}, {}]}}, \"shots\": {}, \"turret\": {{\"indices\": {}, "
        "\"seam\": {}, \"callsTick\": {}, \"callsPawnTick\": {}, \"refused\": {}, \"pawnTicks\": {}, \"heldAfterTick\": {}, "
        "\"changedAfterTick\": {}, \"heldAfterPawn\": {}, \"changedAfterPawn\": {}, \"poseWrites\": {}, \"poseRejected\": {}, "
        "\"controls\": {}}}, \"motion\": {{\"moving\": {}, \"turning\": {}, \"speed\": {}, \"yawRate\": {}}}}}",
        gMotionNow.inVehicle ? "true" : "false", json::JsonString(gVehicleName), json::JsonString(Name(gWhy)), gAim.valid ? "true" : "false",
        gAim.calibrated ? "true" : "false", gAim.hit ? "true" : "false", json::JsonString(gAim.what),
        json::JsonNumber(gAim.distanceUnits / engine::kUnitsPerMeter), json::JsonNumber(gAim.target.x), json::JsonNumber(gAim.target.y),
        json::JsonNumber(gAim.target.z), json::JsonNumber(gAim.direction.x), json::JsonNumber(gAim.direction.y),
        json::JsonNumber(gAim.direction.z), gShotsTotal, indices, gSettings.turretSeam, gTurretCallsTick.load(), gTurretCallsPawn.load(),
        gTurretCallsRefused.load(), gPawnTicks.load(), gCounts.heldAfterTick, gCounts.changedAfterTick, gCounts.heldAfterPawn,
        gCounts.changedAfterPawn, pose.written, pose.rejected, controls, gMotionNow.moving ? "true" : "false",
        gMotionNow.turning ? "true" : "false", json::JsonNumber(gMotionResult.speed), json::JsonNumber(gMotionResult.yawRate));
}

void LogStats() {
    if (!gMotionNow.inVehicle && gCounts.aims == 0 && gCounts.shots == 0) {
        return;
    }
    engine::bone_pose::TurretStats const pose = engine::bone_pose::GetTurretStats();
    log::Info("    mako: {}; last 5 s: {} aims ({} traces, {} hit), {} shots ({} from the gun's start); turret aim calls: {} from the mod's "
              "tick, {} after the vehicle's Tick ({} vehicle ticks seen, {} refused); our aim held through the world tick {} times and "
              "changed {} (mod tick), held {} and changed {} (vehicle tick); pose seam {} writes, {} rejected{}; moving in {} of {} ticks, "
              "turning in {}",
        Name(gWhy), gCounts.aims, gCounts.traces, gCounts.hits, gCounts.shots, gCounts.shotsFromStart, gTurretCallsTick.exchange(0),
        gTurretCallsPawn.exchange(0), gPawnTicks.exchange(0), gTurretCallsRefused.exchange(0), gCounts.heldAfterTick,
        gCounts.changedAfterTick, gCounts.heldAfterPawn, gCounts.changedAfterPawn, pose.written, pose.rejected,
        pose.lastReject != nullptr ? std::format(" (last: {})", pose.lastReject) : std::string(), gCounts.movingTicks, gCounts.motionTicks,
        gCounts.turningTicks);
    // The game re-aims the turret after the vehicle's own Tick (the turret may follow the camera where no pose seam holds ours).
    ULONGLONG const now = GetTickCount64();
    if (gCounts.changedAfterPawn > 2 * (gCounts.heldAfterPawn + 1) && now >= gNextOverwriteWarnMs) {
        gNextOverwriteWarnMs = now + 30000;
        log::Warn("mako: the game re-aimed the turret after our call in the vehicle's Tick in {} of {} checks: without the pose seam "
                  "([Mako] turretSeam bit 4, arm IK's UpdateSkelPose hook) the turret model would follow the camera, not the hand "
                  "[verify in the headset: does the cannon turn with the controller?]",
            gCounts.changedAfterPawn, gCounts.changedAfterPawn + gCounts.heldAfterPawn);
    }
    gCounts = Counts{};
}

void Shutdown() {
#if defined(MEVR_GAME_LE1)
    ForgetVehicle();
#endif
    gSettings = {};
    gWhy = gLoggedWhy = Why::NotInMako;
    gWhyLogged = false;
    gWhyLoggedAt = -1e9;
    gAim = {};
    gLoggedFirstAim = gLoggedFirstTurret = gLoggedNoTurretFn = gLoggedNoBehavior = gLoggedNoTrace = gLoggedTracePath = false;
    gLoggedFirstShot = gLoggedPawnTickMissing = false;
    gVehicleTickHandled = gStartTraceHandled = false;
    gShotsLogged = 0;
    gNextShotLogMs = 0;
    gNextOverwriteWarnMs = 0;
    gLastPawnTickMs = 0;
    gShotsTotal = 0;
    gTurretCallsTick = 0;
    gTurretCallsPawn = 0;
    gTurretCallsRefused = 0;
    gPawnTicks = 0;
    gStartsSeen = 0;
    gCounts = Counts{};
}

} // namespace mevr::game::mako
