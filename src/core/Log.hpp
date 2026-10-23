// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <format>
#include <string_view>

namespace mevr::log {

// Opens <exe dir>/Logs/<fileName>, falling back to %LOCALAPPDATA%/MEVR/<fileName>
// when the game folder isn't writable. Truncates any previous log.
bool Open(std::wstring_view fileName);
void Close();
std::filesystem::path const& Path();

// When set, Write forwards to the sink instead of the file. The hot-reloadable mod DLL uses
// this to log through the host, which owns the file.
using Sink = void (*)(char const* level, char const* message);
void SetSink(Sink sink);

void Write(std::string_view level, std::string_view message);

template <typename... Args>
void Info(std::format_string<Args...> fmt, Args&&... args) {
    Write("info", std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args) {
    Write("warn", std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
    Write("error", std::format(fmt, std::forward<Args>(args)...));
}

} // namespace mevr::log
