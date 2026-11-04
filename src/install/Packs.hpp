// SPDX-License-Identifier: MIT
#pragma once

#include "install/Http.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Optional content for the setup app (research/33, "Texture packs"): third-party packs the player may add, never
// bundled. Each entry says what it is, how big, under which terms, where it officially lives and how it installs.
//
// The packs we list are on Nexus Mods, whose terms let only premium members' apps fetch files directly (everyone else
// downloads on the site), and whose authors forbid re-uploads. So a pack's entry opens its own page; the player
// downloads it there and points the setup at the file; we then install it with Mass Effect Modder's command-line
// tool (GPL, downloaded from its own GitHub releases after the player confirms). Texture installs are permanent:
// MEM rewrites the game's packages and marks them, content mods can't go on afterwards, and only a full restore
// (or reinstall) undoes it, which the page says before anything starts.
namespace mevr::install::packs {

enum class Method {
    PageThenMem,  // open the official page; the player downloads; we install the .mem files with MEM's CLI
    PageOnly,     // open the official page; the player installs it with the author's tool
};

struct Pack {
    char const* id;
    char const* name;
    char const* author;
    char const* description;
    char const* size;         // as the page gives it
    char const* licence;      // the terms in a line
    char const* url;          // the official page
    char const* fileHint;     // what the downloaded file is called (for the file picker and the Downloads scan)
    char const* requirements; // RAM / VRAM notes
    Method method;
    bool large;               // not preselected; a second warning
};

std::vector<Pack> const& Catalogue();
Pack const* Find(std::string_view id);

// ---- Mass Effect Modder (MassEffectModderNoGui, GPL-3.0; github.com/MassEffectModder/MassEffectModder) ------------
// Where the setup keeps it: %LOCALAPPDATA%\MEVR\tools\MassEffectModderNoGui.exe.
std::filesystem::path MemExe();
inline constexpr char kMemReleasesApi[] = "https://api.github.com/repos/MassEffectModder/MassEffectModder/releases/latest";
inline constexpr char kMemPage[] = "https://github.com/MassEffectModder/MassEffectModder/releases";

// The LE build's CLI asset from a GitHub "latest release" answer: name, URL and size of MassEffectModderNoGui-v*.7z
// for Windows (not the Linux / macOS builds), and its SHA-256 when GitHub lists one ("digest": "sha256:<hex>").
// Only a plain file name (it becomes a path) and a github.com download URL are accepted; `why` says what was missing.
struct Asset {
    std::string name;
    std::string url;
    std::uint64_t size = 0;
    std::string tag;
    std::string sha256;  // lower-case hex, or "" (not listed: not checked)
};
std::optional<Asset> MemAssetFrom(std::string_view releaseJson, std::string* why = nullptr);

// Asks GitHub for MEM's latest release (a network request; the test server under --test-server): the asset, or why
// not ("Couldn't reach GitHub: ...", the API quota, an answer without a Windows CLI build).
struct MemLookup {
    std::optional<Asset> asset;
    std::string error;
};
MemLookup FindMemRelease();
// Downloads `asset` into MEM's tools folder (its listed size and SHA-256 checked) and unpacks it (UnpackMem). "" or an
// error. The window's Download button (after the player saw the name and size) and the command line's --get-mem.
std::string DownloadMem(Asset const& asset, std::function<void(http::Progress const&)> const& progress, std::atomic<bool> const* cancel);

// One line of MEM's --ipc output ("[IPC]TASK_PROGRESS 42", "[IPC]STAGE_CONTEXT STAGE_SCAN", "[IPC]ERROR ...").
struct IpcLine {
    std::string command;  // "TASK_PROGRESS", "STAGE_CONTEXT", "ERROR", ... ("" if the line isn't an IPC line)
    std::string argument;
};
IpcLine ParseIpc(std::string_view line);
// A stage's name for the progress line: STAGE_SCAN -> "Scanning the game's textures".
std::string StageText(std::string_view stage);

// MEM's --install-mods input: a .mfl list, one .mem path per line.
std::string MflFor(std::vector<std::filesystem::path> const& memFiles);

// True if LE1's packages carry MEM's texture marker (Game\ME1\BioGame\CookedPCConsole\SFXTest.pcc ends with it):
// textures were installed by MEM (any pack). Content mods can't be added after that.
bool TexturesMarked(std::filesystem::path const& gameRoot);

// Recent files in the player's Downloads folder that look like `pack`'s download.
std::vector<std::filesystem::path> FindDownloads(Pack const& pack);

struct InstallStep {
    std::string stage;  // "Unpacking", "Scanning the game's textures", ...
    int percent = 0;
    std::string detail;
};
// Waits up to `timeoutMs` for the game in `gameRoot` (its MassEffect1.exe and launcher; another copy of the game doesn't
// count, detect::GameRunningIn) to exit, calling `waiting` once if it has to wait. False if it still runs, or `cancel`
// was set.
bool WaitForGameExit(std::filesystem::path const& gameRoot, unsigned timeoutMs, std::atomic<bool> const* cancel,
    std::function<void()> const& waiting);
// Installs a downloaded pack (refused while the game runs or is still exiting): unpacks the archive (MEM's
// --unpack-archive, into %LOCALAPPDATA%\MEVR\packs\<id>, emptied first so an earlier version's files never go along), or
// takes a .mem file as it is, writes the .mfl, points MEM at the game (--set-game-data-path) and runs --install-mods
// --gameid 1 --input <mfl> --verify --ipc, reporting progress. "" or an error. Tested with a stand-in MEM (research/33
// section 10); MEM's real arguments and output are [verify].
std::string InstallWithMem(Pack const& pack, std::filesystem::path const& download, std::filesystem::path const& gameRoot,
    std::function<void(InstallStep const&)> const& progress, std::atomic<bool> const* cancel);

// A program that unpacks MEM's .7z: Windows' own tar.exe (bsdtar: it reads 7z's LZMA only where it's built with liblzma,
// as Windows 11's is since 2023; Windows 10's isn't), else 7-Zip if it's installed.
struct Unpacker {
    std::string name;  // "tar.exe", "7-Zip"
    std::filesystem::path exe;
    bool sevenZip = false;
};
// Those found on this PC, in the order they're tried.
std::vector<Unpacker> Unpackers();
// Unpacks MEM's own .7z into its tools folder with the first of `tools` that manages (and finds the exe if the archive
// holds it in a subfolder). "" or an error naming what was tried.
std::string UnpackMem(std::filesystem::path const& archive);
std::string UnpackMem(std::filesystem::path const& archive, std::vector<Unpacker> const& tools);

// One argument for a Windows command line (CommandLineToArgvW's rules: a trailing backslash is doubled before the
// closing quote, so "D:\Games\" doesn't swallow it).
std::wstring QuoteArg(std::wstring_view arg);

} // namespace mevr::install::packs
