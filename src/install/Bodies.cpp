// SPDX-License-Identifier: MIT
#include "install/Bodies.hpp"

#include "core/Json.hpp"
#include "install/Detect.hpp"
#include "install/Packs.hpp"
#include "install/Toc.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <system_error>

namespace mevr::install::bodies {
namespace {

namespace fs = std::filesystem;

std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// A package name the builder may give: MEVR_<letters, digits, _ . ->.pcc, nothing that names a folder.
bool ValidPackageName(std::string_view name) {
    if (name.size() < 10 || name.size() > 80 || Lower(name.substr(0, 5)) != "mevr_" || Lower(name.substr(name.size() - 4)) != ".pcc") {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; }) &&
        name.find("..") == std::string_view::npos;
}

std::string TocFile(GameId game) {
    return std::string("Game/") + Spec(game).folder + "/BioGame/PCConsoleTOC.bin";
}

constexpr std::string_view kContentPrefix = "bodies/";

// The bare package names on record (content "bodies/<file>").
std::vector<std::string> Recorded(Manifest const& m) {
    std::vector<std::string> out;
    for (FileEntry const& f : m.files) {
        if (f.content.size() > kContentPrefix.size() && f.content.compare(0, kContentPrefix.size(), kContentPrefix) == 0) {
            out.push_back(f.content.substr(kContentPrefix.size()));
        }
    }
    return out;
}

std::string IndexName(GameId game) {
    return std::string("MEVR_Bodies_") + Spec(game).key + ".json";
}

State StateOf(Transaction const& tx) {
    Layout const& layout = tx.Where();
    State s;
    s.packages = Recorded(tx.Data());
    for (std::string const& path : tx.VerifyContent(kContent, false)) {
        s.damaged.push_back(path.substr(path.find_last_of('/') + 1));
    }
    auto const bytes = ReadFile(layout.Full(TocFile(layout.game)));
    auto const toc = bytes ? toc::Parse(*bytes) : std::nullopt;
    for (std::string const& p : s.packages) {
        std::error_code ec;
        std::uintmax_t const size = fs::file_size(layout.Full(CookedFile(layout.game, p)), ec);
        if (!ec && (!toc || toc::Find(*toc, TocName(p)) != std::optional<std::uint32_t>(static_cast<std::uint32_t>(size))) &&
            std::find(s.damaged.begin(), s.damaged.end(), p) == s.damaged.end()) {
            s.damaged.push_back(p);
        }
    }
    fs::path const dir = GameDir(layout.game);
    std::error_code ec;
    s.indexThere = !dir.empty() && fs::exists(dir / detect::Wide(IndexName(layout.game)), ec);
    if (auto const text = ReadFile(dir / detect::Wide(kRecipeFile))) {
        if (auto const v = json::Parse(*text)) {
            s.recipe = v->String("recipe");
        }
    }
    if (auto const text = ReadFile(dir / detect::Wide(kResultFile))) {
        if (auto const v = json::Parse(*text)) {
            s.bodies = static_cast<int>(v->Number("bodies", 0.0));
        }
    }
    return s;
}

// Runs the builder hidden at below-normal priority, every line of its output into `log`, its progress lines to `progress`.
// The exit code; -1 if it couldn't start, -2 when stopped through `cancel`.
int RunBuilder(std::wstring commandLine, fs::path const& workDir, fs::path const& log, ProgressFn const& progress, std::atomic<bool> const* cancel) {
    std::ofstream out(log, std::ios::binary | std::ios::app);
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        return -1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    // Below normal: the game (or anything else the player does meanwhile) keeps the CPU; the build only uses what's idle.
    BOOL const started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
        nullptr, workDir.c_str(), &startup, &process);
    CloseHandle(writePipe);
    if (!started) {
        out << "couldn't start the builder (error " << GetLastError() << ")\n";
        CloseHandle(readPipe);
        return -1;
    }
    bool stopped = false;
    std::string pending;
    char buffer[4096];
    for (;;) {
        if (cancel != nullptr && cancel->load() && !stopped) {
            TerminateProcess(process.hProcess, 1);
            stopped = true;
        }
        DWORD read = 0;
        if (!::ReadFile(readPipe, buffer, sizeof buffer, &read, nullptr) || read == 0) {
            break;
        }
        out.write(buffer, read);
        pending.append(buffer, read);
        for (std::size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
            std::string text = pending.substr(0, nl);
            if (!text.empty() && text.back() == '\r') {
                text.pop_back();
            }
            if (auto const p = ParseProgress(text); p && progress) {
                progress(Step{p->text, p->percent});
            }
            pending.erase(0, nl + 1);
        }
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);
    out << "builder exit code " << code << (stopped ? " (stopped)" : "") << "\n";
    return stopped ? -2 : static_cast<int>(code);
}

