// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// "Report a problem" (research/33 section 8): a zip on the Desktop with the mod's logs and settings, an optional
// snapshot, a listing of the game's Win64 and ASI folders, and REPORT.md first, written for a person and for the AI
// assistant they paste it into: what MEVR is, the system, what went wrong (the last session's errors and warnings,
// deduplicated), the crash if there was one, the settings that differ from the defaults, the files, and a prompt.
// Every text file is redacted first (the Windows user name, the profile folder, the computer name, e-mail addresses).
// Nothing is ever sent: the player reads the zip and decides. The parsing and assembly are pure and unit-tested; the
// reading, the system queries and the zip are Collect / Write.
namespace mevr::install::report {

inline constexpr char kRepoUrl[] = "https://github.com/dnehring/MEVR";
inline constexpr char kIssuesUrl[] = "https://github.com/dnehring/MEVR/issues";
inline constexpr char kNewIssueUrl[] = "https://github.com/dnehring/MEVR/issues/new";
inline constexpr char kTroubleshootingUrl[] = "https://github.com/dnehring/MEVR/blob/main/docs/troubleshooting.md";
// REPORT.md stays under this, so it pastes into any assistant.
inline constexpr std::size_t kReportMdBudget = 30 * 1024;

// ---- Redaction ----------------------------------------------------------------------------------------------------
struct Redactions {
    int userName = 0;
    int computerName = 0;
    int profilePath = 0;
    int email = 0;
    int Total() const { return userName + computerName + profilePath + email; }
    Redactions& operator+=(Redactions const& o);
};

class Redactor {
public:
    Redactor() = default;
    // `profileDirs`: the profile folder (C:\Users\name); under a "Users" folder its last part becomes <user>, anywhere
    // else all of it becomes <profile>. `userNames` (the account name, the profile folder's name) and `computerNames`
    // are replaced wherever they stand as a word (3 characters or more; ASCII letters case-insensitively).
    Redactor(std::vector<std::string> userNames, std::vector<std::string> computerNames, std::vector<std::string> profileDirs);
    // This machine's: %USERNAME%, the profile folder (%USERPROFILE%), %COMPUTERNAME% and the DNS host name.
    static Redactor ForThisMachine();

