// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

// The problem report's small files in %LOCALAPPDATA%\MEVR (research/33 section 8), header-only so the host can use them
// without linking the installer library:
//   report.request    the in-game menu asked for a report (the setup creates it, then deletes the request)
//   report.result     the last report the setup wrote: JSON {ok, name, folder, path, error, time}
//   report.last       touched whenever a report is written
//   report.dismissed  touched when the player says no to "The game crashed last session. Create a report?"
// The crash offer: crash-breadcrumbs.txt is newer than both marks.
namespace mevr::install::report_marks {

inline std::filesystem::path LocalData() {
    wchar_t buffer[MAX_PATH]{};
    DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR";
}

inline std::filesystem::path RequestFile(std::filesystem::path const& local) { return local / L"report.request"; }
inline std::filesystem::path ResultFile(std::filesystem::path const& local) { return local / L"report.result"; }
inline std::filesystem::path LastMark(std::filesystem::path const& local) { return local / L"report.last"; }
inline std::filesystem::path DismissedMark(std::filesystem::path const& local) { return local / L"report.dismissed"; }
inline std::filesystem::path CrashFile(std::filesystem::path const& local) { return local / L"crash-breadcrumbs.txt"; }

using Time = std::filesystem::file_time_type;

inline std::optional<Time> Modified(std::filesystem::path const& file) {
    std::error_code ec;
    Time const t = std::filesystem::last_write_time(file, ec);
    if (ec) {
        return std::nullopt;
    }
    return t;
}

// The rule (unit-tested): a crash file newer than the last report and the last "no".
inline bool CrashNewer(std::optional<Time> crash, std::optional<Time> lastReport, std::optional<Time> dismissed) {
    if (!crash) {
        return false;
    }
    if (lastReport && *lastReport >= *crash) {
        return false;
    }
    if (dismissed && *dismissed >= *crash) {
        return false;
    }
    return true;
}

inline bool CrashOffer(std::filesystem::path const& local) {
    if (local.empty()) {
        return false;
    }
    return CrashNewer(Modified(CrashFile(local)), Modified(LastMark(local)), Modified(DismissedMark(local)));
}

// Writes a mark (its time is what counts; the text is for a person reading the folder).
inline bool Touch(std::filesystem::path const& mark, std::string const& text) {
    std::error_code ec;
    std::filesystem::create_directories(mark.parent_path(), ec);
    std::ofstream out(mark, std::ios::binary | std::ios::trunc);
    out << text << "\n";
    out.close();
    // A crash file written in the same clock tick: the mark is never older than what it answers.
    std::filesystem::last_write_time(mark, Time::clock::now(), ec);
    return static_cast<bool>(out);
}

} // namespace mevr::install::report_marks
