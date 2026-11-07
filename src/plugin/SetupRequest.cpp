// SPDX-License-Identifier: MIT
#include "plugin/SetupRequest.hpp"

#include "core/Json.hpp"
#include "core/Log.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace mevr::host::setup_request {
namespace {

namespace fs = std::filesystem;

fs::path AsiDir() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&InstalledPacksText), &self);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(self, path, MAX_PATH);
    return fs::path(path).parent_path();
}

fs::path RequestFile() {
    wchar_t buffer[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    return fs::path(buffer) / L"MEVR" / L"setup.request";
}

// The voice files Setup's record doesn't know: a developer deploy (tools/deploy-asi.ps1) or a hand copy puts the DLL and a
// model folder into ASI\voice, and the recognizer loads from there whatever the record says (2026-11-06: the row said
// "None yet" in a release build whose own log had just loaded the tiny models).
bool VoiceFilesOnDisk(fs::path const& asi) {
    std::error_code ec;
    if (!fs::exists(asi / L"voice" / L"MEVR-Voice.dll", ec)) return false;
    for (auto const& entry : fs::directory_iterator(asi / L"voice" / L"models", ec)) {
        if (entry.is_directory(ec) && entry.path().filename().wstring().ends_with(L"-streaming-en")) return true;
    }
    return false;
}

std::mutex gMutex;
std::string gText = "None yet";
ULONGLONG gNextRead = 0;

} // namespace

std::string InstalledPacksText() {
    std::lock_guard const lock(gMutex);
    ULONGLONG const now = GetTickCount64();
    if (now < gNextRead) {
        return gText;
    }
    gNextRead = now + 2000;
    fs::path const asi = AsiDir();
    std::ifstream in(asi / L"MEVR-install.json", std::ios::binary);
    std::string names;
    bool voiceRecorded = false;
    bool const bySetup = static_cast<bool>(in);
    if (bySetup) {
        std::ostringstream s;
        s << in.rdbuf();
        if (auto const manifest = json::Parse(s.str())) {
            for (json::Value const& pack : manifest->Get("packs").Items()) {
                std::string const id = pack.String("id");
                std::string const name = pack.String("name", id);
                if (id.find("voice") != std::string::npos || name.find("Voice") != std::string::npos) voiceRecorded = true;
                names += (names.empty() ? "" : ", ") + name;
            }
        }
    }
    if (!voiceRecorded && VoiceFilesOnDisk(asi)) {
        names += (names.empty() ? "" : ", ") + std::string("Voice orders (the files are in ASI\\voice, not put there by Setup)");
    }
    if (names.empty()) {
        gText = bySetup ? "None yet" : "None (the mod wasn't installed with MEVR Setup)";
    } else {
        gText = names;
    }
    return gText;
}

bool EyeSizeAtFirstStart() {
    std::ifstream in(AsiDir() / L"MEVR-install.json", std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream s;
    s << in.rdbuf();
    auto const manifest = json::Parse(s.str());
    return manifest && manifest->Bool("eyeSizeAtFirstStart");
}

bool Request(std::string& message) {
    std::error_code ec;
    fs::path const file = RequestFile();
    fs::create_directories(file.parent_path(), ec);
    json::Value request = json::Value::Object();
    request["page"] = "content";
    request["packs"] = json::Value::Array();
    request["pid"] = static_cast<std::uint64_t>(GetCurrentProcessId());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << json::Write(request);
    if (!out) {
        message = "Couldn't write the request.";
        return false;
    }
    bool const setup = fs::exists(AsiDir() / L"MEVR-Setup.exe", ec);
    message = setup ? "MEVR Setup opens on Optional content when you quit the game."
                    : "Quit the game, then run MEVR Setup: it opens on Optional content.";
    log::Info("main menu: optional content requested for after exit ({})", setup ? "the setup starts then" : "no setup in the ASI folder");
    return true;
}

void LaunchIfRequested() {
    std::error_code ec;
    if (!fs::exists(RequestFile(), ec)) {
        return;
    }
    fs::path const setup = AsiDir() / L"MEVR-Setup.exe";
    if (!fs::exists(setup, ec)) {
        return;
    }
    std::wstring command = L"\"" + setup.wstring() + L"\" --request --wait-pid " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(setup.c_str(), command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, setup.parent_path().c_str(), &startup,
            &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        log::Info("started MEVR Setup for the optional content request");
    } else {
        log::Warn("couldn't start MEVR Setup (error {})", GetLastError());
    }
}

} // namespace mevr::host::setup_request
