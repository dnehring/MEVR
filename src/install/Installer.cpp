// SPDX-License-Identifier: MIT
#include "install/Installer.hpp"

#include "install/Ini.hpp"
#include "install/Packs.hpp"
#include "install/Updates.hpp"

#include <Windows.h>

#include <algorithm>
#include <functional>
#include <system_error>

namespace mevr::install {
namespace {

namespace fs = std::filesystem;

Outcome Finish(Transaction const& tx, bool ok) {
    Outcome o;
    o.ok = ok && tx.Errors().empty();
    o.log = tx.Log();
    o.errors = tx.Errors();
    return o;
}

Outcome Error(std::string message) {
    Outcome o;
    o.errors.push_back(message);
    o.log.push_back("error: " + message);
    return o;
}

// Copies every file under `from` to `relativeDir` (recursively).
bool CopyTree(Transaction& tx, fs::path const& from, std::string const& relativeDir) {
    std::error_code ec;
    bool ok = true;
    std::vector<fs::path> files;
    for (auto const& entry : fs::recursive_directory_iterator(from, ec)) {
        if (entry.is_regular_file(ec)) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (fs::path const& file : files) {
        std::string const rel = detect::Utf8(file.lexically_relative(from).generic_wstring());
        ok = tx.CopyIn(file, relativeDir + "/" + rel) && ok;
    }
    return ok;
}

constexpr char kLogsDir[] = "Game/ME1/Binaries/Win64/Logs";
constexpr char kDxgi[] = "Game/ME1/Binaries/Win64/dxgi.dll";
constexpr char kOpenXrLoader[] = "Game/ME1/Binaries/Win64/openxr_loader.dll";
constexpr char kAside[] = ".mevr-off";  // Halcyon's upstream DLLs moved aside: dxgi.dll.mevr-off

// What is at bink2w64.dll now, against our record.
enum class LoaderNow {
    Missing,     // no bink2w64.dll
    Ours,        // the hash we wrote, or our loader's product name (an older or newer build of ours)
    Rad,         // RAD's own DLL (Steam's "verify files" put it back)
    Unrecorded,  // something else, and we have no record of bink2w64.dll: not ours to judge
    Foreign,     // something else replaced our loader since (ME3Tweaks Mod Manager, another tool): never touched
};

LoaderNow CheckLoader(Layout const& layout, Manifest const& m) {
    fs::path const file = layout.Full(paths::kBink);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return LoaderNow::Missing;
    }
    FileEntry const* entry = m.Find(paths::kBink);
    if (entry != nullptr && !entry->sha256.empty() && Sha256File(file) == entry->sha256) {
        return LoaderNow::Ours;
    }
    switch (detect::ClassifyBink(detect::ReadIdentity(file), true)) {
    case detect::BinkState::Ours: return LoaderNow::Ours;
    case detect::BinkState::Original: return LoaderNow::Rad;
    default: break;
    }
    return entry == nullptr ? LoaderNow::Unrecorded : LoaderNow::Foreign;
}

std::string LoaderName(Layout const& layout) {
    detect::DllIdentity const id = detect::ReadIdentity(layout.Full(paths::kBink));
    std::string const name = !id.product.empty() ? id.product : !id.description.empty() ? id.description : id.company;
    return name.empty() ? std::string("a DLL without a name") : name;
}

bool InstallLoader(Transaction& tx, Payload const& payload, detect::BinkState state) {
    switch (state) {
    case detect::BinkState::Original:
        tx.Data().loader = "mevr";
        return tx.Rename(paths::kBink, paths::kBinkOriginal) && tx.CopyIn(payload.Loader(), paths::kBink);
    case detect::BinkState::Ours:
        tx.Data().loader = "mevr";
        // Our loader without a record of it (the install record was lost, or deleted by hand): adopt it as we would
        // have recorded it, so an uninstall takes it out and puts RAD's DLL back.
        if (tx.Data().Find(paths::kBinkOriginal) == nullptr && !tx.AdoptRenamed(paths::kBink, paths::kBinkOriginal)) {
            return false;
        }
        if (tx.Data().Find(paths::kBink) == nullptr && !tx.AdoptAdded(paths::kBink)) {
            return false;
        }
        return tx.CopyIn(payload.Loader(), paths::kBink);
    case detect::BinkState::LEBinkProxy:
        tx.Data().loader = "lebinkproxy";
        return true;
    case detect::BinkState::OtherProxy:
        tx.Data().loader = "other";
        return true;
    case detect::BinkState::Missing:
    case detect::BinkState::Broken:
        break;
    }
    return false;
}

// Halcyon's upstream MELE-VR: its dxgi.dll (and the openxr_loader.dll it ships) renamed aside, on record.
bool MoveUpstreamAside(Transaction& tx, detect::UpstreamHalcyon const& upstream) {
    bool ok = true;
    if (upstream.dxgi) {
        ok = tx.Rename(kDxgi, std::string(kDxgi) + kAside) && ok;
    }
    if (upstream.openxrLoader) {
        ok = tx.Rename(kOpenXrLoader, std::string(kOpenXrLoader) + kAside) && ok;
    }
    if (upstream.Found()) {
        tx.Note("Halcyon's MELE-VR was installed in Binaries\\Win64: its dxgi.dll" + std::string(upstream.openxrLoader ? " and openxr_loader.dll" : "") +
            " moved aside (*.mevr-off; Uninstall puts them back), so only this mod's renderer runs. Its MELEVR.ini stays.");
    }
    return ok;
}

// Edits an ini file unless it's read-only: then a warning, and true in `*readOnly`.
bool EditUnlessReadOnly(Transaction& tx, std::string_view relative, char const* name,
    std::function<std::string(std::string_view, std::vector<ini::Edit>*)> const& edit, bool* readOnly) {
    if (detect::ReadOnlyFile(tx.Where().Full(relative))) {
        tx.Note(std::string("warning: ") + name + " is read-only (the Read-only box in its Properties is ticked, by you or a tool): left as it is. "
            "Untick it, then use Settings > Apply.");
        if (readOnly != nullptr) {
            *readOnly = true;
        }
        return true;
    }
    return tx.EditIni(relative, edit);
}

// `gamerReadOnly`: set when GamerSettings.ini is read-only (nothing applied to it; the rest still is).
bool ApplySettingsIn(Transaction& tx, settings::Choices const& choices, bool* gamerReadOnly = nullptr) {
    Layout const& layout = tx.Where();
    std::error_code ec;
    if (!fs::exists(layout.Full(paths::kGamerSettings), ec)) {
        tx.Data().gameSettings = false;
        return true;  // the caller says "start the game once first"
    }
    bool gamerLocked = false;
    bool ok = EditUnlessReadOnly(tx, paths::kGamerSettings, "GamerSettings.ini", [&](std::string_view text, std::vector<ini::Edit>* edits) {
        return settings::ApplyGamerSettings(text, choices, edits);
    }, &gamerLocked);
    if (fs::exists(layout.Full(paths::kEngineIni), ec)) {
        ok = EditUnlessReadOnly(tx, paths::kEngineIni, "BIOEngine.ini", [](std::string_view text, std::vector<ini::Edit>* edits) {
            return settings::ApplyEngineSettings(text, edits);
        }, nullptr) && ok;
    }
    if (choices.target.width > 0 && fs::exists(layout.Full(paths::kHalcyonIni), ec)) {
        ok = EditUnlessReadOnly(tx, paths::kHalcyonIni, "MELEVR.ini", [&](std::string_view text, std::vector<ini::Edit>* edits) {
            return ini::SetHalcyonResolution(text, choices.target.width, choices.target.height, edits);
        }, nullptr) && ok;
    }
    if (gamerReadOnly != nullptr) {
        *gamerReadOnly = gamerLocked;
    }
    tx.Data().gameSettings = ok && !gamerLocked;
    return ok && tx.Save();
}

// A folder that doesn't exist, or holds only our run-time files (none of anyone else's, and not empty).
bool OursOrMissing(Layout const& layout, std::string_view relative) {
    std::error_code ec;
    fs::path const dir = layout.Full(relative);
    if (!fs::exists(dir, ec)) {
        return true;
    }
    std::vector<std::string> ours = RuntimeFiles();
    ours.push_back(paths::kHalcyonIni);
    bool any = false;
    for (auto const& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_directory(ec)) {
            continue;
        }
        std::string const rel = layout.Relative(entry.path());
        if (std::none_of(ours.begin(), ours.end(), [&](std::string const& o) { return _stricmp(o.c_str(), rel.c_str()) == 0; })) {
            return false;
        }
        any = true;
    }
    return any && !ec;
}

// After a lost install record: every file the payload puts in the ASI folder that's already there (and the setup's
// copy) recorded as added, with its folders, so an uninstall takes them out.
void AdoptOurFiles(Transaction& tx, Payload const& payload, fs::path const& setupExe) {
    Layout const& layout = tx.Where();
    std::error_code ec;
    std::vector<std::string> targets{paths::kModAsi};
    tx.RecordDir(paths::kAsi);  // before its subfolders: they're removed first (newest first)
    for (auto const& [from, to] : {std::pair{payload.Fonts(), std::string(paths::kAsi) + "/fonts"}, std::pair{payload.Menu(), std::string(paths::kAsi) + "/menu"}}) {
        tx.RecordDir(to);
        for (auto const& entry : fs::recursive_directory_iterator(from, ec)) {
            std::string const rel = to + "/" + detect::Utf8(entry.path().lexically_relative(from).generic_wstring());
            if (entry.is_directory(ec)) {
                tx.RecordDir(rel);
            } else {
                targets.push_back(rel);
            }
        }
    }
    if (!setupExe.empty()) {
        targets.push_back(paths::kSetupExe);
    }
    for (std::string const& rel : targets) {
        if (fs::exists(layout.Full(rel), ec) && tx.Data().Find(rel) == nullptr) {
            tx.AdoptAdded(rel);
        }
    }
}

bool Installed(Transaction const& tx) {
    return tx.Exists() && (!tx.Data().modVersion.empty() || tx.Data().Find(paths::kModAsi) != nullptr);
}

bool Preflight(Layout const& layout, Outcome& out) {
    if (!detect::IsGameRoot(layout.root)) {
        out = Error("Mass Effect Legendary Edition isn't in " + detect::Utf8(layout.root.wstring()));
        return false;
    }
    if (detect::GameRunning()) {
        out = Error("Mass Effect (or its launcher) is running: close it first");
        return false;
    }
    if (!detect::CanWrite(layout.Win64())) {
        out = Error("Can't write to the game's folder: run the setup as administrator");
        return false;
    }
    return true;
}

} // namespace

