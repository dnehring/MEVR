// SPDX-License-Identifier: MIT
#pragma once

// The developer channels (docs/release-checklist.md section 1): the request files tools/*.ps1 write into
// %LOCALAPPDATA%\MEVR (the test loop's snapshot, record, replay, perf and eyediff requests; the mod's console.request,
// which runs any console command for any local process) and the classic menu's test-loop section. On in development
// builds, off in the players' build (the monolithic ASI, MEVR_MONOLITHIC); `[Debug] requests = 0 | 1` in the settings
// file overrides either way. The menu's own Snapshot, Record and frame-timing buttons, the End and Home keys and the
// stick-tap snapshot don't go through request files and stay.
//
// The same build flavour sets what the diagnostics may keep on disk (core/DiskBudget): no limit while developing.

#include "core/Log.hpp"

#include <Windows.h>

#include <atomic>
#include <filesystem>

namespace mevr::dev_channels {

#if defined(MEVR_MONOLITHIC)
inline constexpr bool kDevelopmentBuild = false;
#else
inline constexpr bool kDevelopmentBuild = true;
#endif

// What the switch says, given the file's value (-1: no key).
constexpr bool Resolve(int fileValue, bool developmentBuild) {
    return fileValue < 0 ? developmentBuild : fileValue != 0;
}

// `[Debug] <key>` as an integer, -1 when absent (Win32's profile API: the first section and key win, as core/Ini).
inline int ReadDebugInt(std::filesystem::path const& settingsFile, wchar_t const* key) {
    if (settingsFile.empty()) {
        return -1;
    }
    return static_cast<int>(GetPrivateProfileIntW(L"Debug", key, -1, settingsFile.c_str()));
}

// Whether the request files are read. The file is read at most every 2 s; the answer is logged when it changes.
inline bool Allowed(std::filesystem::path const& settingsFile) {
    static std::atomic<ULONGLONG> nextRead{0};
    static std::atomic<int> state{-1};  // -1 unknown, 0 off, 1 on
    ULONGLONG const now = GetTickCount64();
    if (now >= nextRead.load(std::memory_order_relaxed)) {
        nextRead.store(now + 2000, std::memory_order_relaxed);
        int const fileValue = ReadDebugInt(settingsFile, L"requests");
        int const allowed = Resolve(fileValue, kDevelopmentBuild) ? 1 : 0;
        if (state.exchange(allowed, std::memory_order_relaxed) != allowed) {
            log::Info("dev channels: the developer request files (console, snapshot, record, replay, perf, eyediff) are {} "
                      "({}; [Debug] requests = {} turns them {})",
                allowed ? "read" : "ignored",
                fileValue < 0 ? (kDevelopmentBuild ? "a development build" : "the players' build") : "[Debug] requests",
                allowed ? 0 : 1, allowed ? "off" : "on");
        }
    }
    return state.load(std::memory_order_relaxed) == 1;
}

} // namespace mevr::dev_channels
