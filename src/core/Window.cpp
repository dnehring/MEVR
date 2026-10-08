#include "core/Window.hpp"

#include "core/Log.hpp"

namespace mevr::window {

void BringToForeground(HWND window) {
    HWND const foreground = GetForegroundWindow();
    if (window == nullptr || foreground == window) {
        return;
    }
    DWORD const foregroundThread = GetWindowThreadProcessId(foreground, nullptr);
    DWORD const thisThread = GetCurrentThreadId();
    bool const attached = foregroundThread != 0 && foregroundThread != thisThread &&
        AttachThreadInput(thisThread, foregroundThread, TRUE);
    BOOL const ok = SetForegroundWindow(window);
    if (attached) {
        AttachThreadInput(thisThread, foregroundThread, FALSE);
    }
    log::Info("game window to foreground: {}", ok ? "ok" : "refused by Windows");
}

} // namespace mevr::window
