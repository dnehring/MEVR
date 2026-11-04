// SPDX-License-Identifier: MIT
#include "install/Manifest.hpp"

#include "install/Detect.hpp"
#include "install/Toc.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

namespace mevr::install {
namespace {

namespace fs = std::filesystem;

std::string ToUtf8(std::wstring const& w) {
    if (w.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(std::string_view s) {
    if (s.empty()) {
        return {};
    }
    int const n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

json::Value EditsToJson(std::vector<ini::Edit> const& edits) {
    json::Value array = json::Value::Array();
    for (ini::Edit const& e : edits) {
        json::Value v = json::Value::Object();
        v["section"] = e.section;
        v["key"] = e.key;
        v["before"] = e.before ? json::Value(*e.before) : json::Value();
        v["after"] = e.after;
        if (e.occurrence != 0) {
            v["occurrence"] = e.occurrence;
        }
        array.Push(std::move(v));
    }
    return array;
}

std::string Hex(unsigned char const* bytes, std::size_t n) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out += kDigits[bytes[i] >> 4];
        out += kDigits[bytes[i] & 15];
    }
    return out;
}

// SHA-256 over a stream of chunks (Windows CNG).
class Hasher {
public:
    Hasher() {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
            ok_ = BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0) == 0;
        }
    }
    ~Hasher() {
        if (hash_ != nullptr) {
            BCryptDestroyHash(hash_);
        }
        if (alg_ != nullptr) {
            BCryptCloseAlgorithmProvider(alg_, 0);
        }
    }
    Hasher(Hasher const&) = delete;
    Hasher& operator=(Hasher const&) = delete;
    void Add(void const* data, std::size_t n) {
        ok_ = ok_ && BCryptHashData(hash_, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(n), 0) == 0;
    }
    std::string Finish() {
        unsigned char digest[32]{};
        if (!ok_ || BCryptFinishHash(hash_, digest, sizeof digest, 0) != 0) {
            return {};
        }
        return Hex(digest, sizeof digest);
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    bool ok_ = false;
};

// Why a write may have failed, for the message.
std::string WriteFailureHint(fs::path const& file) {
    if (detect::ReadOnlyFile(file)) {
        return " (it's read-only: untick Read-only in its Properties)";
    }
    return " (is the game running, or is the folder read-only?)";
}

} // namespace

std::string Sha256(std::string_view bytes) {
    Hasher h;
    h.Add(bytes.data(), bytes.size());
    return h.Finish();
}

std::string Sha256File(fs::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    Hasher h;
    std::vector<char> buffer(1 << 16);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize const got = in.gcount();
        if (got > 0) {
            h.Add(buffer.data(), static_cast<std::size_t>(got));
        }
    }
    return h.Finish();
}