// The game's TOC is one this setup can edit (else the packages could never load: nothing is built).
bool TocEditable(Layout const& layout, std::string* why) {
    auto const bytes = ReadFile(layout.Full(TocFile(layout.game)));
    auto const toc = bytes ? toc::Parse(*bytes) : std::nullopt;
    if (!toc) {
        *why = "BioGame\\PCConsoleTOC.bin is missing or not a TOC";
        return false;
    }
    if (!toc::Consistent(*toc)) {
        *why = "BioGame\\PCConsoleTOC.bin isn't laid out as this setup expects";
        return false;
    }
    return true;
}

// A package of ours ME3 has already (its install's), to measure the breasts' limits against: no reference body to build.
fs::path Me3Reference(Layout const& layout) {
    if (layout.game == GameId::LE3) {
        return {};
    }
    fs::path const file = layout.root / L"Game" / L"ME3" / L"BioGame" / L"CookedPCConsole" / L"MEVR_Bodies_LE3.pcc";
    std::error_code ec;
    return fs::is_regular_file(file, ec) ? file : fs::path();
}

void RemoveTree(fs::path const& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return;
    }
    for (auto const& e : fs::recursive_directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec)) {
            RemoveFileRetrying(e.path());
        }
    }
    fs::remove_all(dir, ec);
}

} // namespace

std::string CookedFile(GameId game, std::string_view file) {
    return std::string("Game/") + Spec(game).folder + "/BioGame/CookedPCConsole/" + std::string(file);
}

std::string TocName(std::string_view file) {
    return "BioGame\\CookedPCConsole\\" + std::string(file);
}

fs::path RigDir() {
    fs::path const local = LocalDataDir();
    return local.empty() ? fs::path() : local / L"rig";
}

fs::path GameDir(GameId game) {
    fs::path const rig = RigDir();
    return rig.empty() ? fs::path() : rig / detect::Wide(Spec(game).key);
}

fs::path StageDir(GameId game) {
    fs::path const rig = RigDir();
    return rig.empty() ? fs::path() : rig / (detect::Wide(Spec(game).key) + L".build");
}

fs::path LogFile(GameId game) {
    fs::path const rig = RigDir();
    return rig.empty() ? fs::path() : rig / (L"build-" + detect::Wide(Spec(game).key) + L".log");
}

std::optional<Result> ParseResult(std::string_view text, std::string_view game) {
    auto const v = json::Parse(text);
    if (!v || !v->IsObject() || v->String("format") != "mevr-rig-bodies" || v->Number("version", 0.0) != 1.0 || v->String("game") != game) {
        return std::nullopt;
    }
    Result r;
    r.game = std::string(game);
    for (json::Value const& p : v->Get("packages").Items()) {
        if (!p.IsObject()) {
            return std::nullopt;
        }
        Package pkg{p.String("file"), static_cast<std::uint64_t>(p.Number("size", 0.0))};
        if (!ValidPackageName(pkg.file) || pkg.size == 0 || pkg.size > 0xFFFFFFFFull) {
            return std::nullopt;
        }
        for (Package const& seen : r.packages) {
            if (Lower(seen.file) == Lower(pkg.file)) {
                return std::nullopt;
            }
        }
        r.packages.push_back(std::move(pkg));
    }
    r.bodies = static_cast<int>(v->Number("bodies", 0.0));
    r.failed = static_cast<int>(v->Number("failed", 0.0));
    r.seconds = static_cast<int>(v->Number("seconds", 0.0));
    return r;
}

