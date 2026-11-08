// SPDX-License-Identifier: MIT
// Offline tests for the installer's logic (research/33): JSON, INI editing that never resets a profile, the manifest
// with backup and restore on a fake game folder under the temp directory, version and loader checks, the launch
// arguments, MEM's IPC lines, the update check's parsing. Called from tests/Main.cpp.

#include "core/Json.hpp"
#include "core/vr/MenuSettings.hpp"
#include "install/Detect.hpp"
#include "install/GameSettings.hpp"
#include "install/Http.hpp"
#include "install/Ini.hpp"
#include "install/Installer.hpp"
#include "install/Launch.hpp"
#include "install/Manifest.hpp"
#include "install/Packs.hpp"
#include "install/Toc.hpp"
#include "install/Updates.hpp"
#include "install/Voice.hpp"
#include "install/SteamVrSettings.hpp"
#include "loader/LoaderCore.hpp"
#include "TestTemp.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;
namespace ini = mevr::install::ini;
namespace json = mevr::json;

int* gInstallFailures = nullptr;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL InstallTests.cpp line %d: %s\n", line, what);
        ++*gInstallFailures;
    }
}
#define ICHECK(expr) Check((expr), #expr, __LINE__)

// An Install / Uninstall / Repair... that failed prints its errors, so a failure says which file and why.
bool Ok(mevr::install::Outcome const& outcome) {
    if (!outcome.ok) {
        for (std::string const& e : outcome.errors) {
            std::printf("  its error: %s\n", e.c_str());
        }
    }
    return outcome.ok;
}

void Write(fs::path const& file, std::string const& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string Read(fs::path const& file) {
    return mevr::install::ReadFile(file).value_or("<missing>");
}

void TestJson() {
    auto const v = json::Parse(R"({"a": [1, 2.5, -3e2], "s": "x\"y\\z\u00e9\ud83d\ude00", "t": true, "n": null, "o": {"k": "v"}})");
    ICHECK(v.has_value());
    if (!v) {
        return;
    }
    ICHECK(v->Get("a").Items().size() == 3);
    ICHECK(v->Get("a").Items()[1].AsNumber() == 2.5);
    ICHECK(v->Get("a").Items()[2].AsNumber() == -300.0);
    ICHECK(v->String("s") == "x\"y\\z\xC3\xA9\xF0\x9F\x98\x80");
    ICHECK(v->Bool("t"));
    ICHECK(v->Get("n").IsNull());
    ICHECK(v->Get("o").String("k") == "v");
    ICHECK(v->Get("missing").IsNull());
    // Round trip.
    auto const again = json::Parse(json::Write(*v));
    ICHECK(again.has_value() && json::Write(*again) == json::Write(*v));
    ICHECK(json::Write(json::Value(42)) == "42\n");
    // Errors.
    std::string error;
    ICHECK(!json::Parse("{\"a\": }", &error) && !error.empty());
    ICHECK(!json::Parse("[1, 2"));
    ICHECK(!json::Parse("{} x"));
    ICHECK(json::Parse("\xEF\xBB\xBF{}").has_value());  // a byte order mark
}

void TestSteamVrSettings() {
    namespace sv = mevr::install::steamvr;
    std::string const paths = R"({"runtime":["","relative",42,"C:\\SteamVR"],"config":["D:\\Steam\\config"],"other":true})";
    ICHECK(sv::RuntimePath(paths) == std::optional<fs::path>(L"C:\\SteamVR"));
    ICHECK(sv::ConfigPath(paths) == std::optional<fs::path>(L"D:\\Steam\\config"));
    ICHECK(!sv::RuntimePath("{}"));
    ICHECK(!sv::RuntimePath(R"({"runtime":"C:\\SteamVR"})"));
    ICHECK(!sv::RuntimePath(R"({"runtime":["relative","C:\\bad\u0000path"]})"));
    ICHECK(!sv::RuntimePath("broken"));
    ICHECK(sv::RuntimePath("\xEF\xBB\xBF" R"({"runtime":["C:\\VR-\u00e9"]})") ==
           std::optional<fs::path>(L"C:\\VR-\u00e9"));

    // Byte-exact except for the target value: CRLF, BOM, escaping, nested values and high-precision numbers survive.
    std::string const original = "\xEF\xBB\xBF{\r\n   \"steam.app.1328670\" : {\r\n      \"motionSmoothingOverride\" : 1,\r\n"
        "      \"resolutionScale\" : 1.234567890123456789\r\n   },\r\n"
        "   \"steam.app.other\" : {\"motionSmoothingOverride\": 2},\r\n"
        "   \"steamvr\" : {\"motionSmoothing\":true,\"nested\":[{},[1,2],\"}\\\"{\"]}\r\n}\r\n";
    std::string expected = original;
    expected.replace(expected.find("Override\" : 1") + 12, 1, "2");
    auto const off = sv::EditSmoothing(original, sv::kApp, 2);
    ICHECK(off && *off == expected);
    if (off) {
        ICHECK(sv::EditSmoothing(*off, sv::kApp, 2) == off); // idempotent
        auto const inherit = sv::EditSmoothing(*off, sv::kApp, 0);
        ICHECK(inherit && json::Parse(*inherit)->Get(sv::kApp).Number("motionSmoothingOverride") == 0);
    }
    for (std::string const text : {"{}", "{\r\n}\r\n", "{\"other\": [1, {\"x\": 2}]}",
                                   "{\"steam.app.1328670\":{}}", "{\"steam.app.1328670\":{\n  \"keep\":true\n}}"}) {
        auto const changed = sv::EditSmoothing(text, sv::kApp, 2);
        ICHECK(changed.has_value());
        auto const parsed = changed ? json::Parse(*changed) : std::nullopt;
        ICHECK(parsed && parsed->Get(sv::kApp).Number("motionSmoothingOverride") == 2);
        if (parsed) {
            auto const before = json::Parse(text);
            for (auto const& [key, value] : before->Members()) {
                if (key != sv::kApp) ICHECK(json::Write(parsed->Get(key)) == json::Write(value));
            }
            ICHECK(sv::EditSmoothing(*changed, sv::kApp, 2) == changed);
        }
    }
    ICHECK(!sv::EditSmoothing("{", sv::kApp, 2));
    ICHECK(!sv::EditSmoothing("[]", sv::kApp, 2));
    ICHECK(!sv::EditSmoothing(R"({"steam.app.1328670":false})", sv::kApp, 2));
    ICHECK(!sv::EditSmoothing(R"({"steam.app.1328670":{},"steam.app.1328670":{}})", sv::kApp, 2));
    ICHECK(!sv::EditSmoothing(R"({"steam.app.1328670":{"motionSmoothingOverride":0,"motionSmoothingOverride":2}})", sv::kApp, 2));
    ICHECK(sv::EditSmoothing(R"({"steam.app.1328670":{"keep":"unchanged"}})", sv::kApp, 2) ==
           std::optional<std::string>(R"({"steam.app.1328670":{"keep":"unchanged", "motionSmoothingOverride" : 2}})"));
    ICHECK(!sv::EditSmoothing("{}", "bad\nkey", 2));
    ICHECK(!sv::EditSmoothing("{}", "", 2));
    ICHECK(!sv::EditSmoothing("{}", sv::kApp, 1)); // no force-on command
    ICHECK(sv::Smoothing("bad-command").code == 2); // usage never loads OpenVR or edits the machine
    ICHECK(sv::Smoothing("off", "bad\nkey").code == 2);
}

void TestIni() {
    std::string const gamer = "[SystemSettings]\r\nResX=1920\r\nResY=1080\r\nUseVSync=True\r\n; a comment\r\n\r\n[HDR]\r\nDesiredRange=DynamicRange_HDR\r\n";
    ICHECK(ini::Get(gamer, "SystemSettings", "ResX") == std::optional<std::string>("1920"));
    ICHECK(ini::Get(gamer, "systemsettings", "usevsync") == std::optional<std::string>("True"));
    ICHECK(!ini::Get(gamer, "SystemSettings", "Missing"));
    ICHECK(!ini::Get(gamer, "Nope", "ResX"));
    std::vector<ini::Edit> edits;
    std::string t = ini::Set(gamer, "SystemSettings", "UseVSync", "False", &edits);
    ICHECK(t.find("UseVSync=False\r\n") != std::string::npos);
    ICHECK(t.find("; a comment") != std::string::npos);
    t = ini::Set(t, "SystemSettings", "MotionBlur", "False", &edits);  // a new key: after the section's last line
    ICHECK(t.find("; a comment\r\nMotionBlur=False\r\n\r\n[HDR]") != std::string::npos);
    t = ini::Set(t, "Brand New", "Key", "1", &edits);  // a new section at the end
    ICHECK(t.find("\r\n\r\n[Brand New]\r\nKey=1\r\n") != std::string::npos);
    ICHECK(edits.size() == 3);
    ICHECK(edits[0].before == std::optional<std::string>("True") && edits[0].after == "False");
    ICHECK(!edits[1].before);
    // Setting the same value again records nothing; changing it again keeps the first "before".
    std::size_t const n = edits.size();
    t = ini::Set(t, "SystemSettings", "UseVSync", "False", &edits);
    ICHECK(edits.size() == n);
    t = ini::Set(t, "SystemSettings", "UseVSync", "0", &edits);
    ICHECK(edits.size() == n && edits[0].before == std::optional<std::string>("True") && edits[0].after == "0");
    // Revert puts back exactly the original.
    ICHECK(ini::Revert(t, edits) == gamer);
    // ...except a value the player changed since.
    std::string changed = ini::Set(t, "SystemSettings", "MotionBlur", "True");
    std::string const reverted = ini::Revert(changed, edits);
    ICHECK(ini::Get(reverted, "SystemSettings", "MotionBlur") == std::optional<std::string>("True"));
    ICHECK(ini::Get(reverted, "SystemSettings", "UseVSync") == std::optional<std::string>("True"));
    // Spaced style ("key = value") is kept for new keys in such a section; a file without a final line break.
    std::string const spaced = "[profile:Default]\nmenuKey = 45\nfpEnabled = 1";
    std::string s2 = ini::Set(spaced, "profile:Default", "menuKey", "220");
    ICHECK(s2 == "[profile:Default]\nmenuKey = 220\nfpEnabled = 1");
    s2 = ini::Set(s2, "profile:Default", "sfr2Metric", "1");
    ICHECK(s2 == "[profile:Default]\nmenuKey = 220\nfpEnabled = 1\nsfr2Metric = 1\n");
    ICHECK(ini::Sections("[a]\nx=1\n[b]\n").size() == 2);
    // A key the player's file repeats (in one section, and in a repeated section): every line is set, and each one's
    // own value comes back on revert (QA 2026-10-26: the second value was lost).
    std::string const dup = "[SystemSettings]\r\nUseVSync=True\r\nResX=1920\r\nUseVSync=Maybe\r\n\r\n[SystemSettings]\r\nUseVSync=1\r\n";
    std::vector<ini::Edit> dupEdits;
    std::string const dupSet = ini::Set(dup, "SystemSettings", "UseVSync", "False", &dupEdits);
    ICHECK(dupEdits.size() == 3 && dupEdits[1].occurrence == 1 && dupEdits[1].before == std::optional<std::string>("Maybe"));
    ICHECK(dupSet.find("UseVSync=True") == std::string::npos && dupSet.find("UseVSync=Maybe") == std::string::npos);
    ICHECK(ini::Revert(dupSet, dupEdits) == dup);
    // The player changes only the second line since: it stays theirs, the others come back.
    std::string const dupMine = ini::Revert(std::string(dupSet).replace(dupSet.find("UseVSync=False", dupSet.find("ResX")), 14, "UseVSync=Mine"), dupEdits);
    ICHECK(dupMine == "[SystemSettings]\r\nUseVSync=True\r\nResX=1920\r\nUseVSync=Mine\r\n\r\n[SystemSettings]\r\nUseVSync=1\r\n");
    // Recorded edits survive the manifest's JSON with their occurrence.
    mevr::install::Manifest m;
    mevr::install::FileEntry f;
    f.path = "x.ini";
    f.action = "edited";
    f.edits = dupEdits;
    m.files.push_back(f);
    auto const back = mevr::install::Manifest::FromJson(m.ToJson());
    ICHECK(back && back->files.size() == 1 && back->files[0].edits == dupEdits);
}

void TestHalcyonIni() {
    std::string const release = ini::HalcyonReleaseIni();
    ICHECK(ini::Sections(release).size() == 6);  // active, hotkeys, four slots
    for (char const* slot : {"profile:Default", "profile:Custom 1", "profile:Custom 2", "profile:Custom 3"}) {
        ICHECK(ini::Get(release, slot, "menuKey") == std::optional<std::string>("220"));
        ICHECK(ini::Get(release, slot, "combatHeadAim") == std::optional<std::string>("0"));
        ICHECK(ini::Get(release, slot, "sfr2Metric") == std::optional<std::string>("1"));
    }
    ICHECK(ini::Get(release, "hotkeys", "enabled") == std::optional<std::string>("0"));
    // The fixes change nothing on our own file.
    ICHECK(ini::FixHalcyonIni(release) == release);
    // A player's file: only the fixed keys change; every other value (a tuned eye distance, a profile's resolution,
    // their sfr2Metric = 0) stays.
    std::string const player =
        "[active]\r\nprofile=Custom 1\r\n\r\n[hotkeys]\r\nenabled=1\r\nprofile0=112\r\n\r\n[profile:Default]\r\nmenuKey = 45\r\n"
        "combatHeadAim = 1\r\nsfr2HalfEyeUU = 2.9\r\nhudPanelFollowDeg = 50.0\r\n\r\n[profile:Custom 1]\r\nmenuKey = 220\r\nsfr2Metric = 0\r\n"
        "sfr2ResX = 2824\r\n";
    std::vector<ini::Edit> edits;
    std::string const fixed = ini::FixHalcyonIni(player, &edits);
    ICHECK(ini::Get(fixed, "hotkeys", "enabled") == std::optional<std::string>("0"));
    ICHECK(ini::Get(fixed, "profile:Default", "menuKey") == std::optional<std::string>("220"));
    ICHECK(ini::Get(fixed, "profile:Default", "combatHeadAim") == std::optional<std::string>("0"));
    ICHECK(ini::Get(fixed, "profile:Default", "hudPanelFollowDeg") == std::optional<std::string>("70.0"));
    ICHECK(ini::Get(fixed, "profile:Default", "sfr2Metric") == std::optional<std::string>("1"));
    ICHECK(ini::Get(fixed, "profile:Default", "sfr2HalfEyeUU") == std::optional<std::string>("2.9"));
    ICHECK(ini::Get(fixed, "profile:Custom 1", "sfr2Metric") == std::optional<std::string>("0"));
    ICHECK(ini::Get(fixed, "profile:Custom 1", "sfr2ResX") == std::optional<std::string>("2824"));
    ICHECK(ini::Get(fixed, "active", "profile") == std::optional<std::string>("Custom 1"));
    // Default: hotkeys, menuKey, combatHeadAim, makoHeadAim (missing: added as 0), the follow, sfr2Metric, and as a slot
    // without sfr2Metric (never written by this mod) firstRunDone, vrMode, fpEnabled, mirrorPresentEvery, depthPopKeyEnabled;
    // Custom 1 (sparse, but ours: it has sfr2Metric): combatHeadAim and makoHeadAim added as 0, firstRunDone, and the three
    // keys whose code default isn't a fresh install's (fpEnabled, mirrorPresentEvery, depthPopKeyEnabled), not vrMode.
    ICHECK(ini::Get(fixed, "profile:Default", "makoHeadAim") == std::optional<std::string>("0"));
    ICHECK(ini::Get(fixed, "profile:Custom 1", "combatHeadAim") == std::optional<std::string>("0"));
    ICHECK(ini::Get(fixed, "profile:Custom 1", "makoHeadAim") == std::optional<std::string>("0"));
    ICHECK(ini::Get(fixed, "profile:Custom 1", "fpEnabled") == std::optional<std::string>("1"));
    ICHECK(!ini::Get(fixed, "profile:Custom 1", "vrMode"));
    ICHECK(edits.size() == 17);
    ICHECK(ini::Revert(fixed, edits) == player);
    // A slot with nothing but a resolution gets the menu key and no head aim (else Halcyon's defaults: Insert, head aim).
    std::string const sparse = "[profile:Custom 2]\r\nsfr2ResX = 2824\r\n";
    std::string const sparseFixed = ini::FixHalcyonIni(sparse);
    ICHECK(ini::Get(sparseFixed, "profile:Custom 2", "menuKey") == std::optional<std::string>("220"));
    ICHECK(ini::Get(sparseFixed, "profile:Custom 2", "combatHeadAim") == std::optional<std::string>("0"));
    ICHECK(ini::Get(sparseFixed, "profile:Custom 2", "makoHeadAim") == std::optional<std::string>("0"));
    ICHECK(sparseFixed.find("sfr2ResX = 2824\r\nmenuKey = 220\r\n") != std::string::npos);  // the slot's own spacing
    // The resolution in every slot that has the keys.
    std::string const res = ini::SetHalcyonResolution(release, 2824, 2880);
    ICHECK(ini::Get(res, "profile:Custom 3", "sfr2ResY") == std::optional<std::string>("2880"));
    ICHECK(ini::Get(res, "profile:Default", "expectedResX") == std::optional<std::string>("2824"));
    ICHECK(ini::Get(res, "profile:Default", "monoResX") == std::optional<std::string>("3840"));
}

