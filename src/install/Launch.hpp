// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Starting LE1 without the Legendary Edition launcher's game picker (research/33, "Launching"). The launcher only
// starts Game\ME1\Binaries\Win64\MassEffect1.exe with a few arguments made from the player's launcher settings
// (Documents\BioWare\Mass Effect Legendary Edition\LauncherConfig.cfg: language, English voices, subtitle size);
// ME3Tweaks' launcher autoboot and Mod Manager start the same exe the same way. So does our Launch button, with
// Steam's app id in the environment and steam_appid.txt next to the exe (as Mod Manager does for Steam installs).
namespace mevr::install::launch {

struct LauncherConfig {
    std::string language = "en_US";  // en_US, fr_FR, de_DE, it_IT, ja_JP, es_ES, ru_RU, pl_PL
    bool englishVoices = false;      // EnglishVOEnabled
    int subtitleSize = 20;
};
LauncherConfig ParseLauncherConfig(std::string_view text);
// LauncherConfig.cfg's path (Documents\BioWare\Mass Effect Legendary Edition).
std::filesystem::path LauncherConfigPath();

// LE1's language code for -OVERRIDELANGUAGE (the local or the English-voice variant: en_US INT, fr_FR FR / FE,
// de_DE DE / GE, it_IT IT / IE, ja_JP JA, es_ES ES, ru_RU RA / RU, pl_PL PLPC / PL).
std::string Le1Language(LauncherConfig const& config);
// MassEffect1.exe's arguments: -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles N -OVERRIDELANGUAGE=X
// (the launcher passes "{locale}" literally).
std::wstring Le1Arguments(LauncherConfig const& config);

struct Result {
    bool started = false;
    unsigned long pid = 0;
    std::string message;
};

// Starts LE1 directly. `steam`: the Steam build (starts Steam first if it isn't running, waits up to 60 s for a
// logged-in user, sets SteamAppId / SteamGameId). steam_appid.txt must already be next to the exe (the installer
// writes it, recorded in the manifest).
Result StartLe1(std::filesystem::path const& gameRoot, bool steam);
// The fallback: the game through Steam (its launcher's picker shows).
bool StartThroughSteam();

} // namespace mevr::install::launch
