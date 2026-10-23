// SPDX-License-Identifier: MIT
#pragma once

// Names: reading the engine's name pools (research/34 section 2.2). Pure over memory: the pools
// are an array of base pointers; tests build their own.

#include "ue3/Layout.hpp"
#include "ue3/Types.hpp"

#include <string>
#include <string_view>

namespace mevr::ue3::names {

// The name pools: `pools[i]` is the base address of pool i (from engine/GameAdapter: the pool
// array's RVA, or the byte pattern of the code that indexes it).
struct Pools {
    std::uintptr_t const* bases = nullptr;
    std::uint32_t count = 0;  // entries in `bases`
};

// The entry an index points at, or nullptr (index 0, an out-of-range pool or a null pool).
void const* Entry(Layout const& layout, Pools const& pools, std::int32_t index);

// The name's text without its number ("None" for index 0), as UTF-8. Empty if unreadable.
std::string BaseText(Layout const& layout, Pools const& pools, std::int32_t index);
// "Foo" or, with a number, "Foo_2" (the stored number minus one, as the engine prints it).
std::string ToString(Layout const& layout, Pools const& pools, Name name);

// Case-insensitive (ASCII) comparison of the name's base text with `text` (no allocation).
bool BaseEquals(Layout const& layout, Pools const& pools, std::int32_t index, std::string_view text);
// The whole name, number included, against "Foo" or "Foo_2".
bool Equals(Layout const& layout, Pools const& pools, Name name, std::string_view text);

// ASCII case folding, as UE3 compares names.
char Fold(char c);
bool EqualsNoCase(std::string_view a, std::string_view b);

} // namespace mevr::ue3::names
