// SPDX-License-Identifier: MIT
#include "game/MakoGunner.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <vector>

namespace mevr::game::mako_gunner {
namespace {

namespace mg = vr::mako_gunner;

#if defined(MEVR_GAME_LE1)
ue3::ClassRef gGameModeVehicle{"SFXGame.SFXGameModeVehicle"};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList"};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn"};
ue3::FieldRef gDriver{"Engine.Vehicle", "Driver"};
ue3::FieldRef gPawnWeapon{"Engine.Pawn", "Weapon"};
ue3::FieldRef gCylinder{"Engine.Pawn", "CylinderComponent"};
ue3::FieldRef gCollisionRadius{"Engine.CylinderComponent", "CollisionRadius"};
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes"};
ue3::FunctionRef gIsHostile{"SFXGame.BioPawn.IsHostile"};
ue3::FunctionRef gIsDead{"SFXGame.BioPawn.IsDead"};
ue3::FunctionRef gFastTrace{"Engine.Actor.FastTrace"};
ue3::FunctionRef gMuzzleLoc{"SFXGame.BioWeaponRanged.GetMuzzleLoc"};
ue3::FunctionRef gFireWeapon{"SFXGame.SFXGameModeBase.FireWeapon"};
ue3::FunctionRef gStopFiringWeapon{"SFXGame.SFXGameModeBase.StopFiringWeapon"};
ue3::FunctionRef gFireCannon{"SFXGame.SFXGameModeVehicle.FireCannon"};
ue3::FunctionRef gStopFiringCannon{"SFXGame.SFXGameModeVehicle.StopFiringCannon"};
ue3::FieldRef gPawnBehavior{"SFXGame.BioPawn", "m_oBehavior"};
// A dormant enemy (a base's crew before its encounter starts) is hidden, without collision, never rendered: bullets pass through it
// (measured 2026-11-04: every hit on the terrain). The gunner never takes one.
ue3::FieldRef gHidden{"Engine.Actor", "bHidden"};
ue3::FieldRef gCollideActors{"Engine.Actor", "bCollideActors"};
ue3::FunctionRef gSetHidden{"Engine.Actor.SetHidden"};
ue3::FunctionRef gSetCollision{"Engine.Actor.SetCollision"};
ue3::FunctionRef gHealthAndShields{"SFXGame.BioEpicPawnBehavior.GetHealthAndShieldsScaled"};
#endif

constexpr double kScanEveryS = 0.25;
constexpr double kCannonHoldS = 0.25;
constexpr double kUnitsPerMeter = engine::kUnitsPerMeter;

struct Found {
    ue3::Object* pawn = nullptr;
    mg::Candidate candidate;
    std::string name;
    engine::UnrealVector at{};
};

struct State {
    mg::ControlState control;
    mg::FireState fire;
    bool started = false;
    std::chrono::steady_clock::time_point origin{};
    double lastScan = -1e9;
    std::vector<Found> found;     // the last scan's candidates
    ue3::Object* target = nullptr;
    std::string targetName;
    double targetDistanceM = 0.0;
    bool targetBig = false;
    bool targetCannon = false;   // the cannon for it: big or in a crowd
    int targetNeighbours = 0;
    int targetSerial = 0;         // a new target, a new id for the fire schedule
    ue3::Object* gameMode = nullptr;
    bool mgHeld = false;
    double cannonReleaseAt = -1.0;
    mg::Control loggedControl = mg::Control::Hand;
    bool loggedControlSet = false;
    // This heartbeat period.
    unsigned bursts = 0;
    unsigned cannons = 0;
    unsigned targetsTaken = 0;
    unsigned takeovers = 0;
    double gunnerSeconds = 0.0;
    double manualSeconds = 0.0;
    std::string why = "not riding";
    bool wasActive = false;
    // The last scan's counts: every pawn in the list, the BioPawns alive, within range, hostile.
    int scanPawns = 0;
    int scanDormant = 0;
    int scanAlive = 0;
    int scanInRange = 0;
    int scanHostile = 0;
};
State gState;

double Now() {
    auto const now = std::chrono::steady_clock::now();
    if (!gState.started) {
        gState.started = true;
        gState.origin = now;
    }
    return std::chrono::duration<double>(now - gState.origin).count();
}

#if defined(MEVR_GAME_LE1)
bool CallBool(ue3::Object* object, ue3::FunctionRef& function) {
    if (object == nullptr || function.Get() == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), function.Get());
    ue3::runtime::Call(object, function.Get(), p);
    return p.ReturnBool().value_or(false);
}

