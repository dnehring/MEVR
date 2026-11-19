// SPDX-License-Identifier: MIT
#pragma once

// One settings file for the trilogy (research/25 section 20; the owner, 2026-11-18: "I assume the settings for 2 would carry
// into me3", then "Yes, share them"). The host and the mod of every game read and write %LOCALAPPDATA%\MEVR\settings.ini:
//
//   reads    [<section>.<GAME>] first, then [<section>] (Get/Merged/Load over a parsed file; Int/String/Section with the
//            Win32 profile API's own signatures, so a call site changes only its function's name);
//   writes   per-game keys (core/vr/SettingsKeys PerGame) to [<section>.<GAME>]; every other key to [<section>], taken out
//            of the three games' sections at the same time, so a change in one game is a change in the three. One write is
//            a copy of the file (<file>.<pid>-<tid>.tmp) changed through Win32's profile API and moved over it, under a
//            named mutex every writer of the session shares (the host's threads, the mod's, MEVR Setup);
//   in use   the shared file, unless it is there and can't be read: then the game's old file for the session, and the shared
//            file is never written (InUse; the host decides once and hands the answer to the mod in MEVR_SETTINGS_FILE);
//   taken    Sync, at host attach before the mod loads and every 2 s after: the three games' old ASI\LE{n}MEVR.ini made the
//            shared file once (ME1's the base; keys only ME2's or ME3's had added; where a value differs, the base's kept for
//            the three and the game's own in its section, each logged), each old file marked [Settings] shared = 1 and kept
//            for an older build; after that a key changed in this game's old file applies to this game ([<section>.<GAME>]).
//
// Header-only, as core/vr/CalibrationFile: every target that compiles core/Ini (the hosts, the mods, the tests) uses it.

#include "core/Ini.hpp"
#include "core/vr/CalibrationFile.hpp"
#include "core/vr/SettingsKeys.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mevr::vr::settings_file {

using calibration_file::Access;
using calibration_file::Narrow;
using calibration_file::Probe;
using calibration_file::Wide;

// ---- What never comes into the shared settings file ------------------------------------------------------------------

// The calibration's own sections ([Calibration.<key>], [ManualCalibration.<key>]: calibration.ini since research/25 section
// 17) and the old files' marker [Settings].
inline bool ExcludedSection(std::string_view section) {
    if (Same(section, kMarkerSection)) {
        return true;
    }
    if (auto const game = SplitGameSection(section)) {
        return false;  // [Calibration.LE1] is this file's own: the calibration's per-game settings
    }
    return calibration_file::SharedSection(section);
}

inline bool Excluded(std::string_view section, std::string_view key) {
    return ExcludedSection(section) || calibration_file::Shared(section, key) ||
           (Same(section, "Calibration") && Same(key, "shared"));
}

// ---- Reads over a parsed file ------------------------------------------------------------------------------------------

// A key for one game: [<section>.<game>] first, then [<section>].
inline std::optional<std::string> Get(ini::File const& file, std::string_view game, std::string_view section, std::string_view key) {
    if (!game.empty()) {
        if (auto v = ini::Get(file, GameSectionName(section, game), key)) {
            return v;
        }
    }
    return ini::Get(file, section, key);
}

// The file as one game sees it: every common section with that game's section's keys on top; the games' sections themselves
// are gone (this game's merged in, the others' left out). A file without games' sections comes back as it was.
inline ini::File Merged(ini::File const& file, std::string_view game) {
    ini::File out;
    for (auto const& [name, section] : file) {
        if (!SplitGameSection(name)) {
            out.emplace(name, section);
        }
    }
    if (game.empty()) {
        return out;
    }
    for (auto const& [name, section] : file) {
        if (auto const g = SplitGameSection(name); g && Same(g->game, game)) {
            ini::Section& target = out[g->base];
            for (auto const& [k, v] : section) {
                target.insert_or_assign(k, v);
            }
        }
    }
    return out;
}

// ini::Read with Merged on top: what the mod's modules read once a second.
inline ini::Loaded Load(std::filesystem::path const& path, std::string_view game = kThisGame) {
    ini::Loaded loaded = ini::Read(path);
    loaded.file = Merged(loaded.file, game);
    return loaded;
}

// ---- Reads with the Win32 profile API's signatures ---------------------------------------------------------------------

inline std::wstring GameSectionW(wchar_t const* section, std::string_view game) {
    return std::wstring(section) + L"." + Wide(game);
}

// Whether [section] has the key (Win32 hands back the default only for a missing one; this default is in no file).
inline bool HasKey(wchar_t const* section, wchar_t const* key, wchar_t const* path) {
    wchar_t probe[4]{};
    DWORD const n = GetPrivateProfileStringW(section, key, L"\x1\x2", probe, 4, path);
    return !(n == 2 && probe[0] == 1 && probe[1] == 2);
}

