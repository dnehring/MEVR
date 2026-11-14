// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace mevr::host::process_event::diagnostic {

enum class InstallState { NotAttempted, ResolveFailed, InstallFailed, Installed };
enum class ChainStatus { NotInstalled, Matched, InvalidAddress, Unreadable, UnknownInstruction, Cycle, Exhausted, Changed };
struct ChainResult {
    ChainStatus status = ChainStatus::NotInstalled;
    std::uint32_t hops = 0;
};
struct Snapshot {
    InstallState installation = InstallState::NotAttempted;
    std::uintptr_t target = 0;
    std::uintptr_t detour = 0;
    // Installer-returned next hop; NOT proof of pristine engine code.
    std::uintptr_t nextHop = 0;
    char const* backend = "unavailable";
    char const* exeCheck = "Unavailable";
    ChainResult chain{};
};

inline constexpr std::uint64_t kWatchDurationMs = 10000;
inline constexpr std::uint32_t kMaxInspectedFunctionBytes = 64 * 1024;
struct TargetInspection {
    std::uintptr_t target{}, baseTarget{}, moduleBase{}, functionBegin{}, functionEnd{};
    std::uint32_t rva{};
    bool inMainText{}, boundsKnown{}, exactFunctionStart{}, boundsUsable{}, sameAsBase{}, jumpToBase{};
    std::uint32_t jumpHops{};
    char const* reason = "not-inspected";
    char const* jumpReason = "not-checked";
};
struct ControllerWatch {
    std::uintptr_t object{}, classAddress{}, target{}, world{}, localPlayer{};
    std::int32_t index = -1;
    std::uint32_t gameThread{};
    bool operator==(ControllerWatch const&) const = default;
};
enum class WatchStatus { Unavailable, Watching, Expired, EpochExhausted };
struct WitnessSnapshot {
    WatchStatus status = WatchStatus::Unavailable;
    char const* reason = "no-controller-watch";
    std::uint64_t epoch{}, startedMs{}, expiresMs{};
    ControllerWatch identity{};
    std::uintptr_t functionBegin{}, functionEnd{}, lastMatchingCaller{}, lastMatchingFunction{};
    std::uint64_t entries{}, originalReturns{}, matchingEntries{}, matchingOriginalReturns{};
};
inline char const* Name(WatchStatus status) noexcept {
    switch (status) {
    case WatchStatus::Unavailable: return "unavailable";
    case WatchStatus::Watching: return "watching";
    case WatchStatus::Expired: return "expired";
    case WatchStatus::EpochExhausted: return "epoch-exhausted";
    }
    return "unknown";
}

inline char const* Name(InstallState state) noexcept {
    switch (state) {
    case InstallState::NotAttempted: return "not-attempted";
    case InstallState::ResolveFailed: return "resolve-failed";
    case InstallState::InstallFailed: return "install-failed";
    case InstallState::Installed: return "installed";
    }
    return "unknown";
}
inline char const* Name(ChainStatus status) noexcept {
    switch (status) {
    case ChainStatus::NotInstalled: return "not-installed";
    case ChainStatus::Matched: return "matched-owned-detour";
    case ChainStatus::InvalidAddress: return "invalid-address";
    case ChainStatus::Unreadable: return "unreadable-or-not-executable";
    case ChainStatus::UnknownInstruction: return "unknown-instruction";
    case ChainStatus::Cycle: return "cycle";
    case ChainStatus::Exhausted: return "bound-exhausted";
    case ChainStatus::Changed: return "changed-during-sample";
    }
    return "unknown";
}

// Reader must copy only readable executable memory, without throwing or writing
// the source. Production supplies the existing guarded runtime reader boundary.
using Reader = bool (*)(std::uintptr_t, void*, std::size_t, void*) noexcept;
inline constexpr std::size_t kMaxHops = 4;

