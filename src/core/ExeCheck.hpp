// SPDX-License-Identifier: MIT
#pragma once

// The checks behind our RVA table (engine/Addresses.hpp, research/34 stage 4): is each address the host and the mod
// use where this exe build has it? Pure over an Image (the bytes of .text, the function bounds of .pdata, which RVAs
// are data), so the tests run it over the decrypted .text dump and synthetic images; core/Exe runs it on the live exe.
//
// Each entry is checked two ways:
//   structure  a function begins at its RVA in .pdata (Function), lies inside one (Code), ends there (End), has no
//              unwind data of its own (Leaf: a leaf function), or is in a data section (Data);
//   bytes      the entry's pattern at its site: a Function's first bytes, the bytes around a Code or End address, or
//              for Data an instruction that references it RIP-relatively (its target must be the RVA).
// If the bytes aren't at the site, the pattern is looked for in all of .text: one match (for Data: matches that all
// reference one address) elsewhere means the address moved (a game patch). A function whose first bytes another
// module has replaced with a jump (a hook: E9 rel32, EB rel8, FF 25 absolute) has its bytes checked after the jump.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace mevr::exe {

enum class Kind : std::uint8_t {
    Function,  // a function's start, with unwind data (.pdata)
    Leaf,      // a leaf function's start: no unwind data of its own
    Code,      // an address inside a function (a return address)
    End,       // the first byte past a function (its .pdata end)
    Data,      // a global in a data section
};

struct Entry {
    char const* name = "";
    std::uint32_t rva = 0;
    Kind kind = Kind::Function;
    char const* pattern = nullptr;  // PEiD-style ("48 8B ?? 05"); null: structure only
    // Function/Leaf: 0. Code/End: where the address is in the pattern. Data: where the RIP-relative displacement is
    // in the pattern (the instruction ends right after it, so the target is match + offset + 4 + disp32).
    std::uint32_t offset = 0;
    // Data: where the pattern starts in this build (its RVA). Others: rva - offset (0 here means that).
    std::uint32_t site = 0;
    char const* user = "";  // who reads it ("host", "mod", "render levers"...), for the log
};

enum class Status : std::uint8_t {
    AtRva,           // structure and bytes check out at the RVA
    AtRvaHooked,     // the same, its first bytes replaced by another module's jump (checked after it)
    AtRvaUnchecked,  // structure checks out; no pattern to compare
    BytesDiffer,     // structure checks out, the bytes don't, and the pattern isn't found elsewhere: kept, warned
    Moved,           // the pattern is found elsewhere (exactly one place): the address moved
    Missing,         // the structure doesn't check out and the pattern isn't found
};

// Whether an entry with this status may be used at its RVA (the exe gate passes).
inline bool AtItsRva(Status s) {
    return s == Status::AtRva || s == Status::AtRvaHooked || s == Status::AtRvaUnchecked || s == Status::BytesDiffer;
}
char const* StatusText(Status s);
char const* KindText(Kind k);

struct Result {
    Entry const* entry = nullptr;
    Status status = Status::Missing;
    std::uint32_t found = 0;  // the RVA to use: the entry's own, or where the pattern found it (0: nowhere)
    std::string why;          // what didn't check out, for the log
};

// The function bounds .pdata gives for an RVA: the RUNTIME_FUNCTION containing it.
struct Bounds {
    bool known = true;  // false: no unwind information available (tests over a bare .text dump): structure unchecked
    bool found = false;
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

struct Image {
    std::uint32_t textRva = 0;
    std::span<std::uint8_t const> text;  // .text's bytes, from textRva
    // .pdata's function containing `rva`; null: unknown (structure unchecked).
    std::function<Bounds(std::uint32_t rva)> functionAt;
    // Whether `rva` is in a data section (not executable); null: unknown (unchecked).
    std::function<bool(std::uint32_t rva)> isData;
};

Result Check(Image const& image, Entry const& entry);
std::vector<Result> CheckAll(Image const& image, std::span<Entry const> entries);

// How many bytes of a hook's jump start at `bytes` (0: none): E9 rel32 (5), EB rel8 (2), FF 25 [rip+0] imm64 (14),
// 48 B8 imm64 FF E0 (12).
std::size_t HookJumpLength(std::span<std::uint8_t const> bytes);

} // namespace mevr::exe