inline bool HasSection(wchar_t const* section, wchar_t const* path) {
    wchar_t probe[4]{};
    return GetPrivateProfileSectionW(section, probe, 4, path) > 0;
}

// Whether the file has a [<section>.<game>] at all: the sections' names read once per change of the file (its time and
// size), so a section without one costs a reader nothing but a look at the file's time (most sections have none).
inline bool HasGameSection(wchar_t const* section, wchar_t const* path, std::string_view game) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (game.empty() || path == nullptr || *path == L'\0' || !GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
        return false;
    }
    struct Cache {
        std::mutex mutex;
        std::wstring path;
        std::string game;
        FILETIME stamp{};
        DWORD sizeLow = 0, sizeHigh = 0;
        std::set<std::string> bases;  // lower-cased
    };
    static Cache cache;
    std::lock_guard const guard(cache.mutex);
    if (cache.path != path || cache.game != game || CompareFileTime(&cache.stamp, &data.ftLastWriteTime) != 0 ||
        cache.sizeLow != data.nFileSizeLow || cache.sizeHigh != data.nFileSizeHigh) {
        cache.path = path;
        cache.game = std::string(game);
        cache.stamp = data.ftLastWriteTime;
        cache.sizeLow = data.nFileSizeLow;
        cache.sizeHigh = data.nFileSizeHigh;
        cache.bases.clear();
        std::vector<wchar_t> names(65536);
        DWORD const n = GetPrivateProfileSectionNamesW(names.data(), static_cast<DWORD>(names.size()), path);
        for (std::size_t at = 0; at < n;) {
            std::wstring const name(names.data() + at);
            at += name.size() + 1;
            if (name.empty()) {
                break;
            }
            if (auto const g = SplitGameSection(Narrow(name)); g && Same(g->game, game)) {
                cache.bases.insert(Lowered(g->base));
            }
        }
    }
    return cache.bases.contains(Lowered(Narrow(section)));
}

// GetPrivateProfileIntW with this game's section first.
inline UINT Int(wchar_t const* section, wchar_t const* key, INT fallback, wchar_t const* path, std::string_view game = kThisGame) {
    if (HasGameSection(section, path, game)) {
        std::wstring const own = GameSectionW(section, game);
        if (HasKey(own.c_str(), key, path)) {
            return GetPrivateProfileIntW(own.c_str(), key, fallback, path);
        }
    }
    return GetPrivateProfileIntW(section, key, fallback, path);
}

// GetPrivateProfileStringW with this game's section first.
inline DWORD String(wchar_t const* section, wchar_t const* key, wchar_t const* fallback, wchar_t* out, DWORD size, wchar_t const* path,
    std::string_view game = kThisGame) {
    if (key != nullptr && HasGameSection(section, path, game)) {
        std::wstring const own = GameSectionW(section, game);
        if (HasKey(own.c_str(), key, path)) {
            return GetPrivateProfileStringW(own.c_str(), key, fallback, out, size, path);
        }
    }
    return GetPrivateProfileStringW(section, key, fallback, out, size, path);
}

// GetPrivateProfileSectionW with this game's section's keys on top: without one, Win32's own answer; with one, a "key=value"
// line per key (this game's value first, then the common keys it doesn't name), NUL-separated, a second NUL at the end.
inline DWORD Section(wchar_t const* section, wchar_t* out, DWORD size, wchar_t const* path, std::string_view game = kThisGame) {
    if (!HasGameSection(section, path, game)) {
        return GetPrivateProfileSectionW(section, out, size, path);
    }
    std::wstring const own = GameSectionW(section, game);
    std::vector<std::pair<std::wstring, std::wstring>> lines = calibration_file::SectionLines(path, Narrow(own));
    for (auto& [k, v] : calibration_file::SectionLines(path, Narrow(section))) {
        bool const named = std::any_of(lines.begin(), lines.end(), [&](auto const& kv) {
            return CompareStringOrdinal(kv.first.c_str(), -1, k.c_str(), -1, TRUE) == CSTR_EQUAL;
        });
        if (!named) {
            lines.emplace_back(std::move(k), std::move(v));
        }
    }
    if (size < 2) {
        if (size == 1) {
            out[0] = L'\0';
        }
        return 0;
    }
    DWORD at = 0;
    for (auto const& [k, v] : lines) {
        std::wstring const line = k + L"=" + v;
        if (at + line.size() + 2 > size) {
            out[size - 2] = L'\0';
            out[size - 1] = L'\0';
            return size - 2;  // Win32's answer for a buffer too small
        }
        std::copy(line.begin(), line.end(), out + at);
        at += static_cast<DWORD>(line.size());
        out[at++] = L'\0';
    }
    out[at] = L'\0';
    return at;
}

// ---- Writes --------------------------------------------------------------------------------------------------------------