std::optional<ProgressLine> ParseProgress(std::string_view line) {
    constexpr std::string_view kHead = "progress ";
    if (line.size() <= kHead.size() || line.substr(0, kHead.size()) != kHead) {
        return std::nullopt;
    }
    std::size_t at = kHead.size();
    int percent = 0;
    std::size_t digits = 0;
    while (at < line.size() && std::isdigit(static_cast<unsigned char>(line[at])) && digits < 3) {
        percent = percent * 10 + (line[at] - '0');
        ++at;
        ++digits;
    }
    if (digits == 0 || percent > 100 || (at < line.size() && line[at] != ' ')) {
        return std::nullopt;
    }
    ProgressLine p;
    p.percent = percent;
    p.text = at < line.size() ? std::string(line.substr(at + 1)) : std::string();
    return p;
}

std::wstring CommandLine(fs::path const& exe, GameId game, fs::path const& gameRoot, fs::path const& out, fs::path const& reference) {
    std::wstring c = packs::QuoteArg(exe.wstring()) + L" bodies --game " + detect::Wide(Spec(game).key) + L" --game-root " +
        packs::QuoteArg(gameRoot.wstring()) + L" --out " + packs::QuoteArg(out.wstring());
    if (!reference.empty()) {
        c += L" --ref " + packs::QuoteArg(reference.wstring());
    }
    return c;
}

std::string Recipe(fs::path const& rigDir) {
    std::error_code ec;
    if (!fs::exists(rigDir / L"mevr-rig.exe", ec)) {
        return {};
    }
    std::vector<fs::path> files;
    for (wchar_t const* name : {L"mevr-rig.exe", L"mevr-rig.dll", L"LegendaryExplorerCore.dll"}) {
        if (fs::exists(rigDir / name, ec)) {
            files.push_back(rigDir / name);
        }
    }
    std::vector<fs::path> plans;
    for (auto const& e : fs::directory_iterator(rigDir / L"plans", ec)) {
        if (e.is_regular_file(ec)) {
            plans.push_back(e.path());
        }
    }
    std::sort(plans.begin(), plans.end());
    files.insert(files.end(), plans.begin(), plans.end());
    std::string all;
    for (fs::path const& f : files) {
        all += detect::Utf8(f.lexically_relative(rigDir).generic_wstring()) + ":" + Sha256File(f) + "\n";
    }
    return Sha256(all);
}

State ReadState(Layout const& layout) {
    Transaction tx(layout);
    tx.Load();
    return StateOf(tx);
}

