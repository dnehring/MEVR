// SPDX-License-Identifier: MIT
#pragma once

namespace mevr {

// Edge-triggered key press, polled from the game thread. Only fires while the game window is
// in the foreground, so typing in other apps doesn't trigger it.
// Dev keys: not F-keys (bound in game) and not backtick (UE console).
class Hotkey {
public:
    explicit Hotkey(int virtualKey) : virtualKey_(virtualKey) {}
    bool Pressed();

private:
    int virtualKey_;
    bool wasDown_ = false;
};

bool GameHasFocus();

} // namespace mevr
