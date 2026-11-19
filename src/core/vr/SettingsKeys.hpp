// SPDX-License-Identifier: MIT
#pragma once

// One settings file for the trilogy (research/25 section 20; the owner, 2026-11-18: "Yes, share them"): the names and the
// rule, nothing that reads a file. Pure (only <Windows.h> for the folder), so MEVR Setup's library (src/install) uses it
// too; core/vr/SettingsFile is the reader, the writer and the migration over it.
//
//   %LOCALAPPDATA%\MEVR\settings.ini   the settings of ME1, ME2 and ME3
//   [<section>]                         what the three games share
//   [<section>.LE1] .LE2 .LE3           what applies to that one game: it overrides only the keys it names
//
// Which keys a game keeps for itself is PerGame() below: a rule in code, unit-tested (tests/SettingsFileTests.cpp), never a
// hand list of keys elsewhere. A writer sends a per-game key to [<section>.<GAME>] and every other key to [<section>]; a
// reader asks [<section>.<GAME>] first, then [<section>].

#include <Windows.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mevr::vr::settings_file {

inline constexpr char kFileName[] = "settings.ini";
// The old per-game files as last taken into the shared one (the inbox's reference, research/25 section 20.5).
inline constexpr wchar_t kTakenFolder[] = L"settings-taken";
// The mark a taken old file gets: [Settings] shared = 1 (it stays on disk for an older build).
inline constexpr char kMarkerSection[] = "Settings";
inline constexpr char kMarkerKey[] = "shared";

inline constexpr std::array<std::string_view, 3> kGames{"LE1", "LE2", "LE3"};

// The game this binary is built for ("" in a game-independent target: the tests, the tools), from CMake's MEVR_GAME_LEn.
inline constexpr std::string_view kThisGame =
#if defined(MEVR_GAME_LE1)
    "LE1";
#elif defined(MEVR_GAME_LE2)
    "LE2";
#elif defined(MEVR_GAME_LE3)
    "LE3";
#else
    "";
#endif

inline char LowerAscii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string Lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = LowerAscii(c);
    }
    return out;
}

inline bool Same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (LowerAscii(a[i]) != LowerAscii(b[i])) {
            return false;
        }
    }
    return true;
}

inline std::string_view TrimView(std::string_view text) noexcept {
    std::size_t const first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// "LE1", "LE2" or "LE3" as kGames spells it; nullopt for anything else.
inline std::optional<std::string_view> GameName(std::string_view text) noexcept {
    for (std::string_view const g : kGames) {
        if (Same(text, g)) {
            return g;
        }
    }
    return std::nullopt;
}

// A game's section: "Hands.LE2" -> {"Hands", "LE2"}. nullopt for a common section, and for sections whose last part is
// anything else ("Colour.steamvr_openxr_playstation_vr2" is a headset's, "Body.Shepard" a section of its own).
struct GameSection {
    std::string base;
    std::string game;
};

inline std::optional<GameSection> SplitGameSection(std::string_view section) {
    std::size_t const dot = section.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return std::nullopt;
    }
    if (auto const game = GameName(section.substr(dot + 1))) {
        return GameSection{std::string(section.substr(0, dot)), std::string(*game)};
    }
    return std::nullopt;
}

inline std::string GameSectionName(std::string_view base, std::string_view game) {
    return std::string(base) + "." + std::string(game);
}

// "C:\..." or "\\server\...": a value that names a place on this machine.
inline bool IsAbsolutePath(std::string_view value) noexcept {
    std::string_view const v = TrimView(value);
    bool const drive = v.size() >= 3 && ((v[0] >= 'A' && v[0] <= 'Z') || (v[0] >= 'a' && v[0] <= 'z')) && v[1] == ':' &&
                       (v[2] == '\\' || v[2] == '/');
    return drive || (v.size() >= 2 && v[0] == '\\' && v[1] == '\\');
}

// Why [section] key stays per game (its value lives in [section.<GAME>]); nullptr: the three games share it.
// The rule, in the order it's checked (research/25 section 20.2 has each one's reason and the audit behind it):
//   1. the performance levers: their defaults come from each game's own measurement (the owner, 2026-11-16);
//   2. the development switches: tools set them for the game they run (the fake headset's runtime, the request files);
//   3. the render size's first-start marker, and the first-run markers (each game's first start is its own);
//   4. what is measured on that game's skeleton, and the arm sample (the owner's answer of 2026-11-18);
//   5. the calibration's per-game settings (research/25 section 17: the measurements are shared, these are not);
//   6. a path: into that game's folder, by its key or by its value.
// Everything else is shared: a change in any game's menu is a change in the three (the owner, 2026-11-18).
inline char const* PerGame(std::string_view section, std::string_view key, std::string_view value = {}) {
    if (Same(section, "Performance") || Same(section, "SystemSettings") || Same(section, "Upscale")) {
        return "a performance lever: each game's own, from its measurements";
    }
    if (Same(section, "Debug")) {
        return "a development switch, for the game a tool runs";
    }
    if (Same(section, "Display") && Same(key, "eyeSizeSet")) {
        return "the render size's first-start marker: each game sizes its own render";
    }
    if (Same(section, "Menu") && Same(key, "controlsShown")) {
        return "a first-run marker: each game's first start shows its own guide";
    }
    if (Same(section, "Body.Shepard")) {
        return "measured on this game's skeleton (game/BodyScale: Shepard's standing eyes per mesh)";
    }
    if (Same(section, "Arms")) {
        return "the VR hands tab's arm sample (the owner, 2026-11-18: it stays per game; the calibration's arm length is shared)";
    }
    if (Same(section, "Calibration") && (Same(key, "use") || Same(key, "eye") || Same(key, "grabOffset"))) {
        return "the calibration's per-game setting (the measurements are shared in calibration.ini)";
    }
    if ((Same(section, "Menu") && Same(key, "assets")) || (Same(section, "Voice") && (Same(key, "dll") || Same(key, "modelDir"))) ||
        IsAbsolutePath(value)) {
        return "a path into this game's folder";
    }
    return nullptr;
}

// %LOCALAPPDATA%\MEVR\settings.ini (empty without LOCALAPPDATA).
inline std::filesystem::path DefaultPath() {
    wchar_t buffer[MAX_PATH]{};
    DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR" / kFileName;
}

// Whether a path is a shared settings file (by its name: the tests' files live in temporary folders). A writer routes
// per-game keys only into one; an old per-game file in use (the shared one unreadable) is written as it always was.
inline bool IsSharedPath(std::filesystem::path const& path) {
    std::wstring const name = path.filename().wstring();
    if (name.size() != sizeof(kFileName) - 1) {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i) {
        wchar_t c = name[i];
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
        if (c != static_cast<wchar_t>(kFileName[i])) {
            return false;
        }
    }
    return true;
}

// The old file as last taken into the shared one: <shared folder>\settings-taken\LE1MEVR.ini.
inline std::filesystem::path TakenCopy(std::filesystem::path const& shared, std::string_view game) {
    return shared.parent_path() / kTakenFolder / (std::wstring(game.begin(), game.end()) + L"MEVR.ini");
}

} // namespace mevr::vr::settings_file
