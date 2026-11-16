// SPDX-License-Identifier: MIT
#include "install/Game.hpp"

#include "install/Ini.hpp"

#include <cctype>

namespace mevr::install {
namespace {

// What differs between the games (tools/deploy-asi.ps1, tools/vr-game-settings.ps1 and tools/le2-halcyon-run/prep.ps1
// are the development installs these describe). The sequels' renderer logs: src/halcyon/le2/logger.cpp
// (%LOCALAPPDATA%\MELE2VR\MELE2VR_Log.txt) and src/halcyon/le3/logger.cpp (%LOCALAPPDATA%\MELEVR\ME3PassiveDxgiProbe.log).
// Upstream's installers tell their own dxgi.dll by a string in it: MELE-VR.bat by "MELEVR", MELE2-VR.bat and
// MELE3-VR.bat by "ME2VR" (ME3's renderer keeps ME2's namespace).
constexpr GameSpec kSpecs[] = {
    {GameId::LE1, 1, "LE1", "Mass Effect 1", "ME1", L"ME1", "MassEffect1.exe", L"MassEffect1.exe",
        "MELEVR.ini", true, L"MELEVR", L"MELEVR_Log.txt", "MELE-VR", "MELEVR",
        true, true, true, false,
        true, true, true, true},
    {GameId::LE2, 2, "LE2", "Mass Effect 2", "ME2", L"ME2", "MassEffect2.exe", L"MassEffect2.exe",
        "MELE2VR.ini", false, L"MELE2VR", L"MELE2VR_Log.txt", "MELE2-VR", "ME2VR",
        false, false, true, true,
        false, false, false, false},
    {GameId::LE3, 3, "LE3", "Mass Effect 3", "ME3", L"ME3", "MassEffect3.exe", L"MassEffect3.exe",
        "MELE3VR.ini", false, L"MELEVR", L"ME3PassiveDxgiProbe.log", "MELE3-VR", "ME2VR",
        false, false, false, false,
        false, false, false, false},
};

GamePaths Make(GameSpec const& s) {
    GamePaths p;
    std::string const game = std::string("Game/") + s.folder;
    p.win64 = game + "/Binaries/Win64";
    p.asi = p.win64 + "/ASI";
    p.logs = p.win64 + "/Logs";
    p.bink = p.win64 + "/bink2w64.dll";
    p.binkOriginal = p.win64 + "/bink2w64_original.dll";
    p.exe = p.win64 + "/" + s.exe;
    p.steamAppId = p.win64 + "/steam_appid.txt";
    p.dxgi = p.win64 + "/dxgi.dll";
    p.openXrLoader = p.win64 + "/openxr_loader.dll";
    p.modAsi = p.asi + "/" + s.key + "MEVR.asi";
    p.modIni = p.asi + "/" + s.key + "MEVR.ini";
    p.rendererIni = (s.rendererIniInAsi ? p.asi : p.win64) + "/" + s.rendererIni;
    p.setupExe = p.asi + "/MEVR-Setup.exe";
    p.modLog = p.logs + "/" + s.key + "MEVR.log";
    p.modPrevLog = p.logs + "/" + s.key + "MEVR.prev.log";
    p.loaderLog = p.logs + "/MEVRLoader.log";
    p.config = game + "/BioGame/Config";
    p.gamerSettings = p.config + "/GamerSettings.ini";
    p.engineIni = p.config + "/BIOEngine.ini";
    return p;
}

std::string Squeezed(std::string_view text) {
    std::string out;
    for (char const c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

} // namespace

GameSpec const& Spec(GameId game) {
    return kSpecs[Index(game)];
}

GamePaths const& PathsFor(GameId game) {
    static GamePaths const paths[] = {Make(kSpecs[0]), Make(kSpecs[1]), Make(kSpecs[2])};
    return paths[Index(game)];
}

std::optional<GameId> ParseGame(std::string_view text) {
    std::string const t = Squeezed(text);
    for (GameSpec const& s : kSpecs) {
        std::string const n = std::to_string(s.number);
        if (t == s.key || t == s.folder || t == n || t == "MASSEFFECT" + n) {
            return s.id;
        }
    }
    return std::nullopt;
}

std::array<bool, 3> ParseGameList(std::string_view text) {
    std::array<bool, 3> out{};
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find_first_of(",;", at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        if (auto const game = ParseGame(text.substr(at, end - at))) {
            out[Index(*game)] = true;
        }
        at = end + 1;
    }
    return out;
}

std::array<bool, 3> OfferedGames(std::string_view setupIni) {
    std::array<bool, 3> offered{true, false, false};
    if (auto const games = ini::Get(setupIni, "Setup", "games")) {
        // Mass Effect 1 is always offered: the key only adds the sequels.
        std::array<bool, 3> const listed = ParseGameList(*games);
        offered[Index(GameId::LE2)] = listed[Index(GameId::LE2)];
        offered[Index(GameId::LE3)] = listed[Index(GameId::LE3)];
    }
    return offered;
}

} // namespace mevr::install
