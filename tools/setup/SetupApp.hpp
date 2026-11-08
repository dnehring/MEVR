// SPDX-License-Identifier: MIT
#pragma once

#include "install/Detect.hpp"
#include "install/Installer.hpp"
#include "install/Report.hpp"
#include "install/Updates.hpp"

#include <RmlUi/Core/DataModelHandle.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Rml {
class Context;
}

// MEVR-Setup.exe's state and actions (research/33): what the window shows (the data model "setup" that
// assets/menu/setup.rml binds) and what its buttons do, through src/install. Jobs that touch files or the network run
// on a worker thread; Tick() moves their progress into the model on the UI thread.
namespace mevr::setup {

struct PackView {
    std::string id, name, author, description, size, licence, requirements, state, file;
    bool installed = false;
    bool large = false;
};

// A file the problem report will hold (the Report page's list).
struct ReportFileView {
    std::string name, size, note;
};

struct LogLine {
    std::string text;
    bool error = false;
};

// What the window needs from the platform; the preview leaves them empty.
struct Host {
    std::function<std::optional<std::filesystem::path>(std::wstring const& title)> pickFolder;
    std::function<std::optional<std::filesystem::path>(std::wstring const& title)> pickFile;
    std::function<void(std::string const& url)> openUrl;
    std::function<void()> restartAsAdmin;
    std::function<void(std::filesystem::path const& file)> revealFile;  // Explorer with the file selected
    // Start the verified new installer and close this window only after the new process starts; error or empty.
    std::function<std::string(std::filesystem::path const&, std::filesystem::path const&)> updateInstaller;
};

struct Options {
    std::filesystem::path exe;                      // this program
    std::optional<std::filesystem::path> gameRoot;  // --game
    bool request = false;                           // --request: the in-game menu asked for optional content
    unsigned long waitPid = 0;                      // --wait-pid: the game is still closing
    std::string repo;                               // the update check's repository (MEVR-Setup.ini)
    bool finishUpdate = false;                      // downloaded installer: apply once the old setup exits
};

class App {
public:
    explicit App(Options options, Host host);
    ~App();
    App(App const&) = delete;
    App& operator=(App const&) = delete;

    // The data model, before the document loads.
    bool Bind(Rml::Context* context);
    // Finds the game, reads everything once.
    void Start();
    // On the UI thread every frame: a finished job's result, a waited-for game exiting.
    void Tick();
    void Refresh();

    // The preview: fill the model with a sample state ("fresh", "installed", "nogame", "oldversion", "busy",
    // "confirm-uninstall", "content", ...).
    void Sample(std::string const& state);
    void SetPage(std::string const& page);
    bool Busy() const { return busy_; }
    // Whether the window still waits for the game to exit (--wait-pid).
    bool Waiting() const { return waiting_; }
    // The model's state as "key: value" lines, for the preview's real-start mode (--preview <folder> --request: the
    // "Install after I quit" test, research/33 section 10).
    std::string StateText() const;

private:
    void Act(std::string const& name, std::string const& arg);
    void Confirm(std::string title, std::string text, std::string button, bool danger, std::function<void()> action);
    void RunJob(std::string title, std::function<void()> job);
    void Post(std::string text, bool error = false);
    void Progress(std::string text, int percent);
    void Apply(install::Outcome const& outcome);
    void SetGame(std::filesystem::path const& root, std::string const& source);
    bool SteamGame() const;
    install::settings::Choices ChoicesFromModel() const;
    void UpdatePacks();
    // The Optional content page's voice orders card (install/Voice, research/33 section 14): what's installed, the sizes,
    // what Install would do, from the manifest (no network).
    void UpdateVoice();
    // The Repair page's status (and repairNeeded_) from status_.needsRepair, which `reread` reads again from the files as
    // they are now (whenever the page is shown; Refresh has just read it).
    void UpdateRepairStatus(bool reread = false);
    void Dirty();
    // Report a problem (install/Report): gathers what the zip would hold for the page's preview (a job), shows it.
    void GatherReport();
    void ShowBundle();
    install::report::Options ReportOptions() const;
    // The Welcome page's hero (title, text, its one button) from the state Refresh computed.
    void UpdateHero();
    // "Start the game once": launches the game (unless it runs), waits for GamerSettings.ini and the game's exit, applies.
    void StartFirstRun();
    // The VR game settings to apply now: those chosen at install if they wait, else the pages' current choices.
    install::settings::Choices PendingOrModelChoices() const;

    Options options_;
    Host host_;
    std::optional<install::Payload> payload_;
    install::Status status_;
    install::detect::XrRuntime xr_;
    Rml::DataModelHandle handle_;

