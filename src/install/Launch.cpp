// SPDX-License-Identifier: MIT
#include "install/Launch.hpp"

#include "install/Detect.hpp"
#include "install/Manifest.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <shellapi.h>

#include <cstdlib>
#include <map>
#include <sstream>

namespace mevr::install::launch {
namespace {

namespace fs = std::filesystem;

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    return s.substr(start);
}

bool SteamRunning() {
    DWORD pid = 0;
    DWORD size = sizeof pid;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", RRF_RT_REG_DWORD, nullptr, &pid, &size) != ERROR_SUCCESS ||
        pid == 0) {
        return false;
    }
    HANDLE const process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    bool const alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

bool SteamUserLoggedIn() {
    DWORD user = 0;
    DWORD size = sizeof user;
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", RRF_RT_REG_DWORD, nullptr, &user, &size) ==
               ERROR_SUCCESS &&
        user != 0;
}

} // namespace

LauncherConfig ParseLauncherConfig(std::string_view text) {
    LauncherConfig c;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        std::size_t const eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string const key = Trim(line.substr(0, eq));
        std::string const value = Trim(line.substr(eq + 1));
        if (key == "Language" && !value.empty()) {
            c.language = value;
        } else if (key == "EnglishVOEnabled") {
            c.englishVoices = value == "true";
        } else if (key == "SubtitleSize") {
            int const n = std::atoi(value.c_str());
            if (n > 0 && n < 100) {
                c.subtitleSize = n;
            }
        }
    }
    return c;
}

fs::path LauncherConfigPath() {
    PWSTR documents = nullptr;
    fs::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
        path = fs::path(documents) / L"BioWare" / L"Mass Effect Legendary Edition" / L"LauncherConfig.cfg";
    }
    CoTaskMemFree(documents);
    return path;
}

std::string Le1Language(LauncherConfig const& c) {
    // {local voices, English voices}.
    static std::map<std::string, std::pair<char const*, char const*>> const kCodes = {
        {"en_US", {"INT", "INT"}}, {"fr_FR", {"FR", "FE"}}, {"de_DE", {"DE", "GE"}}, {"it_IT", {"IT", "IE"}},
        {"ja_JP", {"JA", "JA"}}, {"es_ES", {"ES", "ES"}}, {"ru_RU", {"RA", "RU"}}, {"pl_PL", {"PLPC", "PL"}},
    };
    auto const it = kCodes.find(c.language);
    if (it == kCodes.end()) {
        return "INT";
    }
    return c.englishVoices ? it->second.second : it->second.first;
}

std::wstring Le1Arguments(LauncherConfig const& c) {
    return L"-NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles " + std::to_wstring(c.subtitleSize) + L" -OVERRIDELANGUAGE=" +
        detect::Wide(Le1Language(c));
}

Result StartLe1(fs::path const& gameRoot, bool steam) {
    Result r;
    Layout const layout{gameRoot};
    fs::path const exe = layout.Win64() / L"MassEffect1.exe";
    std::error_code ec;
    if (!fs::exists(exe, ec)) {
        r.message = "MassEffect1.exe not found";
        return r;
    }
    LauncherConfig config;
    if (auto const text = ReadFile(LauncherConfigPath())) {
        config = ParseLauncherConfig(*text);
    }
    if (steam) {
        if (!SteamRunning()) {
            ShellExecuteW(nullptr, L"open", L"steam://open/main", nullptr, nullptr, SW_SHOWMINNOACTIVE);
        }
        for (int i = 0; i < 120 && !(SteamRunning() && SteamUserLoggedIn()); ++i) {
            Sleep(500);
        }
        if (!SteamUserLoggedIn()) {
            r.message = "Steam isn't running with a logged-in user; start Steam and try again";
            return r;
        }
        SetEnvironmentVariableW(L"SteamAppId", L"1328670");
        SetEnvironmentVariableW(L"SteamGameId", L"1328670");
        SetEnvironmentVariableW(L"SteamOverlayGameId", L"1328670");
    }
    std::wstring commandLine = L"\"" + exe.wstring() + L"\" " + Le1Arguments(config);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, layout.Win64().c_str(), &startup, &process)) {
        r.message = "couldn't start MassEffect1.exe (error " + std::to_string(GetLastError()) + ")";
        return r;
    }
    r.started = true;
    r.pid = process.dwProcessId;
    r.message = "Started Mass Effect (" + detect::Utf8(Le1Arguments(config)) + ")";
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return r;
}

