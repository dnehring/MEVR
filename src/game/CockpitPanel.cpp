// SPDX-License-Identifier: MIT
#include "game/CockpitPanel.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "game/CockpitProbe.hpp"
#include "game/MakoControls.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <array>
#include <format>

namespace mevr::game::cockpit_panel {
namespace {

namespace cp = vr::cockpit_panel;

cp::Panel gPanel;
cp::Output gOut;
std::optional<cp::Layout> gLayout;  // the last tick's, in tracking space (the snapshot's, for replays that reach to it)
cp::Settings const gSettings;
unsigned gRepairs = 0;
unsigned gExtractions = 0;
bool gWasActive = false;
std::string gLast = "none";
// Each lamp's colour as last asked for: the health ring's nine, then the plate's eight squares.
std::array<cp::Colour, cp::kHealthLamps + cp::kProgressLamps> gLamps{};
constexpr cp::Colour kProgressColour = cp::Colour::Blue;
// The index fingertip ahead of the aim pose along it (the pointing finger over a Touch controller's ring), as the game
// pointer's touch (plugin/GamePointer, touchTipM).
constexpr float kTipM = 0.04f;
// The vehicle's VehicleRepair power as last asked (AskRepair): whether the repair can run.
struct RepairPower {
    ue3::Object* behavior = nullptr;  // the cache's key
    ue3::Object* power = nullptr;
    bool known = false;               // the power answered
    bool gel = true;                  // its IsRequiredResourceAvailable: the omni-gel it costs is there
    float cooldownS = 0.0f;           // its m_fCooldownRemaining
    std::string why = "not asked yet";
};
RepairPower gRepairPower;
unsigned gRefusals = 0;
// The repair on the throttle: the left thumbstick's click while the left hand holds the lever (core/vr/CockpitPanel).
cp::ThrottleClick gThrottleClick;
cp::ClickOutput gClick;
unsigned gThrottleRepairs = 0;
unsigned gClickRefusals = 0;
int gSimulatedClickTicks = 0;  // `mevr cockpit panel throttle`: a click on the held lever for these ticks, without controllers

XrVector3f AimTip(XrPosef const& aim) {
    XrQuaternionf const q = aim.orientation;
    // the aim's -Z (forward) turned by q
    XrVector3f const f{-2.0f * (q.x * q.z + q.w * q.y), -2.0f * (q.y * q.z - q.w * q.x), -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))};
    return {aim.position.x + f.x * kTipM, aim.position.y + f.y * kTipM, aim.position.z + f.z * kTipM};
}

#if defined(MEVR_GAME_LE1)
// Our reflection layer (research/34); engine/ReflectCheck has a row for each.
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes"};
ue3::ClassRef gModeVehicle{"SFXGame.SFXGameModeVehicle"};
ue3::FunctionRef gRepairVehicle{"SFXGame.SFXGameModeVehicle.RepairVehicle"};
ue3::FunctionRef gLeaveWorld{"SFXGame.SFXGameModeVehicle.LeaveWorld"};
// RepairVehicle runs the vehicle behaviour's VehicleRepair power (SetCurrentPower('VehicleRepair'), research/48 section 27):
// whether it can run now is that power's own answer.
ue3::ClassRef gVehicleBase{"SFXGame.BioVehicleBase"};
ue3::FieldRef gVehicleBehavior{"SFXGame.BioVehicleBase", "m_oBehavior"};
ue3::FieldRef gPowers{"SFXGame.BioEpicPawnBehavior", "m_powers"};
ue3::FieldRef gPowerName{"SFXGame.BioPower", "m_nmPower"};
ue3::FieldRef gCooldownRemaining{"SFXGame.BioPower", "m_fCooldownRemaining"};
ue3::FunctionRef gResourceAvailable{"SFXGame.BioPower.IsRequiredResourceAvailable"};
constexpr char const* kRepairPower = "VehicleRepair";