void CallPlain(ue3::Object* object, ue3::FunctionRef& function) {
    if (object == nullptr || function.Get() == nullptr || !ue3::runtime::IsAlive(object)) {
        return;
    }
    ue3::Params p(ue3::runtime::Get(), function.Get());
    ue3::runtime::Call(object, function.Get(), p);
}

engine::UnrealVector ToUnreal(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}

double Distance(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// The player's vehicle game mode (the machine gun's and cannon's execs run on it).
ue3::Object* VehicleGameMode(ue3::Object* controller) {
    if (gState.gameMode != nullptr && ue3::runtime::IsAlive(gState.gameMode)) {
        return gState.gameMode;
    }
    gState.gameMode = nullptr;
    ue3::Object* const manager = controller != nullptr ? gGameModeManager.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    auto const modes = manager != nullptr ? gGameModes.ElementsOf(manager, 32) : std::nullopt;
    for (std::int32_t i = 0; modes && i < modes->count; ++i) {
        ue3::Object* const mode = *static_cast<ue3::Object**>(modes->At(i));
        if (ue3::IsA(mode, gGameModeVehicle)) {
            gState.gameMode = mode;
            break;
        }
    }
    return gState.gameMode;
}

// Where the gun fires from: the active weapon's muzzle, else the turret's middle (150 units up the vehicle's axis).
engine::UnrealVector Muzzle(ue3::Object* vehicle) {
    if (ue3::Object* const weapon = gPawnWeapon.Read<ue3::Object*>(vehicle).value_or(nullptr); weapon != nullptr && gMuzzleLoc.Get() != nullptr) {
        ue3::Params p(ue3::runtime::Get(), gMuzzleLoc.Get());
        if (ue3::runtime::Call(weapon, gMuzzleLoc.Get(), p)) {
            if (auto const at = p.Return<ue3::Vector>()) {
                return ToUnreal(*at);
            }
        }
    }
    engine::UnrealVector const at = ToUnreal(player::Location(vehicle).value_or(ue3::Vector{}));
    ue3::Rotator const turn = player::Rotation(vehicle).value_or(ue3::Rotator{});
    engine::UnrealVector const up = engine::ToBasis(engine::UnrealRotation{turn.pitch, turn.yaw, turn.roll}).up;
    return {at.x + up.x * 150.0, at.y + up.y * 150.0, at.z + up.z * 150.0};
}

// The hostiles the gunner could take, read from the world.
void Scan(Inputs const& in, mg::Settings const& settings) {
    gState.found.clear();
    gState.scanPawns = gState.scanDormant = gState.scanAlive = gState.scanInRange = gState.scanHostile = 0;
    ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(in.vehicle).value_or(nullptr);
    ue3::Object* const driver = gDriver.Read<ue3::Object*>(in.vehicle).value_or(nullptr);
    if (world == nullptr || driver == nullptr || gIsHostile.Get() == nullptr) {
        gState.why = driver == nullptr ? "no driver in the vehicle (hostility unknown)" : "the world or IsHostile can't be read";
        return;
    }
    engine::UnrealVector const me = ToUnreal(player::Location(in.vehicle).value_or(ue3::Vector{}));
    engine::UnrealVector const muzzle = Muzzle(in.vehicle);
    int guard = 0;
    for (ue3::Object* p = gPawnList.Read<ue3::Object*>(world).value_or(nullptr); p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        ++gState.scanPawns;
        if (p == in.vehicle || p == driver || !player::IsBioPawn(p) || CallBool(p, gIsDead)) {
            continue;
        }
        if (gHidden.ReadBool(p).value_or(false) || !gCollideActors.ReadBool(p).value_or(true)) {
            ++gState.scanDormant;
            continue;  // dormant: hidden, nothing to hit
        }
        ++gState.scanAlive;
        std::optional<ue3::Vector> const at = player::Location(p);
        if (!at) {
            continue;
        }
        double const meters = Distance(ToUnreal(*at), me) / kUnitsPerMeter;
        if (meters > settings.rangeM) {
            continue;  // the cheap test first
        }
        ++gState.scanInRange;
        ue3::Params hostile(ue3::runtime::Get(), gIsHostile.Get());
        hostile.Set("Other", p);
        ue3::runtime::Call(driver, gIsHostile.Get(), hostile);
        if (!hostile.ReturnBool().value_or(false)) {
            continue;
        }
        ++gState.scanHostile;
        Found f;
        f.pawn = p;
        f.name = ue3::runtime::Get().NameText(p);
        f.candidate.id = static_cast<int>(gState.found.size());
        f.candidate.distanceM = meters;
        f.at = ToUnreal(*at);
        if (gFastTrace.Get() != nullptr) {
            ue3::Params trace(ue3::runtime::Get(), gFastTrace.Get());
            trace.Set("TraceEnd", *at);
            trace.Set("TraceStart", ue3::Vector{static_cast<float>(muzzle.x), static_cast<float>(muzzle.y), static_cast<float>(muzzle.z)});
            ue3::runtime::Call(in.vehicle, gFastTrace.Get(), trace);
            f.candidate.visible = trace.ReturnBool().value_or(false);
        }
        // No cone test: BioVehicleBehaviorBase.IsWorldLocationWithinTurretCone answered false for a target dead ahead at 30 m
        // (measured 2026-11-04), and the Mako's turret turns all the way round; its own pitch limits stop the rest.
        f.candidate.inCone = true;
        if (ue3::Object* const cylinder = gCylinder.Read<ue3::Object*>(p).value_or(nullptr)) {
            f.candidate.radiusM = gCollisionRadius.Read<float>(cylinder).value_or(0.0f) / kUnitsPerMeter;
        }
        gState.found.push_back(std::move(f));
    }
    // The crowds: how many other hostiles stand within crowdRadiusM of each (the cannon's blast takes them together).
    std::vector<mg::Candidate> candidates;
    std::vector<double> xyz;
    for (Found const& f : gState.found) {
        candidates.push_back(f.candidate);
        xyz.insert(xyz.end(), {f.at.x / kUnitsPerMeter, f.at.y / kUnitsPerMeter, f.at.z / kUnitsPerMeter});
    }
    mg::CountNeighbours(candidates, xyz, settings.crowdRadiusM);
    for (std::size_t i = 0; i < gState.found.size(); ++i) {
        gState.found[i].candidate.neighbours = candidates[i].neighbours;
    }
}

void HoldMachineGun(Inputs const& in, bool hold) {
    if (hold == gState.mgHeld) {
        return;
    }
    ue3::Object* const mode = VehicleGameMode(in.controller);
    if (mode == nullptr) {
        return;
    }
    CallPlain(mode, hold ? gFireWeapon : gStopFiringWeapon);
    gState.mgHeld = hold;
    gState.bursts += hold ? 1u : 0u;
}

void ReleaseTriggers(ue3::Object* controller) {
    if (gState.mgHeld) {
        if (ue3::Object* const mode = VehicleGameMode(controller)) {
            CallPlain(mode, gStopFiringWeapon);
        }
        gState.mgHeld = false;
    }
    if (gState.cannonReleaseAt >= 0.0) {
        if (ue3::Object* const mode = VehicleGameMode(controller)) {
            CallPlain(mode, gStopFiringCannon);
        }
        gState.cannonReleaseAt = -1.0;
    }
}

// Where the free right hand points, against the vehicle: out through the front (within 80 degrees of its forward, not more than
// 25 degrees down)?
bool PointsOut(Inputs const& in) {
    if (!in.frame || in.controllers == nullptr || !in.controllers->right.aimValid) {
        return false;
    }
    engine::UnrealVector const direction = engine::OrientationToWorld(*in.frame, in.controllers->right.aim.orientation).forward;
    ue3::Rotator const turn = player::Rotation(in.vehicle).value_or(ue3::Rotator{});
    engine::UnrealVector const local = engine::Apply(engine::Inverse(engine::ToBasis(engine::UnrealRotation{turn.pitch, turn.yaw, turn.roll})), direction);
    double const yaw = std::atan2(local.y, local.x) * 180.0 / 3.14159265358979323846;
    double const pitch = std::atan2(local.z, std::hypot(local.x, local.y)) * 180.0 / 3.14159265358979323846;
    return std::abs(yaw) <= 80.0 && pitch >= -25.0;
}
#endif

} // namespace

