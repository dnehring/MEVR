// SPDX-License-Identifier: MIT
#include "SetupApp.hpp"

#include "XrProbe.hpp"
#include "core/Json.hpp"
#include "install/GameSettings.hpp"
#include "install/Http.hpp"
#include "install/Launch.hpp"
#include "install/Packs.hpp"
#include "install/Updates.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <memory>
#include <system_error>

namespace mevr::setup {
namespace {

namespace fs = std::filesystem;
namespace detect = install::detect;
namespace packs = install::packs;

std::string OnOff(bool on) {
    return on ? "On" : "Off";
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
    c.Bind("packs", &packs_);
    c.Bind("log_lines", &log_);
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
             Flag{"upstream_found", &upstreamFound_}, Flag{"gamer_read_only", &gamerReadOnly_}, Flag{"remove_local", &removeLocal_}}) {
        c.Bind(f.name, f.value);
    }
    struct Text {
        char const* name;
        std::string* value;
    };
    for (Text const& t : {Text{"page", &page_}, Text{"version", &version_}, Text{"chip_game", &chipGame_}, Text{"chip_xr", &chipXr_},
             Text{"chip_installed", &chipInstalled_}, Text{"hero_title", &heroTitle_}, Text{"hero_text", &heroText_},
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
             Text{"upstream_text", &upstreamText_}, Text{"local_text", &localText_}}) {
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
    updateText_ = updateEnabled_ ? "Not checked yet." : "No release page set yet (MEVR-Setup.ini, [Update] repo).";
    if (options_.request) {
        page_ = "content";
        if (auto const text = install::ReadFile(install::RequestFile())) {
            if (auto const request = json::Parse(*text)) {
                std::string names;
                for (json::Value const& id : request->Get("packs").Items()) {
                    if (packs::Pack const* p = packs::Find(id.AsString())) {
                        names += names.empty() ? p->name : std::string(", ") + p->name;
                    }
                }
                requestText_ = names.empty() ? std::string("Choose a pack below.") : "You asked for: " + names + ".";
            }
        }
        std::error_code ec;
        fs::remove(install::RequestFile(), ec);
    }
    if (options_.waitPid != 0) {
        waiting_ = true;
        waitText_ = "Changes start once the game has exited.";
    }
    Refresh();
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
    rootText_ = gameOk_ ? detect::Utf8(status_.root.wstring()) : "No game folder";
    sourceText_ = status_.source.empty() ? "nothing yet" : status_.source;

    if (!gameOk_) {
        heroTitle_ = "Game not found";
        heroText_ = "Mass Effect Legendary Edition wasn't found in Steam or the EA app. Pick its folder below (the one with Game\\ME1 in it).";
    } else if (!versionOk_) {
        heroTitle_ = "Unsupported game version";
        heroText_ = "The mod is built for MassEffect1.exe 2.0.0.48602 and stays switched off on any other version, so it isn't installed.";
    } else if (!binkUsable) {
        heroTitle_ = "The game needs a repair";
        heroText_ = detect::BinkStateText(status_.bink);
    } else if (installed_) {
        heroTitle_ = "Ready for VR";
        heroText_ = "The mod is installed. Start your VR runtime, put the headset on and launch. In game, \\ or both sticks held opens the VR menu.";
    } else if (upstreamFound_) {
        heroTitle_ = "Ready to install";
        heroText_ = "Halcyon's MELE-VR is installed: Install moves its two DLLs aside (Uninstall puts them back), then puts the mod, its ASI loader and "
                    "the VR game settings in place, recorded so it can all be undone.";
    } else {
        heroTitle_ = "Ready to install";
        heroText_ = "Everything checks out. Install puts the mod, its ASI loader and the VR game settings in place, recorded so it can all be undone.";
    }
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

void App::UpdateRepairStatus() {
    if (sample_) {
        repairTitle_ = installed_ ? "Every file is as installed" : "Not installed";
        repairText_ = installed_ ? "Nothing to do." : "Install first.";
        return;
    }
    if (!installed_ || status_.root.empty()) {
        repairTitle_ = "Not installed";
        repairText_ = "Install first.";
        return;
    }
    install::Transaction tx(install::Layout{status_.root});
    if (!tx.Load() || !tx.Exists()) {
        repairTitle_ = "Not installed";
        repairText_ = "Install first.";
        return;
    }
    auto const bad = tx.Verify();
    repairTitle_ = bad.empty() ? "Every file is as installed" : std::format("{} file(s) need repair", bad.size());
    repairText_.clear();
    for (std::string const& f : bad) {
        repairText_ += (repairText_.empty() ? "" : ", ") + fs::path(detect::Wide(f)).filename().string();
    }
    if (bad.empty()) {
        repairText_ = "Nothing to do.";
    }
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
            UpdateRepairStatus();  // the files as they are now, not when the setup started
            Dirty();
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
                Post("The VR game settings weren't applied: start the game once, quit, then apply them on the Settings page.");
            }
        });
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
        if (detect::GameRunning()) {
            launchText_ = "Mass Effect is already running.";
            Refresh();
            return;
        }
        bool const steam = SteamGame();
        fs::path const root = status_.root;
        RunJob("Starting Mass Effect", [this, root, steam] {
            install::launch::Result const r = install::launch::StartLe1(root, steam);
            Post(r.message, !r.started);
            std::lock_guard const lock(jobMutex_);
            jobProgress_ = r.message;
        });
        jobDone_ = [this] {
            launchText_ = log_.empty() ? "" : log_.back().text;
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
        std::string const api = install::updates::LatestReleaseApi(options_.repo);
        if (api.empty()) {
            return;
        }
        auto result = std::make_shared<std::pair<std::string, std::string>>();
        RunJob("Checking for updates", [this, api, result] {
            install::http::Response const r = install::http::Get(api);
            auto const release = r.status == 200 ? install::updates::ParseLatestRelease(r.body) : std::nullopt;
            if (!release) {
                result->first = "Couldn't check (" + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error) + ").";
            } else if (install::updates::CompareVersions(release->tag, install::updates::kVersion) > 0) {
                result->first = "Version " + release->tag + " is out.";
                result->second = release->page;
            } else {
                result->first = "This is the latest version.";
            }
            Post(result->first);
        });
        jobDone_ = [this, result] {
            updateText_ = result->first;
            updateUrl_ = result->second;
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
            install::http::Response const r = install::http::Get(packs::kMemReleasesApi);
            *asset = r.status == 200 ? packs::MemAssetFrom(r.body) : std::nullopt;
            if (!*asset) {
                Post("Couldn't find the download (" + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error) + "). Use its page instead.", true);
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
                        fs::path const file = packs::MemExe().parent_path() / detect::Wide(a.name);
                        std::string error = install::http::Download(a.url, file, [this, a](install::http::Progress const& p) {
                            int const percent = p.total > 0 ? static_cast<int>(100 * p.received / p.total) : 0;
                            Progress(std::format("Downloading {}: {} of {}", a.name, install::http::SizeText(p.received), install::http::SizeText(p.total)), percent);
                        }, &cancel_);
                        if (error.empty()) {
                            Progress("Unpacking", 100);
                            error = packs::UnpackMem(file);
                        }
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
        bool const reapply = status_.gameSettings;
        auto const choices = ChoicesFromModel();
        std::string const id = p->id;
        std::string const packName = p->name;
        Confirm(std::string("Install ") + p->name + "?",
            std::format("This changes the game's files for good: Mass Effect Modder rewrites its textures ({}; it can take a long while the "
                        "first time, as it scans the game). Install texture packs after every other content mod: content mods can't go on "
                        "afterwards, and only a full restore or reinstall of the game takes textures out. Close the game first.",
                p->size),
            "Install for good", true, [this, layout, file, id, packName, reapply, choices] {
                RunJob("Installing " + packName, [this, layout, file, id, packName, reapply, choices] {
                    packs::Pack const* pack = packs::Find(id);
                    std::string const error = packs::InstallWithMem(*pack, file, layout.root, [this](packs::InstallStep const& s) {
                        Progress(s.detail.empty() ? s.stage : s.stage + ": " + s.detail, s.percent);
                    }, &cancel_);
                    if (!error.empty()) {
                        Post(error, true);
                        return;
                    }
                    install::RecordPack(layout, id, packName);
                    Post(packName + " is installed.");
                    if (reapply) {
                        // MEM writes the game's graphics settings too: ours again on top.
                        Apply(install::ApplySettings(layout, choices));
                    }
                });
            });
    } else if (name == "admin") {
        if (host_.restartAsAdmin) {
            host_.restartAsAdmin();
        }
    }
    Dirty();
}

