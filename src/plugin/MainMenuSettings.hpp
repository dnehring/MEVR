// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Colour.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/MenuSettings.hpp"
#include "plugin/menu/MenuModel.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <filesystem>
#include <string>

// What each row of our main menu drives (research/28), behind the data model (plugin/menu/MenuModel):
//   - our settings: <GAME>MEVR.ini next to the host ASI, which the mod re-reads within a second ([Hands], [Holsters],
//     [Controls], [Throwables], [Sights], [Camera], and the menu's own [Menu]): written key by key with
//     WritePrivateProfileString, as the VR hands tab does (a slider's value 300 ms after it last moved), never the
//     whole file;
//   - Halcyon's: its live config (MELEVR::Config::Get()), as its own menu edits it, saved into the active profile
//     with its own SaveProfile when the menu closes (or right away where its menu saves right away: the VR mode, the
//     resolution, depth of field). Never a reset written over a profile: the menu's Reset is for this session only;
//   - the colour (plugin/Colour), the calibration's status and results (plugin/Calibration), the test loop and the
//     frame probe (Diagnostics).
// Present thread.
namespace mevr::host::main_menu {

// What a command asks of the menu itself.
enum class After {
    Nothing,
    Close,          // Resume
    GameStart,      // close, then press the game's Start once it reads the pad again (its pause menu)
    GameBack,       // close, then press the game's Back (on foot: holster; in a bypass minigame: quit it)
    Calibrate,      // close, then start the guided calibration
    Classic,        // close, then open Halcyon's menu
    Align,          // close, then arm the camera alignment (left trigger)
    Snapshot,       // close, then take a snapshot a second later
    Perf,           // close, then measure frame timing
};

class Settings final : public menu::Backend {
public:
    Settings();

    void Read(menu::Values& values) override;
    void Write(std::string const& key, menu::Values const& values, bool final) override;
    void Command(std::string const& name, std::string const& argument, menu::Values& values) override;

    // Each frame while open: due edits written, the arm measurement, timeouts.
    void Tick(vr::controllers::State const& live, bool headValid, XrVector3f const& head);
    // The menu closed: everything pending written, Halcyon's profile saved if it changed.
    void Closed();
    // B: an armed Reset or a waiting arm measurement is called off. False if there was neither.
    bool CancelPending();
    After TakeAfter();

    // [Menu] in <GAME>MEVR.ini.
    bool PauseWanted() const { return pause_; }
    float DistanceM() const { return distance_; }
    float WidthM() const { return width_; }
    std::filesystem::path Assets() const;   // the folder with main.rml: [Menu] assets, else ASI\menu
    std::filesystem::path Fonts() const;    // ASI\fonts
    std::filesystem::path File() const;     // <GAME>MEVR.ini

    // The colour the Display page's adjusted strip shows (the live values, while a slider moves).
    vr::colour::Settings PreviewColour() const { return colour_; }
    // The Display page is open (holding A there compares).
    bool OnDisplayPage(menu::Values const& values) const { return values.Text("section") == "display"; }

private:
    void ReadMenuSection();
    void Put(char const* section, char const* key, std::string const& value, bool now);
    void Flush(bool all);
    void HalcyonChanged(bool saveNow);
    void SaveHalcyon(char const* why);

    vr::menu_settings::PendingWrites pending_;
    bool halcyonDirty_ = false;
    std::uint64_t halcyonChangedMs_ = 0;
    bool noSave_ = false;          // Reset ran: this session's renderer settings aren't saved over the profile
    bool colourDirty_ = false;
    std::uint64_t colourChangedMs_ = 0;
    vr::colour::Settings colour_;
    After after_ = After::Nothing;
    // [Menu]
    bool pause_ = true;
    float distance_ = 1.3f;
    float width_ = 1.45f;
    std::wstring assets_;
    std::uint64_t nextMenuReadMs_ = 0;
    // The arm measurement: arms out to the sides, a trigger pull.
    enum class Measure { Idle, WaitRelease, WaitPull } measure_ = Measure::Idle;
    std::uint64_t measureSinceMs_ = 0;
    std::string measureMsg_;
    std::uint64_t resetArmedMs_ = 0;
    std::string diagMsg_;
    std::string profMsg_;
    std::string modeNote_;
    std::string perfMsg_;
    std::string contentMsg_;
    bool wasAligning_ = false;
    // A write of the settings file failed (a read-only ASI folder or file): the menu's status line says so.
    bool saveFailed_ = false;
    void Wrote(bool ok, std::string_view what);
};

} // namespace mevr::host::main_menu
