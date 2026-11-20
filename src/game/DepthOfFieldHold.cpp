// SPDX-License-Identifier: MIT
// The engine's depth of field off for a while (game/DepthOfFieldHold.hpp).
#include "game/DepthOfFieldHold.hpp"
#include "engine/SettingsFile.hpp"

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)  // the sequels' galaxy flow's (ME1's conversation world keeps its own)
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "game/ConsoleRequest.hpp"
#include "game/WorldDrawn.hpp"

#include <filesystem>

namespace mevr::game::depth_of_field {
namespace {

bool InIni() {
    // <exe dir>\..\..\BioGame\Config\BIOEngine.ini's [SystemSettings] (it wins over GamerSettings.ini, research/35).
    std::filesystem::path const ini = engine::AsiDir().parent_path().parent_path().parent_path() / L"BioGame" /
        L"Config" / L"BIOEngine.ini";
    ini::Loaded const loaded = ini::Read(ini);
    auto const v = ini::Get(loaded.file, "SystemSettings", "DepthOfField");
    return !v || ini::Lower(ini::Trim(*v)) != "false";
}

} // namespace

void Hold::Off(char const* why) {
    if (!off_) {
        off_ = true;
        why_ = why;
        told_ = false;
    }
    Send();
}

void Hold::Back(char const* why) {
    if (off_) {
        off_ = false;
        why_ = why;
        told_ = false;
    }
    Send();
}

void Hold::Step() {
    Send();
}

void Hold::Send() {
    if (off_ == sentOff_) return;
    if (!world_drawn::Now()) {
        if (!told_) {
            told_ = true;
            log::Info("{}: the depth of field {} waits: the world isn't drawn just now ({})", owner_, off_ ? "off" : "back on", why_);
        }
        return;
    }
    std::string ignored;
    if (off_) {
        was_ = InIni();
        bool const ok = console_request::RunCommand("scale set DepthOfField True", &ignored) &&
                        console_request::RunCommand("scale set DepthOfField False", &ignored);
        sentOff_ = true;  // a console not there yet: it stays as it is (as before)
        log::Info("{}: depth of field off ({}){}", owner_, why_, ok ? "" : " (the console wasn't there: it stays as it is)");
        return;
    }
    if (was_) {
        bool const ok = console_request::RunCommand("scale set DepthOfField False", &ignored) &&
                        console_request::RunCommand("scale set DepthOfField True", &ignored);
        if (!ok) return;  // owed: tried again by the next call or Step
        log::Info("{}: depth of field on again ({}; BIOEngine.ini's [SystemSettings])", owner_, why_);
    }
    sentOff_ = false;
}

} // namespace mevr::game::depth_of_field
#endif
