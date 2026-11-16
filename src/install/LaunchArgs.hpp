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

// The same for any of the three games (`game` 1..3; anything else is Mass Effect 1). The launcher passes each game the
// same switches; what differs is the language code (LE2 and LE3: en_US INT, fr_FR FRA / FRE, de_DE DEU / DEE, it_IT ITA /
// ITE, ja_JP JPN, es_ES ESN, ru_RU RUS, pl_PL POL / POE in LE2 and POL in LE3) and LE3's switch for it, -language=X
// (research/33 section 2: the behaviour of the launcher and of LEBinkProxy's launcher module, read, never copied).
std::string GameLanguage(int game, LauncherConfig const& config);
std::wstring GameArguments(int game, LauncherConfig const& config);
// "Game\ME2\Binaries\Win64\MassEffect2.exe" pieces for the autoboot: the folder under Game and the exe's name.
wchar_t const* GameFolderName(int game);
wchar_t const* GameExeName(int game);

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
// The same for any of the three games: "-applaunch 1328670 -game <n> -autoterminate".
std::wstring SteamBootArgs(int game);
// Which games our own loader in Game\Launcher starts when the launcher is given -game <n> (src/loader BootFromLauncher).
// Today only Mass Effect 1: for a sequel the launcher's own picker shows, and Setup's Launch says so. ME3Tweaks'
// LEBinkProxy there starts all three. When the loader's autoboot takes -game 2 and 3 (GameArguments above is what it
// needs), set these and Launch stops telling players to pick the game.
inline constexpr bool kOurLauncherBoots[4] = {false, true, false, false};

} // namespace mevr::install::launch
