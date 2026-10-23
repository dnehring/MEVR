#include "game/SupportGrip.hpp"

#include "core/Log.hpp"
#include "core/ik/TwoHand.hpp"
#include "core/vr/Calibration.hpp"
#include "core/vr/Controllers.hpp"
#include "engine/BonePose.hpp"
#include "game/BodyCalibration.hpp"
#include "game/Comfort.hpp"
#include "game/HandCalibration.hpp"
#include "game/Holsters.hpp"
#include "game/SquadWeapons.hpp"
#include "game/Throwables.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Weapon.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <string>

namespace mevr::game::support_grip {
namespace {

// A foregrip candidate settles after this many ticks within this of the last one (units).
constexpr int kSettleTicks = 5;
constexpr double kStillUnits = 0.5;
// A settled foregrip this far from the kept one replaces it (only while not held).
constexpr double kMoveUnits = 1.0;
// The animated left hand this close to the chosen point lends it its rotation (units).
constexpr double kRotationFromAnimationUnits = 4.0;
// The animated left wrist this close to LHIK: where the AnimTree's off-hand IK holds it (a
// cross-check of the control's strength, which is what says whether it's on).
constexpr double kIkActiveUnits = 2.0;
// A span with less than this along the barrel (a pistol cupped from below) doesn't define the
// barrel: the right hand keeps aiming.
constexpr double kMinAlongForTwoHandAim = 10.0;
// Overlord's grip blend (grip_presenter.hpp, profile_defaults::blend_seconds).
constexpr double kBlendSeconds = 0.10;

std::uintptr_t gGun = 0;
std::uint32_t gHoldSerial = 0;  // the current hold's serial (0: not held)
std::uint32_t gHolds = 0;       // holds so far (the next serial)
std::optional<Foregrip> gForegrip;
std::optional<Foregrip> gCandidate;
int gStillTicks = 0;
bool gLoggedGun = false;  // the diagnostics line for gGun
double gBlend = 0.0;
bool gLoggedHeld = false;
bool gRequested = false;

std::atomic<bool> gHeld{false};
// The pad mapping's input, written each game tick, read from XInput polls.
std::atomic<float> gDistance{-1.0f};
std::atomic<float> gAcquire{0.12f};
std::atomic<float> gRelease{0.22f};
std::atomic<std::uint32_t> gFlags{gamepad::kFlagLeftTrigger | gamepad::kFlagSupportAims};

// Heartbeat: the nearest the left hand came to the foregrip, and the last write's result.
double gNearest = 1e9;
std::optional<engine::bone_pose::SupportSample> gLastSample;
// The native off-hand IK's state last logged, and its latest sample (heartbeat).
ik::NativeIk gNativeIk = ik::NativeIk::Unknown;
std::optional<NativeIkSample> gNativeSample;
// The held weapon's bIsZoomed last logged (nullopt: no ranged weapon).
std::optional<bool> gLoggedZoomed;
bool gLoggedZoomedOnce = false;

std::string NativeIkText(std::optional<NativeIkSample> const& n) {
    if (!n) {
        return "the AnimTree's off-hand IK (LeftArm): no such control";
    }
    return std::format("the AnimTree's off-hand IK (LeftArm) is {}: strength {:.2f}, target {:.2f}, {}",
        ik::Name(ik::ClassifyNativeIk(n->strength)), n->strength, n->target,
        n->fromAnimNode ? "from the AddCombatIdleOffset node" : "set by the weapon code (draw or holster)");
}

std::string Text(ik::Vec3 const& v) {
    return std::format("({:.1f}, {:.1f}, {:.1f})", v.x, v.y, v.z);
}

std::string Where(std::optional<ik::Vec3> const& point, Candidates const& c) {
    if (!point) {
        return "none";
    }
    ik::GunOffsets const o = ik::OffsetsOnGun(*point, c.barrel, c.muzzle);
    return std::format("{} ({:.1f} along the barrel, {:.1f} off it, {})", Text(*point), o.along, o.off,
        ik::OnGun(*point, c.barrel, c.muzzle) ? "on the gun" : "OFF the gun");
}

Foregrip Choose(Candidates const& c) {
    Foregrip f;
    std::optional<ik::Vec3> point;
    if (c.socket && ik::OnGun(*c.socket, c.barrel, c.muzzle)) {
        point = c.socket;
        f.source = "the weapon's off-hand socket";
    } else if (c.lhik && ik::OnGun(*c.lhik, c.barrel, c.muzzle)) {
        point = c.lhik;
        f.source = "the LHIK bone (the AnimTree's off-hand IK goal)";
    } else if (c.animatedLeft && ik::OnGun(c.animatedLeft->translation, c.barrel, c.muzzle)) {
        point = c.animatedLeft->translation;
        f.source = "the animated left hand";
    } else {
        point = ik::UnderGrip(c.barrel, c.up);
        f.source = "under the grip (no foregrip on the gun)";
    }
    f.position = *point;
    if (c.animatedLeft && ik::Length(c.animatedLeft->translation - f.position) <= kRotationFromAnimationUnits) {
        f.hasRotation = true;
        f.rotation = ik::Normalize(c.animatedLeft->rotation);
    }
    f.twoHandAim = std::abs(ik::OffsetsOnGun(f.position, c.barrel, c.muzzle).along) >= kMinAlongForTwoHandAim;
    return f;
}

void LogGun(Candidates const& c) {
    std::string ik = NativeIkText(c.nativeIk);
    if (c.animatedLeft && c.lhik) {
        double const apart = ik::Length(c.animatedLeft->translation - *c.lhik);
        ik += std::format("; the animated left wrist is {:.1f} units from LHIK ({})", apart,
            apart <= kIkActiveUnits ? "held there" : "not there");
    }
    log::Info("support grip: candidates in the right hand's axes (barrel ({:.3f}, {:.3f}, {:.3f}), muzzle {}): socket {}; "
              "LHIK {}; animated left hand {}; {}",
        c.barrel.x, c.barrel.y, c.barrel.z, Text(c.muzzle), Where(c.socket, c), Where(c.lhik, c),
        Where(c.animatedLeft ? std::optional<ik::Vec3>(c.animatedLeft->translation) : std::nullopt, c), ik);
}

void Reset() {
    gForegrip.reset();
    gCandidate.reset();
    gStillTicks = 0;
    gLoggedGun = false;
}

} // namespace

void Measure(Candidates const& c) {
    if (c.gun != gGun) {
        gGun = c.gun;
        Reset();
    }
    if (!gLoggedGun && c.animatedLeft) {
        gLoggedGun = true;
        LogGun(c);
    }
    gNativeSample = c.nativeIk;
    ik::NativeIk const native = c.nativeIk ? ik::ClassifyNativeIk(c.nativeIk->strength) : ik::NativeIk::Unknown;
    if (native != gNativeIk) {
        gNativeIk = native;
        log::Info("support grip: {}", NativeIkText(c.nativeIk));
    }
    if (gForegrip && gBlend > 0.0) {
        return;  // held: the place under the hand stays put
    }
    Foregrip const f = Choose(c);
    bool const agrees = gCandidate && gCandidate->source == f.source && ik::Length(gCandidate->position - f.position) < kStillUnits;
    gCandidate = f;
    gStillTicks = agrees ? gStillTicks + 1 : 0;
    if (gStillTicks < kSettleTicks) {
        return;
    }
    if (gForegrip && gForegrip->source == f.source && ik::Length(gForegrip->position - f.position) < kMoveUnits) {
        return;
    }
    bool const first = !gForegrip;
    gForegrip = f;
    ik::GunOffsets const o = ik::OffsetsOnGun(f.position, c.barrel, c.muzzle);
    log::Info("support grip: foregrip {} from {}: {} in the right hand's axes, {:.1f} along the barrel and {:.1f} off it; "
              "the hand's rotation {}; {}; the left arm is solved to it while held (the AnimTree's off-hand IK runs "
              "before our right-arm write, so it would leave the hand where the animation had the gun)",
        first ? "set" : "moved", f.source, Text(f.position), o.along, o.off,
        f.hasRotation ? "from the animated left hand" : "from the left controller",
        f.twoHandAim ? "two-hand aim: the barrel turns toward the left hand" : "too short along the barrel to aim with: "
                                                                               "the right hand keeps aiming");
}

std::optional<Foregrip> Current() {
    return gForegrip;
}

Zone CurrentZone() {
    hand_calibration::Controls const controls = hand_calibration::GetControls();
    return {controls.supportCapsule, controls.frontMeters, controls.pastMeters};
}

std::uint32_t HoldSerial() {
    return gHoldSerial;
}

// The right hand's grip at the shoulder, against the headset (tracking space, meters).
bool gShouldered = false;
bool gLoggedAiming = false;
bool AtShoulder() {
    auto const anchor = view_anchor::Current();
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!anchor || !controllers.active || !controllers.right.gripValid) {
        gShouldered = false;
        return false;
    }
    XrQuaternionf const& q = anchor->head.orientation;
    // The headset's forward, -Z rotated by q.
    ik::Vec3 const forward{-2.0 * (q.x * q.z + q.w * q.y), -2.0 * (q.y * q.z - q.w * q.x), -(1.0 - 2.0 * (q.x * q.x + q.y * q.y))};
    XrVector3f const& h = anchor->head.position;
    XrVector3f const& g = controllers.right.grip.position;
    // Calibrated (research/25): around where the player's right hand sat with the rifle shouldered.
    ik::ShoulderZone const zone = body_calibration::ShoulderZone().value_or(ik::ShoulderZone{});
    gShouldered = ik::Shouldered({h.x, h.y, h.z}, forward, {0.0, 1.0, 0.0}, {g.x, g.y, g.z}, gShouldered, zone);
    return gShouldered;
}