std::optional<std::string> ReadFile(fs::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

bool WriteFileAtomic(fs::path const& file, std::string_view bytes) {
    fs::path temp = file;
    temp += L".mevr-writing";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

std::string NowIso() {
    std::time_t const t = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%S", &local);
    return buffer;
}

// ---- Manifest ----------------------------------------------------------------------------------------------------

FileEntry* Manifest::Find(std::string_view path) {
    for (FileEntry& f : files) {
        if (_stricmp(f.path.c_str(), std::string(path).c_str()) == 0) {
            return &f;
        }
    }
    return nullptr;
}

FileEntry const* Manifest::Find(std::string_view path) const {
    return const_cast<Manifest*>(this)->Find(path);
}

json::Value Manifest::ToJson() const {
    json::Value v = json::Value::Object();
    v["schema"] = schema;
    v["game"] = game;
    v["modVersion"] = modVersion;
    v["setupVersion"] = setupVersion;
    v["installedAt"] = installedAt;
    v["loader"] = loader;
    v["gameSettings"] = gameSettings;
    if (pendingSettings) {
        json::Value p = json::Value::Object();
        p["width"] = pendingSettings->width;
        p["height"] = pendingSettings->height;
        p["vsyncOff"] = pendingSettings->vsyncOff;
        p["motionBlurOff"] = pendingSettings->motionBlurOff;
        p["filmGrainOff"] = pendingSettings->filmGrainOff;
        p["hdrOff"] = pendingSettings->hdrOff;
        v["pendingSettings"] = std::move(p);
    }
    json::Value fileArray = json::Value::Array();
    for (FileEntry const& f : files) {
        json::Value e = json::Value::Object();
        e["path"] = f.path;
        e["action"] = f.action;
        if (!f.backup.empty()) {
            e["backup"] = f.backup;
        }
        if (!f.from.empty()) {
            e["from"] = f.from;
        }
        if (!f.sha256.empty()) {
            e["sha256"] = f.sha256;
        }
        if (f.userData) {
            e["userData"] = true;
        }
        if (!f.edits.empty()) {
            e["edits"] = EditsToJson(f.edits);
        }
        if (!f.toc.empty()) {
            json::Value toc = json::Value::Array();
            for (TocEdit const& t : f.toc) {
                json::Value te = json::Value::Object();
                te["name"] = t.name;
                if (t.before) {
                    te["before"] = static_cast<double>(*t.before);
                }
                te["after"] = static_cast<double>(t.after);
                toc.Push(std::move(te));
            }
            e["toc"] = std::move(toc);
        }
        fileArray.Push(std::move(e));
    }
    v["files"] = std::move(fileArray);
    json::Value dirArray = json::Value::Array();
    for (std::string const& d : dirs) {
        dirArray.Push(d);
    }
    v["dirs"] = std::move(dirArray);
    json::Value packArray = json::Value::Array();
    for (PackRecord const& p : packs) {
        json::Value e = json::Value::Object();
        e["id"] = p.id;
        e["name"] = p.name;
        e["method"] = p.method;
        e["installedAt"] = p.installedAt;
        packArray.Push(std::move(e));
    }
    v["packs"] = std::move(packArray);
    return v;
}

std::optional<Manifest> Manifest::FromJson(json::Value const& v) {
    if (!v.IsObject() || v.Number("schema", 0) < 1) {
        return std::nullopt;
    }
    Manifest m;
    m.schema = static_cast<int>(v.Number("schema", 1));
    m.game = v.String("game", "LE1");
    m.modVersion = v.String("modVersion");
    m.setupVersion = v.String("setupVersion");
    m.installedAt = v.String("installedAt");
    m.loader = v.String("loader");
    m.gameSettings = v.Bool("gameSettings");
    if (json::Value const& p = v.Get("pendingSettings"); p.IsObject()) {
        PendingSettings s;
        s.width = static_cast<int>(p.Number("width", 0));
        s.height = static_cast<int>(p.Number("height", 0));
        s.vsyncOff = p.Bool("vsyncOff", true);
        s.motionBlurOff = p.Bool("motionBlurOff", true);
        s.filmGrainOff = p.Bool("filmGrainOff", true);
        s.hdrOff = p.Bool("hdrOff", true);
        m.pendingSettings = s;
    }
    for (json::Value const& e : v.Get("files").Items()) {
        FileEntry f;
        f.path = e.String("path");
        f.action = e.String("action");
        f.backup = e.String("backup");
        f.from = e.String("from");
        f.sha256 = e.String("sha256");
        f.userData = e.Bool("userData");
        for (json::Value const& ed : e.Get("edits").Items()) {
            ini::Edit edit;
            edit.section = ed.String("section");
            edit.key = ed.String("key");
            if (ed.Get("before").IsString()) {
                edit.before = ed.Get("before").AsString();
            }
            edit.after = ed.String("after");
            edit.occurrence = static_cast<int>(ed.Number("occurrence", 0));
            f.edits.push_back(std::move(edit));
        }
        for (json::Value const& te : e.Get("toc").Items()) {
            TocEdit t;
            t.name = te.String("name");
            if (te.Get("before").GetKind() == json::Value::Kind::Number) {
                t.before = static_cast<std::uint32_t>(te.Number("before", 0));
            }
            t.after = static_cast<std::uint32_t>(te.Number("after", 0));
            if (!t.name.empty()) {
                f.toc.push_back(std::move(t));
            }
        }
        if (f.path.empty() || f.action.empty()) {
            return std::nullopt;
        }
        m.files.push_back(std::move(f));
    }
    for (json::Value const& d : v.Get("dirs").Items()) {
        m.dirs.push_back(d.AsString());
    }
    for (json::Value const& p : v.Get("packs").Items()) {
        m.packs.push_back({p.String("id"), p.String("name"), p.String("method"), p.String("installedAt")});
    }
    return m;
}

// ---- Layout --------------------------------------------------------------------------------------------------------

std::string Layout::Relative(fs::path const& full) const {
    return ToUtf8(full.lexically_normal().lexically_relative(root.lexically_normal()).generic_wstring());
}

fs::path Layout::Full(std::string_view relative) const {
    return (root / fs::path(FromUtf8(relative))).lexically_normal();
}

// ---- Transaction ---------------------------------------------------------------------------------------------------

Transaction::Transaction(Layout layout) : layout_(std::move(layout)) {}

void Transaction::Fail(std::string message) {
    errors_.push_back(message);
    log_.push_back("error: " + message);
}

void Transaction::Note(std::string message) {
    log_.push_back(std::move(message));
}

bool Transaction::Load() {
    std::error_code ec;
    fs::path const file = layout_.ManifestFile();
    if (!fs::exists(file, ec)) {
        exists_ = false;
        return true;
    }
    auto const text = ReadFile(file);
    std::string error;
    auto const value = text ? json::Parse(*text, &error) : std::nullopt;
    auto const manifest = value ? Manifest::FromJson(*value) : std::nullopt;
    if (!manifest) {
        Fail("the install record " + ToUtf8(file.wstring()) + " can't be read (" + error + ")");
        return false;
    }
    manifest_ = *manifest;
    exists_ = true;
    return true;
}

bool Transaction::Save() {
    if (!EnsureParent(layout_.ManifestFile())) {
        return false;
    }
    if (!WriteFileAtomic(layout_.ManifestFile(), json::Write(manifest_.ToJson()))) {
        Fail("couldn't write " + ToUtf8(layout_.ManifestFile().wstring()));
        return false;
    }
    exists_ = true;
    return true;
}

bool Transaction::EnsureParent(fs::path const& file) {
    std::vector<fs::path> missing;
    for (fs::path dir = file.parent_path(); !dir.empty(); dir = dir.parent_path()) {
        std::error_code ec;
        if (fs::exists(dir, ec)) {
            break;
        }
        missing.push_back(dir);
        if (dir == dir.parent_path()) {
            break;
        }
    }
    for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
        std::error_code ec;
        if (!fs::create_directory(*it, ec) && ec) {
            Fail("couldn't create " + ToUtf8(it->wstring()) + ": " + ec.message());
            return false;
        }
        std::string const rel = layout_.Relative(*it);
        // The backup folder goes with the manifest; it isn't the player's.
        manifest_.dirs.push_back(rel);
    }
    return true;
}

bool Transaction::Backup(std::string_view relative, std::string& backupRelative) {
    std::string candidate(relative);
    std::error_code ec;
    for (int n = 1; fs::exists(layout_.BackupDir() / fs::path(FromUtf8(candidate)), ec); ++n) {
        candidate = std::string(relative) + "." + std::to_string(n);
    }
    fs::path const target = layout_.BackupDir() / fs::path(FromUtf8(candidate));
    fs::create_directories(target.parent_path(), ec);
    if (!CopyFileW(layout_.Full(relative).c_str(), target.c_str(), TRUE)) {
        Fail("couldn't back up " + std::string(relative) + " (error " + std::to_string(GetLastError()) + ")");
        return false;
    }
    backupRelative = candidate;
    return true;
}

bool Transaction::WriteIn(std::string_view relative, std::string_view bytes, bool userData) {
    fs::path const target = layout_.Full(relative);
    std::error_code ec;
    FileEntry* entry = manifest_.Find(relative);
    FileEntry fresh;
    if (entry == nullptr) {
        fresh.path = std::string(relative);
        fresh.userData = userData;
        if (fs::exists(target, ec)) {
            fresh.action = "replaced";
            if (!Backup(relative, fresh.backup)) {
                return false;
            }
        } else {
            fresh.action = "added";
            if (!EnsureParent(target)) {
                return false;
            }
        }
    }
    if (!WriteFileAtomic(target, bytes)) {
        Fail("couldn't write " + std::string(relative) + WriteFailureHint(target));
        if (entry == nullptr) {
            // Nothing changed, but a backup may have been taken: keep it on record so it's cleaned up.
            if (!fresh.backup.empty()) {
                fs::remove(layout_.BackupDir() / fs::path(FromUtf8(fresh.backup)), ec);
            }
        }
        return false;
    }
    if (entry == nullptr) {
        manifest_.files.push_back(fresh);
        entry = &manifest_.files.back();
        Note(fresh.action + " " + std::string(relative));
    } else {
        Note("updated " + std::string(relative));
        entry->edits.clear();  // the whole file is ours now: key edits recorded on an earlier one no longer apply
    }
    entry->sha256 = Sha256(bytes);
    return Save();
}

bool Transaction::CopyIn(fs::path const& source, std::string_view relative, bool userData) {
    auto const bytes = ReadFile(source);
    if (!bytes) {
        Fail("missing from the download: " + ToUtf8(source.wstring()));
        return false;
    }
    return WriteIn(relative, *bytes, userData);
}

bool Transaction::WriteIfMissing(std::string_view relative, std::string_view bytes, bool userData) {
    std::error_code ec;
    if (fs::exists(layout_.Full(relative), ec)) {
        Note("kept the existing " + std::string(relative));
        return true;
    }
    return WriteIn(relative, bytes, userData);
}

bool Transaction::Rename(std::string_view from, std::string_view to) {
    fs::path const source = layout_.Full(from);
    fs::path const target = layout_.Full(to);
    std::error_code ec;
    if (!fs::exists(source, ec)) {
        Fail("nothing to rename: " + std::string(from));
        return false;
    }
    FileEntry entry;
    entry.path = std::string(to);
    entry.action = "renamed";
    entry.from = std::string(from);
    if (fs::exists(target, ec)) {
        if (!Backup(to, entry.backup)) {
            return false;
        }
        fs::remove(target, ec);
    }
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        Fail("couldn't rename " + std::string(from) + " (error " + std::to_string(GetLastError()) + ")");
        return false;
    }
    manifest_.files.push_back(entry);
    Note("renamed " + std::string(from) + " to " + std::string(to));
    return Save();
}

