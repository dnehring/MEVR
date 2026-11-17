// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

// The three games of the Legendary Edition as the installer sees them (docs/trilogy-plan.md T52): one description per
// game, and every step of src/install (find, install, repair, uninstall, the VR game settings, launch, the problem
// report) takes it as a parameter. Nothing here is a second installer: a native fact that differs between the games is a
// field, and the code that reads it is the same.
namespace mevr::install {

enum class GameId { LE1, LE2, LE3 };

struct GameSpec {
    GameId id;
    int number;                  // 1..3: Game\ME<n>, MassEffect<n>.exe, the launcher's -game <n>
    char const* key;             // "LE1": the manifest's "game"; <key>MEVR.asi, <key>MEVR.ini, <key>MEVR.log
    char const* name;            // "Mass Effect 1"
    char const* folder;          // "ME1" (under Game)
    wchar_t const* folderW;
    char const* exe;             // "MassEffect1.exe"
    wchar_t const* exeW;
    // Halcyon's renderer for this game (built into our host): its settings file and where the game keeps it, its log
    // under %LOCALAPPDATA%, and its upstream release (a dxgi.dll proxy next to the exe: two renderers would run).
    char const* rendererIni;     // "MELEVR.ini", "MELE2VR.ini", "MELE3VR.ini"
    bool rendererIniInAsi;       // LE1: in the ASI folder; the sequels: next to the exe
    wchar_t const* rendererLogDir;  // %LOCALAPPDATA%\<this>
    wchar_t const* rendererLog;     // the file in it
    char const* upstreamName;    // "MELE-VR"
    char const* upstreamMarker;  // the ASCII string upstream's own installer tells its dxgi.dll by
    // The VR game settings (tools/vr-game-settings.ps1 -Game <key>).
    bool stereoTarget;           // the setup sets ResX/ResY and Halcyon's copy of them (LE1); the sequels' renderer writes
                                 // them itself from its ini's RenderW/RenderH at attach
    bool engineIni;              // BIOEngine.ini: SmoothFrameRate and the game's copy of its live settings (LE1)
    bool dynamicShadowsOff;      // GamerSettings.ini DynamicShadows=False (LE1 only: the sequels keep them, the mod turns
                                 // them on there; the owner, 2026-11-17)
    bool foliageRadius;          // GamerSettings.ini FoliageDrawRadiusMultiplier=0.5 (LE1 sets it in BIOEngine.ini instead)
    // What the release carries for this game besides the ASI, the loader, the fonts and the menu.
    bool cockpit;                // the Mako's cockpit package and its TOC entry
    bool voice;                  // MEVR-Voice.dll and the voice orders' downloads
    bool audio;                  // OpenAL Soft
    bool packs;                  // the texture packs' catalogue (Mass Effect Modder)
    // The sequels' renderer: the render size it writes into GamerSettings.ini when its ini has no [VR] RenderW/RenderH (LE3:
    // 3072x2216, the owner's answer to docs/trilogy-plan.md question 17; 0: the game's own resolution, LE2's code default).
    int rendererDefaultW = 0;
    int rendererDefaultH = 0;
};

inline constexpr std::array<GameId, 3> kAllGames{GameId::LE1, GameId::LE2, GameId::LE3};

GameSpec const& Spec(GameId game);
// "LE2", "le2", "ME2", "2", "Mass Effect 2" -> LE2; nullopt for anything else.
std::optional<GameId> ParseGame(std::string_view text);
// "LE1, LE3" -> those games in the trilogy's order, each once; unknown words are skipped.
std::array<bool, 3> ParseGameList(std::string_view text);
inline std::size_t Index(GameId game) { return static_cast<std::size_t>(game); }

// Paths relative to the game root (forward slashes), as the manifest records them. LE1's are also the constants in
// install::paths (Manifest.hpp), which stay for the callers that only ever mean LE1.
struct GamePaths {
    std::string win64;          // Game/ME1/Binaries/Win64
    std::string asi;            // .../ASI
    std::string logs;           // .../Logs
    std::string bink;           // .../bink2w64.dll
    std::string binkOriginal;   // .../bink2w64_original.dll
    std::string exe;            // .../MassEffect1.exe
    std::string steamAppId;     // .../steam_appid.txt
    std::string dxgi;           // .../dxgi.dll (upstream Halcyon's proxy)
    std::string openXrLoader;   // .../openxr_loader.dll (upstream's)
    std::string modAsi;         // .../ASI/LE1MEVR.asi
    std::string modIni;         // .../ASI/LE1MEVR.ini
    std::string rendererIni;    // .../ASI/MELEVR.ini, or .../MELE2VR.ini next to the exe
    std::string setupExe;       // .../ASI/MEVR-Setup.exe
    std::string modLog;         // .../Logs/LE1MEVR.log
    std::string modPrevLog;     // .../Logs/LE1MEVR.prev.log
    std::string loaderLog;      // .../Logs/MEVRLoader.log
    std::string config;         // Game/ME1/BioGame/Config
    std::string gamerSettings;  // .../GamerSettings.ini
    std::string engineIni;      // .../BIOEngine.ini
};
GamePaths const& PathsFor(GameId game);

// The one switch for the sequels (docs/trilogy-plan.md T52: whether and when a sequel is offered is the owner's call).
// MEVR-Setup.ini next to the setup: `[Setup] games = LE1, LE2, LE3`; a missing file or key means LE1 only, as every
// release so far. tools/package-release.ps1 -Games writes it together with the sequels' payload. A game with an install
// record is always shown (so it can be repaired, launched and uninstalled), whatever the switch says.
std::array<bool, 3> OfferedGames(std::string_view setupIni);

} // namespace mevr::install
