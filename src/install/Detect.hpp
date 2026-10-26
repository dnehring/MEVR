// SPDX-License-Identifier: MIT
#pragma once

#include "core/Version.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What the setup app finds on the machine (research/33): the game (Steam, EA app, or a folder the player picks), the
// exe's version, the bink2w64.dll proxy in front of the game, the active OpenXR runtime. The parsing is separate
// from the reading so it can be unit-tested.
namespace mevr::install::detect {

// ---- The game ---------------------------------------------------------------------------------------------------
inline constexpr char kSteamAppId[] = "1328670";

struct GameInstall {
    std::filesystem::path root;  // ...\Mass Effect Legendary Edition
    std::string source;          // "Steam", "EA app", "Folder"
};

// Steam's libraryfolders.vdf: every library's "path" (unescaped).
std::vector<std::filesystem::path> SteamLibraries(std::string_view libraryFoldersVdf);
// An appmanifest_<id>.acf's "installdir".
std::optional<std::string> AppManifestInstallDir(std::string_view acf);
// True if `root` holds Game\ME1\Binaries\Win64\MassEffect1.exe.
bool IsGameRoot(std::filesystem::path const& root);
// The game root for a folder the player picked: the folder itself, or the closest folder above or below it that is one
// (they may pick Game, Game\ME1, Binaries\Win64 or the exe's folder). nullopt if none.
std::optional<std::filesystem::path> RootFromPicked(std::filesystem::path const& picked);
// Every install found: Steam (its libraries, then the Steam uninstall key), the EA app / Origin (HKLM BioWare keys,
// "Install Dir", both registry views). Duplicates removed.
std::vector<GameInstall> FindInstalls();

// ---- The exe ----------------------------------------------------------------------------------------------------
std::optional<ModuleVersion> FileVersion(std::filesystem::path const& file);
// "2.0.0.48602" -> version; nullopt if it isn't four numbers.
std::optional<ModuleVersion> ParseVersion(std::string_view text);
enum class VersionCheck { Supported, Different, Missing };
VersionCheck CheckVersion(std::optional<ModuleVersion> const& version);

// ---- The ASI loader (a bink2w64.dll proxy) ------------------------------------------------------------------------
struct DllIdentity {
    bool exists = false;
    std::string company;
    std::string product;
    std::string description;
};
DllIdentity ReadIdentity(std::filesystem::path const& dll);

enum class BinkState {
    Missing,      // no bink2w64.dll: the game is broken (verify its files)
    Original,     // RAD's own DLL, no proxy: we install ours
    Ours,         // our loader (src/loader)
    LEBinkProxy,  // ME3Tweaks' proxy (installed by ME3Tweaks Mod Manager): we use it
    OtherProxy,   // something else in front of bink2w64_original.dll: we use it, with a warning
    Broken,       // not RAD's, and no bink2w64_original.dll behind it
};
BinkState ClassifyBink(DllIdentity const& bink, bool originalExists);
char const* BinkStateText(BinkState state);

// ---- Halcyon's upstream MELE-VR (a dxgi.dll proxy dropped into Binaries\Win64) -------------------------------------
// Its release puts dxgi.dll, openxr_loader.dll and MELEVR.ini next to the exe; its MELE-VR.bat tells its own dxgi.dll
// by the string "MELEVR" in it (findstr). With it and our host (which has Halcyon's renderer built in) two renderers
// would run, so Install moves its two DLLs aside (recorded, put back on uninstall). Its MELEVR.ini stays.
bool HasMelevrMarker(std::string_view bytes);
struct UpstreamHalcyon {
    bool dxgi = false;          // Win64\dxgi.dll is upstream's
    bool openxrLoader = false;  // Win64\openxr_loader.dll next to it (only counted with upstream's dxgi.dll)
    bool Found() const { return dxgi; }
};
UpstreamHalcyon FindUpstreamHalcyon(std::filesystem::path const& win64);

// ---- VR -----------------------------------------------------------------------------------------------------------
struct XrRuntime {
    std::string manifest;  // HKLM\SOFTWARE\Khronos\OpenXR\1 ActiveRuntime
    std::string name;      // the manifest's runtime.name, if it has one
    std::string kind;      // "SteamVR", "Meta", "Virtual Desktop", "Windows Mixed Reality", "Pimax", "Varjo", "Other", "" (none)
};
// The kind for a manifest path and runtime name (file names matched case-insensitively).
std::string ClassifyRuntime(std::string_view manifestPath, std::string_view runtimeName);
XrRuntime ActiveRuntime();

// The headset's own eye size as the last VR session saw it: Halcyon's log (%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt)
// writes "[XR] recommended eye image 2824x2880; ..." when a session starts. The last such line, or nullopt.
struct EyeSize {
    int width = 0;
    int height = 0;
    bool operator==(EyeSize const&) const = default;
};
std::optional<EyeSize> EyeSizeFromHalcyonLog(std::string_view log);
std::filesystem::path HalcyonLogPath();

// ---- The machine --------------------------------------------------------------------------------------------------
// A process with this exe name is running (e.g. L"MassEffect1.exe").
bool ProcessRunning(wchar_t const* exeName);
// The game or its launcher is running (files would be locked, and the game rewrites its ini files on exit).
bool GameRunning();
// We can create files in `dir` (else: run as administrator).
bool CanWrite(std::filesystem::path const& dir);
// The file has the Read-only attribute (a player or a tool set it on GamerSettings.ini to pin their values).
bool ReadOnlyFile(std::filesystem::path const& file);

std::string Utf8(std::wstring const& w);
std::wstring Wide(std::string_view s);

} // namespace mevr::install::detect