bool Transaction::EditIni(std::string_view relative, std::function<std::string(std::string_view, std::vector<ini::Edit>*)> const& edit) {
    fs::path const target = layout_.Full(relative);
    auto const text = ReadFile(target);
    if (!text) {
        Fail("not found: " + std::string(relative));
        return false;
    }
    FileEntry* entry = manifest_.Find(relative);
    bool const ours = entry != nullptr && (entry->action == "added" || entry->action == "replaced");
    // A file of the player's that was there before us and is kept on record as their data (Halcyon's MELEVR.ini, from an
    // earlier install or copied from upstream's MELE-VR; RecordRuntimeFile: no hash, as we never wrote it): every value we
    // change in it is recorded too, so an uninstall that keeps it puts back each one still as we set it. It keeps no hash.
    bool const preexisting = ours && entry->userData && entry->sha256.empty();
    bool const record = !ours || preexisting;
    std::vector<ini::Edit> edits;
    std::string const changed = edit(*text, record ? &edits : nullptr);
    if (changed == *text) {
        return true;
    }
    if (!WriteFileAtomic(target, changed)) {
        Fail("couldn't write " + std::string(relative) + WriteFailureHint(target));
        return false;
    }
    if (preexisting) {
        for (ini::Edit const& e : edits) {
            ini::Merge(entry->edits, e);
        }
    } else if (ours) {
        entry->sha256 = Sha256(changed);
    } else {
        if (entry == nullptr) {
            FileEntry fresh;
            fresh.path = std::string(relative);
            fresh.action = "edited";
            manifest_.files.push_back(fresh);
            entry = &manifest_.files.back();
        }
        for (ini::Edit const& e : edits) {
            ini::Merge(entry->edits, e);
        }
    }
    Note("edited " + std::string(relative) + " (" + std::to_string(edits.size()) + " values)");
    return Save();
}

