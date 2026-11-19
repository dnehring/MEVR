// SPDX-License-Identifier: MIT
// One calibration for the trilogy (core/vr/CalibrationFile, research/25 section 17): the routing of keys, the override
// sections, the writer, the migration's choice, and a calibrated ME1 player's numbers read back the same from the shared
// file as Win32 read them from LE1MEVR.ini.
#include "core/vr/CalibrationFile.hpp"

#include "TestTemp.hpp"

#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/CalibrationFileTests.cpp line %d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace fs = std::filesystem;
namespace cf = mevr::vr::calibration_file;

void WriteText(fs::path const& path, std::string const& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string ReadBytes(fs::path const& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// What the mod read before 2026-11-18 (game/BodyCalibration's Read): Win32's GetPrivateProfileStringW on the game's ini.
std::string Win32Value(fs::path const& path, std::string const& section, std::string const& key) {
    wchar_t text[256]{};
    GetPrivateProfileStringW(cf::Wide(section).c_str(), cf::Wide(key).c_str(), L"", text, 256, path.c_str());
    return cf::Narrow(text);
}

// The owner's kind of ME1 section (values as the host writes them).
std::string const kMe1Section =
    "[Calibration.oculus_touch_controller__meta_quest_3]\r\n"
    "profile=/interaction_profiles/oculus/touch_controller\r\n"
    "system=Meta Quest 3\r\n"
    "label=Touch Controller (Meta Quest 3)\r\n"
    "position=standing\r\n"
    "heading=-12.5\r\n"
    "standEyeM=1.712\r\n"
    "crouchEyeM=1.104\r\n"
    "armLengthM=0.684\r\n"
    "shoulderWidthM=0.402\r\n"
    "handTrimLeft=0.9952 0.0123 -0.0871 0.0402\r\n"
    "handTrimRight=0.9961 -0.0105 0.0790 -0.0377\r\n"
    "eye=right\r\n"
    "pistolTrim=0.9876 0.1234 -0.0456 0.0789\r\n"
    "pistolLine=0.012, 0.034, -0.056\r\n"
    "pistolSpreadDeg=1.25\r\n"
    "pistolEyeReliefM=0.310\r\n"
    "rifleTrim=0.9911 0.1001 -0.0321 0.0654\r\n"
    "rifleLine=0.010, 0.041, -0.072\r\n"
    "rifleSpreadDeg=2.40\r\n"
    "rifleForegrip=-0.031, -0.022, -0.284\r\n"
    "rifleStock=0.050, 0.120, 0.180\r\n"
    "time=2026-11-17 22:50\r\n";

} // namespace

int RunCalibrationFileTests() {
    fs::path const base = mevr::tests::TempRoot() / L"calibration-file";
    std::error_code ec;
    fs::remove_all(base, ec);

    // ---- Which keys are the player's, which the game's.
    CHECK(cf::Shared("Calibration.touch__quest", "standEyeM") && cf::Shared("ManualCalibration.touch__quest", "leftUpM"));
    CHECK(cf::Shared("calibration.touch__quest.LE2", "x") && cf::Shared("Calibration", "prompted") && cf::Shared("calibration", "LAST"));
    CHECK(!cf::Shared("Calibration", "use") && !cf::Shared("Calibration", "eye") && !cf::Shared("Calibration", "grabOffset"));
    CHECK(!cf::Shared("Calibration", "shared") && !cf::Shared("Hands", "handPitch") && !cf::Shared("Calibration.", "x"));
    CHECK(cf::KeyOf("Calibration.touch__quest", cf::kMeasuredPrefix) == "touch__quest");
    CHECK(cf::KeyOf("Calibration.touch__quest.LE2", cf::kMeasuredPrefix).empty() && cf::KeyOf("Hands", cf::kMeasuredPrefix).empty());
    CHECK(cf::KeyOf("ManualCalibration.Touch__Quest", cf::kManualPrefix) == "touch__quest");

    // ---- A game's override: only the keys it names, only for that game.
    {
        mevr::ini::File const f = mevr::ini::Parse("[Calibration.a]\nx = 1\ny = 2\n[Calibration.a.LE2]\ny = 3\n");
        CHECK(cf::Get(f, "LE2", "Calibration.a", "y") == "3" && cf::Get(f, "LE1", "Calibration.a", "y") == "2");
        CHECK(cf::Get(f, "LE2", "Calibration.a", "x") == "1" && !cf::Get(f, "LE2", "Calibration.a", "z"));
        CHECK(cf::Get(f, "", "Calibration.a", "y") == "2");
        mevr::ini::Section const m = cf::Merged(f, "LE2", "Calibration.a");
        CHECK(m.size() == 2 && m.at("x") == "1" && m.at("y") == "3");
        CHECK(cf::Merged(f, "LE3", "Calibration.a").at("y") == "2" && cf::Merged(f, "LE1", "Calibration.b").empty());
    }

    // ---- The lists.
    CHECK(cf::MergeLists("a, b", "B, c,, d ") == "a, b, c, d" && cf::MergeLists("", "x") == "x" && cf::MergeLists("x", "") == "x");

    // ---- The three games' files under one install, this game's first.
    {
        fs::path const exe = fs::path(L"C:\\Games\\MELE\\Game\\ME2\\Binaries\\Win64");
        std::vector<cf::Source> const s = cf::SourcesUnder(exe, "LE2");
        CHECK(s.size() == 3 && s[0].game == "LE2" && s[1].game == "LE1" && s[2].game == "LE3");
        CHECK(s[0].ini == exe / L"ASI" / L"LE2MEVR.ini");
        CHECK(s[1].ini == fs::path(L"C:\\Games\\MELE\\Game\\ME1\\Binaries\\Win64\\ASI\\LE1MEVR.ini"));
        CHECK(s[2].ini == fs::path(L"C:\\Games\\MELE\\Game\\ME3\\Binaries\\Win64\\ASI\\LE3MEVR.ini"));
    }

    // ---- The choice: the newest calibration; this game's on a tie; an untimed section older than any; manual offsets
    //      from the same game, else the first that has them.
    {
        using C = cf::Candidate;
        cf::Choice c = cf::Choose({C{0, "2026-11-07 00:56", true, true}, C{1, "2026-11-10 10:00", true, false}});
        CHECK(c.measured == 1u && c.manual == 0u);
        c = cf::Choose({C{0, "2026-11-10 10:00", true, false}, C{1, "2026-11-10 10:00", true, true}, C{2, "", false, true}});
        CHECK(c.measured == 0u && c.manual == 1u);
        c = cf::Choose({C{0, "", true, false}, C{2, "2025-01-01 00:00", true, true}});
        CHECK(c.measured == 2u && c.manual == 2u);
        c = cf::Choose({C{1, "", false, true}});
        CHECK(!c.measured && c.manual == 1u);
        c = cf::Choose({});
        CHECK(!c.measured && !c.manual);
    }

    // ---- The writer: one move over the file, the other sections kept, no .tmp left.
    {
        fs::path const file = base / L"apply" / L"calibration.ini";
        WriteText(file, "[Calibration]\r\nprompted=a\r\n[Calibration.a]\r\nx=1\r\nold=2\r\n");
        std::string const error = cf::Apply(file, {cf::RemoveSection("Calibration.a"), cf::Set("Calibration.a", "x", "3"),
                                                    cf::Set("Calibration.b", "y", "4")});
        CHECK(error.empty());
        mevr::ini::File const f = mevr::ini::Read(file).file;
        CHECK(mevr::ini::Get(f, "Calibration", "prompted") == "a" && mevr::ini::Get(f, "Calibration.a", "x") == "3");
        CHECK(!mevr::ini::Get(f, "Calibration.a", "old") && mevr::ini::Get(f, "Calibration.b", "y") == "4");
        CHECK(!fs::exists(file.wstring() + L".tmp"));
        // A new file in a new folder.
        fs::path const fresh = base / L"apply-new" / L"MEVR" / L"calibration.ini";
        CHECK(cf::Apply(fresh, {cf::Set("Calibration", "last", "a")}).empty() && Win32Value(fresh, "Calibration", "last") == "a");
        // Nothing to do: no file made.
        fs::path const none = base / L"apply-none" / L"calibration.ini";
        CHECK(cf::Apply(none, {}).empty() && !fs::exists(none));
        // A file there but held so it can't be read: refused, untouched.
        HANDLE const held = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(held != INVALID_HANDLE_VALUE && cf::Probe(file) == cf::Access::Unreadable);
        CHECK(!cf::Apply(file, {cf::Set("Calibration.a", "x", "9")}).empty());
        CloseHandle(held);
        CHECK(Win32Value(file, "Calibration.a", "x") == "3" && cf::Probe(file) == cf::Access::Readable);
        CHECK(cf::Probe(none) == cf::Access::Missing);
    }

    // ---- A calibrated ME1 player: every value in the shared file reads exactly as Win32 read it from LE1MEVR.ini.
    fs::path const root = base / L"install" / L"Game";
    fs::path const le1 = root / L"ME1" / L"Binaries" / L"Win64" / L"ASI" / L"LE1MEVR.ini";
    fs::path const le2 = root / L"ME2" / L"Binaries" / L"Win64" / L"ASI" / L"LE2MEVR.ini";
    fs::path const le3 = root / L"ME3" / L"Binaries" / L"Win64" / L"ASI" / L"LE3MEVR.ini";
    fs::path const shared = base / L"local" / L"MEVR" / L"calibration.ini";
    std::string const me1 = "[Hands]\r\nhandPitch=90\r\n[Calibration]\r\nprompted=oculus_touch_controller__meta_quest_3, "
                            "oculus_touch_controller__meta_quest_pro\r\nlast=oculus_touch_controller__meta_quest_3\r\nuse=1\r\neye=auto\r\n" +
                            kMe1Section +
                            "[ManualCalibration.oculus_touch_controller__meta_quest_3]\r\nheightOffsetM=0.020\r\nrightPitchDeg=-3.000\r\n"
                            "[Calibration.oculus_touch_controller__meta_quest_pro]\r\nposition=seated\r\nstandEyeM=1.200\r\n"
                            "time=2026-11-10 10:45\r\n";
    WriteText(le1, me1);
    std::vector<cf::Source> const fromMe1 = cf::SourcesUnder(root / L"ME1" / L"Binaries" / L"Win64", "LE1");
    {
        cf::Migration const m = cf::Migrate(shared, fromMe1);
        CHECK(m.error.empty() && !m.refused && m.taken == 2 && m.marked);
        std::string const section = "Calibration.oculus_touch_controller__meta_quest_3";
        mevr::ini::File const now = mevr::ini::Read(shared).file;
        int compared = 0;
        for (auto const& [key, value] : cf::SectionLines(le1, section)) {
            std::string const k = cf::Narrow(key);
            CHECK(cf::Get(now, "LE1", section, k) == Win32Value(le1, section, k));
            CHECK(Win32Value(shared, section, k) == Win32Value(le1, section, k));
            ++compared;
        }
        CHECK(compared == 22);
        CHECK(cf::Get(now, "LE1", "ManualCalibration.oculus_touch_controller__meta_quest_3", "rightPitchDeg") == "-3.000");
        CHECK(cf::Get(now, "LE1", "Calibration.oculus_touch_controller__meta_quest_pro", "position") == "seated");
        CHECK(mevr::ini::Get(now, "Calibration", "prompted") ==
              "oculus_touch_controller__meta_quest_3, oculus_touch_controller__meta_quest_pro");
        CHECK(mevr::ini::Get(now, "Calibration", "last") == "oculus_touch_controller__meta_quest_3");
        // The game's settings stay the game's: not copied.
        CHECK(!mevr::ini::Get(now, "Calibration", "use") && !mevr::ini::Get(now, "Calibration", "eye") && !now.contains("Hands"));
        // The names' case kept (a person reads this file).
        CHECK(ReadBytes(shared).find("standEyeM=1.712") != std::string::npos);
        // ME1's file: only the marker added, every section still there for an older build.
        CHECK(Win32Value(le1, "Calibration", "shared") == "1" && Win32Value(le1, section, "standEyeM") == "1.712");
    }
    // A second launch takes nothing and writes nothing.
    {
        std::string const before = ReadBytes(shared);
        std::string const le1Before = ReadBytes(le1);
        cf::Migration const m = cf::Migrate(shared, fromMe1);
        CHECK(m.taken == 0 && !m.marked && m.error.empty() && m.lines.empty());
        CHECK(ReadBytes(shared) == before && ReadBytes(le1) == le1Before);
    }

    // ---- ME2 starts: ME1's calibration is there, nothing taken; ME2's own newer calibration of another type is.
    std::vector<cf::Source> const fromMe2 = cf::SourcesUnder(root / L"ME2" / L"Binaries" / L"Win64", "LE2");
    {
        WriteText(le2, "[Calibration]\r\nprompted=valve_index_controller__steamvr\r\nuse=1\r\n"
                       "[Calibration.oculus_touch_controller__meta_quest_3]\r\nstandEyeM=1.900\r\ntime=2026-11-18 09:00\r\n"
                       "[Calibration.valve_index_controller__steamvr]\r\nstandEyeM=1.650\r\ntime=2026-11-16 12:00\r\n");
        cf::Migration const m = cf::Migrate(shared, fromMe2);
        mevr::ini::File const now = mevr::ini::Read(shared).file;
        CHECK(m.taken == 1 && m.marked);
        // The shared file's section stands: a per-game section is only taken for a type the shared file lacks.
        CHECK(cf::Get(now, "LE2", "Calibration.oculus_touch_controller__meta_quest_3", "standEyeM") == "1.712");
        CHECK(cf::Get(now, "LE2", "Calibration.valve_index_controller__steamvr", "standEyeM") == "1.650");
        CHECK(mevr::ini::Get(now, "Calibration", "prompted") == "oculus_touch_controller__meta_quest_3, "
                                                                 "oculus_touch_controller__meta_quest_pro, valve_index_controller__steamvr");
        // ME1's marked file was not read again: nothing of ME1 was taken twice.
        CHECK(Win32Value(le2, "Calibration", "shared") == "1");
    }

    // ---- The shared file lost (deleted by hand): the markers no longer count, the games' sections are taken again.
    {
        fs::remove(shared, ec);
        cf::Migration const m = cf::Migrate(shared, fromMe2);
        mevr::ini::File const now = mevr::ini::Read(shared).file;
        CHECK(m.taken == 3 && !m.marked);  // ME2's two types and ME1's Quest Pro; marked already
        // The newest of the two Quest 3 calibrations: ME2's (2026-11-18), with ME1's manual offsets (ME2 has none).
        CHECK(cf::Get(now, "LE2", "Calibration.oculus_touch_controller__meta_quest_3", "standEyeM") == "1.900");
        CHECK(cf::Get(now, "LE2", "ManualCalibration.oculus_touch_controller__meta_quest_3", "heightOffsetM") == "0.020");
        CHECK(!cf::Get(now, "LE2", "Calibration.oculus_touch_controller__meta_quest_3", "pistolTrim"));  // a whole section, not merged
    }

    // ---- A shared file that can't be read: nothing taken, nothing marked, refused.
    {
        fs::path const le3Exe = root / L"ME3" / L"Binaries" / L"Win64";
        WriteText(le3, "[Calibration.htc_vive_controller__steamvr]\r\nstandEyeM=1.600\r\ntime=2026-11-01 08:00\r\n");
        std::string const before = ReadBytes(shared);
        HANDLE const held = CreateFileW(shared.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        cf::Migration const m = cf::Migrate(shared, cf::SourcesUnder(le3Exe, "LE3"));
        CHECK(held != INVALID_HANDLE_VALUE && m.refused && m.taken == 0 && !m.marked && !m.lines.empty());
        CloseHandle(held);
        CHECK(ReadBytes(shared) == before && Win32Value(le3, "Calibration", "shared").empty());
        // Once readable, ME3 takes its own.
        cf::Migration const again = cf::Migrate(shared, cf::SourcesUnder(le3Exe, "LE3"));
        CHECK(again.taken == 1 && again.marked && !again.refused);
    }

    // ---- No game has a calibration: no file made, no marker.
    {
        fs::path const lone = base / L"lone" / L"Game";
        fs::path const loneIni = lone / L"ME1" / L"Binaries" / L"Win64" / L"ASI" / L"LE1MEVR.ini";
        WriteText(loneIni, "[Calibration]\r\nuse=1\r\n[Hands]\r\nfingers=1\r\n");
        fs::path const loneShared = base / L"lone-local" / L"calibration.ini";
        cf::Migration const m = cf::Migrate(loneShared, cf::SourcesUnder(lone / L"ME1" / L"Binaries" / L"Win64", "LE1"));
        CHECK(m.taken == 0 && !m.marked && m.lines.empty() && !fs::exists(loneShared));
        CHECK(Win32Value(loneIni, "Calibration", "shared").empty());
    }

    fs::remove_all(base, ec);
    return failures;
}
