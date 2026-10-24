// SPDX-License-Identifier: MIT
#pragma once

// The engine's allocator, for memory the engine may free or reallocate (research/34 section 2.3):
// appMalloc(size, alignment, flags) and appFree(pointer, flags), the two functions every engine
// allocation goes through (they call GMalloc's vtable +0x8 and +0x20). ue3::runtime finds them in
// the running exe (InitMemory); the tests install their own (Use).
//
// Strings: a parameter block owns its FStrings (ProcessEvent copies the block shallowly, research/34
// section 2.1), so a string we pass in is ours to free, and so is one the engine hands back, both
// through the engine's allocator. OwnedString does the first; FreeString the second.

#include "ue3/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mevr::ue3::memory {

using MallocFn = void* (*)(std::uint32_t size, std::uint32_t alignment, std::uint32_t flags);
using FreeFn = void (*)(void* pointer, std::uint32_t flags);

struct Allocator {
    MallocFn malloc = nullptr;
    FreeFn free = nullptr;
};

// The alignment our allocations ask for (one of the engine's own wrappers, 0x148240, asks 16).
inline constexpr std::uint32_t kAlignment = 16;

// Installs (or, with {}, removes) the allocator. Game thread, before use.
void Use(Allocator allocator);
bool Ready();

// nullptr if not Ready or the size doesn't fit the engine's 32-bit sizes.
void* Malloc(std::size_t bytes);
void Free(void* pointer);

// An FString in the engine's memory (zero-terminated, `count` including the terminator; empty text
// is a null buffer and count 0, as the engine stores it). Freed when this goes. Move-only.
class OwnedString {
public:
    OwnedString() = default;
    explicit OwnedString(std::wstring_view text);
    ~OwnedString();
    OwnedString(OwnedString&& other) noexcept;
    OwnedString& operator=(OwnedString&& other) noexcept;
    OwnedString(OwnedString const&) = delete;
    OwnedString& operator=(OwnedString const&) = delete;

    // False if the text wasn't empty and the allocation failed (no allocator).
    bool Valid() const { return valid_; }
    FString const& Get() const { return string_; }

private:
    void Release();

    FString string_{};
    bool valid_ = true;
};

// Frees a string the engine allocated (a returned or out FString) and empties it.
void FreeString(FString& string);

// UTF-8 <-> the engine's UTF-16.
std::wstring Widen(std::string_view utf8);
std::string Narrow(FString const& string);

} // namespace mevr::ue3::memory
