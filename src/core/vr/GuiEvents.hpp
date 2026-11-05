// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

// The game pointer's input as the game's own GUI events (research/57 section 6, measured 2026-11-05). LE1's screens take the
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
// The pad's directions (the bypass puzzle's d-pad, research/53 section 6.3): pressed 8..11, let go 30..33.
inline constexpr std::uint8_t kControlDown = 8;     // BIOGUI_EVENT_CONTROL_DOWN
inline constexpr std::uint8_t kControlLeft = 9;     // BIOGUI_EVENT_CONTROL_LEFT
inline constexpr std::uint8_t kControlRight = 10;   // BIOGUI_EVENT_CONTROL_RIGHT
inline constexpr std::uint8_t kControlUp = 11;      // BIOGUI_EVENT_CONTROL_UP
inline constexpr std::uint8_t kControlDownRelease = 30;
inline constexpr std::uint8_t kControlLeftRelease = 31;
inline constexpr std::uint8_t kControlRightRelease = 32;
inline constexpr std::uint8_t kControlUpRelease = 33;
// A pad direction's event (pressed or let go): never dropped, and sent without a mouse motion before it.
inline constexpr bool IsControl(std::uint8_t event) {
    return (event >= kControlDown && event <= kControlUp) || (event >= kControlDownRelease && event <= kControlUpRelease);
}

// The host's side: what the pointer did, in order. Moves are coalesced (one in a row); at most kCapacity wait (the oldest
// motion or wheel notches go first when full; a button is never dropped).
class Queue {
public:
    static constexpr std::size_t kCapacity = 64;
    void Move();
    void Button(bool down);
    void Wheel(int notches);  // + away from the player: scroll up
    void Control(std::uint8_t event);  // a pad direction pressed or let go (IsControl; others are ignored)
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
// per tick, with a move just before it so the screen reads the place the event is for; a pad direction goes alone, one per tick
// (a mouse motion would put the bypass puzzle in its mouse mode).
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
