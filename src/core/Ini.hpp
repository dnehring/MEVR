// SPDX-License-Identifier: MIT
#pragma once

// One reader for our settings files (ASI\<GAME>MEVR.ini), shared by the host and the mod, so both see what Win32's
// profile API (GetPrivateProfileString, which the host and every write use) sees:
//   - section and key names are case-insensitive (stored lower-cased; lookups ignore case);
//   - the FIRST occurrence wins: a repeated section's keys and a repeated key are ignored, as Win32 ignores them;
//   - a UTF-8 byte order mark is stripped; a UTF-16 file (BOM, or NULs in the first bytes) is decoded to UTF-8, and
//     the writer must not append narrow text to it (Encoding::Utf16*).
// Numbers are read and written without the C locale (std::from_chars / std::to_chars): "0.5" is 0.5 whatever
// setlocale says. Game-independent and unit-tested (tests/Main.cpp TestIni).

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace mevr::ini {

std::string Lower(std::string_view text);
std::string Trim(std::string_view text);

// Case-insensitive (ASCII) ordering, transparent: find("Hands") and find("hands") are the same lookup.
struct CaseLess {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept;
};

using Section = std::map<std::string, std::string, CaseLess>;
using File = std::map<std::string, Section, CaseLess>;

enum class Encoding { Narrow, Utf8Bom, Utf16Le, Utf16Be };

Encoding Detect(std::string_view bytes) noexcept;
bool IsUtf16(Encoding e) noexcept;
// The file's bytes as UTF-8 text without a BOM (UTF-16 decoded; narrow text passed through as it is).
std::string Decode(std::string_view bytes, Encoding encoding);

// "[Section]" lines and "key = value" lines; ';' and '#' start comments; lines without '=' are skipped.
File Parse(std::string_view text);
// The lines of one section ("key=value" each, as GetPrivateProfileSection gives them, NULs as newlines).
Section ParseKeys(std::string_view lines);

struct Loaded {
    bool exists = false;   // the file could be opened
    Encoding encoding = Encoding::Narrow;
    File file;
};
// Reads and parses a file (wide path: any folder name works).
Loaded Read(std::filesystem::path const& path);

// The leading number of a value ("0.5", " -4 ", "1.25 ; note", "+2"), locale-independent; nullopt if none or not
// finite.
std::optional<double> Number(std::string_view text) noexcept;
// A number with `decimals` places, locale-independent ("0.50").
std::string Fixed(double value, int decimals);

// Lookups: nullopt when the section or key isn't there.
std::optional<std::string> Get(File const& file, std::string_view section, std::string_view key);
std::optional<double> GetNumber(File const& file, std::string_view section, std::string_view key);

// A path as UTF-8 text for logs and UTF-8 APIs (never throws, unlike path::string() for characters outside the
// ANSI code page).
std::string Utf8(std::filesystem::path const& path);

} // namespace mevr::ini
