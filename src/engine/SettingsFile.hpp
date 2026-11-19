// SPDX-License-Identifier: MIT
#pragma once

// Our settings files. Since 2026-11-18 one for the three games, %LOCALAPPDATA%\MEVR\settings.ini (research/25 section 20,
// core/vr/SettingsFile): SettingsFile() is the file in use; GameSettingsFile() is this game's old <exe dir>\ASI\<GAME>MEVR.ini
// (the migration's source and the inbox; the file in use only while the shared one can't be read). One path for the host
// and the mod, whatever the host ASI is called: a renamed ASI used to point the host at <its name>.ini and the mod at
// LE1MEVR.ini. Folders next to the ASI (fonts, menu, cockpit, voice) come from AsiDir(), never from SettingsFile()'s folder.

#include "core/vr/CalibrationFile.hpp"
#include "core/vr/SettingsFile.hpp"
#include "engine/GameAdapter.hpp"

#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace mevr::engine {

// This game's old settings file, <exe dir>\ASI\<GAME>MEVR.ini.
inline std::filesystem::path GameSettingsFile() {
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

// <exe dir>\ASI: the folder of our ASI and its files (fonts, menu, cockpit, voice, audio).
inline std::filesystem::path AsiDir() {
    return GameSettingsFile().parent_path();
}

// The settings file in use: %LOCALAPPDATA%\MEVR\settings.ini, the three games' (core/vr/SettingsFile InUse: the host decides
// at attach, after the old files are taken in, and the mod reads its answer). Read it with vr::settings_file's Load, Int,
// String or Section (this game's [<section>.<GAME>] first); write it with Write, Apply or Append (per-game keys routed).
inline std::filesystem::path SettingsFile() {
    return vr::settings_file::InUse(GameSettingsFile());
}

// Where the settings' migration and inbox look: the three games' ASI\<GAME>MEVR.ini under this install, ME1's first (the base).
inline std::vector<vr::settings_file::Source> SettingsSources() {
    return vr::settings_file::SourcesUnder(AsiDir().parent_path(), MEVR_GAME_NAME_A);
}

// The player's calibration, one file for the three games (research/25 section 17): %LOCALAPPDATA%\MEVR\calibration.ini.
// The host writes it, the mod reads it: both name it here (core/vr/CalibrationFile).
inline std::filesystem::path CalibrationFile() {
    return vr::calibration_file::DefaultPath();
}

// Where the calibration's one-time migration looks: the three games' ASI\<GAME>MEVR.ini under this install, this game's first.
inline std::vector<vr::calibration_file::Source> CalibrationSources() {
    return vr::calibration_file::SourcesUnder(AsiDir().parent_path(), MEVR_GAME_NAME_A);
}

// Development-only engine validation: the host and mod independently select the same immutable session mode.
// Player builds can never disable normal startup through this research option.
inline bool DevelopmentEngineProbe() {
#if defined(MEVR_GAME_LE3) && !defined(MEVR_MONOLITHIC)
    return vr::settings_file::Int(L"Debug", L"engineProbe", 0, SettingsFile().c_str()) != 0;
#else
    return false;
#endif
}

} // namespace mevr::engine
