// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// LE1's BioGame\PCConsoleTOC.bin: the file table the engine finds packages through. A package that isn't in it is never
// loaded, so MEVR Setup adds the cockpit's package to it and takes it out again on uninstall (research/47, docs/mako-cockpit-
// goal.md item 11). One entry at a time, everything else written back as it was read.
//
// The layout (read from the game's own file; research/47 "Players get it"): a header (0x3AB70C13, 0, the bucket count), one
// (offset, count) pair per hash bucket (the offset counted from the pair, 0 for an empty bucket), then each bucket's entries in
// bucket order: a 16-bit record size, 16-bit flags, the file's size, 20 bytes of SHA-1 (zeros in LE1), the path from the game's
// folder ("BioGame\CookedPCConsole\X.pcc") NUL-terminated and padded to 4 bytes. The file's very last record has size 0.
// An entry lives in the bucket of its bare file name's hash (Hash) modulo the bucket count.
//
// The hash is Legendary Explorer's TOCCreator's (GPL-3.0, by way of tools/cockpit/package/Toc.cs): see NOTICE.md.
namespace mevr::install::toc {

struct Entry {
    std::uint16_t flags = 0;
    std::uint32_t size = 0;
    std::array<std::uint8_t, 20> sha1{};
    std::string name;  // "BioGame\CookedPCConsole\X.pcc"
};

struct File {
    std::vector<std::vector<Entry>> buckets;
};

// The engine's hash of a bare file name ("X.pcc"), case-insensitive.
std::uint32_t Hash(std::string_view bareName);

std::optional<File> Parse(std::string_view bytes);
std::string Serialize(File const& file);

// Every entry in the bucket its name's hash gives: a TOC this code may edit.
bool Consistent(File const& file);
// The size recorded for `name` (case-insensitive), if it's there.
std::optional<std::uint32_t> Find(File const& file, std::string_view name);
// `name` with `size`: its size changed, or a new entry in its hash's bucket (at the end). Returns the size it had before.
std::optional<std::uint32_t> Set(File& file, std::string_view name, std::uint32_t size);
// True if `name` was there.
bool Remove(File& file, std::string_view name);

} // namespace mevr::install::toc
