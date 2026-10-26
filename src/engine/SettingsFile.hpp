// SPDX-License-Identifier: MIT
#pragma once

// Our settings file: <exe dir>\ASI\<GAME>MEVR.ini (LE1MEVR.ini for LE1). One path for the host and the mod, whatever
// the host ASI is called: a renamed ASI used to point the host at <its name>.ini and the mod at LE1MEVR.ini.

#include "engine/GameAdapter.hpp"

#include <Windows.h>

#include <filesystem>
#include <string>

namespace mevr::engine {

inline std::filesystem::path SettingsFile() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD const n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) {
            return {};
        }
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);  // a path longer than MAX_PATH
    }
    return std::filesystem::path(buffer).parent_path() / L"ASI" / (MEVR_GAME_NAME_W L"MEVR.ini");
}

} // namespace mevr::engine