bool Payload::Valid() const {
    std::error_code ec;
    return fs::exists(Asi(), ec) && fs::exists(Loader(), ec) && fs::is_directory(Fonts(), ec) && fs::is_directory(Menu(), ec);
}

std::optional<Payload> FindPayload(fs::path const& exeDir) {
    for (fs::path const& dir : {exeDir / L"payload", exeDir}) {
        Payload p{dir};
        if (p.Valid()) {
            return p;
        }
    }
    return std::nullopt;
}

Status ReadStatus(fs::path const& root, std::string const& source) {
    Status s;
    s.root = root;
    s.source = source;
    s.gameFound = detect::IsGameRoot(root);
    s.gameRunning = detect::GameRunning();
    if (!s.gameFound) {
        return s;
    }
    Layout const layout{root};
    s.version = detect::FileVersion(layout.Win64() / L"MassEffect1.exe");
    s.versionCheck = detect::CheckVersion(s.version);
    std::error_code ec;
    s.bink = detect::ClassifyBink(detect::ReadIdentity(layout.Win64() / L"bink2w64.dll"), fs::exists(layout.Win64() / L"bink2w64_original.dll", ec));
    s.canWrite = detect::CanWrite(layout.Win64());
    s.hasGamerSettings = fs::exists(layout.Full(paths::kGamerSettings), ec);
    s.gamerSettingsReadOnly = s.hasGamerSettings && detect::ReadOnlyFile(layout.Full(paths::kGamerSettings));
    s.upstream = detect::FindUpstreamHalcyon(layout.Win64());
    s.texturesMarked = packs::TexturesMarked(root);
    Transaction tx(layout);
    if (tx.Load() && tx.Exists()) {
        s.hasRecord = true;
        s.installed = Installed(tx);  // not when only the game settings or a pack were recorded
        s.installedVersion = tx.Data().modVersion;
        s.loader = tx.Data().loader;
        s.gameSettings = tx.Data().gameSettings;
        s.packs = tx.Data().packs;
    }
    return s;
}

