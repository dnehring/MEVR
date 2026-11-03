// SPDX-License-Identifier: GPL-3.0-only
// The name hash is Legendary Explorer's TOCCreator's (https://github.com/ME3Tweaks/LegendaryExplorer, GPL-3.0), as
// tools/cockpit/package/Toc.cs has it. See NOTICE.md.
#include "install/Toc.hpp"

#include <algorithm>
#include <cstring>

namespace mevr::install::toc {
namespace {

constexpr std::uint32_t kMagic = 0x3AB70C13;
constexpr std::size_t kFixed = 28;  // size, flags, file size, SHA-1

std::array<std::uint32_t, 256> MakeTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t crc = i << 24;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80000000u) != 0 ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
        }
        table[i] = crc;
    }
    return table;
}

std::uint32_t Read32(std::string_view b, std::size_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

std::uint16_t Read16(std::string_view b, std::size_t at) {
    std::uint16_t v = 0;
    std::memcpy(&v, b.data() + at, 2);
    return v;
}

void Put32(std::string& out, std::size_t at, std::uint32_t v) {
    std::memcpy(out.data() + at, &v, 4);
}

void Append32(std::string& out, std::uint32_t v) {
    out.append(reinterpret_cast<char const*>(&v), 4);
}

void Append16(std::string& out, std::uint16_t v) {
    out.append(reinterpret_cast<char const*>(&v), 2);
}

std::size_t RecordSize(Entry const& e) {
    return (kFixed + e.name.size() + 1 + 3) / 4 * 4;
}

char Upper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

bool SameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return Upper(x) == Upper(y); });
}

std::string_view Bare(std::string_view name) {
    std::size_t const slash = name.find_last_of("\\/");
    return slash == std::string_view::npos ? name : name.substr(slash + 1);
}

std::size_t BucketOf(File const& file, std::string_view name) {
    return Hash(Bare(name)) % file.buckets.size();
}

} // namespace

std::uint32_t Hash(std::string_view bareName) {
    static std::array<std::uint32_t, 256> const table = MakeTable();
    std::uint32_t hash = 0;
    for (char const c : bareName) {
        // Each UTF-16 code unit of the upper-cased name, low byte then a zero (ASCII names).
        hash = (hash >> 8) ^ table[(hash ^ static_cast<std::uint8_t>(Upper(c))) & 0xFF];
        hash = (hash >> 8) ^ table[hash & 0xFF];
    }
    return hash;
}

std::optional<File> Parse(std::string_view b) {
    if (b.size() < 12 || Read32(b, 0) != kMagic) {
        return std::nullopt;
    }
    std::uint32_t const count = Read32(b, 8);
    if (count == 0 || 12 + std::size_t{count} * 8 > b.size()) {
        return std::nullopt;
    }
    File file;
    file.buckets.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::size_t const pair = 12 + std::size_t{i} * 8;
        std::uint32_t const offset = Read32(b, pair);
        std::uint32_t const entries = Read32(b, pair + 4);
        std::size_t at = pair + offset;
        for (std::uint32_t k = 0; k < entries; ++k) {
            if (at + kFixed + 1 > b.size()) {
                return std::nullopt;
            }
            std::size_t const end = b.find('\0', at + kFixed);
            if (end == std::string_view::npos) {
                return std::nullopt;
            }
            Entry e;
            std::uint16_t const record = Read16(b, at);
            e.flags = Read16(b, at + 2);
            e.size = Read32(b, at + 4);
            std::memcpy(e.sha1.data(), b.data() + at + 8, 20);
            e.name.assign(b.data() + at + kFixed, end - (at + kFixed));
            // The file's last record says 0: then the next one (there is none) would start where its padded name ends.
            at += record != 0 ? record : RecordSize(e);
            file.buckets[i].push_back(std::move(e));
        }
    }
    return file;
}

std::string Serialize(File const& file) {
    std::string out;
    Append32(out, kMagic);
    Append32(out, 0);
    Append32(out, static_cast<std::uint32_t>(file.buckets.size()));
    std::size_t const table = out.size();
    out.resize(table + file.buckets.size() * 8, '\0');
    // The last entry of the whole file gets record size 0, as the game's own file has it.
    std::size_t lastBucket = file.buckets.size();
    for (std::size_t i = file.buckets.size(); i-- > 0;) {
        if (!file.buckets[i].empty()) {
            lastBucket = i;
            break;
        }
    }
    for (std::size_t i = 0; i < file.buckets.size(); ++i) {
        std::vector<Entry> const& entries = file.buckets[i];
        std::size_t const pair = table + i * 8;
        if (!entries.empty()) {
            Put32(out, pair, static_cast<std::uint32_t>(out.size() - pair));
            Put32(out, pair + 4, static_cast<std::uint32_t>(entries.size()));
        }
        for (std::size_t k = 0; k < entries.size(); ++k) {
            Entry const& e = entries[k];
            std::size_t const record = RecordSize(e);
            bool const last = i == lastBucket && k + 1 == entries.size();
            Append16(out, last ? std::uint16_t{0} : static_cast<std::uint16_t>(record));
            Append16(out, e.flags);
            Append32(out, e.size);
            out.append(reinterpret_cast<char const*>(e.sha1.data()), e.sha1.size());
            out.append(e.name);
            out.append(record - kFixed - e.name.size(), '\0');
        }
    }
    return out;
}

bool Consistent(File const& file) {
    if (file.buckets.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < file.buckets.size(); ++i) {
        for (Entry const& e : file.buckets[i]) {
            if (BucketOf(file, e.name) != i) {
                return false;
            }
        }
    }
    return true;
}

std::optional<std::uint32_t> Find(File const& file, std::string_view name) {
    if (file.buckets.empty()) {
        return std::nullopt;
    }
    for (Entry const& e : file.buckets[BucketOf(file, name)]) {
        if (SameName(e.name, name)) {
            return e.size;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> Set(File& file, std::string_view name, std::uint32_t size) {
    if (file.buckets.empty()) {
        return std::nullopt;
    }
    std::vector<Entry>& bucket = file.buckets[BucketOf(file, name)];
    for (Entry& e : bucket) {
        if (SameName(e.name, name)) {
            std::uint32_t const before = e.size;
            e.size = size;
            return before;
        }
    }
    Entry e;
    e.size = size;
    e.name = std::string(name);
    bucket.push_back(std::move(e));
    return std::nullopt;
}

bool Remove(File& file, std::string_view name) {
    if (file.buckets.empty()) {
        return false;
    }
    std::vector<Entry>& bucket = file.buckets[BucketOf(file, name)];
    auto const it = std::find_if(bucket.begin(), bucket.end(), [&](Entry const& e) { return SameName(e.name, name); });
    if (it == bucket.end()) {
        return false;
    }
    bucket.erase(it);
    return true;
}

} // namespace mevr::install::toc
