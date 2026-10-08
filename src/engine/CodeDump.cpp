#include "engine/CodeDump.hpp"

#include "core/Log.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>

namespace mevr::engine::code_dump {
namespace {

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

    std::filesystem::path const dir = DumpDirectory();
    if (dir.empty()) {
        return;
    }
    // One dump per exe build: the link timestamp identifies it.
    std::string const stem = std::format("MassEffect-{:08X}", nt->FileHeader.TimeDateStamp);
    std::filesystem::path const file = dir / (stem + ".text.bin");
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
        return;
    }
    std::filesystem::create_directories(dir, error);

    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<char const*>(base + text->VirtualAddress), text->Misc.VirtualSize);
    out.close();
    std::ofstream info(dir / (stem + ".txt"));
    info << std::format("timestamp=0x{:08X}\ntext_rva=0x{:X}\ntext_size=0x{:X}\n", nt->FileHeader.TimeDateStamp,
        text->VirtualAddress, text->Misc.VirtualSize);
    if (!out || !info) {
        log::Warn("code dump: writing {} failed", file.string());
        std::filesystem::remove(file, error);
        return;
    }
    log::Info("code dump: saved the decrypted .text ({} MB) to {} (local research data)",
        text->Misc.VirtualSize / (1024 * 1024), file.string());
}

} // namespace mevr::engine::code_dump
