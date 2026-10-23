// SPDX-License-Identifier: MIT
#pragma once

#include "install/Detect.hpp"
#include "install/Installer.hpp"

#include <RmlUi/Core/DataModelHandle.h>

#include <atomic>
#include <filesystem>
#include <functional>
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
};

struct Options {
    std::filesystem::path exe;                      // this program
    std::optional<std::filesystem::path> gameRoot;  // --game
    bool request = false;                           // --request: the in-game menu asked for optional content
    unsigned long waitPid = 0;                      // --wait-pid: the game is still closing
    std::string repo;                               // the update check's repository (MEVR-Setup.ini)
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
    void Dirty();

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
    std::string chipGame_, chipXr_, chipInstalled_;
    std::string heroTitle_, heroText_;
    std::string gameTitle_, gameText_, versionTitle_, versionText_, xrTitle_, xrText_, loaderTitle_, loaderText_, loaderPlan_;
    std::string rootText_, sourceText_, updateText_, updateUrl_;
    std::string quality_ = "performance", qualityText_, headsetText_;
    bool optSettings_ = true, optVsync_ = true, optBlur_ = true, optGrain_ = true, optHdr_ = true;
    std::string curRes_, curWindow_, curHdr_, curVsync_, curBlur_, curGrain_;
    std::string repairTitle_, repairText_, uninstallText_, launchText_, footerText_, waitText_, requestText_, memText_;
    bool keepSettings_ = false;
    std::vector<PackView> packs_;
    std::vector<LogLine> log_;
    std::string progressText_;
    int progress_ = 0;
    bool confirmOpen_ = false, confirmDanger_ = false;
    std::string confirmTitle_, confirmText_, confirmButton_;

    std::function<void()> pending_;  // what Confirm runs
    int headsetWidth_ = 0, headsetHeight_ = 0;
    std::vector<std::pair<std::string, std::filesystem::path>> packFiles_;  // id -> the download the player chose

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
