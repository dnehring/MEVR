// SPDX-License-Identifier: MIT
#include "core/Exe.hpp"

#include "core/Log.hpp"

#include <Windows.h>

#include <cstring>
#include <format>
#include <mutex>
#include <vector>

namespace mevr::exe {
namespace {

std::vector<Result> gResults;
Report gReport;
bool gInitialized = false;

std::mutex gUnlistedLock;
std::vector<std::uintptr_t> gUnlistedLogged;

struct Section {
    std::uint32_t rva = 0;
    std::uint32_t size = 0;
    std::uint32_t characteristics = 0;
    char name[9]{};
};

std::vector<Section> Sections(std::uintptr_t base) {
    std::vector<Section> out;
    auto const* dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    if (base == 0 || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return out;
    }
    auto const* nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return out;
    }
    IMAGE_SECTION_HEADER const* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        Section s;
        s.rva = section->VirtualAddress;
        s.size = section->Misc.VirtualSize;
        s.characteristics = section->Characteristics;
        std::memcpy(s.name, section->Name, 8);
        out.push_back(s);
    }
    return out;
}

} // namespace

std::uintptr_t Base() {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}

Image LiveImage() {
    std::uintptr_t const base = Base();
    std::vector<Section> const sections = Sections(base);
    Image image;
    for (Section const& s : sections) {
        if (std::strcmp(s.name, ".text") == 0) {
            image.textRva = s.rva;
            image.text = std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const*>(base + s.rva), s.size);
        }
    }
    image.functionAt = [base](std::uint32_t rva) {
        Bounds b;
        DWORD64 imageBase = 0;
        auto const* const entry = RtlLookupFunctionEntry(static_cast<DWORD64>(base) + rva, &imageBase, nullptr);
        if (entry != nullptr && imageBase == base) {
            b.found = true;
            b.begin = entry->BeginAddress;
            b.end = entry->EndAddress;
        }
        return b;
    };
    image.isData = [sections](std::uint32_t rva) {
        for (Section const& s : sections) {
            if (rva >= s.rva && rva - s.rva < s.size) {
                bool const executable = (s.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
                bool const data = (s.characteristics & (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_CNT_UNINITIALIZED_DATA)) != 0;
                return !executable && data;
            }
        }
        return false;
    };
    return image;
}

Report Init(std::span<Entry const> table, char const* who) {
    Image const image = LiveImage();
    Report report;
    if (image.text.empty()) {
        gResults.clear();
        report.summary = "the exe has no .text section";
        report.problems = report.summary;
        log::Error("{}: {}", who, report.summary);
        gReport = report;
        gInitialized = true;
        return report;
    }
    gResults = CheckAll(image, table);
    std::string hookedNames;
    report.total = gResults.size();
    for (Result const& r : gResults) {
        switch (r.status) {
        case Status::AtRva:
            ++report.atRva;
            break;
        case Status::AtRvaHooked:
            ++report.hooked;
            hookedNames += std::format("{}{}", hookedNames.empty() ? "" : ", ", r.entry->name);
            break;
        case Status::AtRvaUnchecked:
            ++report.unchecked;
            break;
        case Status::BytesDiffer:
            ++report.differ;
            break;
        case Status::Moved:
            ++report.moved;
            break;
        case Status::Missing:
            ++report.missing;
            break;
        }
        if (r.status == Status::Moved || r.status == Status::Missing) {
            report.problems += std::format("{}{} 0x{:X}: {}{}", report.problems.empty() ? "" : "; ", r.entry->name, r.entry->rva,
                StatusText(r.status), r.status == Status::Moved ? std::format(" to 0x{:X}", r.found) : std::string());
        }
    }
    report.ok = report.moved == 0 && report.missing == 0;
    std::size_t const here = report.atRva + report.hooked + report.unchecked + report.differ;
    report.summary = std::format("{} of {} addresses at their RVAs ({} by their bytes, {} hooked by another module, {} by "
                                 "structure only, {} with different bytes){}",
        here, report.total, report.atRva, report.hooked, report.unchecked, report.differ,
        report.ok ? "" : std::format("; {} moved, {} missing", report.moved, report.missing));
    if (report.ok) {
        log::Info("{}: {}", who, report.summary);
    } else {
        log::Error("{}: {}", who, report.summary);
    }
    if (!hookedNames.empty()) {
        log::Info("{}:   already hooked: {}", who, hookedNames);
    }
    for (Result const& r : gResults) {
        if (r.status == Status::BytesDiffer) {
            log::Warn("{}:   {} ({}, {}) 0x{:X}: {} ({})", who, r.entry->name, KindText(r.entry->kind), r.entry->user,
                r.entry->rva, StatusText(r.status), r.why);
        } else if (r.status == Status::Moved || r.status == Status::Missing) {
            log::Error("{}:   {} ({}, {}) 0x{:X}: {}{} ({})", who, r.entry->name, KindText(r.entry->kind), r.entry->user,
                r.entry->rva, StatusText(r.status), r.status == Status::Moved ? std::format(" to 0x{:X}", r.found) : std::string(),
                r.why);
        }
    }
    gReport = report;
    gInitialized = true;
    return report;
}

bool Initialized() {
    return gInitialized;
}

Report const& LastReport() {
    return gReport;
}

std::span<Result const> Results() {
    return gResults;
}

Result const* Find(std::uint32_t rva) {
    for (Result const& r : gResults) {
        if (r.entry != nullptr && r.entry->rva == rva) {
            return &r;
        }
    }
    return nullptr;
}

void* Resolve(std::uintptr_t rva) {
    std::uintptr_t const base = Base();
    if (rva == 0) {
        return reinterpret_cast<void*>(base);
    }
    if (rva <= 0xFFFFFFFFu) {
        if (Result const* r = Find(static_cast<std::uint32_t>(rva))) {
            if (AtItsRva(r->status)) {
                return reinterpret_cast<void*>(base + rva);
            }
            return r->status == Status::Moved ? reinterpret_cast<void*>(base + r->found) : nullptr;
        }
    }
    if (gInitialized) {
        std::lock_guard const lock(gUnlistedLock);
        bool seen = false;
        for (std::uintptr_t logged : gUnlistedLogged) {
            seen = seen || logged == rva;
        }
        if (!seen) {
            gUnlistedLogged.push_back(rva);
            log::Warn("exe: RVA 0x{:X} isn't in our RVA table (engine/Addresses.hpp): used unchecked", rva);
        }
    }
    return reinterpret_cast<void*>(base + rva);
}

bool IsFunctionStart(void const* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

} // namespace mevr::exe