bool StartThroughSteam() {
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"steam://rungameid/1328670", nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

// ---- The game's first start --------------------------------------------------------------------------------------

namespace first_start {

Phase Next(Observation const& o) {
    if (o.fileExists && !o.gameRunning) {
        return Phase::Apply;
    }
    if (o.gameRunning) {
        if (o.fileExists) {
            return o.sinceLaunchS > kQuitTimeoutS ? Phase::TimedOut : Phase::WaitingForQuit;
        }
        return o.sinceLaunchS > kFileTimeoutS ? Phase::TimedOut : Phase::WaitingForFile;
    }
    // Not running, no file.
    if (o.sinceSeenS < 0.0) {
        return o.sinceLaunchS > kStartGraceS ? Phase::Closed : Phase::Starting;
    }
    return o.sinceSeenS > kGoneGraceS ? Phase::Closed : Phase::WaitingForFile;
}

bool Finished(Phase phase) {
    return phase == Phase::Apply || phase == Phase::Closed || phase == Phase::TimedOut;
}

char const* PhaseText(Phase phase) {
    switch (phase) {
    case Phase::Starting: return "Starting Mass Effect 1 (Steam or the EA app may show first)";
    case Phase::WaitingForFile: return "Waiting for Mass Effect 1 to make its settings file (it does on its first start)";
    case Phase::WaitingForQuit:
        return "Mass Effect 1 made its settings file. Quit the game now (main menu > Quit): the setup then applies the VR game settings";
    case Phase::Apply: return "Mass Effect 1 has closed: applying the VR game settings";
    case Phase::Closed:
        return "Mass Effect 1 closed (or never started) without making its settings file. Start it once from Steam or the EA app, quit at its "
               "main menu, then come back: Repair (or Settings > Apply) puts the VR game settings on";
    case Phase::TimedOut:
        return "Stopped waiting. Once you've started Mass Effect 1 once and quit it, Repair (or Settings > Apply) puts the VR game settings on";
    }
    return "";
}

} // namespace first_start

// ---- The VR runtime before the game ------------------------------------------------------------------------------

namespace runtime_start {

fs::path SteamVrStartup(std::string_view manifestPath) {
    if (manifestPath.empty()) {
        return {};
    }
    return fs::path(detect::Wide(manifestPath)).parent_path() / L"bin" / L"win64" / L"vrstartup.exe";
}

Plan PlanFor(std::string_view kind, std::string_view manifestPath) {
    Plan p;
    if (kind.empty()) {
        p.advice = "No OpenXR runtime is active: install SteamVR, the Meta Quest Link app or Virtual Desktop and make it the active runtime, or the "
                   "game starts flat.";
        return p;
    }
    p.action = Action::AskPlayer;
    if (kind == "SteamVR") {
        p.action = Action::StartSteamVr;
        p.exe = SteamVrStartup(manifestPath);
        p.advice = "Launch starts SteamVR first and waits for it (up to a minute): VR has to be up before the game starts, or it stays off for that "
                   "run. Turn the headset on before you press it.";
    } else if (kind == "Meta") {
        p.advice = "Start the Meta Quest Link app and connect the headset (Link cable or Air Link) before you press Launch: VR has to be up before "
                   "the game starts, or it stays off for that run.";
    } else if (kind == "Virtual Desktop") {
        p.advice = "Connect Virtual Desktop first (the Streamer on this PC, the app in the headset), then press Launch: VR has to be up before the "
                   "game starts, or it stays off for that run.";
    } else if (kind == "Windows Mixed Reality") {
        p.advice = "Connect the headset and let Mixed Reality Portal come up before you press Launch: VR has to be up before the game starts, or it "
                   "stays off for that run.";
    } else if (kind == "Pimax") {
        p.advice = "Start Pimax Play and connect the headset before you press Launch: VR has to be up before the game starts, or it stays off for "
                   "that run.";
    } else if (kind == "Varjo") {
        p.advice = "Start Varjo Base and connect the headset before you press Launch: VR has to be up before the game starts, or it stays off for "
                   "that run.";
    } else {
        p.advice = "Start your VR runtime and connect the headset before you press Launch: VR has to be up before the game starts, or it stays off "
                   "for that run.";
    }
    return p;
}

bool SteamVrUp(bool serverRunning, bool compositorRunning) {
    return serverRunning && compositorRunning;
}

Gate Step(bool up, double waitedS, double timeoutS) {
    if (up) {
        return Gate::Go;
    }
    return waitedS >= timeoutS ? Gate::GoTimedOut : Gate::Wait;
}

bool SteamVrRunning() {
    return SteamVrUp(detect::ProcessRunning(L"vrserver.exe"), detect::ProcessRunning(L"vrcompositor.exe"));
}

bool StartSteamVr(Plan const& plan) {
    std::error_code ec;
    if (!plan.exe.empty() && fs::is_regular_file(plan.exe, ec)) {
        std::wstring commandLine = L"\"" + plan.exe.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION process{};
        if (CreateProcessW(plan.exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, plan.exe.parent_path().c_str(), &startup,
                &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return true;
        }
    }
    std::wstring const url = std::wstring(L"steam://rungameid/") + detect::Wide(kSteamVrAppId);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWMINNOACTIVE)) > 32;
}

} // namespace runtime_start

} // namespace mevr::install::launch