    // ---- Bound to the model ----
    std::string page_ = "welcome";
    std::string version_;
    bool gameOk_ = false, versionOk_ = false, xrOk_ = false, loaderOk_ = false, installed_ = false, ready_ = false;
    bool canInstall_ = false, hasPayload_ = false, adminNeeded_ = false, busy_ = false, waiting_ = false;
    bool hasGamerSettings_ = false, settingsApplied_ = false, texturesMarked_ = false, memReady_ = false, headsetKnown_ = false;
    bool updateEnabled_ = false;
    bool updateAvailable_ = false;
    bool applyingUpdate_ = false;
    std::optional<install::updates::Release> updateRelease_;
    bool upstreamFound_ = false, gamerReadOnly_ = false, removeLocal_ = false;
    std::string upstreamText_, localText_;
    bool sample_ = false;    // the preview's sample states: nothing is read from disk for them
    std::string shownPage_;  // the page the last Tick saw (the document changes `page` by itself)
    std::string chipGame_, chipXr_, chipInstalled_;
    // The Install page's last row: what Install does now, from the installed version against this setup's (an update,
    // the same version again, or going back), never "update" by default.
    std::string installTitle_, installButton_;
    std::string heroTitle_, heroText_;
    std::string gameTitle_, gameText_, versionTitle_, versionText_, xrTitle_, xrText_, loaderTitle_, loaderText_, loaderPlan_;
    std::string rootText_, sourceText_, updateText_, updateUrl_;
    // Automatic uses a low first-launch size and learns the active runtime's eye size when VR starts.
    std::string quality_ = "auto", qualityText_, headsetText_;
    bool optSettings_ = true, optVsync_ = true, optBlur_ = true, optGrain_ = true, optHdr_ = true;
    std::string curRes_, curWindow_, curHdr_, curVsync_, curBlur_, curGrain_;
    std::string repairTitle_, repairText_, uninstallText_, launchText_, footerText_, waitText_, requestText_, memText_;
    bool keepSettings_ = false;
    // The game's first start (research/33 section 9): the VR game settings wait for GamerSettings.ini; "Start the game
    // once" (a job that can be stopped: canCancel_) starts it, waits for the file and the game's exit, then applies them.
    bool settingsPending_ = false, repairNeeded_ = false, canCancel_ = false;
    std::string firstStartText_;
    // The Welcome page's one button: what matters most now ("goto_install", "launch", "repair", "first_start",
    // "apply_pending"), its label, and whether it can run.
    std::string heroAction_, heroButton_;
    bool heroEnabled_ = false;
    // What to start before the game (install/Launch runtime_start), and the in-game menu key's name on this keyboard.
    std::string launchAdvice_, menuKey_;
    std::vector<PackView> packs_;
    // ---- Voice orders (install/Voice): the card's choices (from what's installed when the game is first read) and texts.
    bool voiceInstalled_ = false, voiceSmall_ = false, voiceMedium_ = false, voiceLlm_ = true, voiceReady_ = false, voiceWarn_ = false;
    bool voiceUpToDate_ = false, voiceChoicesRead_ = false;
    std::string voiceState_, voiceSize_, voiceSources_, voiceLicences_, voiceButton_ = "Install", voiceModelText_, voiceParts_;
    std::string cancelText_ = "Stop waiting";  // the progress row's stop button
    std::vector<LogLine> log_;
    std::string progressText_;
    int progress_ = 0;
    bool confirmOpen_ = false, confirmDanger_ = false;
    std::string confirmTitle_, confirmText_, confirmButton_;

    std::function<void()> pending_;  // what Confirm runs
    int headsetWidth_ = 0, headsetHeight_ = 0;
    std::vector<std::pair<std::string, std::filesystem::path>> packFiles_;  // id -> the download the player chose

    // ---- Report a problem
    std::shared_ptr<install::report::Bundle> bundle_;  // the last gathered preview: what Create writes
    std::vector<ReportFileView> reportFiles_;
    bool reportSnapshot_ = false, reportPerf_ = false, reportReady_ = false, crashOffer_ = false, reportHasZip_ = false;
    std::string reportText_, reportRedactions_, reportHeadline_, reportZip_, reportRequest_;
    std::filesystem::path reportZipPath_;

    // ---- The worker ----
    std::thread worker_;
    std::atomic<bool> working_{false};
    std::atomic<bool> cancel_{false};
    std::mutex jobMutex_;
    std::vector<LogLine> jobLog_;
    std::string jobProgress_;
    int jobPercent_ = 0;
    bool jobChanged_ = false;
    std::function<void()> jobDone_;  // runs on the UI thread when the job ends
};

} // namespace mevr::setup
