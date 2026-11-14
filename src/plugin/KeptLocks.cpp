// SPDX-License-Identifier: MIT
#include "plugin/KeptLocks.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <format>

namespace mevr::host::kept_locks {

namespace {

// ntdll's export (kernel32's DeleteCriticalSection forwards to it, so the exe's import slot holds this address). It
// returns an NTSTATUS, which ntdll's own callers may read.
using tRtlDeleteCriticalSection = LONG(NTAPI)(RTL_CRITICAL_SECTION* section);
tRtlDeleteCriticalSection* gOriginal = nullptr;

std::uintptr_t gImageLo = 0;
std::uintptr_t gImageHi = 0;

// Written by the detour on whatever thread deletes (no allocation, no lock: it may run under the loader lock).
constexpr unsigned kNoted = 8;
std::atomic<unsigned> gKept{0};
std::atomic<std::uint32_t> gRva[kNoted]{};
std::atomic<std::uint64_t> gFirstMs{0};

LONG NTAPI DeleteDetour(RTL_CRITICAL_SECTION* section) {
    auto const address = reinterpret_cast<std::uintptr_t>(section);
    if (address >= gImageLo && address < gImageHi) {
        unsigned const n = gKept.fetch_add(1);
        if (n == 0) {
            gFirstMs.store(GetTickCount64());
        }
        if (n < kNoted) {
            gRva[n].store(static_cast<std::uint32_t>(address - gImageLo));
        }
        return 0;  // STATUS_SUCCESS: the section stays initialized and usable
    }
    return gOriginal(section);
}

} // namespace

bool Install(hook::Context& context) {
    auto const base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto const* const dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    auto const* const nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
    gImageLo = base;
    gImageHi = base + nt->OptionalHeader.SizeOfImage;
    HMODULE const ntdll = GetModuleHandleW(L"ntdll.dll");
    auto* const target = ntdll != nullptr
        ? reinterpret_cast<tRtlDeleteCriticalSection*>(GetProcAddress(ntdll, "RtlDeleteCriticalSection"))
        : nullptr;
    if (target == nullptr || !hook::Install(context, "RtlDeleteCriticalSection", target, &DeleteDetour, gOriginal)) {
        log::Warn("kept locks: RtlDeleteCriticalSection not hooked; a quit may fault in the game's deleted lock (research/36)");
        return false;
    }
    log::Info("kept locks: the game's static critical sections are no longer deleted (its teardown destroyed a lock its "
              "worker still takes: the fault at quit, research/36)");
    return true;
}

std::string Summary() {
    unsigned const kept = gKept.load();
    if (kept == 0) {
        return "no static lock of the game's deleted";
    }
    std::string text = std::format("{} deletion(s) of the game's static locks skipped, the first {} ms ago:", kept,
        GetTickCount64() - gFirstMs.load());
    for (unsigned i = 0; i < kept && i < kNoted; ++i) {
        text += std::format(" exe+0x{:X}", gRva[i].load());
    }
    return text;
}

} // namespace mevr::host::kept_locks
