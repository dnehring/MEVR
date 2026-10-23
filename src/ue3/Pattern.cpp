// SPDX-License-Identifier: MIT
#include "ue3/Pattern.hpp"

#include <cstring>

namespace mevr::ue3::pattern {
namespace {

int Hex(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

std::optional<Compiled> Compile(std::string_view text) {
    Compiled out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        if (i + 1 >= text.size()) {
            return std::nullopt;
        }
        if (text[i] == '?' && text[i + 1] == '?') {
            out.bytes.push_back(0);
            out.wild.push_back(true);
        } else {
            int const hi = Hex(text[i]);
            int const lo = Hex(text[i + 1]);
            if (hi < 0 || lo < 0) {
                return std::nullopt;
            }
            out.bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
            out.wild.push_back(false);
        }
        i += 2;
    }
    if (out.bytes.empty()) {
        return std::nullopt;
    }
    return out;
}

std::vector<std::size_t> FindAll(std::span<std::uint8_t const> haystack, Compiled const& pattern, std::size_t limit) {
    std::vector<std::size_t> found;
    std::size_t const n = pattern.bytes.size();
    if (n == 0 || haystack.size() < n) {
        return found;
    }
    // Anchor on the first fixed byte so most positions are rejected by memchr.
    std::size_t anchor = 0;
    while (anchor < n && pattern.wild[anchor]) {
        ++anchor;
    }
    std::size_t const last = haystack.size() - n;
    std::size_t pos = 0;
    while (pos <= last && found.size() < limit) {
        if (anchor < n) {
            void const* hit = std::memchr(haystack.data() + pos + anchor, pattern.bytes[anchor], last - pos + 1);
            if (hit == nullptr) {
                break;
            }
            pos = static_cast<std::size_t>(static_cast<std::uint8_t const*>(hit) - haystack.data()) - anchor;
        }
        bool match = true;
        for (std::size_t k = 0; k < n; ++k) {
            if (!pattern.wild[k] && haystack[pos + k] != pattern.bytes[k]) {
                match = false;
                break;
            }
        }
        if (match) {
            found.push_back(pos);
        }
        ++pos;
    }
    return found;
}

std::optional<std::size_t> RipTarget(std::span<std::uint8_t const> haystack, std::size_t instruction,
                                     std::size_t displacement, std::size_t length) {
    if (instruction + displacement + 4 > haystack.size() || displacement + 4 > length) {
        return std::nullopt;
    }
    std::int32_t rel = 0;
    std::memcpy(&rel, haystack.data() + instruction + displacement, sizeof(rel));
    long long const target = static_cast<long long>(instruction + length) + rel;
    if (target < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(target);
}

} // namespace mevr::ue3::pattern
