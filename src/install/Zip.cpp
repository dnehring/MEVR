// SPDX-License-Identifier: MIT
#include "install/Zip.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>

namespace mevr::install::zip {
namespace {

std::array<std::uint32_t, 256> MakeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        table[i] = c;
    }
    return table;
}

// ---- Deflate ------------------------------------------------------------------------------------------------------
class BitWriter {
public:
    explicit BitWriter(std::string& out) : out_(out) {}
    // `count` bits of `value`, least significant first (deflate's order for everything but Huffman codes).
    void Bits(std::uint32_t value, int count) {
        acc_ |= static_cast<std::uint64_t>(value) << fill_;
        fill_ += count;
        while (fill_ >= 8) {
            out_.push_back(static_cast<char>(acc_ & 0xFFu));
            acc_ >>= 8;
            fill_ -= 8;
        }
    }
    // A Huffman code: its bits from the most significant down.
    void Code(std::uint32_t code, int length) {
        std::uint32_t reversed = 0;
        for (int i = 0; i < length; ++i) {
            reversed = (reversed << 1) | ((code >> i) & 1u);
        }
        Bits(reversed, length);
    }
    void Flush() {
        if (fill_ > 0) {
            out_.push_back(static_cast<char>(acc_ & 0xFFu));
            acc_ = 0;
            fill_ = 0;
        }
    }

private:
    std::string& out_;
    std::uint64_t acc_ = 0;
    int fill_ = 0;
};

constexpr std::uint16_t kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
    4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// The fixed literal/length code (RFC 1951 3.2.6).
void Literal(BitWriter& w, unsigned symbol) {
    if (symbol <= 143) {
        w.Code(0x30 + symbol, 8);
    } else if (symbol <= 255) {
        w.Code(0x190 + (symbol - 144), 9);
    } else if (symbol <= 279) {
        w.Code(symbol - 256, 7);
    } else {
        w.Code(0xC0 + (symbol - 280), 8);
    }
}

void Match(BitWriter& w, unsigned length, unsigned distance) {
    int l = 28;
    while (kLengthBase[l] > length) {
        --l;
    }
    Literal(w, 257 + static_cast<unsigned>(l));
    if (kLengthExtra[l] > 0) {
        w.Bits(length - kLengthBase[l], kLengthExtra[l]);
    }
    int d = 29;
    while (kDistBase[d] > distance) {
        --d;
    }
    w.Code(static_cast<std::uint32_t>(d), 5);
    if (kDistExtra[d] > 0) {
        w.Bits(distance - kDistBase[d], kDistExtra[d]);
    }
}

void PutLe16(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
}

void PutLe32(std::string& out, std::uint32_t v) {
    PutLe16(out, v & 0xFFFFu);
    PutLe16(out, v >> 16);
}

std::uint32_t GetLe16(std::string_view in, std::size_t at) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(in[at])) | static_cast<std::uint32_t>(static_cast<unsigned char>(in[at + 1])) << 8;
}

std::uint32_t GetLe32(std::string_view in, std::size_t at) {
    return GetLe16(in, at) | GetLe16(in, at + 2) << 16;
}

} // namespace

