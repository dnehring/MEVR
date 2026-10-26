// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// A small zip writer for the problem report (install/Report, research/33 section 8): one archive in memory, entries in
// the order they're added (REPORT.md first), each deflated (LZ77 with fixed Huffman codes, RFC 1951) or stored when
// that's smaller. UTF-8 names, no zip64 (a report is a few MB). Our own code: no zlib in the setup or the tests.
namespace mevr::install::zip {

// CRC-32 (IEEE 802.3, as zip uses it).
std::uint32_t Crc32(std::string_view bytes);

// Raw deflate (no zlib header): one final block with the fixed Huffman codes.
std::string Deflate(std::string_view bytes);

struct Entry {
    std::string name;  // forward slashes
    std::uint32_t crc = 0;
    std::uint32_t size = 0;
    std::uint32_t compressedSize = 0;
    std::uint16_t method = 0;  // 0 stored, 8 deflated
    std::uint32_t offset = 0;  // of its local header
};

class Writer {
public:
    // DOS time of every entry: `when` as local time (0: now).
    explicit Writer(std::int64_t when = 0);
    void Add(std::string_view name, std::string_view bytes);
    // The finished archive (adds the central directory; Add after Finish isn't allowed).
    std::string const& Finish();
    std::vector<Entry> const& Entries() const { return entries_; }

private:
    std::string out_;
    std::vector<Entry> entries_;
    std::uint16_t dosTime_ = 0;
    std::uint16_t dosDate_ = 0;
    bool finished_ = false;
};

// Reads an archive's central directory back (names, methods, sizes, CRCs) and each entry's data as written (still
// compressed for method 8). False if it isn't a zip we can read. For the tests and the setup's own check.
struct ReadEntry {
    Entry entry;
    std::string data;
};
bool Read(std::string_view archive, std::vector<ReadEntry>& out);

} // namespace mevr::install::zip
