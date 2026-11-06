// SPDX-License-Identifier: MIT
#pragma once

#include "install/Detect.hpp"
#include "install/GameSettings.hpp"
#include "install/Manifest.hpp"
#include "install/Packs.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The setup app's steps (research/33), on a game root: Install, Uninstall, Repair, the VR game settings and their
// Restore, and what the Welcome page shows. Shared by the window (tools/setup) and its command line, which is how
// they're tested against a fake game folder.
namespace mevr::install {

// What the release ships next to MEVR-Setup.exe (tools/package-release.ps1): payload\LE1MEVR.asi (the monolithic
// build), payload\bink2w64.dll (our loader, src/loader), payload\fonts\*, payload\menu\* (the in-game menu's
// documents and the setup's own), and optionally payload\cockpit\ (the Mako's cockpit: BIOG_MEVR_Cockpit.pcc and cockpit.txt;
// research/47): the package goes in CookedPCConsole with its entry in PCConsoleTOC.bin, the script in ASI\cockpit; and
// optionally payload\voice\MEVR-Voice.dll (the voice orders' recognizer interface, research/54), which goes in ASI\voice with
// the mod; what it needs besides is optional content the setup downloads (install/Voice, research/33 section 14).
struct Payload {
    std::filesystem::path dir;
    bool Valid() const;
    std::filesystem::path Asi() const { return dir / L"LE1MEVR.asi"; }
    std::filesystem::path Loader() const { return dir / L"bink2w64.dll"; }
    std::filesystem::path Fonts() const { return dir / L"fonts"; }
    std::filesystem::path Menu() const { return dir / L"menu"; }
    std::filesystem::path CockpitPackage() const { return dir / L"cockpit" / L"BIOG_MEVR_Cockpit.pcc"; }
    std::filesystem::path CockpitScript() const { return dir / L"cockpit" / L"cockpit.txt"; }
    bool HasCockpit() const;
    std::filesystem::path VoiceDll() const { return dir / L"voice" / L"MEVR-Voice.dll"; }
    bool HasVoice() const;
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
    // Game\Launcher's bink2w64.dll: our loader or LEBinkProxy there boots Mass Effect 1 for Launch (-game 1).
    detect::BinkState launcherBink = detect::BinkState::Missing;
    bool installed = false;      // the mod is: the manifest has a mod version or the ASI (not only settings or a pack)
    bool hasRecord = false;      // a manifest exists (perhaps only for the game settings or a pack)
    std::string installedVersion;
    std::string loader;          // the manifest's
    bool gameSettings = false;
    bool texturesMarked = false;
    bool gameRunning = false;
    bool canWrite = true;
    bool hasGamerSettings = false;  // the game has been started here (it makes GamerSettings.ini on its first start)
    bool gamerSettingsReadOnly = false;
    // The VR game settings chosen while GamerSettings.ini didn't exist yet: applied once it does (Repair, "Start the game
    // once", Settings > Apply).
    std::optional<PendingSettings> pendingSettings;
    bool eyeSizeAtFirstStart = false;  // the manifest's: the mod sets the headset's eye size at the first VR start
    // Installed files Repair would put back (missing or changed: the loader or the cockpit's TOC entry after Steam's
    // "verify files", a deleted menu file...); a loader another tool put in place of ours isn't one.
    std::vector<std::string> needsRepair;
    detect::UpstreamHalcyon upstream;  // Halcyon's own MELE-VR in Binaries\Win64
    std::vector<PackRecord> packs;
};
Status ReadStatus(std::filesystem::path const& root, std::string const& source);

PendingSettings PendingFromChoices(settings::Choices const& choices);
settings::Choices ChoicesFromPending(PendingSettings const& pending);

struct InstallOptions {
    bool gameSettings = true;              // apply the VR game settings
    settings::Choices choices;             // with these
    std::filesystem::path setupExe;        // copied to ASI\MEVR-Setup.exe (for uninstall and "install after I quit")
    std::string source;                    // "Steam" writes steam_appid.txt for the Launch button
    bool ignoreVersion = false;            // install on an exe of another version (the mod stays off: fails closed)
    // Halcyon's upstream MELE-VR found in Binaries\Win64: move its dxgi.dll and openxr_loader.dll aside (recorded as
    // renamed to *.mevr-off, put back on uninstall; its MELEVR.ini stays). False: refuse to install while it's there.
    bool moveUpstreamAside = true;
};

struct Outcome {
    bool ok = false;
    std::vector<std::string> log;
    std::vector<std::string> errors;
};

Outcome Install(Layout const& layout, Payload const& payload, InstallOptions const& options);
// `keepUserData`: MELEVR.ini, LE1MEVR.ini and the mod's logs (the renderer profiles, calibration and settings) stay for
// a reinstall; otherwise they're deleted. `runningExe`: this setup's own path; if it's the copy in the ASI folder it's
// moved aside and deleted after exit (only when the uninstall succeeded). `removeLocalData`: also delete LocalDataDir()
// (snapshots, recordings, frame-timing captures, crash reports, the downloaded Mass Effect Modder).
Outcome Uninstall(Layout const& layout, bool keepUserData, std::filesystem::path const& runningExe, bool removeLocalData = false);
// Puts back any of our files that are missing or changed (from the payload), the loader if it was removed (RAD's DLL put
// back by Steam's "verify files": renamed behind ours again), the cockpit's package and its PCConsoleTOC.bin entry,
// Halcyon's per-slot fixes, and the VR game settings still waiting for GamerSettings.ini once the game has made it.
Outcome Repair(Layout const& layout, Payload const& payload);
Outcome ApplySettings(Layout const& layout, settings::Choices const& choices);
// The game settings back to the player's own values (only those still as we set them).
Outcome RestoreSettings(Layout const& layout);
// Records an installed pack in the manifest.
Outcome RecordPack(Layout const& layout, std::string const& id, std::string const& name);
// A texture pack (packs::Catalogue's `packId`) from the player's `download` through Mass Effect Modder
// (packs::InstallWithMem), then recorded in the manifest, then the VR game settings this install wrote set again where
// MEM changed them (Transaction::ReassertIni: exactly the recorded values, never the Settings page's current choices).
// The window's Optional content page and the command line's --install-pack (research/33 section 10).
Outcome InstallPack(Layout const& layout, std::string const& packId, std::filesystem::path const& download,
    std::function<void(packs::InstallStep const&)> const& progress, std::atomic<bool> const* cancel);

// The request the in-game menu writes ("Install after I quit"): %LOCALAPPDATA%\MEVR\setup.request.
std::filesystem::path RequestFile();
// What it holds (plugin/SetupRequest writes {"page": "content", "packs": [], "pid": <the game's>}); the game then starts
// ASI\MEVR-Setup.exe --request --wait-pid <pid> as it exits. `packs` keeps only ids the catalogue knows.
struct SetupRequest {
    std::string page = "content";
    std::vector<std::string> packs;  // texture pack ids, or "voice" (install/Voice)
    unsigned long pid = 0;
};
// nullopt for text that isn't a request (the setup then opens on Optional content anyway).
std::optional<SetupRequest> ParseSetupRequest(std::string_view text);
// %LOCALAPPDATA%\MEVR: the mod's diagnostics and the setup's downloads (never in the game folder).
std::filesystem::path LocalDataDir();
// The mod's run-time files in the game folder (recorded at install as user data, deleted by a full uninstall).
std::vector<std::string> RuntimeFiles();

} // namespace mevr::install
