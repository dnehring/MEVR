// SPDX-License-Identifier: MIT
#include "SetupApp.hpp"

#include "XrProbe.hpp"
#include "core/Json.hpp"
#include "install/GameSettings.hpp"
#include "install/Http.hpp"
#include "install/Launch.hpp"
#include "install/Packs.hpp"
#include "install/ReportMarks.hpp"
#include "install/Updates.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <format>
#include <memory>
#include <system_error>
#include <thread>

namespace mevr::setup {
namespace {

namespace fs = std::filesystem;
namespace detect = install::detect;
namespace packs = install::packs;

std::string OnOff(bool on) {
    return on ? "On" : "Off";
}

// The in-game menu's key: menuKey = 220 (VK_OEM_5) in every MELEVR.ini slot (install/Ini FixHalcyonIni).
constexpr UINT kMenuKeyVk = 220;

// A key's name on this keyboard layout (GetKeyNameText, as the in-game menu's KeyName): VK_OEM_5 is \ on a US
// keyboard and ^ on a German one. Empty if the layout has no such key.
std::string KeyName(UINT vk) {
    UINT const scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scan == 0) {
        return {};
    }
    wchar_t name[64]{};
    int const n = GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64);
    if (n <= 0) {
        return {};
    }
    return detect::Utf8(std::wstring(name, static_cast<std::size_t>(n)));
}

// "the \ key", "the ^ key", or a description when the layout has no name for it.
std::string MenuKeyText() {
    std::string const name = KeyName(kMenuKeyVk);
    return name.empty() ? std::string("the menu key (\\ on a US keyboard)") : "the " + name + " key";
}

// The pages' row for a game never started here (no GamerSettings.ini yet).
std::string FirstStartText(bool installed, bool pending) {
    if (installed && pending) {
        return "The VR game settings you chose at install are waiting for its settings file. Start the game once: the setup waits for the "
               "file, you quit at the game's main menu, and they go on by themselves.";
    }
    return "Without its settings file the VR game settings can't go on, and VR starts in a bordered window with a broken picture. Start the "
           "game once, before or after Install: the setup waits for the file, you quit at the game's main menu, and the settings go on by "
           "themselves.";
}

// The Repair page's name for a file it would put back.
std::string RepairItemName(std::string const& relative) {
    std::string const file = detect::Utf8(fs::path(detect::Wide(relative)).filename().wstring());
    if (relative == install::paths::kBink) {
        return file + " (the ASI loader)";
    }
    if (relative == install::paths::kToc) {
        return file + " (the cockpit's entry)";
    }
    return file;
}

} // namespace

App::App(Options options, Host host) : options_(std::move(options)), host_(std::move(host)) {
    version_ = install::updates::kVersion;
}

