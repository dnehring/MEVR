// SPDX-License-Identifier: MIT
#pragma once

// Every raw address in the exe the host and the mod use (research/34 stage 4), per game, for 2.0.0.48602 (all three
// exes ship that build). Checked at attach by core/Exe against the running exe (core/ExeCheck: .pdata bounds, data
// sections, byte patterns); the host fails closed if one isn't where this table says (plugin/Entry), and the log
// names each one that moved. engine/GameAdapter's reflection and allocator globals take their RVAs from here.
//
// Sources: our Ghidra work (tools/ghidra/symbols-le1.csv; research/13, 14, 22, 23, 32, 34, 35) and, for the four
// engine functions LExSDKv2 also lists (ProcessEvent, CallFunction, GameEngine::Tick, GNatives), the same facts:
// numbers, not code. The self-check (engine/ReflectCheck, `rva` rows) compares those four with LExSDKv2's.
// Patterns: tools/rva-patterns.py over the decrypted .text dump (engine/CodeDump), each unique in LE1's .text;
// the tests check them against the dump when it's on the machine. LE2 and LE3 have no dump yet: structure only.
//
// plugin/RenderLevers keeps its own constants (the performance session's file); its entries here mirror them, and
// ctest `mevr-rva-table` (tools/check-rva-table.py) fails if a `k...Rva = 0x...` constant anywhere in src/ is missing.
//
// The current game's namespace is inline: addresses::kProcessEvent is this game's; addresses::le1::kTable always
// exists (the tests read it).

#include "core/ExeCheck.hpp"

#include <cstdint>
#include <span>

#if defined(MEVR_GAME_LE1)
#define MEVR_ADDRESSES_LE1 inline namespace le1
#else
#define MEVR_ADDRESSES_LE1 namespace le1
#endif
#if defined(MEVR_GAME_LE2)
#define MEVR_ADDRESSES_LE2 inline namespace le2
#else
#define MEVR_ADDRESSES_LE2 namespace le2
#endif
#if defined(MEVR_GAME_LE3)
#define MEVR_ADDRESSES_LE3 inline namespace le3
#else
#define MEVR_ADDRESSES_LE3 namespace le3
#endif