bool BuildIn(Transaction& tx, Payload const& payload, bool force, ProgressFn const& progress, std::atomic<bool> const* cancel) {
    Layout const& layout = tx.Where();
    GameSpec const& spec = layout.Game();
    std::string const name = spec.name;
    if (!payload.HasRig()) {
        tx.Note("this release has no body builder (payload\\rig): no moving bodies and hair");
        return false;
    }
    if (RigDir().empty()) {
        tx.Fail("no %LOCALAPPDATA% folder: the moving bodies can't be built");
        return false;
    }
    std::string const recipe = Recipe(payload.RigDir());
    State const before = StateOf(tx);
    if (!force && before.Built() && before.damaged.empty() && before.indexThere && before.recipe == recipe) {
        tx.Note("the moving bodies for " + name + " are built already from this release's game-file recipe: kept");
        return true;
    }
    std::string why;
    if (!TocEditable(layout, &why)) {
        tx.Fail(why + ": the moving bodies weren't built (verify the game's files in Steam or the EA app, then Repair)");
        return false;
    }
    fs::path const stage = StageDir(layout.game);
    fs::path const log = LogFile(layout.game);
    RemoveTree(stage);
    std::error_code ec;
    fs::create_directories(stage, ec);
    if (ec) {
        tx.Fail("couldn't create " + detect::Utf8(stage.wstring()) + ": " + ec.message());
        return false;
    }
    {
        std::ofstream head(log, std::ios::binary | std::ios::trunc);
        head << "MEVR Setup " << NowIso() << ": building " << spec.key << "'s moving bodies from " << detect::Utf8(layout.root.wstring()) << "\n";
    }
    fs::path const reference = Me3Reference(layout);
    std::wstring const command = CommandLine(payload.RigExe(), layout.game, layout.root, stage, reference);
    if (progress) {
        progress(Step{"Building the moving bodies from your " + name + " files", 0});
    }
    tx.Note("building the moving bodies and hair from your " + name + " files (below-normal priority; log " + detect::Utf8(log.wstring()) + ")");
    auto const started = std::chrono::steady_clock::now();
    int const code = RunBuilder(command, payload.RigDir(), log, progress, cancel);
    int const seconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count());
    if (code == -2) {
        RemoveTree(stage);
        tx.Fail("the moving bodies' build was stopped: build them later on Optional content (the mod runs without them)");
        return false;
    }
    auto const resultText = ReadFile(stage / detect::Wide(kResultFile));
    auto const result = resultText ? ParseResult(*resultText, spec.key) : std::nullopt;
    if (code == -1 || !result || result->packages.empty()) {
        RemoveTree(stage);
        tx.Fail(std::format("the body builder failed (exit {}): the mod runs without moving bodies; see {}", code, detect::Utf8(log.wstring())));
        return false;
    }
    for (Package const& p : result->packages) {
        if (fs::file_size(stage / detect::Wide(p.file), ec) != p.size || ec) {
            RemoveTree(stage);
            tx.Fail("the body builder's " + p.file + " isn't the size it says: nothing installed; see " + detect::Utf8(log.wstring()));
            return false;
        }
    }
    // What an earlier build put in that this one doesn't make: out, with its TOC entry.
    std::string const tocFile = TocFile(layout.game);
    bool ok = true;
    for (std::string const& old : Recorded(tx.Data())) {
        bool const kept = std::any_of(result->packages.begin(), result->packages.end(), [&](Package const& p) { return Lower(p.file) == Lower(old); });
        if (!kept) {
            ok = tx.RemoveTocEntry(tocFile, TocName(old)) && ok;
            ok = tx.RemoveContent(std::string(kContentPrefix) + old) && ok;
        }
    }
    for (Package const& p : result->packages) {
        std::string const rel = CookedFile(layout.game, p.file);
        if (fs::exists(layout.Full(rel), ec) && tx.Data().Find(rel) == nullptr) {
            // A package of ours there without a record (a lost install record, or tools/rig/install.ps1's development install):
            // ours to replace, and to take out on uninstall, not a file of the game's to back up.
            ok = tx.AdoptAdded(rel) && tx.AdoptTocEntry(tocFile, TocName(p.file)) && ok;
        }
        fs::path const built = stage / detect::Wide(p.file);
        std::string const sha = Sha256File(built);
        ok = ok && tx.PlaceIn(built, rel, sha, std::string(kContentPrefix) + p.file) &&
            tx.SetTocEntry(tocFile, TocName(p.file), static_cast<std::uint32_t>(p.size));
    }
    // The index, the body spheres and the result beside them in rig\<GAME>, where game/Hair reads them.
    fs::path const dir = GameDir(layout.game);
    fs::create_directories(dir, ec);
    RemoveTree(dir / L"colliders");
    for (auto const& e : fs::directory_iterator(stage, ec)) {
        fs::path const to = dir / e.path().filename();
        if (!MoveFileRetrying(e.path(), to)) {
            tx.Fail("couldn't move " + detect::Utf8(e.path().wstring()) + " to " + detect::Utf8(to.wstring()) + " (error " + std::to_string(GetLastError()) + ")");
            ok = false;
        }
    }
    json::Value setup = json::Value::Object();
    setup["recipe"] = json::Value(recipe);
    setup["game"] = json::Value(spec.key);
    setup["builtAt"] = json::Value(NowIso());
    setup["seconds"] = json::Value(seconds);
    if (!WriteFileAtomic(dir / detect::Wide(kRecipeFile), json::Write(setup))) {
        tx.Note("warning: couldn't write " + detect::Utf8((dir / detect::Wide(kRecipeFile)).wstring()) + ": the next install builds them again");
    }
    RemoveTree(stage);
    std::string files;
    for (Package const& p : result->packages) {
        files += (files.empty() ? "" : ", ") + p.file;
    }
    tx.Note(std::format("built {} moving bodies for {} in {} s ({}){}", result->bodies, name, seconds, files,
        result->failed > 0 ? std::format("; {} of the game's bodies couldn't be rigged and stay as they are", result->failed) : std::string()));
    if (progress) {
        progress(Step{"The moving bodies are in", 100});
    }
    return ok;
}

