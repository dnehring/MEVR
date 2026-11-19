// SPDX-License-Identifier: MIT
#pragma once

// One calibration for the trilogy (research/25 section 17; the owner, 2026-11-18: shared unless a game's VRIK differs).
// The player's measurements live in %LOCALAPPDATA%\MEVR\calibration.ini, the same file for ME1, ME2 and ME3:
//   [Calibration.<key>]          the guided calibration's results per controller type (vr::calibration::ProfileKey)
//   [ManualCalibration.<key>]    the manual offsets over it (core/vr/ManualCalibration)
//   [Calibration] prompted, last the controller types prompted once; the last one calibrated
//   [<section>.<GAME>]           (LE1, LE2, LE3) overrides only the keys it names, for that game: a game whose VRIK
//                                ever differs. Nothing writes one; a reader takes it before the common section.
// What stays per game, in ASI\<GAME>MEVR.ini: [Calibration] use, eye, grabOffset (settings, not measurements) and the
// marker [Calibration] shared = 1 (this game's own sections are in the shared file; they stay for an older build).
//
// Reads: core/Ini over one snapshot of the file (case-insensitive, the first section and key win, as Win32's).
// Writes: Win32's profile API on a copy (<file>.tmp) moved over the file, so a reader never sees half a save.
// The migration runs once per launch in the host, before the mod loads (plugin/ModLoader): a controller type the shared
// file lacks is taken from the newest of the three games' ASI\<GAME>MEVR.ini under this install (this game's first on a
// tie). Header-only: every target that compiles core/Ini (the hosts, the mods, the tests) can use it as it is.

#include "core/Ini.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mevr::vr::calibration_file {

inline constexpr char kFileName[] = "calibration.ini";
inline constexpr std::string_view kMeasuredPrefix = "Calibration.";
inline constexpr std::string_view kManualPrefix = "ManualCalibration.";

inline bool SameText(std::string_view a, std::string_view b) {
    return a.size() == b.size() && ini::Lower(a) == ini::Lower(b);
}

inline bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.size() > prefix.size() && SameText(text.substr(0, prefix.size()), prefix);
}

// "Calibration.<key>" or "ManualCalibration.<key>" (an override "<...>.<GAME>" too): a section of the shared file.
inline bool SharedSection(std::string_view section) {
    return StartsWith(section, kMeasuredPrefix) || StartsWith(section, kManualPrefix);
}

// Whether [section] key lives in the shared file (else in the game's own ASI\<GAME>MEVR.ini).
inline bool Shared(std::string_view section, std::string_view key) {
    return SharedSection(section) || (SameText(section, "Calibration") && (SameText(key, "prompted") || SameText(key, "last")));
}

// The controller type a section names: "Calibration.touch__x" -> "touch__x"; "" for an override or another section.
inline std::string KeyOf(std::string_view section, std::string_view prefix) {
    if (!StartsWith(section, prefix)) {
        return {};
    }
    std::string_view const rest = section.substr(prefix.size());
    return rest.find('.') == std::string_view::npos ? ini::Lower(rest) : std::string();
}

// A key: this game's [<section>.<game>] first, then [<section>].
inline std::optional<std::string> Get(ini::File const& file, std::string_view game, std::string_view section, std::string_view key) {
    if (!game.empty()) {
        if (auto v = ini::Get(file, std::string(section) + "." + std::string(game), key)) {
            return v;
        }
    }
    return ini::Get(file, section, key);
}

// A section with this game's overrides on top (keys lower-cased, as core/Ini keeps them).
inline ini::Section Merged(ini::File const& file, std::string_view game, std::string_view section) {
    ini::Section out;
    if (!game.empty()) {
        if (auto const s = file.find(std::string(section) + "." + std::string(game)); s != file.end()) {
            out = s->second;
        }
    }
    if (auto const s = file.find(section); s != file.end()) {
        for (auto const& [k, v] : s->second) {
            out.emplace(k, v);  // an override's key already there wins
        }
    }
    return out;
}

// %LOCALAPPDATA%\MEVR\calibration.ini (empty without LOCALAPPDATA).
inline std::filesystem::path DefaultPath() {
    wchar_t buffer[MAX_PATH]{};
    DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR" / kFileName;
}

// The file is missing, readable, or there but unreadable (another program holds it, no permission): a writer then
// refuses for the session rather than put a new file over a calibration it couldn't read.
enum class Access { Missing, Readable, Unreadable };

inline Access Probe(std::filesystem::path const& path) {
    if (path.empty()) {
        return Access::Unreadable;
    }
    HANDLE const h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD const error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Access::Missing : Access::Unreadable;
    }
    CloseHandle(h);
    return Access::Readable;
}

inline std::wstring Wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    int const n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    }
    return out;
}

inline std::string Narrow(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    }
    return out;
}

