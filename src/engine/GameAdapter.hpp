// SPDX-License-Identifier: MIT
#pragma once

// Per-game differences of the same engine (docs/trilogy-design.md: one system, small adapters).
// Everything game-specific in engine access goes through here, so the rest of the mod compiles
// unchanged for LE1, LE2 and LE3. SDK-free: the LExSDKv2-typed accessors of the transition live
// in engine/GameAdapterSdk.hpp (research/34).
//
// The game comes from MEVR_GAME_LE1/2/3 (CMake, mevr_configure).

#include "engine/Addresses.hpp"
#include "ue3/Layout.hpp"

#include <cstdint>
#include <optional>

// The game's short name as a string literal (file names: L"LE1" L"MEVR.ini"; hook owners: "LE1" "MEVRmod").
#if defined(MEVR_GAME_LE1)
#define MEVR_GAME_NAME_W L"LE1"
#define MEVR_GAME_NAME_A "LE1"
#elif defined(MEVR_GAME_LE2)
#define MEVR_GAME_NAME_W L"LE2"
#define MEVR_GAME_NAME_A "LE2"
#else
#define MEVR_GAME_NAME_W L"LE3"
#define MEVR_GAME_NAME_A "LE3"
#endif

namespace mevr::engine::adapter {

// Game modes better shown on the flat virtual screen than head-tracked: conversations,
// cinematics, full-screen GUI, movies, the galaxy map and similar (the cameras cut, or there's no
// world to look around in). On-foot play, vehicles and the power/weapon/command wheels stay
// head-tracked. SFXGameModeManager.EGameModes differs per game.
enum class Mode { Unknown, OnFoot, Vehicle, Atlas, PowerWheel, WeaponWheel, Command, Injured,
    Conversation, Cinematic, Gui, Movie, Galaxy, Orbital, Minigame };

// Translate only known native contexts. Invalid/loading values remain Unknown rather than wrapping to a byte.
// Values are the original games' SFXGameModeManager.EGameModes (LE3 adds Atlas and InjuredShepard).
inline constexpr Mode GameMode(int mode) {
    if (mode == 0) return Mode::OnFoot;
#if defined(MEVR_GAME_LE1)
    switch (mode) {
    case 1: return Mode::Vehicle; case 2: return Mode::PowerWheel; case 3: return Mode::WeaponWheel;
    case 4: return Mode::Command; case 5: return Mode::Conversation; case 6: return Mode::Cinematic;
    case 7: return Mode::Gui; case 8: return Mode::Movie; case 9: return Mode::Galaxy; case 10: return Mode::Minigame;
    default: return Mode::Unknown;
    }
#elif defined(MEVR_GAME_LE2)
    switch (mode) {
    case 1: return Mode::PowerWheel; case 2: return Mode::WeaponWheel; case 3: return Mode::Command;
    case 4: return Mode::Vehicle; case 5: return Mode::Conversation; case 6: return Mode::Cinematic;
    case 7: return Mode::Gui; case 8: return Mode::Movie; case 9: return Mode::Galaxy; case 10: return Mode::Orbital;
    default: return Mode::Unknown;
    }
#else
    switch (mode) {
    case 1: return Mode::Vehicle; case 2: return Mode::Atlas; case 3: return Mode::PowerWheel;
    case 4: return Mode::WeaponWheel; case 5: return Mode::Command; case 6: return Mode::Injured;
    case 7: return Mode::Conversation; case 8: return Mode::Cinematic; case 9: return Mode::Gui;
    case 10: return Mode::Movie; case 11: return Mode::Galaxy; case 12: return Mode::Orbital;
    default: return Mode::Unknown;
    }
#endif
}
inline constexpr bool IsFlatScreenMode(int mode) {
    auto const context = GameMode(mode);
    return context == Mode::Conversation || context == Mode::Cinematic || context == Mode::Gui ||
        context == Mode::Movie || context == Mode::Galaxy || context == Mode::Orbital || context == Mode::Minigame;
}
inline constexpr bool IsConversationMode(int mode) { return GameMode(mode) == Mode::Conversation; }
inline constexpr bool IsGalaxyMode(int mode) { return GameMode(mode) == Mode::Galaxy; }
inline constexpr bool IsOrbitalMode(int mode) { return GameMode(mode) == Mode::Orbital; }
inline constexpr bool IsMapMode(int mode) { return IsGalaxyMode(mode) || IsOrbitalMode(mode); }
inline constexpr bool IsWristMenuMode(int mode) {
    auto const context = GameMode(mode);
    return context == Mode::OnFoot || context == Mode::Gui || context == Mode::Minigame;
}

// BioPlayerController's game-mode manager property (EGameModes: 0 = on-foot gameplay in all three
// games). LE2 and LE3 renamed it.
#if defined(MEVR_GAME_LE1)
inline constexpr char const* kGameModeManager = "GameModeManager";
#else
inline constexpr char const* kGameModeManager = "GameModeManager2";
#endif

// FFrame, the script VM's frame: FOutputDevice's vtable pointer and three DWORDs (four on LE3), then Node, Object, Code,
// Locals, PreviousFrame, packed on 4 bytes (LExSDKv2's Frame.hpp, `#pragma pack(push, 4)`), so Node is at 0x14 in LE1 and
// LE2 and at 0x18 in LE3. Measured 2026-11-04 (research/19 "The trigger's draw"): at 0x18, LE1's frames hold Node's high
// half and Object's low half; until then the skip handlers and the crash report read LE3's offsets in every game.
struct FrameOffsets {
    std::uint32_t node;
    std::uint32_t object;
    std::uint32_t code;
    std::uint32_t previous;
    std::uint32_t locals;  // between Code and PreviousFrame: the running function's parameters and locals
};
#if defined(MEVR_GAME_LE3)
inline constexpr FrameOffsets kFrame{0x18, 0x20, 0x28, 0x38, 0x30};
#else
inline constexpr FrameOffsets kFrame{0x14, 0x1C, 0x24, 0x34, 0x2C};
#endif

// ---- our reflection layer (ue3/, research/34) ----------------------------------------------------

// Where the reflection fields are. LE1's come from our Ghidra reading of 2.0.0.48602 (research/34
// section 2), LE2's from the same reading of ME2's exe (research/le2-engine-access-discovery.md "M1: what ME2's code
// showed"). LE3 isn't analysed yet: the layer stays off there and the modules using it stand down.
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
#elif defined(MEVR_GAME_LE2)
    // ME2 (MassEffect2.exe 2.0.0.48602): LE1's layout but for UFunction (8 bytes earlier: its UStruct is 8 bytes shorter)
    // and ProcessEvent's vtable slot. Each from the native that uses it (RVAs of ME2's exe):
    ue3::Layout l;
    // UObject: index (~UObject 0x104F80 clears GObjects[index]; execIsA 0xC7860's "<uninitialized>" test), flags
    // (IsPendingKill 0x9B080: bit 61 of the uint64 at +0xC), outer and name (GetPathName 0x10C3D0), class (execIsA).
    l.objectIndex = 0x08;
    l.objectFlags = 0x0C;
    l.objectOuter = 0x40;
    l.objectName = 0x48;
    l.objectClass = 0x50;
    l.pendingKillFlag = 1ull << 61;
    l.processEventSlot = 0x240;  // ProcessEvent (0xD8D50) sits at +0x240 of UObject's vtable (0xFB6BF0) and its subclasses'
    // UField: execIsA walks the class chain at +0x60; ProcessEvent walks a function's fields at +0x68.
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
    l.propertyTyped = 0xD0;  // execLetBool (0xCDA20): BoolProperty's mask and ArrayProperty's inner here
    // UFunction: ProcessEvent tests the flags (0x400 native) and iNative (0, or it returns), copies ParmsSize bytes and
    // calls Func with the return value's place at ReturnValueOffset.
    l.functionFlags = 0xD0;
    l.functionNative = 0xD4;
    l.functionParmsSize = 0xE2;
    l.functionReturnOffset = 0xE4;
    l.functionFunc = 0xF0;
    // Name entries: FName::AppendString (0x75820) and the entry's text (0x74620: at +0xC, UTF-16 when bit 30 is set).
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

// Whether this game's ME1 modules use the reflection layer once it is up (ue3::runtime::Ready). LE1: all of them. LE2:
// the layer is proven there (docs/le2-continuation-plan.md M1), but each ME1 module is ported to ME2 on its own (M3, M6),
// so until it is, it stands down as it did before LE2 had a layout. Engine access (the self-check, the console, the
// world's path, head tracking's reads) asks ue3::runtime::Up() and declares its handles ue3::kEveryGame.
#if defined(MEVR_GAME_LE1)
inline constexpr bool kModulesOnLayer = true;
#else
inline constexpr bool kModulesOnLayer = false;
#endif

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
    g.objectsRva = addresses::kGObjects;  // the RVAs: engine/Addresses.hpp
    // UObject's destructor, where it loads GObjects to clear the object's slot (research/34).
    g.objectsPattern = {"48 63 4B 08 48 8B 05 ?? ?? ?? ?? 48 C7 04 C8 00 00 00 00 48 8D 53 08 48 8D 0D", 4, 3, 7};
    g.namePoolsRva = addresses::kNamePools;
    g.namePoolCount = 8;
    // FName::AppendString, where it loads the pool array before splitting the index (research/34).
    g.namePoolsPattern = {"48 8D 15 ?? ?? ?? ?? 48 C1 F8 1D 81 E1 FF FF FF 1F 83 E0 07 48 03 0C C2", 0, 3, 7};
    g.processEventRva = addresses::kProcessEvent;
    return g;
#elif defined(MEVR_GAME_LE2)
    // LE1's two patterns match ME2's exe too: ~UObject's load of GObjects (one match, at 0x104FC3) and AppendString's load
    // of the pools before the index split (33 matches, all naming 0x1668A10).
    ReflectionGlobals g;
    g.objectsRva = addresses::kGObjects;
    g.objectsPattern = {"48 63 4B 08 48 8B 05 ?? ?? ?? ?? 48 C7 04 C8 00 00 00 00 48 8D 53 08 48 8D 0D", 4, 3, 7};
    g.namePoolsRva = addresses::kNamePools;
    g.namePoolCount = 8;
    g.namePoolsPattern = {"48 8D 15 ?? ?? ?? ?? 48 C1 F8 1D 81 E1 FF FF FF 1F 83 E0 07 48 03 0C C2", 0, 3, 7};
    g.processEventRva = addresses::kProcessEvent;
    return g;
#else
    return std::nullopt;
#endif
}