Result Tick(double deltaSeconds, Inputs const& in) {
    Result result;
#if defined(MEVR_GAME_LE1)
    double const now = Now();
    mg::Settings const settings = hand_calibration::GetMako().gunner;
    bool const active = settings.on && in.riding && in.vehicle != nullptr && ue3::runtime::IsAlive(in.vehicle);
    // The player's hands.
    mg::Hands hands;
    if (in.controllers != nullptr && in.controllers->active) {
        vr::controllers::Hand const& right = in.controllers->right;
        hands.stickHeld = in.stickHeld;
        hands.thumbX = right.stickX;
        hands.thumbY = right.stickY;
        hands.handFree = right.aimValid && !in.stickHeld;
        hands.handIntent = (right.triggerTouchKnown && right.triggerTouch) || right.trigger > 0.05f;
        hands.handPointsOut = active && PointsOut(in);
    }
    hands.busy = in.panelBusy;
    mg::Settings rules = settings;
    rules.on = active;  // off: the old rules (the stick while held, else the hand)
    if (active && !gState.wasActive) {
        gState.control = {};  // riding again: the gunner's at once, not after a handback
    }
    gState.wasActive = active;
    mg::Control const before = gState.control.control;
    mg::Control const control = mg::Step(gState.control, hands, now, rules);
    result.control = control;
    if (active && (!gState.loggedControlSet || control != gState.loggedControl)) {
        if (gState.loggedControlSet && before == mg::Control::Gunner && control != mg::Control::Gunner) {
            ++gState.takeovers;
        }
        gState.loggedControlSet = true;
        gState.loggedControl = control;
        log::Info("mako gunner: {} aims the turret{}", mg::Name(control), hands.busy ? " (a hand works the cockpit's panel)" : "");
    }
    if (!active) {
        ReleaseTriggers(in.controller);
        gState.target = nullptr;
        gState.found.clear();
        gState.fire = {};
        gState.why = !settings.on ? "off ([Mako] gunner = 0)" : !in.riding ? "not riding in the cockpit" : "no vehicle";
        gState.loggedControlSet = false;
        return result;
    }
    if (control != mg::Control::Gunner) {
        ReleaseTriggers(in.controller);
        gState.fire = {};
        gState.manualSeconds += deltaSeconds;
        result.holdHand = control == mg::Control::Hand && !mg::HandAims(hands);
        gState.why = std::format("{} has the gun", mg::Name(control));
        return result;
    }
    // The gunner: who to shoot.
    gState.gunnerSeconds += deltaSeconds;
    if (now - gState.lastScan >= kScanEveryS) {
        gState.lastScan = now;
        Scan(in, settings);
        int current = -1;
        for (Found const& f : gState.found) {
            if (f.pawn == gState.target) {
                current = f.candidate.id;
            }
        }
        std::vector<mg::Candidate> candidates;
        for (Found const& f : gState.found) {
            candidates.push_back(f.candidate);
        }
        int const chosen = mg::Choose(candidates, current, settings);
        ue3::Object* const next = chosen >= 0 ? gState.found[static_cast<std::size_t>(chosen)].pawn : nullptr;
        if (next != gState.target) {
            gState.target = next;
            ++gState.targetSerial;
            if (next != nullptr) {
                Found const& f = gState.found[static_cast<std::size_t>(chosen)];
                gState.targetName = f.name;
                gState.targetDistanceM = f.candidate.distanceM;
                gState.targetBig = mg::IsBig(f.candidate, settings);
                gState.targetCannon = mg::UseCannon(f.candidate, settings);
                gState.targetNeighbours = f.candidate.neighbours;
                ++gState.targetsTaken;
                log::Info("mako gunner: targets {} {:.0f} m away ({}; {:.1f} m wide, {} other{} within {:.0f} m; {} hostile{} in range)", f.name,
                    f.candidate.distanceM,
                    !gState.targetCannon ? "the machine gun"
                        : gState.targetBig ? "the cannon too: a heavy target"
                                           : "the cannon too: a crowd",
                    f.candidate.radiusM * 2.0, f.candidate.neighbours, f.candidate.neighbours == 1 ? "" : "s", settings.crowdRadiusM,
                    gState.found.size(), gState.found.size() == 1 ? "" : "s");
            } else {
                log::Info("mako gunner: no target ({} hostile{} in range, none seen in the turret's reach)", gState.found.size(),
                    gState.found.size() == 1 ? "" : "s");
            }
        } else if (next != nullptr) {
            for (Found const& f : gState.found) {
                if (f.pawn == next) {
                    gState.targetDistanceM = f.candidate.distanceM;
                    // A crowd gathers or thins around the same target: the cannon follows it.
                    bool const cannon = mg::UseCannon(f.candidate, settings);
                    if (cannon != gState.targetCannon) {
                        log::Info("mako gunner: {} {}", f.name, cannon ? "is in a crowd now: the cannon too" : "is alone now: the machine gun");
                    }
                    gState.targetCannon = cannon;
                    gState.targetNeighbours = f.candidate.neighbours;
                }
            }
        }
    }
    if (gState.target != nullptr && (!ue3::runtime::IsAlive(gState.target) || CallBool(gState.target, gIsDead))) {
        gState.target = nullptr;
        ++gState.targetSerial;
        gState.lastScan = -1e9;  // look again at once
    }
    if (gState.target != nullptr) {
        if (auto const at = player::Location(gState.target)) {
            result.target = ToUnreal(*at);
            result.what = gState.targetName;
        }
    }
    result.gunner = true;
    gState.why = result.target ? std::format("the gunner aims at {}", gState.targetName) : std::string("the gunner looks for a target");
    // Fire.
    mg::Orders const orders = mg::Fire(gState.fire, now, result.target ? gState.targetSerial : -1, gState.targetCannon, settings);
    // The player's own trigger (the stick's, as RT) fires the machine gun where the gunner aims: the gunner neither starts nor
    // stops it then, or its pauses would cut the player's fire.
    bool const playerFiring = in.controllers != nullptr && in.controllers->active && in.controllers->right.trigger > 0.1f;
    if (!playerFiring) {
        HoldMachineGun(in, orders.mg);
    } else if (gState.mgHeld) {
        gState.mgHeld = false;  // the player's release stops it, as their own press would
    }
    if (orders.cannon) {
        if (ue3::Object* const mode = VehicleGameMode(in.controller)) {
            CallPlain(mode, gFireCannon);
            gState.cannonReleaseAt = now + kCannonHoldS;
            ++gState.cannons;
        }
    }
    if (gState.cannonReleaseAt >= 0.0 && now >= gState.cannonReleaseAt) {
        if (ue3::Object* const mode = VehicleGameMode(in.controller)) {
            CallPlain(mode, gStopFiringCannon);
        }
        gState.cannonReleaseAt = -1.0;
    }
#else
    (void)deltaSeconds;
    (void)in;
#endif
    return result;
}