// One change: a key's value, a key removed (no value), or the whole section removed (no key, no value).
struct Edit {
    std::wstring section;
    std::wstring key;
    std::optional<std::wstring> value;
};

inline Edit Set(std::string_view section, std::string_view key, std::string_view value) {
    return {Wide(section), Wide(key), Wide(value)};
}

inline Edit Remove(std::string_view section, std::string_view key) {
    return {Wide(section), Wide(key), std::nullopt};
}

// Where each edit goes in a shared file (pure, unit-tested): a per-game key into [<section>.<game>]; any other key into
// [<section>] and out of [<section>.LE1/.LE2/.LE3] wherever `current` (the file now) has it, so the three games read the
// change; an edit already aimed at a game's section stays as it is. No game (a tool): everything to [<section>].
inline std::vector<Edit> Route(std::vector<Edit> const& edits, std::string_view game, ini::File const& current) {
    std::vector<Edit> out;
    for (Edit const& e : edits) {
        std::string const section = Narrow(e.section);
        if (SplitGameSection(section)) {
            out.push_back(e);
            continue;
        }
        if (e.key.empty()) {  // the whole section, and the games' copies of it
            out.push_back(e);
            for (std::string_view const g : kGames) {
                if (current.contains(GameSectionName(section, g))) {
                    out.push_back({Wide(GameSectionName(section, g)), {}, std::nullopt});
                }
            }
            continue;
        }
        std::string const key = Narrow(e.key);
        if (!game.empty() && PerGame(section, key, e.value ? Narrow(*e.value) : std::string()) != nullptr) {
            out.push_back({Wide(GameSectionName(section, game)), e.key, e.value});
            continue;
        }
        out.push_back(e);
        for (std::string_view const g : kGames) {
            if (ini::Get(current, GameSectionName(section, g), key)) {
                out.push_back({Wide(GameSectionName(section, g)), e.key, std::nullopt});
            }
        }
    }
    return out;
}

// Every writer of a settings file in this session waits for the others (the host's Present and game threads, the mod, MEVR
// Setup): a write is read, copy, change, move, and two at once would lose one. Recursive for its thread (a Win32 mutex).
class WriteLock {
public:
    WriteLock() : handle_(CreateMutexW(nullptr, FALSE, L"Local\\MEVR.settings.write")) {
        if (handle_ != nullptr) {
            DWORD const r = WaitForSingleObject(handle_, 2000);
            held_ = r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
        }
    }
    ~WriteLock() {
        if (held_) {
            ReleaseMutex(handle_);
        }
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }
    WriteLock(WriteLock const&) = delete;
    WriteLock& operator=(WriteLock const&) = delete;
    bool Held() const noexcept { return held_; }

private:
    HANDLE handle_ = nullptr;
    bool held_ = false;
};

inline std::string ReadBytes(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::string();
}

// UTF-8 text as the bytes of a file in `encoding` (UTF-16 files get UTF-16; narrow ones the text as it is).
inline std::string Encoded(std::string_view text, ini::Encoding encoding) {
    if (!ini::IsUtf16(encoding)) {
        return std::string(text);
    }
    std::wstring const wide = Wide(text);
    std::string out;
    out.reserve(wide.size() * 2);
    for (wchar_t const c : wide) {
        unsigned const u = static_cast<unsigned>(c);
        if (encoding == ini::Encoding::Utf16Le) {
            out.push_back(static_cast<char>(u & 0xFF));
            out.push_back(static_cast<char>((u >> 8) & 0xFF));
        } else {
            out.push_back(static_cast<char>((u >> 8) & 0xFF));
            out.push_back(static_cast<char>(u & 0xFF));
        }
    }
    return out;
}

// The comment a new shared file starts with.
inline std::string NewFileHeader() {
    return "; MEVR's settings: one file for Mass Effect 1, 2 and 3 (research/25 section 20).\r\n"
           "; [Section] holds what the three games share. [Section.LE1], [Section.LE2] and [Section.LE3] hold what applies to\r\n"
           "; that game alone (its render size, performance levers, development switches, paths): such a section overrides\r\n"
           "; only the keys it names. A change in our menu, in any game, goes to [Section] and so to the three games.\r\n"
           "; The games' old files (Binaries\\Win64\\ASI\\LE1MEVR.ini ...) stay for an older build; a key you change in one of\r\n"
           "; them applies to that game alone.\r\n";
}

