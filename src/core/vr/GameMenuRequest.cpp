// SPDX-License-Identifier: MIT
#include "core/vr/GameMenuRequest.hpp"

namespace mevr::vr::game_menu {

char const* Name(Action action) {
    switch (action) {
    case Action::None: return "none";
    case Action::PauseMenu: return "the game's pause menu";
    case Action::Holster: return "holster";
    case Action::Equipment: return "the equipment screen";
    case Action::Squad: return "the squad screen";
    case Action::Journal: return "the journal";
    case Action::Codex: return "the codex";
    case Action::Map: return "the map";
    case Action::CloseScreens: return "close the game's screens";
    }
    return "none";
}

bool IsScreen(Action action) {
    return ShowFunction(action) != nullptr;
}

char const* ShowFunction(Action action) {
    switch (action) {
    case Action::Equipment: return "ShowEquipmentGui";
    case Action::Squad: return "ShowSquadGui";
    case Action::Journal: return "ShowJournalGui";
    case Action::Codex: return "ShowCodexGui";
    case Action::Map: return "ShowMapGui";
    default: return nullptr;
    }
}

Action FromValue(std::uint32_t value) {
    if (value >= static_cast<std::uint32_t>(Action::PauseMenu) && value <= static_cast<std::uint32_t>(Action::CloseScreens)) {
        return static_cast<Action>(value);
    }
    return Action::None;
}

std::uint32_t Encode(Action action, Origin origin) {
    return static_cast<std::uint32_t>(action) | (static_cast<std::uint32_t>(origin) << 8);
}

Action DecodeAction(std::uint32_t value) {
    return (value & ~0x1FFu) == 0 ? FromValue(value & 0xFFu) : Action::None;
}

Origin DecodeOrigin(std::uint32_t value) {
    return DecodeAction(value) != Action::None && (value & 0x100u) != 0 ? Origin::OmniRail : Origin::Menu;
}

Action Slot::Put(Action action, Origin origin) {
    return DecodeAction(value_.exchange(Encode(action, origin), std::memory_order_acq_rel));
}

Action Slot::Take() {
    return DecodeAction(TakeValue());
}

std::uint32_t Slot::TakeValue() {
    polled_.store(true, std::memory_order_relaxed);
    if (value_.load(std::memory_order_relaxed) == 0) {
        return 0;  // the usual case, every tick: no write
    }
    return value_.exchange(0, std::memory_order_acq_rel);
}

void Take(Schedule& schedule, Action action, Origin origin) {
    if (action == Action::None) {
        return;
    }
    schedule = {action, 0, 0, origin};
}

Step Advance(Schedule& schedule, bool menuOpen, bool pausedByMenu) {
    Step step;
    step.origin = schedule.origin;
    if (schedule.action == Action::None) {
        return step;
    }
    ++schedule.age;
    if (menuOpen) {
        step.dropped = schedule.action;
        step.reopened = true;
        schedule = {};
        return step;
    }
    schedule.settled = pausedByMenu ? 0 : schedule.settled + 1;
    if (schedule.settled >= kSettleTicks) {
        step.run = schedule.action;
        schedule = {};
        return step;
    }
    if (schedule.age >= kGiveUpTicks) {
        step.dropped = schedule.action;
        schedule = {};
    }
    return step;
}

} // namespace mevr::vr::game_menu