// One change: a key's value, a key removed (no value), or the whole section removed (no key, no value).
struct Edit {
    std::wstring section;
    std::wstring key;
    std::optional<std::wstring> value;
};

inline Edit Set(std::string_view section, std::string_view key, std::string_view value) {
    return {Wide(section), Wide(key), Wide(value)};
}

inline Edit RemoveSection(std::string_view section) {
    return {Wide(section), {}, std::nullopt};
}

// Applies the edits to a copy of the file and moves the copy over it; "" or what failed (the file then unchanged).
inline std::string Apply(std::filesystem::path const& path, std::vector<Edit> const& edits) {
    if (edits.empty()) {
        return {};
    }
    if (path.empty()) {
        return "no path (LOCALAPPDATA unknown)";
    }
    Access const access = Probe(path);
    if (access == Access::Unreadable) {
        return "it's there but couldn't be read (never a new file over a calibration)";
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path const tmp = path.wstring() + L".tmp";
    DeleteFileW(tmp.c_str());
    if (access == Access::Readable && !CopyFileW(path.c_str(), tmp.c_str(), FALSE)) {
        return std::format("couldn't copy it to {} (error {})", ini::Utf8(tmp), GetLastError());
    }
    for (Edit const& e : edits) {
        BOOL const ok = WritePrivateProfileStringW(e.section.c_str(), e.key.empty() ? nullptr : e.key.c_str(),
            e.value ? e.value->c_str() : nullptr, tmp.c_str());
        // Removing what isn't there fails without a file; anything else failing is an error.
        if (!ok && (e.value || std::filesystem::exists(tmp, ec))) {
            DWORD const error = GetLastError();
            DeleteFileW(tmp.c_str());
            return std::format("couldn't write [{}] {} (error {})", Narrow(e.section), Narrow(e.key), error);
        }
    }
    if (!std::filesystem::exists(tmp, ec)) {
        return {};  // only removals of what wasn't there
    }
    // A file just written is busy for a moment now and then (antivirus; gotchas/fresh-file-busy-for-a-moment): a few tries.
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return {};
        }
        DWORD const error = GetLastError();
        if (attempt >= 5 || (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)) {
            DeleteFileW(tmp.c_str());
            return std::format("couldn't move {} over it (error {})", ini::Utf8(tmp), error);
        }
        Sleep(20);
    }
}

// A section's lines as Win32 reads them (names' case and order kept, the first of a repeated key, no comments).
inline std::vector<std::pair<std::wstring, std::wstring>> SectionLines(std::filesystem::path const& path, std::string_view section) {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    std::wstring buffer(32768, L'\0');
    DWORD const n = GetPrivateProfileSectionW(Wide(section).c_str(), buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    for (std::size_t at = 0; at < n;) {
        std::wstring const line(buffer.c_str() + at);
        at += line.size() + 1;
        std::size_t const eq = line.find(L'=');
        std::size_t const first = line.find_first_not_of(L" \t");
        if (eq == std::wstring::npos || first == std::wstring::npos || line[first] == L';' || line[first] == L'#') {
            continue;
        }
        auto trim = [](std::wstring_view s) {
            std::size_t const a = s.find_first_not_of(L" \t\r");
            std::size_t const b = s.find_last_not_of(L" \t\r");
            return a == std::wstring_view::npos ? std::wstring() : std::wstring(s.substr(a, b - a + 1));
        };
        std::wstring key = trim(std::wstring_view(line).substr(0, eq));
        if (key.empty()) {
            continue;
        }
        bool const seen = std::any_of(out.begin(), out.end(), [&](auto const& kv) {
            return CompareStringOrdinal(kv.first.c_str(), -1, key.c_str(), -1, TRUE) == CSTR_EQUAL;
        });
        if (!seen) {
            out.emplace_back(std::move(key), trim(std::wstring_view(line).substr(eq + 1)));
        }
    }
    return out;
}

// ---- Lists ([Calibration] prompted: "a, b, c") ----------------------------------------------------------------------

inline std::vector<std::string> SplitList(std::string_view list) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t const end = std::min(list.find(',', start), list.size());
        std::string item = ini::Trim(list.substr(start, end - start));
        if (!item.empty()) {
            out.push_back(std::move(item));
        }
        start = end + 1;
    }
    return out;
}