// The one way a settings file changes, the writers' lock held: a copy of it (or `replace`, a whole new text, for the
// migration's first file), `append` at its end, `edits` through Win32's profile API, the copy moved over the file. A new
// shared file starts with NewFileHeader. "" or what failed (the file then unchanged).
inline std::string Commit(std::filesystem::path const& path, std::vector<Edit> const& edits, std::string_view append = {},
    std::optional<std::string> const& replace = std::nullopt, ini::Encoding replaceEncoding = ini::Encoding::Narrow) {
    if (path.empty()) {
        return "no path (LOCALAPPDATA unknown)";
    }
    if (edits.empty() && append.empty() && !replace) {
        return {};
    }
    Access const access = Probe(path);
    if (access == Access::Unreadable) {
        return "it's there but couldn't be read (never a new file over settings it couldn't read)";
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path const tmp = path.wstring() + std::format(L".{}-{}.tmp", GetCurrentProcessId(), GetCurrentThreadId());
    DeleteFileW(tmp.c_str());
    ini::Encoding encoding = ini::Encoding::Narrow;
    std::string bytes;
    if (replace) {
        encoding = replaceEncoding;
        bytes = (encoding == ini::Encoding::Utf16Le ? std::string("\xFF\xFE", 2) : std::string()) + Encoded(*replace, encoding);
    } else if (access == Access::Readable) {
        bytes = ReadBytes(path);
        encoding = ini::Detect(bytes);
    } else if (IsSharedPath(path)) {
        bytes = NewFileHeader();
    }
    if (!append.empty()) {
        if (!bytes.empty() && bytes.back() != '\n' && !ini::IsUtf16(encoding)) {
            bytes += "\r\n";
        }
        bytes += Encoded(append, encoding);
    }
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            DeleteFileW(tmp.c_str());
            return std::format("couldn't write {}", ini::Utf8(tmp));
        }
    }
    for (Edit const& e : edits) {
        BOOL const ok = WritePrivateProfileStringW(e.section.c_str(), e.key.empty() ? nullptr : e.key.c_str(),
            e.value ? e.value->c_str() : nullptr, tmp.c_str());
        if (!ok && e.value) {
            DWORD const error = GetLastError();
            DeleteFileW(tmp.c_str());
            return std::format("couldn't write [{}] {} (error {})", Narrow(e.section), Narrow(e.key), error);
        }
    }
    // A file just written is busy for a moment now and then (antivirus; gotchas/fresh-file-busy-for-a-moment): a few tries.
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return {};
        }
        DWORD const error = GetLastError();
        if (attempt >= 8 || (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)) {
            DeleteFileW(tmp.c_str());
            return std::format("couldn't move {} over it (error {})", ini::Utf8(tmp), error);
        }
        Sleep(20);
    }
}

// Changes a settings file: routed (Route) when it is a shared one, as given when it is a game's old file. "" or what failed.
inline std::string Apply(std::filesystem::path const& path, std::vector<Edit> const& edits, std::string_view game = kThisGame) {
    if (edits.empty()) {
        return {};
    }
    WriteLock const lock;
    if (!IsSharedPath(path)) {
        return Commit(path, edits);
    }
    ini::File const current = Probe(path) == Access::Readable ? ini::Read(path).file : ini::File{};
    return Commit(path, Route(edits, game, current));
}

// WritePrivateProfileStringW's signature over Apply (a null value removes the key, a null key the section).
inline bool Write(wchar_t const* section, wchar_t const* key, wchar_t const* value, wchar_t const* path, std::string_view game = kThisGame) {
    if (path == nullptr || *path == L'\0' || section == nullptr) {
        return false;
    }
    Edit e{section, key != nullptr ? std::wstring(key) : std::wstring(),
        value != nullptr ? std::optional<std::wstring>(value) : std::nullopt};
    return Apply(std::filesystem::path(path), {std::move(e)}, game).empty();
}

// ---- Blocks of text: the modules' defaults, the migration's files -----------------------------------------------------

struct Moved {
    std::string section;
    std::string key;
    std::string value;
};

// `text` (ini lines, UTF-8) with its per-game keys taken out (only in a section's first occurrence, as Win32 reads only
// that; a key's first line, as Win32 reads only that). `migrate`: the calibration's sections and keys and the markers
// dropped too, and each section's moved keys written right after it as [<section>.<game>]; otherwise they are only listed.
struct SplitText {
    std::string text;
    std::vector<Moved> moved;
    int keys = 0;  // key lines left in `text`
};

