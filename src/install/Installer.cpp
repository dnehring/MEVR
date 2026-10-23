#include "install/Installer.hpp"

#include "install/Ini.hpp"
#include "install/Packs.hpp"
#include "install/Updates.hpp"

#include <Windows.h>

#include <algorithm>
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

bool InstallLoader(Transaction& tx, Payload const& payload, detect::BinkState state) {
    switch (state) {
    case detect::BinkState::Original:
        tx.Data().loader = "mevr";
        return tx.Rename(paths::kBink, paths::kBinkOriginal) && tx.CopyIn(payload.Loader(), paths::kBink);
    case detect::BinkState::Ours:
        tx.Data().loader = "mevr";
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

bool ApplySettingsIn(Transaction& tx, settings::Choices const& choices) {
    Layout const& layout = tx.Where();
    std::error_code ec;
    if (!fs::exists(layout.Full(paths::kGamerSettings), ec)) {
        tx.Data().gameSettings = false;
        return true;  // the caller says "start the game once first"
    }
    bool ok = tx.EditIni(paths::kGamerSettings, [&](std::string_view text, std::vector<ini::Edit>* edits) {
        return settings::ApplyGamerSettings(text, choices, edits);
    });
    if (fs::exists(layout.Full(paths::kEngineIni), ec)) {
        ok = tx.EditIni(paths::kEngineIni, [](std::string_view text, std::vector<ini::Edit>* edits) {
            return settings::ApplyEngineSettings(text, edits);
        }) && ok;
    }
    if (choices.target.width > 0 && fs::exists(layout.Full(paths::kHalcyonIni), ec)) {
        ok = tx.EditIni(paths::kHalcyonIni, [&](std::string_view text, std::vector<ini::Edit>* edits) {
            return ini::SetHalcyonResolution(text, choices.target.width, choices.target.height, edits);
        }) && ok;
    }
    tx.Data().gameSettings = ok;
    return ok && tx.Save();
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
    s.texturesMarked = packs::TexturesMarked(root);
    Transaction tx(layout);
    if (tx.Load() && tx.Exists()) {
        s.installed = true;
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
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    Manifest& m = tx.Data();
    m.modVersion = updates::kVersion;
    m.setupVersion = updates::kVersion;
    m.installedAt = NowIso();
    if (!tx.Save()) {
        return Finish(tx, false);
    }
    bool ok = InstallLoader(tx, payload, bink);
    ok = ok && tx.CopyIn(payload.Asi(), paths::kModAsi);
    ok = ok && CopyTree(tx, payload.Fonts(), std::string(paths::kAsi) + "/fonts");
    ok = ok && CopyTree(tx, payload.Menu(), std::string(paths::kAsi) + "/menu");
    if (ok && !options.setupExe.empty() && fs::exists(options.setupExe, ec) &&
        !fs::equivalent(options.setupExe, layout.Full(paths::kSetupExe), ec)) {
        ok = tx.CopyIn(options.setupExe, paths::kSetupExe);
    }
    // Halcyon's MELEVR.ini: its release file only if there's none; never overwritten or reset. Then the per-slot fixes.
    ok = ok && tx.WriteIfMissing(paths::kHalcyonIni, ini::HalcyonReleaseIni(), true);
    ok = ok && tx.EditIni(paths::kHalcyonIni, [](std::string_view text, std::vector<ini::Edit>* edits) { return ini::FixHalcyonIni(text, edits); });
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

Outcome Uninstall(Layout const& layout, bool keepUserData, fs::path const& runningExe) {
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
    std::error_code ec;
    fs::path const installedSetup = layout.Full(paths::kSetupExe);
    fs::path moved;
    if (!runningExe.empty() && fs::exists(installedSetup, ec) && fs::equivalent(runningExe, installedSetup, ec)) {
        // A running exe can be renamed but not deleted: out of the way now, deleted once we've exited.
        moved = installedSetup;
        moved += L".old";
        fs::remove(moved, ec);
        MoveFileExW(installedSetup.c_str(), moved.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    bool const ok = tx.Uninstall(keepUserData);
    if (!moved.empty()) {
        std::wstring command = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + moved.wstring() + L"\" & rmdir \"" +
            layout.Asi().wstring() + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
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
    if (!tx.Exists()) {
        return Error("Not installed yet: use Install");
    }
    std::error_code ec;
    bool ok = true;
    // The loader: if Steam's "verify files" put RAD's DLL back over ours.
    detect::BinkState const bink =
        detect::ClassifyBink(detect::ReadIdentity(layout.Win64() / L"bink2w64.dll"), fs::exists(layout.Win64() / L"bink2w64_original.dll", ec));
    if (tx.Data().loader == "mevr" && bink == detect::BinkState::Original) {
        if (fs::exists(layout.Win64() / L"bink2w64_original.dll", ec)) {
            ok = tx.CopyIn(payload.Loader(), paths::kBink);
        } else {
            ok = tx.Rename(paths::kBink, paths::kBinkOriginal) && tx.CopyIn(payload.Loader(), paths::kBink);
        }
    }
    for (std::string const& path : tx.Verify()) {
        fs::path source;
        if (path == paths::kModAsi) {
            source = payload.Asi();
        } else if (path == paths::kBink) {
            source = payload.Loader();
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
        ok = tx.EditIni(paths::kHalcyonIni, [](std::string_view text, std::vector<ini::Edit>* edits) { return ini::FixHalcyonIni(text, edits); }) && ok;
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
    bool const ok = ApplySettingsIn(tx, choices);
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

fs::path RequestFile() {
    wchar_t buffer[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    return fs::path(buffer) / L"MEVR" / L"setup.request";
}

} // namespace mevr::install
