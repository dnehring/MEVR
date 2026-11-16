// SPDX-License-Identifier: MIT
#pragma once
#include <Windows.h>
#include <cstdint>

// Shared native contract verified for LE1 and LE3 (research/47 and le3-galaxy).
// A capture's final post effect must bind its family's target, not the global
// backbuffer. Addresses and capture lifetime remain the native adapter's job.
namespace mevr::host::scene_capture_target {
struct Info {
    void* target = nullptr;
    std::uint32_t sizeX = 0, sizeY = 0;
};
inline Info Read(void* renderer) noexcept {
    Info info;
    __try {
        void* const target = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(renderer) + 0x18);
        if (target) {
            void** const table = *reinterpret_cast<void***>(target);
            info.target = target;
            info.sizeX = reinterpret_cast<std::uint32_t (*)(void*)>(table[0x18 / 8])(target);
            info.sizeY = reinterpret_cast<std::uint32_t (*)(void*)>(table[0x20 / 8])(target);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { info = {}; }
    return info;
}
inline bool Bind(Info const& target, void* rhi) noexcept {
    __try {
        if (!target.target || !rhi || !target.sizeX || !target.sizeY) return false;
        void** const table = *reinterpret_cast<void***>(target.target);
        void* const* const surface = reinterpret_cast<void* const* (*)(void*)>(table[0x8 / 8])(target.target);
        if (!surface || !*surface) return false;
        void** const rhiTable = *reinterpret_cast<void***>(rhi);
        reinterpret_cast<void (*)(void*, void*, void*)>(rhiTable[0x520 / 8])(rhi, *surface, nullptr);
        reinterpret_cast<void (*)(void*, std::uint32_t, std::uint32_t, float, std::uint32_t, std::uint32_t, float)>(rhiTable[0x340 / 8])(
            rhi, 0, 0, 0.0f, target.sizeX, target.sizeY, 1.0f);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