inline SplitText Split(std::string_view text, std::string_view game, bool migrate) {
    SplitText out;
    std::string section;
    bool skip = false;
    bool first = false;  // this section's first occurrence
    std::set<std::string> sections, movedKeys;
    std::vector<Moved> pending;  // this section's moved keys, written as [<section>.<game>] when it ends
    auto flush = [&] {
        if (migrate && !pending.empty()) {
            if (!out.text.empty() && out.text.back() != '\n') {
                out.text += "\r\n";
            }
            out.text += "[" + GameSectionName(pending.front().section, game) + "]\r\n";
            for (Moved const& m : pending) {
                out.text += m.key + "=" + m.value + "\r\n";
            }
            out.text += "\r\n";
        }
        pending.clear();
    };
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t const end = text.find('\n', pos);
        std::size_t const next = end == std::string_view::npos ? text.size() : end + 1;
        std::string_view const line = text.substr(pos, next - pos);
        pos = next;
        std::string_view const t = TrimView(line);
        if (!t.empty() && t.front() == '[') {
            flush();
            std::size_t const close = t.find(']');
            section = std::string(TrimView(close == std::string_view::npos ? t.substr(1) : t.substr(1, close - 1)));
            first = sections.insert(ini::Lower(section)).second;
            skip = migrate && ExcludedSection(section);
            if (!skip) {
                out.text += line;
            }
            continue;
        }
        if (skip) {
            continue;
        }
        std::size_t const eq = t.find('=');
        if (t.empty() || t.front() == ';' || t.front() == '#' || section.empty() || eq == std::string_view::npos) {
            out.text += line;
            continue;
        }
        std::string const key(TrimView(t.substr(0, eq)));
        std::string const value(TrimView(t.substr(eq + 1)));
        if (migrate && Excluded(section, key)) {
            continue;
        }
        if (first && !game.empty() && !SplitGameSection(section) && PerGame(section, key, value) != nullptr) {
            if (movedKeys.insert(ini::Lower(section) + "\n" + ini::Lower(key)).second) {
                Moved m{section, key, value};
                pending.push_back(m);
                out.moved.push_back(std::move(m));
            }
            continue;
        }
        out.text += line;
        ++out.keys;
    }
    flush();
    return out;
}

// A module's defaults block added to a settings file (the file made if it isn't there): in a shared file its per-game keys
// go to [<section>.<game>] (each only if that section doesn't have it yet), the rest as the block says. "" or what failed.
inline std::string Append(std::filesystem::path const& path, std::string_view block, std::string_view game = kThisGame) {
    WriteLock const lock;
    if (!IsSharedPath(path)) {
        return Commit(path, {}, block);
    }
    ini::File const current = Probe(path) == Access::Readable ? ini::Read(path).file : ini::File{};
    SplitText const split = Split(block, game, false);
    std::vector<Edit> edits;
    for (Moved const& m : split.moved) {
        if (!ini::Get(current, GameSectionName(m.section, game), m.key)) {
            edits.push_back(Set(GameSectionName(m.section, game), m.key, m.value));
        }
    }
    return Commit(path, edits, split.keys > 0 ? std::string_view(split.text) : std::string_view());
}

// ---- Which file this session uses ---------------------------------------------------------------------------------------

// The host's answer for the mod (the same process): the file in use.
inline constexpr wchar_t kEnvironment[] = L"MEVR_SETTINGS_FILE";

// The shared file, unless it is there but can't be read (another program holds it, no permission): then the game's old file,
// and the shared one is never written this session. A shared file not made yet while the game's old one is there (the
// migration couldn't write it): the old file. Neither there (a new install): the shared file, made by the first write.
inline std::filesystem::path Decide(std::filesystem::path const& shared, std::filesystem::path const& gameFile) {
    if (shared.empty()) {
        return gameFile;
    }
    switch (Probe(shared)) {
    case Access::Readable:
        return shared;
    case Access::Unreadable:
        return gameFile;
    case Access::Missing:
    default: {
        std::error_code ec;
        return !gameFile.empty() && std::filesystem::exists(gameFile, ec) ? gameFile : shared;
    }
    }
}

// MEVR_SETTINGS_FILE when the host set it, else Decide.
inline std::filesystem::path InUse(std::filesystem::path const& gameFile) {
    wchar_t buffer[1024]{};
    DWORD const n = GetEnvironmentVariableW(kEnvironment, buffer, static_cast<DWORD>(std::size(buffer)));
    if (n > 0 && n < std::size(buffer)) {
        return std::filesystem::path(buffer);
    }
    return Decide(DefaultPath(), gameFile);
}

// ---- The old files into the shared one (Sync) ---------------------------------------------------------------------------

// One game's old settings file.
struct Source {
    std::string game;  // "LE1", "LE2", "LE3"
    std::filesystem::path ini;
};

// The three games' ASI\LE{n}MEVR.ini under the install whose game the exe is in (exe dir <root>\Game\ME<n>\Binaries\Win64),
// in the trilogy's order: ME1's first, the base.
inline std::vector<Source> SourcesUnder(std::filesystem::path const& exeDir, std::string_view thisGame) {
    std::vector<Source> out;
    for (calibration_file::Source const& s : calibration_file::SourcesUnder(exeDir, thisGame)) {
        out.push_back({s.game, s.ini});
    }
    std::sort(out.begin(), out.end(), [](Source const& a, Source const& b) { return a.game < b.game; });
    return out;
}

// A file as Win32 reads it: its sections in order (each once), each section's keys (the first of each, names as written).
using Lines = std::vector<std::pair<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>>>;