inline bool Add(std::uintptr_t base, std::int64_t offset, std::uintptr_t& result) noexcept {
    if (offset < 0) {
        auto const magnitude = static_cast<std::uint64_t>(-(offset + 1)) + 1;
        if (magnitude > base) return false;
        result = base - static_cast<std::uintptr_t>(magnitude);
    } else {
        auto const magnitude = static_cast<std::uint64_t>(offset);
        if (magnitude > std::numeric_limits<std::uintptr_t>::max() - base) return false;
        result = base + static_cast<std::uintptr_t>(magnitude);
    }
    return result != 0;
}

// Recognize only the pinned MinHook forms (hook.c / trampoline.c), not arbitrary
// x64 code. This observes patch destinations, never executes the chain or checks
// the installer-returned trampoline. Re-reading all consumed bytes detects a
// changed sample; it is NOT a synchronization guarantee for a later native call.
inline ChainResult CheckChain(std::uintptr_t target, std::uintptr_t detour,
                              Reader read, void* context) noexcept {
    ChainResult result{ChainStatus::InvalidAddress, 0};
    if (!target || !detour || target == detour || !read) return result;
    struct Node {
        std::uintptr_t address{};
        std::array<std::uint8_t, 14> bytes{};
        std::size_t size{};
    };
    std::array<Node, kMaxHops + 1> nodes{};
    std::size_t count = 0;
    std::uintptr_t current = target;
    bool requireHotpatchJump = false;
    for (;;) {
        for (std::size_t i = 0; i < count; ++i) {
            if (nodes[i].address == current) { result.status = ChainStatus::Cycle; return result; }
        }
        if (count == nodes.size()) { result.status = ChainStatus::Exhausted; return result; }
        auto& node = nodes[count++];
        node.address = current;
        node.size = 1;
        if (!read(current, node.bytes.data(), 1, context)) { result.status = ChainStatus::Unreadable; return result; }
        if (current == detour) {
            for (std::size_t i = 0; i < count; ++i) {
                std::array<std::uint8_t, 14> again{};
                if (!read(nodes[i].address, again.data(), nodes[i].size, context) ||
                    std::memcmp(again.data(), nodes[i].bytes.data(), nodes[i].size) != 0) {
                    result.status = ChainStatus::Changed; return result;
                }
            }
            result.status = ChainStatus::Matched;
            return result;
        }
        if (result.hops == kMaxHops) { result.status = ChainStatus::Exhausted; return result; }
        std::uint8_t const opcode = node.bytes[0];
        if (requireHotpatchJump && opcode != 0xE9) { result.status = ChainStatus::UnknownInstruction; return result; }
        requireHotpatchJump = false;
        if (opcode == 0xE9) node.size = 5;
        else if (opcode == 0xEB && current == target) node.size = 2;
        else if (opcode == 0xFF) node.size = 14;
        else { result.status = ChainStatus::UnknownInstruction; return result; }
        if (!read(current, node.bytes.data(), node.size, context)) { result.status = ChainStatus::Unreadable; return result; }
        if (node.bytes[0] != opcode) { result.status = ChainStatus::Changed; return result; }
        std::uintptr_t next = 0;
        if (opcode == 0xE9) {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, node.bytes.data() + 1, sizeof(displacement));
            if (!Add(current, 5 + static_cast<std::int64_t>(displacement), next)) return result;
        } else if (opcode == 0xEB) {
            // MinHook patchAbove writes EB F9 at the original entry and E9 at entry-5.
            if (node.bytes[1] != 0xF9) { result.status = ChainStatus::UnknownInstruction; return result; }
            if (!Add(current, -5, next)) return result;
            requireHotpatchJump = true;
        } else {
            if (node.bytes[1] != 0x25 || node.bytes[2] || node.bytes[3] || node.bytes[4] || node.bytes[5]) {
                result.status = ChainStatus::UnknownInstruction; return result;
            }
            std::uint64_t destination = 0;
            std::memcpy(&destination, node.bytes.data() + 6, sizeof(destination));
            if (!destination || destination > std::numeric_limits<std::uintptr_t>::max()) return result;
            next = static_cast<std::uintptr_t>(destination);
        }
        ++result.hops;
        current = next;
    }
}
} // namespace mevr::host::process_event::diagnostic