// The player's vehicle's VehicleRepair power, or null (`why` says what's missing).
ue3::Object* FindRepairPower(std::string& why) {
    if (!ue3::runtime::Ready()) {
        why = "our reflection layer is off";
        return nullptr;
    }
    ue3::Object* const pc = player::Controller();
    ue3::Object* const pawn = pc != nullptr ? player::Pawn(pc) : nullptr;
    if (pawn == nullptr || !ue3::IsA(pawn, gVehicleBase)) {
        why = "the player's pawn isn't a vehicle";
        return nullptr;
    }
    ue3::Object* const behavior = gVehicleBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
    if (behavior == nullptr || !ue3::runtime::IsAlive(behavior)) {
        why = "the vehicle has no behaviour";
        return nullptr;
    }
    if (behavior == gRepairPower.behavior && gRepairPower.power != nullptr && ue3::runtime::IsAlive(gRepairPower.power)) {
        return gRepairPower.power;
    }
    gRepairPower.behavior = behavior;
    gRepairPower.power = nullptr;
    auto const list = gPowers.ElementsOf(behavior);
    for (std::int32_t i = 0; list && i < list->count && i < 64; ++i) {
        void* const slot = list->At(i);
        ue3::Object* const power = slot != nullptr ? *static_cast<ue3::Object**>(slot) : nullptr;
        if (power != nullptr && ue3::runtime::IsAlive(power) &&
            ue3::runtime::NameText(gPowerName.Read<ue3::Name>(power).value_or(ue3::Name{})) == kRepairPower) {
            gRepairPower.power = power;
            log::Info("cockpit panel: the vehicle's {} power is {}", kRepairPower, ue3::runtime::FullName(power));
            return power;
        }
    }
    why = std::format("the vehicle's behaviour has no {} power in m_powers", kRepairPower);
    return nullptr;
}

