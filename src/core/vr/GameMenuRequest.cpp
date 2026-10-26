// SPDX-License-Identifier: MIT
#include "core/vr/GameMenuRequest.hpp"

namespace mevr::vr::game_menu {

char const* Name(Action action) {
    switch (action) {
    case Action::None: return "none";
    case Action::PauseMenu: return "the game's pause menu";
    case Action::Holster: return "holster";
    }
    return "none";
}

Action FromValue(std::uint32_t value) {
    switch (value) {
    case static_cast<std::uint32_t>(Action::PauseMenu): return Action::PauseMenu;
    case static_cast<std::uint32_t>(Action::Holster): return Action::Holster;
    default: return Action::None;
    }
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