bool Transaction::SetTocEntry(std::string_view relative, std::string_view name, std::uint32_t size) {
    fs::path const target = layout_.Full(relative);
    auto const bytes = ReadFile(target);
    auto file = bytes ? toc::Parse(*bytes) : std::nullopt;
    if (!file) {
        Fail(std::string(relative) + " is missing or not a TOC: verify the game's files in Steam or the EA app");
        return false;
    }
    if (!toc::Consistent(*file)) {
        Fail(std::string(relative) + " isn't laid out as this setup expects: left as it is (the cockpit won't load)");
        return false;
    }
    std::optional<std::uint32_t> const before = toc::Set(*file, name, size);
    if (before && *before == size) {
        Note(std::string(name) + " is already in " + std::string(relative));
    } else if (!WriteFileAtomic(target, toc::Serialize(*file))) {
        Fail("couldn't write " + std::string(relative) + WriteFailureHint(target));
        return false;
    } else {
        Note((before ? "updated " : "added ") + std::string(name) + " in " + std::string(relative));
    }
    FileEntry* entry = manifest_.Find(relative);
    if (entry == nullptr) {
        FileEntry fresh;
        fresh.path = std::string(relative);
        fresh.action = "toc";
        manifest_.files.push_back(fresh);
        entry = &manifest_.files.back();
    }
    auto const it = std::find_if(entry->toc.begin(), entry->toc.end(), [&](TocEdit const& t) { return t.name == name; });
    if (it == entry->toc.end()) {
        entry->toc.push_back(TocEdit{std::string(name), before, size});  // the first `before` is the game's own
    } else {
        it->after = size;
    }
    return Save();
}