// The guided calibration's own foregrip point (research/25): where the player's left hand sat on the rifle when
// calibrating, from the right grip now (tracking meters). A left grip that near takes the gun too, whatever the
// gun's own foregrip (the capsule along the gun stays as generous as before). Nullopt: not calibrated, a pistol.
std::optional<double> CalibratedForegripDistance() {
    auto const stance = body_calibration::TwoHanded();
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!stance || weapon::Type() == weapon::RangedType::Pistol || !controllers.active || !controllers.right.gripValid ||
        !controllers.left.gripValid) {
        return std::nullopt;
    }
    XrPosef const& right = controllers.right.grip;
    XrVector3f const offset = vr::calibration::Rotate(vr::calibration::GunOrientation(right.orientation, *stance), stance->foregrip);
    XrVector3f const& left = controllers.left.grip.position;
    return std::hypot(left.x - (right.position.x + offset.x), left.y - (right.position.y + offset.y), left.z - (right.position.z + offset.z));
}
bool gNearCalibrated = false;  // the last distance came from the calibrated point (logged with the hold)

void Tick(double deltaSeconds, bool requested) {
    gRequested = requested;
    hand_calibration::Refresh();  // at most once a second; arm IK may be off
    hand_calibration::Controls const controls = hand_calibration::GetControls();
    auto const sample = requested ? engine::bone_pose::LatestSupport() : std::nullopt;
    float distance = -1.0f;
    gNearCalibrated = false;
    if (controls.supportGrip && sample && sample->distanceMeters >= 0.0) {
        distance = static_cast<float>(sample->distanceMeters);
        if (auto const calibrated = CalibratedForegripDistance(); calibrated && *calibrated < distance) {
            distance = static_cast<float>(*calibrated);
            gNearCalibrated = true;
        }
        gNearest = std::min(gNearest, static_cast<double>(distance));
        gLastSample = sample;
    }
    gDistance.store(distance, std::memory_order_relaxed);
    gAcquire.store(static_cast<float>(controls.acquireMeters), std::memory_order_relaxed);
    gRelease.store(static_cast<float>(controls.releaseMeters), std::memory_order_relaxed);
    gamepad::Options options;
    options.leftTrigger = controls.leftTrigger;
    options.leftTriggerShoulder = controls.leftTriggerLB;
    options.gripShoulder = controls.gripLB || !controls.supportGrip;
    // Aimed fire: both hands on the gun and, unless turned off, the gun at the shoulder.
    bool const shouldered = AtShoulder();
    options.supportAims = (controls.sniperAims || weapon::Type() != weapon::RangedType::Sniper) &&
                          (!controls.aimAtShoulder || shouldered);
    holsters::AdjustPad(options);  // RB kept off a squeeze at a holster; the wheel's LB if turned off
    squad_weapons::AdjustPad(options);  // the squad menu's controls while it's open; no LB from a left grip away
    throwables::AdjustPad(options);  // B and X off on foot if turned off; no RB from a squeeze at the grenade slot
    comfort::AdjustPad(options);  // our own turn (no right-stick X for the game) and walking toward the left hand
    gFlags.store(gamepad::ToFlags(options), std::memory_order_relaxed);
    bool const aiming = options.supportAims && gHeld.load(std::memory_order_relaxed) && requested;
    std::optional<bool> const zoomed = weapon::Zoomed();
    auto const zoomText = [&zoomed] {
        return zoomed ? std::format("the weapon's bIsZoomed is {}", *zoomed ? "on" : "off") : std::string("no ranged weapon");
    };
    if (aiming != gLoggedAiming) {
        gLoggedAiming = aiming;
        log::Info("support grip: aimed fire {}; {}", aiming ? "on (both hands, the gun at the shoulder): the game sees LT held"
                                                            : "off (hip fire)", zoomText());
    }
    // The game's own answer: bIsZoomed follows LT through BioPlayerInput.bWantsToZoom a tick or so
    // later (forced off while switching weapons or overheated).
    if (!gLoggedZoomedOnce || zoomed != gLoggedZoomed) {
        gLoggedZoomedOnce = true;
        gLoggedZoomed = zoomed;
        char const* const why = aiming || !gHeld.load(std::memory_order_relaxed) ? ""
            : !requested                                                      ? ": held, but arm IK isn't asking for the grip"
                                                                              : ": held, but not at the shoulder (or a sniper rifle)";
        log::Info("support grip: {} (our grip {} LT{})", zoomText(), aiming ? "forces" : "doesn't force", why);
    }

    bool const held = gHeld.load(std::memory_order_relaxed) && requested;
    double const goal = held ? 1.0 : 0.0;
    double const step = std::clamp(deltaSeconds, 0.0, 0.05) / kBlendSeconds;
    gBlend = requested ? std::clamp(gBlend + std::clamp(goal - gBlend, -step, step), 0.0, 1.0) : 0.0;
    if (held && gHoldSerial == 0) {
        gHoldSerial = ++gHolds;
        if (gHoldSerial == 0) {
            gHoldSerial = ++gHolds;  // 0 means "not held"
        }
    } else if (!held) {
        gHoldSerial = 0;
    }
    if (held != gLoggedHeld) {
        gLoggedHeld = held;
        if (held) {
            std::string where = std::format("{:.2f} m from the grab zone", distance);
            if (sample && sample->foregripMeters >= 0.0) {
                where += std::format(", {:.2f} m from the foregrip, {:.2f} m along the barrel from the right hand", sample->foregripMeters,
                    sample->alongMeters);
            }
            if (gNearCalibrated) {
                where += ", at your calibrated foregrip place";
            }
            log::Info("support grip: held ({}; {}, foregrip from {}): {}; the left hand goes to the foregrip{}", where,
                controls.supportCapsule ? "a capsule along the gun" : "the foregrip alone", gForegrip ? gForegrip->source : "?",
                gForegrip && gForegrip->twoHandAim
                    ? "the barrel turns with the line between the hands (taken where you grabbed: no swing)"
                    : "the right hand keeps aiming",
                options.supportAims ? "; the game sees LT held (focused fire)" : "; no LT (sniper rifle, sniperAims = 0)");
        } else {
            log::Info("support grip: released ({})", distance < 0.0f ? "no gun or arm IK" : std::format("{:.2f} m from the grab zone", distance));
        }
    }
}

