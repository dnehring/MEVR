// SPDX-License-Identifier: MIT
#include "install/Launch.hpp"

#include "install/Detect.hpp"
#include "install/Installer.hpp"
#include "install/Manifest.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace mevr::install::launch {
namespace {

namespace fs = std::filesystem;

std::string Win32Error(unsigned long error) {
    wchar_t buffer[2048]{};
    DWORD const n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, buffer,
        static_cast<DWORD>(std::size(buffer)), nullptr);
    std::wstring meaning(buffer, n);
    auto const end = meaning.find_last_not_of(L" \t\r\n");
    meaning.resize(end == std::wstring::npos ? 0 : end + 1);
    return "error " + std::to_string(error) + " (" + (meaning.empty() ? "Windows supplied no description" : detect::Utf8(meaning)) + ")";
}

void LogLaunch(std::string const& line) {
    // Kept outside the game folder: Setup also runs from ASI after ExitProcess, and from a downloaded zip.
    static std::mutex mutex;
    std::lock_guard const lock(mutex);
    OutputDebugStringA(("MEVR Setup launch: " + line + "\n").c_str());
    fs::path const local = LocalDataDir();
    if (local.empty()) {
        return;
    }
    std::error_code ec;
    fs::create_directories(local, ec);
    std::ofstream out(local / L"SetupLaunch.log", std::ios::binary | std::ios::app);
    SYSTEMTIME now{};
    GetSystemTime(&now);
    out << now.wYear << '-' << now.wMonth << '-' << now.wDay << ' ' << now.wHour << ':' << now.wMinute << ':' << now.wSecond
        << " UTC [" << GetCurrentProcessId() << "] " << line << '\n';
}