bool Transaction::AdoptTocEntry(std::string_view relative, std::string_view name) {
    FileEntry* entry = manifest_.Find(relative);
    if (entry != nullptr && std::any_of(entry->toc.begin(), entry->toc.end(), [&](TocEdit const& t) { return t.name == name; })) {
        return true;
    }
    auto const bytes = ReadFile(layout_.Full(relative));
    auto const file = bytes ? toc::Parse(*bytes) : std::nullopt;
    std::optional<std::uint32_t> const size = file ? toc::Find(*file, name) : std::nullopt;
    if (!size) {
        return true;  // not there: nothing to adopt
    }
    if (entry == nullptr) {
        FileEntry fresh;
        fresh.path = std::string(relative);
        fresh.action = "toc";
        manifest_.files.push_back(fresh);
        entry = &manifest_.files.back();
    }
    entry->toc.push_back(TocEdit{std::string(name), std::nullopt, *size});
    Note("recorded " + std::string(name) + " in " + std::string(relative) + " as ours (no install record was left)");
    return Save();
}

bool Transaction::RevertIni(std::string_view relative) {
    FileEntry* entry = manifest_.Find(relative);
    if (entry == nullptr || entry->action != "edited") {
        return true;
    }
    fs::path const target = layout_.Full(relative);
    if (auto const text = ReadFile(target)) {
        std::string const reverted = ini::Revert(*text, entry->edits);
        if (reverted != *text && !WriteFileAtomic(target, reverted)) {
            Fail("couldn't write " + std::string(relative));
            return false;
        }
    }
    Note("restored " + std::string(relative));
    for (auto it = manifest_.files.begin(); it != manifest_.files.end(); ++it) {
        if (&*it == entry) {
            manifest_.files.erase(it);
            break;
        }
    }
    return Save();
}

