// SPDX-License-Identifier: MIT
#include "game/Reload.hpp"

#include "core/Log.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/Throwing.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/PhysicalReloadRequest.hpp"
#include "core/vr/ReloadGesture.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/BonePose.hpp"
#include "game/ArmIk.hpp"
#include "game/BodyCalibration.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Holsters.hpp"
#include "game/Player.hpp"
#include "game/PowerWheel.hpp"
#include "game/SupportGrip.hpp"
#include "game/Throwables.hpp"
#include "game/Vent.hpp"
#include "game/Voice.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace mevr::game::reload {
namespace {

namespace slots = ik::body_slots;
namespace rg = vr::reload_gesture;
namespace rq = vr::reload;

constexpr int kLeft = 0;
constexpr int kRight = 1;
// The writer's "hands crossed" distance (engine/bone_pose kCrossedMeters): no gun frame then.
constexpr double kCrossedMeters = 50.0;
// A request the game never began within this long (and the counts unchanged) was refused.
constexpr double kRejectSeconds = 0.6;
// A reload still running after this long is reported once (the coordinator keeps waiting for the game).
constexpr double kLongSeconds = 6.0;

[[maybe_unused]] double SteadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool gStarted = false;
bool gEnabled = false;
bool gActive = false;
bool gClaimed = false;
std::string gWhy = "not started";
std::string gLoggedWhy;
bool gToldNoHaptics = false;

#if !defined(MEVR_GAME_LE1)
// The button's own route (research/le2-vrik.md; both sequels' script, read 2026-11-16): SFXGameModeDefault.TryReload checks
// IsCombatEnabled (ME2: draws a holstered gun and returns; ME3: interrupts a melee custom action), then calls
// Weapon.TryReload() through the VM, so a weapon already in its reload states ignores it.
ue3::FunctionRef gTryReloadFn{"SFXGame.SFXGameModeDefault.TryReload", ue3::kEveryGame};
ue3::FunctionRef gIsReloadingFn{"SFXGame.BioPlayerController.IsReloading", ue3::kEveryGame};
ue3::FunctionRef gCanReloadFn{"SFXGame.SFXWeapon.CanReload", ue3::kEveryGame};
ue3::FunctionRef gLoadedFn{"SFXGame.SFXWeapon.GetAmmoCountInMagazine", ue3::kEveryGame};
ue3::FunctionRef gGetStateNameFn{"Core.Object.GetStateName", ue3::kEveryGame};
// ME2 declares the counts on SFXWeapon (AmmoUsedCount a float), ME3 on its native base (an int).
#if defined(MEVR_GAME_LE2)
ue3::FieldRef gUsed{"SFXGame.SFXWeapon", "AmmoUsedCount", ue3::kEveryGame};
ue3::FieldRef gSpare{"SFXGame.SFXWeapon", "CurrentSpareAmmo", ue3::kEveryGame};
#else
ue3::FieldRef gUsed{"SFXGame.SFXWeapon_NativeBase", "AmmoUsedCount", ue3::kEveryGame};
ue3::FieldRef gSpare{"SFXGame.SFXWeapon_NativeBase", "CurrentSpareAmmo", ue3::kEveryGame};
#endif
ue3::FieldRef gInfinite{"SFXGame.SFXWeapon", "bInfiniteAmmo", ue3::kEveryGame};
ue3::FieldRef gCanBeReloaded{"SFXGame.SFXWeapon", "bWeaponCanBeReloaded", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gPawnWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
ue3::ClassRef gWeaponClass{"SFXGame.SFXWeapon", ue3::kEveryGame};
ue3::Function* gTryReload = nullptr;
ue3::Function* gIsReloading = nullptr;
ue3::Function* gGetStateName = nullptr;
#endif

rg::Machine gMachine;
std::optional<rq::Coordinator> gCoordinator;
std::uint64_t gEpoch = 0;
std::uint64_t gSample = 0;
std::uint64_t gContextGeneration = 0;
bool gContextWas = false;
std::uint64_t gLease = 0;
bool gGranted = false;

// A generation that grows each time what it watches changes (the coordinator's identities: never zero once seen).
struct Generation {
    std::uintptr_t last = 0;
    std::uint64_t value = 0;
    std::uint64_t Note(std::uintptr_t now) {
        if (value == 0 || now != last) {
            last = now;
            ++value;
        }
        return value;
    }
};
Generation gWorld;
Generation gWeapon;

struct Ammo {
    bool valid = false;
    int loaded = -1;   // in the magazine (the game's GetAmmoCountInMagazine)
    int used = -1;     // AmmoUsedCount
    int spare = -1;    // CurrentSpareAmmo
    bool infinite = false;
    bool canBeReloaded = true;
};

// The request in flight, followed until the game says how it ended.
struct Request {
    rq::RequestKey key;
    double startS = 0.0;
    std::uintptr_t gun = 0;
    Ammo before;
    bool accepted = false;
    bool toldLong = false;
    bool coordinated = true;  // the coordinator issued it (false: `mevr reload try`, outside it)
};
std::optional<Request> gRequest;

// The last tick's view, for the snapshot and the logs.
ik::Vec3 gPouch;
double gPouchDistance = -1.0;
bool gAtPouch = false;
bool gOnGunValid = false;
ik::vent::OnGun gOnGun;
bool gInReceiver = false;
Ammo gAmmo;
bool gCanReload = false;
bool gReloading = false;
std::string gLast;

// Heartbeat counters (this period) and totals.
unsigned gGrabs = 0, gRefusals = 0, gDrops = 0, gInsertions = 0;
unsigned gRequests = 0, gFinished = 0, gAborted = 0, gRejected = 0;

[[maybe_unused]] void SetWhy(std::string why) {
    gWhy = std::move(why);
    if (gWhy != gLoggedWhy) {
        gLoggedWhy = gWhy;
        log::Info("reload: {}", gWhy);
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
        log::Warn("reload: the host can't vibrate the controllers (services older than v9): no haptics");
    }
}

[[maybe_unused]] std::string AmmoText(Ammo const& a) {
    if (!a.valid) {
        return "the ammo can't be read";
    }
    return std::format("magazine {}, used {}, spare {}{}", a.loaded, a.used, a.spare, a.infinite ? " (infinite)" : "");
}

#if !defined(MEVR_GAME_LE1)
// The gun the request is about: the one game/weapon verified in the right hand, else (the console without controllers, a
// request still followed after the hands went away) the player pawn's equipped weapon, if it's an SFXWeapon.
ue3::Object* HeldGun() {
    if (ue3::Object* const held = weapon::HeldObject()) {
        return held;
    }
    ue3::Object* const pawn = player::BioPawn(player::Controller());
    ue3::Object* const equipped = pawn != nullptr ? gPawnWeapon.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    return ue3::runtime::IsAlive(equipped) && ue3::IsA(equipped, gWeaponClass) ? equipped : nullptr;
}

// The weapon's own class's function of that name (an override in a subclass wins; script states don't: ProcessEvent).
ue3::Function* OwnFunction(ue3::Object* object, ue3::FunctionRef& base, std::string_view name) {
    if (!ue3::runtime::IsAlive(object) || base.Get() == nullptr) {
        return nullptr;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const own = r.FindFunction(r.ClassOf(object), name);
    return own != nullptr ? own : base.Get();
}

std::optional<bool> CallBool(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    if (!parms.Valid() || !ue3::runtime::Call(object, function, parms)) {
        return std::nullopt;
    }
    return parms.ReturnBool();
}

Ammo ReadAmmo(ue3::Object* gun) {
    Ammo a;
    if (!ue3::runtime::IsAlive(gun)) {
        return a;
    }
#if defined(MEVR_GAME_LE2)
    std::optional<float> const used = gUsed.Read<float>(gun);
    std::optional<int> const usedCount = used && std::isfinite(*used) ? std::optional<int>(static_cast<int>(std::lround(*used))) : std::nullopt;
#else
    std::optional<int> const usedCount = gUsed.Read<int>(gun);
#endif
    std::optional<int> const spare = gSpare.Read<int>(gun);
    std::optional<int> loaded;
    if (ue3::Function* const fn = OwnFunction(gun, gLoadedFn, "GetAmmoCountInMagazine")) {
        ue3::Params parms(ue3::runtime::Get(), fn);
        if (parms.Valid() && ue3::runtime::Call(gun, fn, parms)) {
            loaded = parms.Return<int>();
        }
    }
    a.valid = usedCount && spare && loaded;
    a.used = usedCount.value_or(-1);
    a.spare = spare.value_or(-1);
    a.loaded = loaded.value_or(-1);
    a.infinite = gInfinite.ReadBool(gun).value_or(false);
    a.canBeReloaded = gCanBeReloaded.ReadBool(gun).value_or(true);
    return a;
}

std::string StateOf(ue3::Object* object) {
    if (object == nullptr || gGetStateName == nullptr) {
        return {};
    }
    ue3::Params parms(ue3::runtime::Get(), gGetStateName);
    if (!ue3::runtime::Call(object, gGetStateName, parms)) {
        return {};
    }
    std::optional<ue3::Name> const name = parms.Return<ue3::Name>();
    return name ? ue3::runtime::NameText(*name) : std::string();
}

// The game is reloading: the controller says so, or the weapon is in a reload state (ME2's StartReload / Reloading).
bool Reloading(ue3::Object* gun) {
    ue3::Object* const pc = player::Controller();
    if (std::optional<bool> const says = CallBool(pc, gIsReloading); says && *says) {
        return true;
    }
    std::string const state = StateOf(gun);
    return state == "Reloading" || state == "StartReload";
}

bool CanReload(ue3::Object* gun) {
    return CallBool(gun, OwnFunction(gun, gCanReloadFn, "CanReload")).value_or(false);
}
#endif

[[maybe_unused]] void ReportTo(rq::FeedbackKind kind, char const* what) {
    if (!gRequest || !gCoordinator || !gRequest->coordinated) {
        return;
    }
    rq::Observation const seen = gCoordinator->Observe({gRequest->key, kind, true});
    if (seen != rq::Observation::Settled && seen != rq::Observation::Accepted) {
        log::Warn("reload: the coordinator took '{}' as {} (request {})", what, static_cast<int>(seen), gRequest->key.serial);
    }
}

// The request in flight: what the game did with it (the game thread, every tick, active or not).
[[maybe_unused]] void FollowRequest(double now) {
#if !defined(MEVR_GAME_LE1)
    if (!gRequest) {
        return;
    }
    Request& req = *gRequest;
    double const age = now - req.startS;
    ue3::Object* const gun = HeldGun();
    if (gun == nullptr || reinterpret_cast<std::uintptr_t>(gun) != req.gun) {
        bool const reloading = gun != nullptr && Reloading(gun);
        if (!reloading) {
            ++gAborted;
            gLast = "aborted: the gun changed";
            log::Info("reload: the request {} ended with the gun gone or changed after {:.2f} s ({}): aborted", req.key.serial, age,
                gun == nullptr ? "no gun in the hand" : "another gun");
            ReportTo(rq::FeedbackKind::Aborted, "aborted (the gun changed)");
            gRequest.reset();
        }
        return;
    }
    bool const reloading = Reloading(gun);
    Ammo const now_ = ReadAmmo(gun);
    if (reloading && !req.accepted) {
        req.accepted = true;
        log::Info("reload: the game reloads ({:.2f} s after the request; {})", age, AmmoText(now_));
        ReportTo(rq::FeedbackKind::Accepted, "accepted");
    }
    if (reloading) {
        if (age > kLongSeconds && !req.toldLong) {
            req.toldLong = true;
            log::Warn("reload: the game is still reloading {:.0f} s after the request ({})", age, AmmoText(now_));
        }
        return;
    }
    bool const changed = now_.valid && req.before.valid &&
        (now_.loaded > req.before.loaded || now_.used < req.before.used || now_.spare < req.before.spare);
    if (changed) {
        ++gFinished;
        gLast = std::format("reloaded: {}", AmmoText(now_));
        log::Info("reload: reloaded after {:.2f} s: {} (was {}); the game's own reload, ammo as the game counts it", age, AmmoText(now_),
            AmmoText(req.before));
        ReportTo(rq::FeedbackKind::Finished, "finished");
        gRequest.reset();
    } else if (req.accepted) {
        ++gAborted;
        gLast = "aborted";
        log::Info("reload: the game's reload stopped after {:.2f} s with the ammo unchanged ({}): aborted", age, AmmoText(now_));
        ReportTo(rq::FeedbackKind::Aborted, "aborted");
        gRequest.reset();
    } else if (age > kRejectSeconds) {
        ++gRejected;
        gLast = "refused by the game";
        log::Info("reload: the game didn't reload ({:.2f} s, {}): refused (in cover, a power, a melee, or nothing to reload)", age,
            AmmoText(now_));
        ReportTo(rq::FeedbackKind::Rejected, "rejected");
        gRequest.reset();
    }
#else
    (void)now;
#endif
}

#if !defined(MEVR_GAME_LE1)
// The coordinator said: ask the game. Never DoReload, never an R press; the game counts the ammo.
void Ask(rq::RequestKey const& key, ue3::Object* gun, Ammo const& before, double now, char const* who, bool coordinated = true) {
    ++gRequests;
    void* const mode = holsters::DefaultGameMode();
    if (mode == nullptr || gTryReload == nullptr) {
        log::Info("reload: {}: no {} to ask the game's reload with", who, mode == nullptr ? "default game mode" : "SFXGameModeDefault.TryReload");
        gRequest = Request{key, now, reinterpret_cast<std::uintptr_t>(gun), before, false, false, coordinated};
        ReportTo(rq::FeedbackKind::Rejected, "rejected (no route)");
        gRequest.reset();
        ++gRejected;
        return;
    }
    gRequest = Request{key, now, reinterpret_cast<std::uintptr_t>(gun), before, false, false, coordinated};
    log::Info("reload: {}: SFXGameModeDefault.TryReload, the game's own reload (request {}; {})", who, key.serial, AmmoText(before));
    ue3::Params parms(ue3::runtime::Get(), gTryReload);
    ue3::runtime::Call(static_cast<ue3::Object*>(mode), gTryReload, parms);  // the game thread, through our reflection layer
}
#endif

[[maybe_unused]] void Inactive(std::string why) {
    if (gMachine.Holding()) {
        log::Info("reload: the clip in the left hand is gone ({})", why);
    }
    gMachine.Reset();
    if (gCoordinator) {
        gCoordinator->CancelGesture();
    }
    gActive = false;
    gContextWas = false;
    gGranted = false;
    gAtPouch = false;
    gOnGunValid = false;
    gInReceiver = false;
    gPouchDistance = -1.0;
    SetWhy(std::move(why));
}

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    return false;  // ME1 has heat and the vent (game/vent): nothing to reload
#else
    if (!ue3::runtime::Up()) {
        return false;
    }
    gTryReload = gTryReloadFn.Get();
    gIsReloading = gIsReloadingFn.Get();
    gGetStateName = gGetStateNameFn.Get();
    bool const fields = gUsed.Get() != nullptr && gSpare.Get() != nullptr && gLoadedFn.Get() != nullptr && gCanReloadFn.Get() != nullptr;
    gStarted = fields && gTryReload != nullptr;
    if (gEpoch == 0) {
        gEpoch = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) | 1u;
    }
    if (!gCoordinator) {
        gCoordinator.emplace(gEpoch);
    }
    log::Info("reload: {} (SFXGameModeDefault.TryReload {}, SFXWeapon.CanReload {}, GetAmmoCountInMagazine {}, AmmoUsedCount and "
              "CurrentSpareAmmo {}, BioPlayerController.IsReloading {}, Object.GetStateName {}); the receiver is [Vent]'s zone, the "
              "pouch a body slot on the left hip",
        gStarted ? "the reload by hand is ready" : "names MISSING: the reload by hand stays off", gTryReload != nullptr,
        gCanReloadFn.Get() != nullptr, gLoadedFn.Get() != nullptr, gUsed.Get() != nullptr && gSpare.Get() != nullptr,
        gIsReloading != nullptr, gGetStateName != nullptr);
    return gStarted;
#endif
}

void Tick(double deltaSeconds, Link const& link) {
    (void)deltaSeconds;  // the gesture runs on the steady clock
    gClaimed = false;
#if defined(MEVR_GAME_LE1)
    (void)link;
    return;
#else
    hand_calibration::Reload const settings = hand_calibration::GetReload();
    gEnabled = settings.enabled;
    double const now = SteadySeconds();
    FollowRequest(now);
    if (!gStarted || !gCoordinator) {
        Inactive("waiting for the script functions");
        return;
    }
    if (!settings.enabled) {
        Inactive("off ([Reload] enabled = 0)");
        return;
    }
    auto const mode = head_tracking::GameMode();
    if (!mode || *mode != 0) {
        Inactive(std::format("not on foot (game mode {})", mode ? std::to_string(*mode) : std::string("unknown")));
        return;
    }
    if (!vr::host_view::Latest().valid) {
        Inactive("no head-tracked world view (menu, flat screen or VR off)");
        return;
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!controllers.active || body_calibration::Calibrating()) {
        Inactive("the controllers aren't active");
        return;
    }
    ue3::Object* const gun = weapon::HeldObject();
    if (gun == nullptr) {
        Inactive("no gun in the right hand (holstered, or not verified yet)");
        return;
    }
    std::optional<holsters::BodyFrame> const body = holsters::SlotFrame();
    if (!body) {
        Inactive("the holsters' body frame isn't up (the pouch hangs from it)");
        return;
    }
    ue3::Object* const pawn = player::BioPawn(player::Controller());
    if (!gContextWas) {
        ++gContextGeneration;
        gContextWas = true;
    }
    gActive = true;
    SetWhy("active");

    // The pouch: a body slot for the left hand, on the holsters' frame and scale (fixed meters seated).
    bool const seated = body_calibration::Position() == vr::calibration::PlayPosition::Seated;
    slots::Slot const pouch = seated ? settings.seatedPouch : settings.pouch;
    slots::Frame frame;
    frame.anchor = {body->anchor[0], body->anchor[1], body->anchor[2]};
    frame.yaw = body->yaw;
    gPouch = slots::SlotCenter(frame, pouch, seated ? 1.0 : body->scale);
    vr::controllers::Hand const& left = controllers.left;
    ik::Vec3 const hand{left.grip.position.x, left.grip.position.y, left.grip.position.z};
    gPouchDistance = left.gripValid ? ik::Length(hand - gPouch) : -1.0;
    bool const atPouch = gPouchDistance >= 0.0 && gPouchDistance <= pouch.radius;
    if (atPouch != gAtPouch) {
        gAtPouch = atPouch;
        log::Info("reload: the left hand {} the clip pouch ({:.2f} m from its centre, radius {:.2f})", atPouch ? "is at" : "left",
            gPouchDistance, pouch.radius);
    }

    // The left hand in the gun's frame, as the pose writer measured it for the support grip (arm IK asked for it).
    auto const sample = arm_ik::SupportRequested() ? engine::bone_pose::LatestSupport() : std::nullopt;
    gOnGunValid = sample && sample->gunFrame && sample->distanceMeters >= 0.0 && sample->distanceMeters < kCrossedMeters &&
        left.gripValid;
    if (gOnGunValid) {
        gOnGun = {sample->alongMeters, sample->upMeters, sample->sideMeters};
    }

    gAmmo = ReadAmmo(gun);
    gReloading = Reloading(gun);
    gCanReload = !gReloading && !gRequest && CanReload(gun);

    rg::Settings gesture;
    gesture.pouchRadiusM = pouch.radius;
    gesture.receiver = hand_calibration::GetVent().zone;
    gesture.maxStepM = settings.maxStepM;
    gesture.holdTimeoutS = settings.holdTimeoutS;
    rg::Input in;
    in.nowS = now;
    in.tracked = left.gripValid;
    in.pouchDistanceM = gPouchDistance;
    in.gunValid = gOnGunValid;
    in.onGun = gOnGun;
    in.grip = left.squeeze;
    in.squeezeClaimed = throwables::ClaimedLeft() || vent::ClaimedLeft() || support_grip::Held() || voice::ClaimedLeft() ||
        power_wheel::OwnsLeftHand();
    in.reloadable = gCanReload;
    rg::Output const out = gMachine.Update(in, gesture);
    gInReceiver = out.inReceiver;
    double const strength = settings.hapticAmplitude;
    switch (out.event) {
    case rg::Event::Grabbed:
        ++gGrabs;
        gLast = "a clip in the left hand";
        log::Info("reload: the left hand took a clip from the pouch ({}): push it into the gun's receiver", AmmoText(gAmmo));
        Pulse(link, kLeft, ik::throwing::GrabClick(strength));
        break;
    case rg::Event::Inserted:
        ++gInsertions;
        log::Info("reload: the clip went into the receiver ({:.2f} m along the gun, {:.2f} above, {:.2f} beside)", gOnGun.along,
            gOnGun.up, gOnGun.side);
        Pulse(link, kLeft, ik::throwing::GrabClick(strength));
        Pulse(link, kRight, ik::throwing::GrabClick(strength));
        break;
    case rg::Event::Dropped:
        ++gDrops;
        gLast = std::format("dropped: {}", out.why != nullptr ? out.why : "?");
        log::Info("reload: the clip is dropped ({})", out.why != nullptr ? out.why : "?");
        break;
    case rg::Event::Refused:
        ++gRefusals;
        gLast = std::format("refused: {}", out.why != nullptr ? out.why : "?");
        log::Info("reload: a squeeze at the pouch: no clip ({}; {}{})", out.why != nullptr ? out.why : "?", AmmoText(gAmmo),
            gReloading ? ", the game is reloading" : gRequest ? ", a reload is on its way" : "");
        Pulse(link, kLeft, ik::throwing::RefusedBuzz(strength));
        break;
    case rg::Event::Lost:
        ++gDrops;
        gLast = std::format("lost: {}", out.why != nullptr ? out.why : "?");
        log::Info("reload: the clip is lost ({})", out.why != nullptr ? out.why : "?");
        if (gCoordinator) {
            gCoordinator->CancelGesture();
        }
        break;
    case rg::Event::None:
        break;
    }

    // The coordinator's frame: identities from the world, the held gun and this context; the left hand granted while no
    // support grip holds it (a new lease each time it's granted again).
    bool const granted = left.gripValid && !support_grip::Held() && !power_wheel::OwnsLeftHand();
    if (granted && !gGranted) {
        ++gLease;
    }
    gGranted = granted;
    ue3::Object* const world = pawn != nullptr ? gWorldInfo.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    rq::Frame f;
    f.sample = ++gSample;
    f.identity = {gWorld.Note(reinterpret_cast<std::uintptr_t>(world) ^ (reinterpret_cast<std::uintptr_t>(pawn) << 1)),
        gWeapon.Note(reinterpret_cast<std::uintptr_t>(gun)), gContextGeneration};
    f.supportedFiniteReserve = gAmmo.valid && !gAmmo.infinite && gAmmo.canBeReloaded;
    f.eligible = gCanReload;
    f.contextAllowed = true;
    f.trackingValid = left.gripValid;
    if (granted) {
        f.grant = {f.identity, gLease, rq::Hand::Left, true};
    }
    f.down = out.down;
    if (out.down.insert && gCoordinator->HeldClip()) {
        f.insertedClip = *gCoordinator->HeldClip();
    }
    rq::StepResult const step = gCoordinator->Step(f);
    if (step.request) {
        Ask(*step.request, gun, gAmmo, now, "the clip went in");
    } else if (out.event == rg::Event::Inserted) {
        log::Info("reload: the insertion asked nothing (coordinator {}: {})", static_cast<int>(step.admission),
            step.admission == rq::Admission::WaitingNative ? "a reload is still on its way" : "the gesture didn't start cleanly");
    }
    if (step.clipInvalidated && gMachine.Holding()) {
        log::Info("reload: the clip in the left hand is gone (the gun, the hand or the game changed)");
        gMachine.Reset();
    }
    gClaimed = gMachine.Holding() || out.event == rg::Event::Grabbed || out.event == rg::Event::Inserted;
#endif
}

bool ClaimedLeft() {
    return gClaimed;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    std::string request = "null";
    if (gRequest) {
        request = std::format("{{\"serial\": {}, \"accepted\": {}, \"before\": [{}, {}, {}]}}", gRequest->key.serial,
            gRequest->accepted ? "true" : "false", gRequest->before.loaded, gRequest->before.used, gRequest->before.spare);
    }
    return std::format("{{\"enabled\": {}, \"active\": {}, \"why\": {}, \"pouch\": [{}, {}, {}], \"pouchDistance\": {}, \"atPouch\": {}, "
                       "\"onGun\": {}, \"inReceiver\": {}, \"holding\": {}, \"ammo\": {{\"valid\": {}, \"magazine\": {}, \"used\": {}, "
                       "\"spare\": {}, \"infinite\": {}}}, \"canReload\": {}, \"reloading\": {}, \"request\": {}, \"last\": {}, "
                       "\"totals\": {{\"insertions\": {}, \"requests\": {}}}}}",
        gEnabled ? "true" : "false", gActive ? "true" : "false", json::JsonString(gWhy), json::JsonNumber(gPouch.x),
        json::JsonNumber(gPouch.y), json::JsonNumber(gPouch.z), json::JsonNumber(gPouchDistance), gAtPouch ? "true" : "false",
        gOnGunValid ? std::format("[{}, {}, {}]", json::JsonNumber(gOnGun.along), json::JsonNumber(gOnGun.up), json::JsonNumber(gOnGun.side))
                    : std::string("null"),
        gInReceiver ? "true" : "false", gMachine.Holding() ? "true" : "false", gAmmo.valid ? "true" : "false", gAmmo.loaded,
        gAmmo.used, gAmmo.spare, gAmmo.infinite ? "true" : "false", gCanReload ? "true" : "false", gReloading ? "true" : "false",
        request, json::JsonString(gLast), gMachine.Insertions(), gRequests);
}

void LogStats() {
#if !defined(MEVR_GAME_LE1)
    if (!gEnabled && gGrabs == 0) {
        return;
    }
    log::Info("    reload: {}; {}; this period: {} clips taken, {} refused, {} dropped, {} insertions, {} requests ({} reloaded, {} "
              "aborted, {} refused by the game){}; the pouch {:.2f} m from the left hand",
        gWhy, gActive ? AmmoText(gAmmo) : std::string("-"), gGrabs, gRefusals, gDrops, gInsertions, gRequests, gFinished, gAborted,
        gRejected, gRequest ? std::format("; request {} in flight{}", gRequest->key.serial, gRequest->accepted ? " (reloading)" : "") : std::string(),
        gPouchDistance);
    gGrabs = gRefusals = gDrops = gInsertions = 0;
    gRequests = gFinished = gAborted = gRejected = 0;
#endif
}

void Shutdown() {
    gMachine.Reset();
    gRequest.reset();
    // The coordinator stays: a new one would need a new epoch, and an unsettled request of this lifetime can't be cleared by
    // construction (core/vr/PhysicalReloadRequest). The module's next load makes its own (its globals start fresh then).
    gStarted = false;
    gActive = false;
    gClaimed = false;
    gWhy = "not started";
    gLoggedWhy.clear();
    gAtPouch = false;
    gOnGunValid = false;
    gInReceiver = false;
    gPouchDistance = -1.0;
    gContextWas = false;
    gGranted = false;
    gToldNoHaptics = false;
#if !defined(MEVR_GAME_LE1)
    gTryReload = gIsReloading = gGetStateName = nullptr;
#endif
}

std::string Command(std::string const& args) {
#if defined(MEVR_GAME_LE1)
    (void)args;
    return "the reload by hand is the sequels' (ME1 has heat: mevr vent)";
#else
    std::size_t const first = args.find_first_not_of(" \t");
    std::string const trimmed = first == std::string::npos ? std::string() : args.substr(first);
    std::string const verb = trimmed.substr(0, trimmed.find(' '));
    if (!gStarted) {
        return "the reload isn't started (no script functions yet)";
    }
    ue3::Object* const gun = HeldGun();
    if (gun == nullptr) {
        return "no gun (none verified in the right hand, none equipped)";
    }
    Ammo const ammo = ReadAmmo(gun);
    bool const reloading = Reloading(gun);
    bool const can = !reloading && CanReload(gun);
    if (verb.empty() || verb == "status") {
        return std::format("{}: {}; CanReload {}, reloading {}; {}; the gesture: {}{}", ue3::runtime::FullName(gun), AmmoText(ammo),
            can, reloading, gWhy, gMachine.Holding() ? "a clip in the left hand" : "no clip",
            gRequest ? std::format("; request {} in flight", gRequest->key.serial) : std::string());
    }
    if (verb == "try") {
        if (gRequest) {
            return std::format("a request ({}) is still in flight", gRequest->key.serial);
        }
        if (!can) {
            return std::format("the game wouldn't reload now ({}; reloading {})", AmmoText(ammo), reloading);
        }
        // Outside the coordinator (no gesture): its own key, followed and logged the same way.
        static std::uint64_t serial = 1000000;
        rq::RequestKey key{gEpoch, ++serial, {}};
        Ask(key, gun, ammo, SteadySeconds(), "mevr reload try", false);
        return std::format("asked the game's reload ({}); see the log's reload: lines", AmmoText(ammo));
    }
    return "usage: mevr reload [status | try]";
#endif
}

} // namespace mevr::game::reload