    std::string Apply(std::string_view text, Redactions* counts = nullptr) const;

private:
    std::vector<std::string> users_;
    std::vector<std::string> computers_;
    std::vector<std::pair<std::string, std::string>> paths_;  // a profile path's spellings -> what replaces it
};

// ---- What went wrong ----------------------------------------------------------------------------------------------
struct Problem {
    std::string level;  // "error", "warn"
    std::string time;   // the first one's
    std::string text;   // the first one's, without time and level
    int count = 0;
};
struct Problems {
    std::vector<Problem> items;  // errors first, then warnings; each by count, then first seen
    int errors = 0;              // every line
    int warnings = 0;
    int distinct = 0;            // before the cap
    bool capped = false;
};
// The mod's log (LE1MEVR.log: "[hh:mm:ss.mmm] [level] text"): every [error] and [warn] line, merged where they differ
// only in numbers (addresses, thread ids, counts; a module's +0x offset is kept), at most `maxPerLevel` of each.
Problems ExtractProblems(std::string_view modLog, std::size_t maxPerLevel = 25);
// Halcyon's log ("2026-10-26 01:43:46.166 | [TAG] text"): lines that say "error", "failed" or "exception" as a word.
Problems ExtractRendererProblems(std::string_view halcyonLog, std::size_t max = 10);
// The key two lines merge under (numbers replaced).
std::string ProblemKey(std::string_view text);

// The last session in Halcyon's log (it appends forever): from its last "=====" start banner, or all of `log`.
std::string_view LastHalcyonSession(std::string_view log);

struct LogFacts {
    std::string modVersion;          // "0.1.0"
    std::string modBuild;            // "hot reload host, commit 1a0622c"
    std::string commit;              // "1a0622c" when the build says
    std::string gameVersion;         // "2.0.0.48602 OK"
    std::string xrRuntime;           // "SteamVR/OpenXR"
    std::string headsetSystem;       // "SteamVR/OpenXR : playstation_vr2"
    std::string interactionProfile;  // "left /interaction_profiles/..., right ..."
    std::string colourProfile;       // "auto (= neutral) for 'SteamVR/OpenXR : null' [Colour.steamvr_openxr_null]"
    std::string eyeSize;             // the headset's recommended eye image, "2824x2880"
    std::string renderSize;          // what the game renders per eye, "2118x2160"
};
LogFacts ReadLogFacts(std::string_view modLog, std::string_view halcyonLog);

// ---- The crash ----------------------------------------------------------------------------------------------------
struct Crash {
    bool present = false;
    bool stale = false;                     // older than the last two sessions (Collect: by the files' times)
    std::string when;                      // "2026-10-26 00:11:26.458"
    std::string exception;                  // "0xC0000005 (access violation) at ntdll.dll+0x1A12D, writing 0x24"
    std::string location;                   // "ntdll.dll+0x1A12D"
    std::string thread;                     // "faulting thread: tid 8732; the game thread is 107332"
    std::vector<std::string> nativeFrames;  // "ntdll.dll+0x1A12D", innermost first
    std::vector<std::string> scriptFrames;
};
// crash-breadcrumbs.txt (plugin/CrashBreadcrumbs, research/36).
Crash ParseCrash(std::string_view breadcrumbs, std::size_t maxFrames = 12);

// ---- Settings that differ from the defaults ----------------------------------------------------------------------
struct SettingDiff {
    std::string file;     // "LE1MEVR.ini", "MELEVR.ini"
    std::string section;
    std::string key;
    std::string value;    // "(missing)" when the template has it and the file doesn't
    std::string defaultValue;
};
struct IniDefault {
    char const* section;
    char const* key;
    char const* value;
};
// Our LE1MEVR.ini keys the menu and the docs name, with the code's defaults.
std::vector<IniDefault> const& ModIniDefaults();
// The keys of ModIniDefaults() the file sets to something else (numbers compared as numbers).
std::vector<SettingDiff> DiffModIni(std::string_view text);
// MELEVR.ini's active profile against the first profile of `templateText` (the release file the setup installs,
// ini::HalcyonReleaseIni): only the template's keys.
std::vector<SettingDiff> DiffHalcyonIni(std::string_view text, std::string_view templateText);

// ---- The system ---------------------------------------------------------------------------------------------------
struct System {
    std::string setupVersion;      // this program's, with its commit
    std::string installedVersion;  // MEVR-install.json's modVersion
    std::string runningMod;        // the log's: "0.1.0 (monolithic, commit 1a0622c)"
    std::string gameVersion;       // MassEffect1.exe's file version
    std::string store;             // "Steam", "EA app", ...
    std::string windows;           // "Windows 11 Home 24H2 (build 26100.2033)"
    std::string gpu;               // "NVIDIA GeForce RTX 4090 (24 GB)"
    std::string gpuDriver;         // "32.0.15.6094 (NVIDIA 560.94)"
    std::string xrRuntime;         // the active runtime: "SteamVR (SteamVR/OpenXR)"
    std::string headset;           // the log's system name
    std::string controllers;       // the log's interaction profile
    std::string colourProfile;
    std::string resolution;        // "headset 2824x2880 per eye; rendering 2118x2160"
};

// ---- The bundle ---------------------------------------------------------------------------------------------------
struct File {
    std::string name;  // in the zip: "logs/LE1MEVR.log"
    std::string bytes;
    std::uint64_t originalSize = 0;
    std::string note;  // "the last 3.0 MB of 191 MB", "locked by the running game"
    bool text = true;
    int redactions = 0;
};

struct Bundle {
    std::vector<File> files;  // REPORT.md is added by Write, first
    Redactions redactions;
    System system;
    LogFacts facts;
    Problems problems;
    Problems previousProblems;  // the session before (LE1MEVR.prev.log), fewer
    Problems rendererProblems;
    Crash crash;
    std::vector<SettingDiff> diffs;
    std::vector<std::string> missing;  // files looked for and not there
    std::string reportMd;
    bool snapshot = false;
};

// Pure: REPORT.md for a bundle (kept under kReportMdBudget).
std::string BuildReportMd(Bundle const& bundle);
// "Crash: 0xC0000005 ... at ntdll.dll+0x1A12D", the first error, the first warning, or a line saying there were none.
std::string Headline(Bundle const& bundle);
// The non-personal summary (versions, Windows, GPU, runtime, headset, controllers, the headline): for the issue form.
std::string IssueSummary(Bundle const& bundle);
// https://github.com/dnehring/MEVR/issues/new?template=bug.yml&title=...&version=...&summary=... (under 8 KB).
std::string IssueUrl(Bundle const& bundle);
std::string UrlEncode(std::string_view text);

// ---- Reading the machine, writing the zip ------------------------------------------------------------------------
struct Options {
    std::filesystem::path gameRoot;    // ...\Mass Effect Legendary Edition (empty: only the local files)
    std::filesystem::path localData;   // %LOCALAPPDATA%\MEVR (empty: the real one)
    std::filesystem::path halcyonLog;  // empty: %LOCALAPPDATA%\MELEVR\MELEVR_Log.txt
    std::filesystem::path outputDir;   // empty: the Desktop, else Documents
    std::string store;                 // "Steam", "EA app" (empty: worked out from the folder)
    bool snapshot = false;             // the latest snapshot's JSON and eye PNGs
    bool perf = false;                 // the newest frame-timing capture (perf\<stamp>-probe.json, -frames.csv, -ticks.csv)
    std::int64_t now = 0;              // the zip's name and times (0: now)
};

Bundle Collect(Options const& options, Redactor const& redactor);
// The redactor for this machine (ForThisMachine).
Bundle Collect(Options const& options);

struct Result {
    bool ok = false;
    std::filesystem::path zip;
    std::string folder;  // "Desktop", "Documents" or the folder's path
    std::string error;
    std::size_t bytes = 0;
};
// Adds REPORT.md (first) and writes the zip; touches report.last and writes report.result.
Result Write(Bundle& bundle, Options const& options);
// Collect + Write.
Result CreateReport(Options const& options);

// "Steam" / "EA app" / "" for a game root.
std::string StoreFor(std::filesystem::path const& gameRoot);
// "1a0622c" (+"-dirty"), baked in at build time; "unknown" without git.
char const* BuildCommit();
// "12.3 KB".
std::string SizeText(std::uint64_t bytes);

} // namespace mevr::install::report
