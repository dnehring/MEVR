#include "game/Holsters.hpp"

#include "core/Log.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Engine.hpp"
#include "engine/Objects.hpp"
#include "game/ArmIk.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>

namespace mevr::game::holsters {
namespace {

namespace slots = ik::body_slots;
using slots::Weapon;

constexpr int kRight = 1;
// Grip latch, as the pad mapping's (game/gamepad_mapping).
constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;
constexpr float kTriggerOn = 0.5f;
// Pulses (seconds, amplitude): entering a slot uses [Holsters] hapticAmplitude.
constexpr float kEnterSeconds = 0.025f;
constexpr float kActionSeconds = 0.06f;
constexpr float kActionAmplitude = 0.8f;
constexpr float kRefusedSeconds = 0.12f;
constexpr float kRefusedAmplitude = 0.3f;
// How long the game gets to show a requested draw or holster before we say it didn't happen.
constexpr double kResultSeconds = 3.0;

#if defined(SDK_TARGET_LE1)
static_assert(offsetof(ABioPawn, m_oBehavior) == 0x7D4);
static_assert(offsetof(UBioPawnBehavior, m_oEquipment) == 0x424);
static_assert(offsetof(UBioEquipment, m_QuickSlotArray) == 0x94);
static_assert(offsetof(UBioEquipment, m_eQuickSlotCurrent) == 0xB8);
static_assert(offsetof(ABioPlayerController, GameModeManager) == 0xA70);
static_assert(offsetof(USFXGameModeManager, GameModes) == 0x70);

UFunction* gSwitchToWeapon = nullptr;
UFunction* gTryHolster = nullptr;
UFunction* gIsInCombatActionState = nullptr;
UFunction* gIsSwitchingWeapons = nullptr;
UFunction* gGetRangedWeaponType = nullptr;
#endif

bool gStarted = false;
slots::Machine gMachine;
slots::Anchor gAnchor;
ik::BodyYaw gOwnYaw;
bool gOwnYawActive = false;
double gYaw = 0.0;
bool gYawValid = false;
char const* gYawSource = "none";
slots::Frame gFrame;
std::array<ik::Vec3, slots::kSlots> gCenters{};
std::array<double, slots::kSlots> gDistances{-1.0, -1.0, -1.0, -1.0};
bool gNear = false;
bool gActive = false;          // this tick's conditions held
std::string gWhy = "not started";
std::string gLoggedWhy;
bool gOwnGrip = false;         // our latch of the right grip
bool gHaveCount = false;
std::uint32_t gLastCount = 0;
bool gToldOldHost = false;
bool gToldNoHaptics = false;
bool gWheelOff = false;
bool gEnabled = false;

// What the player carries, re-read each tick; the type map is rebuilt when the slots change.
struct Carried {
    std::array<std::uintptr_t, slots::kSlots> weapons{};  // compared by address only
    std::array<int, slots::kSlots> typeOfSlot{-1, -1, -1, -1};
    std::array<int, slots::kSlots> slotOfType{-1, -1, -1, -1};
    int current = -1;     // m_eQuickSlotCurrent
    bool drawn = false;   // IsInCombatActionState
    bool switching = false;
};
Carried gCarried;

// A request we made, to report what the game did with it.
struct Pending {
    slots::Action action = slots::Action::None;
    Weapon weapon = Weapon::None;
    double age = 0.0;
};
Pending gPending;

// Heartbeat counters.
unsigned gEntries = 0;
unsigned gDraws = 0;
unsigned gHolsters = 0;
unsigned gRefusals = 0;
double gNearest = 1e9;
int gNearestSlot = -1;

std::string V(ik::Vec3 const& v) {
    return std::format("({:.2f}, {:.2f}, {:.2f})", v.x, v.y, v.z);
}

void SetWhy(std::string why) {
    gWhy = std::move(why);
    if (gWhy != gLoggedWhy) {
        gLoggedWhy = gWhy;
        log::Info("holsters: {}", gWhy);
    }
}

[[maybe_unused]] void Pulse(Link const& link, float seconds, float amplitude) {
    if (amplitude <= 0.0f) {
        return;
    }
    if (link.haptic == nullptr) {
        if (!gToldNoHaptics) {
            gToldNoHaptics = true;
            log::Warn("holsters: the host can't vibrate the controllers (services older than v9): no pulses; close the game "
                      "and redeploy the host");
        }
        return;
    }
    link.haptic(kRight, seconds, amplitude);
}

#if defined(SDK_TARGET_LE1)

bool CallBool(UObject* object, UFunction* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    struct {
        unsigned long ReturnValue;
        unsigned char pad[60];
    } parms{};
    object->ProcessEvent(function, &parms, nullptr);  // directly: never the SDK's wrapper
    return parms.ReturnValue != 0;
}

int RangedType(ABioWeapon* weapon) {
    if (weapon == nullptr || gGetRangedWeaponType == nullptr || !weapon->IsA(ABioWeaponRanged::StaticClass())) {
        return -1;
    }
    ABioWeaponRanged_execGetRangedWeaponType_Parms parms;
    weapon->ProcessEvent(gGetRangedWeaponType, &parms, nullptr);
    return parms.ReturnValue <= 3 ? static_cast<int>(parms.ReturnValue) : -1;
}

struct Game {
    ABioPlayerController* controller = nullptr;
    ABioPawn* pawn = nullptr;
    UBioEquipment* equipment = nullptr;
    USFXGameModeDefault* mode = nullptr;
};

// The player's pawn, equipment and default game mode, live. Nullopt (with `why`) if any is missing.
std::optional<Game> ReadGame(std::string& why) {
    APlayerController* const pc = engine::LocalPlayerController();
    if (pc == nullptr || !pc->IsA(ABioPlayerController::StaticClass())) {
        why = "no player controller";
        return std::nullopt;
    }
    Game game;
    game.controller = static_cast<ABioPlayerController*>(pc);
    APawn* const pawn = pc->Pawn;
    if (pawn == nullptr || !pawn->IsA(ABioPawn::StaticClass())) {
        why = "no player pawn (vehicle or cutscene?)";
        return std::nullopt;
    }
    game.pawn = static_cast<ABioPawn*>(pawn);
    UBioPawnBehavior* const behavior = game.pawn->m_oBehavior;
    game.equipment = behavior != nullptr ? behavior->m_oEquipment : nullptr;
    if (game.equipment == nullptr) {
        why = "the player pawn has no equipment";
        return std::nullopt;
    }
    USFXGameModeManager* const manager = game.controller->GameModeManager;
    if (manager != nullptr) {
        for (int i = 0; i < static_cast<int>(manager->GameModes.Count()); ++i) {
            USFXGameModeBase* const mode = manager->GameModes.GetData()[i];
            if (mode != nullptr && mode->IsA(USFXGameModeDefault::StaticClass())) {
                game.mode = static_cast<USFXGameModeDefault*>(mode);
                break;
            }
        }
    }
    if (game.mode == nullptr) {
        why = "no SFXGameModeDefault in the player controller's game modes";
        return std::nullopt;
    }
    return game;
}

void ReadCarried(Game const& game) {
    std::array<std::uintptr_t, slots::kSlots> weapons{};
    for (int i = 0; i < slots::kSlots; ++i) {
        weapons[i] = reinterpret_cast<std::uintptr_t>(game.equipment->m_QuickSlotArray[i]);
    }
    if (weapons != gCarried.weapons) {
        gCarried.weapons = weapons;
        gCarried.typeOfSlot.fill(-1);
        gCarried.slotOfType.fill(-1);
        std::string text;
        for (int i = 0; i < slots::kSlots; ++i) {
            ABioWeapon* const weapon = game.equipment->m_QuickSlotArray[i];
            int const type = RangedType(weapon);
            gCarried.typeOfSlot[i] = type;
            if (type >= 0 && gCarried.slotOfType[type] < 0) {
                gCarried.slotOfType[type] = i;
            }
            text += std::format(" {}: {}{};", i, weapon != nullptr ? engine::FullName(weapon) : std::string("empty"),
                weapon != nullptr ? std::format(" ({})", slots::Name(static_cast<Weapon>(type))) : std::string());
        }
        bool matches = true;
        for (int i = 0; i < slots::kSlots; ++i) {
            matches = matches && (gCarried.typeOfSlot[i] < 0 || gCarried.typeOfSlot[i] == i);
        }
        log::Info("holsters: the player's quick slots:{} {}", text,
            matches ? "slot = weapon type, as research/19 expects" : "NOT slot = weapon type: drawing by the weapons' own types");
    }
    gCarried.current = game.equipment->m_eQuickSlotCurrent;
    gCarried.drawn = CallBool(game.pawn, gIsInCombatActionState);
    gCarried.switching = CallBool(game.pawn, gIsSwitchingWeapons);
}

Weapon Held() {
    if (!gCarried.drawn || gCarried.current < 0 || gCarried.current >= slots::kSlots) {
        return Weapon::None;
    }
    int const type = gCarried.typeOfSlot[gCarried.current];
    return type >= 0 ? static_cast<Weapon>(type) : Weapon::None;
}

bool Carries(Weapon weapon) {
    int const type = static_cast<int>(weapon);
    return type >= 0 && type < slots::kSlots && gCarried.slotOfType[type] >= 0;
}

void Draw(Game const& game, Weapon weapon, char const* slot, Link const& link) {
    int const quickSlot = gCarried.slotOfType[static_cast<int>(weapon)];
    USFXGameModeDefault_execSwitchToWeapon_Parms parms;
    parms.Slot = static_cast<unsigned char>(quickSlot);
    log::Info("holsters: grab at {}: drawing the {} (SFXGameModeDefault.SwitchToWeapon({}); was {}{})", slot, slots::Name(weapon),
        quickSlot, gCarried.drawn ? slots::Name(Held()) : "holstered", gCarried.switching ? ", the game is mid-switch" : "");
    game.mode->ProcessEvent(gSwitchToWeapon, &parms, nullptr);  // directly: never the SDK's wrapper
    gPending = {slots::Action::Draw, weapon, 0.0};
    ++gDraws;
    Pulse(link, kActionSeconds, kActionAmplitude);
}

void Holster(Game const& game, Weapon weapon, char const* slot, Link const& link) {
    if (!gCarried.drawn) {
        log::Info("holsters: holster at {}: the {} is already put away", slot, slots::Name(weapon));
        return;
    }
    USFXGameModeDefault_execTryHolster_Parms parms;
    log::Info("holsters: the {} goes back in {} (SFXGameModeDefault.TryHolster)", slots::Name(weapon), slot);
    game.mode->ProcessEvent(gTryHolster, &parms, nullptr);  // directly: never the SDK's wrapper
    gPending = {slots::Action::Holster, weapon, 0.0};
    ++gHolsters;
    Pulse(link, kActionSeconds, kActionAmplitude);
}

// What the game did with our last request, once.
void FollowPending(double deltaSeconds) {
    if (gPending.action == slots::Action::None) {
        return;
    }
    gPending.age += deltaSeconds;
    bool const done = gPending.action == slots::Action::Draw ? Held() == gPending.weapon : !gCarried.drawn;
    if (done) {
        log::Info("holsters: {} after {:.2f} s", gPending.action == slots::Action::Draw
            ? std::format("the {} is in hand", slots::Name(gPending.weapon))
            : std::format("the {} is holstered", slots::Name(gPending.weapon)), gPending.age);
        gPending = {};
    } else if (gPending.age > kResultSeconds) {
        log::Warn("holsters: the game didn't {} the {} within {:.0f} s (combat disabled here, the pawn busy, or mid-switch); "
                  "now {}", gPending.action == slots::Action::Draw ? "draw" : "holster", slots::Name(gPending.weapon), kResultSeconds,
            gCarried.drawn ? std::format("holding the {}", slots::Name(Held())) : std::string("holstered"));
        gPending = {};
    }
}

#endif  // SDK_TARGET_LE1

void Inactive(std::string why) {
    if (gMachine.InSlot() >= 0) {
        log::Info("holsters: right hand out of every slot ({})", why);
    }
    gMachine.Reset();
    gAnchor.Reset();
    gOwnYawActive = false;
    gYawValid = false;
    gNear = false;
    gActive = false;
    gDistances.fill(-1.0);
    SetWhy(std::move(why));
}

} // namespace

bool TryStartup() {
#if defined(SDK_TARGET_LE1)
    gSwitchToWeapon = engine::FindFunction(L"SwitchToWeapon", "Function SFXGame.SFXGameModeDefault.SwitchToWeapon");
    gTryHolster = engine::FindFunction(L"TryHolster", "Function SFXGame.SFXGameModeDefault.TryHolster");
    gIsInCombatActionState = engine::FindFunction(L"IsInCombatActionState", "Function SFXGame.BioPawn.IsInCombatActionState");
    gIsSwitchingWeapons = engine::FindFunction(L"IsSwitchingWeapons", "Function SFXGame.BioPawn.IsSwitchingWeapons");
    gGetRangedWeaponType = engine::FindFunction(L"GetRangedWeaponType", "Function SFXGame.BioWeaponRanged.GetRangedWeaponType");
    gStarted = gSwitchToWeapon != nullptr && gTryHolster != nullptr && gIsInCombatActionState != nullptr &&
        gIsSwitchingWeapons != nullptr && gGetRangedWeaponType != nullptr;
    log::Info("holsters: script functions {} (SwitchToWeapon {}, TryHolster {}, IsInCombatActionState {}, IsSwitchingWeapons {}, "
              "GetRangedWeaponType {})", gStarted ? "found" : "MISSING: holsters off", gSwitchToWeapon != nullptr,
        gTryHolster != nullptr, gIsInCombatActionState != nullptr, gIsSwitchingWeapons != nullptr, gGetRangedWeaponType != nullptr);
    return gStarted;
#else
    return false;
#endif
}

void Tick(double deltaSeconds, Link const& link) {
    hand_calibration::Refresh();  // at most once a second
    hand_calibration::Holsters const settings = hand_calibration::GetHolsters();
    gEnabled = settings.enabled;
    gWheelOff = settings.enabled && !settings.weaponWheel;

    vr::controllers::State const controllers = vr::controllers::Latest();
    vr::controllers::Hand const& right = controllers.right;
    // Our own latch of the right grip, kept whatever else happens, so a squeeze held through a
    // pause or a menu is never taken for a new one.
    bool const wasDown = gOwnGrip;
    gOwnGrip = controllers.active && (gOwnGrip ? right.squeeze > kGripOff : right.squeeze >= kGripOn);
    // A new squeeze the game didn't see as RB: the host counts them; an older host can't, so then
    // any new squeeze (and RB reaches the game).
    bool newSqueeze = false;
    if (link.rightGrip) {
        std::uint32_t const count = *link.rightGrip >> 1;
        newSqueeze = gHaveCount && count != gLastCount;
        gLastCount = count;
        gHaveCount = true;
    } else {
        newSqueeze = gOwnGrip && !wasDown;
        if (!gToldOldHost && settings.enabled) {
            gToldOldHost = true;
            log::Warn("holsters: the host (services older than v9) can't keep a holster's squeeze from the game: grabs still "
                      "work, but the game sees RB too; close the game and redeploy the host");
        }
    }

    if (!settings.enabled) {
        Inactive("off ([Holsters] enabled = 0)");
        return;
    }
#if defined(SDK_TARGET_LE1)
    if (!gStarted) {
        Inactive("waiting for the script functions");
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
    if (!controllers.active || !right.gripValid) {
        Inactive("the right controller isn't tracked");
        return;
    }
    std::string why;
    auto const game = ReadGame(why);
    if (!game) {
        Inactive(why);
        return;
    }
    ReadCarried(*game);
    gActive = true;
    SetWhy("active");

    // The body frame. The yaw: arm IK's torso, else our own lagging yaw; held while busy.
    XrPosef const& head = view.head;
    ik::Quat const headRotation{head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w};
    ik::Vec3 const headPosition{head.position.x, head.position.y, head.position.z};
    double const headYaw = ik::YawOf(headRotation.x, headRotation.y, headRotation.z, headRotation.w);
    if (!gOwnYawActive) {
        gOwnYaw.Reset(headYaw);
        gOwnYawActive = true;
    }
    double const ownYaw = gOwnYaw.Update(headYaw, deltaSeconds);
    std::optional<double> const torso = arm_ik::TorsoYaw();
    if (!gYawValid || !gMachine.Busy()) {
        gYaw = torso ? *torso : ownYaw;
        gYawSource = torso ? "arm IK's torso" : "the head (lagging)";
        gYawValid = true;
    }
    gFrame.yaw = gYaw;
    gFrame.anchor = gAnchor.Update(slots::NeutralEye(headPosition, headRotation), deltaSeconds);

    double const scale = slots::Scale(settings.layout);
    ik::Vec3 const hand{right.grip.position.x, right.grip.position.y, right.grip.position.z};
    slots::Input input;
    input.exitMargin = settings.layout.exitMargin;
    for (int i = 0; i < slots::kSlots; ++i) {
        slots::Slot const& slot = settings.layout.slots[i];
        gCenters[i] = slots::SlotCenter(gFrame, slot, scale);
        input.weapon[i] = slot.weapon;
        input.radius[i] = slot.radius;
        input.available[i] = Carries(slot.weapon);
        input.distance[i] = slot.weapon != Weapon::None ? ik::Length(hand - gCenters[i]) : -1.0;
        gDistances[i] = input.distance[i];
        if (input.distance[i] >= 0.0 && input.distance[i] - slot.radius < gNearest) {
            gNearest = input.distance[i] - slot.radius;
            gNearestSlot = i;
        }
    }
    input.tracked = true;
    input.gripDown = gOwnGrip;
    input.newSqueeze = newSqueeze;
    input.trigger = right.trigger >= kTriggerOn;
    input.held = Held();
    input.holster = settings.holster;
    slots::Step const step = gMachine.Update(input);
    gNear = step.nearby;

    auto const name = [&settings](int slot) { return slot >= 0 ? settings.layout.slots[slot].name : "-"; };
    if (step.left >= 0) {
        log::Info("holsters: right hand left {} ({:.2f} m from its center)", name(step.left), input.distance[step.left]);
    }
    if (step.entered >= 0) {
        ++gEntries;
        log::Info("holsters: right hand entered {} ({}, {:.2f} m from its center, radius {:.2f}){}", name(step.entered),
            slots::Name(settings.layout.slots[step.entered].weapon), input.distance[step.entered],
            settings.layout.slots[step.entered].radius, Carries(settings.layout.slots[step.entered].weapon) ? "" : ": you don't carry one");
        Pulse(link, kEnterSeconds, static_cast<float>(settings.hapticAmplitude));
    }
    switch (step.action) {
    case slots::Action::Draw:
        Draw(*game, step.weapon, name(step.slot), link);
        break;
    case slots::Action::Holster:
        Holster(*game, step.weapon, name(step.slot), link);
        break;
    case slots::Action::Unavailable:
        ++gRefusals;
        log::Info("holsters: grab at {}: you carry no {} (its quick slot is empty)", name(step.slot), slots::Name(step.weapon));
        Pulse(link, kRefusedSeconds, kRefusedAmplitude);
        break;
    case slots::Action::AlreadyHeld:
        log::Info("holsters: grab at {}: the {} is already in hand (holster = 0: it stays)", name(step.slot), slots::Name(step.weapon));
        break;
    case slots::Action::HolsterArmed:
        log::Info("holsters: squeezed at {} with the {} in hand: release here to holster it", name(step.slot), slots::Name(step.weapon));
        break;
    case slots::Action::HolsterCancelled:
        log::Info("holsters: holster at {} called off ({})", name(step.slot),
            input.trigger ? "the trigger" : gMachine.InSlot() != step.slot ? "the hand left the slot" : "the gun changed");
        break;
    case slots::Action::Missed:
        log::Info("holsters: squeeze near a slot but in none (nearest {} at {:.2f} m): nothing drawn, no RB", name(gNearestSlot),
            gNearestSlot >= 0 ? input.distance[gNearestSlot] : -1.0);
        break;
    case slots::Action::None:
        break;
    }
    FollowPending(deltaSeconds);
#else
    (void)deltaSeconds;
    (void)newSqueeze;
    Inactive("LE1 only");
#endif
}

void AdjustPad(gamepad::Options& options) {
    if (!gEnabled) {
        return;
    }
    options.rightGripFree = gActive && gNear;
    if (gWheelOff) {
        options.leftTriggerShoulder = false;
        options.gripShoulder = false;
    }
}

std::string StatusJson() {
    namespace json = vr::test_request;
    hand_calibration::Holsters const settings = hand_calibration::GetHolsters();
    std::string list;
    for (int i = 0; i < slots::kSlots; ++i) {
        slots::Slot const& slot = settings.layout.slots[i];
        int const type = static_cast<int>(slot.weapon);
        bool const carried = type >= 0 && type < slots::kSlots && gCarried.slotOfType[type] >= 0;
        list += std::format("{}{{\"name\": {}, \"weapon\": {}, \"center\": [{}, {}, {}], \"radius\": {}, \"distance\": {}, "
                            "\"carried\": {}}}", i > 0 ? ", " : "", json::JsonString(slot.name), json::JsonString(slots::Name(slot.weapon)),
            json::JsonNumber(gCenters[i].x), json::JsonNumber(gCenters[i].y), json::JsonNumber(gCenters[i].z),
            json::JsonNumber(slot.radius), json::JsonNumber(gDistances[i]), carried ? "true" : "false");
    }
    char const* phase = "idle";
    switch (gMachine.CurrentPhase()) {
    case slots::Phase::Idle: phase = "idle"; break;
    case slots::Phase::Grab: phase = "grab"; break;
    case slots::Phase::HolsterArmed: phase = "holster armed"; break;
    }
    return std::format("{{\"enabled\": {}, \"active\": {}, \"why\": {}, \"inSlot\": {}, \"phase\": {}, \"near\": {}, "
                       "\"yawSource\": {}, \"yaw\": {}, \"anchor\": [{}, {}, {}], \"held\": {}, \"drawn\": {}, \"slots\": [{}]}}",
        gEnabled ? "true" : "false", gActive ? "true" : "false", json::JsonString(gWhy),
        gMachine.InSlot() >= 0 ? json::JsonString(settings.layout.slots[gMachine.InSlot()].name) : std::string("null"),
        json::JsonString(phase), gNear ? "true" : "false", json::JsonString(gYawSource), json::JsonNumber(gFrame.yaw),
        json::JsonNumber(gFrame.anchor.x), json::JsonNumber(gFrame.anchor.y), json::JsonNumber(gFrame.anchor.z),
#if defined(SDK_TARGET_LE1)
        json::JsonString(slots::Name(Held())),
#else
        json::JsonString("none"),
#endif
        gCarried.drawn ? "true" : "false", list);
}

void LogStats() {
    if (!gEnabled && gEntries == 0) {
        return;
    }
    hand_calibration::Holsters const settings = hand_calibration::GetHolsters();
    std::string where;
    if (gActive) {
        for (int i = 0; i < slots::kSlots; ++i) {
            where += std::format(" {} {} hand {:.2f} m;", settings.layout.slots[i].name, V(gCenters[i] - gFrame.anchor), gDistances[i]);
        }
    }
    log::Info("    holsters: {}; yaw from {}; in {}; this period: {} entries, {} draws, {} holsters, {} refusals, nearest {}{}",
        gWhy, gYawSource, gMachine.InSlot() >= 0 ? settings.layout.slots[gMachine.InSlot()].name : "no slot", gEntries, gDraws,
        gHolsters, gRefusals,
        gNearestSlot >= 0 ? std::format("{:.2f} m outside {}", gNearest, settings.layout.slots[gNearestSlot].name) : std::string("-"),
        where.empty() ? std::string() : std::format("; slots from the anchor:{}", where));
    gEntries = gDraws = gHolsters = gRefusals = 0;
    gNearest = 1e9;
    gNearestSlot = -1;
}

void Shutdown() {
    gMachine.Reset();
    gAnchor.Reset();
    gOwnYawActive = false;
    gYawValid = false;
    gNear = false;
    gActive = false;
    gEnabled = false;
    gWheelOff = false;
    gOwnGrip = false;
    gHaveCount = false;
    gToldOldHost = false;
    gToldNoHaptics = false;
    gCarried = {};
    gPending = {};
    gStarted = false;
    gWhy = "not started";
    gLoggedWhy.clear();
#if defined(SDK_TARGET_LE1)
    gSwitchToWeapon = gTryHolster = gIsInCombatActionState = gIsSwitchingWeapons = gGetRangedWeaponType = nullptr;
#endif
}

} // namespace mevr::game::holsters
