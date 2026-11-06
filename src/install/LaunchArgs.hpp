// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What starting Mass Effect 1 needs, shared by MEVR Setup (src/install/Launch) and our ASI loader in the launcher
// (src/loader: its autoboot, research/33 section 12). Small on purpose: the loader links it without the installer.
namespace mevr::install::launch {

// The Legendary Edition launcher's settings (Documents\BioWare\Mass Effect Legendary Edition\LauncherConfig.cfg), which
// the launcher turns into the game's arguments.
struct LauncherConfig {
    std::string language = "en_US";  // en_US, fr_FR, de_DE, it_IT, ja_JP, es_ES, ru_RU, pl_PL
    bool englishVoices = false;      // EnglishVOEnabled
    int subtitleSize = 20;
};
LauncherConfig ParseLauncherConfig(std::string_view text);
// LauncherConfig.cfg's path (Documents\BioWare\Mass Effect Legendary Edition).
std::filesystem::path LauncherConfigPath();
// The player's launcher settings, or the defaults when there's no file.
LauncherConfig ReadLauncherConfig();

// LE1's language code for -OVERRIDELANGUAGE (the local or the English-voice variant: en_US INT, fr_FR FR / FE,
// de_DE DE / GE, it_IT IT / IE, ja_JP JA, es_ES ES, ru_RU RA / RU, pl_PL PLPC / PL).
std::string Le1Language(LauncherConfig const& config);
// MassEffect1.exe's arguments: -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles N -OVERRIDELANGUAGE=X
// (the launcher passes "{locale}" literally).
std::wstring Le1Arguments(LauncherConfig const& config);

// The launcher's command line as the autoboot reads it: `-game N` (the game to start, 1-3) and `-autoterminate` (end the
// launcher once the game has started). Case-insensitive; `-game=N` too. Anything else is the launcher's own.
struct LauncherBoot {
    std::optional<int> game;
    bool autoTerminate = false;
};
LauncherBoot ParseLauncherArgs(std::vector<std::wstring> const& args);

// Steam's app id of the Legendary Edition, and the arguments that start Mass Effect 1 through Steam without the
// launcher's picker: Steam passes them to the launcher, where our loader (or ME3Tweaks' LEBinkProxy) boots the game.
inline constexpr wchar_t kSteamAppIdW[] = L"1328670";
inline constexpr wchar_t kSteamBootLe1Args[] = L"-applaunch 1328670 -game 1 -autoterminate";

} // namespace mevr::install::launch