void SetHeld(bool held) {
    gHeld.store(held, std::memory_order_relaxed);
}

bool Held() {
    return gHeld.load(std::memory_order_relaxed);
}

double Blend() {
    return gBlend;
}

gamepad::Options PadOptions() {
    return gamepad::FromFlags(gDistance.load(std::memory_order_relaxed), gAcquire.load(std::memory_order_relaxed),
        gRelease.load(std::memory_order_relaxed), gFlags.load(std::memory_order_relaxed));
}

void LogStats() {
    if (!gForegrip && !gRequested) {
        return;
    }
    std::string last = "no write yet";
    if (gLastSample) {
        last = std::format("last write {:.2f} m from the grab zone ({:.2f} m from the foregrip, {:.2f} m along), blend {:.2f}, {}{}, "
                           "swing {:.1f} degrees, left wrist {:.1f} units from its goal",
            gLastSample->distanceMeters, gLastSample->foregripMeters, gLastSample->alongMeters, gLastSample->blend,
            gLastSample->twoHand ? "two-hand aim" : "one-hand aim",
            gLastSample->calibrated ? " (the calibrated line)" : gLastSample->captured ? " (the grab's line)" : "",
            gLastSample->swingDegrees, gLastSample->leftErrorUnits);
    }
    std::optional<bool> const zoomed = weapon::Zoomed();
    log::Info("    support grip: {}, foregrip {}; nearest {} from the grab zone; {}; {}; bIsZoomed {}", Held() ? "HELD" : "not held",
        gForegrip ? gForegrip->source : "not settled", gNearest < 1e8 ? std::format("{:.2f} m", gNearest) : std::string("-"), last,
        NativeIkText(gNativeSample), zoomed ? (*zoomed ? "on" : "off") : "-");
    gNearest = 1e9;
}

void Shutdown() {
    gGun = 0;
    gHoldSerial = 0;
    Reset();
    gBlend = 0.0;
    gLoggedHeld = false;
    gRequested = false;
    gHeld.store(false);
    gDistance.store(-1.0f);
    gFlags.store(gamepad::kFlagLeftTrigger | gamepad::kFlagSupportAims);
    gNearest = 1e9;
    gLastSample.reset();
    gNativeIk = ik::NativeIk::Unknown;
    gNativeSample.reset();
    gLoggedZoomed.reset();
    gLoggedZoomedOnce = false;
    gLoggedAiming = false;
}

} // namespace mevr::game::support_grip
