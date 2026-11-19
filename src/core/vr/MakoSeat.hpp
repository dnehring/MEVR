// SPDX-License-Identifier: MIT
#pragma once

#include "core/Ini.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string_view>

namespace mevr::vr::mako_seat {

enum class Position { Top, Bottom };
inline constexpr char kTopScript[] = "cockpit\\cockpit.txt";
inline constexpr char kBottomScript[] = "cockpit\\cockpit-v2.txt";

inline std::optional<Position> Parse(std::string_view value) {
    std::string const name = ini::Lower(ini::Trim(value));
    if (name == "top") {
        return Position::Top;
    }
    if (name == "bottom") {
        return Position::Bottom;
    }
    return std::nullopt;
}

inline Position FromScript(std::string_view script) {
    std::string path = ini::Lower(ini::Trim(script));
    std::replace(path.begin(), path.end(), '/', '\\');
    return path == kBottomScript ? Position::Bottom : Position::Top;
}

inline bool BottomAvailable(std::filesystem::path const& asiFolder) {
    std::error_code error;
    return std::filesystem::is_regular_file(asiFolder / kBottomScript, error) &&
           std::filesystem::is_regular_file(
               asiFolder / "../../../BioGame/CookedPCConsole/BIOG_MEVR_CockpitV2.pcc", error);
}

} // namespace mevr::vr::mako_seat
