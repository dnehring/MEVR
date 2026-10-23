#include "game/Sights.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/Objects.hpp"
#include "game/HandCalibration.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Weapon.hpp"

#include <LESDK/Headers.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace mevr::game::sights {
namespace {

namespace sv = vr::sights;

#if defined(SDK_TARGET_LE1)
constexpr char const* kGetPlayerAimError = "Function SFXGame.BioWeaponRanged.GetPlayerAimError";
#endif

UFunction* gAimErrorFunction = nullptr;
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
void OnAimError(UObject* context, UFunction* /*function*/, void* result) {
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
    gAimedShots.fetch_add(1, std::memory_order_relaxed);
    ++gAimedShotsTotal;
}

double Dot(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
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
#if defined(SDK_TARGET_LE1)
    gAimErrorFunction = engine::FindFunction(L"GetPlayerAimError", kGetPlayerAimError);
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
            sv::Gun const placed = sv::AutoPlace(type, gun, length);
            // The gun's top: arm IK rolls the gun with the right controller (its aim pose's up, as far as
            // the barrel allows), so the same up places the sight on top of it.
            vr::controllers::State const controllers = vr::controllers::Latest();
            XrVector3f const aimUp = controllers.active && controllers.right.aimValid
                ? vr::overlay::Rotate(controllers.right.aim.orientation, {0.0f, 1.0f, 0.0f})
                : XrVector3f{0.0f, 1.0f, 0.0f};
            auto top = sv::TopOf(ray.direction, aimUp);
            if (!top) {
                top = sv::TopOf(ray.direction, {0.0f, 1.0f, 0.0f});
            }
            if (!top) {
                status = "none (the barrel points straight up or down: no top)";
            } else {
                bool const scope = gun.kind == sv::Kind::Scope;
                out.kind = static_cast<std::int32_t>(gun.kind);
                out.weapon = type;
                out.flags = (settings.frame && !scope ? sv::kFrame : 0u) | (settings.replaceRing ? sv::kReplaceRing : 0u) |
                    (settings.otherEye ? sv::kOtherEye : 0u);
                out.center = sv::Place(ray.origin, ray.direction, *top, placed.back / unitsPerMeter, placed.height / unitsPerMeter);
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
                status = std::format("the {} has a {} sight{}: {:.1f} units behind the muzzle, {:.1f} above the bore ({}; the gun is "
                                     "{:.1f} units from muzzle to grip), {} {:.1f} units ({:.3f} m at {:.1f} units per meter)",
                    sv::WeaponName(type), sv::Name(gun.kind), scope ? std::format(" ({:.1f}x)", gun.zoom) : std::string(),
                    placed.back, placed.height, gun.autoPlace ? "auto" : "set", length, scope ? "eyepiece radius" : "window radius",
                    scope ? settings.lensUnits : settings.windowUnits, out.radius, unitsPerMeter);
            }
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
                       "\"center\": [{}, {}, {}], \"axis\": [{}, {}, {}], \"radius\": {}, \"zoom\": {}, \"assist\": {{\"on\": {}, "
                       "\"eye\": {}, \"angleDeg\": {}, \"along\": {}, \"offset\": [{}, {}], \"weight\": {}, \"goal\": [{}, {}, {}], "
                       "\"shift\": [{}, {}, {}]}}}}",
        json::JsonString(sv::Name(static_cast<sv::Kind>(gLast.kind))), json::JsonString(sv::WeaponName(gLast.weapon)),
        json::JsonString(gStatus), gHostView, gScopeUp ? "true" : "false", gAimedShotsTotal, json::JsonNumber(gLast.center.x),
        json::JsonNumber(gLast.center.y), json::JsonNumber(gLast.center.z), json::JsonNumber(gLast.axis.x),
        json::JsonNumber(gLast.axis.y), json::JsonNumber(gLast.axis.z), json::JsonNumber(gLast.radius), json::JsonNumber(gLast.zoom),
        gAssisting ? "true" : "false", json::JsonString(gGoal.eye == 0 ? "left" : gGoal.eye == 1 ? "right" : "none"),
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
        log::Info("    sights: {}; {}; scoped aimed fire on {} of {} aim-error queries; {}", gStatus, ViewText(gHostView), aimed, queries,
            assist);
    }
    gOffsetSumX = gOffsetSumY = gOffsetSumAngle = 0.0;
    gOffsetTicks = 0;
    gMaxShift = 0.0f;
}

void Shutdown() {
    gAimErrorFunction = nullptr;
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
