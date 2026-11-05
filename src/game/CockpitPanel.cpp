// SPDX-License-Identifier: MIT
#include "game/CockpitPanel.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "game/CockpitProbe.hpp"
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

void Repair() {
    ++gRepairs;
    CallGame(gRepairVehicle, "RepairVehicle", "repair");
}

void Extract() {
    ++gExtractions;
    CallGame(gLeaveWorld, "LeaveWorld", "extraction");
}
#else
void Repair() {
    gLast = "repair: LE1 only";
}
void Extract() {
    gLast = "extraction: LE1 only";
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
    int const squares = cp::ProgressLamps(gOut.progress, cp::kProgressLamps);
    for (int i = 0; i < squares; ++i) {
        want[static_cast<std::size_t>(cp::kHealthLamps + i)] = kProgressColour;
    }
    cockpit_probe::ShowPanelLamps(want.data(), static_cast<int>(want.size()));
    gLamps = want;
}

}  // namespace

void Tick(double deltaSeconds, Link const& link) {
    cp::Input in;
    in.deltaSeconds = deltaSeconds;
    std::optional<cp::Layout> const layout = link.mainMenuOpen ? std::nullopt : cockpit_probe::PanelLayout();
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
    gOut = gPanel.Step(in, gSettings);
    if (in.active != gWasActive) {
        gWasActive = in.active;
        log::Info("cockpit panel: {}", in.active ? "the repair button and the extraction lever are in reach" : "the controls are out of use");
    }
    if (gOut.repair) {
        Repair();
    }
    if (gOut.extract) {
        Extract();
    }
    if (link.hapticPulse != nullptr) {
        for (int i = 0; i < gOut.hapticCount; ++i) {
            link.hapticPulse(gOut.haptics[i].hand, gOut.haptics[i].seconds, gOut.haptics[i].amplitude);
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
    return std::format("{{\"active\": {}, \"pressM\": {}, \"pressed\": {}, \"progress\": {}, \"pullDeg\": {}, \"held\": {}, \"repairs\": {}, "
                       "\"extractions\": {}, \"lamps\": [{}], \"last\": {}}}",
        gWasActive ? "true" : "false", json::JsonNumber(gOut.pressM), gOut.pressed ? "true" : "false", json::JsonNumber(gOut.progress),
        json::JsonNumber(gOut.pullDeg), gOut.held ? "true" : "false", gRepairs, gExtractions, lamps, json::JsonString(gLast));
}

std::string Command(std::string const& rest) {
    if (rest == "repair") {
        Repair();
        return "cockpit panel: " + gLast;
    }
    if (rest == "extract") {
        Extract();
        return "cockpit panel: " + gLast;
    }
    return "cockpit panel: " + StatusJson();
}

}  // namespace mevr::game::cockpit_panel
