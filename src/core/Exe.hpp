// SPDX-License-Identifier: MIT
#pragma once

// The running exe (research/34 stage 4): its base, and our RVA table (engine/Addresses.hpp) checked against it
// (core/ExeCheck). The host checks the table at attach as part of its exe gate (plugin/Entry: fail closed unless
// the version is the supported one and every address is where the table says); the mod checks it again at startup
// and logs one line. Each DLL (host, mod) has its own copy of this state.
//
// Resolve(rva) is what the hooks use (through hook::Context): a table entry's checked address (where its pattern
// found it, if it moved; null if it's missing), any other RVA as base + rva (logged once: it belongs in the table).

#include "core/ExeCheck.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace mevr::exe {

// The main module's base address (the game's exe).
std::uintptr_t Base();

// The live exe as core/ExeCheck sees it: its .text, .pdata through RtlLookupFunctionEntry, its data sections.
Image LiveImage();

struct Report {
    std::size_t total = 0;
    std::size_t atRva = 0;      // bytes checked at the RVA
    std::size_t hooked = 0;     // the same, after another module's jump
    std::size_t unchecked = 0;  // no pattern: structure only
    std::size_t differ = 0;     // structure right, bytes not (kept, warned)
    std::size_t moved = 0;
    std::size_t missing = 0;
    bool ok = false;  // nothing moved or missing
    std::string summary;  // "41 of 41 addresses at their RVAs (2 hooked by another module, 1 by structure only)"
    std::string problems;  // "UGameEngine::Tick 0x3CA090: moved to 0x3CB000 (its pattern ...); ..." or empty
};

// Checks `table` against the running exe and keeps the results for Resolve. Logs the summary and one line per
// address that isn't where the table says, prefixed with `who` ("exe:"). Call once, before the first Resolve
// (a later call checks again and replaces the results).
Report Init(std::span<Entry const> table, char const* who = "exe");
bool Initialized();
Report const& LastReport();

// The results of the last Init, in the table's order.
std::span<Result const> Results();
// The result for the table entry at this RVA, or null (not in the table, or no Init yet).
Result const* Find(std::uint32_t rva);

// See the header comment. 0 is the base itself.
void* Resolve(std::uintptr_t rva);
template <typename T>
T* ResolveTyped(std::uintptr_t rva) {
    return reinterpret_cast<T*>(Resolve(rva));
}

// The live exe's .pdata: a function begins exactly at `address`.
bool IsFunctionStart(void const* address);

} // namespace mevr::exe