// The union in order: `into`'s entries first, then `from`'s that aren't in it yet (case-insensitive).
inline std::string MergeLists(std::string_view into, std::string_view from) {
    std::vector<std::string> items = SplitList(into);
    for (std::string& item : SplitList(from)) {
        if (std::none_of(items.begin(), items.end(), [&](std::string const& have) { return SameText(have, item); })) {
            items.push_back(std::move(item));
        }
    }
    std::string out;
    for (std::string const& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

// ---- The migration ---------------------------------------------------------------------------------------------------

// One game's settings file, a place the migration takes calibrations from.
struct Source {
    std::string game;  // "LE1", "LE2", "LE3"
    std::filesystem::path ini;
};

// The three games' ASI\<GAME>MEVR.ini under the install whose game the exe is in (exe dir <root>\Game\ME<n>\Binaries\Win64),
// this game's first.
inline std::vector<Source> SourcesUnder(std::filesystem::path const& exeDir, std::string_view thisGame) {
    std::vector<Source> out;
    std::filesystem::path const games = exeDir.parent_path().parent_path().parent_path();  // <root>\Game
    out.push_back({std::string(thisGame), exeDir / L"ASI" / (Wide(thisGame) + L"MEVR.ini")});
    for (int n : {1, 2, 3}) {
        std::string const game = std::format("LE{}", n);
        if (SameText(game, thisGame)) {
            continue;
        }
        out.push_back({game, games / std::format(L"ME{}", n) / L"Binaries" / L"Win64" / L"ASI" / (Wide(game) + L"MEVR.ini")});
    }
    return out;
}

// A controller type's calibration in one source.
struct Candidate {
    std::size_t source = 0;  // index into the sources, this game's first
    std::string time;        // [Calibration.<key>] time ("2026-11-17 22:50"); "" without one (a position chosen only)
    bool measured = false;   // has [Calibration.<key>]
    bool manual = false;     // has [ManualCalibration.<key>]
};

// Where a controller type's sections are taken from: the newest measured section by its time (an untimed one is older than
// any timed; a tie goes to the earlier source, this game's first); its manual offsets from the same source, else the first
// source that has them. nullopt fields: nothing of that kind anywhere.
struct Choice {
    std::optional<std::size_t> measured;
    std::optional<std::size_t> manual;
};

inline Choice Choose(std::vector<Candidate> const& candidates) {
    Choice c;
    Candidate const* best = nullptr;
    for (Candidate const& x : candidates) {
        if (x.measured && (best == nullptr || x.time > best->time || (x.time == best->time && x.source < best->source))) {
            best = &x;
        }
    }
    if (best != nullptr) {
        c.measured = best->source;
        if (best->manual) {
            c.manual = best->source;
        }
    }
    if (!c.manual) {
        for (Candidate const& x : candidates) {
            if (x.manual && (!c.manual || x.source < *c.manual)) {
                c.manual = x.source;
            }
        }
    }
    return c;
}

struct Migration {
    std::vector<std::string> lines;  // what happened, for the log ("calibration: " goes before each)
    int taken = 0;                   // controller types taken into the shared file
    bool marked = false;             // this game's ini was marked [Calibration] shared = 1
    bool refused = false;            // the shared file is there but couldn't be read: nothing done, writes refused
    std::string error;
};

// Once per launch, before the mod loads. For every controller type a source names and the shared file doesn't, its
// sections are copied from the source Choose picks (as Win32 reads them: every value the same text); the prompted lists
// are merged in; [Calibration] last is taken if the shared file has none. A source already marked [Calibration] shared = 1
// is not read again while the shared file exists (its sections were taken before). This game's ini (sources[0]) is marked
// once every calibration in it is in the shared file. The per-game sections are left as they are (an older build reads them).
inline Migration Migrate(std::filesystem::path const& shared, std::vector<Source> const& sources) {
    Migration m;
    Access const access = Probe(shared);
    if (access == Access::Unreadable) {
        m.refused = true;
        m.lines.push_back(std::format("{} is there but couldn't be read: nothing taken into it, and no calibration is saved "
                                      "this session", ini::Utf8(shared)));
        return m;
    }
    ini::File const have = access == Access::Readable ? ini::Read(shared).file : ini::File{};
    bool const sharedExists = access == Access::Readable;

    struct Read {
        bool use = false;   // read and not already taken
        ini::File file;
        std::vector<std::string> keys;  // controller types with a section
    };
    std::vector<Read> reads(sources.size());
    for (std::size_t i = 0; i < sources.size(); ++i) {
        std::error_code ec;
        if (sources[i].ini.empty() || !std::filesystem::exists(sources[i].ini, ec)) {
            continue;
        }
        ini::Loaded loaded = ini::Read(sources[i].ini);
        if (!loaded.exists) {
            continue;
        }
        Read& r = reads[i];
        r.file = std::move(loaded.file);
        for (auto const& [name, section] : r.file) {
            std::string key = KeyOf(name, kMeasuredPrefix);
            if (key.empty()) {
                key = KeyOf(name, kManualPrefix);
            }
            if (!key.empty() && std::find(r.keys.begin(), r.keys.end(), key) == r.keys.end()) {
                r.keys.push_back(key);
            }
        }
        std::optional<std::string> const marker = ini::Get(r.file, "Calibration", "shared");
        r.use = !(sharedExists && marker && ini::Trim(*marker) == "1");
    }

    // The controller types to take, in the order the sources name them.
    std::vector<std::string> keys;
    for (Read const& r : reads) {
        if (!r.use) {
            continue;
        }
        for (std::string const& key : r.keys) {
            bool const inShared = have.contains("Calibration." + key) || have.contains("ManualCalibration." + key);
            if (!inShared && std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(key);
            }
        }
    }

    std::vector<Edit> edits;
    for (std::string const& key : keys) {
        std::string const measured = "Calibration." + key;
        std::string const manual = "ManualCalibration." + key;
        std::vector<Candidate> candidates;
        for (std::size_t i = 0; i < reads.size(); ++i) {
            if (!reads[i].use) {
                continue;
            }
            Candidate c;
            c.source = i;
            c.measured = reads[i].file.contains(measured);
            c.manual = reads[i].file.contains(manual);
            c.time = ini::Trim(ini::Get(reads[i].file, measured, "time").value_or(""));
            if (c.measured || c.manual) {
                candidates.push_back(std::move(c));
            }
        }
        Choice const choice = Choose(candidates);
        std::string what;
        if (choice.measured) {
            for (auto const& [k, v] : SectionLines(sources[*choice.measured].ini, measured)) {
                edits.push_back({Wide(measured), k, v});
            }
            std::string const time = ini::Trim(ini::Get(reads[*choice.measured].file, measured, "time").value_or(""));
            what = std::format("[{}] taken from {}MEVR.ini ({})", measured, sources[*choice.measured].game,
                time.empty() ? std::string("no calibration yet, its play position") : "calibrated " + time);
        }
        if (choice.manual) {
            for (auto const& [k, v] : SectionLines(sources[*choice.manual].ini, manual)) {
                edits.push_back({Wide(manual), k, v});
            }
            what += std::format("{}[{}] from {}MEVR.ini", what.empty() ? "" : ", ", manual, sources[*choice.manual].game);
        }
        if (!what.empty()) {
            m.lines.push_back(what + " into " + kFileName);
            ++m.taken;
        }
    }

    // The prompted lists merged; the last one calibrated if the shared file doesn't say.
    std::string const before = ini::Trim(ini::Get(have, "Calibration", "prompted").value_or(""));
    std::string prompted = before;
    std::optional<std::string> last = ini::Get(have, "Calibration", "last");
    std::string lastFrom;
    for (std::size_t i = 0; i < reads.size(); ++i) {
        if (!reads[i].use) {
            continue;
        }
        prompted = MergeLists(prompted, ini::Get(reads[i].file, "Calibration", "prompted").value_or(""));
        if (!last || ini::Trim(*last).empty()) {
            if (auto const l = ini::Get(reads[i].file, "Calibration", "last"); l && !ini::Trim(*l).empty()) {
                last = ini::Trim(*l);
                lastFrom = sources[i].game;
            }
        }
    }
    if (prompted != before) {
        edits.push_back(Set("Calibration", "prompted", prompted));
        m.lines.push_back(std::format("[Calibration] prompted is now \"{}\" in {} (the games' lists merged)", prompted, kFileName));
    }
    if (!lastFrom.empty() && last) {
        edits.push_back(Set("Calibration", "last", *last));
    }

    if (!edits.empty()) {
        m.error = Apply(shared, edits);
        if (!m.error.empty()) {
            m.lines.push_back(std::format("couldn't write {}: {}; the games' own sections stay in use until it can be",
                ini::Utf8(shared), m.error));
            m.taken = 0;
            return m;
        }
    }

    // This game's ini marked once all of its calibrations are in the shared file (taken now or before).
    if (!sources.empty() && reads[0].use && !reads[0].keys.empty()) {
        ini::File const now = ini::Read(shared).file;
        bool const all = std::all_of(reads[0].keys.begin(), reads[0].keys.end(), [&](std::string const& key) {
            return now.contains("Calibration." + key) || now.contains("ManualCalibration." + key);
        });
        std::optional<std::string> const marker = ini::Get(reads[0].file, "Calibration", "shared");
        if (all && !(marker && ini::Trim(*marker) == "1")) {
            m.marked = WritePrivateProfileStringW(L"Calibration", L"shared", L"1", sources[0].ini.c_str()) != 0;
            m.lines.push_back(m.marked ? std::format("{}MEVR.ini marked [Calibration] shared = 1: its calibrations are in {} (its own "
                                                     "sections stay for an older build)", sources[0].game, kFileName)
                                       : std::format("couldn't mark {}MEVR.ini [Calibration] shared = 1 (error {})", sources[0].game,
                                             GetLastError()));
        }
    }
    return m;
}

} // namespace mevr::vr::calibration_file
