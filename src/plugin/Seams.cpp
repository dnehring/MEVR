// SPDX-License-Identifier: MIT
// The seams every Halcyon renderer's host answers the same way (src/halcyon/MevrSeams.h), implemented once for LE1, LE2
// and LE3: the game thread and its queue, the key poll, the menu key, our main menu, the cinema screen's two settings and
// which OpenXR runtime this process uses. The seams whose answer differs per game are in that game's bridge
// (plugin/HalcyonBridge.cpp and HalcyonOverlays.cpp for LE1, HalcyonLe2Bridge.cpp, HalcyonLe3Bridge.cpp) until those are
// merged; research/trilogy-seams.md lists where each one is.

#include "halcyon/MevrSeams.h"

#include "core/Window.hpp"
#include "plugin/CineScreen.hpp"
#include "plugin/GameThreadCalls.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/MainMenu.hpp"
#endif
#include "plugin/Renderer.hpp"
#include "plugin/Upscale.hpp"

#include <algorithm>
#include <cwctype>
#include <string>

namespace MevrSeams {

// ProcessEvent on the game thread only (research/36): a renderer's Present-thread calls go to plugin/GameThreadCalls, the
// queue the host drains at the start of the next engine tick (plugin/Entry).
bool OnGameThread() noexcept {
    return mevr::host::game_thread::OnGameThread();
}

bool QueueProcessEvent(void* object, void* function, void const* params, std::uint32_t size) noexcept {
    return size == kFunctionParmsSize ? mevr::host::game_thread::Queue(object, function, params)
                                      : mevr::host::game_thread::Queue(object, function, params, size);
}

bool KeyDown(int vk) noexcept {
    return mevr::window::KeyDownInGame(vk);  // the game's window (or its console), not another program's
}

// The eye images' size for a renderer that makes them at the picture's size (the sequels'): larger when ME1's eye
// upscale has something to scale to (plugin/Upscale, [Upscale] method; research/27 "One eye-image pipeline").
void EyeImageSize(std::uint32_t sourceWidth, std::uint32_t sourceHeight, std::uint32_t recommendedWidth,
    std::uint32_t recommendedHeight, std::uint32_t* width, std::uint32_t* height) noexcept {
    mevr::vr::upscale::Size const size =
        mevr::host::upscale::EyeImageSize(sourceWidth, sourceHeight, recommendedWidth, recommendedHeight);
    if (width != nullptr) {
        *width = size.width;
    }
    if (height != nullptr) {
        *height = size.height;
    }
}

bool OnMenuKey(bool ownMenuOpen) noexcept {
#if defined(MEVR_WITH_MAIN_MENU)
    // The renderer's menu key (\ in our deploy and baselines) is our menu's; with Ctrl or Shift held, Halcyon's own (a
    // developer fallback). While Halcyon's menu is open, the key closes it as before.
    bool const modifier = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (!modifier && !ownMenuOpen) {
        mevr::host::main_menu::Toggle("the menu key");
        return true;
    }
    if (mevr::host::main_menu::IsOpen()) {
        mevr::host::main_menu::SetOpen(false, "Ctrl/Shift + the menu key: Halcyon's menu instead");
    }
#else
    (void)ownMenuOpen;
#endif
    return false;
}

bool MainMenuOpen() noexcept {
#if defined(MEVR_WITH_MAIN_MENU)
    return mevr::host::main_menu::IsOpen();
#else
    return false;
#endif
}

bool CineScreenWanted(bool conversation) noexcept {
    // The renderer has the screen (the same capability our menu reads to offer it) and the player chose it.
    return mevr::host::renderer::Capabilities().cineScreen && mevr::host::cine_screen::Wanted(conversation);
}

bool CineScreenFullRate() noexcept {
    return mevr::host::cine_screen::FullRate();
}

namespace {
bool NamesSteamVr(std::wstring path) {
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return path.find(L"steamvr") != std::wstring::npos || path.find(L"steamxr") != std::wstring::npos;
}
} // namespace

bool XrRuntimeIsSteamVr() noexcept {
    // XR_RUNTIME_JSON, which the statically linked loader takes over the machine's active runtime: our [Debug]
    // openxrRuntime (set at attach, before the renderer asks), or the user's own.
    wchar_t manifest[1024] = {};
    if (GetEnvironmentVariableW(L"XR_RUNTIME_JSON", manifest, 1024) > 0) {
        return NamesSteamVr(manifest);
    }
    HKEY key = nullptr;
    bool steam = false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t path[1024] = {};
        DWORD bytes = sizeof(path) - sizeof(wchar_t);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type, reinterpret_cast<LPBYTE>(path), &bytes) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            steam = NamesSteamVr(path);
        }
        RegCloseKey(key);
    }
    return steam;
}

} // namespace MevrSeams
