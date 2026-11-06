// SPDX-License-Identifier: MIT
#pragma once

#include "install/LaunchArgs.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

// Starting LE1 without the Legendary Edition launcher's game picker (research/33, "Launching"). The launcher only
// starts Game\ME1\Binaries\Win64\MassEffect1.exe with a few arguments made from the player's launcher settings
// (Documents\BioWare\Mass Effect Legendary Edition\LauncherConfig.cfg: language, English voices, subtitle size);
// ME3Tweaks' launcher autoboot and Mod Manager start the same exe the same way. So does our Launch button, with
// Steam's app id in the environment and steam_appid.txt next to the exe (as Mod Manager does for Steam installs).
namespace mevr::install::launch {

struct Result {
    bool started = false;
    unsigned long pid = 0;
    std::string message;
};

// Starts LE1. The Steam build (`steam`) goes through Steam (started first if it isn't running, up to 60 s for a
// logged-in user): `steam.exe -applaunch 1328670 -game 1 -autoterminate` when the launcher can boot the game itself (our
// loader in Game\Launcher, or ME3Tweaks' LEBinkProxy: LauncherBoots), else without them (the launcher's picker shows).
// Started directly, the Steam build's exe hands its start back to Steam with its arguments, and Steam then waits for
// the player to confirm them (research/33 section 11). Other builds (the EA app's) start MassEffect1.exe directly with
// the launcher's arguments [verify].
Result StartLe1(std::filesystem::path const& gameRoot, bool steam);
// Whether the launcher at `gameRoot`\Game\Launcher boots a game itself when given -game N: our loader or LEBinkProxy.
bool LauncherBoots(std::filesystem::path const& gameRoot);
// The fallback: the game through Steam (its launcher's picker shows).
bool StartThroughSteam();

// ---- The game's first start (research/33 section 9) --------------------------------------------------------------
// LE1 makes Game\ME1\BioGame\Config\GamerSettings.ini on its first start, and the VR game settings (a borderless
// window at the stereo target) can only go into that file. "Start the game once" starts the game, waits for the file,
// waits for the player to quit (the game rewrites the file when it quits), then applies them. The decision for each
// poll is pure, here; the polling is the setup's.
namespace first_start {
inline constexpr double kStartGraceS = 90.0;        // the game may take this long to show up (Steam, the EA app, its DRM)
inline constexpr double kGoneGraceS = 20.0;         // seen, then gone this long without the file: it closed (a restart through the EA app is shorter)
inline constexpr double kFileTimeoutS = 15 * 60.0;  // running, no file yet
inline constexpr double kQuitTimeoutS = 45 * 60.0;  // the file is there, the game still runs
enum class Phase {
    Starting,        // launched, not seen running yet
    WaitingForFile,  // running (or briefly gone), no GamerSettings.ini yet
    WaitingForQuit,  // the file is there; the player quits the game
    Apply,           // the file is there and the game has exited: apply the settings
    Closed,          // the game went (or never came) without making the file
    TimedOut,
};
struct Observation {
    double sinceLaunchS = 0.0;
    bool gameRunning = false;
    bool fileExists = false;
    double sinceSeenS = -1.0;  // since the game was last seen running; < 0: not seen yet
};
Phase Next(Observation const& o);
bool Finished(Phase phase);  // Apply, Closed, TimedOut
// What the setup shows while it waits, or says at the end.
char const* PhaseText(Phase phase);
} // namespace first_start

// ---- The VR runtime before the game (research/33 section 9) ------------------------------------------------------
// Halcyon's renderer looks for the headset once, when the game starts: a runtime that isn't up then leaves VR off for the
// whole run. For SteamVR, Launch starts it first and waits (bounded); for the others it says what to start.
namespace runtime_start {
inline constexpr double kSteamVrWaitS = 60.0;
inline constexpr char kSteamVrAppId[] = "250820";
enum class Action {
    None,          // no runtime: nothing to start (the Welcome page says so)
    StartSteamVr,  // start SteamVR and wait for it
    AskPlayer,     // the player starts it (the Meta app, Virtual Desktop, ...): `advice` says what
};
struct Plan {
    Action action = Action::None;
    std::filesystem::path exe;  // StartSteamVr: SteamVR's vrstartup.exe when the manifest's folder has it; else the steam:// URL is used
    std::string advice;         // one sentence for the Launch page
};
// For the active OpenXR runtime (detect::XrRuntime's kind and manifest path).
Plan PlanFor(std::string_view kind, std::string_view manifestPath);
// SteamVR's starter for its OpenXR manifest: ...\SteamVR\steamxr_win64.json -> ...\SteamVR\bin\win64\vrstartup.exe
// (not checked on disk). Empty for an empty path.
std::filesystem::path SteamVrStartup(std::string_view manifestPath);
// SteamVR counts as up once its server and compositor run.
bool SteamVrUp(bool serverRunning, bool compositorRunning);
enum class Gate {
    Wait,        // not up yet: poll again
    Go,          // up: start the game
    GoTimedOut,  // not up after the wait: start the game anyway, with a note
};
Gate Step(bool up, double waitedS, double timeoutS = kSteamVrWaitS);

// Live: vrserver.exe and vrcompositor.exe.
bool SteamVrRunning();
// Starts SteamVR: the plan's vrstartup.exe if it exists, else steam://rungameid/250820. False if neither could start.
bool StartSteamVr(Plan const& plan);
} // namespace runtime_start

// The Launch button's whole start, shared by the window and `MEVR-Setup.exe --launch`: SteamVR first when it is the active
// runtime and not running yet (Halcyon's renderer looks for the headset once, when the game starts), waiting for it at most
// runtime_start::kSteamVrWaitS (the game starts anyway after that, or when `cancelled` says so), then StartLe1.
struct LaunchHooks {
    std::function<void(std::string const& step, int percent)> progress;  // optional
    std::function<void(std::string const& line, bool warning)> post;     // optional
    std::function<bool()> cancelled;                                     // optional: the wait for SteamVR cut short
};
Result StartLe1WithVr(std::filesystem::path const& gameRoot, bool steam, std::string_view runtimeKind, std::string_view runtimeManifest,
    LaunchHooks const& hooks);

} // namespace mevr::install::launch
