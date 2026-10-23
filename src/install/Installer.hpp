// SPDX-License-Identifier: MIT
#pragma once

#include "install/Detect.hpp"
#include "install/GameSettings.hpp"
#include "install/Manifest.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The setup app's steps (research/33), on a game root: Install, Uninstall, Repair, the VR game settings and their
// Restore, and what the Welcome page shows. Shared by the window (tools/setup) and its command line, which is how
// they're tested against a fake game folder.
namespace mevr::install {

// What the release ships next to MEVR-Setup.exe (tools/package-release.ps1): payload\LE1MEVR.asi (the monolithic
// build), payload\bink2w64.dll (our loader, src/loader), payload\fonts\*, payload\menu\* (the in-game menu's
// documents and the setup's own).
struct Payload {
    std::filesystem::path dir;
    bool Valid() const;
    std::filesystem::path Asi() const { return dir / L"LE1MEVR.asi"; }
    std::filesystem::path Loader() const { return dir / L"bink2w64.dll"; }
    std::filesystem::path Fonts() const { return dir / L"fonts"; }
    std::filesystem::path Menu() const { return dir / L"menu"; }
};
// `exeDir`\payload, or `exeDir` itself if it holds the files. nullopt when neither does (the copy installed in the
// game's ASI folder has no payload: it can uninstall and add packs, not install or repair).
std::optional<Payload> FindPayload(std::filesystem::path const& exeDir);

struct Status {
    bool gameFound = false;
    std::filesystem::path root;
    std::string source;  // "Steam", "EA app", "Folder"
    std::optional<ModuleVersion> version;
    detect::VersionCheck versionCheck = detect::VersionCheck::Missing;
    detect::BinkState bink = detect::BinkState::Missing;
    bool installed = false;      // a manifest exists
    std::string installedVersion;
    std::string loader;          // the manifest's
    bool gameSettings = false;
    bool texturesMarked = false;
    bool gameRunning = false;
    bool canWrite = true;
    bool hasGamerSettings = false;
    std::vector<PackRecord> packs;
};
Status ReadStatus(std::filesystem::path const& root, std::string const& source);

struct InstallOptions {
    bool gameSettings = true;              // apply the VR game settings
    settings::Choices choices;             // with these
    std::filesystem::path setupExe;        // copied to ASI\MEVR-Setup.exe (for uninstall and "install after I quit")
    std::string source;                    // "Steam" writes steam_appid.txt for the Launch button
    bool ignoreVersion = false;            // install on an exe of another version (the mod stays off: fails closed)
};

struct Outcome {
    bool ok = false;
    std::vector<std::string> log;
    std::vector<std::string> errors;
};

Outcome Install(Layout const& layout, Payload const& payload, InstallOptions const& options);
// `keepUserData`: MELEVR.ini and LE1MEVR.ini (the renderer profiles, calibration and settings) stay for a reinstall.
// `runningExe`: this setup's own path; if it's the copy in the ASI folder it's moved aside and deleted after exit.
Outcome Uninstall(Layout const& layout, bool keepUserData, std::filesystem::path const& runningExe);
// Puts back any of our files that are missing or changed (from the payload), the loader if it was removed, and
// Halcyon's per-slot fixes.
Outcome Repair(Layout const& layout, Payload const& payload);
Outcome ApplySettings(Layout const& layout, settings::Choices const& choices);
// The game settings back to the player's own values (only those still as we set them).
Outcome RestoreSettings(Layout const& layout);
// Records an installed pack in the manifest.
Outcome RecordPack(Layout const& layout, std::string const& id, std::string const& name);

// The request the in-game menu writes ("Install after I quit"): %LOCALAPPDATA%\MEVR\setup.request.
std::filesystem::path RequestFile();

} // namespace mevr::install
