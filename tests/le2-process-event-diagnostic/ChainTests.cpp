// SPDX-License-Identifier: MIT
#include "plugin/ProcessEventDiagnostic.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace d = mevr::host::process_event::diagnostic;
namespace {
void Check(bool value, char const* message) { if (!value) throw std::runtime_error(message); }
struct Memory {
    std::map<std::uintptr_t, std::uint8_t> bytes;
    std::uintptr_t changeAt = 0;
    unsigned reads = 0;
    void Put(std::uintptr_t address, std::vector<std::uint8_t> const& data) {
        for (auto byte : data) bytes[address++] = byte;
    }
    void Jump(std::uintptr_t address, std::uintptr_t destination) {
        std::vector<std::uint8_t> data(5, 0);
        data[0] = 0xE9;
        auto const displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(address) - 5);
        std::memcpy(data.data() + 1, &displacement, 4);
        Put(address, data);
    }
    void Relay(std::uintptr_t address, std::uintptr_t destination) {
        std::vector<std::uint8_t> data(14, 0);
        data[0] = 0xFF; data[1] = 0x25;
        std::uint64_t const target = destination;
        std::memcpy(data.data() + 6, &target, 8);
        Put(address, data);
    }
    static bool Read(std::uintptr_t address, void* target, std::size_t size, void* raw) noexcept {
        auto& self = *static_cast<Memory*>(raw);
        auto* out = static_cast<std::uint8_t*>(target);
        bool const changed = address == self.changeAt && ++self.reads >= 3;
        for (std::size_t i = 0; i < size; ++i) {
            auto const item = self.bytes.find(address + i);
            if (item == self.bytes.end()) return false;
            out[i] = item->second;
        }
        if (changed) out[0] ^= 1;
        return true;
    }
    d::ChainResult Sample(std::uintptr_t from = 0x1000, std::uintptr_t to = 0x3000) {
        return d::CheckChain(from, to, Read, this);
    }
};
Memory Normal() {
    Memory m;
    m.Jump(0x1000, 0x2000); m.Relay(0x2000, 0x3000); m.Put(0x3000, {0x48});
    return m;
}
}
int main() {
    try {
        auto normal = Normal();
        auto result = normal.Sample();
        Check(result.status == d::ChainStatus::Matched && result.hops == 2, "normal MinHook chain");
        auto hotpatch = Normal();
        hotpatch.Jump(0x1000 - 5, 0x2000); hotpatch.Put(0x1000, {0xEB, 0xF9});
        result = hotpatch.Sample();
        Check(result.status == d::ChainStatus::Matched && result.hops == 3, "MinHook hotpatch chain");
        auto wrong = Normal(); wrong.Relay(0x2000, 0x4000); wrong.Put(0x4000, {0x90});
        Check(wrong.Sample().status == d::ChainStatus::UnknownInstruction, "another detour is not owned");
        auto unknown = Normal(); unknown.Put(0x1000, {0x48, 0xB8});
        Check(unknown.Sample().status == d::ChainStatus::UnknownInstruction, "unsupported instructions fail closed");
        auto shortJump = Normal(); shortJump.Put(0x1000, {0xEB, 0x00});
        Check(shortJump.Sample().status == d::ChainStatus::UnknownInstruction, "arbitrary short jump rejected");
        auto indirect = Normal(); indirect.bytes[0x2002] = 1;
        Check(indirect.Sample().status == d::ChainStatus::UnknownInstruction, "non-MinHook RIP displacement rejected");
        auto cycle = Normal(); cycle.Jump(0x2000, 0x1000);
        Check(cycle.Sample().status == d::ChainStatus::Cycle, "cycle rejected");
        Memory bound;
        for (std::uintptr_t at = 0x1000; at <= 0x1500; at += 0x100) bound.Jump(at, at + 0x100);
        Check(bound.Sample().status == d::ChainStatus::Exhausted, "chain bound enforced");
        auto unreadable = Normal(); unreadable.bytes.erase(0x2008);
        Check(unreadable.Sample().status == d::ChainStatus::Unreadable, "partial read rejected");
        auto missingDetour = Normal(); missingDetour.bytes.erase(0x3000);
        Check(missingDetour.Sample().status == d::ChainStatus::Unreadable, "detour must itself be readable executable memory");
        auto changed = Normal(); changed.changeAt = 0x1000;
        Check(changed.Sample().status == d::ChainStatus::Changed, "changed sampled chain rejected");
        auto nullDestination = Normal(); nullDestination.Relay(0x2000, 0);
        Check(nullDestination.Sample().status == d::ChainStatus::InvalidAddress, "null relay rejected");
        Check(normal.Sample(0, 0x3000).status == d::ChainStatus::InvalidAddress, "null target rejected");
        Check(normal.Sample(0x1000, 0x1000).status == d::ChainStatus::InvalidAddress, "self-owned target rejected");
        std::uintptr_t sum{};
        Check(!d::Add(1, -5, sum), "negative underflow rejected");
        Check(!d::Add(std::numeric_limits<std::uintptr_t>::max() - 1, 5, sum), "positive overflow rejected");
        Check(d::Snapshot{}.chain.status == d::ChainStatus::NotInstalled, "default is unavailable");
        std::cout << "17 bounded patch-chain and diagnostic-default checks passed\n";
        return 0;
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