namespace mevr::engine::addresses {

using exe::Entry;
using exe::Kind;

MEVR_ADDRESSES_LE1 {

// UObject::ProcessEvent(this, function, parms, result): the host's hook (and the mod's without a host).
inline constexpr std::uint32_t kProcessEvent = 0x12DD20;
// UObject::CallFunction(this, stack, result, function): the mod's script-call post-handlers.
inline constexpr std::uint32_t kCallFunction = 0x12AE40;
// UGameEngine::Tick(this, deltaSeconds): the host's tick.
inline constexpr std::uint32_t kGameEngineTick = 0x3CA090;
// ULocalPlayer::CalcSceneView; FViewportClient::Draw's start and end, and where its gameplay call returns
// (engine/SceneView; research/13).
inline constexpr std::uint32_t kCalcSceneView = 0x4C5300;
inline constexpr std::uint32_t kCalcSceneViewPrimaryReturn = 0x4C7A4E;
inline constexpr std::uint32_t kViewportDraw = 0x4C71A0;
inline constexpr std::uint32_t kViewportDrawEnd = 0x4C9434;
// USkeletalMeshComponent::UpdateSkelPose (engine/BonePose; research/14).
inline constexpr std::uint32_t kUpdateSkelPose = 0x592440;
// BioPawn's cover acquisition test and its cover trace (engine/CoverAcquire; research/23).
inline constexpr std::uint32_t kTryAcquireCover = 0xAB1430;
inline constexpr std::uint32_t kCoverTrace = 0xAA92F0;
// Our reflection layer's globals (ue3/Runtime, engine/GameAdapter; research/34 section 2.3).
inline constexpr std::uint32_t kGObjects = 0x1770670;
inline constexpr std::uint32_t kNamePools = 0x16A2090;
inline constexpr std::uint32_t kGMalloc = 0x16B6700;
inline constexpr std::uint32_t kAppMalloc = 0xF2330;
inline constexpr std::uint32_t kAppFree = 0xF22F0;
// UObject::GNatives (engine/NativesDump, the skeleton's GetBoneNames report; research/14, 34).
inline constexpr std::uint32_t kGNatives = 0x16DD3C0;

inline constexpr Entry kTable[] = {
    // The host's hooks.
    {"UObject::ProcessEvent", kProcessEvent, Kind::Function, "40 55 41 56 41 57 48 81 EC 90 00 00 00 48 8D 6C", 0, 0, "host, mod"},
    {"UGameEngine::Tick", kGameEngineTick, Kind::Function,
        "48 8B C4 55 53 56 57 41 54 41 56 41 57 48 8D A8 E8 ?? ?? ?? ?? 81 EC E0 02 00 00 48", 0, 0, "host"},
    // The mod's hooks and their companions.
    {"UObject::CallFunction", kCallFunction, Kind::Function, "40 55 53 56 57 41 54 41 55 41 56 41 57 48 81 EC A8 04 00 00", 0,
        0, "mod"},
    {"ULocalPlayer::CalcSceneView", kCalcSceneView, Kind::Function,
        "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 78 FD", 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw", kViewportDraw, Kind::Function,
        "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 68 FC FF FF 48 81 EC C0 04 00 00", 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw (end)", kViewportDrawEnd, Kind::End, nullptr, 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw (CalcSceneView's return)", kCalcSceneViewPrimaryReturn, Kind::Code,
        "48 8B CF E8 ?? ?? ?? ?? 48 8B F0 48 85 C0 0F 84 ?? ?? ?? ?? F2 0F 10 45", 8, 0, "mod (scene view)"},
    {"USkeletalMeshComponent::UpdateSkelPose", kUpdateSkelPose, Kind::Function,
        "48 8B C4 44 89 40 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 B0 48", 0, 0, "mod (arm IK)"},
    {"ABioPawn::TryAcquireCover", kTryAcquireCover, Kind::Function, "48 8B C4 55 56 57 48 8D 68 A1 48 81 EC B0 00 00", 0, 0,
        "mod (cover)"},
    {"ABioPawn::CoverTrace", kCoverTrace, Kind::Function, "48 8B C4 48 89 58 08 48 89 70 10 55 57 41 56 48 8D A8 A8 FE", 0, 0,
        "mod (cover)"},
    // Our reflection layer's globals and the engine's allocator (ue3/Runtime has its own patterns too).
    {"GObjects", kGObjects, Kind::Data, "48 63 4B 08 48 8B 05 ?? ?? ?? ?? 48 C7 04 C8 00 00 00 00 48 8D 53 08 48 8D 0D", 7,
        0x15C1AF, "mod (reflection), host (crash breadcrumbs)"},
    {"the name pools", kNamePools, Kind::Data, "10 4C 8B 41 10 48 8D 2D ?? ?? ?? ?? 33 F6 48 8B", 8, 0x79F3E,
        "mod (reflection), host (crash breadcrumbs)"},
    {"GMalloc", kGMalloc, Kind::Data, "48 85 DB 74 2A 48 8B 0D ?? ?? ?? ?? 48 85 C9 75", 8, 0xED5B3, "mod (engine strings)"},
    {"appMalloc", kAppMalloc, Kind::Function,
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B F1 41 8B D8 48 8B 0D ?? ?? ?? ?? 8B FA 48 85 C9 75 0C "
        "E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B CB 44 8B C7 8B D6",
        0, 0, "mod (engine strings)"},
    {"appFree", kAppFree, Kind::Function,
        "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 8B DA 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 0C E8 ?? ?? ?? ?? "
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B C3 48 8B D7 48 8B 5C 24 30 48 83 C4 20 5F 48 FF 60 20",
        0, 0, "mod (engine strings)"},
    {"GNatives", kGNatives, Kind::Data, "74 13 0F B7 C0 48 8D 0D ?? ?? ?? ?? 48 8B 04 C1", 8, 0x102E31, "mod (diagnostics)"},
    // plugin/RenderLevers (research/22, 32, 35): its constants, mirrored.
    {"FSceneRenderer::Render", 0x76EE20, Kind::Function,
        "48 8B C4 55 41 56 41 57 48 8D 68 A1 48 81 EC C0 00 00 00 48 C7 45 17 FE FF FF FF 48 89 58 10 48", 0, 0, "render levers"},
    {"UmbraCommander::command", 0x8C34C0, Kind::Function, "83 FA 09 0F 87 ?? ?? ?? ?? 55 56 48 8D 6C 24 B8", 0, 0,
        "render levers"},
    {"FD3D11DynamicRHI::GetQueryData", 0x9A04B0, Kind::Function,
        "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 60", 0, 0, "render levers"},
    {"SetUmbraCommander / wireframe state", 0x8C1AE0, Kind::Leaf, "0F B6 C1 48 89 05 ?? ?? ?? ?? C3 CC CC CC CC CC", 0, 0,
        "render levers"},
    {"FSceneRenderer deleting destructor", 0x4ED700, Kind::Function, nullptr, 0, 0, "render levers"},
    {"FSceneRenderTargets::Allocate", 0x772EB0, Kind::Function,
        "48 8B C4 57 41 56 41 57 48 83 EC 50 48 C7 40 D8 FE FF FF FF 48 89 58 10 48 89 68 18 48 89 70 20 48 8B D9 BD", 0, 0,
        "render levers"},
    {"FSceneRenderer::RenderDpgLightTargets", 0x77BE00, Kind::Function, "4C 8B DC 55 53 56 57 49 8D 6B A1 48 81 EC F8 00", 0,
        0, "render levers"},
    {"RenderUmbraBasePass", 0x8BF8F0, Kind::Function,
        "48 8B C4 56 57 41 56 48 83 EC 60 48 C7 40 C8 FE FF FF FF 48 89 58 10 48 89 68 18 48 8B 05 ?? ??", 0, 0,
        "render levers"},
    {"translucency check", 0x7666A0, Kind::Function, "48 89 5C 24 08 4C 63 59 68 BB 01 00 00 00 4D 85", 0, 0, "render levers"},
    {"before-lights pass", 0x759240, Kind::Function,
        "48 8B C4 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 28 FF", 0, 0, "render levers"},
    {"DPG pass", 0x76D650, Kind::Function,
        "48 89 5C 24 20 55 56 57 48 83 EC 40 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 30 48", 0, 0, "render levers"},
    {"DPG lights", 0x76D0A0, Kind::Function, "4C 8B DC 55 56 57 41 56 49 8D 6B A9 48 81 EC A8", 0, 0, "render levers"},
    {"FD3D11BoundShaderState constructor", 0x9A89F0, Kind::Function, "4C 8B DC 4D 89 4B 20 49 89 53 10 49 89 4B 08 55", 0, 0,
        "render levers"},
    {"UmbraCommander visible instance", 0x8BE0B0, Kind::Function, "40 53 55 56 57 41 54 41 56 41 57 48 81 EC A0 00", 0, 0,
        "render levers"},
    {"foreground pass", 0x76CA70, Kind::Function,
        "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E9 ?? ?? ?? ?? 00 00 00 48 C7 45 AF FE FF FF", 0, 0,
        "render levers"},
    {"scene render finish", 0x76DD10, Kind::Function, "40 56 57 48 83 EC 48 33 F6 48 89 5C 24 60 48 89", 0, 0, "render levers"},
    {"UmbraCommander::IssueOcclusionQueries", 0x8B6550, Kind::Function,
        "40 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 C7 44 24 50 FE FF FF FF 48 89 9C 24 B8 00 00", 0, 0,
        "render levers"},
    {"Umbra read of one query result", 0xD48380, Kind::Leaf, "89 91 CC 01 00 00 BA 06 00 00 00 44 89 81 C8 01", 0, 0,
        "render levers"},
    {"Umbra resolve start stamp", 0x1795D40, Kind::Data, "66 0F 5A C0 F3 0F 11 05 ?? ?? ?? ?? E9 ?? ?? ?? ?? B2 01 48", 8,
        0x8C3540, "render levers"},
    {"GSceneRenderTargets", 0x167D0E0, Kind::Data, "96 80 00 00 00 48 8D 0D ?? ?? ?? ?? 89 94 24 90", 8, 0x1CEA75,
        "render levers"},
    {"GDynamicRHI", 0x17938C8, Kind::Data, "87 3E 01 00 00 48 8B 0D ?? ?? ?? ?? 48 8B 01 48", 8, 0x1AA9B4, "render levers"},
    {"scene frame counter", 0x178779C, Kind::Data, "C4 20 5B C3 CC 48 8D 05 ?? ?? ?? ?? C3 CC CC CC", 8, 0x767B7B,
        "render levers"},
    {"depth dirty flags", 0x1795D38, Kind::Data, "48 83 EA 01 75 DA 89 1D ?? ?? ?? ?? 41 89 59 60", 8, 0x8BFDA6,
        "render levers"},
    {"GSkipBeforeLights", 0x167D49C, Kind::Data, "C9 40 0F 94 C7 44 39 35 ?? ?? ?? ?? 74 2B 4C 39", 8, 0x4EC3DC,
        "render levers"},
    {"Umbra commanders", 0x1798E88, Kind::Data, "39 73 68 7E 46 48 8D 35 ?? ?? ?? ?? 0F 1F 84 00", 8, 0x8BFA2C,
        "render levers"},
    {"Umbra commander index", 0x1795D28, Kind::Data, "00 48 03 53 60 48 8B 05 ?? ?? ?? ?? 48 8B 0C C6", 8, 0x8BFA49,
        "render levers"},
};

} // le1

// LE2 and LE3 (2.0.0.48602): the hooks they run today; .pdata-checked with tools/verify_rvas.py, no patterns yet (no
// .text dump of these exes so far). The LE1-only features have no address there (0).
MEVR_ADDRESSES_LE2 {

inline constexpr std::uint32_t kProcessEvent = 0xD8D50;
inline constexpr std::uint32_t kCallFunction = 0xD6770;
inline constexpr std::uint32_t kGameEngineTick = 0x546980;
inline constexpr std::uint32_t kCalcSceneView = 0x6CEB70;
inline constexpr std::uint32_t kCalcSceneViewPrimaryReturn = 0;  // not pinned yet: any call from Draw counts
inline constexpr std::uint32_t kViewportDraw = 0x6D0A00;
inline constexpr std::uint32_t kViewportDrawEnd = 0x6D2C99;
inline constexpr std::uint32_t kUpdateSkelPose = 0;
inline constexpr std::uint32_t kTryAcquireCover = 0;
inline constexpr std::uint32_t kCoverTrace = 0;
inline constexpr std::uint32_t kGObjects = 0;  // LE2's reflection layout: stage 5
inline constexpr std::uint32_t kNamePools = 0;
inline constexpr std::uint32_t kGMalloc = 0;
inline constexpr std::uint32_t kAppMalloc = 0;
inline constexpr std::uint32_t kAppFree = 0;
inline constexpr std::uint32_t kGNatives = 0;

inline constexpr Entry kTable[] = {
    {"UObject::ProcessEvent", kProcessEvent, Kind::Function, nullptr, 0, 0, "host, mod"},
    {"UGameEngine::Tick", kGameEngineTick, Kind::Function, nullptr, 0, 0, "host"},
    {"UObject::CallFunction", kCallFunction, Kind::Function, nullptr, 0, 0, "mod"},
    {"ULocalPlayer::CalcSceneView", kCalcSceneView, Kind::Function, nullptr, 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw", kViewportDraw, Kind::Function, nullptr, 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw (end)", kViewportDrawEnd, Kind::End, nullptr, 0, 0, "mod (scene view)"},
};

} // le2

MEVR_ADDRESSES_LE3 {

inline constexpr std::uint32_t kProcessEvent = 0xF4F80;
inline constexpr std::uint32_t kCallFunction = 0xF2C30;
inline constexpr std::uint32_t kGameEngineTick = 0x5502C0;
inline constexpr std::uint32_t kCalcSceneView = 0x6CFFD0;
inline constexpr std::uint32_t kCalcSceneViewPrimaryReturn = 0;  // not pinned yet: any call from Draw counts
inline constexpr std::uint32_t kViewportDraw = 0x6D1E30;
inline constexpr std::uint32_t kViewportDrawEnd = 0x6D40DB;
inline constexpr std::uint32_t kUpdateSkelPose = 0;
inline constexpr std::uint32_t kTryAcquireCover = 0;
inline constexpr std::uint32_t kCoverTrace = 0;
inline constexpr std::uint32_t kGObjects = 0;  // LE3's reflection layout: stage 5
inline constexpr std::uint32_t kNamePools = 0;
inline constexpr std::uint32_t kGMalloc = 0;
inline constexpr std::uint32_t kAppMalloc = 0;
inline constexpr std::uint32_t kAppFree = 0;
inline constexpr std::uint32_t kGNatives = 0;

inline constexpr Entry kTable[] = {
    {"UObject::ProcessEvent", kProcessEvent, Kind::Function, nullptr, 0, 0, "host, mod"},
    {"UGameEngine::Tick", kGameEngineTick, Kind::Function, nullptr, 0, 0, "host"},
    {"UObject::CallFunction", kCallFunction, Kind::Function, nullptr, 0, 0, "mod"},
    {"ULocalPlayer::CalcSceneView", kCalcSceneView, Kind::Function, nullptr, 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw", kViewportDraw, Kind::Function, nullptr, 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw (end)", kViewportDrawEnd, Kind::End, nullptr, 0, 0, "mod (scene view)"},
};

} // le3

#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
// This game's table.
inline std::span<Entry const> Table() {
    return kTable;
}
#endif

} // namespace mevr::engine::addresses

#undef MEVR_ADDRESSES_LE1
#undef MEVR_ADDRESSES_LE2
#undef MEVR_ADDRESSES_LE3
