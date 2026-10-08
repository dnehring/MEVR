#include "core/Hotkey.hpp"

#include <Windows.h>

namespace mevr {

bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool Hotkey::Pressed() {
    bool const down = GameHasFocus() && (GetAsyncKeyState(virtualKey_) & 0x8000) != 0;
    bool const pressed = down && !wasDown_;
    wasDown_ = down;
    return pressed;
}

} // namespace mevr
