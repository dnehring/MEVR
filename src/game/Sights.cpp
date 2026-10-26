// SPDX-License-Identifier: MIT
#include "game/Sights.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Calibration.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/BodyCalibration.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"

#include <algorithm>
#include <cctype>
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
#endif
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
};
ModelPlace gModel;
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
bool gAssisting = false;      // the goal's weight is up (logged on each change)
double gLastTickMs = 0.0;
// Heartbeat and snapshot: where the aiming eye sat against the unassisted sight line while the gun was
// raised (a systematic offset means the sight's place on the gun is off), and the biggest shift.
double gOffsetSumX = 0.0;
double gOffsetSumY = 0.0;
double gOffsetSumAngle = 0.0;
int gOffsetTicks = 0;
float gMaxShift = 0.0f;

constexpr float kRaisedWeight = 0.05f;  // the assist counts as on from this weight

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

void ResetAssist(char const* why) {
    if (gAssisting) {
        log::Info("sights: alignment assist off ({})", why);
    }
    gSpring.Reset();
    gTakenNow = {};
    gTakenPrev = {};
    gGoal = {};
    gAssistEye = -1;
    gAssisting = false;
}

// The assist's step for this tick's sight (as drawn: it carries gTakenPrev).
void StepAssist(sv::Settings const& settings, sv::Sight const& drawn, double nowMs) {
    double const dt = gLastTickMs > 0.0 ? std::clamp((nowMs - gLastTickMs) / 1000.0, 0.0, 0.15) : 0.0;
    gLastTickMs = nowMs;
    auto const frame = view_anchor::Current();
    if (!settings.assist.enabled || drawn.kind == static_cast<std::int32_t>(sv::Kind::None) || !frame) {
        ResetAssist(!settings.assist.enabled ? "[Sights] assist = 0" : "no sight");
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
    // The sight without the shift already in the drawn gun, so the assist never chases itself.
    XrVector3f const center = Sub3(drawn.center, gTakenPrev);
    gGoal = sv::AssistTarget(settings.assist, center, drawn.axis, drawn.up, eyes, forward, gAssistEye);
    gAssistEye = gGoal.eye;
    XrVector3f const shift = gSpring.Update(gGoal.shift, static_cast<float>(dt));
    gMaxShift = std::max(gMaxShift, Length3(shift));
    bool const on = gGoal.weight >= kRaisedWeight;
    if (on) {
        gOffsetSumX += gGoal.x;
        gOffsetSumY += gGoal.y;
        gOffsetSumAngle += gGoal.angleDeg;
        ++gOffsetTicks;
    }
    if (on != gAssisting) {
        gAssisting = on;
        if (on) {
            log::Info("sights: alignment assist on: the {} eye {:.1f} cm {} and {:.1f} cm {} the sight line ({:.1f} degrees, the sight "
                      "{:.2f} m ahead): the gun slides {:.1f} cm toward it ({:.0f}% weight)",
                gGoal.eye == 0 ? "left" : "right", std::abs(gGoal.x) * 100.0f, gGoal.x >= 0.0f ? "right of" : "left of",
                std::abs(gGoal.y) * 100.0f, gGoal.y >= 0.0f ? "above" : "below", gGoal.angleDeg, gGoal.along,
                Length3(gGoal.shift) * 100.0f, gGoal.weight * 100.0f);
        } else {
            log::Info("sights: alignment assist off (the gun lowered or turned away: the eye {} degrees off the line, the sight {:.2f} m "
                      "ahead); the gun eases back",
                gGoal.eye >= 0 ? std::format("{:.1f}", gGoal.angleDeg) : std::string("-"), gGoal.along);
        }
    }
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

} // namespace

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
            // The gun's top: arm IK rolls the gun with the right controller (its aim pose's up, as far as
            // the barrel allows), so the same up places the sight on top of it.
            vr::controllers::State const controllers = vr::controllers::Latest();
            XrVector3f aimUp = controllers.active && controllers.right.aimValid
                ? vr::overlay::Rotate(controllers.right.aim.orientation, {0.0f, 1.0f, 0.0f})
                : XrVector3f{0.0f, 1.0f, 0.0f};
            // Calibrated (research/25): arm IK holds the gun level in the calibrated gun frame, so its top is that up.
            if (auto const stance = body_calibration::StanceFor(type); stance && controllers.active && controllers.right.gripValid) {
                aimUp = vr::calibration::Rotate(vr::calibration::GunOrientation(controllers.right.grip.orientation, *stance),
                    {0.0f, 1.0f, 0.0f});
            }
            auto top = sv::TopOf(ray.direction, aimUp);
            if (!top) {
                top = sv::TopOf(ray.direction, {0.0f, 1.0f, 0.0f});
            }
            if (!top) {
                status = "none (the barrel points straight up or down: no top)";
            } else {
                bool const scope = gun.kind == sv::Kind::Scope;
                engine::UnrealVector const topWorld = DirectionToWorld(*frame, *top);
                double const nowMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
                MeasureModel(topWorld, gun.kind, length, *muzzle, nowMs);
                sv::Gun const placed = PlaceFor(type, gun, length, scope ? settings.lensUnits : settings.windowUnits);
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
                out.radius = (scope ? settings.lensUnits : settings.windowUnits) / unitsPerMeter;
                out.zoom = scope ? gun.zoom : 1.0f;
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
                                     "{:.1f} units{}; shots {}",
                    sv::WeaponName(type), sv::Name(gun.kind), scope ? std::format(" ({:.1f}x)", gun.zoom) : std::string(),
                    gModel.live && gun.autoPlace ? std::format("on the model's '{}' bone", gModel.boneName)
                                                 : std::format("{:.1f} units behind the muzzle, {:.1f} above the bore{}", placed.back,
                                                       placed.height, placed.side != 0.0f ? std::format(", {:.1f} right", placed.side) : std::string()),
                    !gun.autoPlace ? "set"
                    : gModel.live  ? "auto: followed live as the gun unfolds"
                    : gModel.top   ? "auto: the length share, the height from the bones' top"
                                   : "auto",
                    length, scope ? "eyepiece radius" : "window radius", scope ? settings.lensUnits : settings.windowUnits, out.radius,
                    unitsPerMeter, std::max(settings.eyeBoxUnits, settings.windowUnits),
                    scope && settings.scopeFieldDeg > 0.0f
                        ? std::format("; virtual eyepiece: the picture {:.0f} degrees across with the eye up to {:.2f} m behind it, eye box "
                                      "{:.3f} m", settings.scopeFieldDeg, settings.scopeMaxM, settings.scopeEyeBoxM)
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
    StepAssist(settings, out, nowMs);
    return out;
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

void SetHostView(std::uint32_t view) {
    gHostView = view;
    gScopeUp = (view & sv::kViewScope) != 0 && gLast.kind == static_cast<std::int32_t>(sv::Kind::Scope) &&
        gLastWeapon != 0 && gLastWeapon == weapon::Key();
    if (view != gLoggedHostView) {
        gLoggedHostView = view;
        log::Info("sights: {}", ViewText(view));
    }
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
                       "\"shift\": [{}, {}, {}]}}}}",
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
        json::JsonNumber(shift.x), json::JsonNumber(shift.y), json::JsonNumber(shift.z));
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
        }
        log::Info("    sights: {}; placed {:.1f} back, {:.1f} up, {:.1f} right; {}; scoped aimed fire on {} of {} aim-error queries; {}",
            gStatus, gPlaced.back, gPlaced.height, gPlaced.side, ViewText(gHostView), aimed, queries, assist);
    }
    gOffsetSumX = gOffsetSumY = gOffsetSumAngle = 0.0;
    gOffsetTicks = 0;
    gMaxShift = 0.0f;
}

void Shutdown() {
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
}

} // namespace mevr::game::sights