inline Lines ReadLines(std::filesystem::path const& path) {
    Lines out;
    std::vector<wchar_t> names(65536);
    DWORD const n = GetPrivateProfileSectionNamesW(names.data(), static_cast<DWORD>(names.size()), path.c_str());
    for (std::size_t at = 0; at < n;) {
        std::wstring const name(names.data() + at);
        at += name.size() + 1;
        if (name.empty()) {
            break;
        }
        bool const seen = std::any_of(out.begin(), out.end(), [&](auto const& s) {
            return CompareStringOrdinal(s.first.c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL;
        });
        if (!seen) {
            out.emplace_back(name, calibration_file::SectionLines(path, Narrow(name)));
        }
    }
    return out;
}

inline std::optional<std::wstring> Find(Lines const& lines, std::wstring_view section, std::wstring_view key) {
    for (auto const& [name, keys] : lines) {
        if (CompareStringOrdinal(name.c_str(), -1, std::wstring(section).c_str(), -1, TRUE) != CSTR_EQUAL) {
            continue;
        }
        for (auto const& [k, v] : keys) {
            if (CompareStringOrdinal(k.c_str(), -1, std::wstring(key).c_str(), -1, TRUE) == CSTR_EQUAL) {
                return v;
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// The first occurrence of [section] in `text`: its header and every line up to the next header.
inline std::string SectionText(std::string_view text, std::string_view section) {
    std::string out;
    bool in = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t const end = text.find('\n', pos);
        std::size_t const next = end == std::string_view::npos ? text.size() : end + 1;
        std::string_view const line = text.substr(pos, next - pos);
        pos = next;
        std::string_view const t = TrimView(line);
        if (!t.empty() && t.front() == '[') {
            if (in) {
                break;
            }
            std::size_t const close = t.find(']');
            in = Same(TrimView(close == std::string_view::npos ? t.substr(1) : t.substr(1, close - 1)), section);
        }
        if (in) {
            out += line;
        }
    }
    return out;
}

inline std::string OldFileName(std::string_view game) {
    return std::string(game) + "MEVR.ini";
}

inline std::string GameTitle(std::string_view game) {
    return Same(game, "LE1") ? "ME1" : Same(game, "LE2") ? "ME2" : Same(game, "LE3") ? "ME3" : std::string(game);
}

// "[A] x, [A] y, [B] z" for a log line, cut after `max` names.
inline std::string Names(std::vector<std::string> const& names, std::size_t max = 40) {
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < max; ++i) {
        out += (i == 0 ? "" : ", ") + names[i];
    }
    if (names.size() > max) {
        out += std::format(" and {} more", names.size() - max);
    }
    return out;
}

struct SyncResult {
    std::vector<std::string> lines;  // what happened, for the log ("settings: " before each)
    bool refused = false;            // the shared file is there but couldn't be read: nothing done; the old file in use
    bool created = false;            // the shared file was made now
    bool changed = false;            // the shared file was written
    std::string error;               // a write that failed (the old file then stays in use while the shared one is missing)
};

// Once at host attach (before the mod reads) and every 2 s after, under the writers' lock:
//   - no shared file yet: it is made from the old files, ME1's the base (its text, comments and order kept; the calibration's
//     sections left out; its per-game keys moved to [<section>.LE1] right after their section), then ME2's and ME3's taken
//     in (TakeLate);
//   - an old file not taken yet (no copy in settings-taken): TakeLate. A key the shared file lacks is shared now (a section
//     it lacks comes whole, with its comments); a per-game key goes to [<section>.<GAME>]; a key whose value differs keeps
//     the shared value for the three games and the old file's in [<section>.<GAME>], so that game goes on as it did. Each
//     difference is logged.
//   - this game's old file, taken before: every key changed in it since (against its taken copy) applies to this game alone
//     ([<section>.<GAME>]; a value back to the shared one takes the game's own out), a key removed from it takes the game's
//     own out. The other games' old files are their hosts' to read.
// Each old file taken is marked [Settings] shared = 1 and copied to settings-taken\LE{n}MEVR.ini after the shared file is
// written. A shared file that is there but can't be read is refused: nothing written, the old file stays in use.
inline SyncResult Sync(std::filesystem::path const& shared, std::vector<Source> const& sources, std::string_view thisGame) {
    SyncResult r;
    if (shared.empty()) {
        r.refused = true;
        r.lines.push_back("no LOCALAPPDATA: each game's old file stays in use");
        return r;
    }
    WriteLock const lock;
    Access const access = Probe(shared);
    if (access == Access::Unreadable) {
        r.refused = true;
        r.lines.push_back(std::format("{} is there but couldn't be read: nothing taken into it, nothing written to it this session; "
                                      "this game's old file is in use", ini::Utf8(shared)));
        return r;
    }
    bool const created = access == Access::Missing;
    ini::File have = created ? ini::File{} : ini::Read(shared).file;
    std::optional<std::string> replace;
    ini::Encoding replaceEncoding = ini::Encoding::Narrow;
    std::string append;
    std::vector<Edit> edits;
    std::vector<std::size_t> marks, copies;
    std::error_code ec;

    auto has = [&](std::string const& section, std::string const& key) { return ini::Get(have, section, key); };
    auto put = [&](std::string const& section, std::string const& key, std::string const& value) {
        have[section].insert_or_assign(ini::Lower(key), value);
    };
    auto drop = [&](std::string const& section, std::string const& key) {
        if (auto s = have.find(section); s != have.end()) {
            s->second.erase(ini::Lower(key));
        }
    };

    for (std::size_t i = 0; i < sources.size(); ++i) {
        Source const& s = sources[i];
        if (s.ini.empty() || !std::filesystem::exists(s.ini, ec)) {
            continue;
        }
        std::string const file = OldFileName(s.game);
        std::string const title = GameTitle(s.game);
        std::filesystem::path const taken = TakenCopy(shared, s.game);
        bool const firstTake = created || !std::filesystem::exists(taken, ec);
        if (!firstTake && !Same(s.game, thisGame)) {
            continue;  // taken before; its own host reads what changes in it
        }
        if (firstTake && created && !replace) {
            // The base: its text, as it is, with the calibration out and its per-game keys in its own sections.
            std::string const bytes = ReadBytes(s.ini);
            ini::Encoding const encoding = ini::Detect(bytes);
            SplitText const split = Split(ini::Decode(bytes, encoding), s.game, true);
            replace = NewFileHeader() + "\r\n" + split.text;
            replaceEncoding = ini::IsUtf16(encoding) ? ini::Encoding::Utf16Le : ini::Encoding::Narrow;
            have = ini::Parse(*replace);
            std::vector<std::string> own;
            for (Moved const& m : split.moved) {
                own.push_back(std::format("[{}] {}", m.section, m.key));
            }
            r.lines.push_back(std::format("{} is the base: {} keys for the three games; {} stay {}'s own in its sections ({})", file,
                split.keys, split.moved.size(), title, Names(own, 12)));
            marks.push_back(i);
            copies.push_back(i);
            continue;
        }
        if (firstTake) {
            // A game's old file taken into a shared file that has keys already.
            std::string const bytes = ReadBytes(s.ini);
            std::string const text = ini::Decode(bytes, ini::Detect(bytes));
            std::vector<std::string> sharedNow, own;
            int differ = 0;
            for (auto const& [sectionW, keys] : ReadLines(s.ini)) {
                std::string const section = Narrow(sectionW);
                if (ExcludedSection(section) || SplitGameSection(section)) {
                    continue;
                }
                bool const anyOwn = std::any_of(kGames.begin(), kGames.end(),
                    [&](std::string_view g) { return have.contains(GameSectionName(section, g)); });
                if (!have.contains(section) && !anyOwn) {
                    // A section the shared file doesn't have: whole, with its comments.
                    SplitText const split = Split(SectionText(text, section), s.game, true);
                    if (split.keys > 0 || !split.moved.empty()) {
                        if (!append.empty() && append.back() != '\n') {
                            append += "\r\n";
                        }
                        append += "\r\n" + split.text;
                        for (auto const& [k, v] : ini::Parse(split.text)) {
                            for (auto const& [kk, vv] : v) {
                                have[k].insert_or_assign(kk, vv);
                            }
                        }
                        for (auto const& [k, v] : keys) {
                            std::string const key = Narrow(k);
                            if (!Excluded(section, key) && PerGame(section, key, Narrow(v)) == nullptr) {
                                sharedNow.push_back(std::format("[{}] {}", section, key));
                            }
                        }
                        for (Moved const& m : split.moved) {
                            own.push_back(std::format("[{}] {}", m.section, m.key));
                        }
                    }
                    continue;
                }
                for (auto const& [k, v] : keys) {
                    std::string const key = Narrow(k);
                    std::string const value = Narrow(v);
                    if (Excluded(section, key)) {
                        continue;
                    }
                    std::string const ownSection = GameSectionName(section, s.game);
                    if (PerGame(section, key, value) != nullptr) {
                        if (!has(ownSection, key)) {
                            edits.push_back({Wide(ownSection), k, v});
                            put(ownSection, key, value);
                            own.push_back(std::format("[{}] {}", section, key));
                        }
                        continue;
                    }
                    if (std::optional<std::string> const common = has(section, key)) {
                        if (*common != value && !has(ownSection, key)) {
                            edits.push_back({Wide(ownSection), k, v});
                            put(ownSection, key, value);
                            ++differ;
                            r.lines.push_back(std::format("[{}] {}: the three games keep \"{}\"; {} keeps its own \"{}\" from {} ([{}]): "
                                                          "change it in the menu to share it",
                                section, key, *common, title, value, file, ownSection));
                        }
                        continue;
                    }
                    edits.push_back({Wide(section), k, v});
                    put(section, key, value);
                    sharedNow.push_back(std::format("[{}] {}", section, key));
                }
            }
            r.lines.push_back(std::format("{} taken: {} keys only it had are the three games' now{}{}; {} stay {}'s own; {} differ "
                                          "and stay {}'s own (above)",
                file, sharedNow.size(), sharedNow.empty() ? "" : " (", sharedNow.empty() ? "" : Names(sharedNow) + ")", own.size(),
                title, differ, title));
            marks.push_back(i);
            copies.push_back(i);
            continue;
        }
        // This game's old file, taken before: what changed in it since applies to this game alone.
        Lines const now = ReadLines(s.ini);
        Lines const before = ReadLines(taken);
        bool any = false;
        for (auto const& [sectionW, keys] : now) {
            std::string const section = Narrow(sectionW);
            if (ExcludedSection(section) || SplitGameSection(section)) {
                continue;
            }
            std::string const ownSection = GameSectionName(section, s.game);
            for (auto const& [k, v] : keys) {
                std::string const key = Narrow(k);
                std::string const value = Narrow(v);
                if (Excluded(section, key)) {
                    continue;
                }
                std::optional<std::wstring> const was = Find(before, sectionW, k);
                if (was && *was == v) {
                    continue;
                }
                any = true;
                std::optional<std::string> const common = has(section, key);
                if (PerGame(section, key, value) == nullptr && common && *common == value) {
                    if (has(ownSection, key)) {
                        edits.push_back({Wide(ownSection), k, std::nullopt});
                        drop(ownSection, key);
                    }
                    r.lines.push_back(std::format("[{}] {} = {} in {}: the three games' value; {}'s own taken out", section, key,
                        value, file, title));
                } else {
                    edits.push_back({Wide(ownSection), k, v});
                    put(ownSection, key, value);
                    r.lines.push_back(std::format("[{}] {} = {} in {}: {}'s own ([{}])", section, key, value, file, title, ownSection));
                }
            }
        }
        for (auto const& [sectionW, keys] : before) {
            std::string const section = Narrow(sectionW);
            if (ExcludedSection(section) || SplitGameSection(section)) {
                continue;
            }
            std::string const ownSection = GameSectionName(section, s.game);
            for (auto const& [k, v] : keys) {
                std::string const key = Narrow(k);
                if (Excluded(section, key) || Find(now, sectionW, k)) {
                    continue;
                }
                any = true;
                if (has(ownSection, key)) {
                    edits.push_back({Wide(ownSection), k, std::nullopt});
                    drop(ownSection, key);
                }
                r.lines.push_back(std::format("[{}] {} gone from {}: {} reads the three games' value again", section, key, file, title));
            }
        }
        if (any) {
            copies.push_back(i);
        }
    }

    if (replace || !append.empty() || !edits.empty()) {
        r.error = Commit(shared, edits, append, replace, replaceEncoding);
        if (!r.error.empty()) {
            r.lines.push_back(std::format("couldn't write {}: {}; this game's old file stays in use", ini::Utf8(shared), r.error));
            return r;
        }
        r.changed = true;
        r.created = created;
    }
    for (std::size_t const i : marks) {
        Source const& s = sources[i];
        ini::Loaded const loaded = ini::Read(s.ini);
        if (ini::Trim(ini::Get(loaded.file, kMarkerSection, kMarkerKey).value_or("")) == "1") {
            continue;
        }
        bool const marked = WritePrivateProfileStringW(Wide(kMarkerSection).c_str(), Wide(kMarkerKey).c_str(), L"1", s.ini.c_str()) != 0;
        r.lines.push_back(marked ? std::format("{} marked [{}] {} = 1: it stays for an older build; a key you change in it applies to "
                                               "{} alone", OldFileName(s.game), kMarkerSection, kMarkerKey, GameTitle(s.game))
                                 : std::format("couldn't mark {} [{}] {} = 1 (error {}); taken all the same", OldFileName(s.game),
                                       kMarkerSection, kMarkerKey, GetLastError()));
    }
    for (std::size_t const i : copies) {
        Source const& s = sources[i];
        std::filesystem::path const taken = TakenCopy(shared, s.game);
        std::filesystem::create_directories(taken.parent_path(), ec);
        if (!CopyFileW(s.ini.c_str(), taken.c_str(), FALSE)) {
            r.lines.push_back(std::format("couldn't copy {} to {} (error {}): it is taken again next time", OldFileName(s.game),
                ini::Utf8(taken), GetLastError()));
        }
    }
    return r;
}

} // namespace mevr::vr::settings_file
