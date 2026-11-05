// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

// The game pointer's input as the game's own GUI events (research/56 section 6, measured 2026-11-05). LE1's screens take the
// mouse through SFXGameModeBase.GuiKey(Event) -> BioSFManager.HandleInputEvent -> BioSFHandler.SendMouseToScaleForm, which reads
// the cursor's place (GetCursorPos, answered by the host with the pointer's point). Called by the mod on the game thread, these
// click, scroll and move the cursor exactly and at once: no real input, no window focus, no race with the game's ticks (real
// SendInput clicks landed a tap late about half the time). The host queues them (Queue, any thread), the mod takes them each
// tick (HostServices v30) and sends them (Scheduler: the motion first, at most one button or wheel event per tick).
namespace mevr::vr::gui_events {

// The BIOGUI_EVENT numbers (Engine.BioSFHandler's input events).
inline constexpr std::uint8_t kMouseMove = 6;       // BIOGUI_EVENT_AXIS_MOUSE_X: the screen reads the cursor's place
inline constexpr std::uint8_t kWheelUp = 28;        // BIOGUI_EVENT_KEY_WHEEL_UP
inline constexpr std::uint8_t kWheelDown = 29;      // BIOGUI_EVENT_KEY_WHEEL_DOWN
inline constexpr std::uint8_t kLeftDown = 50;       // BIOGUI_EVENT_MOUSE_BUTTON_LEFT
inline constexpr std::uint8_t kLeftUp = 52;         // BIOGUI_EVENT_MOUSE_BUTTON_LEFT_RELEASE

// The host's side: what the pointer did, in order. Moves are coalesced (one in a row); at most kCapacity wait (the oldest
// motion or wheel notches go first when full; a button is never dropped).
class Queue {
public:
    static constexpr std::size_t kCapacity = 64;
    void Move();
    void Button(bool down);
    void Wheel(int notches);  // + away from the player: scroll up
    // Up to `capacity` events into `out`, oldest first; the number taken.
    std::uint32_t Take(std::uint8_t* out, std::uint32_t capacity);
    std::size_t Size() const;
    void Clear();

private:
    void Push(std::uint8_t event);
    mutable std::mutex mutex_;
    std::deque<std::uint8_t> events_;
};

// The mod's side: this tick's GuiKey calls from what is waiting. A run of moves becomes one; a button or a wheel notch goes one
// per tick, with a move just before it so the screen reads the place the event is for.
class Scheduler {
public:
    void Add(std::uint8_t const* events, std::uint32_t count);
    // This tick's events, in order (empty when nothing waits).
    std::vector<std::uint8_t> Tick();
    std::size_t Waiting() const { return pending_.size(); }
    void Clear() { pending_.clear(); }

private:
    std::deque<std::uint8_t> pending_;
};

} // namespace mevr::vr::gui_events