bool Transaction::RecordRuntimeFile(std::string_view relative) {
    if (manifest_.Find(relative) != nullptr) {
        return true;
    }
    FileEntry entry;
    entry.path = std::string(relative);
    entry.action = "added";
    entry.userData = true;
    manifest_.files.push_back(entry);
    Note("recorded " + std::string(relative) + " (the mod's own settings or log)");
    return Save();
}

void Transaction::RecordDir(std::string_view relative) {
    for (std::string const& d : manifest_.dirs) {
        if (_stricmp(d.c_str(), std::string(relative).c_str()) == 0) {
            return;
        }
    }
    manifest_.dirs.push_back(std::string(relative));
}

bool Transaction::AdoptRenamed(std::string_view from, std::string_view to) {
    std::error_code ec;
    if (!fs::exists(layout_.Full(to), ec)) {
        Fail("nothing to adopt: " + std::string(to));
        return false;
    }
    FileEntry entry;
    entry.path = std::string(to);
    entry.action = "renamed";
    entry.from = std::string(from);
    manifest_.files.push_back(entry);
    Note("recorded " + std::string(to) + " as renamed from " + std::string(from) + " (no install record was left)");
    return Save();
}

bool Transaction::AdoptAdded(std::string_view relative) {
    if (manifest_.Find(relative) != nullptr) {
        return true;
    }
    FileEntry entry;
    entry.path = std::string(relative);
    entry.action = "added";
    entry.sha256 = Sha256File(layout_.Full(relative));
    manifest_.files.push_back(entry);
    Note("recorded " + std::string(relative) + " as ours");
    return Save();
}

void Transaction::Forget(std::string_view relative) {
    std::erase_if(manifest_.files, [&](FileEntry const& f) { return _stricmp(f.path.c_str(), std::string(relative).c_str()) == 0; });
}

