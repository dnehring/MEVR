// SPDX-License-Identifier: MIT
#pragma once

// Per-game differences of the same engine (docs/trilogy-design.md: one system, small adapters).
// Everything game-specific in engine access goes through here, so the rest of the mod compiles
// unchanged for LE1, LE2 and LE3. SDK-free: the LExSDKv2-typed accessors of the transition live
// in engine/GameAdapterSdk.hpp (research/34).
//
// The game comes from MEVR_GAME_LE1/2/3 (CMake, mevr_configure).

#include "ue3/Layout.hpp"

#include <cstdint>
#include <optional>

// The game's short name as a wide string literal (file names: L"LE1" L"MEVR.ini").
#if defined(MEVR_GAME_LE1)
#define MEVR_GAME_NAME_W L"LE1"
#elif defined(MEVR_GAME_LE2)
#define MEVR_GAME_NAME_W L"LE2"
#else
#define MEVR_GAME_NAME_W L"LE3"
#endif

namespace mevr::engine::adapter {

// Game modes better shown on the flat virtual screen than head-tracked: conversations,
// cinematics, full-screen GUI, movies, the galaxy map and similar (the cameras cut, or there's no
// world to look around in). On-foot play, vehicles and the power/weapon/command wheels stay
// head-tracked. SFXGameModeManager.EGameModes differs per game.
inline bool IsFlatScreenMode(unsigned char mode) {
#if defined(MEVR_GAME_LE1)
    return mode >= 5 && mode <= 10;  // Conversation, Cinematic, GUI, Movie, Galaxy, Minigame
#elif defined(MEVR_GAME_LE2)
    return mode >= 5 && mode <= 10;  // Conversation, Cinematic, GUI, Movie, Galaxy, Orbital
#else
    return mode >= 7 && mode <= 12;  // Conversation, Cinematic, GUI, Movie, Galaxy, Orbital
#endif
}

// BioPlayerController's game-mode manager property (EGameModes: 0 = on-foot gameplay in all three
// games). LE2 and LE3 renamed it.
#if defined(MEVR_GAME_LE1)
inline constexpr char const* kGameModeManager = "GameModeManager";
#else
inline constexpr char const* kGameModeManager = "GameModeManager2";
#endif

// ---- our reflection layer (ue3/, research/34) ----------------------------------------------------

// Where the reflection fields are. LE1's come from our Ghidra reading of 2.0.0.48602 (research/34
// section 2). LE2 and LE3 aren't analysed yet: the layer stays off there and the modules using it
// stand down (they are LE1-only today).
inline std::optional<ue3::Layout> ReflectionLayout() {
#if defined(MEVR_GAME_LE1)
    ue3::Layout l;
    // UObject: index (UObject destructor; IsA's "<uninitialized>" test), flags (IsPendingKill,
    // vtable +0x88: bit 61), outer and name (GetPathName), class (IsA).
    l.objectIndex = 0x08;
    l.objectFlags = 0x0C;
    l.objectOuter = 0x40;
    l.objectName = 0x48;
    l.objectClass = 0x50;
    l.pendingKillFlag = 1ull << 61;
    l.processEventSlot = 0x250;  // UObject's vtable (0x140FCBE18) +0x250 = ProcessEvent
    // UField: IsA walks the class chain at +0x60; ProcessEvent walks a function's fields at +0x68.
    l.fieldSuper = 0x60;
    l.fieldNext = 0x68;
    // UStruct: ProcessEvent's first field (+0x70) and frame size (+0x78).
    l.structChildren = 0x70;
    l.structPropertySize = 0x78;
    // UProperty: ProcessEvent's parameter loops (ArrayDim, ElementSize, PropertyFlags, Offset).
    l.propertyArrayDim = 0x70;
    l.propertyElementSize = 0x74;
    l.propertyFlags = 0x78;
    l.propertyOffset = 0x94;
    l.propertyTyped = 0xD0;  // EX_LetBool: BoolProperty's mask and ArrayProperty's inner here
    // UFunction: ProcessEvent (flags, ParmsSize, ReturnValueOffset, Func); iNative after the flags.
    l.functionFlags = 0xD8;
    l.functionNative = 0xDC;
    l.functionParmsSize = 0xEA;
    l.functionReturnOffset = 0xEC;
    l.functionFunc = 0xF8;
    // Name entries: FName::AppendString (0xD62F0) and the entry's text (0xD6250).
    l.namePoolShift = 29;
    l.namePoolMask = 7;
    l.nameOffsetMask = 0x1FFFFFFF;
    l.nameChars = 0x0C;
    l.nameWideFlag = 0x40000000;
    return l;
#else
    return std::nullopt;
#endif
}

// Where the layer finds the engine's globals: an RVA (fast; right for 2.0.0.48602) and a byte
// pattern of code that references the global (survives a patch that moves it). Each pattern names
// the RIP-relative instruction inside it: where it starts, where its displacement is, its length.
struct GlobalPattern {
    char const* bytes = nullptr;
    std::uint32_t instruction = 0;
    std::uint32_t displacement = 0;
    std::uint32_t length = 0;
};
struct ReflectionGlobals {
    std::uint32_t objectsRva = 0;    // TArray<UObject*> GObjects
    GlobalPattern objectsPattern;
    std::uint32_t namePoolsRva = 0;  // the name pools' base pointers
    std::uint32_t namePoolCount = 0;
    GlobalPattern namePoolsPattern;
    std::uint32_t processEventRva = 0;  // only to cross-check the vtable slot
};

inline std::optional<ReflectionGlobals> ReflectionGlobalsForGame() {
#if defined(MEVR_GAME_LE1)
    ReflectionGlobals g;
    g.objectsRva = 0x1770670;
    // UObject's destructor, where it loads GObjects to clear the object's slot (research/34).
    g.objectsPattern = {"48 63 4B 08 48 8B 05 ?? ?? ?? ?? 48 C7 04 C8 00 00 00 00 48 8D 53 08 48 8D 0D", 4, 3, 7};
    g.namePoolsRva = 0x16A2090;
    g.namePoolCount = 8;
    // FName::AppendString, where it loads the pool array before splitting the index (research/34).
    g.namePoolsPattern = {"48 8D 15 ?? ?? ?? ?? 48 C1 F8 1D 81 E1 FF FF FF 1F 83 E0 07 48 03 0C C2", 0, 3, 7};
    g.processEventRva = 0x12DD20;
    return g;
#else
    return std::nullopt;
#endif
}

// The engine's allocator (ue3/Memory, research/34 section 2.3): appMalloc and appFree by RVA, each
// checked against its pattern (the function's own first bytes, unique in .text); the pattern finds
// them if a patch moved them. Both load GMalloc RIP-relatively at `gmallocLoad` (the instruction's
// offset in the pattern; 3 and 7 its displacement and length): the two must agree.
struct AllocatorFunction {
    std::uint32_t rva = 0;
    char const* pattern = nullptr;
    std::uint32_t gmallocLoad = 0;
};
struct AllocatorGlobals {
    AllocatorFunction malloc;  // appMalloc(size, alignment, flags): GMalloc's vtable +0x8
    AllocatorFunction free;    // appFree(pointer, flags): GMalloc's vtable +0x20
    std::uint32_t gmallocRva = 0;  // only to cross-check the two loads
};

inline std::optional<AllocatorGlobals> AllocatorGlobalsForGame() {
#if defined(MEVR_GAME_LE1)
    AllocatorGlobals g;
    g.malloc = {0xF2330,
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B F1 41 8B D8 48 8B 0D ?? ?? ?? ?? 8B FA 48 85 C9 75 0C "
        "E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B CB 44 8B C7 8B D6",
        0x14};
    g.free = {0xF22F0,
        "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 8B DA 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 0C E8 ?? ?? ?? ?? "
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B C3 48 8B D7 48 8B 5C 24 30 48 83 C4 20 5F 48 FF 60 20",
        0x0F};
    g.gmallocRva = 0x16B6700;
    return g;
#else
    return std::nullopt;
#endif
}

} // namespace mevr::engine::adapter
