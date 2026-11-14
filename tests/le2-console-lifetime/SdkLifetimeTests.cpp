// SPDX-License-Identifier: MIT
// Actual pinned SDK empty FString ctor/dtor in packed parameter storage.
// Fake allocator symbols only; no game process, heap, SDK wrappers or dispatch.
#include <LESDK/Headers.hpp>
#include "engine/ScopedUnionMember.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using Parms = APlayerController_execConsoleCommand_Parms;
using Guard = mevr::engine::detail::ScopedUnionMember<FString>;
using Unaligned = FString __unaligned*;
static_assert(sizeof(Parms) == 0x24 && offsetof(Parms, ReturnValue) == 0x14);
static_assert(alignof(FString) == 8 && alignof(Parms) == 4);
static_assert(std::is_nothrow_destructible_v<FString>);
static_assert(std::is_same_v<Guard::StoragePointer, Unaligned>);
static_assert(!std::is_same_v<FString*, Unaligned>, "unaligned qualifier must be distinguishable in this check");
static_assert(std::is_same_v<decltype(std::construct_at(std::declval<Unaligned>())), Unaligned>,
              "construct_at must preserve the MSVC unaligned pointer qualification");

namespace {
unsigned allocatorCalls{};
void Check(bool ok, char const* why) { if (!ok) throw std::runtime_error(why); }
__declspec(noinline) void InspectEmpty(Parms const& parms) {
    auto const* bytes = reinterpret_cast<unsigned char const volatile*>(std::addressof(parms));
    for (unsigned i = 0; i < sizeof(Parms); ++i) Check(bytes[i] == 0, "empty parameter bytes not zero");
}
}
void* sdkMalloc(DWORD, DWORD) { ++allocatorCalls; std::abort(); }
void* sdkRealloc(void*, DWORD, DWORD) { ++allocatorCalls; std::abort(); }
void sdkFree(void*) { ++allocatorCalls; std::abort(); }

// Kept as a named, non-inlined function for inspection of emitted code. The
// address expressions themselves must compile without C4366 or suppression.
__declspec(noinline) void ExerciseSdkPackedEmpty(Parms& parms) {
    Guard command(&parms.Command);
    Guard returned(&parms.ReturnValue);
    InspectEmpty(parms);
}
int main() {
    try {
        for (std::size_t residue : {0u, 4u, 8u, 12u}) {
            alignas(16) std::array<unsigned char, 96> storage;
            storage.fill(0xCD);
            std::size_t const offset = 16 + residue;
            auto* parms = ::new (static_cast<void*>(storage.data() + offset)) Parms;
            auto const address = reinterpret_cast<std::uintptr_t>(parms);
            Check(address % 16 == residue, "unexpected parameter alignment residue");
            Check(reinterpret_cast<std::uintptr_t>(std::addressof(parms->ReturnValue)) - address == 0x14,
                  "packed return offset changed");
            ExerciseSdkPackedEmpty(*parms);
            InspectEmpty(*parms);
            parms->~Parms();
            for (std::size_t i = 0; i < storage.size(); ++i)
                if (i < offset || i >= offset + sizeof(Parms)) Check(storage[i] == 0xCD, "write escaped packed parameter block");
        }
        Check(allocatorCalls == 0, "empty lifetime must not use an allocator");
        std::cout << "Actual SDK packed0x24 empty FString lifetime passed at base residues0/4/8/12; no allocator calls\n";
        return 0;
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
