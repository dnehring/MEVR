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
#include "install/Updates.hpp"

#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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
    ICHECK(edits.size() == 5);
    ICHECK(ini::Revert(fixed, edits) == player);
    // The resolution in every slot that has the keys.
    std::string const res = ini::SetHalcyonResolution(release, 2824, 2880);
    ICHECK(ini::Get(res, "profile:Custom 3", "sfr2ResY") == std::optional<std::string>("2880"));
    ICHECK(ini::Get(res, "profile:Default", "expectedResX") == std::optional<std::string>("2824"));
    ICHECK(ini::Get(res, "profile:Default", "monoResX") == std::optional<std::string>("3840"));
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
    ICHECK(st::TargetFor("headset", 2824, 2880).width == 2824);
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
    std::string const e2 = st::ApplyEngineSettings(engine, &engineEdits);
    ICHECK(ini::Get(e2, "Engine.GameEngine", "SmoothFrameRate") == std::optional<std::string>("FALSE"));
    ICHECK(ini::Get(e2, "Engine.Engine", "bSmoothFrameRate") == std::optional<std::string>("TRUE"));
    ICHECK(ini::Revert(e2, engineEdits) == engine);
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
    d::DllIdentity ours{true, "MEVR", "MEVR ASI loader", "bink2w64.dll proxy"};
    d::DllIdentity other{true, "Someone", "Some proxy", ""};
    ICHECK(d::ClassifyBink(rad, false) == d::BinkState::Original);
    ICHECK(d::ClassifyBink(rad, true) == d::BinkState::Original);
    ICHECK(d::ClassifyBink(lebp, true) == d::BinkState::LEBinkProxy);
    ICHECK(d::ClassifyBink(lebp, false) == d::BinkState::Broken);
    ICHECK(d::ClassifyBink(ours, true) == d::BinkState::Ours);
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

// A fake game folder under %TEMP% (never the real game): the manifest's add / replace / rename / edit and their
// undo, then the Installer's whole Install, RestoreSettings and Uninstall on it.
void TestTransaction() {
    using namespace mevr::install;
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
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
        ICHECK(tx.EditIni(paths::kEngineIni, [](std::string_view t, std::vector<ini::Edit>* e) { return settings::ApplyEngineSettings(t, e); }));
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
    // Installer refuses to run while the game does: then this part is skipped.
    if (detect::GameRunning()) {
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
        ICHECK(RecordPack(layout, "alot-le1", "ALOT for ME1").ok);
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

} // namespace

void RunInstallTests(int& failures) {
    gInstallFailures = &failures;
    TestJson();
    TestIni();
    TestHalcyonIni();
    TestGameSettings();
    TestDetect();
    TestLaunch();
    TestPacksAndUpdates();
    TestTransaction();
}
