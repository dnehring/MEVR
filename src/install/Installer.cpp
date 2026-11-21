// SPDX-License-Identifier: MIT
#include "install/Installer.hpp"

#include "install/Bodies.hpp"
#include "install/Ini.hpp"
#include "install/Packs.hpp"
#include "install/Voice.hpp"
#include "install/Updates.hpp"
#include "core/Ini.hpp"
#include "core/vr/SettingsFile.hpp"  // the three games' settings.ini: its per-game sections (research/25 section 20)

#include <Windows.h>

#include <algorithm>
#include <functional>
#include <system_error>

namespace mevr::install {

PendingSettings PendingFromChoices(settings::Choices const& c) {
    PendingSettings p;
    p.width = c.target.width;
    p.height = c.target.height;
    p.vsyncOff = c.vsyncOff;
    p.motionBlurOff = c.motionBlurOff;
    p.filmGrainOff = c.filmGrainOff;
    p.hdrOff = c.hdrOff;
    p.eyeSizeAtFirstStart = c.eyeSizeAtFirstStart;
    return p;
}

settings::Choices ChoicesFromPending(PendingSettings const& p) {
    settings::Choices c;
    c.target = {p.width, p.height};
    c.vsyncOff = p.vsyncOff;
    c.motionBlurOff = p.motionBlurOff;
    c.filmGrainOff = p.filmGrainOff;
    c.hdrOff = p.hdrOff;
    c.eyeSizeAtFirstStart = p.eyeSizeAtFirstStart;
    return c;
}

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

constexpr char kAside[] = ".mevr-off";  // Halcyon's upstream DLLs moved aside: dxgi.dll.mevr-off

// What is at bink2w64.dll now, against our record.
enum class LoaderNow {
    Missing,     // no bink2w64.dll
    Ours,        // the hash we wrote, or our loader's product name (an older or newer build of ours)
    Rad,         // RAD's own DLL (Steam's "verify files" put it back)
    Unrecorded,  // something else, and we have no record of bink2w64.dll: not ours to judge
    Foreign,     // something else replaced our loader since (ME3Tweaks Mod Manager, another tool): never touched
};

// `bink`: the game's (Layout::Paths().bink) or the launcher's (paths::kLauncherBink).
LoaderNow CheckLoader(Layout const& layout, Manifest const& m, std::string const& bink) {
    fs::path const file = layout.Full(bink);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return LoaderNow::Missing;
    }
    FileEntry const* entry = m.Find(bink);
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

std::string LoaderName(Layout const& layout, std::string const& bink) {
    detect::DllIdentity const id = detect::ReadIdentity(layout.Full(bink));
    std::string const name = !id.product.empty() ? id.product : !id.description.empty() ? id.description : id.company;
    return name.empty() ? std::string("a DLL without a name") : name;
}

bool InstallLoader(Transaction& tx, Payload const& payload, detect::BinkState state) {
    GamePaths const& p = tx.Where().Paths();
    switch (state) {
    case detect::BinkState::Original:
        tx.Data().loader = "mevr";
        return tx.Rename(p.bink, p.binkOriginal) && tx.CopyIn(payload.Loader(), p.bink);
    case detect::BinkState::Ours:
        tx.Data().loader = "mevr";
        // Our loader without a record of it (the install record was lost, or deleted by hand): adopt it as we would
        // have recorded it, so an uninstall takes it out and puts RAD's DLL back.
        if (tx.Data().Find(p.binkOriginal) == nullptr && !tx.AdoptRenamed(p.bink, p.binkOriginal)) {
            return false;
        }
        if (tx.Data().Find(p.bink) == nullptr && !tx.AdoptAdded(p.bink)) {
            return false;
        }
        return tx.CopyIn(payload.Loader(), p.bink);
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

bool OursOrMissing(Layout const& layout, std::string_view relative);

// Our loader in front of the launcher's own Bink too (research/33 section 12): the launcher started with -game 1 then
// boots Mass Effect 1, so Launch needs no picker. Optional: whatever stands there that isn't RAD's or ours is left as it
// is, with a note (LEBinkProxy boots the game itself; anything else means the picker shows).
// The three games share it: a second game's install finds ours there, adopts it on its own record too (the same two
// entries) and writes the release's loader over it; Uninstall leaves it while another game's record still has it
// (KeepSharedLauncher).
bool InstallLauncherLoader(Transaction& tx, Layout const& layout, Payload const& payload) {
    std::string const name = layout.Game().name;
    std::error_code ec;
    if (!fs::exists(layout.Full(paths::kLauncherExe), ec)) {
        return true;  // no Legendary Edition launcher in this install
    }
    detect::BinkState const state =
        detect::ClassifyBink(detect::ReadIdentity(layout.Full(paths::kLauncherBink)), fs::exists(layout.Full(paths::kLauncherBinkOriginal), ec));
    bool ok = true;
    switch (state) {
    case detect::BinkState::Original:
        ok = tx.Rename(paths::kLauncherBink, paths::kLauncherBinkOriginal) && tx.CopyIn(payload.Loader(), paths::kLauncherBink);
        break;
    case detect::BinkState::Ours:
        if (tx.Data().Find(paths::kLauncherBinkOriginal) == nullptr && !tx.AdoptRenamed(paths::kLauncherBink, paths::kLauncherBinkOriginal)) {
            return false;
        }
        if (tx.Data().Find(paths::kLauncherBink) == nullptr && !tx.AdoptAdded(paths::kLauncherBink)) {
            return false;
        }
        ok = tx.CopyIn(payload.Loader(), paths::kLauncherBink);
        break;
    case detect::BinkState::LEBinkProxy:
        tx.Note("Game\\Launcher has ME3Tweaks' LEBinkProxy: it starts " + name + " for Launch itself; left as it is");
        return true;
    case detect::BinkState::OtherProxy:
        tx.Note("Game\\Launcher has another ASI loader (" + LoaderName(layout, paths::kLauncherBink) +
                "): left as it is; Launch may show the launcher's game picker");
        return true;
    case detect::BinkState::Missing:
    case detect::BinkState::Broken:
        tx.Note("Game\\Launcher has no usable bink2w64.dll: left as it is; Launch shows the launcher's game picker (verify the game's files, "
                "then Repair)");
        return true;
    }
    // Its log, the mod's own data (and its folder, when it's ours to make).
    if (ok && OursOrMissing(layout, paths::kLauncherLogs)) {
        tx.RecordDir(paths::kLauncherLogs);
    }
    return ok && tx.RecordRuntimeFile(paths::kLauncherLoaderLog);
}

// Halcyon's upstream MELE-VR: its dxgi.dll (and the openxr_loader.dll it ships) renamed aside, on record.
bool MoveUpstreamAside(Transaction& tx, detect::UpstreamHalcyon const& upstream) {
    GamePaths const& p = tx.Where().Paths();
    GameSpec const& spec = tx.Where().Game();
    bool ok = true;
    if (upstream.dxgi) {
        ok = tx.Rename(p.dxgi, p.dxgi + kAside) && ok;
    }
    if (upstream.openxrLoader) {
        ok = tx.Rename(p.openXrLoader, p.openXrLoader + kAside) && ok;
    }
    if (upstream.Found()) {
        tx.Note(std::string("Halcyon's ") + spec.upstreamName + " was installed in Binaries\\Win64: its dxgi.dll" +
            std::string(upstream.openxrLoader ? " and openxr_loader.dll" : "") +
            " moved aside (*.mevr-off; Uninstall puts them back), so only this mod's renderer runs. Its " + spec.rendererIni + " stays.");
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

// The render size's first-start marker in the three games' settings.ini (research/25 section 20): per game, so
// [Display.<GAME>] eyeSizeSet = 0 when the player chose Automatic again, and only where the file has it (the mod sets it
// to 1 at the first VR start; a missing key already means "not sized yet"). The player's data, never recorded for
// uninstall (the same as calibration.ini); the old ASI\<GAME>MEVR.ini gets its own edit above, for an older build.
void ResetSharedEyeSize(Transaction& tx, std::string_view game) {
    namespace sf = vr::settings_file;
    fs::path const shared = LocalDataDir() / sf::kFileName;
    if (LocalDataDir().empty() || sf::Probe(shared) != sf::Access::Readable) {
        return;
    }
    std::string const section = sf::GameSectionName("Display", game);
    if (!mevr::ini::Get(mevr::ini::Read(shared).file, section, "eyeSizeSet")) {
        return;
    }
    std::string const error = sf::Apply(shared, {sf::Set(section, "eyeSizeSet", "0")}, game);
    tx.Note(error.empty() ? "settings.ini: [" + section + "] eyeSizeSet = 0: the render size follows the headset at the next VR start"
                          : "warning: couldn't write [" + section + "] eyeSizeSet = 0 to " + detect::Utf8(shared.wstring()) + ": " + error);
}

// `gamerReadOnly`: set when GamerSettings.ini is read-only (nothing applied to it; the rest still is).
bool ApplySettingsIn(Transaction& tx, settings::Choices const& choices, bool* gamerReadOnly = nullptr) {
    Layout const& layout = tx.Where();
    GamePaths const& p = layout.Paths();
    GameSpec const& spec = layout.Game();
    std::string const name = spec.name;
    std::error_code ec;
    // A sequel's renderer sizes the stereo target itself (GameSpec::stereoTarget): nothing of the choice's is kept.
    tx.Data().eyeSizeAtFirstStart = spec.stereoTarget && choices.eyeSizeAtFirstStart;  // a later choice (Settings, Keep) replaces it
    if (!fs::exists(layout.Full(p.gamerSettings), ec)) {
        // The game has never been started here: it makes GamerSettings.ini on its first start. Without the VR values the
        // window stays bordered at the monitor's size (Halcyon's stereo breaks) and MELEVR.ini keeps the template's
        // target, so the choices are kept and applied once the file exists (Repair, "Start the game once", Settings).
        tx.Data().gameSettings = false;
        tx.Data().pendingSettings = PendingFromChoices(choices);
        if (!spec.stereoTarget) {
            // A sequel: no stereo target of the setup's waits (its renderer sizes its own), only the switches.
            tx.Data().pendingSettings->width = 0;
            tx.Data().pendingSettings->height = 0;
            tx.Data().pendingSettings->eyeSizeAtFirstStart = false;
        }
        tx.Note("warning: " + name + " hasn't been started on this PC yet, so its settings file (GamerSettings.ini) doesn't exist and the VR "
                "game settings can't go on. They're kept: start the game once (MEVR Setup's \"Start the game once\"), quit at its main menu, "
                "and they're applied (or use Repair afterwards).");
        return tx.Save();
    }
    bool gamerLocked = false;
    bool ok = EditUnlessReadOnly(tx, p.gamerSettings, "GamerSettings.ini", [&](std::string_view text, std::vector<ini::Edit>* edits) {
        return settings::ApplyGamerSettings(text, choices, edits, layout.game);
    }, &gamerLocked);
    if (spec.engineIni && fs::exists(layout.Full(p.engineIni), ec)) {
        ok = EditUnlessReadOnly(tx, p.engineIni, "BIOEngine.ini", [&](std::string_view text, std::vector<ini::Edit>* edits) {
            return settings::ApplyEngineSettings(text, choices, edits);
        }, nullptr) && ok;
    }
    if (spec.stereoTarget && choices.target.width > 0 && fs::exists(layout.Full(p.rendererIni), ec)) {
        ok = EditUnlessReadOnly(tx, p.rendererIni, "MELEVR.ini", [&](std::string_view text, std::vector<ini::Edit>* edits) {
            return ini::SetHalcyonResolution(text, choices.target.width, choices.target.height, edits);
        }, nullptr) && ok;
    }
    if (ok && !gamerLocked && spec.stereoTarget && choices.eyeSizeAtFirstStart && fs::exists(layout.Full(p.modIni), ec)) {
        // Explicitly applying Automatic must work after a previous headset was sized.
        ok = tx.EditIni(p.modIni, [](std::string_view text, std::vector<ini::Edit>* edits) {
            if (ini::Get(text, "Display", "eyeSizeSet").has_value()) {
                return ini::Set(text, "Display", "eyeSizeSet", "0", edits);
            }
            return std::string(text);
        });
    }
    if (ok && !gamerLocked && spec.stereoTarget && choices.eyeSizeAtFirstStart) {
        ResetSharedEyeSize(tx, spec.key);
    }
    if (gamerReadOnly != nullptr) {
        *gamerReadOnly = gamerLocked;
    }
    tx.Data().gameSettings = ok && !gamerLocked;
    if (tx.Data().gameSettings) {
        tx.Data().pendingSettings.reset();
    }
    return ok && tx.Save();
}

// A folder that doesn't exist, or holds only our run-time files (none of anyone else's, and not empty).
bool OursOrMissing(Layout const& layout, std::string_view relative) {
    std::error_code ec;
    fs::path const dir = layout.Full(relative);
    if (!fs::exists(dir, ec)) {
        return true;
    }
    std::vector<std::string> ours = RuntimeFiles(layout.game);
    ours.push_back(layout.Paths().rendererIni);
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
    GamePaths const& p = layout.Paths();
    GameSpec const& spec = layout.Game();
    std::error_code ec;
    std::vector<std::string> targets{p.modAsi};
    tx.RecordDir(p.asi);  // before its subfolders: they're removed first (newest first)
    std::vector<std::pair<fs::path, std::string>> trees{{payload.Fonts(), p.asi + "/fonts"},
        {payload.Menu(), p.asi + "/menu"}};
    if (spec.audio && payload.HasAudio()) {
        trees.emplace_back(payload.Audio(), p.asi + "/audio");
    }
    for (auto const& [from, to] : trees) {
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
        targets.push_back(p.setupExe);
    }
    // The cockpit (research/47): its script's folder, the script, the package and its TOC entry, all only ever put there by us.
    if (spec.cockpit) {
        tx.RecordDir(p.asi + "/cockpit");
        targets.push_back(paths::kCockpitScript);
        targets.push_back(paths::kCockpitPackage);
    }
    for (std::string const& rel : targets) {
        if (fs::exists(layout.Full(rel), ec) && tx.Data().Find(rel) == nullptr) {
            tx.AdoptAdded(rel);
        }
    }
    if (spec.cockpit) {
        tx.AdoptTocEntry(paths::kToc, paths::kCockpitTocName);
    }
    // The voice orders' DLL, the release's own (the downloaded voice files are adopted by nothing: Install fetches them again).
    if (spec.voice && payload.HasVoice() && fs::exists(layout.Full(voice::kDll), ec) && tx.Data().Find(voice::kDll) == nullptr) {
        tx.RecordDir(voice::kDir);
        tx.AdoptAdded(voice::kDll);
    }
}

bool Installed(Transaction const& tx) {
    return tx.Exists() && (!tx.Data().modVersion.empty() || tx.Data().Find(tx.Where().Paths().modAsi) != nullptr);
}

// The launcher's loader and its log are shared by the three games. While another game at this root still has the mod
// installed with the launcher's loader on its record, an uninstall of this one leaves them where they are: forgotten
// here (no error, nothing to put back), so Launch keeps starting the other game without the picker.
void KeepSharedLauncher(Transaction& tx) {
    Layout const& layout = tx.Where();
    if (tx.Data().Find(paths::kLauncherBink) == nullptr) {
        return;
    }
    for (GameId const other : InstalledGames(layout.root, layout.game)) {
        Transaction theirs(Layout{layout.root, other});
        if (!theirs.Load() || theirs.Data().Find(paths::kLauncherBink) == nullptr) {
            continue;
        }
        tx.Note(std::string("left Game\\Launcher's loader in place: ") + Spec(other).name + "'s install uses it too");
        tx.Forget(paths::kLauncherBink);
        tx.Forget(paths::kLauncherBinkOriginal);
        tx.Forget(paths::kLauncherLoaderLog);
        std::erase_if(tx.Data().dirs, [](std::string const& d) { return _stricmp(d.c_str(), paths::kLauncherLogs) == 0; });
        return;
    }
}

// The launcher's loader as another game's install (a newer release's) wrote it: this game's record has the older hash,
// which is nothing to repair.
bool SharedLauncherCurrent(Layout const& layout) {
    std::string now;
    for (GameId const other : InstalledGames(layout.root, layout.game)) {
        Transaction theirs(Layout{layout.root, other});
        if (!theirs.Load()) {
            continue;
        }
        FileEntry const* entry = theirs.Data().Find(paths::kLauncherBink);
        if (entry == nullptr || entry->sha256.empty()) {
            continue;
        }
        if (now.empty()) {
            now = Sha256File(layout.Full(paths::kLauncherBink));
        }
        if (entry->sha256 == now) {
            return true;
        }
    }
    return false;
}

bool Preflight(Layout const& layout, Outcome& out) {
    if (!detect::HasGame(layout.root, layout.game)) {
        out = Error(layout.game == GameId::LE1 ? "Mass Effect Legendary Edition isn't in " + detect::Utf8(layout.root.wstring())
                                               : std::string(layout.Game().name) + " isn't in " + detect::Utf8(layout.root.wstring()));
        return false;
    }
    if (detect::GameRunningIn(layout.root, layout.game)) {
        out = Error("Mass Effect (or its launcher) is running: close it first");
        return false;
    }
    if (!detect::CanWrite(layout.Win64())) {
        out = Error("Can't write to the game's folder: run the setup as administrator");
        return false;
    }
    return true;
}

// The Mako's cockpit (research/47): the package, its TOC entry (without one the game never loads it) and the script the mod runs
// on entering the Mako. Again on Repair (a "verify files" puts the game's own TOC back). A release without it: nothing.
bool InstallCockpit(Transaction& tx, Payload const& payload) {
    if (!payload.HasCockpit()) {
        tx.Note("this release has no cockpit: the Mako keeps the game's chase camera");
        return true;
    }
    std::error_code ec;
    std::uintmax_t const size = fs::file_size(payload.CockpitPackage(), ec);
    if (ec || size > 0xFFFFFFFFull) {
        tx.Fail("the cockpit's package in the release can't be read");
        return false;
    }
    return tx.CopyIn(payload.CockpitPackage(), paths::kCockpitPackage) && tx.CopyIn(payload.CockpitScript(), paths::kCockpitScript) &&
        tx.SetTocEntry(paths::kToc, paths::kCockpitTocName, static_cast<std::uint32_t>(size));
}

// A sequel's renderer settings (MELE2VR.ini, MELE3VR.ini), which live next to the game's exe: where Halcyon's own
// release puts its file too. One that is there without a record of ours is the player's (upstream's install, or kept by
// an earlier uninstall): never written, never recorded, so no uninstall of ours deletes it. With none, the release's file
// goes in (as tools/deploy-asi.ps1 installs upstream's for ME2), or, when the release has none for this game, the file
// the renderer will write itself is put on record as the player's VR data. The baseline the mod needs is pinned in the
// renderer's code at every start (ApplyMevrBaseline), not here.
bool InstallRendererIni(Transaction& tx, Payload const& payload, bool preserveSettings) {
    Layout const& layout = tx.Where();
    GamePaths const& p = layout.Paths();
    std::error_code ec;
    if (fs::exists(layout.Full(p.rendererIni), ec)) {
        if (tx.Data().Find(p.rendererIni) == nullptr) {
            tx.Note(std::string(layout.Game().rendererIni) + " is already next to " + layout.Game().exe + " (Halcyon's own install, or yours): left as it is");
        }
        return true;
    }
    if (tx.Data().Find(p.rendererIni) != nullptr || preserveSettings) {
        return true;  // ours and deleted since: the renderer writes it again
    }
    if (auto const bytes = ReadFile(payload.RendererIni(layout.game))) {
        return tx.WriteIfMissing(p.rendererIni, *bytes, true);
    }
    return tx.RecordRuntimeFile(p.rendererIni);
}

// The voice orders' recognizer interface (research/54) next to the mod; its runtime and models are optional content.
bool InstallVoiceDll(Transaction& tx, Payload const& payload) {
    if (!payload.HasVoice()) {
        tx.Note("this release has no MEVR-Voice.dll: no voice orders");
        return true;
    }
    return tx.CopyIn(payload.VoiceDll(), voice::kDll);
}

} // namespace

bool Payload::HasVoice() const {
    std::error_code ec;
    return fs::exists(VoiceDll(), ec);
}

bool Payload::HasAudio() const {
    std::error_code ec;
    return fs::exists(Audio() / L"soft_oal.dll", ec);
}

bool Payload::HasRig() const {
    std::error_code ec;
    return fs::exists(RigExe(), ec) && fs::exists(RigDir() / L"plans" / L"players.jsonc", ec);
}

bool Payload::HasCockpit() const {
    std::error_code ec;
    return fs::exists(CockpitPackage(), ec) && fs::exists(CockpitScript(), ec);
}

bool Payload::Valid() const {
    std::error_code ec;
    return fs::exists(Asi(), ec) && fs::exists(Loader(), ec) && fs::is_directory(Fonts(), ec) && fs::is_directory(Menu(), ec);
}

fs::path Payload::Asi(GameId game) const {
    return dir / (detect::Wide(Spec(game).key) + L"MEVR.asi");
}

fs::path Payload::RendererIni(GameId game) const {
    return dir / detect::Wide(Spec(game).rendererIni);
}

bool Payload::Has(GameId game) const {
    std::error_code ec;
    return fs::exists(Asi(game), ec) && fs::exists(Loader(), ec) && fs::is_directory(Fonts(), ec) && fs::is_directory(Menu(), ec);
}

std::optional<Payload> FindPayload(fs::path const& exeDir) {
    // Mass Effect 1's mod first, as every release so far; a payload with only a sequel's (a test) counts too.
    for (GameId const game : kAllGames) {
        for (fs::path const& dir : {exeDir / L"payload", exeDir}) {
            Payload p{dir};
            if (p.Has(game)) {
                return p;
            }
        }
    }
    return std::nullopt;
}

std::vector<GameId> InstalledGames(fs::path const& root, std::optional<GameId> except) {
    std::vector<GameId> games;
    for (GameId const game : kAllGames) {
        if (except && *except == game) {
            continue;
        }
        Transaction tx(Layout{root, game});
        if (tx.Load() && Installed(tx)) {
            games.push_back(game);
        }
    }
    return games;
}

Status ReadStatus(fs::path const& root, std::string const& source) {
    return ReadStatus(root, source, GameId::LE1);
}

Status ReadStatus(fs::path const& root, std::string const& source, GameId game) {
    GameSpec const& spec = Spec(game);
    Status s;
    s.game = game;
    s.root = root;
    s.source = source;
    s.gameFound = detect::HasGame(root, game);
    s.gameRunning = s.gameFound ? detect::GameRunningIn(root, game)
                                : detect::ProcessRunning(spec.exeW) || detect::ProcessRunning(L"MassEffectLauncher.exe");
    if (!s.gameFound) {
        return s;
    }
    Layout const layout{root, game};
    GamePaths const& p = layout.Paths();
    s.version = detect::FileVersion(layout.Win64() / spec.exeW);
    s.versionCheck = detect::CheckVersion(s.version);
    std::error_code ec;
    s.bink = detect::ClassifyBink(detect::ReadIdentity(layout.Win64() / L"bink2w64.dll"), fs::exists(layout.Win64() / L"bink2w64_original.dll", ec));
    s.launcherBink = detect::ClassifyBink(detect::ReadIdentity(layout.Full(paths::kLauncherBink)),
        fs::exists(layout.Full(paths::kLauncherBinkOriginal), ec));
    s.canWrite = detect::CanWrite(layout.Win64());
    s.hasGamerSettings = fs::exists(layout.Full(p.gamerSettings), ec);
    s.gamerSettingsReadOnly = s.hasGamerSettings && detect::ReadOnlyFile(layout.Full(p.gamerSettings));
    s.upstream = detect::FindUpstreamHalcyon(layout.Win64(), spec.upstreamMarker);
    s.texturesMarked = spec.packs && packs::TexturesMarked(root);
    Transaction tx(layout);
    if (tx.Load() && tx.Exists()) {
        s.hasRecord = true;
        s.installed = Installed(tx);  // not when only the game settings or a pack were recorded
        s.installedVersion = tx.Data().modVersion;
        s.loader = tx.Data().loader;
        s.gameSettings = tx.Data().gameSettings;
        s.pendingSettings = tx.Data().pendingSettings;
        s.eyeSizeAtFirstStart = tx.Data().eyeSizeAtFirstStart;
        s.packs = tx.Data().packs;
        if (s.installed) {
            // What Repair would put back: a loader another tool installed since stays (Repair leaves it).
            s.needsRepair = tx.Verify();
            if (CheckLoader(layout, tx.Data(), p.bink) == LoaderNow::Foreign) {
                std::erase(s.needsRepair, std::string(p.bink));
            }
            if (CheckLoader(layout, tx.Data(), paths::kLauncherBink) == LoaderNow::Foreign || SharedLauncherCurrent(layout)) {
                std::erase(s.needsRepair, std::string(paths::kLauncherBink));
            }
            // The moving bodies (install/Bodies): a package gone or changed, or their index in %LOCALAPPDATA% gone.
            if (spec.bodies) {
                bodies::State const b = bodies::ReadState(layout);
                for (std::string const& file : b.damaged) {
                    std::string const rel = bodies::CookedFile(game, file);
                    if (std::find(s.needsRepair.begin(), s.needsRepair.end(), rel) == s.needsRepair.end()) {
                        s.needsRepair.push_back(rel);
                    }
                }
                if (b.Built() && !b.indexThere) {
                    s.needsRepair.push_back(detect::Utf8((bodies::GameDir(game) / (L"MEVR_Bodies_" + detect::Wide(spec.key) + L".json")).wstring()));
                }
            }
        }
    }
    return s;
}

Outcome Install(Layout const& layout, Payload const& payload, InstallOptions const& options) {
    Outcome out;
    if (!Preflight(layout, out)) {
        return out;
    }
    GameSpec const& spec = layout.Game();
    GamePaths const& p = layout.Paths();
    if (!payload.Has(layout.game)) {
        return Error(layout.game == GameId::LE1 ? std::string("The setup's payload folder is incomplete: download the release again")
                                                : std::string("This release doesn't carry the mod for ") + spec.name + " (" + spec.key + "MEVR.asi)");
    }
    auto const version = detect::FileVersion(layout.Win64() / spec.exeW);
    if (detect::CheckVersion(version) != detect::VersionCheck::Supported && !options.ignoreVersion) {
        return Error(std::string(spec.exe) + " is version " + (version ? version->ToString() : std::string("unknown")) + "; the mod needs " +
            kSupportedGameVersion.ToString() + " and stays off on any other");
    }
    std::error_code ec;
    detect::BinkState const bink =
        detect::ClassifyBink(detect::ReadIdentity(layout.Win64() / L"bink2w64.dll"), fs::exists(layout.Win64() / L"bink2w64_original.dll", ec));
    if (bink == detect::BinkState::Missing || bink == detect::BinkState::Broken) {
        return Error(detect::BinkStateText(bink));
    }
    detect::UpstreamHalcyon const upstream = detect::FindUpstreamHalcyon(layout.Win64(), spec.upstreamMarker);
    if (upstream.Found() && !options.moveUpstreamAside) {
        return Error(std::string("Halcyon's ") + spec.upstreamName +
                     " is installed in Binaries\\Win64 (its dxgi.dll and openxr_loader.dll): with this mod as well, two VR "
                     "renderers would run. Remove it with its " + spec.upstreamName +
                     ".bat, or let Install move its two DLLs aside (Uninstall puts them back).");
    }
    // Before anything is written: the ASI and Logs folders count as ours when they're missing (the mod creates Logs at
    // run time) or hold nothing but our run-time files (left by a "keep my settings" uninstall).
    // (A missing ASI folder is recorded when the first file goes in.)
    bool const asiOurs = fs::exists(layout.Asi(), ec) && OursOrMissing(layout, p.asi);
    bool const logsOurs = OursOrMissing(layout, p.logs);
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    if (options.preserveSettings && !Installed(tx)) {
        return Error("Install MEVR first before applying an update");
    }
    if (asiOurs) {
        tx.RecordDir(p.asi);
    }
    if (logsOurs) {
        tx.RecordDir(p.logs);
    }
    Manifest& m = tx.Data();
    bool const settingsAlreadyApplied = m.gameSettings;  // an update over an install whose game settings are on
    if (!options.preserveSettings) {
        m.modVersion = updates::kVersion;
        m.setupVersion = updates::kVersion;
        m.installedAt = NowIso();
    }
    if (!tx.Save()) {
        return Finish(tx, false);
    }
    if (bink == detect::BinkState::Ours && tx.Data().Find(p.bink) == nullptr) {
        // Our loader is there but not on record: the install record was lost. What our install put in the ASI folder
        // is adopted as ours too (else the files below would be "replaced" by themselves and come back on uninstall).
        AdoptOurFiles(tx, payload, options.setupExe);
    }
    bool ok = InstallLoader(tx, payload, bink);
    ok = ok && InstallLauncherLoader(tx, layout, payload);
    ok = ok && MoveUpstreamAside(tx, upstream);
    ok = ok && tx.CopyIn(payload.Asi(layout.game), p.modAsi);
    ok = ok && CopyTree(tx, payload.Fonts(), p.asi + "/fonts");
    ok = ok && CopyTree(tx, payload.Menu(), p.asi + "/menu");
    // OpenAL Soft (research/69 "Real 3D audio"): only into our ASI folder; the host points the game's imports at it.
    if (spec.audio && payload.HasAudio()) {
        ok = ok && CopyTree(tx, payload.Audio(), p.asi + "/audio");
    }
    if (spec.cockpit) {
        ok = ok && InstallCockpit(tx, payload);
    }
    if (spec.voice) {
        ok = ok && InstallVoiceDll(tx, payload);
    }
    if (ok && !options.setupExe.empty() && fs::exists(options.setupExe, ec) &&
        !fs::equivalent(options.setupExe, layout.Full(p.setupExe), ec)) {
        ok = tx.CopyIn(options.setupExe, p.setupExe);
    }
    // What the mod writes at run time (its settings and calibration, its logs): on record as the player's data, so a
    // full uninstall deletes them and "keep my settings" keeps them.
    for (std::string const& file : RuntimeFiles(layout.game)) {
        ok = ok && tx.RecordRuntimeFile(file);
    }
    if (layout.game == GameId::LE1) {
        // Halcyon's MELEVR.ini: its release file only if there's none; never overwritten or reset. Then the per-slot fixes.
        // In the ASI folder only this mod's installs and deploys put it, so one that's there without a record (left by an
        // earlier "keep my settings" uninstall) is the player's VR data too.
        ok = ok && tx.RecordRuntimeFile(p.rendererIni);
        if (!options.preserveSettings) {
            ok = ok && tx.WriteIfMissing(p.rendererIni, ini::HalcyonReleaseIni(), true);
            ok = ok && EditUnlessReadOnly(tx, p.rendererIni, "MELEVR.ini",
                           [](std::string_view text, std::vector<ini::Edit>* edits) { return ini::FixHalcyonIni(text, edits); }, nullptr);
        }
    } else {
        ok = ok && InstallRendererIni(tx, payload, options.preserveSettings);
    }
    if (ok && options.source == "Steam") {
        // Steam's app id next to the exe, as Mod Manager writes it, for a direct start of the exe. Launch goes through Steam
        // since research/33 section 12 (a direct start of the Steam build hands back to Steam's arguments prompt); kept
        // for the EA app build's direct start [verify] and recorded, so an uninstall takes it out.
        if (!fs::exists(layout.Full(p.steamAppId), ec)) {
            ok = tx.WriteIn(p.steamAppId, detect::kSteamAppId);
        }
    }
    bool kept = false;
    if (ok && !options.preserveSettings && options.gameSettings && (!settingsAlreadyApplied || options.reapplySettings)) {
        ok = ApplySettingsIn(tx, options.choices);
    } else if (ok && (options.gameSettings || options.preserveSettings)) {
        kept = true;
    }
    if (ok && options.preserveSettings) {
        m.modVersion = updates::kVersion;
        m.setupVersion = updates::kVersion;
        m.installedAt = NowIso();
        ok = tx.Save();
    }
    // The moving bodies and hair (install/Bodies), last: built from the player's own game files, which takes a minute or more.
    // Whatever happens to the build, the install stands (its errors are warnings: the mod runs without them). An update builds
    // them only where they were built before.
    if (ok && spec.bodies && options.bodies && (!options.preserveSettings || bodies::ReadState(layout).Built())) {
        std::size_t const errorsBefore = tx.Errors().size();
        if (payload.HasRig()) {
            bodies::BuildIn(tx, payload, false, [&](bodies::Step const& step) {
                if (options.progress) {
                    options.progress(step.text, step.percent);
                }
            }, options.cancel);
        } else {
            tx.Note("this release has no body builder (payload\\rig): no moving bodies and hair");
        }
        tx.DemoteErrors(errorsBefore);
    } else if (ok && spec.bodies && !options.bodies) {
        tx.Note("the moving bodies and hair weren't built (left out): Optional content builds them");
    }
    Outcome done = Finish(tx, ok);
    if (kept) {
        done.log.push_back("kept your game settings (render resolution and the rest) from the earlier install");
    }
    return done;
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
    GamePaths const& p = layout.Paths();
    // The game settings the mod wrote since the install (the resolution from Halcyon's profiles, its depth of field) are
    // put back too: read from MELEVR.ini now, before the uninstall removes it (research/33 section 13).
    tx.ModWrites(settings::ModWrites(ReadFile(layout.Full(p.rendererIni)).value_or(std::string()), layout.game));
    // A loader another tool put in place of ours since (ME3Tweaks Mod Manager, say) stays, with RAD's DLL behind it.
    std::error_code ec;
    LoaderNow const loader = CheckLoader(layout, tx.Data(), p.bink);
    if (loader == LoaderNow::Foreign) {
        tx.Note("left bink2w64.dll and bink2w64_original.dll as they are: another ASI loader (" + LoaderName(layout, p.bink) + ") replaced ours since");
        tx.Forget(p.bink);
        tx.Forget(p.binkOriginal);
    } else if (loader == LoaderNow::Rad && !fs::exists(layout.Full(p.binkOriginal), ec)) {
        // RAD's DLL is where it belongs and nothing is behind it: nothing to undo (removing it would break the game).
        tx.Note("bink2w64.dll is RAD's own again: left as it is");
        tx.Forget(p.bink);
        tx.Forget(p.binkOriginal);
    }
    KeepSharedLauncher(tx);  // another of the three games still installed here uses the launcher's loader
    if (tx.Data().Find(paths::kLauncherBink) != nullptr) {
        LoaderNow const launcher = CheckLoader(layout, tx.Data(), paths::kLauncherBink);
        if (launcher == LoaderNow::Foreign) {
            tx.Note("left Game\\Launcher's bink2w64.dll as it is: another ASI loader (" + LoaderName(layout, paths::kLauncherBink) +
                    ") replaced ours since");
            tx.Forget(paths::kLauncherBink);
            tx.Forget(paths::kLauncherBinkOriginal);
        } else if (launcher == LoaderNow::Rad && !fs::exists(layout.Full(paths::kLauncherBinkOriginal), ec)) {
            tx.Note("Game\\Launcher's bink2w64.dll is RAD's own again: left as it is");
            tx.Forget(paths::kLauncherBink);
            tx.Forget(paths::kLauncherBinkOriginal);
        }
    }
    bool const asiOurs = std::any_of(tx.Data().dirs.begin(), tx.Data().dirs.end(),
        [&](std::string const& d) { return _stricmp(d.c_str(), p.asi.c_str()) == 0; });
    fs::path const installedSetup = layout.Full(p.setupExe);
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
    // Texture packs aren't ours to undo: MEM rewrote the game's packages (research/33 section 3). The voice orders' files
    // (a "download" record) are files on record like the rest: removed below.
    std::string names;
    for (PackRecord const& pack : tx.Data().packs) {
        if (pack.method == "mem") {
            names += (names.empty() ? "" : ", ") + pack.name;
        }
    }
    if (!names.empty() || (layout.Game().packs && packs::TexturesMarked(layout.root))) {
        tx.Note("texture packs stay" + (names.empty() ? std::string() : " (" + names + ")") +
                ": Mass Effect Modder changed the game's files for good; only a full restore (or reinstall) of the game takes them out");
    }
    bool const ok = tx.Uninstall(keepUserData);
    if (!moved.empty()) {
        if (!ok) {
            // Not uninstalled: this copy stays (and on record), for another try.
            if (MoveFileExW(moved.c_str(), installedSetup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                tx.AdoptAdded(p.setupExe);
            }
        } else {
            // The paths go to cmd in environment variables: cmd expands %NAME% once and never again inside the value,
            // so a % in the game's path stays as it is.
            SetEnvironmentVariableW(L"MEVR_OLD_SETUP", moved.c_str());
            SetEnvironmentVariableW(L"MEVR_ASI_DIR", layout.Asi().c_str());
            // The running Setup can't be deleted until its window closes, which the player does when they like: the
            // helper tries once a second for up to 10 minutes (it gave up after 2 s, so MEVR-Setup.exe.old and the ASI
            // folder usually stayed; Codex review 2026-11-06, install#17). cmd and ping by their full System32 paths,
            // never found by search next to Setup (install#2).
            wchar_t system[MAX_PATH] = {};
            UINT const n = GetSystemDirectoryW(system, MAX_PATH);
            std::wstring const sys = n > 0 && n < MAX_PATH ? std::wstring(system, n) : std::wstring(L"C:\\Windows\\System32");
            std::wstring const cmd = sys + L"\\cmd.exe";
            std::wstring command = L"\"" + cmd + L"\" /d /c (for /l %i in (1,1,600) do @(if exist \"%MEVR_OLD_SETUP%\" (del /f /q \"%MEVR_OLD_SETUP%\" "
                                   L"2>nul & if exist \"%MEVR_OLD_SETUP%\" \"" + sys + L"\\PING.EXE\" -n 2 127.0.0.1 >nul)))";
            if (asiOurs) {
                command += L" & rmdir \"%MEVR_ASI_DIR%\"";  // only if empty, and only the folder we created
            }
            STARTUPINFOW si{};
            si.cb = sizeof si;
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(cmd.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
    }
    // %LOCALAPPDATA%\MEVR is one folder for the three games: it stays while another of them still has the mod.
    std::vector<GameId> const others = removeLocalData ? InstalledGames(layout.root, layout.game) : std::vector<GameId>();
    if (ok && removeLocalData && !others.empty()) {
        tx.Note(std::string("kept ") + detect::Utf8(LocalDataDir().wstring()) + ": " + Spec(others.front()).name +
                " still has the mod installed and uses it too");
    } else if (ok && removeLocalData) {
        fs::path const local = LocalDataDir();
        if (!local.empty() && fs::exists(local, ec)) {
            fs::remove_all(local, ec);
            tx.Note(ec ? "warning: couldn't delete all of " + detect::Utf8(local.wstring()) + " (" + ec.message() + ")"
                       : "deleted " + detect::Utf8(local.wstring()) +
                             " (the settings and calibration the three games share, snapshots, recordings, frame timing, crash reports, downloads)");
        }
    }
    return Finish(tx, ok);
}

Outcome Repair(Layout const& layout, Payload const& payload, std::function<void(std::string const&, int)> const& progress,
    std::atomic<bool> const* cancel) {
    Outcome out;
    if (!Preflight(layout, out)) {
        return out;
    }
    GameSpec const& spec = layout.Game();
    GamePaths const& p = layout.Paths();
    if (!payload.Has(layout.game)) {
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
    bool const originalThere = fs::exists(layout.Full(p.binkOriginal), ec);
    LoaderNow const loader = CheckLoader(layout, tx.Data(), p.bink);
    if (tx.Data().loader == "mevr") {
        if (loader == LoaderNow::Rad) {
            ok = originalThere ? tx.CopyIn(payload.Loader(), p.bink)
                               : tx.Rename(p.bink, p.binkOriginal) && tx.CopyIn(payload.Loader(), p.bink);
        } else if (loader == LoaderNow::Missing && originalThere) {
            ok = tx.CopyIn(payload.Loader(), p.bink);
        } else if (loader == LoaderNow::Missing) {
            tx.Fail("bink2w64.dll and bink2w64_original.dll are both missing: verify the game's files in Steam or the EA app, then Repair");
            ok = false;
        } else if (loader == LoaderNow::Foreign) {
            tx.Note("left bink2w64.dll as it is: another ASI loader (" + LoaderName(layout, p.bink) + ") replaced ours since");
        }
    }
    // The launcher's loader (research/33 section 12): put back like the game's; an install from before it gets it now.
    if (tx.Data().loader == "mevr") {
        if (tx.Data().Find(paths::kLauncherBink) == nullptr) {
            ok = InstallLauncherLoader(tx, layout, payload) && ok;
        } else {
            bool const launcherOriginal = fs::exists(layout.Full(paths::kLauncherBinkOriginal), ec);
            LoaderNow const launcher = CheckLoader(layout, tx.Data(), paths::kLauncherBink);
            if (launcher == LoaderNow::Rad) {
                bool const put = launcherOriginal ? tx.CopyIn(payload.Loader(), paths::kLauncherBink)
                                                  : tx.Rename(paths::kLauncherBink, paths::kLauncherBinkOriginal) &&
                        tx.CopyIn(payload.Loader(), paths::kLauncherBink);
                ok = put && ok;
            } else if (launcher == LoaderNow::Missing && launcherOriginal) {
                ok = tx.CopyIn(payload.Loader(), paths::kLauncherBink) && ok;
            } else if (launcher == LoaderNow::Missing) {
                tx.Note("Game\\Launcher's bink2w64.dll is missing: Launch shows the launcher's game picker until Steam or the EA app verifies the "
                        "game's files");
            } else if (launcher == LoaderNow::Foreign) {
                tx.Note("left Game\\Launcher's bink2w64.dll as it is: another ASI loader (" + LoaderName(layout, paths::kLauncherBink) +
                        ") replaced ours since");
            }
        }
    }
    bool const loaderOurs = CheckLoader(layout, tx.Data(), p.bink) == LoaderNow::Ours;
    bool const launcherLoaderOurs = CheckLoader(layout, tx.Data(), paths::kLauncherBink) == LoaderNow::Ours;
    if (detect::UpstreamHalcyon const upstream = detect::FindUpstreamHalcyon(layout.Win64(), spec.upstreamMarker); upstream.Found()) {
        ok = MoveUpstreamAside(tx, upstream) && ok;
    }
    for (std::string const& path : tx.Verify()) {
        fs::path source;
        if (path == p.modAsi) {
            source = payload.Asi(layout.game);
        } else if (path == p.bink) {
            if (loaderOurs) {  // an older or changed build of ours: the payload's
                source = payload.Loader();
            }
        } else if (path == paths::kLauncherBink) {
            if (launcherLoaderOurs) {
                source = payload.Loader();
            }
        } else if (path.rfind(p.asi + "/fonts/", 0) == 0) {
            source = payload.Fonts() / fs::path(detect::Wide(path.substr(p.asi.size() + 7)));
        } else if (path.rfind(p.asi + "/menu/", 0) == 0) {
            source = payload.Menu() / fs::path(detect::Wide(path.substr(p.asi.size() + 6)));
        } else if (path.rfind(p.asi + "/audio/", 0) == 0) {
            source = payload.Audio() / fs::path(detect::Wide(path.substr(p.asi.size() + 7)));
        } else if (spec.voice && path == voice::kDll) {
            source = payload.VoiceDll();
        } else if (path == p.rendererIni && layout.game != GameId::LE1) {
            continue;  // the player's renderer settings: never put back from the release
        }
        // A file the payload doesn't have (the setup's copy, steam_appid.txt) is left as it is.
        if (!source.empty() && fs::exists(source, ec)) {
            ok = tx.CopyIn(source, path) && ok;
        }
    }
    // The core payload that isn't on record at all: an install that failed before copying it (its version was already
    // saved, so it counts as installed), which the loop above never sees (Codex review 2026-11-06, install#11).
    if (tx.Data().Find(p.modAsi) == nullptr) {
        tx.Note("the mod's ASI isn't on record (an install that didn't finish): installing it");
        ok = tx.CopyIn(payload.Asi(layout.game), p.modAsi) && ok;
    }
    auto const missingFromTree = [&](fs::path const& from, std::string const& relativeDir) {
        bool done = true;
        for (auto const& entry : fs::recursive_directory_iterator(from, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            std::string const rel = relativeDir + "/" + detect::Utf8(entry.path().lexically_relative(from).generic_wstring());
            if (tx.Data().Find(rel) == nullptr) {
                done = tx.CopyIn(entry.path(), rel) && done;
            }
        }
        return done;
    };
    ok = missingFromTree(payload.Fonts(), p.asi + "/fonts") && ok;
    ok = missingFromTree(payload.Menu(), p.asi + "/menu") && ok;
    if (spec.audio && payload.HasAudio()) {  // an install from before OpenAL Soft gets it now
        ok = missingFromTree(payload.Audio(), p.asi + "/audio") && ok;
    }
    if (spec.cockpit) {
        ok = InstallCockpit(tx, payload) && ok;
    }
    // The moving bodies' TOC entries after "verify files", or the bodies built again; never a reason for Repair to fail.
    if (spec.bodies) {
        std::size_t const errorsBefore = tx.Errors().size();
        bodies::RepairIn(tx, payload, [&](bodies::Step const& step) {
            if (progress) {
                progress(step.text, step.percent);
            }
        }, cancel);
        tx.DemoteErrors(errorsBefore);
    }
    // An install from before the voice orders gets their DLL now (their downloads stay optional content).
    if (spec.voice && payload.HasVoice() && tx.Data().Find(voice::kDll) == nullptr) {
        ok = InstallVoiceDll(tx, payload) && ok;
    }
    if (layout.game != GameId::LE1) {
        ok = InstallRendererIni(tx, payload, false) && ok;
    } else if (fs::exists(layout.Full(p.rendererIni), ec)) {
        ok = EditUnlessReadOnly(tx, p.rendererIni, "MELEVR.ini",
                 [](std::string_view text, std::vector<ini::Edit>* edits) { return ini::FixHalcyonIni(text, edits); }, nullptr) && ok;
    } else {
        ok = tx.WriteIfMissing(p.rendererIni, ini::HalcyonReleaseIni(), true) && ok;
    }
    // The VR game settings chosen at install, before the game had ever been started: on now if it has made its file since.
    if (tx.Data().pendingSettings) {
        if (fs::exists(layout.Full(p.gamerSettings), ec)) {
            tx.Note("the game has made its settings file since the install: applying the VR game settings chosen then");
            ok = ApplySettingsIn(tx, ChoicesFromPending(*tx.Data().pendingSettings)) && ok;
        } else {
            tx.Note(std::string("the VR game settings still wait for the game's first start (GamerSettings.ini isn't there yet): start ") + spec.name +
                    " once, quit at its main menu, then Repair again");
        }
    }
    return Finish(tx, ok);
}

Outcome ApplySettings(Layout const& layout, settings::Choices const& choices) {
    Outcome out;
    if (!Preflight(layout, out)) {
        return out;
    }
    GamePaths const& p = layout.Paths();
    std::error_code ec;
    if (!fs::exists(layout.Full(p.gamerSettings), ec)) {
        return Error("GamerSettings.ini doesn't exist yet: start the game once, quit, then apply");
    }
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    bool gamerReadOnly = false;
    bool ok = ApplySettingsIn(tx, choices, &gamerReadOnly);
    if (gamerReadOnly) {
        tx.Fail(std::string("GamerSettings.ini is read-only: untick Read-only in its Properties (Game\\") + layout.Game().folder +
                "\\BioGame\\Config), then apply again");
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
    GamePaths const& p = layout.Paths();
    tx.ModWrites(settings::ModWrites(ReadFile(layout.Full(p.rendererIni)).value_or(std::string()), layout.game));
    bool ok = tx.RevertIni(p.gamerSettings);
    ok = tx.RevertIni(p.engineIni) && ok;
    tx.Data().gameSettings = false;
    tx.Data().pendingSettings.reset();  // the player's own values: nothing waits to go on later either
    tx.Data().eyeSizeAtFirstStart = false;
    ok = ok && (!tx.Exists() || tx.Save());
    return Finish(tx, ok);
}

static void PutPackOnRecord(Transaction& tx, std::string const& id, std::string const& name) {
    auto& list = tx.Data().packs;
    list.erase(std::remove_if(list.begin(), list.end(), [&](PackRecord const& p) { return p.id == id; }), list.end());
    list.push_back({id, name, "mem", NowIso()});
}

Outcome RecordPack(Layout const& layout, std::string const& id, std::string const& name) {
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    PutPackOnRecord(tx, id, name);
    return Finish(tx, tx.Save());
}

Outcome InstallPack(Layout const& layout, std::string const& packId, fs::path const& download,
    std::function<void(packs::InstallStep const&)> const& progress, std::atomic<bool> const* cancel) {
    packs::Pack const* pack = packs::Find(packId);
    if (pack == nullptr) {
        return Error("No texture pack called '" + packId + "' in the catalogue");
    }
    if (pack->method != packs::Method::PageThenMem) {
        return Error(std::string(pack->name) + " installs with its author's own tool, not with this setup");
    }
    if (!layout.Game().packs) {
        return Error(std::string("The texture packs in the catalogue are Mass Effect 1's: none to install for ") + layout.Game().name);
    }
    if (!detect::IsGameRoot(layout.root)) {
        return Error("Mass Effect Legendary Edition isn't in " + detect::Utf8(layout.root.wstring()));
    }
    GamePaths const& p = layout.Paths();
    std::error_code ec;
    if (!fs::is_regular_file(download, ec)) {
        return Error("The downloaded file isn't there: " + detect::Utf8(download.wstring()));
    }
    // The game's ini files as they are before MEM writes its graphics settings into them.
    std::string const gamerBefore = ReadFile(layout.Full(p.gamerSettings)).value_or(std::string());
    std::string const engineBefore = ReadFile(layout.Full(p.engineIni)).value_or(std::string());
    // Not Preflight: "Install after I quit" starts us while the game is still exiting, and InstallWithMem waits for it.
    std::string const error = packs::InstallWithMem(*pack, download, layout.root, progress, cancel);
    if (!error.empty()) {
        return Error(error);
    }
    // On record, and the VR game settings again where MEM's own graphics settings went over them: the values this install
    // wrote, as recorded, and only where they were still in place before MEM ran (the player's own changes stay).
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    tx.Note(std::string(pack->name) + " is installed (Mass Effect Modder changed the game's textures for good)");
    PutPackOnRecord(tx, pack->id, pack->name);
    bool ok = tx.Save();
    ok = tx.ReassertIni(p.gamerSettings, gamerBefore) && ok;
    ok = tx.ReassertIni(p.engineIni, engineBefore) && ok;
    return Finish(tx, ok);
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

std::optional<SetupRequest> ParseSetupRequest(std::string_view text) {
    auto const v = json::Parse(text);
    if (!v || !v->IsObject()) {
        return std::nullopt;
    }
    SetupRequest r;
    r.page = v->String("page", "content");
    if (r.page != "content") {
        r.page = "content";  // the only page the game asks for
    }
    for (json::Value const& id : v->Get("packs").Items()) {
        if (id.IsString() && (packs::Find(id.AsString()) != nullptr || id.AsString() == voice::kContent) &&
            std::find(r.packs.begin(), r.packs.end(), id.AsString()) == r.packs.end()) {
            r.packs.push_back(id.AsString());
        }
    }
    double const pid = v->Number("pid", 0.0);
    r.pid = pid > 0.0 && pid < 4294967296.0 ? static_cast<unsigned long>(pid) : 0;
    return r;
}

std::vector<std::string> RuntimeFiles() {
    return RuntimeFiles(GameId::LE1);
}

std::vector<std::string> RuntimeFiles(GameId game) {
    GamePaths const& p = PathsFor(game);
    return {p.modIni, p.modLog, p.modPrevLog, p.loaderLog, paths::kLauncherLoaderLog};
}

} // namespace mevr::install