bool RepairIn(Transaction& tx, std::optional<Payload> const& payload, ProgressFn const& progress, std::atomic<bool> const* cancel) {
    Layout const& layout = tx.Where();
    State const state = StateOf(tx);
    if (!state.Built()) {
        return true;  // never built here (skipped at install): nothing to repair
    }
    std::vector<std::string> const missing = tx.VerifyContent(kContent, false);
    if (!missing.empty() || !state.indexThere) {
        if (!payload || !payload->HasRig()) {
            tx.Fail("the moving bodies are damaged and this copy has no body builder: run the downloaded release's MEVR Setup");
            return false;
        }
        tx.Note(missing.empty() ? std::string("the moving bodies' index in %LOCALAPPDATA% is gone: building them again")
                                : "a moving bodies package is missing or changed (" + missing.front() + "): building them again");
        return BuildIn(tx, *payload, true, progress, cancel);
    }
    // After "verify files" the game's own TOC is back: each package's entry again.
    bool ok = true;
    for (std::string const& p : state.packages) {
        std::error_code ec;
        std::uintmax_t const size = fs::file_size(layout.Full(CookedFile(layout.game, p)), ec);
        if (!ec) {
            ok = tx.SetTocEntry(TocFile(layout.game), TocName(p), static_cast<std::uint32_t>(size)) && ok;
        }
    }
    return ok;
}

bool RemoveIn(Transaction& tx) {
    Layout const& layout = tx.Where();
    bool ok = true;
    for (std::string const& p : Recorded(tx.Data())) {
        ok = tx.RemoveTocEntry(TocFile(layout.game), TocName(p)) && ok;
    }
    ok = tx.RemoveContent(kContent) && ok;
    // What game/Hair reads goes too, so it never asks for a package that isn't there (rebuilt by Build).
    fs::path const dir = GameDir(layout.game);
    if (!dir.empty()) {
        RemoveTree(dir / L"colliders");
        for (std::string const& f : {IndexName(layout.game), std::string("MEVR_Miranda.json"), std::string(kResultFile), std::string(kRecipeFile)}) {
            RemoveFileRetrying(dir / detect::Wide(f));
        }
    }
    tx.Note("took the moving bodies out of " + std::string(layout.Game().name));
    return ok;
}

namespace {
Outcome Done(Transaction const& tx, bool ok) {
    Outcome o;
    o.ok = ok && tx.Errors().empty();
    o.log = tx.Log();
    o.errors = tx.Errors();
    return o;
}

Outcome Refused(std::string why) {
    Outcome o;
    o.errors.push_back(why);
    o.log.push_back("error: " + why);
    return o;
}

std::optional<Outcome> Check(Layout const& layout) {
    if (!detect::HasGame(layout.root, layout.game)) {
        return Refused(std::string(layout.Game().name) + " isn't in " + detect::Utf8(layout.root.wstring()));
    }
    if (detect::GameRunningIn(layout.root, layout.game)) {
        return Refused("Mass Effect (or its launcher) is running: close it first");
    }
    return std::nullopt;
}
} // namespace

Outcome Build(Layout const& layout, Payload const& payload, ProgressFn const& progress, std::atomic<bool> const* cancel) {
    if (auto refused = Check(layout)) {
        return *refused;
    }
    Transaction tx(layout);
    if (!tx.Load()) {
        return Done(tx, false);
    }
    if (!tx.Exists() || (tx.Data().modVersion.empty() && tx.Data().Find(layout.Paths().modAsi) == nullptr)) {
        return Refused("Install the mod first: the moving bodies are part of it");
    }
    return Done(tx, BuildIn(tx, payload, true, progress, cancel));
}

Outcome Remove(Layout const& layout) {
    if (auto refused = Check(layout)) {
        return *refused;
    }
    Transaction tx(layout);
    if (!tx.Load()) {
        return Done(tx, false);
    }
    return Done(tx, RemoveIn(tx));
}

} // namespace mevr::install::bodies
