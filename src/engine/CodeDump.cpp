#include "engine/CodeDump.hpp"

#include "core/Log.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace mevr::engine::code_dump {
namespace {

// True if every page in [start, start + size) is committed and readable (no guard or
// no-access pages left behind by the DRM), so copying it can't fault.
bool IsReadable(std::uint8_t const* start, std::size_t size) {
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    std::uint8_t const* const end = start + size;
    for (std::uint8_t const* address = start; address < end;) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(address, &info, sizeof info) == 0 || info.State != MEM_COMMIT ||
            (info.Protect & kReadable) == 0 || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        address = static_cast<std::uint8_t const*>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

std::filesystem::path DumpDirectory() {
    char* appData = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&appData, &length, "LOCALAPPDATA") != 0 || appData == nullptr) {
        return {};
    }
    std::filesystem::path dir = std::filesystem::path(appData) / "MEVR" / "dump";
    std::free(appData);
    return dir;
}

// Function starts (from .pdata) that begin with a jmp out of the image: a hook's patch. Their
// first bytes in a dump are the hook's, not the game's.
std::vector<std::uint32_t> PatchedStarts(std::uint8_t const* base, IMAGE_NT_HEADERS64 const* nt) {
    std::vector<std::uint32_t> patched;
    auto const& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    std::uint32_t const imageSize = nt->OptionalHeader.SizeOfImage;
    if (directory.VirtualAddress == 0 || !IsReadable(base + directory.VirtualAddress, directory.Size)) {
        return patched;
    }
    auto const* const functions = reinterpret_cast<RUNTIME_FUNCTION const*>(base + directory.VirtualAddress);
    std::size_t const count = directory.Size / sizeof(RUNTIME_FUNCTION);
    std::uint32_t previous = 0;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t const start = functions[i].BeginAddress;
        if (start == previous || start + 14 >= imageSize) {
            continue;
        }
        previous = start;
        std::uint8_t const* const code = base + start;
        bool hooked = false;
        if (code[0] == 0xE9) {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, code + 1, sizeof displacement);
            std::int64_t const target = static_cast<std::int64_t>(start) + 5 + displacement;
            hooked = target < 0 || target >= static_cast<std::int64_t>(imageSize);
        } else if (code[0] == 0xFF && code[1] == 0x25) {  // jmp [rip+x]: an absolute jump
            hooked = true;
        }
        if (hooked) {
            patched.push_back(start);
        }
    }
    return patched;
}

// The patched_starts count an earlier dump recorded, if it did.
std::optional<std::size_t> RecordedPatchedCount(std::filesystem::path const& info) {
    std::ifstream in(info);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("patched_starts=", 0) == 0) {
            return static_cast<std::size_t>(std::strtoull(line.c_str() + 15, nullptr, 10));
        }
    }
    return std::nullopt;
}

} // namespace

void SaveOnce() {
    auto* const base = reinterpret_cast<std::uint8_t const*>(GetModuleHandleW(nullptr));
    auto const* const dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    if (base == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    auto const* const nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }
    IMAGE_SECTION_HEADER const* text = nullptr;
    IMAGE_SECTION_HEADER const* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 6) == 0) {
            text = section;
            break;
        }
    }
    if (text == nullptr) {
        return;
    }
    if (!IsReadable(base + text->VirtualAddress, text->Misc.VirtualSize)) {
        log::Warn("code dump: .text has unreadable pages; skipped");
        return;
    }

    std::filesystem::path const dir = DumpDirectory();
    if (dir.empty()) {
        return;
    }
    // One dump per exe build: the link timestamp identifies it.
    std::string const stem = std::format("MassEffect-{:08X}", nt->FileHeader.TimeDateStamp);
    std::filesystem::path const file = dir / (stem + ".text.bin");
    std::filesystem::path const infoFile = dir / (stem + ".txt");
    std::vector<std::uint32_t> const patched = PatchedStarts(base, nt);
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
        // Keep it unless this code is cleaner (a dump taken before this check says nothing).
        auto const recorded = RecordedPatchedCount(infoFile);
        if (recorded && *recorded <= patched.size()) {
            return;
        }
        log::Info("code dump: replacing {} ({} hook-patched function starts) with this one ({})", file.string(),
            recorded ? std::to_string(*recorded) : std::string("unknown"), patched.size());
    }
    std::filesystem::create_directories(dir, error);

    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<char const*>(base + text->VirtualAddress), text->Misc.VirtualSize);
    out.close();
    std::ofstream info(infoFile);
    info << std::format("timestamp=0x{:08X}\ntext_rva=0x{:X}\ntext_size=0x{:X}\npatched_starts={}\npatched=",
        nt->FileHeader.TimeDateStamp, text->VirtualAddress, text->Misc.VirtualSize, patched.size());
    for (std::size_t i = 0; i < patched.size(); ++i) {
        info << std::format("{}0x{:X}", i == 0 ? "" : ",", patched[i]);
    }
    info << "\n";
    if (!out || !info) {
        log::Warn("code dump: writing {} failed", file.string());
        std::filesystem::remove(file, error);
        return;
    }
    log::Info("code dump: saved the decrypted .text ({} MB) to {} (local research data); {} function starts "
              "already hook-patched",
        text->Misc.VirtualSize / (1024 * 1024), file.string(), patched.size());
}

} // namespace mevr::engine::code_dump