std::string Wake(ue3::Object* pawn) {
#if defined(MEVR_GAME_LE1)
    if (pawn == nullptr || !ue3::runtime::IsAlive(pawn) || gSetHidden.Get() == nullptr || gSetCollision.Get() == nullptr) {
        return "mako gunner: wake: no such pawn, or SetHidden / SetCollision missing";
    }
    ue3::Params hidden(ue3::runtime::Get(), gSetHidden.Get());
    hidden.SetBool("bNewHidden", false);
    ue3::runtime::Call(pawn, gSetHidden.Get(), hidden);
    ue3::Params collision(ue3::runtime::Get(), gSetCollision.Get());
    collision.SetBool("bNewColActors", true);
    collision.SetBool("bNewBlockActors", true);
    collision.SetBool("bNewIgnoreEncroachers", false);
    ue3::runtime::Call(pawn, gSetCollision.Get(), collision);
    return std::format("{}: shown {}, collision {}", ue3::runtime::FullName(pawn), !gHidden.ReadBool(pawn).value_or(true) ? "yes" : "NO",
        gCollideActors.ReadBool(pawn).value_or(false) ? "yes" : "NO");
#else
    (void)pawn;
    return "LE1 only";
#endif
}

void Stop(char const* why) {
#if defined(MEVR_GAME_LE1)
    if (gState.mgHeld || gState.cannonReleaseAt >= 0.0) {
        ue3::Object* const controller = player::Controller();
        ReleaseTriggers(controller);
    }
#endif
    gState.target = nullptr;
    gState.found.clear();
    gState.fire = {};
    gState.gameMode = nullptr;
    gState.loggedControlSet = false;
    gState.why = why;
}

