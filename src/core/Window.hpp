// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

namespace mevr::window {

// The headset session gaining focus doesn't give the game window Windows focus, and UE3 ignores
// keyboard, mouse and gamepad without it. Windows only lets the foreground thread hand out
// focus, so this briefly shares its input queue. Any thread.
void BringToForeground(HWND window);

} // namespace mevr::window
