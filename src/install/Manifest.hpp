#pragma once

#include "core/Json.hpp"
#include "install/Ini.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What the setup app changed in a game folder (research/33): MEVR-install.json next to the ASI, with a backup of
// every file it replaced, so Uninstall puts the game back exactly. Paths are relative to the game root (the
// "Mass Effect Legendary Edition" folder), with forward slashes.
//
//   added    a file that wasn't there: deleted on uninstall
//   replaced a file that was there: its original is in the backup folder, put back on uninstall
//   renamed  `from` was renamed to `path` (the game's bink2w64.dll to bink2w64_original.dll): renamed back
//   edited   an ini file's keys (ini::Edit): reverted key by key, only where the player hasn't changed them since
//
// `userData` files (Halcyon's MELEVR.ini, the mod's LE1MEVR.ini) hold the player's settings: uninstall keeps them
// unless asked to remove everything.
namespace mevr::install {

struct FileEntry {
    std::string path;
    std::string action;  // "added", "replaced", "renamed", "edited"
    std::string backup;  // replaced, renamed (an occupant moved aside): relative to the backup folder
    std::string from;    // renamed
    std::string sha256;  // added, replaced: what we wrote (Repair compares against it)
    bool userData = false;
    std::vector<ini::Edit> edits;  // edited
};

struct PackRecord {
    std::string id;
    std::string name;
    std::string method;       // "mem"
    std::string installedAt;  // ISO 8601, local time
};

struct Manifest {
    int schema = 1;
    std::string game = "LE1";
    std::string modVersion;
    std::string setupVersion;
    std::string installedAt;
    std::string loader;  // "mevr" (ours), "lebinkproxy", "other": which bink2w64.dll proxy loads the ASI
    std::vector<FileEntry> files;
    std::vector<std::string> dirs;  // folders we created (removed on uninstall when empty), in creation order
    std::vector<PackRecord> packs;
    bool gameSettings = false;  // the VR game settings are applied

    FileEntry* Find(std::string_view path);
    FileEntry const* Find(std::string_view path) const;
    json::Value ToJson() const;
    static std::optional<Manifest> FromJson(json::Value const& value);
};

// Where things live for a game root.
struct Layout {
    std::filesystem::path root;  // ...\Mass Effect Legendary Edition
    std::filesystem::path Win64() const { return root / L"Game" / L"ME1" / L"Binaries" / L"Win64"; }
    std::filesystem::path Asi() const { return Win64() / L"ASI"; }
    std::filesystem::path Config() const { return root / L"Game" / L"ME1" / L"BioGame" / L"Config"; }
    std::filesystem::path ManifestFile() const { return Asi() / L"MEVR-install.json"; }
    std::filesystem::path BackupDir() const { return Asi() / L"MEVR-backup"; }
    // "Game/ME1/Binaries/Win64/ASI/LE1MEVR.asi" for a path under root.
    std::string Relative(std::filesystem::path const& full) const;
    std::filesystem::path Full(std::string_view relative) const;
};

// Relative paths every install uses.
namespace paths {
inline constexpr char kWin64[] = "Game/ME1/Binaries/Win64";
inline constexpr char kAsi[] = "Game/ME1/Binaries/Win64/ASI";
inline constexpr char kBink[] = "Game/ME1/Binaries/Win64/bink2w64.dll";
inline constexpr char kBinkOriginal[] = "Game/ME1/Binaries/Win64/bink2w64_original.dll";
inline constexpr char kModAsi[] = "Game/ME1/Binaries/Win64/ASI/LE1MEVR.asi";
inline constexpr char kModIni[] = "Game/ME1/Binaries/Win64/ASI/LE1MEVR.ini";
inline constexpr char kHalcyonIni[] = "Game/ME1/Binaries/Win64/ASI/MELEVR.ini";
inline constexpr char kSetupExe[] = "Game/ME1/Binaries/Win64/ASI/MEVR-Setup.exe";
inline constexpr char kGamerSettings[] = "Game/ME1/BioGame/Config/GamerSettings.ini";
inline constexpr char kEngineIni[] = "Game/ME1/BioGame/Config/BIOEngine.ini";
inline constexpr char kSteamAppId[] = "Game/ME1/Binaries/Win64/steam_appid.txt";
inline constexpr char kExe[] = "Game/ME1/Binaries/Win64/MassEffect1.exe";
} // namespace paths

// SHA-256 of a file as lower-case hex; "" if it can't be read.
std::string Sha256File(std::filesystem::path const& file);
std::string Sha256(std::string_view bytes);

std::optional<std::string> ReadFile(std::filesystem::path const& file);
// Through a temporary file and a rename, so a reader never sees half of it.
bool WriteFileAtomic(std::filesystem::path const& file, std::string_view bytes);

// The current local time as "2026-10-23T14:05:00".
std::string NowIso();

// One install, uninstall or repair: every change goes through here and is recorded in the manifest (saved after each
// step, so an interrupted run can still be undone). Errors are collected, never thrown.
class Transaction {
public:
    explicit Transaction(Layout layout);

    // Loads the existing manifest (if any). False only if one exists and can't be parsed.
    bool Load();
    bool Save();
    Manifest& Data() { return manifest_; }
    Manifest const& Data() const { return manifest_; }
    bool Exists() const { return exists_; }
    Layout const& Where() const { return layout_; }

    // Copies `source` to `relative`. A file we didn't put there is backed up first (replaced); one we did is overwritten.
    bool CopyIn(std::filesystem::path const& source, std::string_view relative, bool userData = false);
    // Writes `bytes` to `relative`, the same way.
    bool WriteIn(std::string_view relative, std::string_view bytes, bool userData = false);
    // Writes `bytes` only if `relative` doesn't exist (Halcyon's MELEVR.ini: never overwritten). True if it exists now.
    bool WriteIfMissing(std::string_view relative, std::string_view bytes, bool userData = false);
    // Renames `from` to `to` (both relative). An occupant of `to` is moved to the backup folder first.
    bool Rename(std::string_view from, std::string_view to);
    // Edits an ini file in place; the edits are recorded (unless we added the file ourselves). False if it's missing.
    bool EditIni(std::string_view relative, std::function<std::string(std::string_view, std::vector<ini::Edit>*)> const& edit);
    // Reverts the recorded edits of one ini file (Restore for the game settings) and forgets them.
    bool RevertIni(std::string_view relative);

    // Undoes everything recorded, newest first; then removes the backup folder, the folders we created (if empty) and
    // the manifest. `keepUserData`: files marked userData stay.
    bool Uninstall(bool keepUserData);

    // Files whose content isn't what we wrote (missing or changed), for Repair.
    std::vector<std::string> Verify() const;

    std::vector<std::string> const& Errors() const { return errors_; }
    std::vector<std::string> const& Log() const { return log_; }

private:
    bool EnsureParent(std::filesystem::path const& file);
    bool Backup(std::string_view relative, std::string& backupRelative);
    void Fail(std::string message);
    void Note(std::string message);

    Layout layout_;
    Manifest manifest_;
    bool exists_ = false;
    std::vector<std::string> errors_;
    std::vector<std::string> log_;
};

} // namespace mevr::install
