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
    return out;
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
    return std::format("{{\"kind\": {}, \"weapon\": {}, \"status\": {}, \"hostView\": {}, \"scopeUp\": {}, \"aimedShots\": {}, "
                       "\"center\": [{}, {}, {}], \"axis\": [{}, {}, {}], \"radius\": {}, \"zoom\": {}}}",
        json::JsonString(sv::Name(static_cast<sv::Kind>(gLast.kind))), json::JsonString(sv::WeaponName(gLast.weapon)),
        json::JsonString(gStatus), gHostView, gScopeUp ? "true" : "false", gAimedShotsTotal, json::JsonNumber(gLast.center.x),
        json::JsonNumber(gLast.center.y), json::JsonNumber(gLast.center.z), json::JsonNumber(gLast.axis.x),
        json::JsonNumber(gLast.axis.y), json::JsonNumber(gLast.axis.z), json::JsonNumber(gLast.radius), json::JsonNumber(gLast.zoom));
}

void LogStats() {
    unsigned const queries = gAimErrorCalls.exchange(0);
    unsigned const aimed = gAimedShots.exchange(0);
    if (gLast.kind != 0 || aimed > 0) {
        log::Info("    sights: {}; {}; scoped aimed fire on {} of {} aim-error queries", gStatus, ViewText(gHostView), aimed, queries);
    }
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
}

} // namespace mevr::game::sights
