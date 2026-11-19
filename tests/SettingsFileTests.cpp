// SPDX-License-Identifier: MIT
// One settings file for the trilogy (core/vr/SettingsKeys, core/vr/SettingsFile, research/25 section 20): the per-game rule,
// the reads with a game's section on top, the writer's routing, the defaults blocks, the file in use, and the migration:
// every key an ME1 file sets reads back the same from the shared file as Win32 read it from LE1MEVR.ini; ME2's own values
// stay ME2's; a change in one game's menu reaches the three; a key changed later in a game's old file applies to that game.
#include "core/vr/SettingsFile.hpp"

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
        std::printf("FAIL tests/SettingsFileTests.cpp line %d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace fs = std::filesystem;
namespace sf = mevr::vr::settings_file;

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

// What every reader used before 2026-11-18: Win32's GetPrivateProfileStringW on the game's own file. nullopt: no key.
std::optional<std::string> Win32Value(fs::path const& path, std::string const& section, std::string const& key) {
    wchar_t text[1024]{};
    DWORD const n = GetPrivateProfileStringW(sf::Wide(section).c_str(), sf::Wide(key).c_str(), L"\x1\x2", text, 1024, path.c_str());
    if (n == 2 && text[0] == 1 && text[1] == 2) {
        return std::nullopt;
    }
    return sf::Narrow(text);
}

// What a reader of the shared file gets for one game (the host's Win32-shaped reader).
std::optional<std::string> Read(fs::path const& path, std::string_view game, std::string const& section, std::string const& key) {
    wchar_t text[1024]{};
    DWORD const n = sf::String(sf::Wide(section).c_str(), sf::Wide(key).c_str(), L"\x1\x2", text, 1024, path.c_str(), game);
    if (n == 2 && text[0] == 1 && text[1] == 2) {
        return std::nullopt;
    }
    return sf::Narrow(text);
}

bool Is(std::optional<std::string> const& v, char const* want) {
    return v && *v == want;
}

bool HasLine(std::vector<std::string> const& lines, std::string_view text) {
    for (std::string const& l : lines) {
        if (l.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// A Mass Effect 1 settings file of the shipped kind (the module's defaults blocks, the menu's keys, the calibration a
// player made before 2026-11-18): comments, the per-game sections, a headset's colour section, a repeated key (Win32 reads
// the first), an empty value, a quoted value, a path. Never the owner's own file.
std::string const kMe1 =
    "[Hands]\r\n"
    "; The hands (research/18): the hand angle around the grip, and the crouch.\r\n"
    "handPitch = 90\r\n"
    "physicalCrouch=1\r\n"
    "crouchBelow=0.75\r\n"
    "steadyIdle=1\r\n"
    "steadyIdle=0\r\n"
    "[Calibration]\r\n"
    "use=1\r\n"
    "prompted=touch__quest3, touch__psvr2\r\n"
    "last=touch__quest3\r\n"
    "shared=1\r\n"
    "[Sights]\r\n"
    "pistol=none\r\n"
    "reticle=game\r\n"
    "sniperRifle = scope auto\r\n"
    "[Performance]\r\n"
    "; The render levers (research/22): measured per game.\r\n"
    "sharePass =5\r\n"
    "ue3QueryWait=\r\n"
    "minScreenRadiusPx =8\r\n"
    "[Upscale]\r\n"
    "method=fsr\r\n"
    "[SystemSettings]\r\n"
    "DynamicShadows=False\r\n"
    "FoliageDrawRadiusMultiplier=0.5\r\n"
    "[Debug]\r\n"
    "requests=1\r\n"
    "[Display]\r\n"
    "eyeSizeSet=1\r\n"
    "steamvrSmoothing=0\r\n"
    "[Menu]\r\n"
    "pause=1\r\n"
    "controlsShown=1\r\n"
    "assets=C:\\Games\\Mass Effect Legendary Edition\\Game\\ME1\\Binaries\\Win64\\ASI\\menu\r\n"
    "[Voice]\r\n"
    "device=Headset Microphone (Oculus Virtual Audio Device)\r\n"
    "model=small\r\n"
    "label=\"quoted value\"\r\n"
    "[Colour.steamvr_openxr_playstation_vr2]\r\n"
    "profile=neutral\r\n"
    "contrast=1.120\r\n"
    "[Body.Shepard]\r\n"
    "BIOG_HMM_ARM_MED_R.MEDb.HMM_ARM_MEDb_MDL=1.6683\r\n"
    "[Calibration.touch__quest3]\r\n"
    "standEyeM=1.712\r\n"
    "time=2026-11-17 22:50\r\n"
    "[ManualCalibration.touch__quest3]\r\n"
    "gunUpM=0.010\r\n"
    "[Cinematics]\r\n"
    "conversations=world\r\n"
    "widthM=2.9\r\n";

// Mass Effect 2's: the same modules' blocks, some values of its own, a section and keys only it has.
std::string const kMe2 =
    "[Display]\r\n"
    "eyeSizeSet=1\r\n"
    "[Hands]\r\n"
    "handPitch = 90\r\n"
    "physicalCrouch=1\r\n"
    "steadyIdle=0\r\n"
    "[Sights]\r\n"
    "pistol=dot\r\n"
    "reticle=duplex\r\n"
    "sniperRifle = scope auto\r\n"
    "[Holsters]\r\n"
    "smgSlot = thighRight\r\n"
    "[Performance]\r\n"
    "umbraQueryWait=0\r\n"
    "[Voice]\r\n"
    "device=\r\n"
    "model=tiny\r\n"
    "[Reload]\r\n"
    "; The reload by hand (the sequels only).\r\n"
    "enabled = 0\r\n"
    "enabledMoved = 1\r\n"
    "[Body.Shepard]\r\n"
    "transient.sfxcompositeskeletalmesh_0=1.7061\r\n";

void TestRule() {
    CHECK(sf::PerGame("Performance", "sharePass") && sf::PerGame("performance", "anything") && sf::PerGame("SystemSettings", "MotionBlur"));
    CHECK(sf::PerGame("Upscale", "method") && sf::PerGame("Debug", "openxrRuntime"));
    CHECK(sf::PerGame("Display", "eyeSizeSet") && !sf::PerGame("Display", "steamvrSmoothing"));
    CHECK(sf::PerGame("Menu", "controlsShown") && sf::PerGame("Menu", "assets") && !sf::PerGame("Menu", "pause"));
    CHECK(sf::PerGame("Body.Shepard", "some.mesh") && sf::PerGame("Calibration", "use") && sf::PerGame("Calibration", "grabOffset"));
    CHECK(sf::PerGame("Arms", "armLengthM") && sf::PerGame("Arms", "measureRequest"));
    CHECK(!sf::PerGame("Calibration", "prompted") && !sf::PerGame("Hands", "physicalCrouch") && !sf::PerGame("Sights", "pistol"));
    CHECK(sf::PerGame("Voice", "modelDir") && !sf::PerGame("Voice", "device") && sf::PerGame("Cockpit", "script", "D:\\x\\cockpit.txt"));
    CHECK(!sf::PerGame("Cockpit", "script", "cockpit\\cockpit.txt") && sf::PerGame("X", "y", "\\\\server\\share"));

    auto const g = sf::SplitGameSection("Hands.LE2");
    CHECK(g && g->base == "Hands" && g->game == "LE2");
    auto const lower = sf::SplitGameSection("body.shepard.le1");
    CHECK(lower && lower->base == "body.shepard" && lower->game == "LE1");
    CHECK(!sf::SplitGameSection("Colour.steamvr_openxr_playstation_vr2") && !sf::SplitGameSection("Body.Shepard") &&
          !sf::SplitGameSection("Hands") && !sf::SplitGameSection(".LE1") && !sf::SplitGameSection("Hands.LE4"));
    CHECK(sf::IsSharedPath(L"C:\\x\\MEVR\\Settings.INI") && !sf::IsSharedPath(L"C:\\x\\ASI\\LE1MEVR.ini"));
    CHECK(sf::TakenCopy(L"C:\\l\\MEVR\\settings.ini", "LE2") == fs::path(L"C:\\l\\MEVR\\settings-taken\\LE2MEVR.ini"));
}

void TestReads(fs::path const& dir) {
    fs::path const file = dir / L"reads" / L"settings.ini";
    WriteText(file, "; header\r\n[Hands]\r\nfingers=1\r\ncrouchBelow=0.75\r\n[Hands.LE2]\r\nfingers=0\r\n[Hands.LE3]\r\nextra=3\r\n"
                    "[Display.LE1]\r\neyeSizeSet=1\r\n[Colour.headset]\r\ngamma=0.85\r\n");
    mevr::ini::File const parsed = mevr::ini::Read(file).file;
    CHECK(sf::Get(parsed, "LE2", "Hands", "fingers") == std::optional<std::string>("0"));
    CHECK(sf::Get(parsed, "LE1", "Hands", "fingers") == std::optional<std::string>("1"));
    CHECK(!sf::Get(parsed, "LE1", "Hands", "extra") && sf::Get(parsed, "LE3", "Hands", "extra") == std::optional<std::string>("3"));
    mevr::ini::File const two = sf::Merged(parsed, "LE2");
    CHECK(mevr::ini::Get(two, "Hands", "fingers") == std::optional<std::string>("0") && !two.contains("Hands.LE2") && !two.contains("Hands.LE3"));
    CHECK(!two.contains("Display") && two.contains("Colour.headset"));
    mevr::ini::File const one = sf::Load(file, "LE1").file;
    CHECK(mevr::ini::Get(one, "Display", "eyeSizeSet") == std::optional<std::string>("1"));  // a game's section without a common one
    CHECK(mevr::ini::Get(sf::Merged(parsed, ""), "Hands", "fingers") == std::optional<std::string>("1"));

    // The Win32-shaped readers.
    CHECK(sf::Int(L"Hands", L"fingers", 7, file.c_str(), "LE2") == 0 && sf::Int(L"Hands", L"fingers", 7, file.c_str(), "LE1") == 1);
    CHECK(sf::Int(L"Hands", L"missing", 7, file.c_str(), "LE2") == 7 && sf::Int(L"Display", L"eyeSizeSet", 0, file.c_str(), "LE1") == 1);
    CHECK(sf::Int(L"Display", L"eyeSizeSet", 0, file.c_str(), "LE2") == 0);
    CHECK(Is(Read(file, "LE3", "Hands", "extra"), "3") && !Read(file, "LE2", "Hands", "extra") && Is(Read(file, "LE2", "Hands", "crouchBelow"), "0.75"));
    auto section = [&](wchar_t const* name, std::string_view game) {
        std::vector<wchar_t> buffer(4096);
        DWORD const n = sf::Section(name, buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str(), game);
        std::vector<std::wstring> lines;
        for (std::size_t at = 0; at < n && buffer[at] != L'\0';) {
            lines.emplace_back(buffer.data() + at);
            at += lines.back().size() + 1;
        }
        return lines;
    };
    CHECK(section(L"Hands", "LE2") == std::vector<std::wstring>({L"fingers=0", L"crouchBelow=0.75"}));
    CHECK(section(L"Hands", "LE1") == std::vector<std::wstring>({L"fingers=1", L"crouchBelow=0.75"}));  // Win32's own answer
    CHECK(section(L"Colour.headset", "LE2") == std::vector<std::wstring>({L"gamma=0.85"}));
    // The cache of the file's games' sections follows a change of the file.
    CHECK(sf::Write(L"Hands", L"crouchBelow", L"0.70", file.c_str(), "LE1"));
    CHECK(Is(Read(file, "LE2", "Hands", "crouchBelow"), "0.70"));
    mevr::ini::File const after = mevr::ini::Read(file).file;
    CHECK(mevr::ini::Get(after, "Hands.LE2", "fingers") == std::optional<std::string>("0"));  // another key's own value stays
}

void TestRoute() {
    mevr::ini::File current;
    current["Hands"]["fingers"] = "1";
    current["Hands.LE2"]["fingers"] = "0";
    current["Hands.LE3"]["fingers"] = "0";
    current["Hands.LE3"]["other"] = "2";
    current["Performance.LE2"]["sharePass"] = "3";
    std::vector<sf::Edit> const out = sf::Route({sf::Set("Hands", "fingers", "1"), sf::Set("Performance", "sharePass", "5"),
                                                  sf::Set("Hands.LE3", "other", "4"), sf::Set("Hands", "new", "x")},
        "LE1", current);
    auto has = [&](char const* section, char const* key, std::optional<char const*> value) {
        for (sf::Edit const& e : out) {
            if (sf::Narrow(e.section) == section && sf::Narrow(e.key) == key &&
                (value ? e.value && sf::Narrow(*e.value) == *value : !e.value)) {
                return true;
            }
        }
        return false;
    };
    CHECK(has("Hands", "fingers", "1") && has("Hands.LE2", "fingers", std::nullopt) && has("Hands.LE3", "fingers", std::nullopt));
    CHECK(has("Performance.LE1", "sharePass", "5") && !has("Performance", "sharePass", "5"));
    CHECK(has("Hands.LE3", "other", "4") && has("Hands", "new", "x") && out.size() == 6);
    // A writer of no game (a tool): every key to [<section>], the games' own taken out as for any shared key.
    std::vector<sf::Edit> const tool = sf::Route({sf::Set("Performance", "sharePass", "5")}, "", current);
    CHECK(tool.size() == 2 && sf::Narrow(tool[0].section) == "Performance" && sf::Narrow(tool[1].section) == "Performance.LE2" && !tool[1].value);
    // A whole section and the games' copies of it.
    CHECK(sf::Route({{L"Hands", L"", std::nullopt}}, "LE1", current).size() == 3);
}

void TestWrites(fs::path const& dir) {
    // Apply to a shared file: a per-game key to the game's section, a shared one to the common section and out of the games'.
    fs::path const file = dir / L"writes" / L"settings.ini";
    WriteText(file, "[Hands]\r\nfingers=1\r\n[Hands.LE2]\r\nfingers=0\r\n");
    CHECK(sf::Apply(file, {sf::Set("Hands", "fingers", "0"), sf::Set("Display", "eyeSizeSet", "1")}, "LE3").empty());
    CHECK(Win32Value(file, "Hands", "fingers") == std::optional<std::string>("0") && !Win32Value(file, "Hands.LE2", "fingers"));
    CHECK(Win32Value(file, "Display.LE3", "eyeSizeSet") == std::optional<std::string>("1") && !Win32Value(file, "Display", "eyeSizeSet"));
    std::error_code ec;
    int leftovers = 0;
    for (auto const& entry : fs::directory_iterator(file.parent_path(), ec)) {
        leftovers += entry.path().extension() == L".tmp" ? 1 : 0;
    }
    CHECK(leftovers == 0);
    // A game's old file in use: written as it is.
    fs::path const old = dir / L"writes" / L"LE1MEVR.ini";
    WriteText(old, "[Display]\r\neyeSizeSet=0\r\n");
    CHECK(sf::Write(L"Display", L"eyeSizeSet", L"1", old.c_str(), "LE1") && Win32Value(old, "Display", "eyeSizeSet") == std::optional<std::string>("1"));
    CHECK(!Win32Value(old, "Display.LE1", "eyeSizeSet"));
    // Removing a key, and a key not there.
    CHECK(sf::Write(L"Hands", L"fingers", nullptr, file.c_str(), "LE1") && !Win32Value(file, "Hands", "fingers"));
    CHECK(sf::Write(L"Hands", L"never", nullptr, file.c_str(), "LE1"));

    // A defaults block: a new shared file starts with the header; a per-game key goes to the game's section, once.
    fs::path const fresh = dir / L"fresh" / L"settings.ini";
    std::string const block = "\n[Performance]\n; levers\nsharePass = 5\nmotionBlurGuard = 1\n[Cover]\n; VR cover.\nprotect = 1\nreachM = 1.2\n";
    CHECK(sf::Append(fresh, block, "LE2").empty());
    std::string const text = ReadBytes(fresh);
    CHECK(text.rfind("; MEVR's settings", 0) == 0 && text.find("; VR cover.") != std::string::npos);
    CHECK(Win32Value(fresh, "Performance.LE2", "sharePass") == std::optional<std::string>("5") && !Win32Value(fresh, "Performance", "sharePass"));
    CHECK(Win32Value(fresh, "Cover", "reachM") == std::optional<std::string>("1.2"));
    CHECK(sf::Write(L"Performance", L"sharePass", L"2", fresh.c_str(), "LE2"));
    CHECK(sf::Append(fresh, "\n[Performance]\nsharePass = 5\n", "LE2").empty());
    CHECK(Win32Value(fresh, "Performance.LE2", "sharePass") == std::optional<std::string>("2"));  // never over a value there
    // A UTF-16 file (an editor saved it so) gets the block in UTF-16: Win32 reads the old keys and the new.
    fs::path const wide = dir / L"utf16" / L"settings.ini";
    std::string utf16 = "\xFF\xFE";
    for (char const c : std::string("[Hands]\r\nfingers=1\r\n")) {
        utf16.push_back(c);
        utf16.push_back('\0');
    }
    WriteText(wide, utf16);
    CHECK(sf::Append(wide, "\r\n[Cover]\r\nprotect=1\r\n", "LE1").empty());
    CHECK(Win32Value(wide, "Hands", "fingers") == std::optional<std::string>("1") && Win32Value(wide, "Cover", "protect") == std::optional<std::string>("1"));
    CHECK(mevr::ini::IsUtf16(mevr::ini::Read(wide).encoding));
}

void TestInUse(fs::path const& dir) {
    fs::path const shared = dir / L"inuse" / L"local" / L"settings.ini";
    fs::path const game = dir / L"inuse" / L"ASI" / L"LE1MEVR.ini";
    CHECK(sf::Decide(shared, game) == shared);  // neither there: the shared file, made by the first write
    WriteText(game, "[Hands]\r\n");
    CHECK(sf::Decide(shared, game) == game);    // not made yet, the old file there: the old file
    WriteText(shared, "[Hands]\r\n");
    CHECK(sf::Decide(shared, game) == shared);
    HANDLE const held = CreateFileW(shared.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(held != INVALID_HANDLE_VALUE && sf::Decide(shared, game) == game);  // there but unreadable: the old file
    // ... and never written over.
    CHECK(!sf::Apply(shared, {sf::Set("Hands", "fingers", "1")}, "LE1").empty());
    CloseHandle(held);
    CHECK(ReadBytes(shared) == "[Hands]\r\n");
    // The host's answer wins.
    wchar_t before[1024]{};
    DWORD const had = GetEnvironmentVariableW(sf::kEnvironment, before, 1024);
    SetEnvironmentVariableW(sf::kEnvironment, L"C:\\elsewhere\\settings.ini");
    CHECK(sf::InUse(game) == fs::path(L"C:\\elsewhere\\settings.ini"));
    SetEnvironmentVariableW(sf::kEnvironment, had > 0 ? before : nullptr);
}

void TestMigration(fs::path const& dir) {
    fs::path const root = dir / L"install" / L"Game";
    fs::path const me1Exe = root / L"ME1" / L"Binaries" / L"Win64";
    fs::path const me2Exe = root / L"ME2" / L"Binaries" / L"Win64";
    fs::path const me3Exe = root / L"ME3" / L"Binaries" / L"Win64";
    fs::path const le1 = me1Exe / L"ASI" / L"LE1MEVR.ini";
    fs::path const le2 = me2Exe / L"ASI" / L"LE2MEVR.ini";
    fs::path const le3 = me3Exe / L"ASI" / L"LE3MEVR.ini";
    fs::path const shared = dir / L"install-local" / L"MEVR" / L"settings.ini";
    WriteText(le1, kMe1);
    WriteText(le2, kMe2);
    fs::path const original1 = dir / L"install-original" / L"LE1MEVR.ini";
    WriteText(original1, kMe1);

    std::vector<sf::Source> const sources = sf::SourcesUnder(me2Exe, "LE2");
    CHECK(sources.size() == 3 && sources[0].game == "LE1" && sources[1].game == "LE2" && sources[1].ini == le2 && sources[2].ini == le3);

    // ---- The first launch after the update (ME2's, here): the shared file made from ME1's, ME2's taken in.
    sf::SyncResult const r = sf::Sync(shared, sources, "LE2");
    CHECK(r.created && r.changed && !r.refused && r.error.empty() && fs::exists(shared));
    CHECK(HasLine(r.lines, "LE1MEVR.ini is the base") && HasLine(r.lines, "LE2MEVR.ini taken"));
    CHECK(HasLine(r.lines, "[Hands] steadyIdle: the three games keep \"1\"; ME2 keeps its own \"0\""));
    CHECK(HasLine(r.lines, "[Sights] pistol: the three games keep \"none\"") && HasLine(r.lines, "[Voice] model:"));
    CHECK(HasLine(r.lines, "LE1MEVR.ini marked [Settings] shared = 1") && HasLine(r.lines, "LE2MEVR.ini marked"));

    // ME1 reads every key its file set, the same as Win32 read it there.
    int compared = 0, differing = 0;
    for (auto const& [sectionW, keys] : sf::ReadLines(original1)) {
        std::string const section = sf::Narrow(sectionW);
        for (auto const& [k, v] : keys) {
            std::string const key = sf::Narrow(k);
            if (sf::Excluded(section, key)) {
                continue;  // the calibration's: calibration.ini since research/25 section 17
            }
            ++compared;
            std::optional<std::string> const before = Win32Value(original1, section, key);
            std::optional<std::string> const now = Read(shared, "LE1", section, key);
            std::optional<std::string> const mod = sf::Get(mevr::ini::Read(shared).file, "LE1", section, key);
            std::optional<std::string> const modBefore = mevr::ini::Get(mevr::ini::Read(original1).file, section, key);
            if (before != now || mod != modBefore) {
                ++differing;
                std::printf("  ME1 [%s] %s: %s before, %s now\n", section.c_str(), key.c_str(), before.value_or("-").c_str(),
                    now.value_or("-").c_str());
            }
        }
    }
    CHECK(compared >= 25 && differing == 0);
    CHECK(Is(Read(shared, "LE1", "Hands", "steadyIdle"), "1"));         // the repeated key: its first line, as Win32
    CHECK(Is(Read(shared, "LE1", "Performance", "ue3QueryWait"), ""));  // an empty value stays an empty value
    CHECK(Is(Read(shared, "LE1", "Voice", "label"), "quoted value"));

    // The calibration stays in calibration.ini: none of it in the settings.
    mevr::ini::File const made = mevr::ini::Read(shared).file;
    CHECK(!made.contains("Calibration.touch__quest3") && !made.contains("ManualCalibration.touch__quest3"));
    CHECK(!mevr::ini::Get(made, "Calibration", "prompted") && !mevr::ini::Get(made, "Calibration", "shared"));
    CHECK(mevr::ini::Get(made, "Calibration.LE1", "use") == std::optional<std::string>("1"));
    // ME1's comments came along; its per-game keys sit in its own sections.
    std::string const text = ReadBytes(shared);
    CHECK(text.find("; The render levers (research/22): measured per game.") != std::string::npos);
    CHECK(text.find("[Performance.LE1]") != std::string::npos && !mevr::ini::Get(made, "Performance", "sharePass"));

    // ME2 goes on as it did: its own values where they differed, its own per-game keys, the keys only it had.
    CHECK(Is(Read(shared, "LE2", "Hands", "steadyIdle"), "0") && Is(Read(shared, "LE2", "Sights", "pistol"), "dot"));
    CHECK(Is(Read(shared, "LE2", "Sights", "reticle"), "duplex") && Is(Read(shared, "LE2", "Voice", "device"), ""));
    CHECK(Is(Read(shared, "LE2", "Performance", "umbraQueryWait"), "0") && !Read(shared, "LE2", "Performance", "sharePass"));
    CHECK(Is(Read(shared, "LE2", "Display", "eyeSizeSet"), "1") && !Read(shared, "LE2", "Menu", "assets"));
    CHECK(Is(Read(shared, "LE2", "Reload", "enabled"), "0") && Is(Read(shared, "LE2", "Holsters", "smgSlot"), "thighRight"));
    CHECK(Is(Read(shared, "LE2", "Body.Shepard", "transient.sfxcompositeskeletalmesh_0"), "1.7061"));
    CHECK(!Read(shared, "LE2", "Body.Shepard", "BIOG_HMM_ARM_MED_R.MEDb.HMM_ARM_MEDb_MDL"));
    // ... and shares what ME1 set and it didn't.
    CHECK(Is(Read(shared, "LE2", "Cinematics", "widthM"), "2.9") && Is(Read(shared, "LE2", "Hands", "crouchBelow"), "0.75"));
    CHECK(Is(Read(shared, "LE2", "Colour.steamvr_openxr_playstation_vr2", "contrast"), "1.120"));
    CHECK(text.find("; The reload by hand (the sequels only).") != std::string::npos);  // a section ME1 lacked, with its comment
    // ME1 now sees what only ME2 had (shared: the same modules' defaults).
    CHECK(Is(Read(shared, "LE1", "Holsters", "smgSlot"), "thighRight") && !Read(shared, "LE1", "Performance", "umbraQueryWait"));
    // ME3, not run yet: the three games' values, none of the others' own.
    CHECK(Is(Read(shared, "LE3", "Sights", "pistol"), "none") && !Read(shared, "LE3", "Display", "eyeSizeSet"));
    CHECK(!Read(shared, "LE3", "Debug", "requests") && !Read(shared, "LE3", "Menu", "controlsShown"));

    // The old files stay, marked; their copies as taken.
    CHECK(Win32Value(le1, "Settings", "shared") == std::optional<std::string>("1") && Win32Value(le1, "Hands", "physicalCrouch"));
    CHECK(fs::exists(sf::TakenCopy(shared, "LE1")) && fs::exists(sf::TakenCopy(shared, "LE2")) && !fs::exists(sf::TakenCopy(shared, "LE3")));

    // ---- Once taken, nothing again.
    std::string const once = ReadBytes(shared);
    sf::SyncResult const again = sf::Sync(shared, sf::SourcesUnder(me1Exe, "LE1"), "LE1");
    CHECK(!again.changed && again.lines.empty() && ReadBytes(shared) == once);

    // ---- A change in any game's menu reaches the three: ME3's menu turns steadyIdle off, ME2's own value goes.
    CHECK(sf::Apply(shared, {sf::Set("Hands", "steadyIdle", "0"), sf::Set("Hands", "physicalCrouch", "0")}, "LE3").empty());
    for (char const* game : {"LE1", "LE2", "LE3"}) {
        CHECK(Is(Read(shared, game, "Hands", "steadyIdle"), "0") && Is(Read(shared, game, "Hands", "physicalCrouch"), "0"));
    }
    CHECK(!Win32Value(shared, "Hands.LE2", "steadyIdle"));
    // A per-game key stays the game's.
    CHECK(sf::Apply(shared, {sf::Set("Display", "eyeSizeSet", "0")}, "LE2").empty());
    CHECK(Is(Read(shared, "LE2", "Display", "eyeSizeSet"), "0") && Is(Read(shared, "LE1", "Display", "eyeSizeSet"), "1"));

    // ---- The inbox: a key changed in this game's old file applies to this game.
    CHECK(WritePrivateProfileStringW(L"Debug", L"openxrRuntime", L"C:\\SteamVR\\steamxr_win64.json", le1.c_str()));
    CHECK(WritePrivateProfileStringW(L"Cinematics", L"widthM", L"3.4", le1.c_str()));
    CHECK(WritePrivateProfileStringW(L"Sights", L"reticle", nullptr, le1.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reload", L"enabled", L"1", le2.c_str()));  // ME2's file: ME2's host takes it
    sf::SyncResult const inbox = sf::Sync(shared, sf::SourcesUnder(me1Exe, "LE1"), "LE1");
    CHECK(inbox.changed && HasLine(inbox.lines, "[Debug] openxrRuntime") && HasLine(inbox.lines, "[Cinematics] widthM = 3.4"));
    CHECK(Is(Read(shared, "LE1", "Debug", "openxrRuntime"), "C:\\SteamVR\\steamxr_win64.json") && !Read(shared, "LE2", "Debug", "openxrRuntime"));
    CHECK(Is(Read(shared, "LE1", "Cinematics", "widthM"), "3.4") && Is(Read(shared, "LE2", "Cinematics", "widthM"), "2.9"));
    CHECK(HasLine(inbox.lines, "[Sights] reticle gone from LE1MEVR.ini") && Is(Read(shared, "LE1", "Sights", "reticle"), "game"));
    CHECK(Is(Read(shared, "LE2", "Reload", "enabled"), "0"));
    // Back to the three games' value: ME1's own goes; a tool's key taken out again: gone.
    CHECK(WritePrivateProfileStringW(L"Cinematics", L"widthM", L"2.9", le1.c_str()));
    CHECK(WritePrivateProfileStringW(L"Debug", L"openxrRuntime", nullptr, le1.c_str()));
    sf::SyncResult const back = sf::Sync(shared, sf::SourcesUnder(me1Exe, "LE1"), "LE1");
    CHECK(back.changed && !Win32Value(shared, "Cinematics.LE1", "widthM") && !Read(shared, "LE1", "Debug", "openxrRuntime"));
    // ME2's host takes its own.
    sf::SyncResult const two = sf::Sync(shared, sf::SourcesUnder(me2Exe, "LE2"), "LE2");
    CHECK(two.changed && Is(Read(shared, "LE2", "Reload", "enabled"), "1") && !Win32Value(shared, "Reload", "enabled").value_or("").empty());

    // ---- A game's old file that appears later (ME3 installed after): taken in the same way, its values its own.
    WriteText(le3, "[Sights]\r\npistol=holo\r\n[Hands]\r\nfingers=1\r\n[Performance]\r\nsharePass=0\r\n");
    sf::SyncResult const late = sf::Sync(shared, sf::SourcesUnder(me3Exe, "LE3"), "LE3");
    CHECK(late.changed && HasLine(late.lines, "LE3MEVR.ini taken") && HasLine(late.lines, "[Sights] pistol"));
    CHECK(Is(Read(shared, "LE3", "Sights", "pistol"), "holo") && Is(Read(shared, "LE1", "Sights", "pistol"), "none"));
    CHECK(Is(Read(shared, "LE3", "Performance", "sharePass"), "0") && Is(Read(shared, "LE1", "Hands", "fingers"), "1"));

    // ---- A shared file that can't be read: nothing taken, nothing written, refused.
    fs::path const other = dir / L"refused" / L"MEVR" / L"settings.ini";
    WriteText(other, "[Hands]\r\n");
    fs::path const fresh1 = dir / L"refused-game" / L"Game" / L"ME1" / L"Binaries" / L"Win64";
    WriteText(fresh1 / L"ASI" / L"LE1MEVR.ini", "[Hands]\r\nfingers=1\r\n");
    HANDLE const held = CreateFileW(other.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    sf::SyncResult const refused = sf::Sync(other, sf::SourcesUnder(fresh1, "LE1"), "LE1");
    CHECK(held != INVALID_HANDLE_VALUE && refused.refused && !refused.changed && !refused.lines.empty());
    CloseHandle(held);
    CHECK(ReadBytes(other) == "[Hands]\r\n" && !Win32Value(fresh1 / L"ASI" / L"LE1MEVR.ini", "Settings", "shared"));
    CHECK(!fs::exists(sf::TakenCopy(other, "LE1")));

    // ---- No old file anywhere (a new install): nothing made; the first write makes it.
    fs::path const none = dir / L"none" / L"MEVR" / L"settings.ini";
    sf::SyncResult const empty = sf::Sync(none, sf::SourcesUnder(dir / L"none-game" / L"Game" / L"ME1" / L"Binaries" / L"Win64", "LE1"), "LE1");
    CHECK(!empty.changed && empty.lines.empty() && !fs::exists(none));

    // ---- A UTF-16 base: the shared file is UTF-16 too, and reads the same.
    fs::path const wideRoot = dir / L"wide" / L"Game" / L"ME1" / L"Binaries" / L"Win64";
    std::string utf16 = "\xFF\xFE";
    for (char const c : std::string("[Hands]\r\nfingers=1\r\n[Performance]\r\nsharePass=3\r\n")) {
        utf16.push_back(c);
        utf16.push_back('\0');
    }
    WriteText(wideRoot / L"ASI" / L"LE1MEVR.ini", utf16);
    fs::path const wideShared = dir / L"wide-local" / L"settings.ini";
    CHECK(sf::Sync(wideShared, sf::SourcesUnder(wideRoot, "LE1"), "LE1").changed);
    CHECK(mevr::ini::IsUtf16(mevr::ini::Read(wideShared).encoding) && Is(Read(wideShared, "LE1", "Hands", "fingers"), "1"));
    CHECK(Is(Read(wideShared, "LE1", "Performance", "sharePass"), "3") && !Read(wideShared, "LE2", "Performance", "sharePass"));
}

} // namespace

int RunSettingsFileTests() {
    fs::path const base = mevr::tests::TempRoot() / L"settings-file";
    std::error_code ec;
    fs::remove_all(base, ec);
    TestRule();
    TestReads(base);
    TestRoute();
    TestWrites(base);
    TestInUse(base);
    TestMigration(base);
    fs::remove_all(base, ec);
    return failures;
}
