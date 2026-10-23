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
    std::ifstream in(AsiDir() / L"MEVR-install.json", std::ios::binary);
    if (!in) {
        gText = "None (the mod wasn't installed with MEVR Setup)";
        return gText;
    }
    std::ostringstream s;
    s << in.rdbuf();
    std::string names;
    if (auto const manifest = json::Parse(s.str())) {
        for (json::Value const& pack : manifest->Get("packs").Items()) {
            names += (names.empty() ? "" : ", ") + pack.String("name", pack.String("id"));
        }
    }
    gText = names.empty() ? "None yet" : names;
    return gText;
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
