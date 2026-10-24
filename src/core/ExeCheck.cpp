// SPDX-License-Identifier: MIT
#include "core/ExeCheck.hpp"

#include "ue3/Pattern.hpp"

#include <cstring>
#include <format>
#include <optional>

namespace mevr::exe {
namespace {

std::uint32_t SiteOf(Entry const& e) {
    if (e.kind == Kind::Data) {
        return e.site;
    }
    return e.site != 0 ? e.site : e.rva - e.offset;
}

bool InText(Image const& image, std::uint32_t rva, std::size_t size = 1) {
    return rva >= image.textRva && static_cast<std::uint64_t>(rva) - image.textRva + size <= image.text.size();
}

// The structural check; empty when it passes.
std::string Structure(Image const& image, Entry const& e) {
    auto const bounds = [&](std::uint32_t rva) { return image.functionAt ? image.functionAt(rva) : Bounds{false}; };
    switch (e.kind) {
    case Kind::Function: {
        if (!InText(image, e.rva)) {
            return "not in .text";
        }
        Bounds const b = bounds(e.rva);
        if (b.known && (!b.found || b.begin != e.rva)) {
            return b.found ? std::format("inside the function at 0x{:X}, not its start", b.begin) : "no function starts here (.pdata)";
        }
        return {};
    }
    case Kind::Leaf: {
        if (!InText(image, e.rva)) {
            return "not in .text";
        }
        Bounds const b = bounds(e.rva);
        if (b.known && b.found && b.begin != e.rva) {
            return std::format("inside the function at 0x{:X}", b.begin);
        }
        return {};
    }
    case Kind::Code: {
        if (!InText(image, e.rva)) {
            return "not in .text";
        }
        Bounds const b = bounds(e.rva);
        if (b.known && (!b.found || b.begin == e.rva)) {
            return b.found ? "a function start, not inside one" : "not inside a function (.pdata)";
        }
        return {};
    }
    case Kind::End: {
        if (e.rva == 0 || !InText(image, e.rva - 1)) {
            return "not in .text";
        }
        Bounds const b = bounds(e.rva - 1);
        if (b.known && (!b.found || b.end != e.rva)) {
            return b.found ? std::format("the function at 0x{:X} ends at 0x{:X}", b.begin, b.end) : "no function ends here (.pdata)";
        }
        return {};
    }
    case Kind::Data:
        if (InText(image, e.rva)) {
            return "in .text, not a data section";
        }
        if (image.isData && !image.isData(e.rva)) {
            return "not in a data section";
        }
        return {};
    }
    return "unknown kind";
}

bool Matches(std::span<std::uint8_t const> bytes, ue3::pattern::Compiled const& p, std::size_t from) {
    if (bytes.size() < p.bytes.size()) {
        return false;
    }
    for (std::size_t k = from; k < p.bytes.size(); ++k) {
        if (!p.wild[k] && bytes[k] != p.bytes[k]) {
            return false;
        }
    }
    return true;
}

std::uint32_t RipTargetAt(Image const& image, std::uint32_t site, std::uint32_t offset) {
    std::uint32_t const disp = site + offset;
    if (!InText(image, disp, 4)) {
        return 0;
    }
    std::int32_t rel = 0;
    std::memcpy(&rel, image.text.data() + (disp - image.textRva), sizeof(rel));
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(disp) + 4 + rel);
}

} // namespace

char const* StatusText(Status s) {
    switch (s) {
    case Status::AtRva:
        return "at its RVA";
    case Status::AtRvaHooked:
        return "at its RVA (hooked by another module: bytes checked after its jump)";
    case Status::AtRvaUnchecked:
        return "at its RVA (no pattern: structure only)";
    case Status::BytesDiffer:
        return "at its RVA, but its bytes differ (kept)";
    case Status::Moved:
        return "moved";
    case Status::Missing:
        return "missing";
    }
    return "?";
}

char const* KindText(Kind k) {
    switch (k) {
    case Kind::Function:
        return "function";
    case Kind::Leaf:
        return "leaf function";
    case Kind::Code:
        return "code";
    case Kind::End:
        return "function end";
    case Kind::Data:
        return "data";
    }
    return "?";
}

