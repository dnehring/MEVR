// SPDX-License-Identifier: MIT
#include "game/Sights.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Ads.hpp"
#include "core/vr/Calibration.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/ScopeArt.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/BodyCalibration.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/ScopeParts.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace mevr::game::sights {
namespace {

namespace sv = vr::sights;

#if defined(MEVR_GAME_LE1)
// Our reflection layer (research/34).
ue3::FunctionRef gGetPlayerAimError{"SFXGame.BioWeaponRanged.GetPlayerAimError"};
ue3::FunctionRef gTrace{"Engine.Actor.Trace"};
// The camera's near plane (research/21 section 14): LocalPlayer.NearClipPlane, 10 units by default (Halcyon's render hook
// read it live; MassFPS pulls it in to 1, research/11). The scope's pull never brings the eyepiece inside it.
ue3::FieldRef gControllerPlayer{"Engine.PlayerController", "Player"};
ue3::FieldRef gNearClipPlane{"Engine.LocalPlayer", "NearClipPlane"};
// The game's own magnifications (research/21 section 16): the held weapon's stats object (one class per manufacturer) and
// its AimModes, each a zoomed camera FOV and whether it's a scope.
ue3::FieldRef gWeaponStats{"SFXGame.BioWeaponRanged", "Stats"};
ue3::FieldRef gAimModes{"SFXGame.SFXWeaponStatsBase", "AimModes"};
ue3::FieldRef gAimZoomFov{gAimModes, "ZoomFOV"};
ue3::FieldRef gAimScoped{gAimModes, "bScoped"};
// The game's scope art (vr::scope_art): three Texture2Ds of GUI_SF_HUD in Startup_INT.pcc, always loaded.
ue3::ClassRef gTexture2D{"Engine.Texture2D"};
ue3::FieldRef gTextureResource{"Engine.Texture", "Resource"};
ue3::FieldRef gTextureSizeX{"Engine.Texture2D", "SizeX"};
ue3::FieldRef gTextureSizeY{"Engine.Texture2D", "SizeY"};
#endif
constexpr float kDefaultNearUnits = 10.0f;  // UE3's default near plane, when the field can't be read
// How far the sight line is traced for the shot's convergence point (game units: 300 m at the life-size view).
constexpr double kConvergeRangeUnits = 30000.0;
// What the sight line hits must be this far ahead of the muzzle to aim at (a wall at the sight: parallel instead).
constexpr float kMinAheadUnits = 20.0f;

ue3::Function* gAimErrorFunction = nullptr;
ue3::Function* gTraceFunction = nullptr;

// The sight as last placed, in the world (research/21 section 10): its place on the gun (gun units: `back`
// behind the muzzle along the barrel, `height` toward `top`), so a shot can rebuild the sight line from the gun as
// it is when it fires, and how far ahead along the line the world was hit this tick.
struct WorldSight {
    bool valid = false;
    std::uintptr_t weapon = 0;
    double back = 0.0;
    double height = 0.0;
    double side = 0.0;
    engine::UnrealVector top{};  // world, unit: the gun's top as the sight uses it
    double zeroUnits = 0.0;      // [Sights] zeroM in game units (0: parallel)
    bool converge = false;
    ue3::Object* hitActor = nullptr;  // what the sight line hit (the squad context's aim target)
    // The tick's trace along the line from the sight's centre.
    bool traced = false;
    bool hit = false;
    double distance = 0.0;       // game units from the sight's centre (the trace's range when nothing was hit)
    std::string what;            // the actor hit
};
WorldSight gWorld;
bool gLoggedTracePath = false;

// Per weapon: the sight's place on the gun's own model (research/21 sections 11 and 13). Its bones are re-read every
// kRemeasureMs (ME1's guns unfold for a second after a draw, and the first tick's model still has the pose from before
// arm IK aimed it); a bone named for a sight is followed live, in the world, every tick.
struct ModelPlace {
    std::uintptr_t weapon = 0;
    bool measured = false;
    double measuredMs = 0.0;
    std::optional<float> top;  // the model's top above the bore, from its bones (gun units)
    std::optional<float> boundsTop;  // ...and from its mesh bounds (logged only: a loose culling box)
    // A bone named for a sight (scope, sight, optic, lens, eyepiece, reticle; a scope's eyepiece first): the sight goes there.
    std::optional<std::size_t> bone;
    std::string boneName;
    std::string text;
    // The bone's place in the gun's frame (live), and how it settled after the draw.
    std::optional<sv::ModelPoint> live;
    sv::ModelPoint reference{};
    double stillSinceMs = 0.0;
    double startMs = 0.0;
    float travelled = 0.0f;  // how far the bone moved since the draw (gun units)
    bool settledLogged = false;
    int settleLogs = 0;
    // The game's model (research/21 section 16): its mesh's name and, for the rifles' meshes, its optic (where the
    // eyepiece or the housing's rear is from the sight's bone, its radius, the parts to hide), used when its bone is the
    // sight's.
    std::string mesh;
    std::optional<sv::Optic> optic;
    bool opticApplies = false;
};
ModelPlace gModel;

// The game's magnifications for the held weapon (research/21 section 16), read once per weapon, and the level chosen by
// the right stick's flick while aiming down the scope.
struct Zoom {
    std::uintptr_t weapon = 0;
    bool read = false;
    sv::GameZoom game;
    std::string stats;  // the stats object, for the log
    int level = 0;
    sv::ZoomFlick flick;
};
Zoom gZoom;
void (*gHaptic)(int hand, float seconds, float amplitude) = nullptr;

// The game's scope art (vr::scope_art): its three textures found once (again every 10 s while not), checked alive each
// tick; a new generation whenever what's published changes.
struct Art {
    ue3::Object* textures[vr::scope_art::kTextures]{};
    bool found = false;
    double searchedMs = -1e9;
    int searches = 0;
    sv::Sight::ArtTexture published[vr::scope_art::kTextures]{};
    std::uint64_t generation = 0;
    std::string text = "not looked for";
};
Art gArt;
constexpr double kArtSearchEveryMs = 10000.0;
constexpr double kRemeasureMs = 250.0;
constexpr float kSettleUnits = 0.3f;    // the bone counts as still while it stays this close
constexpr double kSettleMs = 500.0;     // ...for this long
sv::Gun gPlaced{};               // this tick's place (auto or set), for SightInHand
std::uintptr_t gPlacedWeapon = 0;
sv::Sight gLast{0};
std::uintptr_t gLastWeapon = 0;
std::string gStatus = "none";
std::string gLoggedStatus;
std::uint32_t gHostView = 0;
std::uint32_t gLoggedHostView = 0;
bool gScopeUp = false;  // the held gun's sight is a scope and the host shows its picture
bool gAimedFire = true;
std::atomic<unsigned> gAimErrorCalls{0};
std::atomic<unsigned> gAimedShots{0};
unsigned gAimedShotsTotal = 0;
bool gLoggedAimed = false;

// The alignment assist. The spring's value is what arm IK takes next tick; the drawn gun read by hand
// aim's ray carries what arm IK took the tick before (the engine poses the gun before our tick).
sv::AssistSpring gSpring;
XrVector3f gTakenNow{};   // what arm IK took this tick
XrVector3f gTakenPrev{};  // ...and the tick before: the shift in the gun hand aim's ray reads
sv::AssistGoal gGoal;
int gAssistEye = -1;
bool gAssisting = false;      // the goal's weight is up (logged on each change, with hysteresis)
double gLastTickMs = 0.0;

// Aiming down the sight (research/21 section 15; vr::ads, Overlord's corridor, dwell and hysteresis): measured on the gun
// without the assist's move, each tick a sight is drawn. A scope with [Sights] ads = 1 shows its picture (and hides its
// lens part) only under ADS; the continuous approach weight drives the sideways move and the pull for every sight.
namespace ads = vr::ads;
ads::Policy gAdsPolicy;
ads::Sample gAdsSample;
bool gAdsGate = false;        // this tick's sight is a scope whose picture waits for ADS
bool gAds = false;            // ADS is on (a scope's only: gAdsGate)
float gApproach = 0.0f;       // ads::Approach this tick
float gDistanceWeight = 0.0f; // ads::DistanceWeight this tick
float gPullLimit = 0.0f;      // this tick's pull limit (meters)
bool gFastSpring = false;     // the spring's ADS profile this tick
double gAdsSinceMs = 0.0;
int gAdsOnsHeartbeat = 0;
int gAdsTicksHeartbeat = 0;
double gAdsEyeSumHeartbeat = 0.0;  // the aiming eye's distance to the pulled eyepiece while ADS was on
constexpr float kAdsReachHysteresisM = 0.10f;  // [Sights] adsReachM to come on, this much further to go off
constexpr float kAssistOnWeight = 0.10f;       // the alignment assist's log line: on from this weight...
constexpr float kAssistOffWeight = 0.02f;      // ...off below this
// Heartbeat and snapshot: where the aiming eye sat against the unassisted sight line while the gun was
// raised (a systematic offset means the sight's place on the gun is off), and the biggest shift.
double gOffsetSumX = 0.0;
double gOffsetSumY = 0.0;
double gOffsetSumAngle = 0.0;
int gOffsetTicks = 0;
float gMaxShift = 0.0f;

// The pull to the eye (research/21 sections 14-15): this tick's (relief and limit; zero with the pull off), the near
// plane it was floored on, and per raise (or ADS) how far it pulled.
sv::Pull gPull;
float gNearUnits = kDefaultNearUnits;
bool gNearRead = false;        // the near plane came from the game (else the default)
std::string gLoggedRelief;     // the relief line, logged when it changes
float gRaisePullMax = 0.0f;    // this raise: the most the gun was pulled (meters)
float gRaiseAlongBefore = 0.0f;  // ...the eye's distance to the unpulled eyepiece then
float gRaiseAlongAfter = 0.0f;   // ...and to the pulled one
float gPullSumHeartbeat = 0.0f;
int gPullTicksHeartbeat = 0;
// The held gun's [Sights] <type>Hide, parsed when it changes.
std::string gHideText;
std::vector<std::string> gHideList;

// BioWeaponRanged.GetPlayerAimError's answer, after the call (CallFunction post-handler: the weapon's
// GetAdjustedAim script asks it for every shot, research/18). With the scope's picture up, the answer
// becomes the zoomed one for the same accuracy: undo the unzoomed lerp (the native's answers for accuracy
// 0 and 1), then ask the zoomed native for that accuracy (bIsZoomed set for that call only).
void OnAimError(ue3::Object* context, ue3::Function* /*function*/, void* result) {
    gAimErrorCalls.fetch_add(1, std::memory_order_relaxed);
    if (result == nullptr || !gScopeUp || !gAimedFire || context == nullptr ||
        reinterpret_cast<std::uintptr_t>(context) != weapon::Key()) {
        return;
    }
    float& error = *static_cast<float*>(result);
    auto const low = weapon::AimError(context, 0.0f);
    auto const high = weapon::AimError(context, 1.0f);
    if (!low || !high || !std::isfinite(error)) {
        return;
    }
    float const span = *high - *low;
    float const accuracy = std::abs(span) > 1e-6f ? std::clamp((error - *low) / span, 0.0f, 1.0f) : 0.0f;
    auto const zoomed = weapon::ZoomedAimError(context, accuracy);
    if (!zoomed) {
        return;  // already zoomed (the support grip's LT), or not the held weapon
    }
    if (!gLoggedAimed) {
        gLoggedAimed = true;
        log::Info("sights: first scoped shot with aimed fire: aim error {:.4f} -> {:.4f} (accuracy {:.2f}; the game's zoomed "
                  "spread, without its scope camera)",
            error, *zoomed, accuracy);
    }
    error = *zoomed;
    write_journal::Wrote("Sights.aim error (CF result)", &error);
    gAimedShots.fetch_add(1, std::memory_order_relaxed);
    ++gAimedShotsTotal;
}

double Dot(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

engine::UnrealVector Add(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

engine::UnrealVector Sub(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

engine::UnrealVector Times(engine::UnrealVector const& a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}

double Len(engine::UnrealVector const& a) {
    return std::sqrt(Dot(a, a));
}

XrVector3f ToXr(engine::UnrealVector const& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

engine::UnrealVector FromXr(XrVector3f const& v) {
    return {v.x, v.y, v.z};
}

// A tracking-space direction in the world (the frame's axes: tracking -Z, +X and +Y).
engine::UnrealVector DirectionToWorld(engine::TrackingFrame const& frame, XrVector3f const& v) {
    return engine::Apply(frame.rotation, {-static_cast<double>(v.z), v.x, v.y});
}

#if defined(MEVR_GAME_LE1)
// Actor.Trace on the player's pawn (which it ignores), directly through ProcessEvent (never a generated wrapper),
// with the function's own parameter block: the actor hit and, if the native's out parameter came back, where.
ue3::Object* TraceOnce(ue3::Object* pawn, engine::UnrealVector const& start, engine::UnrealVector const& end, engine::UnrealVector* hit) {
    ue3::Params parms(ue3::runtime::Get(), gTraceFunction);
    parms.Set("TraceEnd", ue3::Vector{static_cast<float>(end.x), static_cast<float>(end.y), static_cast<float>(end.z)});
    parms.Set("TraceStart", ue3::Vector{static_cast<float>(start.x), static_cast<float>(start.y), static_cast<float>(start.z)});
    parms.SetBool("bTraceActors", true);
    ue3::runtime::Call(pawn, gTraceFunction, parms);
    if (hit != nullptr) {
        ue3::Vector const at = parms.Get<ue3::Vector>("HitLocation").value_or(ue3::Vector{});
        *hit = {at.x, at.y, at.z};
    }
    return parms.Return<ue3::Object*>().value_or(nullptr);
}
#endif

// The world along the sight line: how far from the sight's centre it's hit (the range when nothing is), the actor.
// Bisects with the hit/no-hit answer when the hit point doesn't come back (as game/throwables does).
void TraceSightLine(engine::UnrealVector const& center, engine::UnrealVector const& axis) {
    gWorld.traced = false;
    gWorld.hitActor = nullptr;
#if defined(MEVR_GAME_LE1)
    ue3::Object* const pawn = gTraceFunction != nullptr ? player::Pawn(player::Controller()) : nullptr;
    if (pawn == nullptr) {
        return;
    }
    engine::UnrealVector const end = Add(center, Times(axis, kConvergeRangeUnits));
    engine::UnrealVector at{};
    ue3::Object* actor = TraceOnce(pawn, center, end, &at);
    gWorld.traced = true;
    gWorld.hit = actor != nullptr;
    gWorld.distance = kConvergeRangeUnits;
    gWorld.what.clear();
    if (actor == nullptr) {
        return;
    }
    double const along = Dot(Sub(at, center), axis);
    bool const plausible = (at.x != 0.0 || at.y != 0.0 || at.z != 0.0) && along >= 0.0 && along <= kConvergeRangeUnits + 1.0 &&
        Len(Sub(at, Add(center, Times(axis, along)))) < 10.0;
    if (plausible) {
        gWorld.distance = along;
    } else {
        double lo = 0.0;
        double hi = 1.0;
        for (int i = 0; i < 10; ++i) {
            double const mid = 0.5 * (lo + hi);
            if (ue3::Object* const a = TraceOnce(pawn, center, Add(center, Times(axis, kConvergeRangeUnits * mid)), nullptr)) {
                hi = mid;
                actor = a;
            } else {
                lo = mid;
            }
        }
        gWorld.distance = kConvergeRangeUnits * hi;
    }
    gWorld.what = ue3::runtime::FullName(actor);
    gWorld.hitActor = actor;
    if (!gLoggedTracePath) {
        gLoggedTracePath = true;
        log::Info("sights: the sight line's trace (Actor.Trace through ProcessEvent): the hit point {}", plausible
            ? "came back" : "didn't come back (a native's out parameter): found by bisecting with the hit/no-hit answer");
    }
#else
    (void)center;
    (void)axis;
#endif
}

engine::UnrealVector Cross(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// A world point in the gun's frame: behind the muzzle along the barrel, above the bore toward `top`, to the right
// (UE axes: up x forward = right).
sv::ModelPoint InGunFrame(engine::UnrealVector const& point, weapon::Muzzle const& muzzle, engine::UnrealVector const& top) {
    engine::UnrealVector const d = Sub(point, muzzle.location);
    return {static_cast<float>(-Dot(d, muzzle.barrel)), static_cast<float>(Dot(d, top)), static_cast<float>(Dot(d, Cross(top, muzzle.barrel)))};
}

// The held gun's model against its bore: its bones re-read every kRemeasureMs (the top, the sight's bone), logged once
// per weapon; the sight's bone followed every tick.
void MeasureModel(engine::UnrealVector const& topWorld, sv::Kind kind, float length, weapon::Muzzle const& muzzle, double nowMs) {
    std::uintptr_t const key = weapon::Key();
    bool const fresh = gModel.weapon != key || !gModel.measured;
    if (fresh || nowMs - gModel.measuredMs >= kRemeasureMs) {
        if (fresh) {
            gModel = {};
            gModel.weapon = key;
            gModel.startMs = nowMs;
        }
        if (auto const model = weapon::ModelInGunFrame(topWorld)) {
            gModel.measured = true;
            gModel.measuredMs = nowMs;
            // The bones come first in the list, then the sockets ("socket " before the name).
            std::vector<std::string> names;
            std::vector<sv::ModelPoint> bones;
            for (weapon::ModelPoint const& p : model->points) {
                names.push_back(p.name);
                if (p.name.rfind("socket ", 0) != 0) {
                    bones.push_back({static_cast<float>(p.back), static_cast<float>(p.up), static_cast<float>(p.side)});
                }
            }
            gModel.top = sv::ModelTop(bones, length);
            float const bounds = static_cast<float>(model->upMax);
            gModel.boundsTop = std::isfinite(bounds) ? std::optional<float>(bounds) : std::nullopt;
            if (fresh) {
                gModel.bone = sv::SightBone(kind, names);
                if (gModel.bone && *gModel.bone >= bones.size()) {
                    gModel.bone.reset();
                }
                gModel.boneName = gModel.bone ? names[*gModel.bone] : std::string();
                // Which of the game's models it is (research/21 section 16): the rifles' carry a measured optic.
#if defined(MEVR_GAME_LE1)
                ue3::Object* const component = weapon::HeldMesh();
                ue3::Object* const asset = component != nullptr && ue3::runtime::Ready() ? player::SkeletalMesh(component) : nullptr;
                gModel.mesh = asset != nullptr ? ue3::runtime::Get().NameText(asset) : std::string();
#endif
                gModel.optic = sv::OpticFor(gModel.mesh);
                gModel.opticApplies = gModel.optic && gModel.bone && _stricmp(gModel.boneName.c_str(), gModel.optic->bone) == 0;
                log::Info("sights: the {} model is {}{}", sv::WeaponName(static_cast<int>(weapon::Type())),
                    gModel.mesh.empty() ? std::string("unnamed") : gModel.mesh,
                    !gModel.optic ? std::string(": no optic of the game's on it (the generic placement)")
                    : gModel.opticApplies
                        ? std::format(": the game's {} sight: {:.1f} units behind its '{}' bone, {:.1f} {} it, radius {:.2f} units{} "
                                      "(research/21 section 16)",
                              kind == sv::Kind::Scope ? "scope's eyepiece" : "housing's rear opening", gModel.optic->back, gModel.boneName,
                              std::abs(gModel.optic->up), gModel.optic->up >= 0.0f ? "above" : "under", gModel.optic->lens,
                              *gModel.optic->hide != '\0' ? std::format("; '{}' hides while the scope's picture is up", gModel.optic->hide)
                                                          : std::string())
                        : std::format(": its optic belongs on the '{}' bone, but the sight's is '{}': the generic placement",
                              gModel.optic->bone, gModel.boneName.empty() ? std::string("none") : gModel.boneName));
                std::string points;
                for (weapon::ModelPoint const& p : model->points) {
                    points += std::format("{}{} ({:.1f} back, {:.1f} up, {:.1f} right)", points.empty() ? "" : "; ", p.name, p.back, p.up, p.side);
                }
                gModel.text = std::format("bounds {:.1f}..{:.1f} units behind the muzzle, {:.1f}..{:.1f} above the bore, {:.1f}..{:.1f} to "
                                          "its right; bones' top {}",
                    model->backMin, model->backMax, model->upMin, model->upMax, model->sideMin, model->sideMax,
                    gModel.top ? std::format("{:.1f}", *gModel.top) : std::string("-"));
                log::Info("sights: the {} model against its bore (gun units, as first drawn: it may still be unfolding and not yet "
                          "aimed): {}; {}{}",
                    sv::WeaponName(static_cast<int>(weapon::Type())), gModel.text, points.empty() ? std::string("no bones or sockets") : points,
                    gModel.bone ? std::format("; auto puts its sight on the '{}' bone, followed live", gModel.boneName)
                    : gModel.top ? std::format("; auto places its sight from the bones' top ({:.1f} above the bore; the bounds say {:.1f}, a "
                                               "loose box)", *gModel.top, gModel.boundsTop.value_or(0.0f))
                                 : std::string("; no plausible top: auto keeps the typical height"));
            }
        }
    }
    // The sight's bone, live: where it is in the gun's frame now, and when it has settled after the draw.
    gModel.live.reset();
    if (gModel.measured && gModel.bone) {
        if (auto const world = weapon::BoneWorld(*gModel.bone)) {
            sv::ModelPoint const p = InGunFrame(*world, muzzle, topWorld);
            if (std::isfinite(p.back) && std::isfinite(p.up) && std::isfinite(p.side) && p.back > -10.0f && p.back < 200.0f &&
                std::abs(p.up) < 30.0f && std::abs(p.side) < 30.0f) {
                gModel.live = p;
                float const moved = std::sqrt((p.back - gModel.reference.back) * (p.back - gModel.reference.back) +
                    (p.up - gModel.reference.up) * (p.up - gModel.reference.up) + (p.side - gModel.reference.side) * (p.side - gModel.reference.side));
                if (gModel.stillSinceMs == 0.0 || moved > kSettleUnits) {
                    if (gModel.stillSinceMs != 0.0) {
                        gModel.travelled += moved;
                        if (gModel.settledLogged && gModel.settleLogs < 4) {
                            gModel.settledLogged = false;  // it moves again: log where it settles next
                        }
                    }
                    gModel.reference = p;
                    gModel.stillSinceMs = nowMs;
                } else if (!gModel.settledLogged && nowMs - gModel.stillSinceMs >= kSettleMs && gModel.settleLogs < 4) {
                    gModel.settledLogged = true;
                    ++gModel.settleLogs;
                    log::Info("sights: the {}'s sight sits on its '{}' bone: {:.1f} units behind the muzzle, {:.1f} above the bore, {:.1f} "
                              "to its right (still for {:.1f} s, {:.1f} s after the draw; it moved {:.1f} units before)",
                        sv::WeaponName(static_cast<int>(weapon::Type())), gModel.boneName, p.back, p.up, p.side,
                        (nowMs - gModel.stillSinceMs) / 1000.0, (nowMs - gModel.startMs) / 1000.0, gModel.travelled);
                }
            }
        }
    }
}

// The sight's place on this gun: [Sights] <type>Place, else auto: on the model's sight bone (live), else the length
// share with the height from the bones' top.
sv::Gun PlaceFor(int type, sv::Gun const& gun, float length, float radius) {
    sv::Gun placed = sv::AutoPlace(type, gun, length);
    if (!gun.autoPlace || !gModel.measured || gModel.weapon != weapon::Key()) {
        return placed;
    }
    if (gModel.live) {
        placed.back = gModel.live->back;
        placed.height = gModel.live->up;
        placed.side = gModel.live->side;
        if (gModel.opticApplies) {
            // The game's model (research/21 section 16): the eyepiece's glass, or the housing's rear opening for a dot,
            // measured from this bone on the mesh.
            placed.back += gModel.optic->back;
            placed.height += gModel.optic->up;
        } else if (gun.kind == sv::Kind::Scope && !sv::IsEyepieceName(gModel.boneName)) {
            // A scope on a housing's bone, not its eyepiece's: the eyepiece is at its rear (a guess per type).
            placed.back += sv::HousingToEyepiece(type);
        }
    } else if (gModel.top) {
        if (auto const height = sv::HeightOnModel(gun.kind == sv::Kind::None ? sv::Kind::Dot : gun.kind, *gModel.top, radius)) {
            placed.height = *height;
        }
    }
    return placed;
}

XrVector3f Sub3(XrVector3f a, XrVector3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float Length3(XrVector3f a) {
    return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

// Why a sample is outside the ADS exit corridor (and our reach): the ADS off line.
std::string OutsideText(ads::Sample const& s, float reachExit) {
    if (!s.valid) {
        return "no measurement";
    }
    ads::Corridor const& c = ads::kExit;
    std::string out;
    auto const add = [&](std::string const& text) { out += (out.empty() ? "" : ", ") + text; };
    float const farLimit = c.farDepth + ads::FarExtension(std::max(s.sightToMuzzle, 0.0f));
    if (s.depth < c.nearDepth) {
        add(std::format("the eye {:.2f} m behind the muzzle (under {:.2f})", s.depth, c.nearDepth));
    }
    if (s.depth > farLimit) {
        add(std::format("the eye {:.2f} m behind the muzzle (past {:.2f})", s.depth, farLimit));
    }
    if (s.lateral > c.lateral) {
        add(std::format("{:.1f} cm beside the bore (past {:.0f})", s.lateral * 100.0f, c.lateral * 100.0f));
    }
    if (s.height < c.below) {
        add(std::format("{:.1f} cm below the bore (past {:.0f})", -s.height * 100.0f, -c.below * 100.0f));
    }
    if (s.height > c.above) {
        add(std::format("{:.1f} cm above the bore (past {:.0f})", s.height * 100.0f, c.above * 100.0f));
    }
    if (s.facingDeg > c.facingDeg) {
        add(std::format("the head {:.0f} degrees off the barrel (past {:.0f})", s.facingDeg, c.facingDeg));
    }
    if (reachExit > 0.0f && s.eyeToSight > reachExit) {
        add(std::format("the eye {:.2f} m from the sight (past {:.2f})", s.eyeToSight, reachExit));
    }
    return out.empty() ? std::string("back inside") : out;
}

std::string SampleText(ads::Sample const& s) {
    return std::format("the centre eye {:.2f} m behind the muzzle, {:.1f} cm beside the bore, {:.1f} cm {} it, the head {:.1f} "
                       "degrees off the barrel, {:.2f} m from the sight",
        s.depth, s.lateral * 100.0f, std::abs(s.height) * 100.0f, s.height >= 0.0f ? "above" : "below", s.facingDeg, s.eyeToSight);
}

float ReachExit(sv::Settings const& settings) {
    return settings.adsReachM > 0.0f ? settings.adsReachM + kAdsReachHysteresisM : 0.0f;
}

// How far this sight may slide toward the eye (research/21 section 15, Overlord's per-optic distances): a red dot none, a
// holo 5 cm, a low-power scope (under 3x) 10 cm and other scopes scopePullMaxM (15 cm), all no further than the relief;
// a scope under ADS scopeAdsPullMaxM (35 cm). scopePullMaxM = 0: no scope pulls.
float PullLimit(sv::Settings const& settings, sv::Sight const& sight, bool adsOn) {
    if (!settings.assist.enabled) {
        return 0.0f;
    }
    switch (static_cast<sv::Kind>(sight.kind)) {
    case sv::Kind::Holo: return ads::PullDistance(ads::Optic::Holographic);
    case sv::Kind::Scope: {
        if (!(settings.scopePullMaxM > 0.0f) || !(settings.scopeReliefM > 0.0f)) {
            return 0.0f;
        }
        float const before = sight.zoom < 3.0f ? std::min(ads::PullDistance(ads::Optic::LowPower), settings.scopePullMaxM)
                                               : settings.scopePullMaxM;
        return adsOn ? std::max(before, settings.scopeAdsPullMaxM) : before;
    }
    default: return 0.0f;
    }
}

void ResetAssist(char const* why) {
    if (gAds) {
        log::Info("sights: ADS off ({}): the scope's picture goes; the scope was pulled at most {:.1f} cm", why, gRaisePullMax * 100.0f);
    }
    if (gAssisting) {
        log::Info("sights: alignment assist off ({})", why);
    }
    gSpring.Reset();
    gTakenNow = {};
    gTakenPrev = {};
    gGoal = {};
    gAssistEye = -1;
    gAssisting = false;
    gAdsPolicy.Reset();
    gAdsSample = {};
    gAdsGate = false;
    gAds = false;
    gApproach = gDistanceWeight = gPullLimit = 0.0f;
    gFastSpring = false;
}

// The assist's step for this tick's sight (as drawn: it carries gTakenPrev), and the ADS latch.
void StepAssist(sv::Settings const& settings, sv::Sight const& drawn, vr::aim_ray::Ray const& ray, double nowMs) {
    double const dt = gLastTickMs > 0.0 ? std::clamp((nowMs - gLastTickMs) / 1000.0, 0.0, 0.15) : 0.0;
    gLastTickMs = nowMs;
    auto const frame = view_anchor::Current();
    if (drawn.kind == static_cast<std::int32_t>(sv::Kind::None) || !frame) {
        ResetAssist("no sight");
        return;
    }
    XrPosef const& head = frame->head;
    XrVector3f const right = vr::overlay::Rotate(head.orientation, {1.0f, 0.0f, 0.0f});
    XrVector3f const forward = vr::overlay::Rotate(head.orientation, {0.0f, 0.0f, -1.0f});
    // The eyes about the head's centre (the view's eye midpoint), a typical IPD apart [verify: the snapshot's
    // eye poses are 6.4 cm apart on this headset].
    constexpr float kHalfIpd = 0.032f;
    XrVector3f const eyes[2] = {{head.position.x - right.x * kHalfIpd, head.position.y - right.y * kHalfIpd, head.position.z - right.z * kHalfIpd},
        {head.position.x + right.x * kHalfIpd, head.position.y + right.y * kHalfIpd, head.position.z + right.z * kHalfIpd}};
    // The sight and the muzzle without the shift already in the drawn gun, so neither the assist nor ADS chases itself.
    XrVector3f const center = Sub3(drawn.center, gTakenPrev);
    XrVector3f const muzzle = Sub3(ray.origin, gTakenPrev);
    bool const scope = drawn.kind == static_cast<std::int32_t>(sv::Kind::Scope);
    // ADS (Overlord's corridor on the centre eye, its dwell and hysteresis, and our reach to the sight).
    gAdsSample = ads::Measure(head.position, forward, muzzle, center, ray.direction, drawn.up);
    bool const wasAds = gAds;
    gAdsGate = scope && settings.ads;
    if (gAdsGate) {
        ads::Reach const reach{settings.adsReachM, ReachExit(settings)};
        gAds = gAdsPolicy.Update(ads::Inside(gAdsSample, gAdsPolicy.Active(), reach), static_cast<float>(dt));
    } else {
        gAdsPolicy.Reset();
        gAds = false;
    }
    // The continuous weight: how raised the gun is (Overlord's approach), times how near the sight is to the eye.
    gApproach = ads::Approach(gAdsSample);
    gDistanceWeight = ads::DistanceWeight(gAdsSample.eyeToSight, gAds);
    float const weight = settings.assist.enabled ? gApproach * gDistanceWeight : 0.0f;
    gPullLimit = gPull.reliefM > 0.0f ? PullLimit(settings, drawn, gAds) : 0.0f;
    gPull.maxM = gPullLimit;
    sv::Assist assist = settings.assist;
    if (!scope) {
        assist.maxShiftM = std::min(assist.maxShiftM, assist.dotMaxShiftM);  // a dot's or holo's sideways move is gentle
    }
    gGoal = sv::AssistTarget(assist, center, drawn.axis, drawn.up, eyes, weight, gAssistEye, gPull);
    gAssistEye = gGoal.eye;
    // The spring: Overlord's ADS transition, its faster (thermal) profile while ADS pulls further and while a pull past the
    // ordinary limit eases back.
    auto const along = [&](XrVector3f const& v) { return std::max(0.0f, -(v.x * drawn.axis.x + v.y * drawn.axis.y + v.z * drawn.axis.z)); };
    float const ordinary = gPull.reliefM > 0.0f ? PullLimit(settings, drawn, false) : 0.0f;
    gFastSpring = gAds || along(gSpring.Value()) > ordinary + 0.01f;
    ads::SpringProfile const profile = gFastSpring ? ads::kFastSpring : ads::kNormalSpring;
    XrVector3f shift{};
    if (settings.assist.enabled) {
        shift = gSpring.Update(gGoal.shift, static_cast<float>(dt), profile.frequency, profile.maxSpeed);
    } else {
        gSpring.Reset();
    }
    gMaxShift = std::max(gMaxShift, Length3(shift));
    // How much of the spring's move is the pull (back along the line toward the eye), and where that leaves the eye.
    float const pulled = along(shift);
    bool const on = gAssisting ? gGoal.weight >= kAssistOffWeight : gGoal.weight >= kAssistOnWeight;
    if (gAds && !wasAds) {
        gRaisePullMax = 0.0f;
        gRaiseAlongBefore = gRaiseAlongAfter = gGoal.along;
        gAdsSinceMs = nowMs;
        ++gAdsOnsHeartbeat;
        log::Info("sights: ADS on ({}; inside the entry corridor for {:.0f} ms): the scope's picture shows and the scope may be pulled "
                  "{:.2f} m toward the eye, to {:.2f} m in front of it ({:.2f} m wanted now, the {} eye {:.2f} m behind the eyepiece); "
                  "approach weight {:.2f}",
            SampleText(gAdsSample), ads::kDwellS * 1000.0f, gPullLimit, gPull.reliefM,
            std::clamp(gGoal.along - gPull.reliefM, 0.0f, gPullLimit), gGoal.eye == 0 ? "left" : "right", gGoal.along, gApproach);
    }
    if (on || gAds) {
        if (on) {
            gOffsetSumX += gGoal.x;
            gOffsetSumY += gGoal.y;
            gOffsetSumAngle += gGoal.angleDeg;
            ++gOffsetTicks;
        }
        if (gPullLimit > 0.0f) {
            gPullSumHeartbeat += pulled;
            ++gPullTicksHeartbeat;
            if (pulled >= gRaisePullMax) {
                gRaisePullMax = pulled;
                gRaiseAlongBefore = gGoal.along;
                gRaiseAlongAfter = gGoal.along - pulled;
            }
        }
    }
    if (gAds) {
        ++gAdsTicksHeartbeat;
        gAdsEyeSumHeartbeat += gGoal.along - pulled;
    }
    if (!gAds && wasAds) {
        log::Info("sights: ADS off ({}; outside the exit corridor for {:.0f} ms) after {:.1f} s: the scope's picture goes and the scope "
                  "eases back; pulled at most {:.1f} cm: the eye {:.2f} m behind the eyepiece before, {:.2f} m after (relief {:.2f} m{})",
            OutsideText(gAdsSample, ReachExit(settings)), ads::kDwellS * 1000.0f, (nowMs - gAdsSinceMs) / 1000.0, gRaisePullMax * 100.0f,
            gRaiseAlongBefore, gRaiseAlongAfter, gPull.reliefM,
            gRaiseAlongBefore - gPull.reliefM > PullLimit(settings, drawn, true) + 0.005f ? ": the pull's limit was reached" : "");
    }
    if (on != gAssisting) {
        gAssisting = on;
        if (on) {
            if (!gAds) {
                gRaisePullMax = 0.0f;
                gRaiseAlongBefore = gRaiseAlongAfter = gGoal.along;
            }
            log::Info("sights: alignment assist on: the {} eye {:.1f} cm {} and {:.1f} cm {} the sight line ({:.1f} degrees, the sight "
                      "{:.2f} m ahead): the gun slides {:.1f} cm toward it ({:.0f}% weight: approach {:.2f}, distance {:.2f}){}",
                gGoal.eye == 0 ? "left" : "right", std::abs(gGoal.x) * 100.0f, gGoal.x >= 0.0f ? "right of" : "left of",
                std::abs(gGoal.y) * 100.0f, gGoal.y >= 0.0f ? "above" : "below", gGoal.angleDeg, gGoal.along,
                Length3(gGoal.shift) * 100.0f, gGoal.weight * 100.0f, gApproach, gDistanceWeight,
                gPullLimit > 0.0f ? std::format("; pulled {:.1f} cm back along its line (at most {:.2f} m, to {:.2f} m in front of the eye)",
                                        gGoal.pull * 100.0f, gPullLimit, gPull.reliefM)
                                  : std::string());
        } else {
            log::Info("sights: alignment assist off (the gun lowered or turned away: {}); the gun eases back{}", SampleText(gAdsSample),
                gPullLimit > 0.0f || gRaisePullMax > 0.0f
                    ? std::format("; pulled at most {:.1f} cm: the eye {:.2f} m behind the sight before, {:.2f} m after", gRaisePullMax * 100.0f,
                          gRaiseAlongBefore, gRaiseAlongAfter)
                    : std::string());
        }
    }
}

// The camera's near plane in game units: the local player's NearClipPlane, else UE3's default.
float NearUnits() {
#if defined(MEVR_GAME_LE1)
    ue3::Object* const pc = player::Controller();
    ue3::Object* const localPlayer = pc != nullptr ? gControllerPlayer.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
    if (auto const plane = localPlayer != nullptr ? gNearClipPlane.Read<float>(localPlayer) : std::nullopt;
        plane && std::isfinite(*plane) && *plane > 0.0f && *plane < 200.0f) {
        gNearRead = true;
        return *plane;
    }
#endif
    gNearRead = false;
    return kDefaultNearUnits;
}

std::string ViewText(std::uint32_t view) {
    if (view == 0) {
        return "no eye through it";
    }
    std::string eyes = (view & sv::kViewLeft) != 0 && (view & sv::kViewRight) != 0 ? "both eyes"
        : (view & sv::kViewLeft) != 0                                                ? "the left eye"
                                                                                     : "the right eye";
    return eyes + ((view & sv::kViewScope) != 0 ? " sees the scope's picture" : " looks through it");
}

// The gun's top for a barrel along `direction`: arm IK rolls the gun with the right controller (its aim pose's up, as
// far as the barrel allows), so the same up places the sight on top of it. Calibrated (research/25): arm IK holds the
// gun level in the calibrated gun frame, so its top is that up. Nullopt when the barrel points straight up or down.
std::optional<XrVector3f> TopFor(int type, XrVector3f const& direction) {
    vr::controllers::State const controllers = vr::controllers::Latest();
    XrVector3f aimUp = controllers.active && controllers.right.aimValid
        ? vr::overlay::Rotate(controllers.right.aim.orientation, {0.0f, 1.0f, 0.0f})
        : XrVector3f{0.0f, 1.0f, 0.0f};
    if (auto const stance = body_calibration::StanceFor(type); stance && controllers.active && controllers.right.gripValid) {
        aimUp = vr::calibration::Rotate(vr::calibration::GunOrientation(controllers.right.grip.orientation, *stance), {0.0f, 1.0f, 0.0f});
    }
    auto top = sv::TopOf(direction, aimUp);
    if (!top) {
        top = sv::TopOf(direction, {0.0f, 1.0f, 0.0f});
    }
    return top;
}

// The held weapon's own magnifications (research/21 section 16), once per weapon: its stats object's AimModes.
void ReadGameZoom(int type) {
    std::uintptr_t const key = weapon::Key();
    if (gZoom.read && gZoom.weapon == key) {
        return;
    }
    gZoom = {};
    gZoom.weapon = key;
    gZoom.read = true;
#if defined(MEVR_GAME_LE1)
    ue3::Object* const held = weapon::HeldObject();
    ue3::Object* const stats = held != nullptr ? gWeaponStats.Read<ue3::Object*>(held).value_or(nullptr) : nullptr;
    std::string modes;
    if (stats != nullptr) {
        gZoom.stats = ue3::runtime::FullName(stats);
        if (auto const elements = gAimModes.ElementsOf(stats, 16)) {
            for (std::int32_t i = 0; i < elements->count; ++i) {
                void const* const mode = elements->At(i);
                float const fov = gAimZoomFov.Read<float>(mode).value_or(0.0f);
                bool const scoped = gAimScoped.ReadBool(mode).value_or(false);
                sv::AddAimMode(gZoom.game, fov, scoped);
                modes += std::format("{}{:.1f} degrees{}", modes.empty() ? "" : ", ", fov,
                    scoped ? std::format(" (a scope, {:.1f}x)", sv::ZoomFromFov(fov)) : std::string(" (not a scope)"));
            }
        }
    }
    log::Info("sights: the {}'s own aim modes ({}.AimModes): {}{}", sv::WeaponName(type),
        gZoom.stats.empty() ? std::string("no stats object") : gZoom.stats, modes.empty() ? std::string("none read") : modes,
        gZoom.game.count > 0 ? std::format("; a scope there shows {:.1f}x{}", gZoom.game.zoom[0],
                                   gZoom.game.count > 1 ? std::format(", {:.1f}x after a flick of the right stick forward while aiming down "
                                                                      "it", gZoom.game.zoom[1])
                                                        : std::string())
                             : std::string());
#else
    (void)type;
#endif
}

// The fallback magnification when the game's can't be read (or the gun has no scoped mode of its own).
float FallbackZoom(int type) {
    return type == 3 ? 4.0f : 1.5f;
}

// The magnification shown: [Sights] <type> = scope N, else the held weapon's own at the chosen level. A flick of the right
// stick forward (back) while aiming down the scope steps to the next (previous) of them.
float ZoomFor(sv::Settings const& settings, int type, sv::Gun const& gun) {
    if (gun.kind != sv::Kind::Scope) {
        return 1.0f;
    }
    if (gun.zoom >= 1.0f) {
        return gun.zoom;
    }
    ReadGameZoom(type);
    if (settings.zoomStick && gZoom.game.count > 1 && gAds) {
        vr::controllers::State const controllers = vr::controllers::Latest();
        int const step = controllers.active ? gZoom.flick.Update(controllers.right.stickY) : 0;
        int const level = std::clamp(gZoom.level + step, 0, gZoom.game.count - 1);
        if (step != 0 && level != gZoom.level) {
            gZoom.level = level;
            if (gHaptic != nullptr) {
                gHaptic(1, 0.03f, 0.35f);  // a detent under the thumb
            }
            log::Info("sights: the {}'s scope zooms {} to {:.1f}x (the game's aim mode {} of {}, {:.1f} degrees)", sv::WeaponName(type),
                step > 0 ? "in" : "out", sv::ZoomAt(gZoom.game, level), level + 1, gZoom.game.count, gZoom.game.fovDeg[level]);
        }
    } else {
        gZoom.flick.Reset();
    }
    float const zoom = sv::ZoomAt(gZoom.game, gZoom.level);
    return zoom >= 1.0f ? zoom : FallbackZoom(type);
}

// The eyepiece's radius (gun units): <type>Lens, else the game's model's own, else lensUnits.
float LensFor(sv::Settings const& settings, int type) {
    if (type >= 0 && type < sv::kWeaponTypes && settings.guns[type].lens > 0.0f) {
        return settings.guns[type].lens;
    }
    if (gModel.opticApplies && gModel.weapon == weapon::Key()) {
        return gModel.optic->lens;
    }
    return settings.lensUnits;
}

// The game's scope art (research/21 section 16): its textures found in GObjects (by class, name and package), once, again
// every 10 s while any is missing; then each tick their resources, published with a new generation when they change.
void PublishArt(sv::Sight& out, double nowMs) {
    namespace art = vr::scope_art;
#if defined(MEVR_GAME_LE1)
    if (!ue3::runtime::Ready()) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    for (ue3::Object*& t : gArt.textures) {
        if (t != nullptr && (!ue3::runtime::IsAlive(t) || !ue3::IsA(t, gTexture2D))) {
            t = nullptr;
            gArt.found = false;
        }
    }
    if (!gArt.found && nowMs - gArt.searchedMs >= kArtSearchEveryMs) {
        gArt.searchedMs = nowMs;
        ++gArt.searches;
        ue3::Class* const cls = gTexture2D.Get();
        std::int32_t const count = cls != nullptr ? r.ObjectCount() : 0;
        for (std::int32_t i = 0; i < count; ++i) {
            ue3::Object* const object = r.ObjectAt(i);
            if (object == nullptr || r.ClassOf(object) != cls) {
                continue;
            }
            for (int k = 0; k < art::kTextures; ++k) {
                if (gArt.textures[k] == nullptr && r.NameIs(object, art::Info(k).name) && r.NameIs(r.OuterOf(object), art::kPackage) &&
                    r.IsAlive(object)) {
                    gArt.textures[k] = object;
                }
            }
        }
        gArt.found = std::all_of(std::begin(gArt.textures), std::end(gArt.textures), [](ue3::Object* t) { return t != nullptr; });
        std::string missing;
        for (int k = 0; k < art::kTextures; ++k) {
            if (gArt.textures[k] == nullptr) {
                missing += std::format("{}{}.{}", missing.empty() ? "" : ", ", art::kPackage, art::Info(k).name);
            }
        }
        gArt.text = gArt.found ? "found" : "not found: " + missing;
        if (gArt.found || gArt.searches <= 1) {
            log::Info("sights: the game's scope art {} ({} objects searched{})", gArt.found ? "found" : gArt.text, count,
                gArt.found ? "; the host composes it once" : "; our own reticle meanwhile, looked for again every 10 s");
        }
    }
    if (!gArt.found) {
        return;
    }
    sv::Sight::ArtTexture now[art::kTextures]{};
    for (int k = 0; k < art::kTextures; ++k) {
        now[k].resource = gTextureResource.Read<std::uint64_t>(gArt.textures[k]).value_or(0);
        now[k].width = static_cast<std::uint32_t>(std::max(gTextureSizeX.Read<std::int32_t>(gArt.textures[k]).value_or(0), 0));
        now[k].height = static_cast<std::uint32_t>(std::max(gTextureSizeY.Read<std::int32_t>(gArt.textures[k]).value_or(0), 0));
        if (now[k].resource == 0 || now[k].width != art::Info(k).width || now[k].height != art::Info(k).height) {
            gArt.text = std::format("{} has no resource or another size ({}x{})", art::Info(k).name, now[k].width, now[k].height);
            return;  // not loaded (or another movie's art): our own reticle
        }
    }
    if (std::memcmp(now, gArt.published, sizeof(now)) != 0) {
        std::memcpy(gArt.published, now, sizeof(now));
        ++gArt.generation;
        gArt.text = std::format("published, generation {}", gArt.generation);
    }
    std::memcpy(out.art, gArt.published, sizeof(out.art));
    out.artGeneration = gArt.generation;
#else
    (void)out;
    (void)nowMs;
#endif
}

} // namespace

void SetHaptic(void (*pulse)(int hand, float seconds, float amplitude)) {
    gHaptic = pulse;
}

std::optional<XrVector3f> GunTop(vr::aim_ray::Ray const& ray) {
    if (!ray.valid) {
        return std::nullopt;
    }
    return TopFor(static_cast<int>(weapon::Type()), ray.direction);
}

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    gTraceFunction = gTrace.Get();
    if (gTraceFunction == nullptr) {
        log::Warn("sights: Actor.Trace not found: shots keep the barrel's line (no convergence on the sight)");
    }
    gAimErrorFunction = gGetPlayerAimError.Get();
    if (gAimErrorFunction == nullptr) {
        log::Warn("sights: BioWeaponRanged.GetPlayerAimError not found: no aimed fire through a scope (the sights still draw)");
        return false;
    }
    engine::call_function::AddPostHandler(gAimErrorFunction, &OnAimError);
    return true;
#else
    return false;
#endif
}

sv::Sight Current(vr::aim_ray::Ray const& ray) {
    sv::Settings const settings = hand_calibration::GetSights();
    gAimedFire = settings.aimedFire;
    sv::Sight out{};
    out.kind = static_cast<std::int32_t>(sv::Kind::None);
    gPull = {};  // set below for a scope with the pull on
    std::string status;
    int const type = static_cast<int>(weapon::Type());
    if (!settings.enabled) {
        status = "off ([Sights] enabled = 0)";
    } else if (!ray.valid) {
        status = "none (no gun aimed along its barrel)";
    } else if (type < 0 || type >= sv::kWeaponTypes) {
        status = "none (the held weapon's type is unknown)";
    } else if (settings.guns[type].kind == sv::Kind::None) {
        status = std::format("none for the {} ([Sights] {} = none)", sv::WeaponName(type), sv::WeaponKey(type));
    } else {
        auto const muzzle = weapon::Current();
        auto const frame = view_anchor::Current();
        if (!muzzle || !frame || !(frame->unitsPerMeter > 0.0)) {
            status = "none (no muzzle or no view)";
        } else {
            sv::Gun const& gun = settings.guns[type];
            float const unitsPerMeter = static_cast<float>(frame->unitsPerMeter);
            engine::UnrealVector const toMuzzle{muzzle->location.x - muzzle->origin.x, muzzle->location.y - muzzle->origin.y,
                muzzle->location.z - muzzle->origin.z};
            float const length = static_cast<float>(Dot(toMuzzle, muzzle->barrel));
            auto const top = TopFor(type, ray.direction);
            if (!top) {
                status = "none (the barrel points straight up or down: no top)";
            } else {
                bool const scope = gun.kind == sv::Kind::Scope;
                engine::UnrealVector const topWorld = DirectionToWorld(*frame, *top);
                double const nowMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
                MeasureModel(topWorld, gun.kind, length, *muzzle, nowMs);
                float const lensUnits = LensFor(settings, type);  // the model's own eyepiece once it's known
                float const zoom = ZoomFor(settings, type, gun);
                sv::Gun const placed = PlaceFor(type, gun, length, scope ? lensUnits : settings.windowUnits);
                gPlaced = placed;
                gPlacedWeapon = weapon::Key();
                out.kind = static_cast<std::int32_t>(gun.kind);
                out.weapon = type;
                out.flags = (settings.frame && !scope ? sv::kFrame : 0u) | (settings.replaceRing ? sv::kReplaceRing : 0u) |
                    (settings.otherEye ? sv::kOtherEye : 0u);
                out.eyeBox = std::max(settings.eyeBoxUnits, settings.windowUnits) / unitsPerMeter;
                out.center = sv::Place(ray.origin, ray.direction, *top, placed.back / unitsPerMeter, placed.height / unitsPerMeter,
                    placed.side / unitsPerMeter);
                out.axis = sv::Zeroed(out.center, ray.origin, ray.direction, settings.zeroM * 100.0f / unitsPerMeter);
                out.up = sv::TopOf(out.axis, *top).value_or(*top);
                out.radius = (scope ? lensUnits : settings.windowUnits) / unitsPerMeter;
                out.zoom = scope ? zoom : 1.0f;
                out.dotSizeDeg = settings.dotSizeDeg;
                out.ringSizeDeg = settings.ringSizeDeg;
                out.dotColor = settings.dotColor;
                out.scopeColor = settings.scopeColor;
                out.scopeReticle = static_cast<std::int32_t>(settings.scopeReticle);
                out.focusM = settings.focusM * 100.0f / unitsPerMeter;
                out.eyeReliefM = settings.eyeReliefM;
                out.resolution = settings.resolution;
                out.scopeFieldDeg = settings.scopeFieldDeg;
                out.scopeEyeBox = settings.scopeEyeBoxM;
                out.scopeMaxM = settings.scopeMaxM;
                // Aiming down the sight gates a scope's picture (research/21 section 15): the mod publishes the scope only
                // under ADS, whose reach ends at adsReachM + 0.10 m, so the host's own range (scopeMaxM) is set past that and
                // never cuts the picture under ADS.
                if (scope && settings.ads) {
                    float const reach = ReachExit(settings);
                    out.scopeMaxM = reach > 0.0f ? reach + 0.10f : std::max(settings.scopeMaxM, 1.2f);
                }
                // The game's own scope view (research/21 section 16), published with the scope even while its picture waits
                // for ADS, so the host has it composed before the first look.
                if (scope && settings.scopeReticle == sv::ScopeReticle::Game) {
                    PublishArt(out, nowMs);
                }
                // The pull to the eye (research/21 sections 14-15): the sight comes to the relief in front of the aiming eye
                // (scopes, and a holo by 5 cm), and a scope's picture is the model's eyepiece seen from there (the host's
                // virtual eyepiece with the field the eyepiece subtends at the relief: exactly the eyepiece once pulled).
                // How far it may slide this tick (ADS or not) is StepAssist's (PullLimit).
                gPull = {};
                bool const scopePull = scope && settings.scopePullMaxM > 0.0f && settings.scopeReliefM > 0.0f;
                bool const pullOn = settings.assist.enabled && (scopePull || gun.kind == sv::Kind::Holo);
                if (pullOn) {
                    gNearUnits = NearUnits();
                    gPull.reliefM = sv::ReliefFor(settings.scopeReliefM, gNearUnits, unitsPerMeter);
                    if (scope && settings.scopeFieldDeg > 0.0f) {
                        out.scopeFieldDeg = sv::PulledFieldDeg(out.radius, gPull.reliefM);
                    }
                    float const ordinary = PullLimit(settings, out, false);
                    float const underAds = scope && settings.ads ? PullLimit(settings, out, true) : ordinary;
                    std::string const key = std::format("{:.1f} {} {:.1f} {:.3f} {:.2f} {:.2f} {} {:.2f} {}", gNearUnits, gNearRead, unitsPerMeter,
                        gPull.reliefM, ordinary, underAds, type, lensUnits, static_cast<int>(gun.kind));
                    if (key != gLoggedRelief) {
                        gLoggedRelief = key;
                        log::Info("sights: the camera's near plane is {:.1f} units ({}; {:.3f} m at {:.1f} units per meter): the {}'s {} is "
                                  "pulled to {:.3f} m in front of the eye ([Sights] scopeReliefM {:.2f}, at least the near plane + 2 cm; "
                                  "Overlord's 8 cm would cut the tube), at most {:.2f} m{}{}",
                            gNearUnits, gNearRead ? "LocalPlayer.NearClipPlane" : "UE3's default: the field wasn't read",
                            gNearUnits / unitsPerMeter, unitsPerMeter, sv::WeaponName(type), scope ? "scope" : "holo", gPull.reliefM,
                            settings.scopeReliefM, ordinary,
                            underAds != ordinary ? std::format(", {:.2f} m while aiming down it (ADS)", underAds) : std::string(),
                            scope ? std::format("; its eyepiece ({:.1f} units, {:.1f} cm across) then looks {:.1f} degrees across", lensUnits,
                                        2.0f * out.radius * 100.0f, sv::PulledFieldDeg(out.radius, gPull.reliefM))
                                  : std::string());
                    }
                }
                // The same sight in the world, for the shots (research/21 section 10): the line traced this tick.
                gWorld.valid = true;
                gWorld.weapon = weapon::Key();
                gWorld.back = placed.back;
                gWorld.height = placed.height;
                gWorld.side = placed.side;
                gWorld.top = topWorld;
                gWorld.zeroUnits = settings.zeroM * 100.0;
                gWorld.converge = settings.converge;
                if (settings.converge) {
                    engine::UnrealVector const center = Add(Add(Sub(muzzle->location, Times(muzzle->barrel, placed.back)),
                        Times(topWorld, placed.height)), Times(Cross(topWorld, muzzle->barrel), placed.side));
                    engine::UnrealVector const axis = FromXr(sv::Zeroed(ToXr(Sub(center, muzzle->location)), {0.0f, 0.0f, 0.0f},
                        ToXr(muzzle->barrel), static_cast<float>(gWorld.zeroUnits)));
                    TraceSightLine(center, axis);
                } else {
                    gWorld.traced = false;
                }
                // Logged on change: a sight on a bone moves while the gun unfolds, so its numbers go in their own line
                // (MeasureModel's "sits on its bone") and the heartbeat, not here.
                status = std::format("the {} has a {} sight{}: {} ({}; the gun is "
                                     "{:.1f} units from muzzle to grip), {} {:.1f} units ({:.3f} m at {:.1f} units per meter), eye box "
                                     "{:.1f} units{}{}; shots {}",
                    sv::WeaponName(type), sv::Name(gun.kind),
                    scope ? std::format(" ({:.1f}x{}, {} reticle)", zoom, gun.zoom >= 1.0f ? "" : gZoom.game.count > 0 ? ": the game's" : ": the "
                                                                                                    "game's couldn't be read",
                                sv::Name(settings.scopeReticle))
                          : std::string(),
                    gModel.live && gun.autoPlace ? std::format("on the model's '{}' bone", gModel.boneName)
                                                 : std::format("{:.1f} units behind the muzzle, {:.1f} above the bore{}", placed.back,
                                                       placed.height, placed.side != 0.0f ? std::format(", {:.1f} right", placed.side) : std::string()),
                    !gun.autoPlace ? "set"
                    : gModel.live  ? "auto: followed live as the gun unfolds"
                    : gModel.top   ? "auto: the length share, the height from the bones' top"
                                   : "auto",
                    length, scope ? "eyepiece radius" : "window radius", scope ? lensUnits : settings.windowUnits, out.radius,
                    unitsPerMeter, std::max(settings.eyeBoxUnits, settings.windowUnits),
                    scope && out.scopeFieldDeg > 0.0f
                        ? std::format("; virtual eyepiece: the picture {:.1f} degrees across{} with the eye up to {:.2f} m behind it, eye box "
                                      "{:.3f} m", out.scopeFieldDeg,
                              gPull.reliefM > 0.0f ? std::format(" (the eyepiece seen from {:.3f} m, where it's pulled)", gPull.reliefM)
                                                   : std::string(),
                              out.scopeMaxM, settings.scopeEyeBoxM)
                        : std::string(),
                    scope && settings.ads
                        ? std::format("; the picture only while aiming down it (ADS: the centre eye within {:.2f} m of the eyepiece and in "
                                      "Overlord's corridor for {:.0f} ms)", settings.adsReachM, ads::kDwellS * 1000.0f)
                        : std::string(),
                    settings.converge ? "converge on what the sight line hits" : "parallel to the sight line ([Sights] converge = 0)");
            }
        }
    }
    if (out.kind == static_cast<std::int32_t>(sv::Kind::None)) {
        gWorld.valid = false;
        gWorld.traced = false;
        // The next draw measures the model again (and logs where its sight bone settles after that draw's unfolding).
        if (gModel.weapon != 0 && gModel.weapon != weapon::Key()) {
            gModel = {};
        }
    }
    gStatus = status;
    // Logged when it changes, not with each tick's hand motion (the numbers above only change with the gun,
    // the settings or the view's scale).
    if (status != gLoggedStatus) {
        gLoggedStatus = status;
        log::Info("sights: {}", status);
    }
    gLast = out;
    gLastWeapon = weapon::Key();
    double const nowMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    StepAssist(settings, out, ray, nowMs);
    // A scope waiting for ADS isn't published: the host draws no picture (the model's own lens shows, the ring reticle
    // stays), and game/scope_capture doesn't render. The shots, the assist and the model keep the sight as placed.
    sv::Sight published = out;
    if (gAdsGate && !gAds) {
        published.kind = static_cast<std::int32_t>(sv::Kind::None);
    }
    // The gun kick (game/gun_kick, research/24 section 8): everything above is the gun as the controller holds it (game/weapon
    // takes the kick out of its readings), so ADS, the assist and the shots' convergence never see a kick. What the host draws
    // keeps that line (the dot, the scope's picture and its capture look where the shots go) but sits on the model as drawn:
    // the window (a dot's frame, a scope's eyepiece) moves with the kicked gun.
    if (published.kind != static_cast<std::int32_t>(sv::Kind::None)) {
        if (auto const frame = view_anchor::Current()) {
            if (auto const drawn = weapon::ToDrawn(engine::ToWorld(*frame, published.center))) {
                published.center = engine::ToTracking(*frame, *drawn);
            }
        }
    }
    return published;
}

std::optional<ik::Vec3> SightInHand(weapon::Grip const& grip, ik::Vec3 const& upInHand) {
    int const type = static_cast<int>(weapon::Type());
    auto const muzzle = weapon::Current();
    if (type < 0 || type >= sv::kWeaponTypes || !muzzle) {
        return std::nullopt;
    }
    sv::Settings const settings = hand_calibration::GetSights();
    engine::UnrealVector const toMuzzle{muzzle->location.x - muzzle->origin.x, muzzle->location.y - muzzle->origin.y,
        muzzle->location.z - muzzle->origin.z};
    // The place the sight was drawn at (the model's height included), else the type's typical one.
    sv::Gun const placed = gPlacedWeapon != 0 && gPlacedWeapon == weapon::Key()
        ? gPlaced
        : sv::AutoPlace(type, settings.guns[type], static_cast<float>(Dot(toMuzzle, muzzle->barrel)));
    ik::Vec3 const barrel = ik::Normalize(grip.barrel, {1.0, 0.0, 0.0});
    ik::Vec3 up = upInHand - barrel * ik::Dot(upInHand, barrel);
    if (!(ik::Length(up) > 1e-6)) {
        return std::nullopt;
    }
    up = ik::Normalize(up);
    ik::Vec3 const right = ik::Cross(up, barrel);  // UE axes: up x forward = right
    return grip.muzzle - barrel * static_cast<double>(placed.back) + up * static_cast<double>(placed.height) +
        right * static_cast<double>(placed.side);
}

std::optional<ShotAim> ShotAlongSight(weapon::Muzzle const& muzzle) {
    if (!gWorld.valid || !gWorld.converge || !gWorld.traced || gWorld.weapon == 0 || gWorld.weapon != weapon::Key()) {
        return std::nullopt;
    }
    // The sight line from the gun as it is now (the tick's top, made square to the live barrel).
    engine::UnrealVector const barrel = Times(muzzle.barrel, 1.0 / std::max(Len(muzzle.barrel), 1e-9));
    engine::UnrealVector top = Sub(gWorld.top, Times(barrel, Dot(gWorld.top, barrel)));
    double const topLength = Len(top);
    if (!(topLength > 1e-6)) {
        return std::nullopt;
    }
    top = Times(top, 1.0 / topLength);
    engine::UnrealVector const sightFromMuzzle =
        Add(Add(Times(barrel, -gWorld.back), Times(top, gWorld.height)), Times(Cross(top, barrel), gWorld.side));
    XrVector3f const axis = sv::Zeroed(ToXr(sightFromMuzzle), {0.0f, 0.0f, 0.0f}, ToXr(barrel), static_cast<float>(gWorld.zeroUnits));
    ShotAim aim;
    aim.direction = FromXr(sv::ConvergedShot(ToXr(sightFromMuzzle), axis, static_cast<float>(gWorld.distance), kMinAheadUnits));
    aim.sightLine = FromXr(axis);
    aim.sightCenter = Add(muzzle.location, sightFromMuzzle);
    aim.distance = gWorld.distance;
    aim.hit = gWorld.hit;
    aim.what = gWorld.what;
    aim.toSightDeg = sv::AngleDeg(ToXr(aim.direction), axis);
    aim.toBarrelDeg = sv::AngleDeg(ToXr(aim.direction), ToXr(barrel));
    return aim;
}

XrVector3f TakeGunShift() {
    gTakenPrev = gTakenNow;
    gTakenNow = gSpring.Value();
    return gTakenNow;
}

float RaisedWeight() {
    float const weight = gApproach * gDistanceWeight;
    return std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 0.0f;
}

void SetHostView(std::uint32_t view) {
    gHostView = view;
    // Under ADS only, when the picture waits for it (research/21 section 15): the lens part hides and aimed fire applies
    // only while the picture the host drew is the one ADS asked for.
    gScopeUp = (view & sv::kViewScope) != 0 && gLast.kind == static_cast<std::int32_t>(sv::Kind::Scope) &&
        gLastWeapon != 0 && gLastWeapon == weapon::Key() && (!gAdsGate || gAds);
    if (view != gLoggedHostView) {
        gLoggedHostView = view;
        log::Info("sights: {}", ViewText(view));
    }
    // The model's parts hidden while the scope's picture is up (the sniper's glowing lens; research/21 section 14).
    int const type = static_cast<int>(weapon::Type());
    std::string const hide = type >= 0 && type < sv::kWeaponTypes ? hand_calibration::GetSights().guns[type].hide : std::string();
    bool const ownModel = gModel.measured && gModel.weapon != 0 && gModel.weapon == weapon::Key();
    // "auto": the game's model's own parts (research/21 section 16), so the list follows the model as well as the setting.
    std::string const key = hide + "|" + (ownModel ? gModel.mesh : std::string()) + "|" + std::to_string(type);
    if (key != gHideText) {
        gHideText = key;
        gHideList = sv::HideList(hide, ownModel ? gModel.optic : std::nullopt, type);
    }
    scope_parts::Update(gScopeUp, gHideList, ownModel ? gModel.bone : std::nullopt, type);
}

namespace {

std::string AdsJson() {
    namespace json = vr::test_request;
    ads::Sample const& s = gAdsSample;
    return std::format("{{\"gate\": {}, \"active\": {}, \"entering\": {}, \"leaving\": {}, \"heldS\": {}, \"valid\": {}, \"depthM\": {}, "
                       "\"lateralM\": {}, \"heightM\": {}, \"facingDeg\": {}, \"eyeToSightM\": {}, \"sightToMuzzleM\": {}, "
                       "\"approach\": {}, \"distanceWeight\": {}, \"weight\": {}, \"pullLimitM\": {}, \"fastSpring\": {}}}",
        gAdsGate ? "true" : "false", gAds ? "true" : "false", gAdsPolicy.Entering() ? "true" : "false",
        gAdsPolicy.Leaving() ? "true" : "false", json::JsonNumber(gAdsPolicy.Held()), s.valid ? "true" : "false", json::JsonNumber(s.depth),
        json::JsonNumber(s.lateral), json::JsonNumber(s.height), json::JsonNumber(s.facingDeg), json::JsonNumber(s.eyeToSight),
        json::JsonNumber(s.sightToMuzzle), json::JsonNumber(gApproach), json::JsonNumber(gDistanceWeight),
        json::JsonNumber(gApproach * gDistanceWeight), json::JsonNumber(gPullLimit), gFastSpring ? "true" : "false");
}

// What came from the game (research/21 section 16): the model and its optic, the weapon's magnifications, the scope art.
std::string GameJson() {
    namespace json = vr::test_request;
    std::string zooms;
    for (int i = 0; i < gZoom.game.count; ++i) {
        zooms += std::format("{}{}", i == 0 ? "" : ", ", json::JsonNumber(gZoom.game.zoom[i]));
    }
    return std::format("{{\"mesh\": {}, \"optic\": {}, \"zooms\": [{}], \"zoomLevel\": {}, \"stats\": {}, \"art\": {{\"state\": {}, "
                       "\"generation\": {}}}}}",
        json::JsonString(gModel.mesh), gModel.opticApplies ? "true" : "false", zooms, gZoom.level, json::JsonString(gZoom.stats),
        json::JsonString(gArt.text), gArt.generation);
}

} // namespace

ue3::Object* ConvergeActor() {
    return gWorld.traced && gWorld.hit ? gWorld.hitActor : nullptr;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    XrVector3f const shift = gSpring.Value();
    return std::format("{{\"kind\": {}, \"weapon\": {}, \"status\": {}, \"hostView\": {}, \"scopeUp\": {}, \"aimedShots\": {}, "
                       "\"center\": [{}, {}, {}], \"axis\": [{}, {}, {}], \"radius\": {}, \"zoom\": {}, \"eyeBox\": {}, "
                       "\"place\": {{\"back\": {}, \"height\": {}, \"side\": {}, \"modelTop\": {}, \"boundsTop\": {}, \"bone\": {}, "
                       "\"boneLive\": {}, \"boneSettled\": {}, \"model\": {}}}, "
                       "\"converge\": {{\"on\": {}, \"traced\": {}, \"hit\": {}, \"distanceM\": {}, \"what\": {}}}, \"assist\": {{\"on\": {}, "
                       "\"eye\": {}, \"angleDeg\": {}, \"along\": {}, \"offset\": [{}, {}], \"weight\": {}, \"goal\": [{}, {}, {}], "
                       "\"shift\": [{}, {}, {}], \"pull\": {{\"reliefM\": {}, \"maxM\": {}, \"goalM\": {}, \"nearUnits\": {}, "
                       "\"nearFromGame\": {}, \"raisePulledM\": {}, \"raiseEyeBeforeM\": {}, \"raiseEyeAfterM\": {}}}}}, \"ads\": {}, "
                       "\"parts\": {}, \"game\": {}}}",
        json::JsonString(sv::Name(static_cast<sv::Kind>(gLast.kind))), json::JsonString(sv::WeaponName(gLast.weapon)),
        json::JsonString(gStatus), gHostView, gScopeUp ? "true" : "false", gAimedShotsTotal, json::JsonNumber(gLast.center.x),
        json::JsonNumber(gLast.center.y), json::JsonNumber(gLast.center.z), json::JsonNumber(gLast.axis.x),
        json::JsonNumber(gLast.axis.y), json::JsonNumber(gLast.axis.z), json::JsonNumber(gLast.radius), json::JsonNumber(gLast.zoom),
        json::JsonNumber(gLast.eyeBox), json::JsonNumber(gPlaced.back), json::JsonNumber(gPlaced.height), json::JsonNumber(gPlaced.side),
        json::JsonNumber(gModel.top.value_or(-1.0f)), json::JsonNumber(gModel.boundsTop.value_or(-1.0f)), json::JsonString(gModel.boneName),
        gModel.live ? "true" : "false", gModel.settledLogged ? "true" : "false", json::JsonString(gModel.text), gWorld.converge ? "true" : "false",
        gWorld.traced ? "true" : "false", gWorld.hit ? "true" : "false", json::JsonNumber(gWorld.distance / 100.0),
        json::JsonString(gWorld.what), gAssisting ? "true" : "false", json::JsonString(gGoal.eye == 0 ? "left" : gGoal.eye == 1 ? "right" : "none"),
        json::JsonNumber(gGoal.angleDeg), json::JsonNumber(gGoal.along), json::JsonNumber(gGoal.x), json::JsonNumber(gGoal.y),
        json::JsonNumber(gGoal.weight), json::JsonNumber(gGoal.shift.x), json::JsonNumber(gGoal.shift.y), json::JsonNumber(gGoal.shift.z),
        json::JsonNumber(shift.x), json::JsonNumber(shift.y), json::JsonNumber(shift.z), json::JsonNumber(gPull.reliefM),
        json::JsonNumber(gPull.maxM), json::JsonNumber(gGoal.pull), json::JsonNumber(gNearUnits), gNearRead ? "true" : "false",
        json::JsonNumber(gRaisePullMax), json::JsonNumber(gRaiseAlongBefore), json::JsonNumber(gRaiseAlongAfter), AdsJson(),
        scope_parts::StatusJson(), GameJson());
}

void LogStats() {
    unsigned const queries = gAimErrorCalls.exchange(0);
    unsigned const aimed = gAimedShots.exchange(0);
    if (gLast.kind != 0 || aimed > 0) {
        std::string assist = "alignment assist idle";
        if (gOffsetTicks > 0) {
            assist = std::format("alignment assist on {} ticks: the aiming eye sat on average {:.1f} cm {} and {:.1f} cm {} the "
                                 "unassisted sight line ({:.1f} degrees); the gun moved at most {:.1f} cm",
                gOffsetTicks, std::abs(gOffsetSumX / gOffsetTicks) * 100.0, gOffsetSumX >= 0.0 ? "right of" : "left of",
                std::abs(gOffsetSumY / gOffsetTicks) * 100.0, gOffsetSumY >= 0.0 ? "above" : "below", gOffsetSumAngle / gOffsetTicks,
                gMaxShift * 100.0f);
            if (gPullTicksHeartbeat > 0) {
                assist += std::format(", the sight pulled {:.1f} cm toward the eye on average", gPullSumHeartbeat / gPullTicksHeartbeat * 100.0f);
            }
        }
        if (gAdsGate || gAdsTicksHeartbeat > 0) {
            assist += gAdsTicksHeartbeat > 0
                ? std::format("; ADS {} times, on {} ticks: the aiming eye {:.2f} m behind the pulled eyepiece on average", gAdsOnsHeartbeat,
                      gAdsTicksHeartbeat, gAdsEyeSumHeartbeat / gAdsTicksHeartbeat)
                : std::string("; no ADS");
        }
        log::Info("    sights: {}; placed {:.1f} back, {:.1f} up, {:.1f} right; {}; scoped aimed fire on {} of {} aim-error queries; {}",
            gStatus, gPlaced.back, gPlaced.height, gPlaced.side, ViewText(gHostView), aimed, queries, assist);
    }
    gOffsetSumX = gOffsetSumY = gOffsetSumAngle = 0.0;
    gOffsetTicks = 0;
    gMaxShift = 0.0f;
    gPullSumHeartbeat = 0.0f;
    gPullTicksHeartbeat = 0;
    gAdsOnsHeartbeat = 0;
    gAdsTicksHeartbeat = 0;
    gAdsEyeSumHeartbeat = 0.0;
}

void Shutdown(bool exiting) {
    scope_parts::Shutdown(exiting);  // the hidden lens comes back before the mod goes (game thread: the host's tick)
    gPull = {};
    gNearUnits = kDefaultNearUnits;
    gNearRead = false;
    gLoggedRelief.clear();
    gRaisePullMax = gRaiseAlongBefore = gRaiseAlongAfter = 0.0f;
    gPullSumHeartbeat = 0.0f;
    gPullTicksHeartbeat = 0;
    gHideText.clear();
    gHideList.clear();
    gZoom = {};
    gArt = {};  // a reloaded mod finds the textures again; the host composes on the generation (which starts over: it
                // drops its layers when a generation goes back or the resources change)
    gAimErrorFunction = nullptr;
    gTraceFunction = nullptr;
    gWorld = {};
    gLoggedTracePath = false;
    gModel = {};
    gPlaced = {};
    gPlacedWeapon = 0;
    gLast = sv::Sight{0};
    gLastWeapon = 0;
    gStatus = "none";
    gLoggedStatus.clear();
    gHostView = 0;
    gLoggedHostView = 0;
    gScopeUp = false;
    gAimedShotsTotal = 0;
    gLoggedAimed = false;
    gSpring.Reset();
    gTakenNow = {};
    gTakenPrev = {};
    gGoal = {};
    gAssistEye = -1;
    gAssisting = false;
    gLastTickMs = 0.0;
    gOffsetSumX = gOffsetSumY = gOffsetSumAngle = 0.0;
    gOffsetTicks = 0;
    gMaxShift = 0.0f;
    gAdsPolicy.Reset();
    gAdsSample = {};
    gAdsGate = false;
    gAds = false;
    gApproach = gDistanceWeight = gPullLimit = 0.0f;
    gFastSpring = false;
    gAdsSinceMs = 0.0;
    gAdsOnsHeartbeat = 0;
    gAdsTicksHeartbeat = 0;
    gAdsEyeSumHeartbeat = 0.0;
}

} // namespace mevr::game::sights