std::uint32_t Crc32(std::string_view bytes) {
    static std::array<std::uint32_t, 256> const table = MakeCrcTable();
    std::uint32_t c = 0xFFFFFFFFu;
    for (char ch : bytes) {
        c = table[(c ^ static_cast<unsigned char>(ch)) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

std::string Deflate(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() / 3 + 64);
    BitWriter w(out);
    w.Bits(1, 1);  // BFINAL
    w.Bits(1, 2);  // BTYPE 01: fixed Huffman codes

    constexpr std::size_t kWindow = 32768;
    constexpr int kHashBits = 15;
    constexpr std::size_t kMaxChain = 48;
    constexpr std::size_t kMinMatch = 3;
    constexpr std::size_t kMaxMatch = 258;
    std::vector<std::int32_t> head(std::size_t{1} << kHashBits, -1);
    std::vector<std::int32_t> prev(kWindow, -1);
    auto const* data = reinterpret_cast<unsigned char const*>(bytes.data());
    std::size_t const n = bytes.size();
    auto hash = [&](std::size_t i) {
        std::uint32_t const v = static_cast<std::uint32_t>(data[i]) << 16 | static_cast<std::uint32_t>(data[i + 1]) << 8 | data[i + 2];
        return (v * 2654435761u) >> (32 - kHashBits);
    };
    auto insert = [&](std::size_t i) {
        if (i + kMinMatch <= n) {
            std::uint32_t const h = hash(i);
            prev[i % kWindow] = head[h];
            head[h] = static_cast<std::int32_t>(i);
        }
    };
    std::size_t i = 0;
    while (i < n) {
        std::size_t bestLength = 0;
        std::size_t bestDistance = 0;
        if (i + kMinMatch <= n) {
            std::int32_t candidate = head[hash(i)];
            std::size_t const limit = std::min(kMaxMatch, n - i);
            for (std::size_t chain = 0; candidate >= 0 && chain < kMaxChain; ++chain) {
                std::size_t const c = static_cast<std::size_t>(candidate);
                if (i - c > kWindow - 1 || c >= i) {
                    break;
                }
                if (data[c + bestLength] == data[i + bestLength]) {
                    std::size_t length = 0;
                    while (length < limit && data[c + length] == data[i + length]) {
                        ++length;
                    }
                    if (length > bestLength) {
                        bestLength = length;
                        bestDistance = i - c;
                        if (length == limit) {
                            break;
                        }
                    }
                }
                std::int32_t const next = prev[c % kWindow];
                if (next >= candidate) {
                    break;  // a stale slot from a position a window back
                }
                candidate = next;
            }
        }
        if (bestLength >= kMinMatch) {
            Match(w, static_cast<unsigned>(bestLength), static_cast<unsigned>(bestDistance));
            for (std::size_t k = 0; k < bestLength; ++k) {
                insert(i + k);
            }
            i += bestLength;
        } else {
            Literal(w, data[i]);
            insert(i);
            ++i;
        }
    }
    Literal(w, 256);  // end of block
    w.Flush();
    return out;
}

Writer::Writer(std::int64_t when) {
    std::time_t const t = when != 0 ? static_cast<std::time_t>(when) : std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);
    int const year = std::max(local.tm_year + 1900, 1980);
    dosDate_ = static_cast<std::uint16_t>(((year - 1980) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
    dosTime_ = static_cast<std::uint16_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
}

void Writer::Add(std::string_view name, std::string_view bytes) {
    if (finished_) {
        return;
    }
    Entry e;
    e.name = std::string(name);
    e.crc = Crc32(bytes);
    e.size = static_cast<std::uint32_t>(bytes.size());
    e.offset = static_cast<std::uint32_t>(out_.size());
    std::string deflated = bytes.empty() ? std::string() : Deflate(bytes);
    bool const deflate = !bytes.empty() && deflated.size() < bytes.size();
    e.method = deflate ? 8 : 0;
    e.compressedSize = static_cast<std::uint32_t>(deflate ? deflated.size() : bytes.size());
    PutLe32(out_, 0x04034b50u);
    PutLe16(out_, 20);      // version needed: 2.0
    PutLe16(out_, 0x0800);  // UTF-8 names
    PutLe16(out_, e.method);
    PutLe16(out_, dosTime_);
    PutLe16(out_, dosDate_);
    PutLe32(out_, e.crc);
    PutLe32(out_, e.compressedSize);
    PutLe32(out_, e.size);
    PutLe16(out_, static_cast<std::uint32_t>(e.name.size()));
    PutLe16(out_, 0);
    out_ += e.name;
    if (deflate) {
        out_ += deflated;
    } else {
        out_.append(bytes.data(), bytes.size());
    }
    entries_.push_back(std::move(e));
}

std::string const& Writer::Finish() {
    if (finished_) {
        return out_;
    }
    finished_ = true;
    std::uint32_t const start = static_cast<std::uint32_t>(out_.size());
    for (Entry const& e : entries_) {
        PutLe32(out_, 0x02014b50u);
        PutLe16(out_, 20);  // made by: MS-DOS / FAT attributes, 2.0
        PutLe16(out_, 20);
        PutLe16(out_, 0x0800);
        PutLe16(out_, e.method);
        PutLe16(out_, dosTime_);
        PutLe16(out_, dosDate_);
        PutLe32(out_, e.crc);
        PutLe32(out_, e.compressedSize);
        PutLe32(out_, e.size);
        PutLe16(out_, static_cast<std::uint32_t>(e.name.size()));
        PutLe16(out_, 0);  // extra
        PutLe16(out_, 0);  // comment
        PutLe16(out_, 0);  // disk
        PutLe16(out_, 0);  // internal attributes
        PutLe32(out_, 0);  // external attributes
        PutLe32(out_, e.offset);
        out_ += e.name;
    }
    std::uint32_t const size = static_cast<std::uint32_t>(out_.size()) - start;
    PutLe32(out_, 0x06054b50u);
    PutLe16(out_, 0);
    PutLe16(out_, 0);
    PutLe16(out_, static_cast<std::uint32_t>(entries_.size()));
    PutLe16(out_, static_cast<std::uint32_t>(entries_.size()));
    PutLe32(out_, size);
    PutLe32(out_, start);
    PutLe16(out_, 0);
    return out_;
}

bool Read(std::string_view archive, std::vector<ReadEntry>& out) {
    out.clear();
    if (archive.size() < 22) {
        return false;
    }
    std::size_t end = archive.size() - 22;
    while (GetLe32(archive, end) != 0x06054b50u) {
        if (end == 0 || archive.size() - end > 22 + 65535) {
            return false;
        }
        --end;
    }
    std::uint32_t const count = GetLe16(archive, end + 10);
    std::size_t at = GetLe32(archive, end + 16);
    for (std::uint32_t k = 0; k < count; ++k) {
        if (at + 46 > archive.size() || GetLe32(archive, at) != 0x02014b50u) {
            return false;
        }
        ReadEntry r;
        r.entry.method = static_cast<std::uint16_t>(GetLe16(archive, at + 10));
        r.entry.crc = GetLe32(archive, at + 16);
        r.entry.compressedSize = GetLe32(archive, at + 20);
        r.entry.size = GetLe32(archive, at + 24);
        std::uint32_t const nameLength = GetLe16(archive, at + 28);
        std::uint32_t const extra = GetLe16(archive, at + 30);
        std::uint32_t const comment = GetLe16(archive, at + 32);
        r.entry.offset = GetLe32(archive, at + 42);
        if (at + 46 + nameLength > archive.size()) {
            return false;
        }
        r.entry.name = std::string(archive.substr(at + 46, nameLength));
        std::size_t const local = r.entry.offset;
        if (local + 30 > archive.size() || GetLe32(archive, local) != 0x04034b50u) {
            return false;
        }
        std::size_t const data = local + 30 + GetLe16(archive, local + 26) + GetLe16(archive, local + 28);
        if (data + r.entry.compressedSize > archive.size()) {
            return false;
        }
        r.data = std::string(archive.substr(data, r.entry.compressedSize));
        out.push_back(std::move(r));
        at += 46 + nameLength + extra + comment;
    }
    return true;
}

} // namespace mevr::install::zip
