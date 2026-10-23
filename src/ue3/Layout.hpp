// SPDX-License-Identifier: MIT
#pragma once

// Where the engine keeps the reflection fields we read: the UObject header, UField, UStruct,
// UProperty and UFunction, and the name entries. One instance per game, from engine/GameAdapter.hpp.
// Every value is ours, from reading the engine's own code (research/34 section 2 lists the
// functions each one comes from); nothing here is taken from a generated SDK.

#include <cstdint>

namespace mevr::ue3 {

struct Layout {
    // UObject
    std::uint32_t objectIndex = 0;   // int32: the object's slot in GObjects
    std::uint32_t objectFlags = 0;   // uint64
    std::uint32_t objectOuter = 0;   // Object*
    std::uint32_t objectName = 0;    // Name (8 bytes)
    std::uint32_t objectClass = 0;   // Class*
    std::uint64_t pendingKillFlag = 0;  // in objectFlags: the object is being destroyed
    std::uint32_t processEventSlot = 0;  // byte offset of ProcessEvent in every UObject's vtable

    // UField
    std::uint32_t fieldSuper = 0;  // Field*: a struct's parent struct (a class's superclass)
    std::uint32_t fieldNext = 0;   // Field*: the next field of the owning struct

    // UStruct
    std::uint32_t structChildren = 0;      // Field*: the first field declared in this struct
    std::uint32_t structPropertySize = 0;  // int32: the size of an instance's own data

    // UProperty
    std::uint32_t propertyArrayDim = 0;     // int32: static array count (1 for a scalar)
    std::uint32_t propertyElementSize = 0;  // int32
    std::uint32_t propertyFlags = 0;        // uint64 (kParm, kOutParm, kReturnParm...)
    std::uint32_t propertyOffset = 0;       // int32: offset in the owner's instance data
    // The first field after UProperty in its typed subclasses: BoolProperty's bit mask (uint32),
    // StructProperty's struct, ObjectProperty's class, ArrayProperty's inner property.
    std::uint32_t propertyTyped = 0;

    // UFunction
    std::uint32_t functionFlags = 0;        // uint32
    std::uint32_t functionNative = 0;       // uint16: the native index (0: not a GNatives entry)
    std::uint32_t functionParmsSize = 0;    // uint16: the parameter block's size
    std::uint32_t functionReturnOffset = 0; // uint16: the return value's offset in that block
    std::uint32_t functionFunc = 0;         // void*: the native (or script) entry point

    // Name entries (the name pools): an index picks pool (index >> poolShift) & poolMask and the
    // byte offset index & offsetMask in it; the characters start at nameChars, UTF-16 when the
    // entry's first uint32 has nameWideFlag, Latin-1 otherwise, zero-terminated.
    std::uint32_t namePoolShift = 0;
    std::uint32_t namePoolMask = 0;
    std::uint32_t nameOffsetMask = 0;
    std::uint32_t nameChars = 0;
    std::uint32_t nameWideFlag = 0;
};

// UProperty flags (the same in every UE3 game; ProcessEvent's parameter loop tests them).
namespace property_flags {
inline constexpr std::uint64_t kOptionalParm = 0x10;
inline constexpr std::uint64_t kParm = 0x80;
inline constexpr std::uint64_t kOutParm = 0x100;
inline constexpr std::uint64_t kReturnParm = 0x400;
} // namespace property_flags

// UFunction flags.
namespace function_flags {
inline constexpr std::uint32_t kNative = 0x400;
} // namespace function_flags

} // namespace mevr::ue3
