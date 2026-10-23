// SPDX-License-Identifier: MIT
#pragma once

#include "loader/SpiAbi.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The game-independent parts of our ASI loader (research/33), unit-tested in tests/InstallTests.cpp.
namespace mevr::loader {

// Which program loaded us, from its exe name (case-insensitive): MassEffect1/2/3.exe, MassEffectLauncher.exe;
// nullopt for anything else (a test host).
std::optional<spi::Game> HostFromExeName(std::wstring_view exeName);

// A PEiD-style pattern: hex bytes and "?" / "??" wildcards separated by spaces. nullopt if malformed; `tooLong` set
// when it has more than 100 bytes (or the text more than 299 characters).
struct Pattern {
    std::vector<std::uint8_t> bytes;
    std::vector<bool> mask;  // true: must match
};
std::optional<Pattern> ParsePattern(std::string_view text, bool* tooLong = nullptr);
// The first offset in `memory` where `pattern` matches, or nullopt.
std::optional<std::size_t> FindPattern(std::span<std::uint8_t const> memory, Pattern const& pattern);

// The DRM check: an EA-protected exe has its .text encrypted on disk and decrypted in memory before the game runs.
// We compare `samples` 16-byte windows spread over .text; it counts as decrypted once at least 90% differ from the
// file (a few may equal it by relocation or chance; while still encrypted almost all are equal).
bool LooksDecrypted(int differing, int samples);
// The offsets of `samples` windows of `window` bytes spread evenly over a section of `size` bytes.
std::vector<std::size_t> SampleOffsets(std::size_t size, int samples, std::size_t window);

// Whether a plugin that declared `gameFlags` and `minVersion` should be attached in `host` (nullopt: a test host,
// which attaches everything).
bool ShouldAttach(int gameFlags, int minVersion, std::optional<spi::Game> host);

// The ASI files to load, in load order (by name, case-insensitively).
std::vector<std::wstring> SortAsiNames(std::vector<std::wstring> names);

} // namespace mevr::loader
