// SPDX-License-Identifier: MIT
// SDK-owned test objects use host allocator stubs, never the game allocator.
#include <LESDK/Headers.hpp>
#include "core/qa/BoundedUtf16.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <malloc.h>
#include <new>
#include <stdexcept>

namespace u = mevr::qa::utf16;
namespace {
unsigned allocations{}, frees{}, checks{};
void Check(bool condition, char const* message) { ++checks; if (!condition) throw std::runtime_error(message); }
struct Memory {
    alignas(8) std::array<std::byte, 20> header{};
    std::uintptr_t body{};
    std::size_t bytes{};
    unsigned bodyReads{};
    static bool Read(std::uintptr_t source, void* destination, std::size_t size, void* raw) noexcept {
        auto& memory = *static_cast<Memory*>(raw);
        if (source == reinterpret_cast<std::uintptr_t>(memory.header.data() + 4) && size == u::kHeaderBytes) {
            std::memcpy(destination, memory.header.data() + 4, size); return true;
        }
        if (source != memory.body || size > memory.bytes) return false;
        ++memory.bodyReads;
        std::memcpy(destination, reinterpret_cast<void const*>(source), size); return true;
    }
    u::Result Snapshot(FString const& text) {
        std::memcpy(header.data() + 4, &text, u::kHeaderBytes);
        body = reinterpret_cast<std::uintptr_t>(text.Chars());
        bytes = (static_cast<std::size_t>(text.Length()) + 1) * sizeof(wchar_t);
        auto const address = reinterpret_cast<std::uintptr_t>(header.data() + 4);
        Check(address % 8 == 4, "actual header copied to packed alignment residue");
        return u::ReadFString(address, Read, this);
    }
};
}
void* sdkMalloc(DWORD count, DWORD alignment) {
    ++allocations;
    if (void* memory = _aligned_malloc(count, alignment)) return memory;
    throw std::bad_alloc();
}
void* sdkRealloc(void* original, DWORD count, DWORD alignment) {
    ++allocations;
    if (void* memory = _aligned_realloc(original, count, alignment)) return memory;
    throw std::bad_alloc();
}
void sdkFree(void* original) { ++frees; _aligned_free(original); }

int main() {
    static_assert(sizeof(FString) == 16 && alignof(FString) == 8);
    static_assert(sizeof(APlayerController_execConsoleCommand_Parms) == 0x24 &&
                  offsetof(APlayerController_execConsoleCommand_Parms, ReturnValue) == 0x14);
    try {
        {
            FString empty;
            Memory memory;
            auto result = memory.Snapshot(empty);
            Check(result.status == u::Status::Empty && result.header.data == 0 && result.header.count == 0 && result.header.capacity == 0,
                "actual SDK default header is null/zero/zero");
        }
        Check(allocations == 0 && frees == 0, "empty SDK object does not allocate");
        {
            FString text(L"Menu \u00E9 \U0001F600");
            unsigned const allocated = allocations, freed = frees;
            Memory memory;
            auto result = memory.Snapshot(text);
            Check(result.status == u::Status::Text && result.utf8 == "Menu \xC3\xA9 \xF0\x9F\x98\x80", "actual SDK text converted strictly");
            Check(result.header.data == reinterpret_cast<std::uintptr_t>(text.Chars()) && result.header.count == text.Length() + 1 &&
                result.header.capacity == text.Capacity() + 1, "header offsets and terminator-inclusive counts match actual SDK accessors");
            Check(allocations == allocated && frees == freed, "observer neither allocates SDK storage nor frees observed pointer");
            auto const originalPointer = result.header.data;
            auto const originalCapacity = result.header.capacity;
            text.Clear(); // Only the test's own SDK object; no game memory exists.
            memory.bodyReads = 0;
            result = memory.Snapshot(text);
            Check(result.status == u::Status::Empty && result.header.data == originalPointer && result.header.count == 0 &&
                result.header.capacity == originalCapacity && memory.bodyReads == 0, "actual SDK Clear retains allocated zero-count header");
            Check(allocations == allocated && frees == freed, "cleared observer still leaves storage ownership untouched");
        }
        Check(frees == 1, "only original SDK owner frees test backing storage");
        std::cout << checks << " actual pinned SDK header/ownership-boundary checks passed\n";
        return 0;
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
