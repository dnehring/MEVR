// SPDX-License-Identifier: MIT
#include "game/Vent.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/Vent.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/BonePose.hpp"
#include "game/ArmIk.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "game/SupportGrip.hpp"
#include "game/Throwables.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

namespace mevr::game::vent {
namespace {

namespace vnt = ik::vent;

constexpr int kLeft = 0;
constexpr int kRight = 1;
// Grip latch, as the pad mapping's (game/gamepad_mapping).
constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;
// The writer's "hands crossed" distance (engine/bone_pose kCrossedMeters): no gun frame then.
constexpr double kCrossedMeters = 50.0;

[[maybe_unused]] double SteadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool gStarted = false;
bool gEnabled = false;
bool gActive = false;  // this tick's conditions held (on foot, view, controllers, a gun in the right hand)
std::string gWhy = "not started";
std::string gLoggedWhy;

vnt::Machine gMachine;
vnt::Params gParams;
vnt::OnGun gOnGun;
bool gOnGunValid = false;

// Squeezes.
bool gClaimed = false;     // this tick's new left squeeze is the vent's
bool gClaimQueue = false;  // ...until the machine has seen it
bool gOwnGrip = false;
bool gHaveCount = false;
std::uint32_t gLastCount = 0;
bool gToldOldGrip = false;
bool gToldNoHaptics = false;

// The held gun's heat this tick.
double gHeatRaw = -1.0;  // m_fCurrentHeat
double gHeat = -1.0;     // ...as a fraction of ventMaxHeat
bool gOverheated = false;
bool gWasOverheated = false;
double gMaxHeatSeen = 0.0;

// Heartbeat counters and the last event.
unsigned gGrabs = 0;
unsigned gVents = 0;
unsigned gRefusals = 0;
unsigned gSlow = 0;
unsigned gLapsed = 0;
std::string gLast = "none";

void SetWhy(std::string why) {
    gWhy = std::move(why);
    if (gWhy != gLoggedWhy) {
        gLoggedWhy = gWhy;
        log::Info("vent: {}", gWhy);
    }
}

[[maybe_unused]] void Pulse(Link const& link, int hand, ik::throwing::Buzz const& buzz) {
    if (!(buzz.amplitude > 0.0f) || !(buzz.seconds > 0.0f)) {
        return;
    }
    if (link.hapticHz != nullptr) {
        link.hapticHz(hand, buzz.seconds, buzz.amplitude, buzz.frequency);
    } else if (link.haptic != nullptr) {
        link.haptic(hand, buzz.seconds, buzz.amplitude);
    } else if (!gToldNoHaptics) {
        gToldNoHaptics = true;
        log::Warn("vent: the host can't vibrate the controllers (services older than v9): no haptics");
    }
}

[[maybe_unused]] std::string Percent(double fraction) {
    return fraction >= 0.0 ? std::format("{:.0f}%", 100.0 * fraction) : std::string("?");
}

void Inactive(std::string why) {
    if (gMachine.Holding()) {
        log::Info("vent: the left hand's hold on the receiver is dropped ({})", why);
    }
    gMachine.Reset();
    gActive = false;
    gOnGunValid = false;
    gClaimQueue = false;
    gHeat = gHeatRaw = -1.0;
    gOverheated = false;
    SetWhy(std::move(why));
}

#if defined(MEVR_GAME_LE1)

// Our reflection layer (research/34): BioWeaponRanged.m_fCurrentHeat (+0x500) and m_bOverheated (+0x548 bit 0x10,
// LExSDKv2's headers; research/45), the event DoCoolDownEffect, BioPawn.IsSwitchingWeapons; engine/ReflectCheck
// compares each.
ue3::FieldRef gCurrentHeat{"SFXGame.BioWeaponRanged", "m_fCurrentHeat"};
ue3::FieldRef gOverheatedField{"SFXGame.BioWeaponRanged", "m_bOverheated"};
ue3::FunctionRef gCoolDownEffectFn{"SFXGame.BioWeaponRanged.DoCoolDownEffect"};
ue3::FunctionRef gIsSwitchingWeaponsFn{"SFXGame.BioPawn.IsSwitchingWeapons"};
ue3::Function* gCoolDownEffect = nullptr;
ue3::Function* gIsSwitchingWeapons = nullptr;

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

void ReadHeat(ue3::Object* weapon, hand_calibration::Vent const& settings) {
    std::optional<float> const heat = gCurrentHeat.Read<float>(weapon);
    gHeatRaw = heat && std::isfinite(*heat) ? *heat : -1.0;
    gHeat = gHeatRaw >= 0.0 ? gHeatRaw / settings.maxHeat : -1.0;
    gMaxHeatSeen = std::max(gMaxHeatSeen, gHeatRaw);
    gOverheated = gOverheatedField.ReadBool(weapon).value_or(false);
    if (gOverheated && !gWasOverheated) {
        // The scale check for ventMaxHeat (research/45 [verify]): what m_fCurrentHeat reads when the game overheats.
        log::Info("vent: the {} overheated at heat {:.3f} (ventMaxHeat {:.2f}{})", ue3::runtime::FullName(weapon), gHeatRaw,
            settings.maxHeat,
            gHeatRaw > 0.0 && std::abs(gHeatRaw - settings.maxHeat) > 0.15 * settings.maxHeat
                ? ": the setting doesn't match the game's scale; set ventMaxHeat to about this heat"
                : "");
    }
    gWasOverheated = gOverheated;
}

void DoVent(Link const& link, ue3::Object* weapon, hand_calibration::Vent const& settings, vnt::Step const& step) {
    double const before = gHeatRaw;
    bool const wasOverheated = gOverheated;
    bool wrote = false;
    if (float* const heat = gCurrentHeat.In<float>(weapon)) {
        *heat = 0.0f;
        write_journal::Wrote("Vent.heat", heat);
        wrote = true;
    }
    if (void* const word = gOverheatedField.Address(weapon)) {
        gOverheatedField.WriteBool(weapon, false);
        write_journal::Wrote("Vent.overheated", word, sizeof(std::uint32_t));
    }
    bool const effect = settings.effect && gCoolDownEffect != nullptr;
    if (effect) {
        CallVoid(weapon, gCoolDownEffect);  // the game's own cool-down feedback; game thread (the mod's tick)
    }
    ++gVents;
    Pulse(link, kLeft, vnt::VentPulse(settings.hapticAmplitude));
    Pulse(link, kRight, vnt::VentPulse(settings.hapticAmplitude));
    gLast = std::format("vented at {:.1f} m/s ({:.2f} back, {:.2f} m), heat {:.2f} -> 0{}", step.release.speed, step.back, step.travel,
        before, wasOverheated ? " (overheated)" : "");
    log::Info("vent: VENTED the {}: heat {:.3f} ({} of the overheat{}) -> 0{} with a {:.1f} m/s stroke ({:.1f} m/s back along the gun, "
              "{:.2f} m pulled, fastest {:.0f} ms before the release); both hands pulse{}; next in {:.1f} s",
        ue3::runtime::FullName(weapon), before, Percent(gHeat), wasOverheated ? ", overheated" : "", wrote ? "" : " (the heat field didn't resolve: NOT written)",
        step.release.speed, step.back, step.travel, step.release.peakAgo * 1000.0, effect ? "; the game's cool-down effect" : "",
        settings.cooldownS);
    gHeatRaw = 0.0;
    gHeat = 0.0;
    gOverheated = false;
    gWasOverheated = false;
}

#endif  // MEVR_GAME_LE1

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    if (!ue3::runtime::Ready()) {
        return false;
    }
    gCoolDownEffect = gCoolDownEffectFn.Get();
    gIsSwitchingWeapons = gIsSwitchingWeaponsFn.Get();
    bool const fields = gCurrentHeat.Get() != nullptr && gOverheatedField.Get() != nullptr;
    gStarted = fields;
    log::Info("vent: {}{}{}", fields ? "BioWeaponRanged.m_fCurrentHeat and m_bOverheated found" : "the heat fields are MISSING: the vent stays off",
        gCoolDownEffect != nullptr ? "; DoCoolDownEffect found" : "; DoCoolDownEffect not found (no effect on a vent)",
        gIsSwitchingWeapons != nullptr ? "" : "; BioPawn.IsSwitchingWeapons not found");
    return gStarted;
#else
    return false;
#endif
}

void Claim(Link const& link) {
    gClaimed = false;
    hand_calibration::Refresh();  // at most once a second
    vr::controllers::State const controllers = vr::controllers::Latest();
    bool const was = gOwnGrip;
    gOwnGrip = controllers.active && (gOwnGrip ? controllers.left.squeeze > kGripOff : controllers.left.squeeze >= kGripOn);
    bool fresh = false;
    if (link.leftGrip) {
        // The host's count of left squeezes that began away from the gun (the support grip's zone, which game/support_grip
        // hides while the hand is at the receiver of a hot gun): each new one, while it's still the current squeeze.
        std::uint32_t const count = *link.leftGrip >> 1;
        fresh = gHaveCount && count != gLastCount && (*link.leftGrip & 1u) != 0;
        gLastCount = count;
        gHaveCount = true;
    } else {
        fresh = gOwnGrip && !was && !support_grip::Held();
        if (!gToldOldGrip && gEnabled) {
            gToldOldGrip = true;
            log::Warn("vent: the host doesn't report the left grip (services older than v11): the vent follows our own reading of "
                      "the grip, and the game may see that squeeze too; close the game and redeploy the host");
        }
    }
    if (!fresh || !gEnabled || !gActive || !gOnGunValid || throwables::ClaimedLeft() || support_grip::Held() ||
        !controllers.left.gripValid) {
        return;
    }
    if (gMachine.InZone()) {
        gClaimed = true;
        gClaimQueue = true;
    }
}

bool ClaimedLeft() {
    return gClaimed;
}

void Tick(double deltaSeconds, Link const& link) {
    (void)deltaSeconds;
    hand_calibration::Vent const settings = hand_calibration::GetVent();
    gEnabled = settings.enabled;
    gParams.minSpeed = settings.speedMin;
    gParams.minTravel = settings.travel;
    gParams.maxHoldSeconds = settings.holdMax;
    gParams.cooldownSeconds = settings.cooldownS;
    gParams.minHeat = settings.minHeat;
#if defined(MEVR_GAME_LE1)
    if (!settings.enabled) {
        Inactive("off ([Vent] enabled = 0)");
        return;
    }
    if (!gStarted) {
        Inactive("waiting for the script fields");
        return;
    }
    auto const mode = head_tracking::GameMode();
    if (!mode || *mode != 0) {
        Inactive(std::format("not on foot (game mode {})", mode ? std::to_string(*mode) : std::string("unknown")));
        return;
    }
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid) {
        Inactive("no head-tracked world view (menu, flat screen or VR off)");
        return;
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!controllers.active) {
        Inactive("the controllers aren't active");
        return;
    }
    ue3::Object* const weapon = weapon::HeldObject();
    if (weapon == nullptr) {
        Inactive("no ranged weapon in the right hand (holstered, or not verified yet)");
        return;
    }
    gActive = true;
    SetWhy("active");
    ReadHeat(weapon, settings);

