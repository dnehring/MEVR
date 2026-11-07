// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mevr::install::steamvr {
inline constexpr char kApp[] = "steam.app.1328670";
// Pure parsers/edit: invalid input is refused; unrelated JSON bytes are preserved.
std::optional<std::filesystem::path> RuntimePath(std::string_view paths);
std::optional<std::filesystem::path> ConfigPath(std::string_view paths);
std::optional<std::string> EditSmoothing(std::string_view settings, std::string_view app, int value);
struct Result {
    int code = 3;  // 0 success, 1 SteamVR stopped (get), 2 usage, 3 failure
    std::string line;
};
// Only MEVR-Setup calls this. Never initialize OpenVR in the game process.
Result Smoothing(std::string_view command, std::string_view app = kApp);
} // namespace mevr::install::steamvr