// The target's health and shields (0..1) through its behavior's GetHealthAndShieldsScaled: whether the shots land.
std::string TargetHealthJson() {
#if defined(MEVR_GAME_LE1)
    if (gState.target == nullptr || !ue3::runtime::IsAlive(gState.target) || gHealthAndShields.Get() == nullptr) {
        return "null";
    }
    ue3::Object* const behavior = gPawnBehavior.Read<ue3::Object*>(gState.target).value_or(nullptr);
    if (behavior == nullptr || !ue3::runtime::IsAlive(behavior)) {
        return "null";
    }
    ue3::Params p(ue3::runtime::Get(), gHealthAndShields.Get());
    ue3::runtime::Call(behavior, gHealthAndShields.Get(), p);
    return std::format("[{}, {}]", vr::test_request::JsonNumber(p.Get<float>("fHealth").value_or(-1.0f)),
        vr::test_request::JsonNumber(p.Get<float>("fShields").value_or(-1.0f)));
#else
    return "null";
#endif
}

std::string StatusJson() {
    namespace json = vr::test_request;
    std::string candidates;
    for (Found const& f : gState.found) {
        if (candidates.size() > 1500) {
            break;
        }
        candidates += std::format("{}{{\"name\": {}, \"distanceM\": {}, \"visible\": {}, \"inCone\": {}, \"radiusM\": {}, \"neighbours\": {}}}",
            candidates.empty() ? "" : ", ", json::JsonString(f.name), json::JsonNumber(f.candidate.distanceM), f.candidate.visible ? "true" : "false",
            f.candidate.inCone ? "true" : "false", json::JsonNumber(f.candidate.radiusM), f.candidate.neighbours);
    }
    return std::format("{{\"control\": {}, \"why\": {}, \"target\": {}, \"targetHealthShields\": {}, \"distanceM\": {}, \"big\": {}, \"cannon\": {}, \"scan\": {{\"pawns\": {}, "
                       "\"dormant\": {}, \"alive\": {}, \"inRange\": {}, \"hostile\": {}}}, \"candidates\": [{}], \"machineGun\": {}, \"bursts\": {}, "
                       "\"cannons\": {}, \"targets\": {}, \"takeovers\": {}}}",
        json::JsonString(mg::Name(gState.control.control)), json::JsonString(gState.why), json::JsonString(gState.target != nullptr ? gState.targetName : ""),
        TargetHealthJson(), json::JsonNumber(gState.targetDistanceM), gState.targetBig ? "true" : "false", gState.targetCannon ? "true" : "false",
        gState.scanPawns, gState.scanDormant,
        gState.scanAlive, gState.scanInRange,
        gState.scanHostile, candidates, gState.mgHeld ? "true" : "false", gState.bursts, gState.cannons, gState.targetsTaken, gState.takeovers);
}

void LogStats() {
    if (gState.gunnerSeconds <= 0.0 && gState.manualSeconds <= 0.0) {
        return;
    }
    log::Info("    mako gunner: {}; this period: the gunner {:.0f} s, the player {:.0f} s, {} takeover{}, {} target{}, {} burst{}, {} cannon shot{}",
        gState.why, gState.gunnerSeconds, gState.manualSeconds, gState.takeovers, gState.takeovers == 1 ? "" : "s", gState.targetsTaken,
        gState.targetsTaken == 1 ? "" : "s", gState.bursts, gState.bursts == 1 ? "" : "s", gState.cannons, gState.cannons == 1 ? "" : "s");
    gState.gunnerSeconds = gState.manualSeconds = 0.0;
    gState.takeovers = gState.targetsTaken = gState.bursts = gState.cannons = 0;
}

void Shutdown() {
    Stop("the mod shuts down");
    gState = State{};
}

} // namespace mevr::game::mako_gunner
