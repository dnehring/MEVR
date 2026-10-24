// SPDX-License-Identifier: MIT
#pragma once

// Names and script frames read where nothing may be trusted (the crash report, research/36): every read of
// game memory goes through a reader that can fail (the host's checks the page and catches faults), and text
// goes into a fixed buffer, never the heap. Pure over memory, like ue3/Reflect: the tests give it a plain
// copy as the reader and a synthetic engine.

#include "core/FixedText.hpp"
#include "ue3/Layout.hpp"
#include "ue3/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mevr::ue3::safe {

// Copies `size` bytes from `address`; false if they can't be read.
using ReadFn = bool (*)(void const* address, void* out, std::size_t size);

struct Memory {
    Layout layout;
    std::uintptr_t poolArray = 0;  // the name pools' base pointer array
    std::uint32_t poolCount = 0;
    std::uintptr_t objects = 0;    // &GObjects, a TArray<Object*>
    ReadFn read = nullptr;

    template <class T>
    bool Read(std::uintptr_t address, T& out) const {
        return read != nullptr && address != 0 && read(reinterpret_cast<void const*>(address), &out, sizeof(T));
    }
};

// The name's text: "None", "Foo" or "Foo_2". False (with "<name?>" appended) if it can't be read.
bool AppendName(Memory const& m, Name name, text::Appender& out);
// Whether `object` sits in GObjects at its own index: the check that a pointer is a live object.
bool Listed(Memory const& m, std::uintptr_t object);
// The object's own name.
bool AppendObjectName(Memory const& m, std::uintptr_t object, text::Appender& out);
// "Outer.Outer.Name" (the outer chain, at most 8 deep).
bool AppendPath(Memory const& m, std::uintptr_t object, text::Appender& out);
// "Class Outer.Name", as ue3::Reflection::FullName.
bool AppendFull(Memory const& m, std::uintptr_t object, text::Appender& out);
// The object's own name against `text` (ASCII case-insensitively; no number).
bool NameIs(Memory const& m, std::uintptr_t object, std::string_view text);

// Where an FFrame keeps its fields, and where its vtable pointer may point (the exe's read-only data).
struct FrameLayout {
    std::uint32_t node = 0;      // UStruct*: the function (or state, or class) being run
    std::uint32_t object = 0;    // UObject*: the object it runs on
    std::uint32_t code = 0;      // BYTE*: the next bytecode
    std::uint32_t previous = 0;  // FFrame*: the caller's frame
    std::uint32_t structScript = 0;  // UStruct's Script: TArray<BYTE> (the node's bytecode)
    std::uintptr_t vtableLo = 0;
    std::uintptr_t vtableHi = 0;
};

struct Frame {
    std::uintptr_t at = 0;
    std::uintptr_t node = 0;
    std::uintptr_t object = 0;
    std::uintptr_t code = 0;
    std::uintptr_t previous = 0;
    std::int64_t codeOffset = -1;  // code - Script.data when inside the node's bytecode, else -1
    std::int32_t scriptSize = -1;
};

// The script frames on a stack between `lo` and `hi`, lowest address (the innermost call) first: each an
// 8-byte aligned FFrame whose vtable is in range, whose node is a listed Function, State or Class and whose
// object is listed. At most `max`.
std::size_t FindFrames(Memory const& m, FrameLayout const& layout, std::uintptr_t lo, std::uintptr_t hi, Frame* out,
    std::size_t max);

} // namespace mevr::ue3::safe