bool Transaction::Uninstall(bool keepUserData) {
    std::error_code ec;
    bool ok = true;
    // What has been undone: dropped from the record if something else fails, so another try never undoes it twice
    // (removing bink2w64.dll a second time would delete RAD's DLL, renamed back by the first try).
    std::vector<bool> undone(manifest_.files.size(), false);
    for (std::size_t n = manifest_.files.size(); n-- > 0;) {
        FileEntry const& f = manifest_.files[n];
        fs::path const target = layout_.Full(f.path);
        if (f.action == "added") {
            if (f.userData && keepUserData) {
                // Kept, with what we changed in it put back (each value still as we set it; the player's since stay).
                if (!f.edits.empty()) {
                    if (auto const text = ReadFile(target)) {
                        std::string const reverted = ini::Revert(*text, f.edits);
                        if (reverted != *text && !WriteFileAtomic(target, reverted)) {
                            Fail("couldn't restore the values in " + f.path);
                            ok = false;
                            continue;
                        }
                    }
                    Note("kept " + f.path + " (the values the setup changed in it put back)");
                } else {
                    Note("kept " + f.path);
                }
                continue;  // still on record if something else fails (a retry may be a full uninstall); Revert is idempotent
            }
            if (fs::exists(target, ec) && !fs::remove(target, ec)) {
                Fail("couldn't remove " + f.path + ": " + ec.message());
                ok = false;
                continue;
            }
            Note("removed " + f.path);
        } else if (f.action == "replaced") {
            if (f.userData && keepUserData) {
                Note("kept " + f.path);
                continue;
            }
            fs::path const backup = layout_.BackupDir() / fs::path(FromUtf8(f.backup));
            if (!CopyFileW(backup.c_str(), target.c_str(), FALSE)) {
                Fail("couldn't restore " + f.path + " (error " + std::to_string(GetLastError()) + ")");
                ok = false;
                continue;
            }
            Note("restored " + f.path);
        } else if (f.action == "renamed") {
            fs::path const original = layout_.Full(f.from);
            if (fs::exists(original, ec)) {
                Note(f.from + " is already back; left " + f.path);
            } else if (f.path != paths::kBinkOriginal && !fs::exists(target, ec)) {
                Note(f.path + " is gone: nothing to rename back to " + f.from);
            } else if (!MoveFileExW(target.c_str(), original.c_str(), MOVEFILE_WRITE_THROUGH)) {
                Fail("couldn't rename " + f.path + " back to " + f.from + " (error " + std::to_string(GetLastError()) + ")");
                ok = false;
                continue;
            } else {
                Note("renamed " + f.path + " back to " + f.from);
            }
            if (!f.backup.empty()) {
                fs::path const backup = layout_.BackupDir() / fs::path(FromUtf8(f.backup));
                if (!CopyFileW(backup.c_str(), target.c_str(), FALSE)) {
                    Fail("couldn't restore " + f.path);
                    ok = false;
                    continue;
                }
            }
        } else if (f.action == "toc") {
            auto const bytes = ReadFile(target);
            auto file = bytes ? toc::Parse(*bytes) : std::nullopt;
            if (!file) {
                Note(f.path + " is missing or unreadable: no entries to take out");
            } else {
                for (TocEdit const& t : f.toc) {
                    if (t.before) {
                        toc::Set(*file, t.name, *t.before);
                    } else {
                        toc::Remove(*file, t.name);
                    }
                }
                if (!WriteFileAtomic(target, toc::Serialize(*file))) {
                    Fail("couldn't take our entries out of " + f.path);
                    ok = false;
                    continue;
                }
                Note("took our entries out of " + f.path);
            }
        } else if (f.action == "edited") {
            if (auto const text = ReadFile(target)) {
                std::string const reverted = ini::Revert(*text, f.edits);
                if (reverted != *text && !WriteFileAtomic(target, reverted)) {
                    Fail("couldn't restore the values in " + f.path);
                    ok = false;
                    continue;
                }
                Note("restored the values in " + f.path);
            }
        }
        undone[n] = true;
    }
    if (!ok) {
        // What's left stays on record for another try.
        std::vector<FileEntry> left;
        for (std::size_t n = 0; n < manifest_.files.size(); ++n) {
            if (!undone[n]) {
                left.push_back(manifest_.files[n]);
            }
        }
        manifest_.files = std::move(left);
        Save();
        return false;
    }
    fs::remove_all(layout_.BackupDir(), ec);
    fs::remove(layout_.ManifestFile(), ec);
    for (auto it = manifest_.dirs.rbegin(); it != manifest_.dirs.rend(); ++it) {
        fs::path const dir = layout_.Full(*it);
        if (fs::is_directory(dir, ec) && fs::is_empty(dir, ec)) {
            fs::remove(dir, ec);
        }
    }
    manifest_ = Manifest{};
    exists_ = false;
    Note("uninstalled");
    return true;
}

std::vector<std::string> Transaction::Verify() const {
    std::vector<std::string> bad;
    std::error_code ec;
    for (FileEntry const& f : manifest_.files) {
        fs::path const target = layout_.Full(f.path);
        if (f.action == "renamed") {
            // The game's own Bink DLL behind our loader; a file moved aside (Halcyon's upstream DLLs) isn't needed.
            if (f.path == paths::kBinkOriginal && !fs::exists(target, ec)) {
                bad.push_back(f.path);
            }
        } else if (f.action == "toc") {
            auto const bytes = ReadFile(target);
            auto const file = bytes ? toc::Parse(*bytes) : std::nullopt;
            for (TocEdit const& t : f.toc) {
                if (!file || toc::Find(*file, t.name) != std::optional<std::uint32_t>(t.after)) {
                    bad.push_back(f.path);
                    break;
                }
            }
        } else if ((f.action == "added" || f.action == "replaced") && !f.userData && !f.sha256.empty()) {
            if (Sha256File(target) != f.sha256) {
                bad.push_back(f.path);
            }
        }
    }
    return bad;
}

} // namespace mevr::install
