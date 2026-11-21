// SPDX-License-Identifier: MIT
#pragma once

#include "install/Installer.hpp"
#include "install/Manifest.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The rigged bodies and hair (docs/trilogy-hair-plan.md sections 7 and 12, research/33 section 18): BioWare's meshes with our
// soft-tissue bones and hair chains, so they are built on the player's PC from the player's own game files and never shipped.
// The release carries the builder (payload\rig: mevr-rig.exe, self-contained, and its plans; tools/rig/README.md "The players'
// build"); MEVR Setup runs `mevr-rig bodies` for a game at below-normal priority into %LOCALAPPDATA%\MEVR\rig\<GAME>.build, its
// output logged in %LOCALAPPDATA%\MEVR\rig\build-<GAME>.log, then puts each package it made (MEVR_Bodies_<GAME>.pcc, ME2's and
// ME3's MEVR_Miranda.pcc) in the game's BioGame\CookedPCConsole with a PCConsoleTOC.bin entry (else the game never loads it),
// recorded in that game's MEVR-install.json as the content "bodies/<file>", and moves the package index and the body spheres
// (what game/Hair reads) into %LOCALAPPDATA%\MEVR\rig\<GAME>. Uninstall takes the packages and the TOC entries out like any
// other file; the folder in %LOCALAPPDATA% goes only with the "%LOCALAPPDATA%\MEVR" box. A build that fails never fails the
// install: the mod runs without rigged bodies (game/Hair finds no index) and says so.
namespace mevr::install::bodies {

inline constexpr char kContent[] = "bodies";
inline constexpr char kName[] = "Moving bodies and hair";
inline constexpr char kResultFile[] = "bodies.json";  // the builder's result (Bodies.cs WriteResult)
inline constexpr char kRecipeFile[] = "setup.json";   // in rig\<GAME>: which builder and plans made the packages there

// Where the game finds a package (relative to the game root), and its name in PCConsoleTOC.bin (from the game's folder).
std::string CookedFile(GameId game, std::string_view file);
std::string TocName(std::string_view file);  // "BioGame\CookedPCConsole\<file>"

// %LOCALAPPDATA%\MEVR\rig, its <GAME> folder (what game/Hair reads), the build's folder and its log.
std::filesystem::path RigDir();
std::filesystem::path GameDir(GameId game);
std::filesystem::path StageDir(GameId game);
std::filesystem::path LogFile(GameId game);

// ---- The builder's output, parsed (unit-tested) ---------------------------------------------------------------------
struct Package {
    std::string file;  // "MEVR_Bodies_LE1.pcc": a bare file name, nothing else is accepted
    std::uint64_t size = 0;
};
struct Result {
    std::string game;
    std::vector<Package> packages;
    int bodies = 0;
    int failed = 0;
    int seconds = 0;
};
// bodies.json (`mevr-rig-bodies` version 1, for `game`); nullopt for anything else, a package name that isn't a bare MEVR_*.pcc
// name, or one listed twice.
std::optional<Result> ParseResult(std::string_view json, std::string_view game);
// "progress <percent> <text>" -> (percent 0..100, text); nullopt for any other line.
struct ProgressLine {
    int percent = 0;
    std::string text;
};
std::optional<ProgressLine> ParseProgress(std::string_view line);
// The builder's command line (the exe quoted, every path quoted).
std::wstring CommandLine(std::filesystem::path const& exe, GameId game, std::filesystem::path const& gameRoot,
    std::filesystem::path const& out, std::filesystem::path const& reference);
// A hash of the builder and its plans (payload\rig: mevr-rig.dll or .exe, plans\*): the same recipe makes the same packages, so
// an install over one that has them, built from it, doesn't build again. "" without the builder.
std::string Recipe(std::filesystem::path const& rigDir);

// ---- What's installed --------------------------------------------------------------------------------------------------
struct State {
    std::vector<std::string> packages;  // the files on record (bare names)
    std::vector<std::string> damaged;   // recorded packages missing or another size, or without their TOC entry
    bool indexThere = false;            // rig\<GAME>\MEVR_Bodies_<GAME>.json, which game/Hair reads
    std::string recipe;                 // rig\<GAME>\setup.json's
    int bodies = 0;                     // rig\<GAME>\bodies.json's count
    bool Built() const { return !packages.empty(); }
};
State ReadState(Layout const& layout);

struct Step {
    std::string text;
    int percent = 0;
};
using ProgressFn = std::function<void(Step const&)>;

// The work on an open install record (Install, Repair, the Optional content page's Build): builds the game's packages and puts
// them in place, replacing what an earlier build put there (a package the new build doesn't make taken out with its TOC entry).
// `force`: build even when the recorded packages are there and came from this payload's recipe. Errors go through the
// transaction; the callers that must not fail on them demote them (Transaction::DemoteErrors). False when nothing was built.
bool BuildIn(Transaction& tx, Payload const& payload, bool force, ProgressFn const& progress, std::atomic<bool> const* cancel);
// After Steam's or the EA app's "verify files" (the game's own TOC put back): each recorded package that's there gets its TOC
// entry again; one missing or changed is built again from `payload` (when it has the builder).
bool RepairIn(Transaction& tx, std::optional<Payload> const& payload, ProgressFn const& progress, std::atomic<bool> const* cancel);
// Takes every package and its TOC entry out of the game (rig\<GAME> stays: %LOCALAPPDATA%'s, with the uninstall's box).
bool RemoveIn(Transaction& tx);

// The same as whole steps (MEVR Setup's Optional content page and --build-bodies / --remove-bodies): refused while the game runs.
Outcome Build(Layout const& layout, Payload const& payload, ProgressFn const& progress, std::atomic<bool> const* cancel);
Outcome Remove(Layout const& layout);

} // namespace mevr::install::bodies