    // The left hand in the gun's frame, as the pose writer measured it for the support grip (arm IK asked for it).
    auto const sample = arm_ik::SupportRequested() ? engine::bone_pose::LatestSupport() : std::nullopt;
    gOnGunValid = sample && sample->gunFrame && sample->distanceMeters >= 0.0 && sample->distanceMeters < kCrossedMeters &&
        controllers.left.gripValid;
    if (gOnGunValid) {
        gOnGun = {sample->alongMeters, sample->upMeters, sample->sideMeters};
    }

    vnt::Input input;
    input.now = SteadySeconds();
    input.onGunValid = gOnGunValid;
    input.onGun = gOnGun;
    input.newSqueeze = gClaimQueue;
    input.squeeze = controllers.left.squeeze;
    input.heat = gHeat;
    ue3::Object* const pawn = player::BioPawn(player::Controller());
    input.canVent = pawn != nullptr && !CallBool(pawn, gIsSwitchingWeapons);
    gClaimQueue = false;
    vnt::Step const step = gMachine.Update(input, settings.zone, gParams);
    if (step.entered) {
        bool const hot = vnt::Refusal(gHeat, gMachine.CooldownLeft(input.now), gParams) == nullptr;
        log::Info("vent: the left hand is at the receiver ({:.2f} m along the gun, {:.2f} above, {:.2f} beside; heat {}{})", gOnGun.along,
            gOnGun.up, gOnGun.side, Percent(gHeat), hot ? ": a squeeze and a pull back vents it" : ": nothing to vent; a squeeze is the two-handed grip");
        if (hot) {
            Pulse(link, kLeft, vnt::EdgeTick(settings.hapticAmplitude));
        }
    }
    if (step.left) {
        log::Info("vent: the left hand left the receiver ({:.2f} along, {:.2f} above, {:.2f} beside)", gOnGun.along, gOnGun.up, gOnGun.side);
    }
    switch (step.event) {
    case vnt::Event::Grabbed:
        ++gGrabs;
        Pulse(link, kLeft, ik::throwing::GrabClick(settings.hapticAmplitude));
        log::Info("vent: the left hand took the receiver (heat {}): pull it back and away, letting go, to vent", Percent(gHeat));
        break;
    case vnt::Event::Vented:
        DoVent(link, weapon, settings, step);
        break;
    case vnt::Event::Refused: {
        ++gRefusals;
        Pulse(link, kLeft, ik::throwing::RefusedBuzz(settings.hapticAmplitude));
        std::string detail = step.why != nullptr ? step.why : "?";
        if (step.why != nullptr && std::string_view(step.why) == "cooldown") {
            detail += std::format(", {:.1f} s left", gMachine.CooldownLeft(input.now));
        } else if (step.why != nullptr && std::string_view(step.why) == "the gun is cool") {
            detail += std::format(" (heat {} under ventMinHeat {:.2f})", Percent(gHeat), settings.minHeat);
        }
        gLast = std::format("refused: {}", detail);
        log::Info("vent: a squeeze at the receiver: no vent ({})", detail);
        break;
    }
    case vnt::Event::Slow:
        ++gSlow;
        gLast = std::format("let go without a rack: {}", step.why != nullptr ? step.why : "?");
        log::Info("vent: the left hand let go at {:.2f} m/s ({:.2f} m/s back along the gun, {:.2f} m pulled; at least {:.1f} m/s, "
                  "{:.0f}% back, {:.2f} m): no vent ({})",
            step.release.valid ? step.release.speed : 0.0, step.back, step.travel, settings.speedMin, 100.0 * gParams.backFraction,
            settings.travel, step.why != nullptr ? step.why : "?");
        break;
    case vnt::Event::Lapsed:
        ++gLapsed;
        gLast = "held too long";
        log::Info("vent: the left grip stayed on the receiver over {:.1f} s: a grip, not a rack; nothing when it opens", settings.holdMax);
        break;
    case vnt::Event::Lost:
        gLast = "lost";
        log::Info("vent: the hold on the receiver is lost ({})", step.why != nullptr ? step.why : "?");
        break;
    case vnt::Event::None:
        break;
    }
#else
    (void)link;
    Inactive("LE1 only");
#endif
}

