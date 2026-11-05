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

Action Slot::Put(Action action) {
    return FromValue(value_.exchange(static_cast<std::uint32_t>(action), std::memory_order_acq_rel));
}

Action Slot::Take() {
    polled_.store(true, std::memory_order_relaxed);
    if (value_.load(std::memory_order_relaxed) == 0) {
        return Action::None;  // the usual case, every tick: no write
    }
    return FromValue(value_.exchange(0, std::memory_order_acq_rel));
}

void Take(Schedule& schedule, Action action) {
    if (action == Action::None) {
        return;
    }
    schedule = {action, 0, 0};
}

Step Advance(Schedule& schedule, bool menuOpen, bool pausedByMenu) {
    Step step;
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
