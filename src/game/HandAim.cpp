// SPDX-License-Identifier: MIT
#include "game/HandAim.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Calibration.hpp"
#include "core/vr/Controllers.hpp"
#include "engine/CallFunction.hpp"
#include "engine/Legacy.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ArmIk.hpp"
#include "game/BodyCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "game/Sights.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Weapon.hpp"
#include "core/vr/TestRequest.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace mevr::game::hand_aim {
namespace {

constexpr char const* kGetAdjustedAimFor = "SFXGame.BioPlayerController.GetAdjustedAimFor";

// ---- engine access: our reflection layer on LE1 (research/34), LExSDKv2 on LE2/LE3 until stage 5
// (engine/Legacy) -----------------------------------------------------------------------------------

#if defined(MEVR_GAME_LE1)
// The weapon's own aim query (script, called natively), which gets the shot's start (StartFireLoc):
// diagnostics only.
constexpr char const* kWeaponGetAdjustedAim = "SFXGame.BioWeaponRanged.GetAdjustedAim";
// Where shots start. BioWeaponRanged's native TickFire (0xC621E0) asks the pawn through
// ProcessEvent (the event thunk 0xC63C10, Pawn.GetWeaponStartTraceLocation(None)), then the weapon
// (GetAdjustedAim), and traces from the one along the other (0xC589F0). The player's pawn answers
// with BioPawn's native override; Engine.Pawn's script version is registered too, for pawns
// without one.
constexpr char const* kPawnStartTrace = "SFXGame.BioPawn.GetWeaponStartTraceLocation";
constexpr char const* kEnginePawnStartTrace = "Engine.Pawn.GetWeaponStartTraceLocation";
// A shot whose start is within this of the muzzle started there (Unreal units).
constexpr double kAtMuzzleUnits = 1.0;

ue3::FunctionRef gAimForFn{kGetAdjustedAimFor};
ue3::FieldRef gAimForReturn{gAimForFn, "ReturnValue"};
ue3::FunctionRef gWeaponAimFn{kWeaponGetAdjustedAim};
ue3::FieldRef gStartFireLoc{gWeaponAimFn, "StartFireLoc"};
ue3::FunctionRef gPawnStartTraceFn{kPawnStartTrace};
ue3::FunctionRef gEnginePawnStartTraceFn{kEnginePawnStartTrace};
// Both functions' ReturnValue (the self-check compares them: the same place in both blocks).
ue3::FieldRef gPawnStartReturn{gPawnStartTraceFn, "ReturnValue"};
ue3::FieldRef gEnginePawnStartReturn{gEnginePawnStartTraceFn, "ReturnValue"};

ue3::Function* FindAimFor() {
    return gAimForFn.Get();
}
ue3::Object* LocalController() {
    return player::Controller();
}
std::optional<ue3::Vector> PawnLocation(ue3::Object const* controller) {
    ue3::Object* const pawn = player::Pawn(controller);
    return pawn != nullptr ? player::Location(pawn) : std::nullopt;
}
ue3::Rotator* AimForReturn(void* parms) {
    return gAimForReturn.In<ue3::Rotator>(parms);
}
#else
ue3::Function* FindAimFor() {
    return engine::legacy::FindFunction(kGetAdjustedAimFor);
}
ue3::Object* LocalController() {
    return engine::legacy::LocalPlayerController();
}
std::optional<ue3::Vector> PawnLocation(ue3::Object const* controller) {
    return engine::legacy::PawnLocation(controller);
}
ue3::Rotator* AimForReturn(void* parms) {
    return engine::legacy::AdjustedAimReturn(parms);
}
#endif

enum class Path { Game, Controller, Barrel };

ue3::Function* gGetAdjustedAimFor = nullptr;
bool gEnabled = false;  // game thread
std::atomic<unsigned> gAimCalls{0};
std::atomic<unsigned> gBarrelAims{0};
std::atomic<unsigned> gControllerAims{0};
std::atomic<unsigned> gNoAnchor{0};
std::atomic<unsigned> gStartQueries{0};
std::atomic<unsigned> gStartsMoved{0};
std::atomic<unsigned> gShotsAtMuzzle{0};
std::atomic<unsigned> gShotsElsewhere{0};
std::atomic<unsigned> gRaysPublished{0};
bool gLoggedOverride = false;
bool gLoggedBarrel = false;
bool gLoggedShotStart = false;
bool gLoggedStartMoved = false;
bool gLoggedAtMuzzle = false;
bool gLoggedElsewhere = false;
std::string gPathStatus;
std::string gLoggedPathStatus;
std::string gRayStatus = "none";
std::string gLoggedRayStatus;
bool gLoggedFirstRay = false;
// The last aim query's ray (world), for the shot-start diagnostic. Game thread.
bool gLastRayValid = false;
Path gLastPath = Path::Game;
engine::UnrealVector gLastRayStart{};
engine::UnrealVector gLastRayDirection{};

// The last shot along the barrel, for the per-shot log and the snapshot (research/21 section 10): where it was aimed
// (toward what the sight line hit, or along the barrel), and, after the weapon's own aim query, where it started.
struct ShotRecord {
    bool valid = false;
    bool converged = false;           // aimed at the sight line's hit (else along the barrel)
    double toSightDeg = 0.0;          // the shot's direction against the sight line
    double toBarrelDeg = 0.0;         // ...and against the barrel
    double sightDistanceM = 0.0;      // what the sight line hit, from the sight (game meters; the range if nothing)
    bool sightHit = false;
    std::string what;
    double startOffMuzzle = -1.0;     // units between the game's StartFireLoc and our muzzle (-1: not seen yet)
    unsigned count = 0;
};
ShotRecord gShot;
std::atomic<unsigned> gConvergedShots{0};
std::atomic<unsigned> gOtherWeaponAims{0};  // GetAdjustedAim of weapons we don't hold (squadmates), skipped
double gSumToSight = 0.0;
double gMaxToSight = 0.0;
unsigned gShotsLogged = 0;
ULONGLONG gNextShotLogMs = 0;

double Distance(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

void LogFirstOverride(engine::TrackingFrame const& frame, ue3::Rotator const& game, ue3::Rotator const& hand,
    engine::UnrealVector const& handWorld) {
    gLoggedOverride = true;
    double distanceToPawn = -1.0;
    if (auto const at = PawnLocation(LocalController())) {
        distanceToPawn = std::hypot(handWorld.x - at->x, handWorld.y - at->y, handWorld.z - at->z);
    }
    log::Info("hand aim: first shot aimed by the right controller: game aim pitch {} yaw {} -> controller pitch {} yaw {}; "
              "hand at ({:.0f}, {:.0f}, {:.0f}), {:.0f} units from the head and {:.0f} from the pawn",
        game.pitch, game.yaw, hand.pitch, hand.yaw, handWorld.x, handWorld.y, handWorld.z,
        Distance(handWorld, frame.headWorld), distanceToPawn);
}

// The held gun's muzzle, when shots should follow it: hand aim on, on foot, and the gun in the
// right hand pointed by the controller (arm IK turns the hand so the barrel follows the aim ray,
// and the pose is being written). Otherwise the controller's ray stands in (fail closed).
std::optional<weapon::Muzzle> BarrelMuzzle() {
    if (!gEnabled || !head_tracking::OnFoot() || !arm_ik::RightHandAimsGun()) {
        return std::nullopt;
    }
    return weapon::Current();
}

// Why shots don't follow the gun barrel (for status lines), or nullptr if they do.
char const* WhyNoBarrel() {
    if (!head_tracking::OnFoot()) {
        return "not on foot";
    }
    if (!weapon::Current()) {
        return "no verified ranged weapon in the right hand";
    }
    if (!arm_ik::RightHandAimsGun()) {
        return "arm IK isn't pointing the gun yet (off, no aim pose, or the grip not measured: hold still a moment)";
    }
    return nullptr;
}

// The right controller's aim ray as a world rotation, unwound next to the game's own aim. False
// (the game's aim stays) unless on foot with a tracked controller and a valid view anchor.
bool HandRotation(ue3::Rotator const& game, ue3::Rotator& out) {
    if (!gEnabled || !head_tracking::OnFoot()) {
        return false;
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!controllers.active || !controllers.right.aimValid) {
        return false;
    }
    auto const frame = view_anchor::Current();
    if (!frame) {
        gNoAnchor.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    XrPosef aim = controllers.right.aim;
    // Calibrated (research/25): the calibrated sight line from the grip, not the runtime's aim pose.
    if (auto const stance = body_calibration::StanceFor(static_cast<int>(weapon::Type())); stance && controllers.right.gripValid) {
        aim.position = vr::calibration::SightLine(controllers.right.grip, *stance).point;
        aim.orientation = vr::calibration::GunOrientation(controllers.right.grip.orientation, *stance);
    }
    engine::Basis const ray = engine::OrientationToWorld(*frame, aim.orientation);
    engine::UnrealRotation const rotation = engine::ToRotation(ray.forward);
    out.pitch = engine::Unwind(rotation.pitch, game.pitch);
    out.yaw = engine::Unwind(rotation.yaw, game.yaw);
    out.roll = 0;
    gLastRayStart = engine::ToWorld(*frame, aim.position);
    gLastRayDirection = ray.forward;
    gLastRayValid = true;
    if (!gLoggedOverride) {
        LogFirstOverride(*frame, game, out, gLastRayStart);
    }
    return true;
}

// Along the held gun's barrel (its start moves to the muzzle in OnStartTrace), or, with a sight on the gun
// ([Sights] converge), from the muzzle toward the point the sight line hits: the shot lands under the dot.
bool BarrelRotation(ue3::Rotator const& game, ue3::Rotator& out) {
    auto const muzzle = BarrelMuzzle();
    if (!muzzle) {
        return false;
    }
    auto const aim = sights::ShotAlongSight(*muzzle);
    engine::UnrealVector const direction = aim ? aim->direction : muzzle->barrel;
    engine::UnrealRotation const rotation = engine::ToRotation(direction);
    out.pitch = engine::Unwind(rotation.pitch, game.pitch);
    out.yaw = engine::Unwind(rotation.yaw, game.yaw);
    out.roll = 0;
    gLastRayStart = muzzle->location;
    gLastRayDirection = direction;
    gLastRayValid = true;
    gShot.valid = true;
    gShot.converged = aim.has_value();
    gShot.toSightDeg = aim ? aim->toSightDeg : 0.0;
    gShot.toBarrelDeg = aim ? aim->toBarrelDeg : 0.0;
    gShot.sightDistanceM = aim ? aim->distance / 100.0 : 0.0;
    gShot.sightHit = aim && aim->hit;
    gShot.what = aim ? aim->what : std::string();
    gShot.startOffMuzzle = -1.0;
    ++gShot.count;
    if (aim) {
        gConvergedShots.fetch_add(1, std::memory_order_relaxed);
        gSumToSight += aim->toSightDeg;
        gMaxToSight = std::max(gMaxToSight, aim->toSightDeg);
    }
    if (!gLoggedBarrel) {
        gLoggedBarrel = true;
        log::Info("hand aim: first shot along the gun barrel: game aim pitch {} yaw {} -> barrel pitch {} yaw {}; muzzle at "
                  "({:.0f}, {:.0f}, {:.0f})",
            game.pitch, game.yaw, out.pitch, out.yaw, muzzle->location.x, muzzle->location.y, muzzle->location.z);
    }
    return true;
}

bool IsLocalPlayer(ue3::Object* context) {
    return context != nullptr && context == LocalController();
}

// `tag` names the write for the crash breadcrumbs (research/36).
void Apply(ue3::Object* context, ue3::Rotator* result, char const* tag) {
    gAimCalls.fetch_add(1, std::memory_order_relaxed);
    gLastRayValid = false;
    gLastPath = Path::Game;
    if (result == nullptr || !IsLocalPlayer(context)) {
        return;
    }
    ue3::Rotator rotation{};
    if (BarrelRotation(*result, rotation)) {
        *result = rotation;
        write_journal::Wrote(tag, result);
        gLastPath = Path::Barrel;
        gBarrelAims.fetch_add(1, std::memory_order_relaxed);
    } else if (HandRotation(*result, rotation)) {
        *result = rotation;
        write_journal::Wrote(tag, result);
        gLastPath = Path::Controller;
        gControllerAims.fetch_add(1, std::memory_order_relaxed);
    }
}

// Script-to-script calls (the weapon's fire code): `result` is the return value.
void OnCallFunction(ue3::Object* context, ue3::Function* /*function*/, void* result) {
    Apply(context, static_cast<ue3::Rotator*>(result), "HandAim.aim (CF result)");
}

// Native-to-script calls: the return value lives in the parms.
void OnProcessEvent(ue3::Object* context, ue3::Function* /*function*/, void* parms, void* /*result*/) {
    Apply(context, AimForReturn(parms), "HandAim.aim (PE return)");
}

#if defined(MEVR_GAME_LE1)
// The shot's start: the pawn's answer goes back through the parms, so rewriting it here moves the
// trace to the muzzle. Only for the local player's pawn while shots follow the barrel.
void OnStartTrace(ue3::Object* context, ue3::Function* function, void* parms, void* /*result*/) {
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr || context == nullptr || context != player::Pawn(controller)) {
        return;
    }
    gStartQueries.fetch_add(1, std::memory_order_relaxed);
    auto const muzzle = BarrelMuzzle();
    if (!muzzle) {
        return;
    }
    ue3::FieldRef& returned = function == gPawnStartTraceFn.Get() ? gPawnStartReturn : gEnginePawnStartReturn;
    ue3::Vector* const at = returned.In<ue3::Vector>(parms);
    if (at == nullptr) {
        return;  // the parameter didn't resolve (logged once)
    }
    ue3::Vector& start = *at;
    engine::UnrealVector const game{start.x, start.y, start.z};
    start = {static_cast<float>(muzzle->location.x), static_cast<float>(muzzle->location.y), static_cast<float>(muzzle->location.z)};
    write_journal::Wrote("HandAim.start trace (PE ret)", &start);
    gStartsMoved.fetch_add(1, std::memory_order_relaxed);
    if (!gLoggedStartMoved) {
        gLoggedStartMoved = true;
        log::Info("hand aim: shot start moved to the muzzle: the pawn said ({:.0f}, {:.0f}, {:.0f}), the muzzle is at "
                  "({:.0f}, {:.0f}, {:.0f}), {:.0f} units away",
            game.x, game.y, game.z, muzzle->location.x, muzzle->location.y, muzzle->location.z, Distance(game, muzzle->location));
    }
}

// One line per shot, rate-limited (the first three per weapon, then one every two seconds): the shot against the
// sight line and the barrel, where it started against our muzzle, and what the sight line hit.
void LogShot() {
    ULONGLONG const now = GetTickCount64();
    if (gShotsLogged >= 3 && now < gNextShotLogMs) {
        return;
    }
    ++gShotsLogged;
    gNextShotLogMs = now + 2000;
    if (gShot.converged) {
        log::Info("hand aim: shot {}: from the muzzle ({:.2f} units from the game's start) toward what the sight line hit, {:.1f} m "
                  "past the sight ({}): {:.3f} degrees off the sight line, {:.3f} off the barrel",
            gShot.count, gShot.startOffMuzzle, gShot.sightDistanceM,
            gShot.sightHit ? gShot.what : std::string("nothing within the trace: aimed at its end"), gShot.toSightDeg, gShot.toBarrelDeg);
    } else {
        log::Info("hand aim: shot {}: from the muzzle ({:.2f} units from the game's start) along the barrel (no sight line to converge "
                  "on: no sight on this gun, [Sights] converge = 0, or no trace)",
            gShot.count, gShot.startOffMuzzle);
    }
}

// After the weapon's aim query: where this shot really starts. Along the barrel, that should be
// the muzzle (OnStartTrace); along the controller's ray, how far shots pass beside it (once).
// Only the held weapon's: squadmates' weapons ask too (their own controllers' aim, never ours), and their
// shots start where they stand (2026-10-23: the "273 units from the muzzle" warnings were theirs).
void OnWeaponAim(ue3::Object* context, ue3::Function* /*function*/, void* parms, void* /*result*/) {
    if (!gLastRayValid) {
        return;
    }
    if (context == nullptr || reinterpret_cast<std::uintptr_t>(context) != weapon::Key()) {
        gOtherWeaponAims.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    ue3::Vector const* const startFire = gStartFireLoc.In<ue3::Vector>(parms);
    if (startFire == nullptr) {
        return;  // the parameter didn't resolve (logged once)
    }
    ue3::Vector const& at = *startFire;
    engine::UnrealVector const start{at.x, at.y, at.z};
    if (gLastPath == Path::Barrel) {
        double const off = Distance(start, gLastRayStart);
        bool const atMuzzle = off <= kAtMuzzleUnits;
        gShot.startOffMuzzle = off;
        LogShot();
        (atMuzzle ? gShotsAtMuzzle : gShotsElsewhere).fetch_add(1, std::memory_order_relaxed);
        bool& logged = atMuzzle ? gLoggedAtMuzzle : gLoggedElsewhere;
        if (!logged) {
            logged = true;
            if (atMuzzle) {
                log::Info("hand aim: the shot started at the muzzle ({:.2f} units off) and went along the barrel", off);
            } else {
                log::Warn("hand aim: a shot along the barrel started {:.0f} units from the muzzle, at ({:.0f}, {:.0f}, {:.0f}): "
                          "the start override didn't reach it (it runs parallel to the barrel)",
                    off, start.x, start.y, start.z);
            }
        }
        return;
    }
    if (gLoggedShotStart) {
        return;
    }
    gLoggedShotStart = true;
    engine::UnrealVector const d = gLastRayDirection;
    double const length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    engine::UnrealVector const v{start.x - gLastRayStart.x, start.y - gLastRayStart.y, start.z - gLastRayStart.z};
    double const along = length > 0.0 ? (v.x * d.x + v.y * d.y + v.z * d.z) / length : 0.0;
    double const total = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    log::Info("hand aim: shots start at ({:.0f}, {:.0f}, {:.0f}), {:.0f} units from the hand: {:.0f} along the "
              "controller's ray and {:.0f} off it (how far shots pass beside where it points)",
        start.x, start.y, start.z, total, along, std::sqrt(std::max(0.0, total * total - along * along)));
}
#endif

void LogPathChange() {
    if (!gEnabled) {
        gPathStatus = "off";
    } else if (char const* const why = WhyNoBarrel()) {
        gPathStatus = std::format("shots follow the right controller's aim ray ({})", why);
    } else {
        gPathStatus = std::format("shots follow the gun barrel from the muzzle ({})", weapon::Status());
    }
    if (gPathStatus != gLoggedPathStatus) {
        log::Info("hand aim: {}", gPathStatus);
        gLoggedPathStatus = gPathStatus;
    }
}

} // namespace

bool TryStartup() {
    gGetAdjustedAimFor = FindAimFor();
    if (gGetAdjustedAimFor == nullptr) {
        return false;
    }
    engine::call_function::AddPostHandler(gGetAdjustedAimFor, &OnCallFunction);
    engine::process_event::AddPostHandler(gGetAdjustedAimFor, &OnProcessEvent);
#if defined(MEVR_GAME_LE1)
    if (ue3::Function* const weaponAim = gWeaponAimFn.Get()) {
        engine::process_event::AddPostHandler(weaponAim, &OnWeaponAim);
    }
    int startHandlers = 0;
    for (ue3::FunctionRef* ref : {&gPawnStartTraceFn, &gEnginePawnStartTraceFn}) {
        if (ue3::Function* const start = ref->Get()) {
            engine::process_event::AddPostHandler(start, &OnStartTrace);
            ++startHandlers;
        }
    }
    if (startHandlers == 0) {
        log::Warn("hand aim: Pawn.GetWeaponStartTraceLocation not found: shots along the gun barrel keep the game's start");
    }
#endif
    return true;
}

bool Toggle() {
    gEnabled = !gEnabled;
    return gEnabled;
}

void SetEnabled(bool on) {
    gEnabled = on;
}

bool Enabled() {
    return gEnabled;
}

void Tick() {
    LogPathChange();
}

vr::aim_ray::Ray AimRay() {
    vr::aim_ray::Ray ray;
    char const* why = gEnabled ? WhyNoBarrel() : "hand aim off";
    std::optional<weapon::Muzzle> muzzle;
    std::optional<engine::TrackingFrame> frame;
    if (why == nullptr && !(muzzle = BarrelMuzzle())) {
        why = "no muzzle";
    }
    if (why == nullptr && !(frame = view_anchor::Current())) {
        why = "no view anchor";
    }
    if (why == nullptr) {
        ray.valid = true;
        ray.origin = engine::ToTracking(*frame, muzzle->location);
        ray.direction = engine::DirectionToTracking(*frame, muzzle->barrel);
        gRaysPublished.fetch_add(1, std::memory_order_relaxed);
        if (!gLoggedFirstRay) {
            gLoggedFirstRay = true;
            XrVector3f const& head = frame->head.position;
            // Where the right controller is: the muzzle should sit a gun's length ahead of it
            // (at the view's scale), not metres away (research/14, "Reach").
            vr::controllers::State const controllers = vr::controllers::Latest();
            XrVector3f const grip = controllers.right.gripValid ? controllers.right.grip.position : head;
            log::Info("hand aim: first aim ray (tracking space): muzzle ({:.3f}, {:.3f}, {:.3f}) m, {:.2f} m from the head, "
                      "{:.2f} m from the right grip ({}), direction ({:.3f}, {:.3f}, {:.3f}); view {:.1f} units per meter",
                ray.origin.x, ray.origin.y, ray.origin.z,
                std::hypot(ray.origin.x - head.x, ray.origin.y - head.y, ray.origin.z - head.z),
                std::hypot(ray.origin.x - grip.x, ray.origin.y - grip.y, ray.origin.z - grip.z),
                controllers.right.gripValid ? "tracked" : "not tracked: from the head", ray.direction.x, ray.direction.y,
                ray.direction.z, frame->unitsPerMeter);
        }
    }
    gRayStatus = why == nullptr ? "from the muzzle along the barrel" : std::format("none ({})", why);
    if (gRayStatus != gLoggedRayStatus) {
        log::Info("hand aim: aim ray {}", gRayStatus);
        gLoggedRayStatus = gRayStatus;
    }
    return ray;
}

void LogStats() {
    if (gGetAdjustedAimFor == nullptr) {
        return;
    }
    unsigned const calls = gAimCalls.exchange(0);
    unsigned const barrel = gBarrelAims.exchange(0);
    unsigned const controller = gControllerAims.exchange(0);
    unsigned const noAnchor = gNoAnchor.exchange(0);
    unsigned const starts = gStartQueries.exchange(0);
    unsigned const moved = gStartsMoved.exchange(0);
    unsigned const atMuzzle = gShotsAtMuzzle.exchange(0);
    unsigned const elsewhere = gShotsElsewhere.exchange(0);
    unsigned const rays = gRaysPublished.exchange(0);
    unsigned const converged = gConvergedShots.exchange(0);
    unsigned const others = gOtherWeaponAims.exchange(0);
    if (gEnabled || calls > 0) {
        log::Info("    hand aim: {}, {} aim queries: {} along the gun barrel ({} of them toward what the sight line hit{}), {} along the "
                  "controller's ray, {} kept the game's aim (no view anchor); {} shot-start queries, {} moved to the muzzle; shots "
                  "checked: {} from the muzzle, {} not; {} other weapons' aim queries skipped; aim ray {} ({} published)",
            gEnabled ? "on" : "off", calls, barrel, converged,
            converged > 0 ? std::format(", {:.3f} degrees off the sight line on average, {:.3f} at most", gSumToSight / converged, gMaxToSight)
                          : std::string(),
            controller, noAnchor, starts, moved, atMuzzle, elsewhere, others, gRayStatus, rays);
    }
    gSumToSight = 0.0;
    gMaxToSight = 0.0;
}

void Shutdown() {
    gGetAdjustedAimFor = nullptr;
    gEnabled = false;
    gLoggedOverride = false;
    gLoggedBarrel = false;
    gLoggedShotStart = false;
    gLoggedStartMoved = false;
    gLoggedAtMuzzle = false;
    gLoggedElsewhere = false;
    gLoggedFirstRay = false;
    gPathStatus.clear();
    gLoggedPathStatus.clear();
    gRayStatus = "none";
    gLoggedRayStatus.clear();
    gLastRayValid = false;
    gLastPath = Path::Game;
    gShot = {};
    gShotsLogged = 0;
    gNextShotLogMs = 0;
    gSumToSight = gMaxToSight = 0.0;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"enabled\": {}, \"lastShot\": {{\"valid\": {}, \"count\": {}, \"converged\": {}, \"toSightDeg\": {}, "
                       "\"toBarrelDeg\": {}, \"sightDistanceM\": {}, \"sightHit\": {}, \"what\": {}, \"startOffMuzzle\": {}}}}}",
        gEnabled ? "true" : "false", gShot.valid ? "true" : "false", gShot.count, gShot.converged ? "true" : "false",
        json::JsonNumber(gShot.toSightDeg), json::JsonNumber(gShot.toBarrelDeg), json::JsonNumber(gShot.sightDistanceM),
        gShot.sightHit ? "true" : "false", json::JsonString(gShot.what), json::JsonNumber(gShot.startOffMuzzle));
}

} // namespace mevr::game::hand_aim