// Upgrading from Halcyon's upstream MELE-VR (research/33 section 9): a MELEVR.ini this mod never wrote (no sfr2Metric in a
// slot) gets a fresh install's mode, first person, welcome, mirror rate and depth-pop key in every slot; everything else
// stays; a slot this mod wrote keeps the player's own choices; the edits revert to the file exactly.
void TestUpstreamHalcyonIni() {
    // Upstream's file, in the shape of its release MELEVR.ini after its MELE-VR.bat (AER in one slot, DIBR in another,
    // third person, its welcome not seen, Insert, head aim on, the mirror every frame, the depth-pop key on), with values a
    // player tuned there, LF line endings as Halcyon's own save writes them.
    std::string const upstream =
        "; MELE VR config - written by the in-headset menu.\n\n[active]\nprofile=Custom 1\n\n[hotkeys]\nenabled=1\nprofile0=112\n\n"
        "[profile:Default]\nvrMode = 1\nsfr2ResX = 4096\nsfr2ResY = 4250\nexpectedResX = 3072\nexpectedResY = 3188\nfirstRunDone = 0\n"
        "menuKey = 45\nfpEnabled = 0\ncombatHeadAim = 1\nmakoHeadAim = 1\nmirrorPresentEvery = 1\ndepthPopKeyEnabled = 1\nsfr2HalfEyeUU = 3.4\n"
        "hudPanelWidthM = 1.15\n\n"
        "[profile:Custom 1]\nvrMode = 3\nfirstRunDone = 1\nfpEnabled = 0\nmirrorPresentEvery = 4\nleanGain = 1.5\n";
    std::vector<ini::Edit> edits;
    std::string const fixed = ini::FixHalcyonIni(upstream, &edits);
    std::string const fresh = ini::HalcyonReleaseIni();
    for (char const* slot : {"profile:Default", "profile:Custom 1"}) {
        // What a fresh install writes, key for key.
        for (char const* key : {"vrMode", "firstRunDone", "menuKey", "fpEnabled", "combatHeadAim", "makoHeadAim", "mirrorPresentEvery",
                 "depthPopKeyEnabled", "sfr2Metric"}) {
            ICHECK(ini::Get(fixed, slot, key) == ini::Get(fresh, "profile:Default", key));
        }
    }
    // The player's own values stay: tuning, the resolution, the active profile.
    ICHECK(ini::Get(fixed, "profile:Default", "sfr2HalfEyeUU") == std::optional<std::string>("3.4"));
    ICHECK(ini::Get(fixed, "profile:Default", "hudPanelWidthM") == std::optional<std::string>("1.15"));
    ICHECK(ini::Get(fixed, "profile:Default", "sfr2ResX") == std::optional<std::string>("4096"));
    ICHECK(ini::Get(fixed, "profile:Custom 1", "leanGain") == std::optional<std::string>("1.5"));
    ICHECK(ini::Get(fixed, "active", "profile") == std::optional<std::string>("Custom 1"));
    ICHECK(fixed.find("\r") == std::string::npos);  // the file's own line endings
    // Every edit recorded; reverted, the file is upstream's again byte for byte.
    ICHECK(ini::Revert(fixed, edits) == upstream);
    // Once fixed, the slots are ours (sfr2Metric): a second pass changes nothing.
    std::vector<ini::Edit> again;
    ICHECK(ini::FixHalcyonIni(fixed, &again) == fixed && again.empty());
    // A slot this mod wrote (it has sfr2Metric): Depth (DIBR), third person and the mirror every frame chosen in our menu
    // stay; only the welcome is marked seen.
    std::string const ours = "[profile:Default]\r\nvrMode = 3\r\nfirstRunDone = 0\r\nmenuKey = 220\r\nfpEnabled = 0\r\ncombatHeadAim = 0\r\n"
                             "makoHeadAim = 0\r\nmirrorPresentEvery = 1\r\ndepthPopKeyEnabled = 1\r\nsfr2Metric = 1\r\n";
    std::vector<ini::Edit> oursEdits;
    std::string const oursFixed = ini::FixHalcyonIni(ours, &oursEdits);
    ICHECK(ini::Get(oursFixed, "profile:Default", "vrMode") == std::optional<std::string>("3"));
    ICHECK(ini::Get(oursFixed, "profile:Default", "fpEnabled") == std::optional<std::string>("0"));
    ICHECK(ini::Get(oursFixed, "profile:Default", "mirrorPresentEvery") == std::optional<std::string>("1"));
    ICHECK(ini::Get(oursFixed, "profile:Default", "depthPopKeyEnabled") == std::optional<std::string>("1"));
    ICHECK(ini::Get(oursFixed, "profile:Default", "firstRunDone") == std::optional<std::string>("1"));
    ICHECK(oursEdits.size() == 1);
    // src/halcyon/le1/runtime/MELEVR.ini's shape (the old starting file: no sfr2Metric, Insert, third person): normalised too.
    std::string const old = "[profile:Default]\r\nvrMode = 2\r\nmenuKey = 45\r\nfirstRunDone = 0\r\nfpEnabled = 0\r\ndepthPopKeyEnabled = 1\r\n";
    std::string const oldFixed = ini::FixHalcyonIni(old);
    ICHECK(ini::Get(oldFixed, "profile:Default", "fpEnabled") == std::optional<std::string>("1"));
    ICHECK(ini::Get(oldFixed, "profile:Default", "depthPopKeyEnabled") == std::optional<std::string>("0"));
    ICHECK(ini::Get(oldFixed, "profile:Default", "mirrorPresentEvery") == std::optional<std::string>("2"));
    ICHECK(ini::Get(oldFixed, "profile:Default", "vrMode") == std::optional<std::string>("2"));
}