// GNatives, the bytecode handler table (UObject::GNatives[EX_Max], 0x1000 entries; research/34 section 2.1
// reads execLetBool as its entry 0x14). The RVA is the one engine/skeleton's diagnostic has used since research/14
// (LExSDKv2's BUILTIN_GNATIVES_RVA); since stage 4 an entry of engine/Addresses.hpp whose pattern (an instruction
// that loads it, `lea rcx, [GNatives]` before an indexed read) proves it at attach. The self-check's `global
// GNatives` row compares it, and checks that entry 0x14 points into .text. Only the research diagnostics read
// it (engine/natives_dump, engine/skeleton's GetBoneNames report); 0 where unknown.
inline std::uint32_t NativesTableRva() {
    return addresses::kGNatives;
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
    g.malloc = {addresses::kAppMalloc,
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B F1 41 8B D8 48 8B 0D ?? ?? ?? ?? 8B FA 48 85 C9 75 0C "
        "E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B CB 44 8B C7 8B D6",
        0x14};
    g.free = {addresses::kAppFree,
        "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 8B DA 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 0C E8 ?? ?? ?? ?? "
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B C3 48 8B D7 48 8B 5C 24 30 48 83 C4 20 5F 48 FF 60 20",
        0x0F};
    g.gmallocRva = addresses::kGMalloc;
    return g;
#elif defined(MEVR_GAME_LE2)
    // ME2: appMalloc (0x9C900) is LE1's byte for byte; appFree (0x9C8C0) is LE1's but for its last instruction: GMalloc's
    // Free is its vtable's +0x18 there (LE1's +0x20). Both take LE1's parameters (size, alignment, flags; pointer, flags).
    AllocatorGlobals g;
    g.malloc = {addresses::kAppMalloc,
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B F1 41 8B D8 48 8B 0D ?? ?? ?? ?? 8B FA 48 85 C9 75 0C "
        "E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B CB 44 8B C7 8B D6",
        0x14};
    g.free = {addresses::kAppFree,
        "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 8B DA 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 0C E8 ?? ?? ?? ?? "
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B C3 48 8B D7 48 8B 5C 24 30 48 83 C4 20 5F 48 FF 60 18",
        0x0F};
    g.gmallocRva = addresses::kGMalloc;
    return g;
#else
    return std::nullopt;
#endif
}

} // namespace mevr::engine::adapter