std::size_t HookJumpLength(std::span<std::uint8_t const> b) {
    if (b.size() >= 5 && b[0] == 0xE9) {
        return 5;
    }
    if (b.size() >= 2 && b[0] == 0xEB) {
        return 2;
    }
    if (b.size() >= 14 && b[0] == 0xFF && b[1] == 0x25 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0) {
        return 14;
    }
    if (b.size() >= 12 && b[0] == 0x48 && b[1] == 0xB8 && b[10] == 0xFF && b[11] == 0xE0) {
        return 12;
    }
    return 0;
}

Result Check(Image const& image, Entry const& e) {
    Result r;
    r.entry = &e;
    std::string const structure = Structure(image, e);
    std::optional<ue3::pattern::Compiled> const compiled =
        e.pattern != nullptr ? ue3::pattern::Compile(e.pattern) : std::nullopt;
    if (e.pattern != nullptr && !compiled) {
        r.why = "its pattern doesn't parse";
    }
    if (!compiled) {
        r.status = structure.empty() ? Status::AtRvaUnchecked : Status::Missing;
        r.found = structure.empty() ? e.rva : 0;
        if (!structure.empty()) {
            r.why = r.why.empty() ? structure : r.why + "; " + structure;
        }
        return r;
    }

    // The bytes at the site.
    std::uint32_t const site = SiteOf(e);
    if (structure.empty() && InText(image, site, compiled->bytes.size())) {
        std::span<std::uint8_t const> const here = image.text.subspan(site - image.textRva, compiled->bytes.size());
        bool const startsAtFunction = (e.kind == Kind::Function || e.kind == Kind::Leaf) && e.offset == 0;
        std::size_t const jump = startsAtFunction ? HookJumpLength(here) : 0;
        bool const target = e.kind != Kind::Data || RipTargetAt(image, site, e.offset) == e.rva;
        if (target && Matches(here, *compiled, 0)) {
            r.status = Status::AtRva;
            r.found = e.rva;
            return r;
        }
        if (target && jump != 0 && jump < compiled->bytes.size() && Matches(here, *compiled, jump)) {
            r.status = Status::AtRvaHooked;
            r.found = e.rva;
            return r;
        }
    }

    // Elsewhere in .text.
    std::vector<std::size_t> const hits = ue3::pattern::FindAll(image.text, *compiled, 64);
    std::uint32_t found = 0;
    bool agree = !hits.empty();
    for (std::size_t hit : hits) {
        std::uint32_t const at = image.textRva + static_cast<std::uint32_t>(hit);
        std::uint32_t const address = e.kind == Kind::Data ? RipTargetAt(image, at, e.offset) : at + e.offset;
        if (found != 0 && address != found) {
            agree = false;
        }
        found = address;
    }
    if (e.kind != Kind::Data && hits.size() > 1) {
        agree = false;  // a code pattern must be unique
    }
    if (agree && found == e.rva && structure.empty()) {
        r.status = Status::AtRva;  // a data global referenced from somewhere else now: still at its RVA
        r.found = e.rva;
        return r;
    }
    if (agree && found != e.rva) {
        r.status = Status::Moved;
        r.found = found;
        r.why = std::format("its pattern is at 0x{:X}", found);
        return r;
    }
    std::string const pattern = hits.empty() ? "its pattern isn't in .text"
        : agree ? "its pattern matches, but the structure doesn't"
                : std::format("its pattern matches {} places that don't agree", hits.size());
    if (structure.empty()) {
        r.status = Status::BytesDiffer;
        r.found = e.rva;
        r.why = pattern;
    } else {
        r.status = Status::Missing;
        r.why = structure + "; " + pattern;
    }
    return r;
}

std::vector<Result> CheckAll(Image const& image, std::span<Entry const> entries) {
    std::vector<Result> results;
    results.reserve(entries.size());
    for (Entry const& e : entries) {
        results.push_back(Check(image, e));
    }
    return results;
}

} // namespace mevr::exe
