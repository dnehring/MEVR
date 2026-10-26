// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>

// Our main menu's Game menu and Holster buttons (research/28 "The game's menus", HostServices v19): the host's menu
// asks, the mod does it on the game thread by calling the game's own functions (the active game mode's ShowMenuGui:
// the pause menu; SFXGameModeDefault.TryHolster). The game-independent parts, unit-tested in tests/Main.cpp: the
// hand-off between the threads (Slot) and when the mod runs a request it took (Schedule).
namespace mevr::vr::game_menu {

// What the menu asked for (HostServices::takeGameMenuRequest's value).
enum class Action : std::uint32_t {
    None = 0,
    PauseMenu = 1,  // the game's pause menu (Start: Console_Menu = ShowMenuGui)
    Holster = 2,    // put the weapon away (Back: Console_Holster = TryHolster), only while one is drawn
};
char const* Name(Action action);
// Values from another module (an older or newer host): anything unknown is None.
Action FromValue(std::uint32_t value);

// The host's side: the menu's button stores a request (any thread), the mod takes it once (HostServices v19). A newer
// request replaces one not yet taken. Whether anything has ever asked (Taken() called) tells the host a v19 mod runs.
class Slot {
public:
    // Returns the request it replaced (None if none was waiting).
    Action Put(Action action);
    // The waiting request, cleared (None if none).
    Action Take();
    // A mod has polled at least once.
    bool Polled() const { return polled_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> value_{0};
    std::atomic<bool> polled_{false};
};

// The mod's side, one step per game tick: a request taken from the host waits until our menu is closed and the
// menu's pause is lifted (game/MenuPause unpauses on the tick it sees the menu closed) for kSettleTicks ticks in a
// row, then runs once. Our menu opening again calls it off; so does waiting longer than kGiveUpTicks.
inline constexpr int kSettleTicks = 2;
inline constexpr int kGiveUpTicks = 600;  // about 5-10 s of game ticks
struct Schedule {
    Action action = Action::None;
    int settled = 0;  // ticks in a row with the menu closed and the game not paused by it
    int age = 0;      // ticks since it was taken
};
struct Step {
    Action run = Action::None;      // do this now
    Action dropped = Action::None;  // this request was called off
    bool reopened = false;          // ...because our menu opened again (else it waited too long)
};
// A request just taken (None: nothing new, the schedule is left alone). A new one replaces one still waiting (the menu
// can't ask twice without opening in between, which calls the first off anyway).
void Take(Schedule& schedule, Action action);
// One game tick. `menuOpen`: our main menu is open; `pausedByMenu`: the game is still paused by it.
Step Advance(Schedule& schedule, bool menuOpen, bool pausedByMenu);

} // namespace mevr::vr::game_menu
