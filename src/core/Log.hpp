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