unsigned long LaunchSteam(SteamLaunchMethod method, std::wstring const& target, std::wstring const& arguments, fs::path const& directory) {
    if (method == SteamLaunchMethod::Process) {
        std::wstring command = L"\"" + target + L"\" " + arguments;
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &process)) {
            return GetLastError();
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return ERROR_SUCCESS;
    }
    // Setup's workers need COM too. An existing MTA (RPC_E_CHANGED_MODE) remains usable for this operation.
    HRESULT const com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";  // honors RUNASADMIN/app compatibility; no forced elevation for ordinary Steam installs
    info.lpFile = target.c_str();
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    info.nShow = SW_SHOWNORMAL;
    BOOL const ok = ShellExecuteExW(&info);
    DWORD const error = ok ? ERROR_SUCCESS : GetLastError();
    if (info.hProcess != nullptr) {
        CloseHandle(info.hProcess);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return error;
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

std::wstring SteamValue(HKEY hive, wchar_t const* name, DWORD view = 0) {
    std::string const label = std::string(hive == HKEY_CURRENT_USER ? "HKCU" : "HKLM") +
        (view == RRF_SUBKEY_WOW6432KEY ? " (32-bit) " : view == RRF_SUBKEY_WOW6464KEY ? " (64-bit) " : " ") + detect::Utf8(name);
    DWORD size = 0;
    DWORD const flags = RRF_RT_REG_SZ | view;
    LSTATUS error = RegGetValueW(hive, L"Software\\Valve\\Steam", name, flags, nullptr, nullptr, &size);
    if (error != ERROR_SUCCESS) {
        LogLaunch("Steam registry " + label + ": " + Win32Error(error));
        return {};
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    error = RegGetValueW(hive, L"Software\\Valve\\Steam", name, flags, nullptr, buffer.data(), &size);
    if (error != ERROR_SUCCESS) {
        LogLaunch("Steam registry " + label + ": " + Win32Error(error));
        return {};
    }
    LogLaunch("Steam registry " + label + "=\"" + detect::Utf8(buffer.data()) + "\"");
    return buffer.data();
}

fs::path SteamExe() {
    auto isFile = [](fs::path const& path) {
        std::error_code ec;
        bool const found = fs::is_regular_file(path, ec);
        LogLaunch("Steam executable candidate \"" + detect::Utf8(path.wstring()) + "\": " +
            (found ? "found" : ec ? Win32Error(static_cast<unsigned long>(ec.value())) : "not a regular file"));
        return found;
    };
    fs::path exe = ResolveSteamExe(SteamValue(HKEY_CURRENT_USER, L"SteamExe"), {}, isFile);
    if (!exe.empty()) {
        return exe;
    }
    // InstallPath is normally machine-wide (32-bit registry view); also accept per-user installations and SteamPath.
    for (auto const& value : {SteamValue(HKEY_CURRENT_USER, L"InstallPath"), SteamValue(HKEY_CURRENT_USER, L"SteamPath"),
             SteamValue(HKEY_LOCAL_MACHINE, L"InstallPath", RRF_SUBKEY_WOW6432KEY),
             SteamValue(HKEY_LOCAL_MACHINE, L"InstallPath", RRF_SUBKEY_WOW6464KEY)}) {
        exe = ResolveSteamExe({}, value, isFile);
        if (!exe.empty()) {
            return exe;
        }
    }
    return {};
}

} // namespace

fs::path NormalizeSteamPath(std::wstring_view raw) {
    auto trim = [](std::wstring_view value) {
        auto const first = value.find_first_not_of(L" \t\r\n");
        return first == std::wstring_view::npos ? std::wstring_view() : value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
    };
    raw = trim(raw);
    if (raw.size() >= 2 && raw.front() == L'"' && raw.back() == L'"') {
        raw = trim(raw.substr(1, raw.size() - 2));
    }
    if (raw.empty() || raw.find(L'"') != std::wstring_view::npos || raw.find(L'\0') != std::wstring_view::npos) {
        return {};
    }
    std::wstring value(raw);
    std::replace(value.begin(), value.end(), L'/', L'\\');
    fs::path path(value);
    return path.is_absolute() ? path.lexically_normal().make_preferred() : fs::path();
}

fs::path ResolveSteamExe(std::wstring_view steamExe, std::wstring_view installPath, std::function<bool(fs::path const&)> const& isFile) {
    fs::path const primary = NormalizeSteamPath(steamExe);
    if (!primary.empty() && isFile(primary)) {
        return primary;
    }
    fs::path const install = NormalizeSteamPath(installPath);
    fs::path const fallback = install.empty() ? fs::path() : install / L"steam.exe";
    return !fallback.empty() && isFile(fallback) ? fallback : fs::path();
}

Result RequestSteamLaunch(fs::path const& steamExe, bool boots, SteamLaunchHooks const& hooks) {
    Result result;
    std::wstring const arguments = boots ? kSteamBootLe1Args : L"-applaunch 1328670";
    // Valve's run/<appid>//<args>/ form (research/33): keep the empty path segment and trailing slash.
    std::wstring const url = boots ? L"steam://run/1328670//-game 1 -autoterminate/" : L"steam://run/1328670";
    std::string failures;
    auto log = [&](std::string const& line) {
        if (hooks.log) {
            hooks.log(line);
        }
    };
    for (auto const method : {SteamLaunchMethod::Process, SteamLaunchMethod::Shell, SteamLaunchMethod::Url}) {
        bool const isUrl = method == SteamLaunchMethod::Url;
        char const* const name = method == SteamLaunchMethod::Process ? "Starting Steam directly"
            : isUrl ? "Opening Steam's game link" : "Opening Steam through Windows";
        if (!isUrl && steamExe.empty()) {
            log(std::string(name) + ": skipped (Steam's executable wasn't found)");
            continue;
        }
        std::wstring const target = isUrl ? url : steamExe.wstring();
        fs::path const directory = isUrl ? fs::path() : steamExe.parent_path();
        log(std::string(name) + ": target=\"" + detect::Utf8(target) + "\" arguments=\"" +
            detect::Utf8(isUrl ? std::wstring() : arguments) + "\" directory=\"" + detect::Utf8(directory.wstring()) + "\"");
        unsigned long const error = hooks.launch(method, target, isUrl ? std::wstring() : arguments, directory);
        log(std::string(name) + (error == ERROR_SUCCESS ? ": request accepted" : ": " + Win32Error(error)));
        if (error == ERROR_SUCCESS) {
            result.started = true;
            result.message = boots ? "Asked Steam to start Mass Effect 1."
                : "Asked Steam to start Mass Effect Legendary Edition: pick Mass Effect 1 in its launcher (Repair lets Launch skip that step).";
            if (isUrl && boots) {
                result.message += " Steam may ask you to confirm the launch options; if its game picker appears, pick Mass Effect 1.";
            }
            return result;
        }
        failures += std::string(name) + ": " + Win32Error(error) + ". ";
    }
    result.message = std::string("Windows couldn't ask Steam to start Mass Effect Legendary Edition. ") +
        (steamExe.empty() ? "Steam's program wasn't found in its saved locations. " : "") + failures +
        "Start Mass Effect Legendary Edition from Steam's library and pick Mass Effect 1; your MEVR installation still applies.";
    return result;
}

bool LauncherBoots(fs::path const& gameRoot) {
    fs::path const dir = gameRoot / L"Game" / L"Launcher";
    std::error_code ec;
    detect::BinkState const state =
        detect::ClassifyBink(detect::ReadIdentity(dir / L"bink2w64.dll"), fs::exists(dir / L"bink2w64_original.dll", ec));
    return state == detect::BinkState::Ours || state == detect::BinkState::LEBinkProxy;
}

Result StartLe1(fs::path const& gameRoot, bool steam) {
    Result r;
    LogLaunch("Launch requested: game=\"" + detect::Utf8(gameRoot.wstring()) + "\" store=" + (steam ? "Steam" : "EA app / direct"));
    Layout const layout{gameRoot};
    fs::path const exe = layout.Win64() / L"MassEffect1.exe";
    std::error_code ec;
    if (!fs::exists(exe, ec)) {
        r.message = "MassEffect1.exe not found";
        LogLaunch(r.message);
        return r;
    }
    LauncherConfig const config = ReadLauncherConfig();
    if (steam) {
        if (!SteamRunning()) {
            LogLaunch("Opening Steam: steam://open/main");
            unsigned long const error = LaunchSteam(SteamLaunchMethod::Url, L"steam://open/main", {}, {});
            LogLaunch(error == ERROR_SUCCESS ? "Opening Steam: request accepted" : "Opening Steam: " + Win32Error(error));
        }
        for (int i = 0; i < 120 && !(SteamRunning() && SteamUserLoggedIn()); ++i) {
            Sleep(500);
        }
        if (!(SteamRunning() && SteamUserLoggedIn())) {
            r.message = "Steam isn't running with a logged-in user; start Steam and try again";
            LogLaunch(r.message);
            return r;
        }
        // Through Steam: started directly, the exe hands its start back to Steam, which then asks the player to confirm
        // the arguments (research/33 section 11).
        fs::path const steamExe = SteamExe();
        bool const boots = LauncherBoots(gameRoot);
        r = RequestSteamLaunch(steamExe, boots, {LaunchSteam, LogLaunch});
        LogLaunch(r.message);
        return r;
    }
    std::wstring commandLine = L"\"" + exe.wstring() + L"\" " + Le1Arguments(config);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    LogLaunch("Starting Mass Effect 1 directly: " + detect::Utf8(commandLine) + " directory=\"" + detect::Utf8(layout.Win64().wstring()) + "\"");
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, layout.Win64().c_str(), &startup, &process)) {
        DWORD const error = GetLastError();
        r.message = "Windows couldn't start Mass Effect 1: " + Win32Error(error) +
            ". Start the game from the EA app; your MEVR installation still applies.";
        LogLaunch(r.message);
        return r;
    }
    r.started = true;
    r.pid = process.dwProcessId;
    r.message = "Started Mass Effect (" + detect::Utf8(Le1Arguments(config)) + ")";
    LogLaunch(r.message);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return r;
}

