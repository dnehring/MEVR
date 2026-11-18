// SPDX-License-Identifier: MIT
// The engine's depth of field off for a while (game/DepthOfFieldHold.hpp).
#include "game/DepthOfFieldHold.hpp"

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)  // the sequels' galaxy flow's (ME1's conversation world keeps its own)
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "engine/SettingsFile.hpp"
#include "game/ConsoleRequest.hpp"

#include <filesystem>

namespace mevr::game::depth_of_field {
namespace {

bool InIni() {
    // <exe dir>\..\..\BioGame\Config\BIOEngine.ini's [SystemSettings] (it wins over GamerSettings.ini, research/35).
    std::filesystem::path const ini = engine::SettingsFile().parent_path().parent_path().parent_path().parent_path() / L"BioGame" /
        L"Config" / L"BIOEngine.ini";
    ini::Loaded const loaded = ini::Read(ini);
    auto const v = ini::Get(loaded.file, "SystemSettings", "DepthOfField");
    return !v || ini::Lower(ini::Trim(*v)) != "false";
}

} // namespace

void Hold::Off(char const* why) {
    if (off_) {
        return;
    }
    off_ = true;
    was_ = InIni();
    std::string ignored;
    bool const ok = console_request::RunCommand("scale set DepthOfField True", &ignored) &&
                    console_request::RunCommand("scale set DepthOfField False", &ignored);
    log::Info("{}: depth of field off ({}){}", owner_, why, ok ? "" : " (the console wasn't there: it stays as it is)");
}

void Hold::Back(char const* why) {
    if (!off_) {
        return;
    }
    if (was_) {
        std::string ignored;
        bool const ok = console_request::RunCommand("scale set DepthOfField False", &ignored) &&
                        console_request::RunCommand("scale set DepthOfField True", &ignored);
        if (!ok) {
            return;  // owed: tried again on the next call
        }
        log::Info("{}: depth of field on again ({}; BIOEngine.ini's [SystemSettings])", owner_, why);
    }
    off_ = false;
}

} // namespace mevr::game::depth_of_field
#endif