// Why the repair can't run now, asked while the button is pushed (game thread). Unknown: none (the press goes ahead).
cp::Refusal AskRepair() {
    std::string why;
    ue3::Object* const power = FindRepairPower(why);
    ue3::Function* const fn = gResourceAvailable.Get();
    if (power == nullptr || fn == nullptr) {
        gRepairPower.known = false;
        gRepairPower.why = power == nullptr ? why : "SFXGame.BioPower.IsRequiredResourceAvailable not found";
        return cp::Refusal::None;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    ue3::runtime::Call(power, fn, parms);  // a native: runtime::Call clears its index for the call
    gRepairPower.known = true;
    gRepairPower.gel = parms.ReturnBool().value_or(true);
    gRepairPower.cooldownS = std::max(0.0f, gCooldownRemaining.Read<float>(power).value_or(0.0f));
    gRepairPower.why = "asked";
    return !gRepairPower.gel ? cp::Refusal::NoGel : gRepairPower.cooldownS > 0.0f ? cp::Refusal::Cooling : cp::Refusal::None;
}

// The player's vehicle mode: Y and X run these on it in the Mako (BIOInput.ini's [SFXGame.SFXGameModeVehicle], research/46).
ue3::Object* VehicleMode(std::string& why) {
    if (!ue3::runtime::Ready()) {
        why = "our reflection layer is off";
        return nullptr;
    }
    ue3::Object* const pc = player::Controller();
    if (pc == nullptr || !player::IsBioPlayerController(pc)) {
        why = "no player controller";
        return nullptr;
    }
    ue3::Object* const manager = gGameModeManager.Read<ue3::Object*>(pc).value_or(nullptr);
    auto const* const modes = manager != nullptr ? gGameModes.In<ue3::TArray<ue3::Object*>>(manager) : nullptr;
    if (modes == nullptr) {
        why = "the player controller has no game modes";
        return nullptr;
    }
    for (int i = 0; i < modes->Count(); ++i) {
        if (ue3::Object* const mode = modes->data[i]; mode != nullptr && ue3::IsA(mode, gModeVehicle)) {
            return mode;
        }
    }
    why = "no SFXGameModeVehicle in the player controller's game modes";
    return nullptr;
}

// The game's own function on the vehicle mode (game thread), looked up on the mode's class for an override.
void CallGame(ue3::FunctionRef& declared, char const* name, char const* what) {
    std::string why;
    ue3::Object* const mode = VehicleMode(why);
    if (mode != nullptr && declared.Get() == nullptr) {
        why = std::format("SFXGame.SFXGameModeVehicle.{} not found", name);
    }
    if (!why.empty()) {
        gLast = std::format("{}: not done ({})", what, why);
        log::Warn("cockpit panel: the {} wasn't called: {}", what, why);
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const function = r.FindFunction(r.ClassOf(mode), name);
    ue3::Function* const call = function != nullptr ? function : declared.Get();
    ue3::Params parms(r, call);
    ue3::runtime::Call(mode, call, parms);  // through ProcessEvent: never a generated wrapper
    gLast = std::format("{}: {}.{} called", what, r.NameText(r.OuterOf(call)), name);
    log::Info("cockpit panel: the {}: {}.{} called on the player's vehicle mode", what, r.NameText(r.OuterOf(call)), name);
}

void Repair(char const* what) {
    ++gRepairs;
    CallGame(gRepairVehicle, "RepairVehicle", what);
}

void Extract() {
    ++gExtractions;
    CallGame(gLeaveWorld, "LeaveWorld", "extraction");
}
#else
void Repair(char const* what) {
    gLast = std::format("{}: LE1 only", what);
}
void Extract() {
    gLast = "extraction: LE1 only";
}
cp::Refusal AskRepair() {
    return cp::Refusal::None;
}
#endif

// The lamps as the hull and the hold say (game/CockpitProbe swaps only what changed on the model).
void ShowLamps(bool riding) {
    std::array<cp::Colour, cp::kHealthLamps + cp::kProgressLamps> want{};
    std::optional<float> const health = riding ? cockpit_probe::HullHealth() : std::nullopt;
    if (health) {
        int const lit = cp::HealthLamps(*health, cp::kHealthLamps);
        for (int i = 0; i < lit; ++i) {
            want[static_cast<std::size_t>(i)] = cp::HealthColour(*health);
        }
    }
    // The plate's squares: the hold's progress, or all red while the repair can't run (solid: no omni-gel; flashing: cooling down),
    // for a press of the button or a click on the throttle.
    bool const buttonRefused = gOut.refused != cp::Refusal::None;
    bool const refused = buttonRefused || gClick.refused != cp::Refusal::None;
    bool const refusedLit = buttonRefused ? gOut.refusedLit : gClick.refusedLit;
    int const squares = refused ? (refusedLit ? cp::kProgressLamps : 0) : cp::ProgressLamps(gOut.progress, cp::kProgressLamps);
    cp::Colour const colour = refused ? cp::Colour::Red : kProgressColour;
    for (int i = 0; i < squares; ++i) {
        want[static_cast<std::size_t>(cp::kHealthLamps + i)] = colour;
    }
    cockpit_probe::ShowPanelLamps(want.data(), static_cast<int>(want.size()));
    gLamps = want;
}

}  // namespace

void Tick(double deltaSeconds, Link const& link) {
    cp::Input in;
    in.deltaSeconds = deltaSeconds;
    std::optional<cp::Layout> const layout = link.mainMenuOpen ? std::nullopt : cockpit_probe::PanelLayout();
    gLayout = layout;
    vr::controllers::State const controllers = vr::controllers::Latest();
    in.active = layout.has_value() && controllers.active;
    if (layout) {
        in.layout = *layout;
    }
    for (int h = 0; h < 2; ++h) {
        vr::controllers::Hand const& hand = h == 0 ? controllers.left : controllers.right;
        in.hands[h].tracked = controllers.active && hand.gripValid && hand.aimValid;
        in.hands[h].grip = hand.grip.position;
        in.hands[h].tip = AimTip(hand.aim);
        in.hands[h].squeeze = hand.squeeze;
    }
    // Asked only while a fingertip pushes the cap (last tick's), so a press finds the answer waiting.
    in.refusal = in.active && gOut.pressHand >= 0 ? AskRepair() : cp::Refusal::None;
    cp::Refusal const refusedBefore = gOut.refused;
    gOut = gPanel.Step(in, gSettings);
    if (gOut.refused != refusedBefore && gOut.refused != cp::Refusal::None) {
        ++gRefusals;
        log::Info("cockpit panel: the repair can't run: {} (the VehicleRepair power's cooldown {:.1f} s): the squares show red",
            cp::Name(gOut.refused), gRepairPower.cooldownS);
    }
    if (in.active != gWasActive) {
        gWasActive = in.active;
        log::Info("cockpit panel: {}", in.active ? "the repair button and the extraction lever are in reach" : "the controls are out of use");
    }
    if (gOut.repair) {
        Repair("repair");
    }
    if (gOut.extract) {
        Extract();
    }

    // The repair on the throttle (the user, 2026-11-05): the left thumbstick's click while the left hand holds the lever, with the
    // cockpit up or not. The power is asked while the click is down on the lever, so its first tick finds the answer.
    cp::ClickInput click;
    click.deltaSeconds = deltaSeconds;
    click.leverHeld = !link.mainMenuOpen && controllers.active && mako_controls::CurrentPose().leverHeld;
    click.click = controllers.active && controllers.left.stickClick;
    if (gSimulatedClickTicks > 0) {
        --gSimulatedClickTicks;
        click.leverHeld = click.click = true;
    }
    click.refusal = click.leverHeld && click.click ? AskRepair() : cp::Refusal::None;
    cp::Refusal const clickBefore = gClick.refused;
    gClick = gThrottleClick.Step(click, gSettings);
    if (gClick.refused != cp::Refusal::None && gClick.refused != clickBefore) {
        ++gClickRefusals;
        log::Info("cockpit panel: the throttle's click: the repair can't run: {} (the VehicleRepair power's cooldown {:.1f} s)",
            cp::Name(gClick.refused), gRepairPower.cooldownS);
    }
    if (gClick.repair) {
        ++gThrottleRepairs;
        Repair("repair (the throttle's click)");
    }

    if (link.hapticPulse != nullptr) {
        for (int i = 0; i < gOut.hapticCount; ++i) {
            link.hapticPulse(gOut.haptics[i].hand, gOut.haptics[i].seconds, gOut.haptics[i].amplitude);
        }
        for (int i = 0; i < gClick.hapticCount; ++i) {
            link.hapticPulse(gClick.haptics[i].hand, gClick.haptics[i].seconds, gClick.haptics[i].amplitude);
        }
    }
    if (layout) {
        ShowLamps(cockpit_probe::Riding());
    }
}

void AdjustPad(gamepad::Options& options) {
    if (gOut.inReach[1]) {
        options.rightGripFree = true;  // a new squeeze here takes the handle, it isn't the cannon's RB
    }
    if (gOut.inReach[0]) {
        options.gripShoulder = false;  // and never LB
    }
}

vr::cockpit_panel::Output const& Current() {
    return gOut;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    std::string lamps;
    for (cp::Colour const colour : gLamps) {
        lamps += std::format("{}{}", lamps.empty() ? "" : ", ", json::JsonString(cp::Name(colour)));
    }
    // Where the controls are, in tracking space (metres; null when not riding): a recording that reaches for them is a few
    // lines from these (the fake headset's head sits at the tracking origin).
    std::string layout = "null";
    if (gLayout) {
        auto const v = [&](XrVector3f const& p) {
            return std::format("[{}, {}, {}]", json::JsonNumber(p.x), json::JsonNumber(p.y), json::JsonNumber(p.z));
        };
        layout = std::format("{{\"buttonTop\": {}, \"buttonIn\": {}, \"travelM\": {}, \"hinge\": {}, \"hingeAxis\": {}, \"grip\": {}, "
                             "\"pullDeg\": {}}}",
            v(gLayout->buttonTop), v(gLayout->buttonIn), json::JsonNumber(gLayout->travelM), v(gLayout->hinge), v(gLayout->hingeAxis),
            v(gLayout->grip), json::JsonNumber(gLayout->pullDeg));
    }
    std::string const power = std::format("{{\"known\": {}, \"gel\": {}, \"cooldownS\": {}, \"why\": {}}}", gRepairPower.known ? "true" : "false",
        gRepairPower.gel ? "true" : "false", json::JsonNumber(gRepairPower.cooldownS), json::JsonString(gRepairPower.why));
    std::string const throttle = std::format("{{\"refused\": {}, \"refusedLit\": {}, \"refusals\": {}, \"repairs\": {}}}",
        json::JsonString(cp::Name(gClick.refused)), gClick.refusedLit ? "true" : "false", gClickRefusals, gThrottleRepairs);
    return std::format("{{\"active\": {}, \"pressM\": {}, \"pressed\": {}, \"progress\": {}, \"refused\": {}, \"refusedLit\": {}, \"refusals\": {}, "
                       "\"repairPower\": {}, \"throttleClick\": {}, \"pullDeg\": {}, \"held\": {}, \"repairs\": {}, \"extractions\": {}, "
                       "\"lamps\": [{}], \"last\": {}, \"layout\": {}}}",
        gWasActive ? "true" : "false", json::JsonNumber(gOut.pressM), gOut.pressed ? "true" : "false", json::JsonNumber(gOut.progress),
        json::JsonString(cp::Name(gOut.refused)), gOut.refusedLit ? "true" : "false", gRefusals, power, throttle, json::JsonNumber(gOut.pullDeg),
        gOut.held ? "true" : "false", gRepairs, gExtractions, lamps, json::JsonString(gLast), layout);
}

std::string Command(std::string const& rest) {
    if (rest == "repair") {
        Repair("repair");
        return "cockpit panel: " + gLast;
    }
    if (rest == "throttle") {  // a click on the held throttle lever over the next two ticks, as the left thumb would make it
        gSimulatedClickTicks = 2;
        return "cockpit panel: the throttle's click goes down for the next two ticks (repair, or the refusal on the squares)";
    }
    if (rest == "extract") {
        Extract();
        return "cockpit panel: " + gLast;
    }
    if (rest == "ask") {  // what a press would show now, without tracked controllers
        cp::Refusal const refusal = AskRepair();
        return std::format("cockpit panel: a press now: {} (the power {}; omni-gel {}, cooldown {:.1f} s)", cp::Name(refusal), gRepairPower.why,
            gRepairPower.gel ? "there" : "missing", gRepairPower.cooldownS);
    }
    return "cockpit panel: " + StatusJson();
}

}  // namespace mevr::game::cockpit_panel