bool StartThroughSteam() {
    LogLaunch("Opening Steam's game picker: steam://rungameid/1328670");
    unsigned long const error = LaunchSteam(SteamLaunchMethod::Url, L"steam://rungameid/1328670", {}, {});
    LogLaunch(error == ERROR_SUCCESS ? "Opening Steam's game picker: request accepted" : "Opening Steam's game picker: " + Win32Error(error));
    return error == ERROR_SUCCESS;
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

namespace mevr::install::launch {

Result StartLe1WithVr(std::filesystem::path const& gameRoot, bool steam, std::string_view runtimeKind, std::string_view runtimeManifest,
    LaunchHooks const& hooks) {
    namespace rs = runtime_start;
    auto progress = [&](std::string const& step, int percent) {
        if (hooks.progress) {
            hooks.progress(step, percent);
        }
    };
    auto post = [&](std::string const& line, bool warning) {
        if (hooks.post) {
            hooks.post(line, warning);
        }
    };
    auto cancelled = [&] { return hooks.cancelled && hooks.cancelled(); };
    rs::Plan const plan = rs::PlanFor(runtimeKind, runtimeManifest);
    if (plan.action == rs::Action::StartSteamVr && !rs::SteamVrRunning()) {
        progress("Starting SteamVR", 0);
        if (!rs::StartSteamVr(plan)) {
            post("Couldn't start SteamVR: starting the game anyway. If VR stays off, start SteamVR yourself, quit the game and Launch again.", true);
        } else {
            post("Started SteamVR; waiting for it (at most " + std::to_string(static_cast<int>(rs::kSteamVrWaitS)) + " s).", false);
            auto const t0 = std::chrono::steady_clock::now();
            for (;;) {
                double const waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                rs::Gate const gate = rs::Step(rs::SteamVrRunning(), waited);
                if (gate == rs::Gate::Go) {
                    post("SteamVR is up.", false);
                    break;
                }
                bool const stop = cancelled();
                if (gate == rs::Gate::GoTimedOut || stop) {
                    post(stop ? "Stopped waiting for SteamVR: starting the game now. If VR stays off, quit the game once SteamVR is up and Launch again."
                              : "SteamVR isn't up after a minute (is the headset on and connected?): starting the game anyway. If VR stays off, quit the "
                                "game once SteamVR shows the headset and Launch again.",
                        true);
                    break;
                }
                progress("Waiting for SteamVR (turn the headset on)", static_cast<int>(100.0 * waited / rs::kSteamVrWaitS));
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }
    }
    Result const r = StartLe1(gameRoot, steam);
    post(r.message, !r.started);
    return r;
}

} // namespace mevr::install::launch