App::~App() {
    cancel_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool App::Bind(Rml::Context* context) {
    Rml::DataModelConstructor c = context->CreateDataModel("setup");
    if (!c) {
        return false;
    }
    if (auto pack = c.RegisterStruct<PackView>()) {
        pack.RegisterMember("id", &PackView::id);
        pack.RegisterMember("name", &PackView::name);
        pack.RegisterMember("author", &PackView::author);
        pack.RegisterMember("description", &PackView::description);
        pack.RegisterMember("size", &PackView::size);
        pack.RegisterMember("licence", &PackView::licence);
        pack.RegisterMember("requirements", &PackView::requirements);
        pack.RegisterMember("state", &PackView::state);
        pack.RegisterMember("file", &PackView::file);
        pack.RegisterMember("installed", &PackView::installed);
        pack.RegisterMember("large", &PackView::large);
    }
    c.RegisterArray<std::vector<PackView>>();
    if (auto line = c.RegisterStruct<LogLine>()) {
        line.RegisterMember("text", &LogLine::text);
        line.RegisterMember("error", &LogLine::error);
    }
    c.RegisterArray<std::vector<LogLine>>();
    if (auto file = c.RegisterStruct<ReportFileView>()) {
        file.RegisterMember("name", &ReportFileView::name);
        file.RegisterMember("size", &ReportFileView::size);
        file.RegisterMember("note", &ReportFileView::note);
    }
    c.RegisterArray<std::vector<ReportFileView>>();
    c.Bind("packs", &packs_);
    c.Bind("log_lines", &log_);
    c.Bind("report_files", &reportFiles_);
    struct Flag {
        char const* name;
        bool* value;
    };
    for (Flag const& f : {Flag{"game_ok", &gameOk_}, Flag{"version_ok", &versionOk_}, Flag{"xr_ok", &xrOk_}, Flag{"loader_ok", &loaderOk_},
             Flag{"installed", &installed_}, Flag{"ready", &ready_}, Flag{"can_install", &canInstall_}, Flag{"has_payload", &hasPayload_},
             Flag{"admin_needed", &adminNeeded_}, Flag{"busy", &busy_}, Flag{"waiting", &waiting_}, Flag{"has_gamer_settings", &hasGamerSettings_},
             Flag{"settings_applied", &settingsApplied_}, Flag{"textures_marked", &texturesMarked_}, Flag{"mem_ready", &memReady_},
             Flag{"headset_known", &headsetKnown_}, Flag{"update_enabled", &updateEnabled_}, Flag{"opt_settings", &optSettings_},
             Flag{"opt_vsync", &optVsync_}, Flag{"opt_blur", &optBlur_}, Flag{"opt_grain", &optGrain_}, Flag{"opt_hdr", &optHdr_},
             Flag{"keep_settings", &keepSettings_}, Flag{"confirm_open", &confirmOpen_}, Flag{"confirm_danger", &confirmDanger_},
             Flag{"upstream_found", &upstreamFound_}, Flag{"gamer_read_only", &gamerReadOnly_}, Flag{"remove_local", &removeLocal_},
             Flag{"report_snapshot", &reportSnapshot_}, Flag{"report_perf", &reportPerf_}, Flag{"report_ready", &reportReady_},
             Flag{"crash_offer", &crashOffer_}, Flag{"report_has_zip", &reportHasZip_}, Flag{"settings_pending", &settingsPending_},
             Flag{"repair_needed", &repairNeeded_}, Flag{"can_cancel", &canCancel_}, Flag{"hero_enabled", &heroEnabled_}}) {
        c.Bind(f.name, f.value);
    }
    struct Text {
        char const* name;
        std::string* value;
    };
    for (Text const& t : {Text{"page", &page_}, Text{"version", &version_}, Text{"chip_game", &chipGame_}, Text{"chip_xr", &chipXr_},
             Text{"chip_installed", &chipInstalled_}, Text{"install_title", &installTitle_}, Text{"install_button", &installButton_}, Text{"hero_title", &heroTitle_}, Text{"hero_text", &heroText_},
             Text{"game_title", &gameTitle_}, Text{"game_text", &gameText_}, Text{"version_title", &versionTitle_},
             Text{"version_text", &versionText_}, Text{"xr_title", &xrTitle_}, Text{"xr_text", &xrText_}, Text{"loader_title", &loaderTitle_},
             Text{"loader_text", &loaderText_}, Text{"loader_plan", &loaderPlan_}, Text{"root_text", &rootText_},
             Text{"source_text", &sourceText_}, Text{"update_text", &updateText_}, Text{"update_url", &updateUrl_}, Text{"quality", &quality_},
             Text{"quality_text", &qualityText_}, Text{"headset_text", &headsetText_}, Text{"cur_res", &curRes_}, Text{"cur_window", &curWindow_},
             Text{"cur_hdr", &curHdr_}, Text{"cur_vsync", &curVsync_}, Text{"cur_blur", &curBlur_}, Text{"cur_grain", &curGrain_},
             Text{"repair_title", &repairTitle_}, Text{"repair_text", &repairText_}, Text{"uninstall_text", &uninstallText_},
             Text{"launch_text", &launchText_}, Text{"footer_text", &footerText_}, Text{"wait_text", &waitText_},
             Text{"request_text", &requestText_}, Text{"mem_text", &memText_}, Text{"progress_text", &progressText_},
             Text{"confirm_title", &confirmTitle_}, Text{"confirm_text", &confirmText_}, Text{"confirm_button", &confirmButton_},
             Text{"upstream_text", &upstreamText_}, Text{"local_text", &localText_}, Text{"report_text", &reportText_},
             Text{"report_redactions", &reportRedactions_}, Text{"report_headline", &reportHeadline_}, Text{"report_zip", &reportZip_},
             Text{"report_request", &reportRequest_}, Text{"hero_action", &heroAction_}, Text{"hero_button", &heroButton_},
             Text{"launch_advice", &launchAdvice_}, Text{"menu_key", &menuKey_}, Text{"first_start_text", &firstStartText_}}) {
        c.Bind(t.name, t.value);
    }
    c.Bind("progress", &progress_);
    c.BindEventCallback("act", [this](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& args) {
        if (args.empty()) {
            return;
        }
        Act(args[0].Get<Rml::String>(), args.size() > 1 ? args[1].Get<Rml::String>() : std::string());
    });
    handle_ = c.GetModelHandle();
    return true;
}

void App::Dirty() {
    if (handle_) {
        handle_.DirtyAllVariables();
    }
}

void App::SetPage(std::string const& page) {
    page_ = page;
    Dirty();
}

std::string App::StateText() const {
    std::string packs;
    for (PackView const& p : packs_) {
        packs += std::format("pack {}: {}{}\n", p.id, p.state, p.installed ? " (installed)" : "");
    }
    return std::format("page: {}\ngame: {}\ngame ok: {}\ninstalled: {}\nrequest text: {}\nwaiting: {}\nwait text: {}\nmem ready: {}\n{}", page_,
        gameOk_ ? detect::Utf8(status_.root.wstring()) : std::string("none"), gameOk_ ? 1 : 0, installed_ ? 1 : 0, requestText_, waiting_ ? 1 : 0,
        waitText_, memReady_ ? 1 : 0, packs);
}

bool App::SteamGame() const {
    // Found through Steam, or a picked folder inside a Steam library.
    std::wstring path = status_.root.wstring();
    std::transform(path.begin(), path.end(), path.begin(), ::towlower);
    return status_.source == "Steam" || path.find(L"\\steamapps\\common\\") != std::wstring::npos;
}

void App::SetGame(fs::path const& root, std::string const& source) {
    status_ = install::ReadStatus(root, source);
}

void App::Start() {
    payload_ = install::FindPayload(options_.exe.parent_path());
    // The game: --game; this copy's own game (it lives in Game\ME1\Binaries\Win64\ASI); the installs we can find.
    if (options_.gameRoot) {
        SetGame(detect::RootFromPicked(*options_.gameRoot).value_or(*options_.gameRoot), "Folder");
    } else {
        fs::path const asiDir = options_.exe.parent_path();
        fs::path const own = asiDir.parent_path().parent_path().parent_path().parent_path().parent_path();
        if (_wcsicmp(asiDir.filename().c_str(), L"ASI") == 0 && detect::IsGameRoot(own)) {
            std::error_code ec;
            bool const steam = fs::exists(own / L"Game" / L"ME1" / L"Binaries" / L"Win64" / L"steam_appid.txt", ec);
            SetGame(own, steam ? "Steam" : "this copy's game folder");
        } else {
            auto const installs = detect::FindInstalls();
            if (!installs.empty()) {
                SetGame(installs.front().root, installs.front().source);
            }
        }
    }
    xr_ = detect::ActiveRuntime();
    if (auto const log = install::ReadFile(detect::HalcyonLogPath())) {
        if (auto const size = detect::EyeSizeFromHalcyonLog(*log)) {
            headsetWidth_ = size->width;
            headsetHeight_ = size->height;
            quality_ = "headset";
        }
    }
    updateEnabled_ = !install::updates::LatestReleaseApi(options_.repo).empty();
    updateText_ = updateEnabled_ ? "Check asks GitHub for the newest release and links its page; nothing downloads by itself."
                             : "No release page set yet (MEVR-Setup.ini, [Update] repo).";
    if (options_.request) {
        // "Install after I quit" (plugin/SetupRequest): the game wrote the request and started us as it exited.
        page_ = "content";
        requestText_ = "Choose a pack below.";
        if (auto const text = install::ReadFile(install::RequestFile())) {
            if (auto const request = install::ParseSetupRequest(*text)) {
                std::string names;
                for (std::string const& id : request->packs) {
                    if (packs::Pack const* p = packs::Find(id)) {
                        names += names.empty() ? p->name : std::string(", ") + p->name;
                    }
                }
                if (!names.empty()) {
                    requestText_ = "You asked for: " + names + ".";
                }
            }
        }
        std::error_code ec;
        fs::remove(install::RequestFile(), ec);
    }
    if (options_.waitPid != 0) {
        waiting_ = true;
        waitText_ = "Changes start once the game has exited.";
    }
    // Report a problem: a request the game left (its setup copy wasn't there to answer it), or a crash since the last
    // report and the last "no".
    {
        namespace marks = install::report_marks;
        fs::path const local = install::LocalDataDir();
        std::error_code ec;
        if (!local.empty() && fs::exists(marks::RequestFile(local), ec)) {
            if (auto const text = install::ReadFile(marks::RequestFile(local))) {
                if (auto const request = json::Parse(*text)) {
                    reportSnapshot_ = request->Bool("snapshot");
                    reportPerf_ = request->Bool("perf");
                }
            }
            fs::remove(marks::RequestFile(local), ec);
            reportRequest_ = "You asked for a report from the game's menu. Look over what goes in, then create it.";
            page_ = "report";
        }
        crashOffer_ = marks::CrashOffer(local);
    }
    Refresh();
}

install::report::Options App::ReportOptions() const {
    install::report::Options o;
    o.gameRoot = gameOk_ ? status_.root : fs::path();
    o.store = SteamGame() ? "Steam" : status_.source == "EA app" ? "EA app" : std::string();
    o.snapshot = reportSnapshot_;
    o.perf = reportPerf_;
    return o;
}

void App::GatherReport() {
    if (busy_ || sample_) {
        return;
    }
    auto bundle = std::make_shared<install::report::Bundle>();
    install::report::Options const options = ReportOptions();
    RunJob("Gathering what the report holds", [bundle, options] { *bundle = install::report::Collect(options); });
    jobDone_ = [this, bundle] {
        bundle_ = bundle;
        reportReady_ = true;
        ShowBundle();
    };
}

void App::ShowBundle() {
    reportFiles_.clear();
    if (!bundle_) {
        return;
    }
    install::report::Bundle const& b = *bundle_;
    reportFiles_.push_back({"REPORT.md", "~" + install::report::SizeText(install::report::BuildReportMd(b).size()),
        "The summary: system, what went wrong, settings changed, a prompt for your AI assistant"});
    std::uint64_t total = 0;
    for (install::report::File const& f : b.files) {
        total += f.bytes.size();
        std::string note = f.note;
        if (f.redactions > 0) {
            note += std::format("{}{} redacted", note.empty() ? "" : "; ", f.redactions);
        }
        reportFiles_.push_back({f.name, install::report::SizeText(f.bytes.size()), note});
    }
    install::report::Redactions const& r = b.redactions;
    reportRedactions_ = std::format("{} redactions: your Windows user name {} times, the computer's name {}, your profile folder {}, e-mail "
                                    "addresses {}. They read <user>, <computer>, <profile>, <email> in the zip.",
        r.Total(), r.userName, r.computerName, r.profilePath, r.email);
    reportHeadline_ = install::report::Headline(b);
    reportText_ = std::format("{} files, {} before compression.{}", b.files.size() + 1, install::report::SizeText(total),
        b.missing.empty() ? std::string() : " Not on this PC: " + [&] {
            std::string m;
            for (std::string const& x : b.missing) {
                m += (m.empty() ? "" : ", ") + x;
            }
            return m;
        }() + ".");
    Dirty();
}

void App::Refresh() {
    if (!status_.root.empty()) {
        status_ = install::ReadStatus(status_.root, status_.source);
    }
    xrOk_ = !xr_.kind.empty();
    hasPayload_ = payload_.has_value();
    gameOk_ = status_.gameFound;
    versionOk_ = status_.versionCheck == detect::VersionCheck::Supported;
    installed_ = status_.installed;
    adminNeeded_ = gameOk_ && !status_.canWrite;
    hasGamerSettings_ = status_.hasGamerSettings;
    gamerReadOnly_ = status_.gamerSettingsReadOnly;
    upstreamFound_ = gameOk_ && status_.upstream.Found();
    upstreamText_ = upstreamFound_
        ? std::string("Its dxgi.dll") + (status_.upstream.openxrLoader ? " and openxr_loader.dll are" : " is") +
              " in Binaries\\Win64. This mod has Halcyon's renderer built in, and with both two would run, so Install renames " +
              (status_.upstream.openxrLoader ? "them" : "it") + " to *.mevr-off. Uninstall puts them back. Its MELEVR.ini stays. Installing means you agree."
        : std::string();
    settingsApplied_ = status_.gameSettings;
    texturesMarked_ = status_.texturesMarked;
    bool const binkUsable = status_.bink != detect::BinkState::Missing && status_.bink != detect::BinkState::Broken;
    loaderOk_ = gameOk_ && binkUsable;
    canInstall_ = gameOk_ && versionOk_ && binkUsable && hasPayload_ && !adminNeeded_;
    ready_ = gameOk_ && versionOk_ && binkUsable;
    headsetKnown_ = headsetWidth_ > 0;

    chipGame_ = gameOk_ ? "Mass Effect 1 LE" : "Game not found";
    chipXr_ = xrOk_ ? xr_.kind : "No VR runtime";
    chipInstalled_ = installed_ ? "Installed " + status_.installedVersion : "Not installed";
    // Install's label: it puts this setup's version in place, so it's an update only when that is newer.
    std::string const mine = install::updates::kVersion;
    std::string const theirs = status_.installedVersion;
    int const order = installed_ && !theirs.empty() ? install::updates::CompareVersions(theirs, mine) : 0;
    if (!installed_) {
        installTitle_ = "Ready";
        installButton_ = "Install";
    } else if (theirs.empty()) {
        installTitle_ = std::format("Installed: install again to put this setup's version ({}) in place", mine);
        installButton_ = "Reinstall";
    } else if (order < 0) {
        installTitle_ = std::format("Installed {}: this setup updates it to {}", theirs, mine);
        installButton_ = "Update";
    } else if (order == 0) {
        installTitle_ = std::format("Installed {}, the same version as this setup: install again to replace its files", theirs);
        installButton_ = "Reinstall";
    } else {
        installTitle_ = std::format("Installed {} is newer than this setup ({}): installing goes back to {}", theirs, mine, mine);
        installButton_ = std::format("Install {}", mine);
    }
    rootText_ = gameOk_ ? detect::Utf8(status_.root.wstring()) : "No game folder";
    sourceText_ = status_.source.empty() ? "nothing yet" : status_.source;
    settingsPending_ = status_.pendingSettings.has_value();
    menuKey_ = MenuKeyText();
    launchAdvice_ = install::launch::runtime_start::PlanFor(xr_.kind, xr_.manifest).advice;
    firstStartText_ = FirstStartText(installed_, settingsPending_);
    gameTitle_ = gameOk_ ? "Found" : "Not found";
    gameText_ = gameOk_ ? status_.source + ": " + detect::Utf8(status_.root.filename().wstring()) : "Pick the folder on the Welcome page";
    versionTitle_ = status_.version ? status_.version->ToString() : "Unknown";
    versionText_ = versionOk_ ? "The version the mod is built for"
        : gameOk_ ? "The mod needs 2.0.0.48602 and stays off on any other. Update the game in Steam or the EA app."
                  : "";
    xrTitle_ = xrOk_ ? xr_.kind : "None";
    xrText_ = xrOk_ ? (xr_.name.empty() ? std::string("The active OpenXR runtime") : xr_.name)
                    : "No OpenXR runtime is active: install SteamVR, the Meta app or Virtual Desktop and make it the active runtime";
    switch (status_.bink) {
    case detect::BinkState::Original: loaderTitle_ = "None yet"; break;
    case detect::BinkState::Ours: loaderTitle_ = "MEVR loader"; break;
    case detect::BinkState::LEBinkProxy: loaderTitle_ = "ME3Tweaks'"; break;
    case detect::BinkState::OtherProxy: loaderTitle_ = "Another proxy"; break;
    case detect::BinkState::Missing:
    case detect::BinkState::Broken: loaderTitle_ = gameOk_ ? "Broken" : "-"; break;
    }
    loaderText_ = gameOk_ ? detect::BinkStateText(status_.bink) : "";
    loaderPlan_ = status_.bink == detect::BinkState::LEBinkProxy ? "ME3Tweaks' ASI loader is already installed: it loads the mod; nothing to add"
        : status_.bink == detect::BinkState::OtherProxy           ? "Another bink2w64.dll proxy is installed: it's used as it is (it must load ASI\\*.asi)"
                                                                  : "Ours: the game's bink2w64.dll is kept as bink2w64_original.dll and ours goes in front of it";
    install::settings::Target const target = install::settings::TargetFor(quality_, headsetWidth_, headsetHeight_);
    qualityText_ = quality_ == "keep" ? "The resolution stays as it is"
        : target.width > 0           ? std::format("{} × {} per eye{}", target.width, target.height,
                                           quality_ == "headset" ? " (your headset's own size)" : quality_ == "performance" ? " (Halcyon's default)" : "")
                                     : "";
    headsetText_ = headsetKnown_ ? std::format("{} × {} per eye, from the last VR session", headsetWidth_, headsetHeight_)
                                 : "Not known yet: it's read from the last VR session, or Detect asks the VR runtime";
    // The Settings page's current values.
    if (auto const gamer = install::ReadFile(install::Layout{status_.root}.Full(install::paths::kGamerSettings)); gameOk_ && gamer) {
        install::settings::Current const now = install::settings::Read(*gamer);
        curRes_ = now.resX > 0 ? std::format("{} × {}", now.resX, now.resY) : "Default";
        curWindow_ = now.borderless ? "Borderless" : "Windowed or full screen";
        curHdr_ = OnOff(now.hdr);
        curVsync_ = OnOff(now.vsync);
        curBlur_ = OnOff(now.motionBlur);
        curGrain_ = OnOff(now.filmGrain);
    } else {
        curRes_ = curWindow_ = curHdr_ = curVsync_ = curBlur_ = curGrain_ = "-";
    }
    // Repair, Uninstall, Launch.
    UpdateRepairStatus();
    UpdateHero();
    {
        fs::path const local = install::LocalDataDir();
        localText_ = local.empty() ? "Not found."
                                   : detect::Utf8(local.wstring()) + ": snapshots, recordings, frame-timing captures, crash reports, the downloaded "
                                     "Mass Effect Modder and texture-pack work files. Off: they stay.";
    }
    if (status_.hasRecord) {
        install::Transaction tx(install::Layout{status_.root});
        tx.Load();
        int added = 0, replaced = 0, edited = 0;
        for (install::FileEntry const& f : tx.Data().files) {
            added += f.action == "added" ? 1 : 0;
            replaced += f.action == "replaced" || f.action == "renamed" ? 1 : 0;
            edited += f.action == "edited" ? 1 : 0;
        }
        uninstallText_ = std::format("{} files removed, {} put back as they were, {} settings files returned to your values.", added, replaced, edited);
    } else {
        uninstallText_ = "Nothing is installed.";
    }
    if (launchText_.empty()) {
        launchText_ = "The Legendary Edition's launcher only starts MassEffect1.exe with your language and subtitle settings; Launch does "
                      "the same directly, with Steam running.";
    }
    footerText_ = gameOk_ ? "Mod log: Binaries\\Win64\\Logs\\LE1MEVR.log · loader log: Logs\\MEVRLoader.log" : "";
    std::error_code ec;
    memReady_ = fs::exists(packs::MemExe(), ec);
    memText_ = memReady_ ? "Ready: it installs the texture packs below (GPL, from its own GitHub releases)."
                         : "Installs texture packs (GPL, by the ALOT team). Downloaded from its own GitHub releases when you press Download.";
    UpdatePacks();
    Dirty();
}

void App::UpdateRepairStatus(bool reread) {
    if (sample_) {
        // The preview's stand-ins: status_.needsRepair as Sample set it.
    } else if (!installed_ || status_.root.empty()) {
        repairTitle_ = "Not installed";
        repairText_ = "Install first.";
        repairNeeded_ = false;
        return;
    } else if (reread) {
        // The files as they are now (ReadStatus: what Repair would put back; a loader another tool installed since isn't).
        status_.needsRepair = install::ReadStatus(status_.root, status_.source).needsRepair;
    }
    if (!installed_) {
        repairTitle_ = "Not installed";
        repairText_ = "Install first.";
        repairNeeded_ = false;
        return;
    }
    std::vector<std::string> items;
    for (std::string const& f : status_.needsRepair) {
        items.push_back(RepairItemName(f));
    }
    // The VR game settings chosen at install, waiting for the game's first start: Repair puts them on once the file exists.
    bool const settingsDue = settingsPending_ && hasGamerSettings_;
    if (settingsDue) {
        items.push_back("the VR game settings chosen at install (the game has made its settings file since)");
    }
    repairNeeded_ = !items.empty();
    std::size_t const files = status_.needsRepair.size();
    repairTitle_ = files == 0 && !settingsDue ? "Every file is as installed"
        : files == 0                          ? "The VR game settings are waiting"
        : files == 1                          ? "1 file needs repair"
                                              : std::format("{} files need repair", files);
    repairText_.clear();
    for (std::string const& item : items) {
        repairText_ += (repairText_.empty() ? "" : ", ") + item;
    }
    if (items.empty()) {
        repairText_ = settingsPending_ ? "Nothing to do. The VR game settings still wait for the game's first start (Welcome page)." : "Nothing to do.";
    } else {
        repairText_ += ". Repair puts them back.";
    }
}

void App::UpdateHero() {
    bool const binkUsable = status_.bink != detect::BinkState::Missing && status_.bink != detect::BinkState::Broken;
    heroAction_.clear();
    heroButton_.clear();
    heroEnabled_ = false;
    if (!gameOk_) {
        heroTitle_ = "Game not found";
        heroText_ = "Mass Effect Legendary Edition wasn't found in Steam or the EA app. Pick its folder below (the one with Game\\ME1 in it).";
    } else if (!versionOk_) {
        heroTitle_ = "Unsupported game version";
        heroText_ = "The mod is built for MassEffect1.exe 2.0.0.48602 and stays switched off on any other version, so it isn't installed.";
    } else if (!binkUsable) {
        heroTitle_ = "The game needs a repair";
        heroText_ = detect::BinkStateText(status_.bink);
    } else if (installed_ && repairNeeded_ && !status_.needsRepair.empty()) {
        heroTitle_ = "The mod needs a repair";
        heroText_ = "Some of its files changed or went missing since the install (Steam's or the EA app's \"verify files\" puts the game's own "
                    "loader and file table back, which switches the mod off): " + repairText_;
        heroAction_ = "repair";
        heroButton_ = "Repair";
        heroEnabled_ = hasPayload_;
        if (!hasPayload_) {
            heroText_ += " Run MEVR-Setup.exe from the download to repair.";
        }
    } else if (!hasGamerSettings_) {
        heroTitle_ = installed_ ? "One step left: start the game once" : "Mass Effect 1 hasn't been started here yet";
        heroText_ = installed_ ? "Mass Effect 1 makes its settings file on its first start, and the VR game settings you chose at install go into "
                                 "it. Start it once: the setup waits for the file, you quit at the game's main menu, and they go on by themselves."
                               : "It makes its settings file on its first start, and the VR game settings go into it. Start it once now, or "
                                 "install first: the settings you choose are kept until the file exists.";
        heroAction_ = "first_start";
        heroButton_ = "Start the game once";
        heroEnabled_ = true;
    } else if (installed_ && settingsPending_) {
        heroTitle_ = "One step left: the VR game settings";
        heroText_ = "Mass Effect 1 has made its settings file since the install: put on the VR game settings you chose then (a borderless window at "
                    "the VR resolution; your values are kept for Restore).";
        heroAction_ = "apply_pending";
        heroButton_ = "Apply them";
        heroEnabled_ = true;
    } else if (installed_) {
        heroTitle_ = "Ready for VR";
        heroText_ = "The mod is installed. " + launchAdvice_ + " In game, " + menuKey_ + " or both sticks held opens the VR menu.";
        heroAction_ = "launch";
        heroButton_ = "Launch";
        heroEnabled_ = true;
    } else if (upstreamFound_) {
        heroTitle_ = "Ready to install";
        heroText_ = "Halcyon's MELE-VR is installed: Install moves its two DLLs aside (Uninstall puts them back), then puts the mod, its ASI loader and "
                    "the VR game settings in place, recorded so it can all be undone.";
        heroAction_ = "goto_install";
        heroButton_ = "Install";
        heroEnabled_ = canInstall_;
    } else {
        heroTitle_ = "Ready to install";
        heroText_ = "Everything checks out. Install puts the mod, its ASI loader and the VR game settings in place, recorded so it can all be undone.";
        heroAction_ = "goto_install";
        heroButton_ = "Install";
        heroEnabled_ = canInstall_;
    }
}

install::settings::Choices App::PendingOrModelChoices() const {
    return status_.pendingSettings ? install::ChoicesFromPending(*status_.pendingSettings) : ChoicesFromModel();
}

void App::StartFirstRun() {
    if (!gameOk_ || waiting_ || busy_) {
        return;
    }
    namespace fst = install::launch::first_start;
    install::Layout const layout{status_.root};
    bool const steam = SteamGame();
    // From the Settings page: what's chosen there; elsewhere what the install chose, if it waits.
    install::settings::Choices const choices = page_ == "settings" ? ChoicesFromModel() : PendingOrModelChoices();
    canCancel_ = true;
    RunJob(fst::PhaseText(fst::Phase::Starting), [this, layout, steam, choices] {
        auto const t0 = std::chrono::steady_clock::now();
        auto seconds = [](std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); };
        std::error_code ec;
        bool const already = detect::GameRunningIn(layout.root);
        if (already) {
            Post("Mass Effect 1 is already running: waiting for its settings file and for it to close.");
        } else {
            install::launch::Result const r = install::launch::StartLe1(layout.root, steam);
            Post(r.message, !r.started);
            if (!r.started) {
                Post(fst::PhaseText(fst::Phase::Closed), true);
                return;
            }
        }
        double lastSeen = -1.0;  // seconds since t0 when the game was last seen running
        fst::Phase phase = fst::Phase::Starting;
        fst::Phase shown = fst::Phase::Apply;  // anything but the first phase, so the first one is posted
        while (!cancel_) {
            fst::Observation o;
            o.sinceLaunchS = seconds(std::chrono::steady_clock::now() - t0);
            o.gameRunning = detect::GameRunningIn(layout.root);
            o.fileExists = fs::exists(layout.Full(install::paths::kGamerSettings), ec);
            if (o.gameRunning) {
                lastSeen = o.sinceLaunchS;
            }
            o.sinceSeenS = lastSeen < 0.0 ? -1.0 : o.sinceLaunchS - lastSeen;
            phase = fst::Next(o);
            if (phase != shown) {
                shown = phase;
                Progress(fst::PhaseText(phase), phase == fst::Phase::WaitingForQuit ? 66 : phase == fst::Phase::WaitingForFile ? 33 : -1);
                if (phase == fst::Phase::WaitingForQuit) {
                    Post(fst::PhaseText(phase));
                }
            }
            if (fst::Finished(phase)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if (cancel_) {
            Post("Stopped waiting. Once Mass Effect 1 has made its settings file and closed, Repair (or Settings > Apply) puts the VR game settings on.");
            return;
        }
        if (phase != fst::Phase::Apply) {
            Post(fst::PhaseText(phase), true);
            return;
        }
        Post(fst::PhaseText(phase));
        Apply(install::ApplySettings(layout, choices));
    });
}

void App::UpdatePacks() {
    packs_.clear();
    for (packs::Pack const& p : packs::Catalogue()) {
        PackView v;
        v.id = p.id;
        v.name = p.name;
        v.author = p.author;
        v.description = p.description;
        v.size = p.size;
        v.licence = p.licence;
        v.requirements = p.requirements;
        v.large = p.large;
        v.installed = std::any_of(status_.packs.begin(), status_.packs.end(), [&](install::PackRecord const& r) { return r.id == p.id; });
        for (auto const& [id, file] : packFiles_) {
            if (id == p.id) {
                v.file = detect::Utf8(file.filename().wstring());
            }
        }
        if (v.file.empty()) {
            auto const found = packs::FindDownloads(p);
            if (!found.empty()) {
                packFiles_.emplace_back(p.id, found.front());
                v.file = detect::Utf8(found.front().filename().wstring());
            }
        }
        if (v.installed) {
            v.state = "Installed with this setup.";
        } else if (!v.file.empty()) {
            v.state = "Ready to install: " + v.file;
        } else {
            v.state = "Not downloaded: open its page, download the MEM version, then choose the file.";
        }
        packs_.push_back(std::move(v));
    }
}

void App::Post(std::string text, bool error) {
    std::lock_guard const lock(jobMutex_);
    jobLog_.push_back({std::move(text), error});
    jobChanged_ = true;
}

void App::Progress(std::string text, int percent) {
    std::lock_guard const lock(jobMutex_);
    jobProgress_ = std::move(text);
    if (percent >= 0) {
        jobPercent_ = percent;
    }
    jobChanged_ = true;
}

void App::Apply(install::Outcome const& outcome) {
    for (std::string const& line : outcome.log) {
        Post(line, line.rfind("error: ", 0) == 0);
    }
    Post(outcome.ok ? "Done." : "Stopped: see the errors above.", !outcome.ok);
}

void App::RunJob(std::string title, std::function<void()> job) {
    if (busy_) {
        return;
    }
    busy_ = true;
    cancel_ = false;
    log_.clear();
    {
        std::lock_guard const lock(jobMutex_);
        jobLog_.clear();
        jobProgress_ = title;
        jobPercent_ = 0;
        jobChanged_ = true;
    }
    working_ = true;
    worker_ = std::thread([this, job = std::move(job)] {
        job();
        working_ = false;
    });
    Dirty();
}

void App::Tick() {
    if (page_ != shownPage_) {
        shownPage_ = page_;
        if (page_ == "repair" && !busy_) {
            UpdateRepairStatus(true);  // the files as they are now, not when the setup started
            UpdateHero();
            Dirty();
        }
        if (page_ == "report" && !busy_) {
            GatherReport();  // the preview: what the zip would hold now
        }
    }
    if (waiting_ && options_.waitPid != 0) {
        HANDLE const process = OpenProcess(SYNCHRONIZE, FALSE, options_.waitPid);
        bool const gone = process == nullptr || WaitForSingleObject(process, 0) != WAIT_TIMEOUT;
        if (process != nullptr) {
            CloseHandle(process);
        }
        if (gone) {
            waiting_ = false;
            options_.waitPid = 0;
            Refresh();
        }
    }
    bool changed = false;
    {
        std::lock_guard const lock(jobMutex_);
        if (jobChanged_) {
            jobChanged_ = false;
            log_ = jobLog_;
            progressText_ = jobProgress_;
            progress_ = jobPercent_;
            changed = true;
        }
    }
    if (busy_ && !working_ && worker_.joinable()) {
        worker_.join();
        busy_ = false;
        canCancel_ = false;
        {
            std::lock_guard const lock(jobMutex_);
            log_ = jobLog_;
        }
        if (jobDone_) {
            auto done = std::move(jobDone_);
            jobDone_ = {};
            done();
        }
        Refresh();
        changed = true;
    }
    if (changed) {
        Dirty();
    }
}

install::settings::Choices App::ChoicesFromModel() const {
    install::settings::Choices c;
    c.target = install::settings::TargetFor(quality_, headsetWidth_, headsetHeight_);
    c.vsyncOff = optVsync_;
    c.motionBlurOff = optBlur_;
    c.filmGrainOff = optGrain_;
    c.hdrOff = optHdr_;
    return c;
}

void App::Confirm(std::string title, std::string text, std::string button, bool danger, std::function<void()> action) {
    confirmTitle_ = std::move(title);
    confirmText_ = std::move(text);
    confirmButton_ = std::move(button);
    confirmDanger_ = danger;
    confirmOpen_ = true;
    pending_ = std::move(action);
    Dirty();
}

void App::Act(std::string const& name, std::string const& arg) {
    install::Layout const layout{status_.root};
    if (name == "confirm") {
        if (!confirmOpen_) {
            return;  // a key press on a dialog that isn't open
        }
        confirmOpen_ = false;
        auto action = std::move(pending_);
        pending_ = {};
        if (action) {
            action();
        }
        Dirty();
        return;
    }
    if (name == "cancel") {
        confirmOpen_ = false;
        pending_ = {};
        Dirty();
        return;
    }
    if (name == "stop_waiting") {
        // The one job that can be stopped: "Start the game once" waiting for the game (or Launch waiting for SteamVR).
        if (busy_ && canCancel_) {
            cancel_ = true;
        }
        return;
    }
    if (busy_ || confirmOpen_) {
        return;  // every button below is disabled while a job runs or a dialog is open: the keyboard can't get past that
    }
    if (name == "browse") {
        if (!host_.pickFolder) {
            return;
        }
        if (auto const picked = host_.pickFolder(L"The Mass Effect Legendary Edition folder")) {
            if (auto const root = detect::RootFromPicked(*picked)) {
                SetGame(*root, "Folder");
            } else {
                Post("That folder doesn't hold Mass Effect Legendary Edition (Game\\ME1\\Binaries\\Win64\\MassEffect1.exe).", true);
                std::lock_guard const lock(jobMutex_);
                log_ = jobLog_;
            }
            Refresh();
        }
    } else if (name == "install") {
        if (!canInstall_ || waiting_ || !payload_) {
            return;
        }
        install::InstallOptions options;
        options.gameSettings = optSettings_;
        options.choices = ChoicesFromModel();
        options.setupExe = options_.exe;
        options.source = SteamGame() ? "Steam" : status_.source;
        auto const payload = *payload_;
        RunJob("Installing", [this, layout, payload, options] {
            Apply(install::Install(layout, payload, options));
            if (!install::ReadStatus(layout.root, "").hasGamerSettings && options.gameSettings) {
                Post("One step left: Mass Effect 1 hasn't been started on this PC, so the VR game settings wait for its settings file. Press "
                     "\"Start the game once\" above (or on the Welcome page).",
                    true);
            }
        });
    } else if (name == "goto_install") {
        page_ = "install";
    } else if (name == "first_start") {
        StartFirstRun();
    } else if (name == "apply_pending") {
        if (waiting_ || !hasGamerSettings_) {
            return;
        }
        auto const choices = PendingOrModelChoices();
        RunJob("Applying the VR game settings", [this, layout, choices] { Apply(install::ApplySettings(layout, choices)); });
    } else if (name == "uninstall") {
        if (waiting_ || !installed_) {
            return;
        }
        bool const keep = keepSettings_;
        bool const removeLocal = removeLocal_;
        std::string text = keep ? "Removes the mod and its loader and puts back every file and game setting it changed. Your VR settings and calibration stay."
                                : "Removes the mod and its loader, puts back every file and game setting it changed, and deletes your VR settings and calibration.";
        if (removeLocal) {
            text += " It also deletes the mod's diagnostics and downloads in %LOCALAPPDATA%\\MEVR.";
        }
        Confirm("Uninstall the VR mod?", text, "Uninstall", true, [this, layout, keep, removeLocal] {
            RunJob("Uninstalling", [this, layout, keep, removeLocal] { Apply(install::Uninstall(layout, keep, options_.exe, removeLocal)); });
        });
    } else if (name == "verify") {
        if (!installed_) {
            return;
        }
        Refresh();  // UpdateRepairStatus: the files as they are now
    } else if (name == "repair") {
        if (waiting_ || !installed_ || !payload_) {
            return;
        }
        auto const payload = *payload_;
        RunJob("Repairing", [this, layout, payload] { Apply(install::Repair(layout, payload)); });
    } else if (name == "apply") {
        if (waiting_ || !hasGamerSettings_) {
            return;
        }
        auto const choices = ChoicesFromModel();
        RunJob("Applying the VR game settings", [this, layout, choices] { Apply(install::ApplySettings(layout, choices)); });
    } else if (name == "restore") {
        if (waiting_ || !settingsApplied_) {
            return;
        }
        RunJob("Restoring your settings", [this, layout] { Apply(install::RestoreSettings(layout)); });
    } else if (name == "launch") {
        if (!gameOk_ || waiting_) {
            return;
        }
        if (detect::GameRunningIn(status_.root)) {
            launchText_ = "Mass Effect is already running.";
            Refresh();
            return;
        }
        bool const steam = SteamGame();
        fs::path const root = status_.root;
        namespace rs = install::launch::runtime_start;
        rs::Plan const plan = rs::PlanFor(xr_.kind, xr_.manifest);
        canCancel_ = plan.action == rs::Action::StartSteamVr;  // the wait for SteamVR can be cut short
        RunJob("Starting Mass Effect", [this, root, steam, plan] {
            // SteamVR first: Halcyon's renderer looks for the headset once, when the game starts.
            if (plan.action == rs::Action::StartSteamVr && !rs::SteamVrRunning()) {
                Progress("Starting SteamVR", 0);
                if (!rs::StartSteamVr(plan)) {
                    Post("Couldn't start SteamVR: starting the game anyway. If VR stays off, start SteamVR yourself, quit the game and Launch again.", true);
                } else {
                    Post("Started SteamVR; waiting for it (at most " + std::to_string(static_cast<int>(rs::kSteamVrWaitS)) + " s).");
                    auto const t0 = std::chrono::steady_clock::now();
                    for (;;) {
                        double const waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                        rs::Gate const gate = rs::Step(rs::SteamVrRunning(), waited);
                        if (gate == rs::Gate::Go) {
                            Post("SteamVR is up.");
                            break;
                        }
                        if (gate == rs::Gate::GoTimedOut || cancel_) {
                            Post(cancel_ ? "Stopped waiting for SteamVR: starting the game now. If VR stays off, quit the game once SteamVR is up and "
                                           "Launch again."
                                         : "SteamVR isn't up after a minute (is the headset on and connected?): starting the game anyway. If VR stays "
                                           "off, quit the game once SteamVR shows the headset and Launch again.",
                                true);
                            break;
                        }
                        Progress("Waiting for SteamVR (turn the headset on)", static_cast<int>(100.0 * waited / rs::kSteamVrWaitS));
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    }
                }
            }
            install::launch::Result const r = install::launch::StartLe1(root, steam);
            Post(r.message, !r.started);
            std::lock_guard const lock(jobMutex_);
            jobProgress_ = r.message;
        });
        jobDone_ = [this] {
            launchText_.clear();
            for (LogLine const& line : log_) {
                launchText_ += (launchText_.empty() ? "" : " ") + line.text;
            }
        };
    } else if (name == "launch_steam") {
        install::launch::StartThroughSteam();
    } else if (name == "detect_headset") {
        Confirm("Ask the VR runtime?",
            "Detect starts your VR runtime (SteamVR may open) to ask for the headset's own eye resolution. Nothing else changes.", "Detect", false,
            [this] {
                auto probe = std::make_shared<HeadsetProbe>();
                RunJob("Asking the VR runtime", [this, probe] {
                    *probe = ProbeHeadset();
                    if (probe->ok) {
                        Post(std::format("{} reports {} × {} per eye ({})", probe->runtime, probe->width, probe->height, probe->system));
                    } else {
                        Post("No answer: " + probe->error, true);
                    }
                });
                jobDone_ = [this, probe] {
                    if (probe->ok) {
                        headsetWidth_ = probe->width;
                        headsetHeight_ = probe->height;
                        quality_ = "headset";
                    }
                };
            });
    } else if (name == "check_update") {
        if (!updateEnabled_) {
            return;
        }
        if (install::updates::LatestReleaseApi(options_.repo).empty()) {
            return;
        }
        auto result = std::make_shared<install::updates::CheckResult>();
        std::string const repo = options_.repo;
        RunJob("Checking for updates", [this, repo, result] {
            *result = install::updates::Check(repo);
            Post(result->text, result->state == install::updates::CheckResult::State::Failed);
        });
        jobDone_ = [this, result] {
            updateText_ = result->text;
            updateUrl_ = result->page;
        };
    } else if (name == "open_update") {
        if (host_.openUrl && !updateUrl_.empty()) {
            host_.openUrl(updateUrl_);
        }
    } else if (name == "mem_page") {
        if (host_.openUrl) {
            host_.openUrl(packs::kMemPage);
        }
    } else if (name == "mem_download") {
        if (memReady_) {
            return;
        }
        // First what the latest release offers (its name and size), then the download once the player has seen them.
        auto asset = std::make_shared<std::optional<packs::Asset>>();
        RunJob("Looking up Mass Effect Modder's latest release", [this, asset] {
            packs::MemLookup const found = packs::FindMemRelease();
            *asset = found.asset;
            if (!*asset) {
                Post(found.error + ". Use its page instead.", true);
            }
        });
        jobDone_ = [this, asset] {
            if (!*asset) {
                return;
            }
            packs::Asset const a = **asset;
            Confirm("Download Mass Effect Modder?",
                std::format("{} ({}, {} on a 50 Mbit/s line) from github.com/MassEffectModder, its official releases. GPL-3.0. It goes to "
                            "%LOCALAPPDATA%\\MEVR\\tools.",
                    a.name, install::http::SizeText(a.size), install::http::TimeText(a.size, 6.25e6)),
                "Download", false, [this, a] {
                    RunJob("Downloading " + a.name, [this, a] {
                        std::string const error = packs::DownloadMem(a, [this, a](install::http::Progress const& p) {
                            if (p.total > 0 && p.received >= p.total) {
                                Progress("Unpacking " + a.name, 100);
                                return;
                            }
                            int const percent = p.total > 0 ? static_cast<int>(100 * p.received / p.total) : 0;
                            Progress(std::format("Downloading {}: {} of {}", a.name, install::http::SizeText(p.received), install::http::SizeText(p.total)), percent);
                        }, &cancel_);
                        Post(error.empty() ? "Mass Effect Modder is ready." : error, !error.empty());
                    });
                });
        };
    } else if (name == "pack_page") {
        if (packs::Pack const* p = packs::Find(arg); p != nullptr && host_.openUrl) {
            host_.openUrl(p->url);
        }
    } else if (name == "pack_file") {
        packs::Pack const* p = packs::Find(arg);
        if (p == nullptr || !host_.pickFile) {
            return;
        }
        if (auto const file = host_.pickFile(std::wstring(L"The downloaded ") + detect::Wide(p->name))) {
            std::erase_if(packFiles_, [&](auto const& e) { return e.first == arg; });
            packFiles_.emplace_back(arg, *file);
            Refresh();
        }
    } else if (name == "pack_install") {
        packs::Pack const* p = packs::Find(arg);
        auto const it = std::find_if(packFiles_.begin(), packFiles_.end(), [&](auto const& e) { return e.first == arg; });
        if (p == nullptr || it == packFiles_.end() || !gameOk_ || waiting_ || !memReady_) {
            return;
        }
        fs::path const file = it->second;
        std::string const id = p->id;
        std::string const packName = p->name;
        Confirm(std::string("Install ") + p->name + "?",
            std::format("This changes the game's files for good: Mass Effect Modder rewrites its textures ({}; it can take a long while the "
                        "first time, as it scans the game). Install texture packs after every other content mod: content mods can't go on "
                        "afterwards, and only a full restore or reinstall of the game takes textures out. Close the game first.",
                p->size),
            "Install for good", true, [this, layout, file, id, packName] {
                RunJob("Installing " + packName, [this, layout, file, id] {
                    // MEM, the manifest's record, and our recorded VR game settings again where MEM wrote over them.
                    Apply(install::InstallPack(layout, id, file, [this](packs::InstallStep const& s) {
                        Progress(s.detail.empty() ? s.stage : s.stage + ": " + s.detail, s.percent);
                    }, &cancel_));
                });
            });
    } else if (name == "admin") {
        if (host_.restartAsAdmin) {
            host_.restartAsAdmin();
        }
    } else if (name == "report_toggle") {
        // A checkbox: what the zip holds changes, so the preview is gathered again.
        if (arg == "snapshot") {
            reportSnapshot_ = !reportSnapshot_;
        } else if (arg == "perf") {
            reportPerf_ = !reportPerf_;
        }
        GatherReport();
    } else if (name == "report_refresh") {
        GatherReport();
    } else if (name == "report_create") {
        if (!bundle_) {
            return;
        }
        auto bundle = bundle_;
        install::report::Options const options = ReportOptions();
        auto result = std::make_shared<install::report::Result>();
        RunJob("Writing the report", [this, bundle, options, result] {
            *result = install::report::Write(*bundle, options);
            Post(result->ok ? "Saved " + detect::Utf8(result->zip.filename().wstring()) + " (" + install::report::SizeText(result->bytes) +
                                  ") to " + result->folder + ". Nothing was sent anywhere."
                            : "Couldn't save the report: " + result->error,
                !result->ok);
        });
        jobDone_ = [this, result] {
            crashOffer_ = false;
            reportHasZip_ = result->ok;
            if (result->ok) {
                reportZipPath_ = result->zip;
                reportZip_ = "Saved to " + result->folder + ": " + detect::Utf8(result->zip.filename().wstring()) +
                             ". Open it to see exactly what's inside; attach it to your issue or give it to your AI assistant.";
                if (host_.revealFile) {
                    host_.revealFile(result->zip);
                }
            } else {
                reportZip_ = "Couldn't save the report: " + result->error;
            }
        };
    } else if (name == "report_reveal") {
        if (host_.revealFile && !reportZipPath_.empty()) {
            host_.revealFile(reportZipPath_);
        }
    } else if (name == "report_issue") {
        if (host_.openUrl && bundle_) {
            host_.openUrl(install::report::IssueUrl(*bundle_));
        }
    } else if (name == "crash_report") {
        page_ = "report";
    } else if (name == "crash_dismiss") {
        install::report_marks::Touch(install::report_marks::DismissedMark(install::LocalDataDir()), "dismissed in MEVR Setup");
        crashOffer_ = false;
    }
    Dirty();
}

void App::Sample(std::string const& state) {
    // The preview's stand-ins (tools/setup --preview): a PS VR2 on SteamVR, the Steam copy.
    sample_ = true;
    bool const notInstalled = state == "fresh" || state == "nogame" || state == "oldversion" || state == "firststart-fresh";
    bool const neverStarted = state.rfind("firststart", 0) == 0;  // no GamerSettings.ini yet
    status_ = install::Status{};
    status_.gameFound = state != "nogame";
    status_.root = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Mass Effect Legendary Edition";
    status_.source = "Steam";
    status_.version = state == "oldversion" ? detect::ParseVersion("2.0.0.48419") : detect::ParseVersion("2.0.0.48602");
    status_.versionCheck = state == "oldversion" ? detect::VersionCheck::Different : detect::VersionCheck::Supported;
    status_.bink = notInstalled ? detect::BinkState::Original : detect::BinkState::Ours;
    status_.installed = !notInstalled;
    status_.installedVersion = "0.1.0";
    status_.loader = "mevr";
    status_.hasGamerSettings = !neverStarted;
    status_.gameSettings = status_.installed && !neverStarted && state != "pending-apply";
    if ((neverStarted && status_.installed) || state == "pending-apply") {
        install::settings::Choices choices;
        choices.target = install::settings::TargetFor("headset", 2824, 2880);
        status_.pendingSettings = install::PendingFromChoices(choices);
    }
    if (state == "repair-needed") {
        status_.needsRepair = {install::paths::kBink, install::paths::kToc};
    }
    status_.canWrite = true;
    if (state == "content") {
        status_.packs.push_back({"alot-le1", "ALOT for ME1", "mem", "2026-10-23T12:00:00"});
        status_.texturesMarked = true;
    }
    payload_ = install::Payload{L"payload"};
    hasPayload_ = true;
    xr_ = state == "meta" ? detect::XrRuntime{"C:\\Program Files\\Oculus\\Support\\oculus-runtime\\oculus_openxr_64.json", "Oculus", "Meta"}
                          : detect::XrRuntime{"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json", "SteamVR/OpenXR", "SteamVR"};
    headsetWidth_ = 2824;
    headsetHeight_ = 2880;
    quality_ = "headset";
    log_.clear();
    busy_ = false;
    canCancel_ = false;
    confirmOpen_ = false;
    packFiles_.clear();
    if (state == "content") {
        packFiles_.emplace_back("isl-le1", L"Improved Static Lighting for ME1-363-1-0.7z");
    }
    // Refresh, without touching the disk for the status (the preview's root doesn't exist here).
    install::Status const keep = status_;
    fs::path const root = status_.root;
    status_.root.clear();
    Refresh();
    status_ = keep;
    status_.root = root;
    // Refresh read nothing for these; set what the pages show.
    gameOk_ = keep.gameFound;
    versionOk_ = keep.versionCheck == detect::VersionCheck::Supported;
    installed_ = keep.installed;
    settingsApplied_ = keep.gameSettings;
    hasGamerSettings_ = keep.hasGamerSettings;
    settingsPending_ = keep.pendingSettings.has_value();
    texturesMarked_ = keep.texturesMarked;
    loaderOk_ = gameOk_;
    canInstall_ = gameOk_ && versionOk_;
    ready_ = canInstall_;
    adminNeeded_ = false;
    rootText_ = gameOk_ ? detect::Utf8(root.wstring()) : "No game folder";
    chipGame_ = gameOk_ ? "Mass Effect 1 LE" : "Game not found";
    chipInstalled_ = installed_ ? "Installed 0.1.0" : "Not installed";
    gameTitle_ = gameOk_ ? "Found" : "Not found";
    gameText_ = gameOk_ ? "Steam: Mass Effect Legendary Edition" : "Pick the folder on the Welcome page";
    versionTitle_ = keep.version ? keep.version->ToString() : "Unknown";
    loaderTitle_ = keep.bink == detect::BinkState::Ours ? "MEVR loader" : "None yet";
    loaderText_ = gameOk_ ? detect::BinkStateText(keep.bink) : "";
    versionText_ = !gameOk_    ? ""
        : versionOk_           ? "The version the mod is built for"
                               : "The mod needs 2.0.0.48602 and stays off on any other. Update the game in Steam or the EA app.";
    firstStartText_ = FirstStartText(installed_, settingsPending_);
    bool const applied = installed_ && settingsApplied_;
    curRes_ = !hasGamerSettings_ ? "-" : applied ? "2824 × 2880" : "2560 × 1440";
    curWindow_ = !hasGamerSettings_ ? "-" : applied ? "Borderless" : "Windowed or full screen";
    curHdr_ = !hasGamerSettings_ ? "-" : "Off";
    curVsync_ = !hasGamerSettings_ ? "-" : OnOff(!applied);
    curBlur_ = !hasGamerSettings_ ? "-" : OnOff(!applied);
    curGrain_ = !hasGamerSettings_ ? "-" : OnOff(!applied);
    UpdateRepairStatus();
    UpdateHero();
    uninstallText_ = installed_ ? "41 files removed, 2 put back as they were, 2 settings files returned to your values." : "Nothing is installed.";
    memReady_ = state == "content";
    memText_ = memReady_ ? "Ready: it installs the texture packs below (GPL, from its own GitHub releases)."
                         : "Installs texture packs (GPL, by the ALOT team). Downloaded from its own GitHub releases when you press Download.";
    footerText_ = "Mod log: Binaries\\Win64\\Logs\\LE1MEVR.log · loader log: Logs\\MEVRLoader.log";
    UpdatePacks();
    if (state == "content") {
        for (PackView& v : packs_) {
            v.installed = v.id == "alot-le1";
            v.state = v.installed ? "Installed with this setup." : v.id == "isl-le1" ? "Ready to install: Improved Static Lighting for ME1-363-1-0.7z"
                                                                                     : "Not downloaded: open its page, download the MEM version, then choose the file.";
            v.file = v.id == "isl-le1" ? "Improved Static Lighting for ME1-363-1-0.7z" : "";
        }
        requestText_ = "You asked for: Improved Static Lighting for ME1.";
    } else {
        for (PackView& v : packs_) {
            v.installed = false;
            v.file.clear();
            v.state = "Not downloaded: open its page, download the MEM version, then choose the file.";
        }
        requestText_.clear();
    }
    if (state == "busy") {
        busy_ = true;
        progressText_ = "Installing: copied Game/ME1/Binaries/Win64/ASI/menu/main.rcss";
        progress_ = 64;
        log_ = {{"renamed Game/ME1/Binaries/Win64/bink2w64.dll to Game/ME1/Binaries/Win64/bink2w64_original.dll", false},
            {"added Game/ME1/Binaries/Win64/bink2w64.dll", false}, {"added Game/ME1/Binaries/Win64/ASI/LE1MEVR.asi", false},
            {"added Game/ME1/Binaries/Win64/ASI/fonts/Rajdhani-Bold.ttf", false}, {"added Game/ME1/Binaries/Win64/ASI/menu/main.rcss", false}};
    }
    if (state == "firststart-busy") {
        namespace fst = install::launch::first_start;
        busy_ = true;
        canCancel_ = true;
        progressText_ = fst::PhaseText(fst::Phase::WaitingForQuit);
        progress_ = 66;
        log_ = {{"Started Mass Effect (-NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles 20 -OVERRIDELANGUAGE=INT)", false},
            {fst::PhaseText(fst::Phase::WaitingForQuit), false}};
    }
    if (state == "firststart-installed") {
        log_ = {{"added Game/ME1/Binaries/Win64/ASI/LE1MEVR.asi", false},
            {"warning: Mass Effect 1 hasn't been started on this PC yet, so its settings file (GamerSettings.ini) doesn't exist and the VR game "
             "settings can't go on. They're kept: start the game once (MEVR Setup's \"Start the game once\"), quit at its main menu, and they're "
             "applied (or use Repair afterwards).",
                false},
            {"Done.", false},
            {"One step left: Mass Effect 1 hasn't been started on this PC, so the VR game settings wait for its settings file. Press \"Start the "
             "game once\" above (or on the Welcome page).",
                true}};
    }
    if (state == "installed-log") {
        log_ = {{"kept the existing Game/ME1/Binaries/Win64/ASI/MELEVR.ini", false},
            {"edited Game/ME1/Binaries/Win64/ASI/MELEVR.ini (2 values)", false},
            {"edited Game/ME1/BioGame/Config/GamerSettings.ini (9 values)", false},
            {"edited Game/ME1/BioGame/Config/BIOEngine.ini (1 values)", false}, {"Done.", false}};
    }
    if (state == "confirm-uninstall") {
        Confirm("Uninstall the VR mod?",
            "Removes the mod and its loader, puts back every file and game setting it changed, and deletes your VR settings and calibration.",
            "Uninstall", true, {});
    }
    if (state == "confirm-pack") {
        Confirm("Install Improved Static Lighting for ME1?",
            "This changes the game's files for good: Mass Effect Modder rewrites its textures (about 9.7 GB; it can take a long while the first "
            "time, as it scans the game). Install texture packs after every other content mod: content mods can't go on afterwards, and only a "
            "full restore or reinstall of the game takes textures out. Close the game first.",
            "Install for good", true, {});
    }
    if (state == "waiting") {
        waiting_ = true;
        waitText_ = "Changes start once the game has exited.";
    } else {
        waiting_ = false;
    }
    updateEnabled_ = false;
    updateText_ = "No release page set yet (MEVR-Setup.ini, [Update] repo).";
    // Report a problem's stand-ins: a crash last session, a gathered preview.
    crashOffer_ = state == "crash";
    reportRequest_ = state == "report-request" ? "You asked for a report from the game's menu. Look over what goes in, then create it." : "";
    reportSnapshot_ = state == "report-request";
    reportPerf_ = false;
    reportHasZip_ = state == "report-saved";
    reportZip_ = reportHasZip_ ? "Saved to Desktop: MEVR-report-2026-10-26-0143.zip. Open it to see exactly what's inside; attach it to your issue or "
                                 "give it to your AI assistant."
                               : "";
    reportReady_ = state.rfind("report", 0) == 0;
    reportFiles_.clear();
    if (reportReady_) {
        reportFiles_ = {{"REPORT.md", "~6.2 KB", "The summary: system, what went wrong, settings changed, a prompt for your AI assistant"},
            {"logs/LE1MEVR.log", "246.1 KB", "4 redacted"}, {"logs/LE1MEVR.prev.log", "502.1 KB", "6 redacted"},
            {"logs/MELEVR_Log.txt", "3.0 MB", "its last session only: the first 0.5 MB and the last 2.5 MB of 9.1 MB (the file holds 182.7 MB from every session)"},
            {"logs/MEVRLoader.log", "1.2 KB", ""}, {"logs/crash-breadcrumbs.txt", "29.4 KB", "written 2026-10-26 00:11"},
            {"logs/reflect-check.json", "80.9 KB", "12 redacted"}, {"settings/LE1MEVR.ini", "12.7 KB", ""}, {"settings/MELEVR.ini", "9.8 KB", ""},
            {"settings/MEVR-install.json", "6.0 KB", ""}, {"system/folders.txt", "3.1 KB", "file names and sizes"}};
        if (reportSnapshot_) {
            reportFiles_.push_back({"snapshot/20261026-014210.json", "18.2 KB", ""});
            reportFiles_.push_back({"snapshot/20261026-014210-left.png", "2.1 MB", "what that eye showed"});
            reportFiles_.push_back({"snapshot/20261026-014210-right.png", "2.1 MB", "what that eye showed"});
        }
        reportRedactions_ = "22 redactions: your Windows user name 4 times, the computer's name 2, your profile folder 16, e-mail addresses 0. "
                            "They read <user>, <computer>, <profile>, <email> in the zip.";
        reportHeadline_ = "Crash: 0xC0000005 (access violation) at ntdll.dll+0x1A12D";
        reportText_ = "12 files, 4.1 MB before compression.";
    }
    Dirty();
}

} // namespace mevr::setup