bool MasksSupportZone() {
    if (!gEnabled || !gActive || !gOnGunValid || !gMachine.InZone()) {
        return false;
    }
    // A hot gun's receiver is the vent's; a cool one's is the two-handed grip as before.
    return gMachine.Holding() || vnt::Refusal(gHeat, gMachine.CooldownLeft(SteadySeconds()), gParams) == nullptr;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    double const now = SteadySeconds();
    return std::format("{{\"enabled\": {}, \"active\": {}, \"why\": {}, \"onGun\": {}, \"inZone\": {}, \"holding\": {}, \"masksSupportZone\": {}, "
                       "\"heat\": {}, \"heatRaw\": {}, \"overheated\": {}, \"cooldownLeft\": {}, \"vents\": {}, \"last\": {}}}",
        gEnabled ? "true" : "false", gActive ? "true" : "false", json::JsonString(gWhy),
        gOnGunValid ? std::format("[{}, {}, {}]", json::JsonNumber(gOnGun.along), json::JsonNumber(gOnGun.up), json::JsonNumber(gOnGun.side))
                    : std::string("null"),
        gMachine.InZone() ? "true" : "false", gMachine.Holding() ? "true" : "false", MasksSupportZone() ? "true" : "false",
        json::JsonNumber(gHeat), json::JsonNumber(gHeatRaw), gOverheated ? "true" : "false", json::JsonNumber(gMachine.CooldownLeft(now)),
        gMachine.Vents(), json::JsonString(gLast));
}