// The setup's launch decisions (research/33 section 9): the game's first start, the VR runtime before the game.
void TestFirstStartAndRuntime() {
    namespace fst = mevr::install::launch::first_start;
    namespace rs = mevr::install::launch::runtime_start;
    auto next = [](double since, bool running, bool file, double seen) {
        fst::Observation o;
        o.sinceLaunchS = since;
        o.gameRunning = running;
        o.fileExists = file;
        o.sinceSeenS = seen;
        return fst::Next(o);
    };
    ICHECK(next(1, false, false, -1) == fst::Phase::Starting);                           // just launched
    ICHECK(next(fst::kStartGraceS + 1, false, false, -1) == fst::Phase::Closed);         // it never came
    ICHECK(next(30, true, false, 0) == fst::Phase::WaitingForFile);                     // at its intro, no file yet
    ICHECK(next(31, false, false, 1) == fst::Phase::WaitingForFile);                    // gone a moment (a restart through the EA app)
    ICHECK(next(60, false, false, fst::kGoneGraceS + 1) == fst::Phase::Closed);          // gone for good without the file
    ICHECK(next(90, true, true, 0) == fst::Phase::WaitingForQuit);                      // the file is there: the player quits
    ICHECK(next(95, false, true, 2) == fst::Phase::Apply);                              // quit: apply
    ICHECK(next(0, false, true, -1) == fst::Phase::Apply);                              // the file was already there
    ICHECK(next(fst::kFileTimeoutS + 1, true, false, 0) == fst::Phase::TimedOut);
    ICHECK(next(fst::kQuitTimeoutS + 1, true, true, 0) == fst::Phase::TimedOut);
    ICHECK(fst::Finished(fst::Phase::Apply) && fst::Finished(fst::Phase::Closed) && fst::Finished(fst::Phase::TimedOut));
    ICHECK(!fst::Finished(fst::Phase::Starting) && !fst::Finished(fst::Phase::WaitingForFile) && !fst::Finished(fst::Phase::WaitingForQuit));
    for (fst::Phase p : {fst::Phase::Starting, fst::Phase::WaitingForFile, fst::Phase::WaitingForQuit, fst::Phase::Apply, fst::Phase::Closed,
             fst::Phase::TimedOut}) {
        ICHECK(std::string(fst::PhaseText(p)).size() > 10);
    }
    // SteamVR: started and waited for; its starter from the OpenXR manifest's folder.
    std::string const steamManifest = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json";
    rs::Plan const steam = rs::PlanFor("SteamVR", steamManifest);
    ICHECK(steam.action == rs::Action::StartSteamVr);
    ICHECK(steam.exe == fs::path(L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\vrstartup.exe"));
    ICHECK(steam.advice.find("SteamVR") != std::string::npos);
    ICHECK(rs::SteamVrStartup("").empty());
    // The others: the player starts them; the advice names what.
    rs::Plan const meta = rs::PlanFor("Meta", "C:\\Program Files\\Oculus\\Support\\oculus-runtime\\oculus_openxr_64.json");
    ICHECK(meta.action == rs::Action::AskPlayer && meta.exe.empty() && meta.advice.find("Meta Quest Link") != std::string::npos);
    ICHECK(rs::PlanFor("Virtual Desktop", "x").advice.find("Virtual Desktop") != std::string::npos);
    ICHECK(rs::PlanFor("Other", "x").action == rs::Action::AskPlayer);
    ICHECK(rs::PlanFor("", "").action == rs::Action::None && !rs::PlanFor("", "").advice.empty());
    // Up = server and compositor; the wait is bounded and never blocks the launch.
    ICHECK(rs::SteamVrUp(true, true) && !rs::SteamVrUp(true, false) && !rs::SteamVrUp(false, true));
    ICHECK(rs::Step(false, 0) == rs::Gate::Wait);
    ICHECK(rs::Step(false, rs::kSteamVrWaitS - 0.5) == rs::Gate::Wait);
    ICHECK(rs::Step(true, 3) == rs::Gate::Go);
    ICHECK(rs::Step(false, rs::kSteamVrWaitS) == rs::Gate::GoTimedOut);
    ICHECK(rs::Step(false, 5, 4) == rs::Gate::GoTimedOut);
    // Whose game is running: a path under the root (any case, either slash), not a sibling folder with the same prefix.
    namespace d = mevr::install::detect;
    fs::path const root = L"C:\\Games\\Mass Effect Legendary Edition";
    ICHECK(d::PathUnder(L"C:\\Games\\Mass Effect Legendary Edition\\Game\\ME1\\Binaries\\Win64\\MassEffect1.exe", root));
    ICHECK(d::PathUnder(L"c:/games/mass effect legendary edition/Game/Launcher/MassEffectLauncher.exe", root));
    ICHECK(d::PathUnder(L"\\\\?\\C:\\Games\\Mass Effect Legendary Edition\\Game\\ME1\\Binaries\\Win64\\MassEffect1.exe", root));
    ICHECK(d::PathUnder(L"C:\\Games\\Mass Effect Legendary Edition\\x.exe", L"C:\\Games\\Mass Effect Legendary Edition\\"));
    ICHECK(!d::PathUnder(L"C:\\Games\\Mass Effect Legendary Edition 2\\Game\\ME1\\Binaries\\Win64\\MassEffect1.exe", root));
    ICHECK(!d::PathUnder(L"D:\\Other\\MassEffect1.exe", root));
    ICHECK(!d::PathUnder(root, root));
    ICHECK(!d::PathUnder(L"", root));
}

void TestGameSettings() {
    namespace st = mevr::install::settings;
    namespace ms = mevr::vr::menu_settings;
    // The ladder matches the in-game menu's.
    for (char const* tier : {"low", "performance", "balanced", "sharp", "max"}) {
        st::Target const t = st::TargetFor(tier, 0, 0);
        ICHECK(t.width == ms::QualityWidth(tier) && t.height == ms::EyeHeight(t.width));
    }
    ICHECK(st::TargetFor("performance", 0, 0).height == 3188);
    ICHECK(st::TargetFor("auto", 0, 0).width == 3072 && st::TargetFor("auto", 0, 0).height == 3188);  // the stand-in until the first VR start
    ICHECK(st::TargetFor("headset", 2824, 2880).width == 2400 && st::TargetFor("headset", 2824, 2880).height == 2448);  // 0.85, FSR
    ICHECK(st::TargetFor("headset", 2064, 2208).width == 1754 && st::TargetFor("headset", 2064, 2208).height == 1876);
    ICHECK(st::TargetFor("headset", 0, 0).width == 0);
    ICHECK(st::TargetFor("keep", 1, 1).width == 0);
    std::string const original = "[SystemSettings]\r\nResX=2560\r\nResY=1440\r\nFullscreen=True\r\nUseVSync=True\r\nMotionBlur=True\r\n"
                                 "FilmGrain=True\r\n\r\n[HDR]\r\nDesiredRange=DynamicRange_HDR\r\n";
    st::Choices choices;
    choices.target = st::TargetFor("performance", 0, 0);
    std::vector<ini::Edit> edits;
    std::string const applied = st::ApplyGamerSettings(original, choices, &edits);
    st::Current const now = st::Read(applied);
    ICHECK(now.resX == 3072 && now.resY == 3188);
    ICHECK(now.borderless && !now.vsync && !now.motionBlur && !now.filmGrain && !now.hdr);
    ICHECK(ini::Get(applied, "SystemSettings", "Fullscreen") == std::optional<std::string>("False"));
    ICHECK(ini::Revert(applied, edits) == original);
    // "keep": the resolution stays.
    st::Choices keep;
    ICHECK(st::Read(st::ApplyGamerSettings(original, keep, nullptr)).resX == 2560);
    std::string const engine = "[Engine.Engine]\r\nbSmoothFrameRate=TRUE\r\n[Engine.GameEngine]\r\nSmoothFrameRate=TRUE\r\nMaxSmoothedFrameRate=62\r\n";
    std::vector<ini::Edit> engineEdits;
    std::string const e2 = st::ApplyEngineSettings(engine, choices, &engineEdits);
    ICHECK(ini::Get(e2, "Engine.GameEngine", "SmoothFrameRate") == std::optional<std::string>("FALSE"));
    ICHECK(ini::Get(e2, "Engine.Engine", "bSmoothFrameRate") == std::optional<std::string>("TRUE"));
    ICHECK(ini::Sections(e2).size() == 2);  // no [SystemSettings] of ours in a file that has none
    ICHECK(ini::Revert(e2, engineEdits) == engine);
    // BIOEngine.ini's [SystemSettings]: the game's copy of its live settings, which it rewrites during play with the VR values
    // (research/33 section 13: Uninstall left ResX/ResY there). Setup records those keys in the game's spelling, so after the
    // game's rewrite (the same text) Revert gives the player's file back; UseVsync and [HDR], which the game doesn't copy, stay.
    std::string const bioEngine =
        "[Engine.GameEngine]\r\nSmoothFrameRate=TRUE\r\n\r\n[SystemSettings]\r\nDynamicShadows=True\r\nMotionBlur=True\r\nUseVsync=True\r\n"
        "Fullscreen=False\r\nMaxAnisotropy=4\r\nResX=2118\r\nResY=2160\r\nFoliageDrawRadiusMultiplier=1.000000\r\n\r\n[HDR]\r\n"
        "DesiredRange=DynamicRange_Auto\r\n";
    std::vector<ini::Edit> bioEdits;
    std::string const played = st::ApplyEngineSettings(bioEngine, choices, &bioEdits);
    ICHECK(ini::Get(played, "SystemSettings", "ResX") == std::optional<std::string>("3072"));
    ICHECK(ini::Get(played, "SystemSettings", "ResY") == std::optional<std::string>("3188"));
    ICHECK(ini::Get(played, "SystemSettings", "DynamicShadows") == std::optional<std::string>("False"));
    ICHECK(ini::Get(played, "SystemSettings", "MotionBlur") == std::optional<std::string>("False"));
    ICHECK(ini::Get(played, "SystemSettings", "MaxAnisotropy") == std::optional<std::string>("16"));
    ICHECK(ini::Get(played, "SystemSettings", "FoliageDrawRadiusMultiplier") == std::optional<std::string>("0.500000"));
    ICHECK(ini::Get(played, "SystemSettings", "UseVsync") == std::optional<std::string>("True"));
    ICHECK(ini::Get(played, "HDR", "DesiredRange") == std::optional<std::string>("DynamicRange_Auto"));
    ICHECK(ini::Get(played, "Engine.GameEngine", "SmoothFrameRate") == std::optional<std::string>("FALSE"));
    ICHECK(ini::Revert(played, bioEdits) == bioEngine);
    // Motion blur stays recorded when the player keeps it in GamerSettings: the mod's `scale set` turns it off anyway, and the
    // game copies that. "keep" leaves the resolution alone; the player changing a value since keeps theirs.
    st::Choices keepBlur;
    keepBlur.motionBlurOff = false;
    std::vector<ini::Edit> keepEdits;
    std::string const kept = st::ApplyEngineSettings(bioEngine, keepBlur, &keepEdits);
    ICHECK(ini::Get(kept, "SystemSettings", "MotionBlur") == std::optional<std::string>("False"));
    ICHECK(ini::Get(kept, "SystemSettings", "ResX") == std::optional<std::string>("2118"));
    std::string const changed = ini::Set(kept, "SystemSettings", "FoliageDrawRadiusMultiplier", "0.750000");
    ICHECK(ini::Get(ini::Revert(changed, keepEdits), "SystemSettings", "FoliageDrawRadiusMultiplier") == std::optional<std::string>("0.750000"));
    ICHECK(ini::Get(ini::Revert(changed, keepEdits), "SystemSettings", "DynamicShadows") == std::optional<std::string>("True"));

    // What the mod writes after the install (research/33 section 13): the first VR start's eye size and our menu's resolution
    // go through Halcyon's profile into GamerSettings.ini (the game copies them into BIOEngine.ini), Halcyon's depth of field
    // at every launch. Uninstall puts the player's values back over those (ModWrites), never over the player's own.
    std::string const halcyon = "[profile:Default]\nsfr2ResX = 2824\nsfr2ResY = 2880\nmonoResX = 3840\nmonoResY = 2160\nexpectedResX = 0\n";
    ini::Ours const mod = st::ModWrites(halcyon);
    st::Choices autoChoice;
    autoChoice.target = st::TargetFor("auto", 0, 0);
    autoChoice.eyeSizeAtFirstStart = true;
    std::vector<ini::Edit> autoEdits;
    std::string const installed = st::ApplyGamerSettings(original, autoChoice, &autoEdits);
    std::string afterPlay = ini::Set(ini::Set(installed, "SystemSettings", "ResX", "2824"), "SystemSettings", "ResY", "2880");
    afterPlay = ini::Set(afterPlay, "SystemSettings", "DepthOfField", "True");  // Halcyon adds the line when it's missing
    ICHECK(ini::Revert(afterPlay, autoEdits) != original);  // the gap: the eye size and the line stayed
    ICHECK(ini::Revert(afterPlay, autoEdits, mod) == original);
    // A resolution the player chose in the game's own options (none of Halcyon's) stays theirs.
    std::string const own = ini::Set(installed, "SystemSettings", "ResX", "1920");
    ICHECK(ini::Get(ini::Revert(own, autoEdits, mod), "SystemSettings", "ResX") == std::optional<std::string>("1920"));
    // "keep": ResX/ResY and DepthOfField recorded as they are, nothing changed for them; the mod's later values put back.
    std::string const withDof = "[SystemSettings]\r\nResX=2560\r\nResY=1440\r\nDepthOfField=False\r\n\r\n[HDR]\r\nDesiredRange=DynamicRange_SDR\r\n";
    std::vector<ini::Edit> keepResEdits;
    std::string const keptInstall = st::ApplyGamerSettings(withDof, keep, &keepResEdits);
    ICHECK(ini::Get(keptInstall, "SystemSettings", "ResX") == std::optional<std::string>("2560"));
    ICHECK(ini::Get(keptInstall, "SystemSettings", "DepthOfField") == std::optional<std::string>("False"));
    std::string modded = ini::Set(ini::Set(keptInstall, "SystemSettings", "ResX", "3840"), "SystemSettings", "ResY", "2160");
    modded = ini::Set(modded, "SystemSettings", "DepthOfField", "True");
    ICHECK(ini::Revert(modded, keepResEdits, mod) == withDof);
    ICHECK(ini::Revert(keptInstall, keepResEdits, mod) == withDof);  // nothing written since: the same file
    // Only [SystemSettings]; a value never in Halcyon's profiles isn't the mod's; "0" (expectedResX unset) never is.
    ICHECK(!mod(ini::Edit{"Other", "ResX", std::string("1"), "1", 0}, "2824"));
    ICHECK(!mod(ini::Edit{"SystemSettings", "ResX", std::string("1"), "1", 0}, "0"));
    ICHECK(mod(ini::Edit{"SystemSettings", "resy", std::string("1"), "1", 0}, "2160"));
    ICHECK(!st::ModWrites("")(ini::Edit{"SystemSettings", "ResX", std::string("1"), "1", 0}, "2824"));
    // ini::Record: once per key, an earlier edit wins.
    std::vector<ini::Edit> recorded;
    ini::Record(withDof, "SystemSettings", "ResX", &recorded);
    ini::Record(withDof, "SystemSettings", "ResX", &recorded);
    ini::Record(withDof, "SystemSettings", "Missing", &recorded);
    ICHECK(recorded.size() == 2 && recorded[0].before == std::optional<std::string>("2560") && recorded[0].after == "2560" &&
           !recorded[1].before && recorded[1].after.empty());
}

void TestDetect() {
    namespace d = mevr::install::detect;
    std::string const vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t\t\"apps\"\n\t\t{\n"
                            "\t\t\t\"1328670\"\t\t\"123\"\n\t\t}\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n}\n";
    auto const libs = d::SteamLibraries(vdf);
    ICHECK(libs.size() == 2);
    if (libs.size() == 2) {
        ICHECK(libs[0] == fs::path(L"C:\\Program Files (x86)\\Steam"));
        ICHECK(libs[1] == fs::path(L"D:\\SteamLibrary"));
    }
    ICHECK(d::AppManifestInstallDir("\"AppState\"\n{\n\t\"appid\"\t\t\"1328670\"\n\t\"installdir\"\t\t\"Mass Effect Legendary Edition\"\n}\n") ==
        std::optional<std::string>("Mass Effect Legendary Edition"));
    ICHECK(!d::AppManifestInstallDir("{}"));
    // Versions.
    ICHECK(d::ParseVersion("2.0.0.48602") == std::optional<mevr::ModuleVersion>(mevr::kSupportedGameVersion));
    ICHECK(!d::ParseVersion("2.0.0"));
    ICHECK(!d::ParseVersion("2.0.0.x"));
    ICHECK(d::CheckVersion(d::ParseVersion("2.0.0.48602")) == d::VersionCheck::Supported);
    ICHECK(d::CheckVersion(d::ParseVersion("2.0.0.48603")) == d::VersionCheck::Different);
    ICHECK(d::CheckVersion(std::nullopt) == d::VersionCheck::Missing);
    // The loader in front of the game.
    d::DllIdentity rad{true, "RAD Game Tools, Inc.", "Bink and Smacker", "RAD Video Tools"};
    d::DllIdentity lebp{true, "ME3Tweaks", "LEBinkProxy (RELEASE)", "A multifunctional proxy DLL"};
    d::DllIdentity ours{true, "MEVR", "MEVR ASI loader", "bink2w64.dll proxy"};  // up to 2026-11-05
    d::DllIdentity oursSigned{true, "MEVR", "MEVR", "MEVR ASI loader (bink2w64.dll proxy)"};  // one product name (code signing)
    d::DllIdentity other{true, "Someone", "Some proxy", ""};
    ICHECK(d::ClassifyBink(rad, false) == d::BinkState::Original);
    ICHECK(d::ClassifyBink(rad, true) == d::BinkState::Original);
    ICHECK(d::ClassifyBink(lebp, true) == d::BinkState::LEBinkProxy);
    ICHECK(d::ClassifyBink(lebp, false) == d::BinkState::Broken);
    ICHECK(d::ClassifyBink(ours, true) == d::BinkState::Ours);
    ICHECK(d::ClassifyBink(oursSigned, true) == d::BinkState::Ours);
    ICHECK(d::ClassifyBink(other, true) == d::BinkState::OtherProxy);
    ICHECK(d::ClassifyBink(d::DllIdentity{}, true) == d::BinkState::Missing);
    // OpenXR runtimes.
    ICHECK(d::ClassifyRuntime("C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json", "") == "SteamVR");
    ICHECK(d::ClassifyRuntime("C:\\Program Files\\Oculus\\Support\\oculus-runtime\\oculus_openxr_64.json", "") == "Meta");
    ICHECK(d::ClassifyRuntime("C:\\Program Files\\VirtualDesktopXR\\virtualdesktop-openxr.json", "") == "Virtual Desktop");
    ICHECK(d::ClassifyRuntime("C:\\WINDOWS\\system32\\MixedRealityRuntime.json", "") == "Windows Mixed Reality");
    ICHECK(d::ClassifyRuntime("C:\\x\\runtime.json", "SteamVR") == "SteamVR");
    ICHECK(d::ClassifyRuntime("C:\\x\\something.json", "") == "Other");
    ICHECK(d::ClassifyRuntime("", "") == "");
    // The headset's size from Halcyon's log: the last session's line.
    std::string const log = "2026-10-22 | [XR] recommended eye image 2064x2096; game render source ...\r\n"
                            "2026-10-23 00:16:39.327 | [XR] recommended eye image 2824x2880; game render source 3072x3188; using eye\r\n";
    ICHECK(d::EyeSizeFromHalcyonLog(log) == std::optional<d::EyeSize>(d::EyeSize{2824, 2880}));
    ICHECK(!d::EyeSizeFromHalcyonLog("nothing here"));
    ICHECK(!d::EyeSizeFromHalcyonLog("recommended eye image 0x0"));
}

void TestLaunch() {
    namespace l = mevr::install::launch;
    l::LauncherConfig const c = l::ParseLauncherConfig("EnglishVOEnabled=true\r\nLanguage=de_DE\r\nSubtitleSize=26\r\n");
    ICHECK(c.language == "de_DE" && c.englishVoices && c.subtitleSize == 26);
    ICHECK(l::Le1Language(c) == "GE");
    ICHECK(l::Le1Language(l::ParseLauncherConfig("Language=de_DE\n")) == "DE");
    ICHECK(l::Le1Language(l::ParseLauncherConfig("")) == "INT");
    ICHECK(l::Le1Language(l::ParseLauncherConfig("Language=xx_XX")) == "INT");
    ICHECK(l::Le1Arguments(l::LauncherConfig{}) == L"-NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles 20 -OVERRIDELANGUAGE=INT");
    ICHECK(l::Le1Arguments(c) == L"-NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles 26 -OVERRIDELANGUAGE=GE");
    // The launcher's command line as our loader's autoboot reads it (research/33 section 12): what Steam passes for
    // `-applaunch 1328670 -game 1 -autoterminate`, and what the EA app or a player's shortcut might add around it.
    l::LauncherBoot const boot = l::ParseLauncherArgs({L"-game", L"1", L"-autoterminate"});
    ICHECK(boot.game == 1 && boot.autoTerminate);
    ICHECK(l::ParseLauncherArgs({L"-GAME", L"1"}).game == 1 && !l::ParseLauncherArgs({L"-GAME", L"1"}).autoTerminate);
    ICHECK(l::ParseLauncherArgs({L"-game=2"}).game == 2);
    ICHECK(l::ParseLauncherArgs({L"-x", L"-game", L"3", L"-y"}).game == 3);
    ICHECK(!l::ParseLauncherArgs({}).game && !l::ParseLauncherArgs({}).autoTerminate);   // the player's own start: the picker
    ICHECK(!l::ParseLauncherArgs({L"-game"}).game);                                       // no number
    ICHECK(!l::ParseLauncherArgs({L"-game", L"4"}).game);                                 // not a Legendary Edition game
    ICHECK(!l::ParseLauncherArgs({L"-game", L"12"}).game && !l::ParseLauncherArgs({L"-game", L"x"}).game);
    ICHECK(l::ParseLauncherArgs({L"-autoterminate"}).autoTerminate && !l::ParseLauncherArgs({L"-autoterminate"}).game);
    ICHECK(std::wstring(l::kSteamBootLe1Args) == L"-applaunch 1328670 -game 1 -autoterminate");

    // Registry paths must resolve independently of Setup's folder, keeping Unicode/case and accepting Steam's slashes.
    ICHECK(l::NormalizeSteamPath(L"c:/program files (x86)/steam/steam.exe").native() == L"c:\\program files (x86)\\steam\\steam.exe");
    ICHECK(l::NormalizeSteamPath(L" \t\"D:/Steam/old/../steam.exe\"\r\n").native() == L"D:\\Steam\\steam.exe");
    ICHECK(l::NormalizeSteamPath(L"C:/Users/Zo\u00eb/\u6e38\u620f/Steam.exe").native() == L"C:\\Users\\Zo\u00eb\\\u6e38\u620f\\Steam.exe");
    ICHECK(l::NormalizeSteamPath(L"//server/games/Steam/steam.exe").native() == L"\\\\server\\games\\Steam\\steam.exe");
    for (auto const* bad : {L"", L" \t", L"steam.exe", L"Steam/steam.exe", L"C:steam.exe", L"/Steam/steam.exe", L"\"C:/Steam/steam.exe"}) {
        ICHECK(l::NormalizeSteamPath(bad).empty());
    }
    std::wstring const longPath = L"C:/" + std::wstring(280, L'x') + L"/steam.exe";
    ICHECK(l::NormalizeSteamPath(longPath).native().size() == longPath.size());  // no MAX_PATH registry buffer
    fs::path const primary = L"C:\\Steam\\steam.exe", moved = L"D:\\Steam\\steam.exe";
    auto bothFiles = [&](fs::path const& p) { return p == primary || p == moved; };
    auto movedOnly = [&](fs::path const& p) { return p == moved; };
    ICHECK(l::ResolveSteamExe(L"C:/Steam/steam.exe", L"D:/Steam", bothFiles) == primary);
    ICHECK(l::ResolveSteamExe(L"C:/Steam/steam.exe", L"D:/Steam", movedOnly) == moved);
    ICHECK(l::ResolveSteamExe(L"", L"\"D:/Steam\"", movedOnly) == moved);
    ICHECK(l::ResolveSteamExe(L"steam.exe", L"D:/Steam", movedOnly) == moved);
    ICHECK(l::ResolveSteamExe(L"C:/Steam", L"D:/Steam", movedOnly) == moved);  // directory isn't an exe
    ICHECK(l::ResolveSteamExe(L"C:/Steam/steam.exe", L"D:/Steam", [](fs::path const&) { return false; }).empty());

    // Every success boundary, both launcher modes; no calls after acceptance (avoids duplicate launch requests).
    using Method = l::SteamLaunchMethod;
    std::vector<Method> const order{Method::Process, Method::Shell, Method::Url};
    fs::path const steam = L"D:\\Steam folder\\steam.exe";
    for (bool const boots : {false, true}) {
        for (std::size_t succeedsAt = 0; succeedsAt <= order.size(); ++succeedsAt) {
            std::vector<Method> calls;
            std::vector<std::string> log;
            l::SteamLaunchHooks hooks;
            hooks.log = [&](std::string const& line) { log.push_back(line); };
            hooks.launch = [&](Method method, std::wstring const& target, std::wstring const& args, fs::path const& directory) -> unsigned long {
                std::size_t const index = calls.size();
                calls.push_back(method);
                ICHECK(index < order.size() && method == order[index]);
                if (method == Method::Url) {
                    ICHECK(target == (boots ? L"steam://run/1328670//-game 1 -autoterminate/" : L"steam://run/1328670"));
                    ICHECK(args.empty() && directory.empty());
                } else {
                    ICHECK(target == steam.native() && directory == steam.parent_path());
                    ICHECK(args == (boots ? L"-applaunch 1328670 -game 1 -autoterminate" : L"-applaunch 1328670"));
                }
                if (index == succeedsAt) {
                    return ERROR_SUCCESS;
                }
                // The likely report, a declined elevation prompt, then a broken Steam URL association.
                return method == Method::Process ? ERROR_ELEVATION_REQUIRED : method == Method::Shell ? ERROR_CANCELLED : ERROR_NO_ASSOCIATION;
            };
            l::Result const result = l::RequestSteamLaunch(steam, boots, hooks);
            ICHECK(result.started == (succeedsAt < order.size()) && result.pid == 0);
            ICHECK(calls.size() == std::min(succeedsAt + 1, order.size()));
            ICHECK(log.size() == calls.size() * 2);  // target/arguments/directory and result for every attempt
            if (!result.started) {
                for (auto const* code : {"740", "1223", "1155"}) {
                    ICHECK(result.message.find(std::string("error ") + code + " (") != std::string::npos);
                }
                ICHECK(result.message.find("Steam's library") != std::string::npos && result.message.find("installation still applies") != std::string::npos);
                ICHECK(result.message.find("Windows supplied no description") == std::string::npos);  // FormatMessage, any Windows language
            }
            ICHECK((result.message.find("confirm the launch options") != std::string::npos) == (boots && succeedsAt == 2));
        }
    }
    for (bool const acceptsUrl : {false, true}) {
        unsigned calls = 0;
        std::vector<std::string> log;
        l::SteamLaunchHooks hooks;
        hooks.log = [&](std::string const& line) { log.push_back(line); };
        hooks.launch = [&](Method method, std::wstring const&, std::wstring const&, fs::path const&) -> unsigned long {
            ++calls;
            ICHECK(method == Method::Url);  // missing/stale registry still reaches the protocol handler
            return acceptsUrl ? ERROR_SUCCESS : ERROR_NO_ASSOCIATION;
        };
        l::Result const result = l::RequestSteamLaunch({}, true, hooks);
        ICHECK(calls == 1 && result.started == acceptsUrl && log.size() == 4);
        if (!acceptsUrl) {
            ICHECK(result.message.find("program wasn't found") != std::string::npos);
        }
    }
}

void TestPacksAndUpdates() {
    namespace p = mevr::install::packs;
    namespace u = mevr::install::updates;
    ICHECK(!p::Catalogue().empty() && p::Find("alot-le1") != nullptr && p::Find("nope") == nullptr);
    for (p::Pack const& pack : p::Catalogue()) {
        ICHECK(std::string(pack.url).rfind("https://", 0) == 0);
    }
    p::IpcLine const a = p::ParseIpc("[IPC]TASK_PROGRESS 42");
    ICHECK(a.command == "TASK_PROGRESS" && a.argument == "42");
    p::IpcLine const b = p::ParseIpc("[IPC]STAGE_CONTEXT STAGE_SCAN");
    ICHECK(b.command == "STAGE_CONTEXT" && p::StageText(b.argument).find("Scanning") == 0);
    ICHECK(p::ParseIpc("plain output").command.empty());
    ICHECK(p::MflFor({fs::path(L"C:\\a\\x.mem"), fs::path(L"C:\\b\\y.mem")}) == "C:\\a\\x.mem\r\nC:\\b\\y.mem\r\n");
    std::string const release = R"({"tag_name": "v534", "assets": [
        {"name": "MassEffectModderNoGui-v534-linux.7z", "browser_download_url": "https://github.com/x/linux.7z", "size": 5},
        {"name": "MassEffectModder-v534.7z", "browser_download_url": "https://github.com/x/gui.7z", "size": 6},
        {"name": "MassEffectModderNoGui-v534.7z", "browser_download_url": "https://github.com/MassEffectModder/MassEffectModder/releases/download/v534/MassEffectModderNoGui-v534.7z", "size": 7234567}]})";
    auto const asset = p::MemAssetFrom(release);
    ICHECK(asset && asset->name == "MassEffectModderNoGui-v534.7z" && asset->size == 7234567 && asset->tag == "v534");
    ICHECK(!p::MemAssetFrom(R"({"assets": [{"name": "MassEffectModderNoGui-v1.7z", "browser_download_url": "https://evil.example/x.7z"}]})"));
    // Updates.
    ICHECK(u::LatestReleaseApi(u::kPlaceholderRepo).empty());
    ICHECK(u::LatestReleaseApi("https://github.com/someone/mevr") == "https://api.github.com/repos/someone/mevr/releases/latest");
    ICHECK(u::LatestReleaseApi("https://github.com/someone/mevr.git/") == "https://api.github.com/repos/someone/mevr/releases/latest");
    ICHECK(u::LatestReleaseApi("http://github.com/a/b").empty());
    ICHECK(u::LatestReleaseApi("https://github.com/a").empty());
    ICHECK(u::LatestReleaseApi("https://github.com/a/b?c").empty());
    auto const r = u::ParseLatestRelease(R"({"tag_name": "v0.2.0", "name": "MEVR 0.2", "html_url": "https://github.com/someone/mevr/releases/tag/v0.2.0"})");
    ICHECK(r && r->tag == "v0.2.0");
    ICHECK(!u::ParseLatestRelease(R"({"tag_name": "v1", "html_url": "https://evil.example/"})"));
    ICHECK(u::CompareVersions("v0.2.0", "0.1.0") > 0);
    ICHECK(u::CompareVersions("0.1", "0.1.0") == 0);
    ICHECK(u::CompareVersions("0.1.0-beta", "0.1.0") == 0);
    ICHECK(u::CompareVersions("0.9.9", "0.10.0") < 0);
    ICHECK(mevr::install::http::SizeText(175500000) == "176 MB");
    ICHECK(mevr::install::http::SizeText(9700000000ull) == "9.7 GB");
    ICHECK(mevr::install::http::TimeText(600000000, 10000000.0) == "about 1 minute");
    ICHECK(mevr::install::http::TimeText(1000, 10000000.0) == "under a minute");
}

// The pure parts of the web and MEM paths (research/33 section 10; the paths themselves run against a local server in
// tools/setup/test-network.py): the update check's verdicts, MEM's asset with its digest, the test server's mapping,
// command-line quoting, and the in-game menu's setup.request.
void TestNetworkPaths() {
    namespace p = mevr::install::packs;
    namespace u = mevr::install::updates;
    namespace http = mevr::install::http;
    using State = u::CheckResult::State;
    std::string const page = "https://github.com/dnehring/MEVR/releases/tag/";
    auto release = [&](char const* tag, char const* extra = "") {
        return std::string(R"({"tag_name": ")") + tag + R"(", "html_url": ")" + page + tag + "\"" + extra + "}";
    };
    // The update check's verdicts.
    ICHECK(u::Interpret(0, "", "the request failed: couldn't connect (error 12029)", false, "0.1.0").state == State::Failed);
    ICHECK(u::Interpret(0, "", "", false, "0.1.0").text == "Couldn't check: no answer.");
    ICHECK(u::Interpret(404, R"({"message": "Not Found"})", "", false, "0.1.0").state == State::NoRelease);
    ICHECK(u::Interpret(403, "", "", true, "0.1.0").state == State::RateLimited);
    ICHECK(u::Interpret(429, "", "", true, "0.1.0").state == State::RateLimited);
    ICHECK(u::Interpret(403, "", "", false, "0.1.0").text == "Couldn't check: GitHub answered HTTP 403.");
    ICHECK(u::Interpret(500, "", "", false, "0.1.0").state == State::Failed);
    ICHECK(u::Interpret(200, "{", "", false, "0.1.0").text == "Couldn't check: GitHub's answer wasn't a release.");
    ICHECK(u::Interpret(200, release("v9.0.0"), "the answer is too large", false, "0.1.0").state == State::Failed);
    u::CheckResult const newer = u::Interpret(200, release("v0.2.0"), "", false, "0.1.0");
    ICHECK(newer.state == State::Newer && newer.page == page + "v0.2.0" && newer.tag == "v0.2.0" && newer.text == "Version v0.2.0 is out.");
    ICHECK(u::Interpret(200, release("v0.1.0"), "", false, "0.1.0").text == "This is the latest version.");
    u::CheckResult const older = u::Interpret(200, release("v0.0.9"), "", false, "0.1.0");
    ICHECK(older.state == State::Latest && older.page.empty() && older.text.find("(v0.0.9)") != std::string::npos);
    ICHECK(u::Interpret(200, release("v0.2.0", R"(, "prerelease": true)"), "", false, "0.1.0").state == State::Latest);
    ICHECK(u::Interpret(200, release("v0.2.0", R"(, "draft": true)"), "", false, "0.1.0").page.empty());
    ICHECK(u::Interpret(200, R"({"tag_name": "v9", "html_url": "https://evil.example/"})", "", false, "0.1.0").state == State::Failed);
    ICHECK(u::Check(u::kPlaceholderRepo).state == State::Off);  // no request
    ICHECK(std::string(u::StateName(State::RateLimited)) == "rate limited");

    // MEM's asset: GitHub's digest, a plain file name, why not.
    auto memRelease = [](std::string const& asset) {
        return R"({"tag_name": "v534", "assets": [{"name": "MassEffectModderNoGui-v534-linux.7z", "browser_download_url": "https://github.com/l.7z"}, )" +
            asset + "]}";
    };
    std::string const sha(64, 'a');
    auto withDigest = p::MemAssetFrom(memRelease(R"({"name": "MassEffectModderNoGui-v534.7z", "browser_download_url": "https://github.com/m.7z", "size": 9, "digest": "SHA256:)" +
        std::string(64, 'A') + "\"}"));
    ICHECK(withDigest && withDigest->sha256 == sha && withDigest->size == 9);
    auto shortDigest = p::MemAssetFrom(memRelease(R"({"name": "MassEffectModderNoGui-v534.7z", "browser_download_url": "https://github.com/m.7z", "digest": "sha256:abc"})"));
    ICHECK(shortDigest && shortDigest->sha256.empty());
    auto otherDigest = p::MemAssetFrom(memRelease(R"({"name": "MassEffectModderNoGui-v534.7z", "browser_download_url": "https://github.com/m.7z", "digest": "md5:)" + sha + "\"}"));
    ICHECK(otherDigest && otherDigest->sha256.empty());
    std::string why;
    ICHECK(!p::MemAssetFrom(memRelease(R"({"name": "MassEffectModderNoGui\\..\\x.7z", "browser_download_url": "https://github.com/m.7z"})"), &why) &&
           why.find("plain file name") != std::string::npos);
    ICHECK(!p::MemAssetFrom(R"({"tag_name": "v1", "assets": []})", &why) && why.find("(v1) has no Windows command-line build") != std::string::npos);
    ICHECK(!p::MemAssetFrom("[]", &why) && why == "GitHub's answer wasn't a release");
    ICHECK(!p::MemAssetFrom(memRelease(R"({"name": "MassEffectModderNoGui-v534.7z", "browser_download_url": "https://evil.example/m.7z"})"), &why) &&
           why.find("github.com") != std::string::npos);

    // The test server: only 127.0.0.1 or https, every URL mapped under it, off again with "".
    ICHECK(http::EffectiveUrl("https://api.github.com/repos/a/b") == "https://api.github.com/repos/a/b");
    for (char const* bad : {"http://localhost:8080", "http://127.0.0.2", "http://127.0.0.1:", "http://127.0.0.1:80/x", "http://127.0.0.1:1234567",
             "http://127.0.0.1.evil.example", "ftp://127.0.0.1", "127.0.0.1:80", "https://", "https://a b"}) {
        ICHECK(!http::SetTestServer(bad));
    }
    ICHECK(http::EffectiveUrl("https://github.com/x") == "https://github.com/x");  // a refused server changed nothing
    ICHECK(http::SetTestServer("http://127.0.0.1:8123/"));
    ICHECK(http::EffectiveUrl("https://api.github.com/repos/a/b/releases/latest") == "http://127.0.0.1:8123/api.github.com/repos/a/b/releases/latest");
    ICHECK(http::EffectiveUrl("https://github.com/o/r/releases/download/v1/x.7z?y=1") == "http://127.0.0.1:8123/github.com/o/r/releases/download/v1/x.7z?y=1");
    ICHECK(http::EffectiveUrl("http://127.0.0.1:8123/github.com/x") == "http://127.0.0.1:8123/github.com/x");
    ICHECK(http::SetTestServer("http://127.0.0.1"));
    ICHECK(http::SetTestServer("https://mirror.example/test"));
    ICHECK(http::EffectiveUrl("https://github.com/x") == "https://mirror.example/test/github.com/x");
    ICHECK(http::SetTestServer(""));
    ICHECK(http::EffectiveUrl("https://github.com/x") == "https://github.com/x");
    // Without a test server, plain http is refused before anything is sent.
    ICHECK(http::Get("http://127.0.0.1:1/x").error == "not an https URL");
    std::filesystem::path const temp = mevr::tests::TempRoot();
    fs::path const target = fs::path(temp) / L"mevr-http-test" / L"x.bin";
    ICHECK(http::Download("http://127.0.0.1:1/x", target, nullptr, nullptr) == "not an https URL" && !fs::exists(target));
    ICHECK(http::ErrorText(12029).find("couldn't connect") == 0 && http::ErrorText(12002).find("no answer in time") == 0);
    ICHECK(http::ErrorText(5) == "error 5");

    // Command-line arguments (CommandLineToArgvW's rules): a trailing backslash must not eat the closing quote.
    ICHECK(p::QuoteArg(L"C:\\Games\\Mass Effect Legendary Edition") == L"\"C:\\Games\\Mass Effect Legendary Edition\"");
    ICHECK(p::QuoteArg(L"C:\\Games\\Mass Effect Legendary Edition\\") == L"\"C:\\Games\\Mass Effect Legendary Edition\\\\\"");
    ICHECK(p::QuoteArg(L"a\\\"b") == L"\"a\\\\\\\"b\"");
    ICHECK(p::QuoteArg(L"") == L"\"\"");
    for (std::wstring const arg : {std::wstring(L"D:\\x y\\"), std::wstring(L"a\\\\b\\\""), std::wstring(L"plain"), std::wstring(L"\\\\server\\share\\")}) {
        std::wstring const line = L"x.exe " + p::QuoteArg(arg);
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(line.c_str(), &argc);
        ICHECK(argv != nullptr && argc == 2 && arg == argv[1]);
        LocalFree(argv);
    }

    // The in-game menu's request (plugin/SetupRequest writes page, packs and the game's pid).
    auto const req = mevr::install::ParseSetupRequest("{\n  \"page\": \"content\",\n  \"packs\": [],\n  \"pid\": 4321\n}");
    ICHECK(req && req->page == "content" && req->packs.empty() && req->pid == 4321);
    auto const named = mevr::install::ParseSetupRequest(R"({"page": "settings", "packs": ["alot-le1", "nope", "alot-le1", 7], "pid": -3})");
    ICHECK(named && named->page == "content" && named->packs == std::vector<std::string>{"alot-le1"} && named->pid == 0);
    ICHECK(!mevr::install::ParseSetupRequest("{ not json") && !mevr::install::ParseSetupRequest("[]"));

    // After MEM wrote the game's settings: ours again only where they were in place before it ran; the uninstall still
    // puts the player's own values back.
    {
        using namespace mevr::install;
        fs::path const root = fs::path(temp) / L"mevr-reassert-test" / L"Mass Effect Legendary Edition";
        std::error_code ec;
        fs::remove_all(root.parent_path(), ec);
        Layout const layout{root};
        Write(layout.Win64() / L"MassEffect1.exe", "exe");
        std::string const original = "[SystemSettings]\r\nUseVSync=True\r\nMotionBlur=True\r\nFilmGrain=True\r\nResX=1920\r\n";
        Write(layout.Full(paths::kGamerSettings), original);
        {
            Transaction tx(layout);
            ICHECK(tx.Load() && tx.EditIni(paths::kGamerSettings, [](std::string_view t, std::vector<ini::Edit>* e) {
                settings::Choices c;
                c.target = settings::TargetFor("performance", 0, 0);
                return settings::ApplyGamerSettings(t, c, e);
            }));
        }
        std::string const installed = Read(layout.Full(paths::kGamerSettings));
        std::string beforeMem = ini::Set(installed, "SystemSettings", "FilmGrain", "True");  // the player's, since
        std::string afterMem = ini::Set(beforeMem, "SystemSettings", "MotionBlur", "True");  // MEM's
        afterMem = ini::Set(afterMem, "SystemSettings", "TEXTUREGROUP_World", "(MinLODSize=256)");
        Write(layout.Full(paths::kGamerSettings), afterMem);
        {
            Transaction tx(layout);
            ICHECK(tx.Load() && tx.ReassertIni(paths::kGamerSettings, beforeMem) && tx.Errors().empty());
            ICHECK(tx.ReassertIni(paths::kEngineIni, ""));  // not on record: nothing
        }
        std::string const now = Read(layout.Full(paths::kGamerSettings));
        ICHECK(ini::Get(now, "SystemSettings", "MotionBlur") == std::optional<std::string>("False"));
        ICHECK(ini::Get(now, "SystemSettings", "FilmGrain") == std::optional<std::string>("True"));
        ICHECK(ini::Get(now, "SystemSettings", "ResX") == std::optional<std::string>("3072"));
        ICHECK(ini::Get(now, "SystemSettings", "TEXTUREGROUP_World") == std::optional<std::string>("(MinLODSize=256)"));
        {
            Transaction tx(layout);
            ICHECK(tx.Load() && tx.RevertIni(paths::kGamerSettings));
        }
        std::string const reverted = Read(layout.Full(paths::kGamerSettings));
        ICHECK(ini::Get(reverted, "SystemSettings", "MotionBlur") == std::optional<std::string>("True"));
        ICHECK(ini::Get(reverted, "SystemSettings", "ResX") == std::optional<std::string>("1920"));
        ICHECK(ini::Get(reverted, "SystemSettings", "TEXTUREGROUP_World") == std::optional<std::string>("(MinLODSize=256)"));
        fs::remove_all(root.parent_path(), ec);
    }
}

// A fake game folder under %TEMP% (never the real game): the manifest's add / replace / rename / edit and their
// undo, then the Installer's whole Install, RestoreSettings and Uninstall on it.
void TestTransaction() {
    using namespace mevr::install;
    std::filesystem::path const temp = mevr::tests::TempRoot();
    fs::path const root = fs::path(temp) / L"mevr-install-test" / L"Mass Effect Legendary Edition";
    std::error_code ec;
    fs::remove_all(root.parent_path(), ec);
    Layout const layout{root};
    Write(layout.Win64() / L"MassEffect1.exe", "exe");
    Write(layout.Win64() / L"bink2w64.dll", "RAD original");
    Write(layout.Asi() / L"OtherMod.asi", "another mod");
    std::string const gamer = "[SystemSettings]\r\nResX=1920\r\nResY=1080\r\nUseVSync=True\r\n\r\n[HDR]\r\nDesiredRange=DynamicRange_HDR\r\n";
    Write(layout.Full(paths::kGamerSettings), gamer);
    std::string const engine = "[Engine.GameEngine]\r\nSmoothFrameRate=TRUE\r\n";
    Write(layout.Full(paths::kEngineIni), engine);
    std::string const existingMod = "old ASI";
    Write(layout.Full(paths::kModAsi), existingMod);  // an older copy someone put there by hand

    fs::path const payloadDir = root.parent_path() / L"payload";
    Write(payloadDir / L"LE1MEVR.asi", "new ASI");
    Write(payloadDir / L"bink2w64.dll", "our loader");
    Write(payloadDir / L"fonts" / L"A.ttf", "font");
    Write(payloadDir / L"menu" / L"main.rml", "<rml/>");

    {
        Transaction tx(layout);
        ICHECK(tx.Load() && !tx.Exists());
        ICHECK(tx.Rename(paths::kBink, paths::kBinkOriginal));
        ICHECK(tx.CopyIn(payloadDir / L"bink2w64.dll", paths::kBink));
        ICHECK(tx.CopyIn(payloadDir / L"LE1MEVR.asi", paths::kModAsi));
        ICHECK(tx.CopyIn(payloadDir / L"fonts" / L"A.ttf", "Game/ME1/Binaries/Win64/ASI/fonts/A.ttf"));
        ICHECK(tx.WriteIfMissing(paths::kHalcyonIni, ini::HalcyonReleaseIni(), true));
        ICHECK(tx.WriteIfMissing(paths::kHalcyonIni, "never this", true));  // kept
        ICHECK(tx.EditIni(paths::kGamerSettings, [](std::string_view t, std::vector<ini::Edit>* e) {
            settings::Choices c;
            c.target = settings::TargetFor("low", 0, 0);
            return settings::ApplyGamerSettings(t, c, e);
        }));
        ICHECK(tx.EditIni(paths::kEngineIni, [](std::string_view t, std::vector<ini::Edit>* e) { return settings::ApplyEngineSettings(t, {}, e); }));
        ICHECK(tx.Errors().empty());
    }
    ICHECK(Read(layout.Win64() / L"bink2w64.dll") == "our loader");
    ICHECK(Read(layout.Win64() / L"bink2w64_original.dll") == "RAD original");
    ICHECK(Read(layout.Full(paths::kModAsi)) == "new ASI");
    ICHECK(Read(layout.Full(paths::kHalcyonIni)) == ini::HalcyonReleaseIni());
    ICHECK(ini::Get(Read(layout.Full(paths::kGamerSettings)), "SystemSettings", "ResX") == std::optional<std::string>("2048"));
    {
        // The manifest reads back; Verify sees a changed file.
        Transaction tx(layout);
        ICHECK(tx.Load() && tx.Exists());
        ICHECK(tx.Data().files.size() == 7);
        FileEntry const* asi = tx.Data().Find(paths::kModAsi);
        ICHECK(asi != nullptr && asi->action == "replaced" && !asi->backup.empty());
        ICHECK(tx.Verify().empty());
        Write(layout.Full(paths::kModAsi), "tampered");
        auto const bad = tx.Verify();
        ICHECK(bad.size() == 1 && bad[0] == paths::kModAsi);
        // The player changes a value we set, and one we didn't, while the game runs.
        std::string g = Read(layout.Full(paths::kGamerSettings));
        g = ini::Set(g, "SystemSettings", "UseVSync", "True");  // theirs again: stays
        g = ini::Set(g, "SystemSettings", "Gamma", "2.4");      // not ours: stays
        Write(layout.Full(paths::kGamerSettings), g);
        // The player's own Halcyon settings since.
        Write(layout.Full(paths::kModIni), "[Hands]\r\n");
        // Keep the player's data on uninstall.
        ICHECK(tx.Uninstall(true));
        ICHECK(tx.Errors().empty());
    }
    ICHECK(Read(layout.Win64() / L"bink2w64.dll") == "RAD original");
    ICHECK(!fs::exists(layout.Win64() / L"bink2w64_original.dll"));
    ICHECK(Read(layout.Full(paths::kModAsi)) == existingMod);
    ICHECK(fs::exists(layout.Full(paths::kHalcyonIni)));  // userData, kept
    ICHECK(!fs::exists(layout.Asi() / L"fonts"));          // our folder, removed
    ICHECK(Read(layout.Asi() / L"OtherMod.asi") == "another mod");
    ICHECK(!fs::exists(layout.ManifestFile()));
    ICHECK(!fs::exists(layout.BackupDir()));
    std::string const g = Read(layout.Full(paths::kGamerSettings));
    ICHECK(ini::Get(g, "SystemSettings", "ResX") == std::optional<std::string>("1920"));
    ICHECK(ini::Get(g, "SystemSettings", "UseVSync") == std::optional<std::string>("True"));
    ICHECK(!ini::Get(g, "SystemSettings", "BorderlessWindow"));
    ICHECK(ini::Get(g, "SystemSettings", "Gamma") == std::optional<std::string>("2.4"));
    ICHECK(ini::Get(g, "HDR", "DesiredRange") == std::optional<std::string>("DynamicRange_HDR"));
    ICHECK(Read(layout.Full(paths::kEngineIni)) == engine);

    // A full install with the Installer on the same folder. The fake exe has no version (ignoreVersion); the fake
    // bink2w64.dll has no version resource, so with an original behind it it's "another proxy", used as it is. The
    // Installer refuses to run while this folder's game does: then this part is skipped (a real game elsewhere doesn't count).
    if (detect::GameRunningIn(root)) {
        std::printf("  (the game is running: the Installer part of TestTransaction is skipped)\n");
        fs::remove_all(root.parent_path(), ec);
        return;
    }
    Write(layout.Win64() / L"bink2w64_original.dll", "RAD original");
    InstallOptions options;
    options.ignoreVersion = true;
    options.choices.target = settings::TargetFor("performance", 0, 0);
    options.source = "Steam";
    auto const payload = FindPayload(root.parent_path());
    ICHECK(payload.has_value());
    if (payload) {
        Outcome const out = Install(layout, *payload, options);
        ICHECK(out.ok);
        for (std::string const& e : out.errors) {
            std::printf("  install error: %s\n", e.c_str());
        }
        Status const s = ReadStatus(root, "Steam");
        ICHECK(s.installed && s.loader == "other" && s.gameSettings && s.bink == detect::BinkState::OtherProxy);
        ICHECK(Read(layout.Full(paths::kSteamAppId)) == "1328670");
        // The existing MELEVR.ini kept, with the fixes.
        std::string const h = Read(layout.Full(paths::kHalcyonIni));
        ICHECK(ini::Get(h, "profile:Default", "sfr2ResX") == std::optional<std::string>("3072"));
        ICHECK(Ok(RecordPack(layout, "alot-le1", "ALOT for ME1")));
        ICHECK(ReadStatus(root, "Steam").packs.size() == 1);
        Outcome const back = RestoreSettings(layout);
        ICHECK(back.ok);
        ICHECK(ini::Get(Read(layout.Full(paths::kGamerSettings)), "SystemSettings", "ResX") == std::optional<std::string>("1920"));
        Outcome const gone = Uninstall(layout, false, {});
        ICHECK(gone.ok);
        ICHECK(!fs::exists(layout.Full(paths::kModAsi)) || Read(layout.Full(paths::kModAsi)) == existingMod);
        ICHECK(!fs::exists(layout.Full(paths::kSteamAppId)));
    }
    fs::remove_all(root.parent_path(), ec);
}

// The QA pass of 2026-10-26 (research/33): Halcyon's upstream MELE-VR moved aside and back, a loader another tool put in
// place of ours left alone by Uninstall and Repair, the mod's run-time files deleted by a full uninstall only, "installed"
// only with the mod on record, a read-only GamerSettings.ini, the local data box. A fake game under %TEMP%.
void TestQaFixes() {
    using namespace mevr::install;
    ICHECK(detect::HasMelevrMarker(std::string("MZ\0\0 ... MELEVR 1.0", 18)));
    ICHECK(detect::HasMelevrMarker(std::string("M\0E\0L\0E\0V\0R\0", 12)));
    ICHECK(!detect::HasMelevrMarker("ReShade dxgi.dll"));
    std::filesystem::path const temp = mevr::tests::TempRoot();
    fs::path const base = fs::path(temp) / L"mevr-install-qa";
    fs::path const root = base / L"Mass Effect Legendary Edition";
    if (detect::GameRunningIn(root)) {
        std::printf("  (a game runs from the test folder: TestQaFixes is skipped)\n");
        return;
    }
    std::error_code ec;
    fs::remove_all(base, ec);
    Layout const layout{root};
    fs::path const payloadDir = base / L"payload";
    Write(payloadDir / L"LE1MEVR.asi", "new ASI");
    Write(payloadDir / L"bink2w64.dll", "our loader");
    Write(payloadDir / L"fonts" / L"A.ttf", "font");
    Write(payloadDir / L"menu" / L"main.rml", "<rml/>");
    Payload const payload{payloadDir};
    std::string const gamer = "[SystemSettings]\r\nResX=1920\r\nUseVSync=True\r\n";
    auto fresh = [&] {
        fs::remove_all(root, ec);
        Write(layout.Win64() / L"MassEffect1.exe", "exe");
        Write(layout.Win64() / L"bink2w64.dll", "a proxy");  // no version resource: "another proxy" in front of...
        Write(layout.Win64() / L"bink2w64_original.dll", "RAD original");
        Write(layout.Full(paths::kGamerSettings), gamer);
    };
    InstallOptions options;
    options.ignoreVersion = true;
    options.choices.target = settings::TargetFor("performance", 0, 0);

    // 1. Halcyon's upstream MELE-VR in Win64: refused when asked to, else moved aside and back; its MELEVR.ini stays.
    fresh();
    Write(layout.Win64() / L"dxgi.dll", std::string("MZ upstream \0 MELEVR renderer", 30));
    Write(layout.Win64() / L"openxr_loader.dll", "khronos loader");
    Write(layout.Win64() / L"MELEVR.ini", "[profile:Default]\r\nvrMode = 2\r\n");
    ICHECK(ReadStatus(root, "Folder").upstream.Found() && ReadStatus(root, "Folder").upstream.openxrLoader);
    InstallOptions refuse = options;
    refuse.moveUpstreamAside = false;
    Outcome const refused = Install(layout, payload, refuse);
    ICHECK(!refused.ok && !fs::exists(layout.ManifestFile()) && fs::exists(layout.Win64() / L"dxgi.dll"));
    Outcome const in = Install(layout, payload, options);
    ICHECK(in.ok);
    ICHECK(!fs::exists(layout.Win64() / L"dxgi.dll") && !fs::exists(layout.Win64() / L"openxr_loader.dll"));
    ICHECK(fs::exists(layout.Win64() / L"dxgi.dll.mevr-off") && fs::exists(layout.Win64() / L"openxr_loader.dll.mevr-off"));
    ICHECK(fs::exists(layout.Win64() / L"MELEVR.ini"));
    ICHECK(!ReadStatus(root, "Folder").upstream.Found());
    {
        Transaction tx(layout);
        ICHECK(tx.Load() && tx.Verify().empty());  // the files moved aside aren't something Repair must bring back
    }
    ICHECK(Ok(Uninstall(layout, false, {})));
    ICHECK(Read(layout.Win64() / L"dxgi.dll").find("MELEVR") != std::string::npos);
    ICHECK(Read(layout.Win64() / L"openxr_loader.dll") == "khronos loader");
    ICHECK(!fs::exists(layout.Win64() / L"dxgi.dll.mevr-off"));
    ICHECK(Read(layout.Win64() / L"MELEVR.ini") == "[profile:Default]\r\nvrMode = 2\r\n");

    // 2. Our loader (recorded by hash), then another tool's loader in its place: Uninstall and Repair leave both DLLs.
    fresh();
    fs::remove(layout.Win64() / L"bink2w64_original.dll", ec);
    Write(layout.Win64() / L"bink2w64.dll", "RAD original");
    auto ourInstall = [&] {
        Transaction tx(layout);
        ICHECK(tx.Load());
        tx.Data().modVersion = "0.1.0";
        tx.Data().loader = "mevr";
        ICHECK(tx.Rename(paths::kBink, paths::kBinkOriginal));
        ICHECK(tx.CopyIn(payloadDir / L"bink2w64.dll", paths::kBink));
        ICHECK(tx.CopyIn(payloadDir / L"LE1MEVR.asi", paths::kModAsi));
    };
    ourInstall();
    Write(layout.Win64() / L"bink2w64.dll", "ME3Tweaks' loader");  // installed after us
    ICHECK(ReadStatus(root, "Folder").installed && ReadStatus(root, "Folder").needsRepair.empty());  // not something Repair changes
    Outcome const repaired = Repair(layout, payload);
    ICHECK(repaired.ok && Read(layout.Win64() / L"bink2w64.dll") == "ME3Tweaks' loader");
    ICHECK(Ok(Uninstall(layout, false, {})));
    ICHECK(Read(layout.Win64() / L"bink2w64.dll") == "ME3Tweaks' loader");
    ICHECK(Read(layout.Win64() / L"bink2w64_original.dll") == "RAD original");
    ICHECK(!fs::exists(layout.Full(paths::kModAsi)));
    // ...and with our loader still in place, the uninstall puts RAD's DLL back.
    fs::remove(layout.Win64() / L"bink2w64_original.dll", ec);
    Write(layout.Win64() / L"bink2w64.dll", "RAD original");
    ourInstall();
    ICHECK(Ok(Uninstall(layout, false, {})));
    ICHECK(Read(layout.Win64() / L"bink2w64.dll") == "RAD original" && !fs::exists(layout.Win64() / L"bink2w64_original.dll"));
    // ...and an uninstall that fails half way (a locked file) keeps only what's left on record: the retry doesn't
    // remove bink2w64.dll again (by then RAD's DLL, renamed back by the first try).
    ourInstall();
    HANDLE const lock = CreateFileW(layout.Full(paths::kModAsi).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ICHECK(lock != INVALID_HANDLE_VALUE);
    ICHECK(!Uninstall(layout, false, {}).ok);
    CloseHandle(lock);
    ICHECK(Read(layout.Win64() / L"bink2w64.dll") == "RAD original" && ReadStatus(root, "Folder").installed);
    ICHECK(Ok(Uninstall(layout, false, {})));
    ICHECK(Read(layout.Win64() / L"bink2w64.dll") == "RAD original" && !fs::exists(layout.Full(paths::kModAsi)));
    ICHECK(!fs::exists(layout.ManifestFile()));

    // 2b. An update keeps the player's game settings (the owner's QA, 2026-11-07: reinstalling put the default render
    // resolution back over the player's own); reapplySettings applies them again.
    fresh();
    if (fs::exists(layout.Full(paths::kGamerSettings))) {
        ICHECK(Ok(Install(layout, payload, options)));
        std::string const applied = Read(layout.Full(paths::kGamerSettings));
        std::string mine = applied;
        if (auto const at = mine.find("ResX="); at != std::string::npos) {
            mine.replace(at, mine.find('', at) - at, "ResX=1456");
        }
        Write(layout.Full(paths::kGamerSettings), mine);
        ICHECK(Ok(Install(layout, payload, options)));  // the update
        ICHECK(ini::Get(Read(layout.Full(paths::kGamerSettings)), "SystemSettings", "ResX") == std::optional<std::string>("1456"));
        InstallOptions again = options;
        again.reapplySettings = true;
        ICHECK(Ok(Install(layout, payload, again)));
        ICHECK(ini::Get(Read(layout.Full(paths::kGamerSettings)), "SystemSettings", "ResX") != std::optional<std::string>("1456"));
        ICHECK(Ok(Uninstall(layout, false, {})));
    }

    // 3. The mod's run-time files: kept by "keep my settings", deleted by a full uninstall (with the Logs folder the
    // game run made, once empty). Not "installed" with only settings or a pack on record.
    fresh();
    ICHECK(Ok(Install(layout, payload, options)));
    ICHECK(ReadStatus(root, "Folder").installed);
    Write(layout.Full(paths::kModIni), "[Calibration]\r\n");
    Write(layout.Win64() / L"Logs" / L"LE1MEVR.log", "log");
    Write(layout.Win64() / L"Logs" / L"LE1MEVR.prev.log", "log");
    ICHECK(Ok(Uninstall(layout, true, {})));
    ICHECK(fs::exists(layout.Full(paths::kModIni)) && fs::exists(layout.Win64() / L"Logs" / L"LE1MEVR.log"));
    ICHECK(fs::exists(layout.Full(paths::kHalcyonIni)));
    ICHECK(Ok(Install(layout, payload, options)));  // the files left by "keep" are on record again
    ICHECK(Ok(Uninstall(layout, false, {})));
    ICHECK(!fs::exists(layout.Full(paths::kModIni)) && !fs::exists(layout.Full(paths::kHalcyonIni)));
    ICHECK(!fs::exists(layout.Win64() / L"Logs") && !fs::exists(layout.Asi()));
    ICHECK(Ok(ApplySettings(layout, options.choices)));
    Status const settingsOnly = ReadStatus(root, "Folder");
    ICHECK(settingsOnly.hasRecord && settingsOnly.gameSettings && !settingsOnly.installed);
    ICHECK(!Repair(layout, payload).ok);  // "Not installed yet"
    ICHECK(Ok(RestoreSettings(layout)) && Read(layout.Full(paths::kGamerSettings)) == gamer);
    ICHECK(Ok(RecordPack(layout, "alot-le1", "ALOT for ME1")) && !ReadStatus(root, "Folder").installed);
    fs::remove(layout.ManifestFile(), ec);

    // 4. A read-only GamerSettings.ini: the install goes on (with a warning), Settings > Apply says why it can't.
    fresh();
    SetFileAttributesW(layout.Full(paths::kGamerSettings).c_str(), FILE_ATTRIBUTE_READONLY);
    ICHECK(ReadStatus(root, "Folder").gamerSettingsReadOnly);
    Outcome const ro = Install(layout, payload, options);
    ICHECK(ro.ok && fs::exists(layout.Full(paths::kModAsi)) && !ReadStatus(root, "Folder").gameSettings);
    bool warned = false;
    for (std::string const& line : ro.log) {
        warned = warned || (line.find("read-only") != std::string::npos && line.find("GamerSettings.ini") != std::string::npos);
    }
    ICHECK(warned);
    Outcome const apply = ApplySettings(layout, options.choices);
    ICHECK(!apply.ok && !apply.errors.empty() && apply.errors.back().find("read-only") != std::string::npos);
    ICHECK(Read(layout.Full(paths::kGamerSettings)) == gamer);
    SetFileAttributesW(layout.Full(paths::kGamerSettings).c_str(), FILE_ATTRIBUTE_NORMAL);

    // 5. The local data box: %LOCALAPPDATA%\MEVR only when asked (here a stand-in LOCALAPPDATA).
    wchar_t realLocal[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", realLocal, MAX_PATH);
    fs::path const fakeLocal = base / L"LocalAppData";
    SetEnvironmentVariableW(L"LOCALAPPDATA", fakeLocal.c_str());
    Write(fakeLocal / L"MEVR" / L"snapshots" / L"a.png", "png");
    ICHECK(Ok(Uninstall(layout, false, {}, false)) && fs::exists(fakeLocal / L"MEVR" / L"snapshots" / L"a.png"));
    ICHECK(Ok(Install(layout, payload, options)));
    ICHECK(Ok(Uninstall(layout, false, {}, true)) && !fs::exists(fakeLocal / L"MEVR"));
    SetEnvironmentVariableW(L"LOCALAPPDATA", realLocal);

    // 6. A lost install record, then a reinstall over our loader (the built bink2w64.dll next to this exe: its version
    // resource says MEVR): adopted, so the uninstall takes it all out and puts the original back.
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    fs::path const builtLoader = fs::path(self).parent_path() / L"bink2w64.dll";
    if (fs::exists(builtLoader)) {
        fresh();
        fs::copy_file(builtLoader, layout.Win64() / L"bink2w64.dll", fs::copy_options::overwrite_existing, ec);
        Write(payloadDir / L"bink2w64.dll", Read(builtLoader));
        ICHECK(ReadStatus(root, "Folder").bink == detect::BinkState::Ours);
        InstallOptions plain = options;
        plain.gameSettings = false;
        ICHECK(Ok(Install(layout, payload, plain)));
        fs::remove(layout.ManifestFile(), ec);
        Outcome const again = Install(layout, payload, plain);
        ICHECK(again.ok);
        {
            Transaction tx(layout);
            ICHECK(tx.Load());
            FileEntry const* bink = tx.Data().Find(paths::kBink);
            FileEntry const* original = tx.Data().Find(paths::kBinkOriginal);
            FileEntry const* asi = tx.Data().Find(paths::kModAsi);
            ICHECK(bink != nullptr && bink->action == "added" && original != nullptr && original->action == "renamed");
            ICHECK(asi != nullptr && asi->action == "added");
        }
        ICHECK(Ok(Uninstall(layout, false, {})));
        ICHECK(Read(layout.Win64() / L"bink2w64.dll") == "RAD original" && !fs::exists(layout.Win64() / L"bink2w64_original.dll"));
        ICHECK(!fs::exists(layout.Asi()));
        Write(payloadDir / L"bink2w64.dll", "our loader");
    } else {
        std::printf("  (no built bink2w64.dll next to the tests: the lost-record case is skipped)\n");
    }
    fs::remove_all(base, ec);
}

// A small PCConsoleTOC.bin built the game's way (7 buckets, five packages).
std::string SmallToc() {
    namespace toc = mevr::install::toc;
    toc::File file;
    file.buckets.resize(7);
    for (char const* bare : {"A.pcc", "Bb.pcc", "Ccc.pcc", "Dddd.pcc", "Startup.pcc"}) {
        toc::Entry e;
        e.name = std::string("BioGame\\CookedPCConsole\\") + bare;
        e.size = static_cast<std::uint32_t>(e.name.size() * 1000);
        file.buckets[toc::Hash(bare) % 7].push_back(e);
    }
    return toc::Serialize(file);
}

// The first-run problems of research/33 section 9 through the Installer, on a fake game under %TEMP%: a game never
// started (no GamerSettings.ini: the settings wait, Repair applies them once the game has made the file), an upgrade from
// Halcyon's upstream MELE-VR (its MELEVR.ini normalised, each change undone by a "keep my settings" uninstall), and
// Steam's "verify files" (the cockpit's TOC entry back by Repair).
void TestFirstRunInstall() {
    using namespace mevr::install;
    std::filesystem::path const temp = mevr::tests::TempRoot();
    fs::path const base = fs::path(temp) / L"mevr-install-firstrun";
    fs::path const root = base / L"Mass Effect Legendary Edition";
    if (detect::GameRunningIn(root)) {
        std::printf("  (a game runs from the test folder: TestFirstRunInstall is skipped)\n");
        return;
    }
    std::error_code ec;
    fs::remove_all(base, ec);
    Layout const layout{root};
    fs::path const payloadDir = base / L"payload";
    Write(payloadDir / L"LE1MEVR.asi", "new ASI");
    Write(payloadDir / L"bink2w64.dll", "our loader");
    Write(payloadDir / L"fonts" / L"A.ttf", "font");
    Write(payloadDir / L"menu" / L"main.rml", "<rml/>");
    Write(payloadDir / L"cockpit" / L"BIOG_MEVR_Cockpit.pcc", std::string(4321, 'p'));
    Write(payloadDir / L"cockpit" / L"cockpit.txt", "seat 0 0 0\r\n");
    Payload const payload{payloadDir};
    ICHECK(payload.Valid() && payload.HasCockpit());
    std::string const toc = SmallToc();
    auto fresh = [&] {
        fs::remove_all(root, ec);
        Write(layout.Win64() / L"MassEffect1.exe", "exe");
        Write(layout.Win64() / L"bink2w64.dll", "a proxy");  // no version resource: "another proxy" in front of...
        Write(layout.Win64() / L"bink2w64_original.dll", "RAD original");
        Write(layout.Full(paths::kToc), toc);
    };
    InstallOptions options;
    options.ignoreVersion = true;
    options.choices.target = settings::TargetFor("auto", 0, 0);  // the setup's default with no headset size known
    options.choices.eyeSizeAtFirstStart = true;

    // 1. Never started: no GamerSettings.ini. The install goes on, says so, and keeps the choices.
    fresh();
    ICHECK(!ReadStatus(root, "Folder").hasGamerSettings);
    Outcome const in = Install(layout, payload, options);
    ICHECK(in.ok);
    bool said = false;
    for (std::string const& line : in.log) {
        said = said || (line.find("hasn't been started") != std::string::npos && line.find("GamerSettings.ini") != std::string::npos);
    }
    ICHECK(said);
    Status s = ReadStatus(root, "Folder");
    ICHECK(s.installed && !s.gameSettings && s.pendingSettings.has_value());
    ICHECK(s.pendingSettings && s.pendingSettings->width == 3072 && s.pendingSettings->height == 3188 && s.pendingSettings->hdrOff);
    ICHECK(s.eyeSizeAtFirstStart && s.pendingSettings && s.pendingSettings->eyeSizeAtFirstStart);  // the mod's job later
    ICHECK(s.needsRepair.empty());
    ICHECK(ini::Get(Read(layout.Full(paths::kHalcyonIni)), "profile:Default", "sfr2ResX") == std::optional<std::string>("4096"));  // not yet
    // Repair before the first start: nothing to apply yet, and it says why.
    Outcome const early = Repair(layout, payload);
    ICHECK(early.ok && ReadStatus(root, "Folder").pendingSettings.has_value() && !fs::exists(layout.Full(paths::kGamerSettings)));
    // The game's first start makes the file; Repair applies what was chosen at install.
    std::string const gamer = "[SystemSettings]\r\nFullscreen=True\r\nResX=1920\r\nResY=1080\r\nUseVSync=True\r\n\r\n[HDR]\r\nDesiredRange=DynamicRange_HDR\r\n";
    Write(layout.Full(paths::kGamerSettings), gamer);
    ICHECK(ReadStatus(root, "Folder").hasGamerSettings);
    Outcome const later = Repair(layout, payload);
    ICHECK(later.ok);
    std::string const g = Read(layout.Full(paths::kGamerSettings));
    ICHECK(settings::Read(g).resX == 3072 && settings::Read(g).resY == 3188 && settings::Read(g).borderless && !settings::Read(g).hdr);
    ICHECK(ini::Get(Read(layout.Full(paths::kHalcyonIni)), "profile:Default", "sfr2ResX") == std::optional<std::string>("3072"));
    s = ReadStatus(root, "Folder");
    ICHECK(s.gameSettings && !s.pendingSettings && s.eyeSizeAtFirstStart);  // carried through the pending settings
    // Restore: the game's own file again; nothing waits.
    ICHECK(Ok(RestoreSettings(layout)) && Read(layout.Full(paths::kGamerSettings)) == gamer && !ReadStatus(root, "Folder").pendingSettings);
    ICHECK(!ReadStatus(root, "Folder").eyeSizeAtFirstStart);
    ICHECK(Ok(Uninstall(layout, false, {})));
    ICHECK(Read(layout.Full(paths::kToc)) == toc && !fs::exists(layout.Asi()));
    // The choices survive the manifest's JSON.
    {
        Manifest m;
        m.pendingSettings = PendingFromChoices(options.choices);
        auto const back = Manifest::FromJson(m.ToJson());
        ICHECK(back && back->pendingSettings == m.pendingSettings);
        ICHECK(ChoicesFromPending(*m.pendingSettings).target.width == 3072);
        ICHECK(Manifest::FromJson(Manifest{}.ToJson()) && !Manifest::FromJson(Manifest{}.ToJson())->pendingSettings);
    }

    // 2. Upgrading from upstream: its MELEVR.ini put in the ASI folder. Normalised on install (every slot), its other
    // values kept; a "keep my settings" uninstall puts back each value still as we set it; a value the player changed since
    // stays theirs.
    fresh();
    Write(layout.Full(paths::kGamerSettings), gamer);
    std::string const upstream = "[active]\r\nprofile=Default\r\n\r\n[profile:Default]\r\nvrMode = 1\r\nsfr2ResX = 4096\r\nsfr2ResY = 4250\r\n"
                                 "firstRunDone = 0\r\nmenuKey = 45\r\nfpEnabled = 0\r\nmirrorPresentEvery = 1\r\nsfr2HalfEyeUU = 3.4\r\n\r\n"
                                 "[profile:Custom 1]\r\nvrMode = 3\r\nfpEnabled = 0\r\n";
    Write(layout.Full(paths::kHalcyonIni), upstream);
    ICHECK(Ok(Install(layout, payload, options)));
    std::string h = Read(layout.Full(paths::kHalcyonIni));
    for (char const* slot : {"profile:Default", "profile:Custom 1"}) {
        ICHECK(ini::Get(h, slot, "vrMode") == std::optional<std::string>("2"));
        ICHECK(ini::Get(h, slot, "fpEnabled") == std::optional<std::string>("1"));
        ICHECK(ini::Get(h, slot, "firstRunDone") == std::optional<std::string>("1"));
        ICHECK(ini::Get(h, slot, "mirrorPresentEvery") == std::optional<std::string>("2"));
        ICHECK(ini::Get(h, slot, "menuKey") == std::optional<std::string>("220"));
    }
    ICHECK(ini::Get(h, "profile:Default", "sfr2HalfEyeUU") == std::optional<std::string>("3.4"));
    ICHECK(ini::Get(h, "profile:Default", "sfr2ResX") == std::optional<std::string>("3072"));  // the VR game settings' target
    {
        Transaction tx(layout);
        ICHECK(tx.Load());
        FileEntry const* entry = tx.Data().Find(paths::kHalcyonIni);
        ICHECK(entry != nullptr && entry->userData && entry->edits.size() >= 10);  // every change on record
    }
    // An update over the top changes nothing more: the slots are this mod's now.
    ICHECK(Ok(Install(layout, payload, options)) && Read(layout.Full(paths::kHalcyonIni)) == h);
    ICHECK(Ok(Uninstall(layout, true, {})));
    ICHECK(Read(layout.Full(paths::kHalcyonIni)) == upstream);  // kept, exactly as it came
    // Again, and this time the player picks Depth (DIBR) in the Custom 1 slot through our menu before uninstalling (the
    // in-game save writes every key, sfr2MetricDepth among them).
    ICHECK(Ok(Install(layout, payload, options)));
    h = ini::Set(Read(layout.Full(paths::kHalcyonIni)), "profile:Custom 1", "vrMode", "3");
    h = ini::Set(h, "profile:Custom 1", "sfr2MetricDepth", "1.00");
    Write(layout.Full(paths::kHalcyonIni), h);
    ICHECK(Ok(Repair(layout, payload)) && ini::Get(Read(layout.Full(paths::kHalcyonIni)), "profile:Custom 1", "vrMode") == std::optional<std::string>("3"));
    ICHECK(Ok(Uninstall(layout, true, {})));
    std::string const kept = Read(layout.Full(paths::kHalcyonIni));
    ICHECK(ini::Get(kept, "profile:Custom 1", "vrMode") == std::optional<std::string>("3"));  // theirs since
    ICHECK(ini::Get(kept, "profile:Default", "vrMode") == std::optional<std::string>("1"));   // ours, put back
    ICHECK(ini::Get(kept, "profile:Default", "menuKey") == std::optional<std::string>("45"));
    // Reinstalled: the slot the player saved in game is still theirs (Depth stays); the untouched one is normalised again.
    ICHECK(Ok(Install(layout, payload, options)));
    h = Read(layout.Full(paths::kHalcyonIni));
    ICHECK(ini::Get(h, "profile:Custom 1", "vrMode") == std::optional<std::string>("3"));
    ICHECK(ini::Get(h, "profile:Default", "vrMode") == std::optional<std::string>("2"));
    // The player deletes it; Repair writes the release file, wholly ours from then on: a "keep" uninstall leaves it as written
    // (the key edits recorded on the old file don't apply to it).
    fs::remove(layout.Full(paths::kHalcyonIni), ec);
    ICHECK(Ok(Repair(layout, payload)) && Read(layout.Full(paths::kHalcyonIni)) == ini::HalcyonReleaseIni());
    ICHECK(Ok(Uninstall(layout, true, {})) && Read(layout.Full(paths::kHalcyonIni)) == ini::HalcyonReleaseIni());
    // A full uninstall deletes it (the player's VR data, as before).
    ICHECK(Ok(Install(layout, payload, options)));
    ICHECK(Ok(Uninstall(layout, false, {})) && !fs::exists(layout.Full(paths::kHalcyonIni)));

    // 3. Steam's "verify files": the game's own TOC back (and the cockpit's package gone): Status says so, Repair puts both back.
    fresh();
    Write(layout.Full(paths::kGamerSettings), gamer);
    ICHECK(Ok(Install(layout, payload, options)));
    std::string const installedToc = Read(layout.Full(paths::kToc));
    ICHECK(installedToc != toc && ReadStatus(root, "Folder").needsRepair.empty());
    Write(layout.Full(paths::kToc), toc);
    fs::remove(layout.Full(paths::kCockpitPackage), ec);
    std::vector<std::string> const bad = ReadStatus(root, "Folder").needsRepair;
    ICHECK(bad.size() == 2 && std::find(bad.begin(), bad.end(), paths::kToc) != bad.end() &&
           std::find(bad.begin(), bad.end(), paths::kCockpitPackage) != bad.end());
    ICHECK(Ok(Repair(layout, payload)));
    ICHECK(Read(layout.Full(paths::kToc)) == installedToc && fs::exists(layout.Full(paths::kCockpitPackage)));
    ICHECK(ReadStatus(root, "Folder").needsRepair.empty());
    ICHECK(Ok(Uninstall(layout, false, {})) && Read(layout.Full(paths::kToc)) == toc);

    // 4. A lost install record, then a reinstall over our loader (the built bink2w64.dll next to this exe): the cockpit's
    // script, package and TOC entry are adopted with the rest, so the uninstall takes them out too.
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    fs::path const builtLoader = fs::path(self).parent_path() / L"bink2w64.dll";
    if (fs::exists(builtLoader)) {
        fresh();
        fs::copy_file(builtLoader, layout.Win64() / L"bink2w64.dll", fs::copy_options::overwrite_existing, ec);
        Write(payloadDir / L"bink2w64.dll", Read(builtLoader));
        InstallOptions plain = options;
        plain.gameSettings = false;
        ICHECK(Ok(Install(layout, payload, plain)));
        fs::remove(layout.ManifestFile(), ec);
        ICHECK(Ok(Install(layout, payload, plain)));
        {
            Transaction tx(layout);
            ICHECK(tx.Load());
            FileEntry const* pkg = tx.Data().Find(paths::kCockpitPackage);
            FileEntry const* tocEntry = tx.Data().Find(paths::kToc);
            ICHECK(pkg != nullptr && pkg->action == "added");
            ICHECK(tocEntry != nullptr && tocEntry->toc.size() == 1 && !tocEntry->toc[0].before);
        }
        ICHECK(Ok(Uninstall(layout, false, {})));
        ICHECK(Read(layout.Full(paths::kToc)) == toc && !fs::exists(layout.Full(paths::kCockpitPackage)) && !fs::exists(layout.Asi()));
        ICHECK(Read(layout.Win64() / L"bink2w64.dll") == "RAD original");
    } else {
        std::printf("  (no built bink2w64.dll next to the tests: the lost-record cockpit case is skipped)\n");
    }
    fs::remove_all(base, ec);
}

// PCConsoleTOC.bin (install/Toc, research/47 "Players get it").
void TestToc() {
    namespace toc = mevr::install::toc;
    // Where the game's own TOC (5126 buckets) keeps these: the first bucket, the last, and where the packager put ours.
    ICHECK(toc::Hash("BIOA_END80_00_DSG.pcc") % 5126 == 0);
    ICHECK(toc::Hash("BIOA_WAR30_02_PLC.pcc") % 5126 == 5125);
    ICHECK(toc::Hash("BIOG_MEVR_Cockpit.pcc") % 5126 == 1558);
    ICHECK(toc::Hash("biog_mevr_cockpit.PCC") == toc::Hash("BIOG_MEVR_Cockpit.pcc"));
    // A small TOC built the game's way, written, read back, edited.
    toc::File file;
    file.buckets.resize(7);
    for (char const* bare : {"A.pcc", "Bb.pcc", "Ccc.pcc", "Dddd.pcc", "Startup.pcc"}) {
        toc::Entry e;
        e.name = std::string("BioGame\\CookedPCConsole\\") + bare;
        e.size = static_cast<std::uint32_t>(e.name.size() * 1000);
        file.buckets[toc::Hash(bare) % 7].push_back(e);
    }
    ICHECK(toc::Consistent(file));
    std::string const bytes = toc::Serialize(file);
    ICHECK(bytes.size() % 4 == 0);
    auto const back = toc::Parse(bytes);
    ICHECK(back.has_value() && toc::Serialize(*back) == bytes);
    if (!back) {
        return;
    }
    toc::File edited = *back;
    std::string const name = "BioGame\\CookedPCConsole\\BIOG_MEVR_Cockpit.pcc";
    ICHECK(!toc::Find(edited, name));
    ICHECK(!toc::Set(edited, name, 29338932));  // added
    ICHECK(toc::Consistent(edited));
    auto const reread = toc::Parse(toc::Serialize(edited));
    ICHECK(reread && toc::Find(*reread, "biogame\\cookedpcconsole\\biog_mevr_cockpit.pcc") == std::optional<std::uint32_t>(29338932));
    ICHECK(toc::Set(edited, name, 5) == std::optional<std::uint32_t>(29338932));  // updated
    ICHECK(toc::Remove(edited, name) && !toc::Remove(edited, name));
    ICHECK(toc::Serialize(edited) == bytes);  // out again: byte for byte the TOC before
    // An entry in the wrong bucket: not a TOC we edit.
    toc::File wrong = *back;
    toc::Entry stray;
    stray.name = "BioGame\\CookedPCConsole\\A.pcc";
    wrong.buckets[(toc::Hash("A.pcc") + 1) % 7].push_back(stray);
    ICHECK(!toc::Consistent(wrong));
    ICHECK(!toc::Parse("not a toc"));
    // The game's own file, when given (MEVR_TOC_FILE): read and written back byte for byte.
    if (wchar_t real[MAX_PATH]{}; GetEnvironmentVariableW(L"MEVR_TOC_FILE", real, MAX_PATH) > 0) {
        auto const game = mevr::install::ReadFile(fs::path(real));
        auto const parsed = game ? toc::Parse(*game) : std::nullopt;
        ICHECK(parsed && toc::Consistent(*parsed) && toc::Serialize(*parsed) == *game);
        std::printf("  TOC round trip of the game's own file: %s\n", parsed && toc::Serialize(*parsed) == *game ? "identical" : "DIFFERENT");
    }

    // Through the manifest: set, verify, uninstall takes it out again (a fake game folder; no game needed).
    std::filesystem::path const temp = mevr::tests::TempRoot();
    fs::path const base = fs::path(temp) / L"mevr-install-toc";
    std::error_code ec;
    fs::remove_all(base, ec);
    mevr::install::Layout const layout{base / L"Mass Effect Legendary Edition"};
    namespace paths = mevr::install::paths;
    Write(layout.Full(paths::kToc), bytes);
    {
        mevr::install::Transaction tx(layout);
        ICHECK(tx.Load());
        ICHECK(tx.SetTocEntry(paths::kToc, paths::kCockpitTocName, 1234));
        ICHECK(tx.Verify().empty());
    }
    auto const installed = toc::Parse(Read(layout.Full(paths::kToc)));
    ICHECK(installed && toc::Find(*installed, paths::kCockpitTocName) == std::optional<std::uint32_t>(1234));
    {
        // Steam's "verify files" puts the game's TOC back: Verify reports it for Repair.
        Write(layout.Full(paths::kToc), bytes);
        mevr::install::Transaction tx(layout);
        ICHECK(tx.Load());
        auto const bad = tx.Verify();
        ICHECK(bad.size() == 1 && bad[0] == paths::kToc);
        ICHECK(tx.SetTocEntry(paths::kToc, paths::kCockpitTocName, 1234) && tx.Verify().empty());
        // The manifest survives a reload with the entry's record.
        mevr::install::Transaction again(layout);
        ICHECK(again.Load());
        auto const* entry = again.Data().Find(paths::kToc);
        ICHECK(entry != nullptr && entry->action == "toc" && entry->toc.size() == 1 && !entry->toc[0].before && entry->toc[0].after == 1234);
        ICHECK(again.Uninstall(false));
    }
    ICHECK(Read(layout.Full(paths::kToc)) == bytes);
    fs::remove_all(base, ec);
}

void TestLoaderCore() {
    namespace ld = mevr::loader;
    using G = mevr::loader::spi::Game;
    ICHECK(ld::HostFromExeName(L"MassEffect1.exe") == G::LE1);
    ICHECK(ld::HostFromExeName(L"masseffect3.EXE") == G::LE3);
    ICHECK(ld::HostFromExeName(L"MassEffectLauncher.exe") == G::Launcher);
    ICHECK(!ld::HostFromExeName(L"mevr-loader-test.exe"));
    bool tooLong = false;
    auto const p = ld::ParsePattern("48 8B ?? 05 ? C3");
    ICHECK(p && p->bytes.size() == 6 && !p->mask[2] && !p->mask[4] && p->bytes[1] == 0x8B);
    ICHECK(!ld::ParsePattern("48 8G"));
    ICHECK(!ld::ParsePattern("?? 48"));  // a leading wildcard has nothing to anchor on
    ICHECK(!ld::ParsePattern(""));
    std::string longText;
    for (int i = 0; i < 101; ++i) {
        longText += "90 ";
    }
    ICHECK(!ld::ParsePattern(longText, &tooLong) && tooLong);
    std::uint8_t const memory[] = {0x00, 0x48, 0x8B, 0x01, 0x48, 0x8B, 0x77, 0x05, 0x11, 0xC3, 0x00};
    ICHECK(p && ld::FindPattern(memory, *p) == std::optional<std::size_t>(4));
    auto const missing = ld::ParsePattern("C3 C3");
    ICHECK(missing && !ld::FindPattern(memory, *missing));
    // The DRM check: 90% of the samples must differ from the file.
    ICHECK(ld::LooksDecrypted(256, 256));
    ICHECK(ld::LooksDecrypted(231, 256));
    ICHECK(!ld::LooksDecrypted(200, 256));
    ICHECK(!ld::LooksDecrypted(3, 256));  // still encrypted: only relocated windows differ
    ICHECK(!ld::LooksDecrypted(0, 0));
    auto const offsets = ld::SampleOffsets(0xF9BB81, 256, 16);
    ICHECK(offsets.size() == 256 && offsets.front() == 0 && offsets.back() == 0xF9BB81 - 16);
    ICHECK(ld::SampleOffsets(8, 4, 16).empty());
    // Which plugins attach: LE1MEVR.asi declares LE1 only (1 << 1) and SPI 3.
    ICHECK(ld::ShouldAttach(1 << 1, 3, G::LE1));
    ICHECK(!ld::ShouldAttach(1 << 1, 3, G::LE2));
    ICHECK(!ld::ShouldAttach(1 << 1, 4, G::LE1));
    ICHECK(ld::ShouldAttach(0, 3, std::nullopt));
    auto const sorted = ld::SortAsiNames({L"b.asi", L"A.asi", L"LE1MEVR.asi"});
    ICHECK(sorted.size() == 3 && sorted[0] == L"A.asi" && sorted[2] == L"LE1MEVR.asi");
}

// The voice orders as optional content (research/33 section 14, install/Voice): the catalogue's layout matches what
// plugin/Voice and plugin/VoiceModel look for, the model's release lookup, and the manifest's content entries (placed,
// checked by size or SHA-256, left out of Repair's check, removed alone or with the uninstall). No network: Install is
// only run up to its refusals.
void TestVoice() {
    using namespace mevr::install;
    namespace voice = mevr::install::voice;
    auto under = [](std::string_view path, std::string_view dir) { return path.size() > dir.size() + 1 && path.substr(0, dir.size()) == dir && path[dir.size()] == '/'; };
    auto hex64 = [](std::string const& s) { return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos; };

    // ---- The catalogue: tools/fetch-voice.ps1's files, where tools/deploy-asi.ps1 puts them.
    std::vector<voice::Part> const& parts = voice::Catalogue();
    ICHECK(parts.size() == 6 && parts.front().id == "recognizer" && parts.back().id == "llm-model");
    ICHECK(std::string(voice::kDir) == std::string(paths::kAsi) + "/voice");
    ICHECK(std::string(voice::kDll) == std::string(voice::kDir) + "/MEVR-Voice.dll");
    ICHECK(std::string(voice::kLlmModel) == std::string(voice::kLlmDir) + "/model.gguf");  // plugin/VoiceModel's default
    std::vector<std::string> targets;
    for (voice::Part const& p : parts) {
        ICHECK(!p.name.empty() && !p.licence.empty() && !p.origin.empty());
        ICHECK(voice::Find(p.id) == &p);
        ICHECK(voice::ContentTag(p.id) == "voice/" + p.id);
        for (voice::Source const& s : p.sources) {
            ICHECK(s.url.rfind("https://github.com/", 0) == 0 || s.url.rfind("https://huggingface.co/", 0) == 0);
            ICHECK(s.size > 0 && hex64(s.sha256));  // every fixed download pinned
            ICHECK(!s.files.empty());
            ICHECK((s.archive == voice::Archive::None) == (s.files.size() == 1 && s.files[0].member.empty()));
            for (voice::FileSpec const& f : s.files) {
                ICHECK(under(f.target, voice::kDir));
                ICHECK(f.size > 0);
                ICHECK(f.sha256.empty() || hex64(f.sha256));
                ICHECK(std::find(targets.begin(), targets.end(), f.target) == targets.end());  // one part per file
                targets.push_back(f.target);
            }
        }
    }
    ICHECK(voice::Find("llm-model")->sources.empty() && voice::Find("llm-model")->modelSize > 600'000'000ull);
    ICHECK(voice::Find("nope") == nullptr);
    auto has = [&](std::string const& t) { return std::find(targets.begin(), targets.end(), t) != targets.end(); };
    ICHECK(has(voice::kOnnxRuntime));
    for (char const* size : {"tiny", "small", "medium"}) {
        // plugin/Voice: ASI\voice\models\<model>-streaming-en with its streaming_config.json; Moonshine's eight files.
        std::string const dir = std::string(voice::kModelsDir) + "/" + size + "-streaming-en/";
        for (char const* f : {"streaming_config.json", "frontend.model.ort", "frontend.weights.ort", "encoder.ort", "adapter.ort", "cross_kv.ort",
                 "decoder_kv.ort", "tokenizer.bin"}) {
            ICHECK(has(dir + f));
        }
        voice::Part const* p = voice::Find(std::string("models-") + size);
        ICHECK(p != nullptr && p->sources.size() == 8);
    }
    // plugin/VoiceModel loads llama.dll from ASI\voice\llm; it imports ggml and ggml-base, which imports libomp; ggml loads a CPU backend.
    for (char const* f : {"llama.dll", "ggml.dll", "ggml-base.dll", "libomp.dll", "ggml-cpu-x64.dll", "ggml-cpu-haswell.dll"}) {
        ICHECK(has(std::string(voice::kLlmDir) + "/" + f));
    }
    ICHECK(!has(voice::kDll));  // ours ships in the release, never downloaded

    // ---- The choices.
    ICHECK((voice::PartsFor({}) == std::vector<std::string>{"recognizer", "models-tiny", "llm-runtime", "llm-model"}));
    voice::Choices bare;
    bare.llm = false;
    ICHECK((voice::PartsFor(bare) == std::vector<std::string>{"recognizer", "models-tiny"}));
    voice::Choices all{true, true, true};
    ICHECK(voice::PartsFor(all).size() == 6);
    voice::Sizes const small = voice::SizesFor(voice::PartsFor(bare));
    ICHECK(small.download > 60'000'000ull && small.download < 80'000'000ull);  // Moonshine's release (26 MB) and the tiny models (45 MB)
    ICHECK(small.disk > 55'000'000ull && small.disk < 65'000'000ull);         // onnxruntime.dll (14 MB) and the models
    ICHECK(voice::SizesFor({"llm-model"}).download == voice::Find("llm-model")->modelSize);
    ICHECK(voice::SizesFor({"llm-model"}, 1234).download == 1234);
    ICHECK(voice::SizesFor({"nope"}).download == 0);

    // ---- The model's release.
    ICHECK(voice::ReleaseByTagApi(updates::kDefaultRepo, voice::kModelTag) ==
        "https://api.github.com/repos/dnehring/MEVR/releases/tags/voice-model-1");
    ICHECK(voice::ReleaseByTagApi("https://github.com/someone/fork.git/", "v1.2") == "https://api.github.com/repos/someone/fork/releases/tags/v1.2");
    ICHECK(voice::ReleaseByTagApi(updates::kDefaultRepo, "a/b").empty());
    ICHECK(voice::ReleaseByTagApi(updates::kDefaultRepo, "").empty());
    ICHECK(voice::ReleaseByTagApi(updates::kPlaceholderRepo, voice::kModelTag).empty());
    ICHECK(voice::ReleaseByTagApi("https://example.com/a/b", voice::kModelTag).empty());
    std::string const sha(64, 'a');
    std::string const release = R"({"tag_name": "voice-model-1", "assets": [
        {"name": "notes.txt", "browser_download_url": "https://github.com/dnehring/MEVR/releases/download/voice-model-1/notes.txt", "size": 5},
        {"name": "model.gguf", "size": 639446880, "digest": "sha256:)" + std::string(64, 'A') + R"(",
         "browser_download_url": "https://github.com/dnehring/MEVR/releases/download/voice-model-1/model.gguf"}]})";
    std::string why;
    auto const model = voice::ModelFromRelease(release, voice::kModelAsset, &why);
    ICHECK(model.has_value());
    if (model) {
        ICHECK(model->size == 639446880ull && model->sha256 == sha && model->archive == voice::Archive::None);
        ICHECK(model->files.size() == 1 && model->files[0].target == voice::kLlmModel && model->files[0].sha256 == sha);
    }
    ICHECK(!voice::ModelFromRelease(release, "other.gguf", &why) && why.find("has no file called other.gguf") != std::string::npos);
    ICHECK(!voice::ModelFromRelease(R"({"assets": [{"name": "m.gguf", "browser_download_url": "https://evil.example/m.gguf", "size": 1}]})", "m.gguf", &why) &&
        why.find("github.com") != std::string::npos);
    ICHECK(!voice::ModelFromRelease("[]", "m.gguf", &why));
    auto const noDigest = voice::ModelFromRelease(R"({"assets": [{"name": "m.gguf", "browser_download_url": "https://github.com/a/b/m.gguf", "size": 7,
        "digest": "md5:abc"}]})", "m.gguf");
    ICHECK(noDigest && noDigest->sha256.empty() && noDigest->size == 7);

    std::filesystem::path const temp = mevr::tests::TempRoot();
    fs::path const base = fs::path(temp) / L"mevr-install-voice";
    std::error_code ec;
    fs::remove_all(base, ec);
    {
        voice::ModelSource const none = voice::ReadModelSource(base / L"missing.ini");
        ICHECK(none.repo == updates::kDefaultRepo && none.tag == voice::kModelTag && none.asset == voice::kModelAsset && none.url.empty());
        Write(base / L"MEVR-Setup.ini", "[Update]\r\nrepo = https://github.com/someone/fork\r\n[Voice]\r\nmodelTag = voice-model-2 \r\n"
                                         "modelSha256 = not-a-sum\r\nmodelSize = 12x\r\n");
        voice::ModelSource const set = voice::ReadModelSource(base / L"MEVR-Setup.ini");
        ICHECK(set.repo == "https://github.com/someone/fork" && set.tag == "voice-model-2" && set.sha256.empty() && set.size == 0);
        Write(base / L"MEVR-Setup.ini", "[Voice]\r\nmodelRepo = https://github.com/a/b\r\nmodelUrl = https://example.org/m.gguf\r\nmodelSha256 = " +
            std::string(64, 'B') + "\r\nmodelSize = 99\r\n");
        voice::ModelSource const direct = voice::ReadModelSource(base / L"MEVR-Setup.ini");
        ICHECK(direct.repo == "https://github.com/a/b" && direct.url == "https://example.org/m.gguf" && direct.sha256 == std::string(64, 'b') &&
            direct.size == 99);
        voice::ModelSource plain = direct;
        plain.url = "http://example.org/m.gguf";
        ICHECK(!voice::FindModel(plain).source);  // only https (no request made)
        voice::ModelLookup const fromUrl = voice::FindModel(direct);
        ICHECK(fromUrl.source && fromUrl.source->files.size() == 1 && fromUrl.source->files[0].target == voice::kLlmModel && fromUrl.source->size == 99);
    }
    {
        auto const req = ParseSetupRequest(R"({"page": "content", "packs": ["voice", "nope", "alot-le1"], "pid": 1})");
        ICHECK(req && (req->packs == std::vector<std::string>{"voice", "alot-le1"}));
    }

    // ---- The manifest's content entries on a fake game.
    fs::path const root = base / L"Mass Effect Legendary Edition";
    if (detect::GameRunningIn(root)) {
        std::printf("  (a game runs from the test folder: TestVoice's file part is skipped)\n");
        return;
    }
    Layout const layout{root};
    Write(layout.Win64() / L"MassEffect1.exe", "exe");
    Write(layout.Win64() / L"bink2w64.dll", "RAD original");
    fs::path const downloads = base / L"downloads";
    std::string const tiny = std::string(voice::kModelsDir) + "/tiny-streaming-en/encoder.ort";
    std::string const config = std::string(voice::kModelsDir) + "/tiny-streaming-en/streaming_config.json";
    std::string const gguf = voice::kLlmModel;
    Write(layout.Full(gguf), "a model the player put there");  // replaced, put back by Remove
    {
        Transaction tx(layout);
        ICHECK(tx.Load() && !tx.Exists());
        ICHECK(!voice::ReadState(layout).dll);
        Write(base / L"MEVR-Voice.dll", "our recognizer interface");
        ICHECK(tx.CopyIn(base / L"MEVR-Voice.dll", voice::kDll));  // the mod's own
        Write(downloads / L"encoder.ort", "encoder bytes");
        Write(downloads / L"streaming_config.json", "{}");
        Write(downloads / L"model.gguf", "fine-tuned");
        ICHECK(tx.PlaceIn(downloads / L"encoder.ort", tiny, Sha256("encoder bytes"), voice::ContentTag("models-tiny")));
        ICHECK(tx.PlaceIn(downloads / L"streaming_config.json", config, Sha256("{}"), voice::ContentTag("models-tiny")));
        ICHECK(tx.PlaceIn(downloads / L"model.gguf", gguf, Sha256("fine-tuned"), voice::ContentTag("llm-model")));
        ICHECK(!fs::exists(downloads / L"encoder.ort"));  // moved, not copied
        ICHECK(!tx.PlaceIn(downloads / L"gone.bin", config, "", voice::ContentTag("models-tiny")));  // a missing download
        ICHECK(tx.Errors().size() == 1);
    }
    ICHECK(Read(layout.Full(tiny)) == "encoder bytes" && Read(layout.Full(gguf)) == "fine-tuned");
    {
        Transaction tx(layout);
        ICHECK(tx.Load() && tx.Exists());
        FileEntry const* e = tx.Data().Find(tiny);
        ICHECK(e != nullptr && e->action == "added" && e->content == "voice/models-tiny" && e->size == 13 && e->sha256 == Sha256("encoder bytes"));
        FileEntry const* m = tx.Data().Find(gguf);
        ICHECK(m != nullptr && m->action == "replaced" && !m->backup.empty() && m->content == "voice/llm-model");
        FileEntry const* dll = tx.Data().Find(voice::kDll);
        ICHECK(dll != nullptr && dll->content.empty());
        // The manifest's JSON keeps them.
        auto const again = Manifest::FromJson(tx.Data().ToJson());
        ICHECK(again && again->Find(tiny) && again->Find(tiny)->content == "voice/models-tiny" && again->Find(tiny)->size == 13);
        ICHECK(again && again->Find(voice::kDll) && again->Find(voice::kDll)->content.empty());
        ICHECK(tx.VerifyContent("voice", false).empty() && tx.VerifyContent("voice", true).empty());
        ICHECK(tx.VerifyContent("voice/models-tiny", true).empty() && tx.VerifyContent("voi", false).empty());
    }
    voice::State st = voice::ReadState(layout);
    ICHECK(st.dll && st.Has("models-tiny") && st.Has("llm-model") && !st.Has("recognizer") && st.damaged.empty() && !st.recorded);
    // Changed files: Repair's check never lists downloads (it can't put them back); the content check does, by size quickly and
    // by SHA-256 when asked.
    Write(layout.Full(tiny), "encoder BYTES");  // the same size
    Write(layout.Full(config), "{ }");          // another size
    {
        Transaction tx(layout);
        ICHECK(tx.Load());
        ICHECK(tx.Verify().empty());
        ICHECK((tx.VerifyContent("voice", false) == std::vector<std::string>{config}));
        auto const deep = tx.VerifyContent("voice", true);
        ICHECK(deep.size() == 2);
    }
    ICHECK(voice::ReadState(layout).damaged.size() == 1);
    Outcome const checked = voice::Check(layout, true);
    ICHECK(!checked.ok && checked.errors.size() == 2);
    Write(layout.Full(voice::kDll), "tampered");
    {
        Transaction tx(layout);
        ICHECK(tx.Load() && (tx.Verify() == std::vector<std::string>{voice::kDll}));  // the mod's own file: Repair's
    }
    // Remove: the downloads go (the player's own model back), MEVR-Voice.dll and ASI\voice stay, the folders we made go.
    {
        Transaction tx(layout);
        ICHECK(tx.Load());
        tx.Data().packs.push_back({voice::kContent, voice::kName, "download", "2026-11-05T12:00:00"});
        ICHECK(tx.Save());
    }
    ICHECK(voice::ReadState(layout).recorded);
    Outcome const removed = voice::Remove(layout);
    ICHECK(removed.ok);
    ICHECK(!fs::exists(layout.Full(tiny)) && !fs::exists(layout.Full(std::string(voice::kModelsDir))));
    ICHECK(Read(layout.Full(gguf)) == "a model the player put there");
    ICHECK(fs::exists(layout.Full(voice::kDll)) && fs::is_directory(layout.Full(voice::kDir)));
    st = voice::ReadState(layout);
    ICHECK(st.parts.empty() && !st.recorded && st.dll);
    {
        Transaction tx(layout);
        ICHECK(tx.Load() && tx.Data().Find(voice::kDll) != nullptr && tx.Data().Find(tiny) == nullptr && tx.Data().Find(gguf) == nullptr);
        // Placed again, then the whole uninstall takes the downloads, the DLL and the folders.
        Write(downloads / L"encoder.ort", "encoder bytes");
        ICHECK(tx.PlaceIn(downloads / L"encoder.ort", tiny, Sha256("encoder bytes"), voice::ContentTag("models-tiny")));
        ICHECK(tx.Uninstall(false));
    }
    ICHECK(!fs::exists(layout.Full(tiny)) && !fs::exists(layout.Full(voice::kDll)) && !fs::exists(layout.Full(std::string(voice::kModelsDir))));
    ICHECK(Read(layout.Full(gguf)) == "a model the player put there");  // never ours: it and its folders stay

    // Install's refusals before any download: no MEVR-Voice.dll and no payload to take it from.
    Outcome const noDll = voice::Install(layout, std::nullopt, bare, voice::ModelSource{}, {}, nullptr);
    ICHECK(!noDll.ok && !noDll.errors.empty() && noDll.errors[0].find("MEVR-Voice.dll") != std::string::npos);
    Outcome const noGame = voice::Install(Layout{base / L"nowhere"}, std::nullopt, bare, voice::ModelSource{}, {}, nullptr);
    ICHECK(!noGame.ok);
    ICHECK(!voice::Remove(Layout{base / L"nowhere"}).ok);

    // The release's MEVR-Voice.dll goes in with the mod, comes back with Repair, goes with Uninstall.
    fs::path const payloadDir = base / L"payload";
    Write(payloadDir / L"LE1MEVR.asi", "new ASI");
    Write(payloadDir / L"bink2w64.dll", "our loader");
    Write(payloadDir / L"fonts" / L"A.ttf", "font");
    Write(payloadDir / L"menu" / L"main.rml", "<rml/>");
    Write(payloadDir / L"voice" / L"MEVR-Voice.dll", "release recognizer interface");
    Payload const payload{payloadDir};
    ICHECK(payload.Valid() && payload.HasVoice());
    fs::remove_all(root, ec);
    Write(layout.Win64() / L"MassEffect1.exe", "exe");
    Write(layout.Win64() / L"bink2w64.dll", "a proxy");
    Write(layout.Win64() / L"bink2w64_original.dll", "RAD original");
    InstallOptions options;
    options.ignoreVersion = true;
    options.gameSettings = false;
    Outcome const in = Install(layout, payload, options);
    ICHECK(in.ok);
    ICHECK(Read(layout.Full(voice::kDll)) == "release recognizer interface");
    fs::remove(layout.Full(voice::kDll), ec);
    ICHECK((ReadStatus(root, "Folder").needsRepair == std::vector<std::string>{voice::kDll}));
    ICHECK(Ok(Repair(layout, payload)) && Read(layout.Full(voice::kDll)) == "release recognizer interface");
    ICHECK(Ok(Uninstall(layout, false, {})) && !fs::exists(layout.Full(voice::kDir)));
    fs::remove_all(base, ec);
}

// A file another process holds for a moment, as antivirus does right after a write (the installer's record failed 3 installs
// in about 80 that way, error 5, 2026-11-06): the write over it waits and goes through once it's let go. A read-only one fails
// at once, with no wait.
void TestBusyFile() {
    fs::path const dir = mevr::tests::TempRoot() / L"mevr-busy-file";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::path const file = dir / L"MEVR-install.json";
    Write(file, "old");
    // Opened without delete sharing, as the scanner holds it: Windows refuses a replace at once.
    HANDLE const held = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    ICHECK(held != INVALID_HANDLE_VALUE);
    fs::path const other = dir / L"other.json";
    Write(other, "other");
    BOOL const moved = MoveFileExW(other.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING);
    DWORD const refused = GetLastError();
    ICHECK(!moved && (refused == ERROR_ACCESS_DENIED || refused == ERROR_SHARING_VIOLATION));
    // Let go after 100 ms: the write waits for it.
    auto const t0 = std::chrono::steady_clock::now();
    std::thread release([held] {
        Sleep(100);
        CloseHandle(held);
    });
    bool const wrote = mevr::install::WriteFileAtomic(file, "new");
    double const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    release.join();
    ICHECK(wrote && Read(file) == "new" && ms >= 90.0 && ms < 1300.0);
    std::printf("install: a write over a file held 100 ms went through after %.0f ms (refused at once with error %lu)\n", ms,
        refused);
    // Read-only: refused at once (no retries), with the error for the message.
    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY);
    auto const t1 = std::chrono::steady_clock::now();
    bool const readOnlyWrote = mevr::install::WriteFileAtomic(file, "newer");
    DWORD const readOnlyError = GetLastError();
    double const readOnlyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    ICHECK(!readOnlyWrote && readOnlyError == ERROR_ACCESS_DENIED && readOnlyMs < 100.0 && Read(file) == "new");
    ICHECK(!fs::exists(fs::path(file) += L".mevr-writing"));
    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
    fs::remove_all(dir, ec);
}

} // namespace

void RunInstallTests(int& failures) {
    gInstallFailures = &failures;
    TestJson();
    TestSteamVrSettings();
    TestIni();
    TestHalcyonIni();
    TestUpstreamHalcyonIni();
    TestGameSettings();
    TestDetect();
    TestLaunch();
    TestFirstStartAndRuntime();
    TestPacksAndUpdates();
    TestNetworkPaths();
    TestTransaction();
    TestQaFixes();
    TestLoaderCore();
    TestToc();
    TestFirstRunInstall();
    TestVoice();
    TestBusyFile();
}
