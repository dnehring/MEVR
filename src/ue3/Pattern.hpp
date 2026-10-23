// SPDX-License-Identifier: MIT
#pragma once

// Byte patterns: the fallback that finds an engine global when its compiled-in RVA doesn't check
// out (a game patch moved it). Pure functions over a byte span, unit-tested offline.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace mevr::ue3::pattern {

// "48 8D 15 ?? ?? ?? ?? 48 C1" -> bytes and a wildcard mask. nullopt on a malformed pattern.
struct Compiled {
    std::vector<std::uint8_t> bytes;
    std::vector<bool> wild;
};
std::optional<Compiled> Compile(std::string_view text);

// Every offset in `haystack` where the pattern matches (at most `limit`).
std::vector<std::size_t> FindAll(std::span<std::uint8_t const> haystack, Compiled const& pattern, std::size_t limit = 8);

// The target of a RIP-relative operand: the int32 at `displacement` inside an instruction that
// starts at `instruction` and is `length` bytes long. Offsets are relative to `haystack`'s start.
std::optional<std::size_t> RipTarget(std::span<std::uint8_t const> haystack, std::size_t instruction,
                                     std::size_t displacement, std::size_t length);

} // namespace mevr::ue3::pattern