Outcome Install(Layout const& layout, Payload const& payload, InstallOptions const& options) {
    Outcome out;
    if (!Preflight(layout, out)) {
        return out;
    }
    if (!payload.Valid()) {
        return Error("The setup's payload folder is incomplete: download the release again");
    }
    auto const version = detect::FileVersion(layout.Win64() / L"MassEffect1.exe");
    if (detect::CheckVersion(version) != detect::VersionCheck::Supported && !options.ignoreVersion) {
        return Error("MassEffect1.exe is version " + (version ? version->ToString() : std::string("unknown")) + "; the mod needs " +
            kSupportedGameVersion.ToString() + " and stays off on any other");
    }
    std::error_code ec;
    detect::BinkState const bink =
        detect::ClassifyBink(detect::ReadIdentity(layout.Win64() / L"bink2w64.dll"), fs::exists(layout.Win64() / L"bink2w64_original.dll", ec));
    if (bink == detect::BinkState::Missing || bink == detect::BinkState::Broken) {
        return Error(detect::BinkStateText(bink));
    }
    detect::UpstreamHalcyon const upstream = detect::FindUpstreamHalcyon(layout.Win64());
    if (upstream.Found() && !options.moveUpstreamAside) {
        return Error("Halcyon's MELE-VR is installed in Binaries\\Win64 (its dxgi.dll and openxr_loader.dll): with this mod as well, two VR "
                     "renderers would run. Remove it with its MELE-VR.bat, or let Install move its two DLLs aside (Uninstall puts them back).");
    }
    // Before anything is written: the ASI and Logs folders count as ours when they're missing (the mod creates Logs at
    // run time) or hold nothing but our run-time files (left by a "keep my settings" uninstall).
    // (A missing ASI folder is recorded when the first file goes in.)
    bool const asiOurs = fs::exists(layout.Asi(), ec) && OursOrMissing(layout, paths::kAsi);
    bool const logsOurs = OursOrMissing(layout, kLogsDir);
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    if (asiOurs) {
        tx.RecordDir(paths::kAsi);
    }
    if (logsOurs) {
        tx.RecordDir(kLogsDir);
    }
    Manifest& m = tx.Data();
    m.modVersion = updates::kVersion;
    m.setupVersion = updates::kVersion;
    m.installedAt = NowIso();
    if (!tx.Save()) {
        return Finish(tx, false);
    }
    if (bink == detect::BinkState::Ours && tx.Data().Find(paths::kBink) == nullptr) {
        // Our loader is there but not on record: the install record was lost. What our install put in the ASI folder
        // is adopted as ours too (else the files below would be "replaced" by themselves and come back on uninstall).
        AdoptOurFiles(tx, payload, options.setupExe);
    }
    bool ok = InstallLoader(tx, payload, bink);
    ok = ok && MoveUpstreamAside(tx, upstream);
    ok = ok && tx.CopyIn(payload.Asi(), paths::kModAsi);
    ok = ok && CopyTree(tx, payload.Fonts(), std::string(paths::kAsi) + "/fonts");
    ok = ok && CopyTree(tx, payload.Menu(), std::string(paths::kAsi) + "/menu");
    if (ok && !options.setupExe.empty() && fs::exists(options.setupExe, ec) &&
        !fs::equivalent(options.setupExe, layout.Full(paths::kSetupExe), ec)) {
        ok = tx.CopyIn(options.setupExe, paths::kSetupExe);
    }
    // What the mod writes at run time (its settings and calibration, its logs): on record as the player's data, so a
    // full uninstall deletes them and "keep my settings" keeps them.
    for (std::string const& file : RuntimeFiles()) {
        ok = ok && tx.RecordRuntimeFile(file);
    }
    // Halcyon's MELEVR.ini: its release file only if there's none; never overwritten or reset. Then the per-slot fixes.
    // In the ASI folder only this mod's installs and deploys put it, so one that's there without a record (left by an
    // earlier "keep my settings" uninstall) is the player's VR data too.
    ok = ok && tx.RecordRuntimeFile(paths::kHalcyonIni);
    ok = ok && tx.WriteIfMissing(paths::kHalcyonIni, ini::HalcyonReleaseIni(), true);
    ok = ok && EditUnlessReadOnly(tx, paths::kHalcyonIni, "MELEVR.ini",
                   [](std::string_view text, std::vector<ini::Edit>* edits) { return ini::FixHalcyonIni(text, edits); }, nullptr);
    if (ok && options.source == "Steam") {
        // The Launch button starts the exe directly, which needs Steam's app id next to it (as Mod Manager does).
        if (!fs::exists(layout.Full(paths::kSteamAppId), ec)) {
            ok = tx.WriteIn(paths::kSteamAppId, detect::kSteamAppId);
        }
    }
    if (ok && options.gameSettings) {
        ok = ApplySettingsIn(tx, options.choices);
    }
    return Finish(tx, ok);
}