void LogStats() {
    if (!gEnabled && gVents == 0 && gGrabs == 0) {
        return;
    }
    log::Info("    vent: {}; heat {} (raw {:.3f}{}; max seen {:.3f}); the left hand {} the receiver{}; this period: {} grabs, {} vents, {} refused, "
              "{} slow, {} lapsed; last: {}",
        gWhy, Percent(gHeat), gHeatRaw, gOverheated ? ", overheated" : "", gMaxHeatSeen, gMachine.InZone() ? "at" : "off",
        gOnGunValid ? std::format(" ({:.2f} along, {:.2f} above, {:.2f} beside)", gOnGun.along, gOnGun.up, gOnGun.side) : std::string(" (no gun frame)"),
        gGrabs, gVents, gRefusals, gSlow, gLapsed, gLast);
    gGrabs = gVents = gRefusals = gSlow = gLapsed = 0;
}

void Shutdown() {
    gMachine.ResetAll();
    gParams = {};
    gOnGun = {};
    gOnGunValid = false;
    gActive = false;
    gEnabled = false;
    gClaimed = gClaimQueue = false;
    gOwnGrip = false;
    gHaveCount = false;
    gLastCount = 0;
    gToldOldGrip = gToldNoHaptics = false;
    gHeatRaw = gHeat = -1.0;
    gOverheated = gWasOverheated = false;
    gMaxHeatSeen = 0.0;
    gGrabs = gVents = gRefusals = gSlow = gLapsed = 0;
    gLast = "none";
    gStarted = false;
    gWhy = "not started";
    gLoggedWhy.clear();
#if defined(MEVR_GAME_LE1)
    gCoolDownEffect = gIsSwitchingWeapons = nullptr;
#endif
}

} // namespace mevr::game::vent
