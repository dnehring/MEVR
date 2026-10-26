// SPDX-License-Identifier: MIT
#include "plugin/ReportRequest.hpp"

#include "core/Json.hpp"
#include "core/Log.hpp"
#include "install/ReportMarks.hpp"

#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace mevr::host::report_request {
namespace {

namespace fs = std::filesystem;
namespace marks = install::report_marks;

fs::path AsiDir() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&StatusText), &self);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(self, path, MAX_PATH);
    return fs::path(path).parent_path();
}

std::mutex gMutex;
std::atomic<bool> gStarting{false};
std::string gStatus;             // what the menu shows
marks::Time gRequestedAt{};      // the result must be newer than this to be ours
bool gWaiting = false;           // a request is out and its result hasn't come
ULONGLONG gNextRead = 0;
bool gOffer = false;
ULONGLONG gNextOffer = 0;

} // namespace

void Request(bool snapshot, bool perf, std::string& message) {
    if (gStarting.exchange(true)) {
        message = "A report is already being started.";
        return;
    }
    fs::path const setup = AsiDir() / L"MEVR-Setup.exe";
    std::error_code ec;
    bool const haveSetup = fs::exists(setup, ec);
    {
        std::lock_guard const lock(gMutex);
        gRequestedAt = marks::Time::clock::now();
        gWaiting = haveSetup;
        gStatus = haveSetup ? "Writing the report in the background..."
                            : "MEVR Setup isn't in the game's ASI folder: after the game, run MEVR Setup and it opens on Report a problem.";
        message = gStatus;
    }
    // The file and the process start off the render thread: a few milliseconds, but never in a frame.
    std::thread([snapshot, perf, setup, haveSetup] {
        fs::path const local = marks::LocalData();
        std::error_code ec;
        fs::create_directories(local, ec);
        json::Value request = json::Value::Object();
        request["snapshot"] = snapshot;
        request["perf"] = perf;
        request["pid"] = static_cast<std::uint64_t>(GetCurrentProcessId());
        {
            std::ofstream out(marks::RequestFile(local), std::ios::binary | std::ios::trunc);
            out << json::Write(request);
        }
        if (haveSetup) {
            // --game: the Win64 folder (the setup finds the game root above it).
            wchar_t exe[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            std::wstring command = L"\"" + setup.wstring() + L"\" --report --from-game --game \"" + fs::path(exe).parent_path().wstring() + L"\"";
            STARTUPINFOW startup{};
            startup.cb = sizeof startup;
            PROCESS_INFORMATION process{};
            if (CreateProcessW(setup.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS | BELOW_NORMAL_PRIORITY_CLASS,
                    nullptr, setup.parent_path().c_str(), &startup, &process)) {
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                log::Info("report: started MEVR Setup --report (snapshot {}, frame timing {})", snapshot, perf);
            } else {
                DWORD const error = GetLastError();
                log::Warn("report: couldn't start MEVR Setup (error {}); the request waits for its next start", error);
                std::lock_guard const lock(gMutex);
                gWaiting = false;
                gStatus = "Couldn't start MEVR Setup: after the game, run it and it opens on Report a problem.";
            }
        } else {
            log::Info("report: requested; no MEVR Setup in the ASI folder, so it waits for the setup's next start");
        }
        gStarting = false;
    }).detach();
}

std::string StatusText() {
    std::lock_guard const lock(gMutex);
    ULONGLONG const now = GetTickCount64();
    if (!gWaiting || now < gNextRead) {
        return gStatus;
    }
    gNextRead = now + 1000;
    fs::path const local = marks::LocalData();
    auto const written = marks::Modified(marks::ResultFile(local));
    if (!written || *written < gRequestedAt) {
        return gStatus;
    }
    std::ifstream in(marks::ResultFile(local), std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    auto const result = json::Parse(s.str());
    if (!result) {
        return gStatus;  // still being written
    }
    gWaiting = false;
    if (result->Bool("ok")) {
        gStatus = "Saved to " + (result->String("folder") == "Desktop" ? std::string("the Desktop") : result->String("folder")) + ": " +
                  result->String("name") + ". Open it after the game: nothing was sent.";
        gOffer = false;
    } else {
        gStatus = "The report wasn't saved: " + result->String("error", "unknown error") + ". MEVR Setup > Report a problem can try again.";
    }
    log::Info("report: {}", gStatus);
    return gStatus;
}

bool CrashOffer() {
    std::lock_guard const lock(gMutex);
    ULONGLONG const now = GetTickCount64();
    if (now >= gNextOffer) {
        gNextOffer = now + 2000;
        gOffer = !gWaiting && marks::CrashOffer(marks::LocalData());
    }
    return gOffer;
}

void DismissCrash() {
    {
        std::lock_guard const lock(gMutex);
        gOffer = false;
        gNextOffer = GetTickCount64() + 2000;
    }
    std::thread([] { marks::Touch(marks::DismissedMark(marks::LocalData()), "dismissed in the game's menu"); }).detach();
    log::Info("report: the crash offer was dismissed");
}

} // namespace mevr::host::report_request
