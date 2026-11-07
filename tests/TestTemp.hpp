// SPDX-License-Identifier: MIT
// This test run's own temp folder, %TEMP%\mevr-tests-<pid>: every fake game folder and scratch file the tests write goes
// under it, so two runs at once (two sessions' ctest in two worktrees, same %TEMP%) never delete or overwrite each
// other's. Until 2026-11-06 they shared fixed names (%TEMP%\mevr-install-test...) and failed now and then. main()
// removes it at the end.
#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace mevr::tests {

inline std::filesystem::path const& TempRoot() {
    static std::filesystem::path const root = [] {
        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        std::filesystem::path const p =
            std::filesystem::path(temp) / (L"mevr-tests-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ec;
        std::filesystem::remove_all(p, ec);  // a crashed run's, under a reused process id
        std::filesystem::create_directories(p, ec);
        return p;
    }();
    return root;
}

inline void RemoveTempRoot() {
    std::error_code ec;
    std::filesystem::remove_all(TempRoot(), ec);
}

} // namespace mevr::tests