void App::Sample(std::string const& state) {
    // The preview's stand-ins (tools/setup --preview): a PS VR2 on SteamVR, the Steam copy.
    sample_ = true;
    status_ = install::Status{};
    status_.gameFound = state != "nogame";
    status_.root = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Mass Effect Legendary Edition";
    status_.source = "Steam";
    status_.version = state == "oldversion" ? detect::ParseVersion("2.0.0.48419") : detect::ParseVersion("2.0.0.48602");
    status_.versionCheck = state == "oldversion" ? detect::VersionCheck::Different : detect::VersionCheck::Supported;
    status_.bink = state == "fresh" || state == "oldversion" ? detect::BinkState::Original : detect::BinkState::Ours;
    status_.installed = state != "fresh" && state != "nogame" && state != "oldversion";
    status_.installedVersion = "0.1.0";
    status_.loader = "mevr";
    status_.gameSettings = status_.installed;
    status_.hasGamerSettings = true;
    status_.canWrite = true;
    if (state == "content") {
        status_.packs.push_back({"alot-le1", "ALOT for ME1", "mem", "2026-10-23T12:00:00"});
        status_.texturesMarked = true;
    }
    payload_ = install::Payload{L"payload"};
    hasPayload_ = true;
    xr_ = {"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json", "SteamVR/OpenXR", "SteamVR"};
    headsetWidth_ = 2824;
    headsetHeight_ = 2880;
    quality_ = "headset";
    log_.clear();
    busy_ = false;
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
    hasGamerSettings_ = true;
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
    if (!gameOk_) {
        heroTitle_ = "Game not found";
        heroText_ = "Mass Effect Legendary Edition wasn't found in Steam or the EA app. Pick its folder below (the one with Game\\ME1 in it).";
    } else if (!versionOk_) {
        heroTitle_ = "Unsupported game version";
        heroText_ = "The mod is built for MassEffect1.exe 2.0.0.48602 and stays switched off on any other version, so it isn't installed.";
        versionText_ = "The mod needs 2.0.0.48602 and stays off on any other. Update the game in Steam or the EA app.";
    } else if (installed_) {
        heroTitle_ = "Ready for VR";
        heroText_ = "The mod is installed. Start your VR runtime, put the headset on and launch. In game, \\ or both sticks held opens the VR menu.";
        versionText_ = "The version the mod is built for";
    } else {
        heroTitle_ = "Ready to install";
        heroText_ = "Everything checks out. Install puts the mod, its ASI loader and the VR game settings in place, recorded so it can all be undone.";
        versionText_ = "The version the mod is built for";
    }
    curRes_ = installed_ ? "2824 × 2880" : "2560 × 1440";
    curWindow_ = installed_ ? "Borderless" : "Windowed or full screen";
    curHdr_ = "Off";
    curVsync_ = OnOff(!installed_);
    curBlur_ = OnOff(!installed_);
    curGrain_ = OnOff(!installed_);
    UpdateRepairStatus();
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
    Dirty();
}

} // namespace mevr::setup