Outcome Uninstall(Layout const& layout, bool keepUserData, fs::path const& runningExe, bool removeLocalData) {
    Outcome out;
    if (!Preflight(layout, out)) {
        return out;
    }
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    if (!tx.Exists()) {
        return Error("Nothing to uninstall: no install record (MEVR-install.json) in the game's ASI folder");
    }
    // A loader another tool put in place of ours since (ME3Tweaks Mod Manager, say) stays, with RAD's DLL behind it.
    std::error_code ec;
    LoaderNow const loader = CheckLoader(layout, tx.Data());
    if (loader == LoaderNow::Foreign) {
        tx.Note("left bink2w64.dll and bink2w64_original.dll as they are: another ASI loader (" + LoaderName(layout) + ") replaced ours since");
        tx.Forget(paths::kBink);
        tx.Forget(paths::kBinkOriginal);
    } else if (loader == LoaderNow::Rad && !fs::exists(layout.Full(paths::kBinkOriginal), ec)) {
        // RAD's DLL is where it belongs and nothing is behind it: nothing to undo (removing it would break the game).
        tx.Note("bink2w64.dll is RAD's own again: left as it is");
        tx.Forget(paths::kBink);
        tx.Forget(paths::kBinkOriginal);
    }
    bool const asiOurs = std::any_of(tx.Data().dirs.begin(), tx.Data().dirs.end(),
        [](std::string const& d) { return _stricmp(d.c_str(), paths::kAsi) == 0; });
    fs::path const installedSetup = layout.Full(paths::kSetupExe);
    fs::path moved;
    if (!runningExe.empty() && fs::exists(installedSetup, ec) && fs::equivalent(runningExe, installedSetup, ec)) {
        // A running exe can be renamed but not deleted: out of the way now, deleted once we've exited.
        moved = installedSetup;
        moved += L".old";
        fs::remove(moved, ec);
        if (!MoveFileExW(installedSetup.c_str(), moved.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            moved.clear();
        }
    }
    bool const ok = tx.Uninstall(keepUserData);
    if (!moved.empty()) {
        if (!ok) {
            // Not uninstalled: this copy stays (and on record), for another try.
            if (MoveFileExW(moved.c_str(), installedSetup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                tx.AdoptAdded(paths::kSetupExe);
            }
        } else {
            // The paths go to cmd in environment variables: cmd expands %NAME% once and never again inside the value,
            // so a % in the game's path stays as it is.
            SetEnvironmentVariableW(L"MEVR_OLD_SETUP", moved.c_str());
            SetEnvironmentVariableW(L"MEVR_ASI_DIR", layout.Asi().c_str());
            std::wstring command = L"cmd.exe /d /c ping -n 3 127.0.0.1 >nul & del /f /q \"%MEVR_OLD_SETUP%\"";
            if (asiOurs) {
                command += L" & rmdir \"%MEVR_ASI_DIR%\"";  // only if empty, and only the folder we created
            }
            STARTUPINFOW si{};
            si.cb = sizeof si;
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
    }
    if (ok && removeLocalData) {
        fs::path const local = LocalDataDir();
        if (!local.empty() && fs::exists(local, ec)) {
            fs::remove_all(local, ec);
            tx.Note(ec ? "warning: couldn't delete all of " + detect::Utf8(local.wstring()) + " (" + ec.message() + ")"
                       : "deleted " + detect::Utf8(local.wstring()) + " (snapshots, recordings, frame timing, crash reports, downloads)");
        }
    }
    return Finish(tx, ok);
}

Outcome Repair(Layout const& layout, Payload const& payload) {
    Outcome out;
    if (!Preflight(layout, out)) {
        return out;
    }
    if (!payload.Valid()) {
        return Error("Repair needs the downloaded release (this copy has no payload)");
    }
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    if (!Installed(tx)) {
        return Error("Not installed yet: use Install");
    }
    std::error_code ec;
    bool ok = true;
    // The loader, only where it's still ours, RAD's (Steam's "verify files" put it back over ours) or missing; one
    // another tool installed since stays.
    bool const originalThere = fs::exists(layout.Full(paths::kBinkOriginal), ec);
    LoaderNow const loader = CheckLoader(layout, tx.Data());
    if (tx.Data().loader == "mevr") {
        if (loader == LoaderNow::Rad) {
            ok = originalThere ? tx.CopyIn(payload.Loader(), paths::kBink)
                               : tx.Rename(paths::kBink, paths::kBinkOriginal) && tx.CopyIn(payload.Loader(), paths::kBink);
        } else if (loader == LoaderNow::Missing && originalThere) {
            ok = tx.CopyIn(payload.Loader(), paths::kBink);
        } else if (loader == LoaderNow::Missing) {
            tx.Fail("bink2w64.dll and bink2w64_original.dll are both missing: verify the game's files in Steam or the EA app, then Repair");
            ok = false;
        } else if (loader == LoaderNow::Foreign) {
            tx.Note("left bink2w64.dll as it is: another ASI loader (" + LoaderName(layout) + ") replaced ours since");
        }
    }
    bool const loaderOurs = CheckLoader(layout, tx.Data()) == LoaderNow::Ours;
    if (detect::UpstreamHalcyon const upstream = detect::FindUpstreamHalcyon(layout.Win64()); upstream.Found()) {
        ok = MoveUpstreamAside(tx, upstream) && ok;
    }
    for (std::string const& path : tx.Verify()) {
        fs::path source;
        if (path == paths::kModAsi) {
            source = payload.Asi();
        } else if (path == paths::kBink) {
            if (loaderOurs) {  // an older or changed build of ours: the payload's
                source = payload.Loader();
            }
        } else if (path.rfind(std::string(paths::kAsi) + "/fonts/", 0) == 0) {
            source = payload.Fonts() / fs::path(detect::Wide(path.substr(std::string(paths::kAsi).size() + 7)));
        } else if (path.rfind(std::string(paths::kAsi) + "/menu/", 0) == 0) {
            source = payload.Menu() / fs::path(detect::Wide(path.substr(std::string(paths::kAsi).size() + 6)));
        }
        // A file the payload doesn't have (the setup's copy, steam_appid.txt) is left as it is.
        if (!source.empty() && fs::exists(source, ec)) {
            ok = tx.CopyIn(source, path) && ok;
        }
    }
    if (fs::exists(layout.Full(paths::kHalcyonIni), ec)) {
        ok = EditUnlessReadOnly(tx, paths::kHalcyonIni, "MELEVR.ini",
                 [](std::string_view text, std::vector<ini::Edit>* edits) { return ini::FixHalcyonIni(text, edits); }, nullptr) && ok;
    } else {
        ok = tx.WriteIfMissing(paths::kHalcyonIni, ini::HalcyonReleaseIni(), true) && ok;
    }
    return Finish(tx, ok);
}

Outcome ApplySettings(Layout const& layout, settings::Choices const& choices) {
    Outcome out;
    if (!Preflight(layout, out)) {
        return out;
    }
    std::error_code ec;
    if (!fs::exists(layout.Full(paths::kGamerSettings), ec)) {
        return Error("GamerSettings.ini doesn't exist yet: start the game once, quit, then apply");
    }
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    bool gamerReadOnly = false;
    bool ok = ApplySettingsIn(tx, choices, &gamerReadOnly);
    if (gamerReadOnly) {
        tx.Fail("GamerSettings.ini is read-only: untick Read-only in its Properties (Game\\ME1\\BioGame\\Config), then apply again");
        ok = false;
    }
    return Finish(tx, ok);
}

Outcome RestoreSettings(Layout const& layout) {
    Outcome out;
    if (!Preflight(layout, out)) {
        return out;
    }
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    bool ok = tx.RevertIni(paths::kGamerSettings);
    ok = tx.RevertIni(paths::kEngineIni) && ok;
    tx.Data().gameSettings = false;
    ok = ok && (!tx.Exists() || tx.Save());
    return Finish(tx, ok);
}

Outcome RecordPack(Layout const& layout, std::string const& id, std::string const& name) {
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    auto& list = tx.Data().packs;
    list.erase(std::remove_if(list.begin(), list.end(), [&](PackRecord const& p) { return p.id == id; }), list.end());
    list.push_back({id, name, "mem", NowIso()});
    return Finish(tx, tx.Save());
}

fs::path LocalDataDir() {
    wchar_t buffer[MAX_PATH]{};
    DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return fs::path(buffer) / L"MEVR";
}

fs::path RequestFile() {
    return LocalDataDir() / L"setup.request";
}

std::vector<std::string> RuntimeFiles() {
    std::string const logs = std::string(kLogsDir) + "/";
    return {paths::kModIni, logs + "LE1MEVR.log", logs + "LE1MEVR.prev.log", logs + "MEVRLoader.log"};
}

} // namespace mevr::install
