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
// the tests check them against the dump when it's on the machine. LE2's likewise over ME2's dump (M1, research/
// le2-engine-access-discovery.md); LE3 has no dump yet: structure only.
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
// ABioWeaponRanged::TickFire (research/14, 17). The pawn start query inside it can omit CurrentWeapon.
inline constexpr std::uint32_t kRangedTickFire = 0xC621E0;
// USkeletalMeshComponent::UpdateBounds (engine/BodyBounds; research/69 "The culling camera is ahead of the eye").
inline constexpr std::uint32_t kSkelUpdateBounds = 0x58EEC0;
// UPrimitiveComponent's hand-over of its Bounds to its Umbra object (the box as the object's matrix): UpdateBounds ends with it.
inline constexpr std::uint32_t kPrimitiveUmbraBounds = 0x25CFE0;
// FISACTSoundSource::Update: a playing source's volume, pitch, relative flag and position sent to ISACT each frame
// (engine/VoicePlace; research/69 "The voices, placed").
inline constexpr std::uint32_t kSoundSourceUpdate = 0x7BA90;
// FName::FName(const TCHAR*, EFindName): an engine name from text; the mod calls it with FNAME_Find (0) only, so it never adds a
// name (engine/NameLookup; research/69 "People walking to their marks": the AI subtype 'ALL').
inline constexpr std::uint32_t kNameFromText = 0xD1150;
// BioPawn's cover acquisition test and its cover trace (engine/CoverAcquire; research/23).
inline constexpr std::uint32_t kTryAcquireCover = 0xAB1430;
inline constexpr std::uint32_t kCoverTrace = 0xAA92F0;
// BioPlayerSelection's native UpdateSelection body (engine/SelectionGaze; research/40).
inline constexpr std::uint32_t kUpdateSelection = 0xC79D70;
// The conversation's one placement function (UBioConversation's vtable slot 0x2C0: an actor onto a stage mark, or a facing
// alone) and BioWorldInfo's interruption of an ambient conversation (slot 0x9F0) (engine/ConversationHooks; research/69).
inline constexpr std::uint32_t kConversationSetPositionRotator = 0xC862D0;
inline constexpr std::uint32_t kInterruptConversation = 0xB60460;
// AActor::MoveWithInterpMoveTrack (the actor's vtable slot +0x558, the same function for pawns): the one place a matinee's
// move track places its actor (engine/ConversationHooks, game/AuthoredMoment; research/69 "The natives behind an authored
// moment").
inline constexpr std::uint32_t kMoveWithInterpMoveTrack = 0x4B75F0;
// UBioUIWorld's native Tick (its vtable's slot 104): the command queue, then UWorld::Tick of the UI world with GWorld swapped
// (game/UiWorldFigure, research/57 section 7; called through the object's vtable after checking the slot holds it).
inline constexpr std::uint32_t kUiWorldTick = 0xB3B810;
// Our reflection layer's globals (ue3/Runtime, engine/GameAdapter; research/34 section 2.3).
inline constexpr std::uint32_t kGObjects = 0x1770670;
inline constexpr std::uint32_t kNamePools = 0x16A2090;
inline constexpr std::uint32_t kGMalloc = 0x16B6700;
inline constexpr std::uint32_t kAppMalloc = 0xF2330;
inline constexpr std::uint32_t kAppFree = 0xF22F0;
// UObject::GNatives (engine/NativesDump, the skeleton's GetBoneNames report; research/14, 34).
inline constexpr std::uint32_t kGNatives = 0x16DD3C0;

// Blur/no-blur combined post-process variants. Local native-layout comparison: research/62.
inline constexpr std::uint32_t kMotionBlurProxyRender = 0x279360;
inline constexpr std::uint32_t kNoBlurProxyRender = 0x27A100;
struct MotionBlurProxyPair { std::uint32_t blur, sharp; std::uint64_t blurFingerprint, sharpFingerprint; };
inline constexpr MotionBlurProxyPair kMotionBlurProxyPairs[] = {
    {0x105AEF8, 0x105AE08, 0x6CD03E4274DBA196ull, 0x848D48F43C6221A0ull},
    {0x105AF48, 0x105AE58, 0xECA9283EEBA1A4B5ull, 0x1116C6455B39348Full},
    {0x105AF98, 0x105AEA8, 0x144D241C0DA8E776ull, 0xC922E9C2358D1A89ull},
    {0x105C9A8, 0x105C908, 0xC10B96F012ABEF43ull, 0x4C37BA1A32F3551Full},
    {0x105C9F8, 0x105C958, 0x1EF20DB51907E672ull, 0xB06FE58E0B81B648ull},
};

inline constexpr Entry kTable[] = {
    {"MotionBlurProxy::Render", kMotionBlurProxyRender, Kind::Function, "48 8B C4 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 90", 0, 0, "motion blur guard"},
    {"NoBlurProxy::Render", kNoBlurProxyRender, Kind::Function, "48 8B C4 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 90", 0, 0, "motion blur guard"},
    {"MotionBlurProxy.Pair1.Blur", 0x105AEF8, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x27B776, "motion blur guard"},
    {"MotionBlurProxy.Pair1.Sharp", 0x105AE08, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x27C416, "motion blur guard"},
    {"MotionBlurProxy.Pair2.Blur", 0x105AF48, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x27B2C5, "motion blur guard"},
    {"MotionBlurProxy.Pair2.Sharp", 0x105AE58, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x27BF65, "motion blur guard"},
    {"MotionBlurProxy.Pair3.Blur", 0x105AF98, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x27AEC6, "motion blur guard"},
    {"MotionBlurProxy.Pair3.Sharp", 0x105AEA8, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x27BB66, "motion blur guard"},
    {"MotionBlurProxy.Pair4.Blur", 0x105C9A8, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 07 45 8B C4 B8 01 00", 3, 0x285326, "motion blur guard"},
    {"MotionBlurProxy.Pair4.Sharp", 0x105C908, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 07 45 8B C4 B8 01 00", 3, 0x2850EE, "motion blur guard"},
    {"MotionBlurProxy.Pair5.Blur", 0x105C9F8, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 07 45 8B C4 B8 01 00", 3, 0x28543E, "motion blur guard"},
    {"MotionBlurProxy.Pair5.Sharp", 0x105C958, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 07 45 8B C4 B8 01 00", 3, 0x285206, "motion blur guard"},
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
    {"ABioWeaponRanged::TickFire", kRangedTickFire, Kind::Function,
        "48 89 5C 24 18 55 56 57 48 8B EC 48 81 EC 80 00 00 00 48 8B F1", 0, 0, "mod (Mako muzzle)"},
    {"USkeletalMeshComponent::UpdateBounds", kSkelUpdateBounds, Kind::Function,
        "40 55 53 48 8D AC 24 08 FF FF FF 48 81 EC F8 01", 0, 0, "mod (body bounds)"},
    {"UPrimitiveComponent::UpdateUmbraBounds", kPrimitiveUmbraBounds, Kind::Function,
        "40 53 48 81 EC C0 00 00 00 48 83 B9 10 02 00 00", 0, 0, "mod (body bounds)"},
    {"FName::FName(const TCHAR*, EFindName)", kNameFromText, Kind::Function, "40 53 48 83 EC 30 45 8B C8 C7 44 24 20 01 00 00", 0, 0,
        "mod (names)"},
    {"FISACTSoundSource::Update", kSoundSourceUpdate, Kind::Function,
        "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D A8 A8 FD FF FF 48", 0, 0, "mod (voices placed)"},
    {"ABioPawn::TryAcquireCover", kTryAcquireCover, Kind::Function, "48 8B C4 55 56 57 48 8D 68 A1 48 81 EC B0 00 00", 0, 0,
        "mod (cover)"},
    {"ABioPawn::CoverTrace", kCoverTrace, Kind::Function, "48 8B C4 48 89 58 08 48 89 70 10 55 57 41 56 48 8D A8 A8 FE", 0, 0,
        "mod (cover)"},
    {"UBioPlayerSelection::UpdateSelection", kUpdateSelection, Kind::Function, "40 55 53 57 41 54 41 55 41 57 48 8D AC 24 78 FF",
        0, 0, "mod (interaction)"},
    {"UBioConversation::SetPositionRotator", kConversationSetPositionRotator, Kind::Function,
        "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC A0 00 00 00 48 C7 45 C7 FE FF FF FF 48 89 58 08 48 89 70 10 48 89 78 18 0F",
        0, 0, "mod (conversations)"},
    {"ABioWorldInfo::InterruptConversation", kInterruptConversation, Kind::Function,
        "48 89 5C 24 08 57 48 83 EC 20 48 8B D9 48 8B FA 48 8B 89 1C", 0, 0, "mod (conversations)"},
    {"AActor::MoveWithInterpMoveTrack", kMoveWithInterpMoveTrack, Kind::Function, "48 8B C4 55 53 56 57 41 56 41 57 48 8D A8 58 FE", 0, 0,
        "mod (authored moments)"},
    {"UBioUIWorld::Tick", kUiWorldTick, Kind::Function,
        "40 53 48 83 EC 40 48 C7 44 24 20 FE FF FF FF 0F 29 74 24 30 0F 28 F1 8B 81 18 02 00 00 A8 01", 0, 0, "mod (ui world figure)"},
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
    {"scene capture's CaptureScene thunk", 0x742C80, Kind::Leaf, "48 8B 09 48 85 C9 74 07 48 8B 01 48 FF 60 08 C3", 0, 0,
        "render levers"},
    {"scene capture's Umbra camera thunk", 0x7526C0, Kind::Leaf, "4C 8B D2 48 8B D1 48 8B 09 48 85 C9 74 18 48 8B 52 08 4D 8B C8", 0,
        0, "ui world figure"},
    {"FSceneRenderTargets::BeginRenderingBackBuffer", 0x773D30, Kind::Function,
        "40 53 48 83 EC 50 48 C7 44 24 40 FE FF FF FF 48 8B D9 48 8B", 0, 0, "render levers"},
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
    // The show flags' bits per word (32), which RenderLevers' ShowFlag() reads to test a bit of FSceneRenderer +0x28 (a
    // literal in RenderLevers.cpp until 2026-11-16; the reference is the scene render's own test of show flag 0x33).
    {"show flags' bits per word", 0x164C720, Kind::Data, "41 B8 33 00 00 00 41 8B C0 44 8B 0D ?? ?? ?? ?? 41 F7 F1 8B", 12,
        0x76EF10, "render levers"},
};

} // le1

// LE2 (2.0.0.48602, timestamp 0x60AEEFA5): the hooks it runs today and our reflection layer's globals (M1,
// research/le2-engine-access-discovery.md "M1: what ME2's code showed"); patterns from tools/rva-patterns.py over ME2's
// .text dump (MassEffect-60AEEFA5.text.bin), each unique there; the tests check them against the dump when it's on the
// machine. LE3 uses the same original-code workflow against its 0x60AEF0C4 captured text.
// Unsupported game-specific features retain a zero address.
MEVR_ADDRESSES_LE2 {

inline constexpr std::uint32_t kProcessEvent = 0xD8D50;
inline constexpr std::uint32_t kCallFunction = 0xD6770;
inline constexpr std::uint32_t kGameEngineTick = 0x546980;
inline constexpr std::uint32_t kCalcSceneView = 0x6CEB70;
inline constexpr std::uint32_t kCalcSceneViewPrimaryReturn = 0;  // not pinned yet: any call from Draw counts
inline constexpr std::uint32_t kViewportDraw = 0x6D0A00;
inline constexpr std::uint32_t kViewportDrawEnd = 0x6D2C99;
// M3 (research/le2-vrik.md "The skeleton"): execForceSkelUpdate (0x77D750) calls it as (this, 0, 1), as LE1's does; it
// sizes SpaceBases (+0x2B8) and LocalAtoms (+0x2C8) to the RefSkeleton with 0x20-byte FBoneAtoms, as LE1's 0x592440.
inline constexpr std::uint32_t kUpdateSkelPose = 0x79B8A0;
// engine/BodyBounds' two functions in ME2 (LE1 0x58EEC0, 0x25CFE0; research/sequel-render-levers.md "Body bounds"), rows of
// the table below: ME1's culling pad on every game since lane AU4 (2026-11-18; the audits' REN-28, docs/trilogy-plan.md
// rule 1), the body's boxes grown by the host's view offset wherever the game's camera culls ahead of the drawn eye.
inline constexpr std::uint32_t kSkelUpdateBounds = 0x798350;
inline constexpr std::uint32_t kPrimitiveUmbraBounds = 0x3146C0;
inline constexpr std::uint32_t kSoundSourceUpdate = 0;
inline constexpr std::uint32_t kNameFromText = 0;
inline constexpr std::uint32_t kTryAcquireCover = 0;
inline constexpr std::uint32_t kCoverTrace = 0;
// BioPlayerSelection's target search, void(this), as ME3's (engine/SelectionGaze; research/40 section 6): called from the
// update's body (0xACE8F0, behind execUpdateSelection 0xB4AFB0) and from execFindCurrentSelectionTarget (0xB4AF70). It
// takes the cone's origin from the outer controller's PlayerCamera (+0x6A0) CameraCache.POV.Location (+0x464) and its axis
// from the controller's Actor.Rotation (+0x124).
inline constexpr std::uint32_t kUpdateSelection = 0xAC0530;
inline constexpr std::uint32_t kConversationSetPositionRotator = 0;
inline constexpr std::uint32_t kInterruptConversation = 0;
inline constexpr std::uint32_t kMoveWithInterpMoveTrack = 0;
// Our reflection layer's globals (ue3/Runtime, engine/GameAdapter): GObjects (TArray<UObject*>; Halcyon's ME2 code uses it
// too, engine_probe.cpp, pchud.cpp, and the host's queue for Halcyon's off-thread ProcessEvent calls checks objects against
// it, plugin/HalcyonLe2), the name pools, the engine's allocator (appMalloc, appFree and the GMalloc both load) and
// GNatives. Each is where ME2's code uses it: ~UObject (0x104F80), FName::AppendString (0x75820), appMalloc (0x9C900),
// appFree (0x9C8C0); LExSDKv2's LE2 build has the same four data RVAs.
inline constexpr std::uint32_t kGObjects = 0x173CC48;
inline constexpr std::uint32_t kNamePools = 0x1668A10;
inline constexpr std::uint32_t kGMalloc = 0x1682378;
inline constexpr std::uint32_t kAppMalloc = 0x9C900;
inline constexpr std::uint32_t kAppFree = 0x9C8C0;
inline constexpr std::uint32_t kGNatives = 0x16A99B0;

inline constexpr Entry kTable[] = {
    // The host's hooks.
    {"UObject::ProcessEvent", kProcessEvent, Kind::Function, "40 55 41 56 41 57 48 81 EC 90 00 00 00 48 8D 6C 24 20 48 C7", 0, 0,
        "host, mod"},
    {"UGameEngine::Tick", kGameEngineTick, Kind::Function, "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 A8 FC FF", 0, 0,
        "host"},
    // The mod's hooks.
    {"UObject::CallFunction", kCallFunction, Kind::Function, "40 55 53 56 57 41 54 41 55 41 56 41 57 48 81 EC A8 04 00 00", 0,
        0, "mod"},
    {"ULocalPlayer::CalcSceneView", kCalcSceneView, Kind::Function,
        "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 88 FD", 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw", kViewportDraw, Kind::Function,
        "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 58 FC FF FF 48 81 EC D0 04 00 00", 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw (end)", kViewportDrawEnd, Kind::End, nullptr, 0, 0, "mod (scene view)"},
    {"USkeletalMeshComponent::UpdateSkelPose", kUpdateSkelPose, Kind::Function,
        "48 8B C4 44 89 40 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 48 81", 0, 0, "mod (arm IK)"},
    {"UBioPlayerSelection::FindCurrentSelectionTarget", kUpdateSelection, Kind::Function,
        "48 8B C4 48 89 48 08 55 53 57 41 54 41 55 41 56", 0, 0, "mod (interaction gaze)"},
    // Our reflection layer's globals and the engine's allocator (ue3/Runtime has its own patterns too).
    {"GObjects", kGObjects, Kind::Data, "C9 74 5B EB F0 4C 8B 05 ?? ?? ?? ?? 4B 8B 04 06", 8, 0x9B24B,
        "mod (reflection), host (Halcyon LE2's game-thread queue)"},
    {"the name pools", kNamePools, Kind::Data, "74 2B 48 8B D8 48 8D 15 ?? ?? ?? ?? 48 C1 F8 1D", 8, 0x7450B, "mod (reflection)"},
    {"GMalloc", kGMalloc, Kind::Data, "48 85 DB 74 2A 48 8B 0D ?? ?? ?? ?? 48 85 C9 75", 8, 0x96B03, "mod (engine strings)"},
    {"appMalloc", kAppMalloc, Kind::Function,
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B F1 41 8B D8 48 8B 0D ?? ?? ?? ?? 8B FA 48 85 C9 75 0C "
        "E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B CB 44 8B C7 8B D6",
        0, 0, "mod (engine strings)"},
    {"appFree", kAppFree, Kind::Function,
        "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 8B DA 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 0C E8 ?? ?? ?? ?? "
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B C3 48 8B D7 48 8B 5C 24 30 48 83 C4 20 5F 48 FF 60 18",
        0, 0, "mod (engine strings)"},
    {"GNatives", kGNatives, Kind::Data, "74 13 0F B7 C0 48 8D 0D ?? ?? ?? ?? 48 8B 04 C1", 8, 0xAEF81, "mod (diagnostics)"},
    // plugin/RenderLevers' addresses in ME2 (research/sequel-render-levers.md "ME2": each found by matching LE1's function
    // instruction for instruction, most identical but for the view stride, and by its place in the same callers). The row
    // names are LE1's, exactly: plugin/RenderLevers finds each address by its row name, and a missing row stands that
    // lever down on ME2.
    {"FSceneRenderer::Render", 0x34BF70, Kind::Function,
        "48 8B C4 55 41 56 41 57 48 8D 68 A1 48 81 EC C0 00 00 00 48 C7 45 17 FE FF FF FF 48 89 58 10 48", 0, 0, "render levers"},
    {"UmbraCommander::command", 0x3FF410, Kind::Function, "83 FA 09 0F 87 ?? ?? ?? ?? 55 56 48 8D 6C 24 B8", 0, 0,
        "render levers"},
    {"FD3D11DynamicRHI::GetQueryData", 0x986790, Kind::Function,
        "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 60", 0, 0, "render levers"},
    {"SetUmbraCommander / wireframe state", 0x3FB390, Kind::Leaf, "0F B6 C1 48 89 05 ?? ?? ?? ?? C3 CC CC CC CC CC", 0, 0,
        "render levers"},
    {"FSceneRenderer deleting destructor", 0x342480, Kind::Function,
        "4C 8B DC 57 41 56 41 57 48 83 EC 50 49 C7 43 B8 FE FF FF FF 49 89 5B 08 49 89 6B 10 49 89 73 18 8B EA 4C 8B", 0, 0,
        "render levers"},
    {"FSceneRenderTargets::Allocate", 0x353AB0, Kind::Function,
        "48 8B C4 57 41 56 41 57 48 83 EC 50 48 C7 40 D8 FE FF FF FF 48 89 58 10", 0, 0, "render levers"},
    {"FSceneRenderer::RenderDpgLightTargets", 0x372F50, Kind::Function, "4C 8B DC 53 55 56 57 48 81 EC E8 ?? ?? ?? ?? 8B", 0,
        0, "render levers"},
    {"RenderUmbraBasePass", 0x3F80C0, Kind::Function,
        "48 8B C4 56 57 41 56 48 83 EC 60 48 C7 40 C8 FE FF FF FF 48 89 58 10 48 89 68 18 48 8B 05 ?? ??", 0, 0,
        "render levers"},
    {"translucency check", 0x343E80, Kind::Function, "48 89 5C 24 08 4C 63 59 68 BB 01 00 00 00 4D 85", 0, 0, "render levers"},
    {"before-lights pass", 0x336B20, Kind::Function,
        "48 8B C4 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 28 FF", 0, 0, "render levers"},
    {"DPG pass", 0x34A660, Kind::Function,
        "48 89 5C 24 20 55 56 57 48 83 EC 40 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 30 48", 0, 0, "render levers"},
    {"DPG lights", 0x34A0B0, Kind::Function, "4C 8B DC 55 56 57 41 56 49 8D 6B A9 48 81 EC A8", 0, 0, "render levers"},
    {"FD3D11BoundShaderState constructor", 0x98ECD0, Kind::Function, "4C 8B DC 4D 89 4B 20 49 89 53 10 49 89 4B 08 55", 0, 0,
        "render levers"},
    {"UmbraCommander visible instance", 0x3F3ED0, Kind::Function, "40 53 55 56 57 41 54 41 56 41 57 48 81 EC A0 00", 0, 0,
        "render levers"},
    {"foreground pass", 0x349A20, Kind::Function,
        "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E9 ?? ?? ?? ?? 00 00 00 48 C7 45 AF FE FF FF", 0, 0,
        "render levers"},
    {"scene render finish", 0x34AE50, Kind::Function, "40 56 57 48 83 EC 48 33 F6 48 89 5C 24 60 48 89", 0, 0, "render levers"},
    {"scene capture's CaptureScene thunk", 0x329B90, Kind::Leaf, "48 8B 09 48 85 C9 74 07 48 8B 01 48 FF 60 08 C3", 0, 0,
        "render levers"},
    {"scene capture's Umbra camera thunk", 0x333240, Kind::Leaf, "4C 8B D2 48 8B D1 48 8B 09 48 85 C9 74 18 48 8B 52 08 4D 8B C8", 0,
        0, "ui world figure"},
    {"FSceneRenderTargets::BeginRenderingBackBuffer", 0x354570, Kind::Function,
        "40 53 48 83 EC 30 48 C7 44 24 20 FE FF FF FF 48 8B D9 48 8B 0D ?? ?? ?? ?? 48 8B 01", 0, 0, "render levers"},
    {"UmbraCommander::IssueOcclusionQueries", 0x3DBB80, Kind::Function,
        "40 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 C7 44 24 50 FE FF FF FF 48 89 9C 24 B8 00 00", 0, 0,
        "render levers"},
    {"Umbra read of one query result", 0xD55BC0, Kind::Leaf, "89 91 CC 01 00 00 BA 06 00 00 00 44 89 81 C8 01", 0, 0,
        "render levers"},
    {"Umbra resolve start stamp", 0x175D2A0, Kind::Data, "66 0F 5A C0 F3 0F 11 05 ?? ?? ?? ?? E9 ?? ?? ?? ?? BA 01 00", 8,
        0x3FF490, "render levers"},
    {"GSceneRenderTargets", 0x161DDB0, Kind::Data, "44 8B C3 8B D0 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B CF", 8, 0x34C205,
        "render levers"},
    {"GDynamicRHI", 0x174C648, Kind::Data, "87 3E 01 00 00 48 8B 0D ?? ?? ?? ?? 48 8B 01 48", 8, 0x1BAD14, "render levers"},
    {"scene frame counter", 0x175301C, Kind::Data, "C4 20 5B C3 CC 48 8D 05 ?? ?? ?? ?? C3 CC CC CC", 8, 0x344B2B,
        "render levers"},
    {"depth dirty flags", 0x175D298, Kind::Data, "48 83 EA 01 75 DA 89 1D ?? ?? ?? ?? 41 89 59 60", 8, 0x3F8576,
        "render levers"},
    {"GSkipBeforeLights", 0x161E180, Kind::Data, "C9 40 0F 94 C7 44 39 35 ?? ?? ?? ?? 74 2B 4C 39", 8, 0x6F548C,
        "render levers"},
    {"Umbra commanders", 0x175D348, Kind::Data, "39 73 68 7E 46 48 8D 35 ?? ?? ?? ?? 0F 1F 84 00", 8, 0x3F81FC,
        "render levers"},
    {"Umbra commander index", 0x175D288, Kind::Data, "00 48 03 53 60 48 8B 05 ?? ?? ?? ?? 48 8B 0C C6", 8, 0x3F8219,
        "render levers"},
    // The show flags' bits per word (32), which RenderLevers' ShowFlag() reads to test a bit of FSceneRenderer +0x28: LE1's
    // is the literal 0x164C720 in RenderLevers.cpp and has no row of its own.
    {"show flags' bits per word", 0x1618A30, Kind::Data, "41 B8 33 00 00 00 41 8B C0 44 8B 0D ?? ?? ?? ?? 41 F7 F1 8B", 12,
        0x34C060, "render levers"},
    // engine/BodyBounds' two functions in ME2 (kSkelUpdateBounds and kPrimitiveUmbraBounds above).
    {"USkeletalMeshComponent::UpdateBounds", kSkelUpdateBounds, Kind::Function, "40 55 53 48 8D AC 24 08 FF FF FF 48 81 EC F8 01", 0,
        0, "mod (body bounds)"},
    {"UPrimitiveComponent::UpdateUmbraBounds", kPrimitiveUmbraBounds, Kind::Function, "40 53 48 81 EC C0 00 00 00 48 8B 01 48 8B D9 FF", 0,
        0, "mod (body bounds)"},
};

} // le2

MEVR_ADDRESSES_LE3 {

// Capture loop and post-effect target contract, verified in the 60AEF0C4 dump.
inline constexpr std::uint32_t kCaptureScene = 0x344540;
inline constexpr std::uint32_t kCaptureRender = 0x365370;
inline constexpr std::uint32_t kCaptureRenderUmbra = 0x3685C0;
inline constexpr std::uint32_t kBeginBackbuffer = 0x36D760;
inline constexpr std::uint32_t kDynamicRhi = 0x1894018;

// ME1's render levers on ME3 (lane R3, research/sequel-render-levers.md "ME3": each row's evidence, the offsets its hook
// reads and a recipe). Named as plugin/RenderLevers names its LE1 constants (k...Rva there). Ghidra over the 60AEF0C4 dump:
// each found by the call structure it has in LE1 (the scene render's body, InitViews' tail, the commander's command switch)
// and, where the code is unchanged, by masked bytes equal to LE1's. Nothing uses them yet: lane PL's address table does.
// The scene render (LE1 0x76EE20): the capture loop, then the eye's render through kCaptureRenderUmbra (LE1 inlines that
// branch), then the renderer's deletion. void(renderer).
inline constexpr std::uint32_t kSceneRender = 0x368850;
// FSceneRenderer's deleting destructor (LE1 0x4ED700; ME3's has the destructor inlined). void*(renderer, flags); 0x110 bytes.
inline constexpr std::uint32_t kDeleteRenderer = 0x35E6E0;
// GSceneRenderTargets.Allocate(this, sizeX, sizeY, 0) (LE1 0x772EB0) and the object (LE1 0x167D0E0).
inline constexpr std::uint32_t kAllocateTargets = 0x36CDC0;
inline constexpr std::uint32_t kSceneRenderTargets = 0x1764A20;
// (byte which): the Umbra commander index for the render (LE1 0x8C1AE0). A leaf; it writes kCommanderIndex.
inline constexpr std::uint32_t kSetCommander = 0x3E84E0;
// InitViews' three calls on the Umbra path, in order: the resolve (LE1 0x8BFBB0), the DPG's light targets (renderer, dpg)
// (LE1 0x77BE00: where the replay's tail starts) and the opaque base pass over the views (LE1 0x8BF8F0).
inline constexpr std::uint32_t kUmbraResolve = 0x3E5890;
inline constexpr std::uint32_t kDpgLightTargets = 0x38A450;
inline constexpr std::uint32_t kPrepassViews = 0x3E55C0;
// The Umbra branch's passes after InitViews, as kCaptureRenderUmbra calls them (LE1's, in the same order): the translucency
// check (renderer) 0x7666A0, the before-lights pass (renderer) 0x759240 only while kSkipBeforeLights is 0, the DPG pass
// (renderer, dpg, uint* dirty) 0x76D650, the DPG lights (renderer, dpg, translucency, uint* dirty, flag) 0x76D0A0, the
// foreground pass (renderer, 2, int* out, uint* dirty, flag) 0x76CA70, the finish (renderer, translucency, dirty) 0x76DD10.
inline constexpr std::uint32_t kTranslucencyCheck = 0x3600A0;
inline constexpr std::uint32_t kBeforeLights = 0x351020;
inline constexpr std::uint32_t kDpgPass = 0x366F20;
inline constexpr std::uint32_t kDpgLights = 0x366960;
inline constexpr std::uint32_t kForeground = 0x366280;
inline constexpr std::uint32_t kFinish = 0x3675D0;
// The globals that branch reads (LE1 0x178779C, 0x1795D38, 0x167D49C, 0x164C720).
inline constexpr std::uint32_t kSceneFrame = 0x1898678;
inline constexpr std::uint32_t kDepthDirty = 0x189EC04;
inline constexpr std::uint32_t kSkipBeforeLights = 0x1764E18;
inline constexpr std::uint32_t kShowFlagBitsPerWord = 0x1756A20;
// BioWare's Umbra commander: its two instances and the index in use (LE1 0x1798E88, 0x1795D28); command(commander, code),
// slot 1 of the commander's first vtable 0x1157F68 (LE1 0x8C34C0), and the float it stamps at code 0 (LE1 0x1795D40);
// code 5's IssueOcclusionQueries(commander) (LE1 0x8B6550); the function every visible instance goes through, int(commander,
// record, float distanceSq, a4, a5, a6, a7) (LE1 0x8BE0B0: ME3 also calls it with a7 = 0 from 0x3E3650); and Umbra's read
// of one query result (context, head, count, wait), a leaf (LE1 0xD48380: byte-identical library code).
inline constexpr std::uint32_t kCommanders = 0x189ECC8;
inline constexpr std::uint32_t kCommanderIndex = 0x189EBF8;
inline constexpr std::uint32_t kCommanderCommand = 0x3E8B80;
inline constexpr std::uint32_t kUmbraResolveStart = 0x189EC0C;
inline constexpr std::uint32_t kUmbraIssueQueries = 0x3D8EB0;
inline constexpr std::uint32_t kUmbraInstanceVisible = 0x3E3B00;
inline constexpr std::uint32_t kUmbraReadResult = 0xE02430;
// A dword command() tests first (`cmp dword ptr [rip+X], 0` at 0x3E8B98, its only reference) and does nothing for any code
// while it is 0; LE1 and ME2 have no such gate (research/sequel-render-levers.md "ME3", difference 4). Read only, for the
// log (plugin/RenderLeversTable's umbraCommandGate).
inline constexpr std::uint32_t kUmbraCommandGate = 0x1767208;
// FD3D11DynamicRHI::GetQueryData(this, query, out, size, bWait) (LE1 0x9A04B0) and the bound shader state's constructor
// (LE1 0x9A89F0): both byte-identical to LE1's. The RHI's vtable is 0x12FC7A0 (LE1 0x120FF00), the same slots.
inline constexpr std::uint32_t kGetQueryData = 0x9A7D10;
inline constexpr std::uint32_t kBoundShaderStateCtor = 0x9B0270;
// The capture's Umbra camera thunk (LE1 0x7526C0; a leaf, called once from 0x35F090).
inline constexpr std::uint32_t kCaptureUmbraCamera = 0x34D750;
// USkeletalMeshComponent::UpdateBounds and the primitive's hand-over of its Bounds to Umbra (LE1 0x58EEC0, 0x25CFE0): ME1's
// culling pad on every game since lane AU4 (2026-11-18; the audits' REN-28, docs/trilogy-plan.md rule 1), kSkelUpdateBounds
// and kPrimitiveUmbraBounds below.
inline constexpr std::uint32_t kSkelUpdateBoundsNative = 0x793850;
inline constexpr std::uint32_t kPrimitiveUmbraBoundsNative = 0x32EC10;

// Native tier evaluation, void(this, int force), before streaming queues are
// classified. Ghidra from the installed 60AEF0C4 dump; research/le3-galaxy.md.
inline constexpr std::uint32_t kReevaluateStreaming = 0xAC67C0;

// FLensFlareDynamicData's reflection pass, void(data, scene proxy, draw interface, view, DPG): the source projected through
// the view's ViewProjectionMatrix (+0x260, FSceneView::WorldToScreen 0x34EF00), each element placed on the screen line
// through the centre and turned back into a world point through InvTranslatedViewProjection (+0x210) + ViewOrigin (+0x320)
// at clip depth 0.1 (plugin/FlareWindow; research/30 section 9.6). ME2's own (0x252AB0) unprojects through the world-space
// InvViewProjection (+0x2E0), which pairs with +0x260 in every mode, so it needs no row; ME1 has no UE3 lens flare (its sun
// is BioSunFlareComponent's world-space sprite, 0xB1E5D0).
inline constexpr std::uint32_t kLensFlareReflections = 0x26B780;

inline constexpr std::uint32_t kProcessEvent = 0xF4F80;
inline constexpr std::uint32_t kCallFunction = 0xF2C30;
inline constexpr std::uint32_t kGameEngineTick = 0x5502C0;
inline constexpr std::uint32_t kCalcSceneView = 0x6CFFD0;
inline constexpr std::uint32_t kCalcSceneViewPrimaryReturn = 0;  // not pinned yet: any call from Draw counts
inline constexpr std::uint32_t kViewportDraw = 0x6D1E30;
inline constexpr std::uint32_t kViewportDrawEnd = 0x6D40DB;
inline constexpr std::uint32_t kUpdateSkelPose = 0x796EB0;
inline constexpr std::uint32_t kSkelUpdateBounds = kSkelUpdateBoundsNative;
inline constexpr std::uint32_t kPrimitiveUmbraBounds = kPrimitiveUmbraBoundsNative;
inline constexpr std::uint32_t kSoundSourceUpdate = 0;
inline constexpr std::uint32_t kNameFromText = 0;
inline constexpr std::uint32_t kTryAcquireCover = 0;
inline constexpr std::uint32_t kCoverTrace = 0;
// FindCurrentSelectionTarget body, void(this); research/le3-interaction-gaze.md.
inline constexpr std::uint32_t kUpdateSelection = 0xA9BF90;
inline constexpr std::uint32_t kConversationSetPositionRotator = 0;
inline constexpr std::uint32_t kInterruptConversation = 0;
inline constexpr std::uint32_t kMoveWithInterpMoveTrack = 0;
inline constexpr std::uint32_t kGObjects = 0x1887E40;  // Original captured native code; research/le3-engine-access-discovery.md.
inline constexpr std::uint32_t kNamePools = 0x17B33D0;
inline constexpr std::uint32_t kGMalloc = 0x17CD588;
inline constexpr std::uint32_t kAppMalloc = 0xBF110;
inline constexpr std::uint32_t kAppFree = 0xBF0D0;
inline constexpr std::uint32_t kGNatives = 0x17F4BB0;

// ME3 counterparts of LE1's combined post-process guard. Native constructor,
// factory and full dispatch-table audit: research/le3-recenter-transition-failure.md.
inline constexpr std::uint32_t kMotionBlurProxyRender = 0x3E3F20;
inline constexpr std::uint32_t kNoBlurProxyRender = 0x3E4250;
inline constexpr le1::MotionBlurProxyPair kMotionBlurProxyPairs[] = {
    {0x1154B20, 0x1154990, 0x672AC625B88E709Cull, 0x34208617999F0373ull},
    {0x1154B70, 0x11549E0, 0xEB2AEFDB51EC1DB1ull, 0x1924AFE40335BB63ull},
    {0x1154BC0, 0x1154A30, 0x3402F4E6469ECCF5ull, 0x15454443A34FC654ull},
    {0x1154C10, 0x1154A80, 0x55281E47FA78BD1Aull, 0x3B367E8E9327D8E3ull},
    {0x1154C60, 0x1154AD0, 0x328E9C2384836DAAull, 0xC7B82F3E6CDC2313ull},
    {0x1157E78, 0x1157DD8, 0x1AE92F8BB1E9F57Bull, 0x1786DF4B7BB170D0ull},
    {0x1157EC8, 0x1157E28, 0x682EAA364BF8D6F0ull, 0x2A143EDF5F7AC152ull},
};

inline constexpr Entry kTable[] = {
    {"MotionBlurProxy::Render", kMotionBlurProxyRender, Kind::Function, "48 8B C4 56 57 41 54 41 56 41 57 48 81 EC 80 00 00 00 48 C7 40 A0 FE FF FF FF", 0, 0, "motion blur guard"},
    {"NoBlurProxy::Render", kNoBlurProxyRender, Kind::Function, "48 8B C4 56 57 41 54 41 56 41 57 48 81 EC 80 00 00 00 48 C7 40 A0 FE FF FF FF", 0, 0, "motion blur guard"},
    {"MotionBlurProxy.Pair1.Blur", 0x1154B20, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D5314, "motion blur guard"},
    {"MotionBlurProxy.Pair1.Sharp", 0x1154990, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D6734, "motion blur guard"},
    {"MotionBlurProxy.Pair2.Blur", 0x1154B70, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D4F34, "motion blur guard"},
    {"MotionBlurProxy.Pair2.Sharp", 0x11549E0, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D6354, "motion blur guard"},
    {"MotionBlurProxy.Pair3.Blur", 0x1154BC0, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D4AA1, "motion blur guard"},
    {"MotionBlurProxy.Pair3.Sharp", 0x1154A30, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D5EC1, "motion blur guard"},
    {"MotionBlurProxy.Pair4.Blur", 0x1154C10, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D46B4, "motion blur guard"},
    {"MotionBlurProxy.Pair4.Sharp", 0x1154A80, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D5AD4, "motion blur guard"},
    {"MotionBlurProxy.Pair5.Blur", 0x1154C60, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D42D4, "motion blur guard"},
    {"MotionBlurProxy.Pair5.Sharp", 0x1154AD0, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3", 3, 0x3D56F4, "motion blur guard"},
    {"MotionBlurProxy.Pair6.Blur", 0x1157E78, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 06 45 8B C4 B8 01 00", 3, 0x3DA4DB, "motion blur guard"},
    {"MotionBlurProxy.Pair6.Sharp", 0x1157DD8, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 06 45 8B C4 B8 01 00", 3, 0x3DA273, "motion blur guard"},
    {"MotionBlurProxy.Pair7.Blur", 0x1157EC8, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 06 45 8B C4 B8 01 00", 3, 0x3DA60B, "motion blur guard"},
    {"MotionBlurProxy.Pair7.Sharp", 0x1157E28, Kind::Data, "48 8D 05 ?? ?? ?? ?? 48 89 06 45 8B C4 B8 01 00", 3, 0x3DA3A3, "motion blur guard"},
    // The host's hooks.
    {"UObject::ProcessEvent", kProcessEvent, Kind::Function, "40 55 41 56 41 57 48 81 EC 90 00 00 00 48 8D 6C 24 20 48 C7", 0, 0,
        "host, mod"},
    {"UGameEngine::Tick", kGameEngineTick, Kind::Function, "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 A8 FC FF", 0, 0,
        "host"},
    // The mod's hooks.
    {"UObject::CallFunction", kCallFunction, Kind::Function, "40 55 53 56 57 41 54 41 55 41 56 41 57 48 81 EC A8 04 00 00", 0,
        0, "mod"},
    {"ULocalPlayer::CalcSceneView", kCalcSceneView, Kind::Function,
        "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 98 FD FF FF 48 81 EC 88 03 00", 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw", kViewportDraw, Kind::Function,
        "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 58 FC FF FF 48 81 EC D0 04 00 00", 0, 0, "mod (scene view)"},
    {"FViewportClient::Draw (end)", kViewportDrawEnd, Kind::End, nullptr, 0, 0, "mod (scene view)"},
    {"USkeletalMeshComponent::UpdateSkelPose", kUpdateSkelPose, Kind::Function,
        "48 8B C4 44 89 40 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 48 81", 0, 0, "mod (arm IK)"},
    // Our reflection layer's globals and the engine's allocator (ue3/Runtime has its own patterns too).
    {"GObjects", kGObjects, Kind::Data, "C9 74 5B EB F0 4C 8B 05 ?? ?? ?? ?? 4B 8B 04 06", 8, 0xBC8EB,
        "mod (reflection), host (Halcyon LE3's game-thread queue)"},
    {"UBioPlayerSelection::FindCurrentSelectionTarget", kUpdateSelection, Kind::Function,
        "48 8B C4 48 89 48 08 55 56 57 41 54 41 55 41 57", 0, 0, "mod (interaction gaze)"},
    {"UBioTierManager::ReevaluateStreaming", kReevaluateStreaming, Kind::Function,
        "41 56 48 83 EC 40 4C 8B F1 85 D2 75 0D ?? ?? ??", 0, 0, "mod (CIC art retention)"},
    {"FLensFlareDynamicData::RenderReflections", kLensFlareReflections, Kind::Function,
        "48 8B C4 4C 89 40 18 55 53 56 41 56 41 57 48 8D", 0, 0, "host (flare window)"},
    // Three of the capture rows carry LE1's row names: plugin/RenderLevers finds a row by name, plugin/Le3SceneCapture by RVA.
    {"scene capture's CaptureScene thunk", kCaptureScene, Kind::Leaf, "48 8B 09 48 85 C9 74 07 48 8B 01 48 FF 60 08 C3", 0, 0, "host (capture target)"},
    {"Capture.Render", kCaptureRender, Kind::Function, "48 8B C4 56 57 41 56 48 83 EC 60 48 C7 40 C0 FE", 0, 0, "host (capture target)"},
    {"Capture.RenderUmbra", kCaptureRenderUmbra, Kind::Function, "48 89 74 24 18 48 89 7C 24 20 55 48 8B EC 48 83 EC 50 48 8B", 0, 0, "host (capture target)"},
    {"FSceneRenderTargets::BeginRenderingBackBuffer", kBeginBackbuffer, Kind::Function, "40 53 48 83 EC 30 48 C7 44 24 20 FE FF FF FF 48 8B D9 48 8B 0D ?? ?? ?? ?? 48 8B 01", 0, 0, "host (capture target)"},
    {"GDynamicRHI", kDynamicRhi, Kind::Data, "40 53 48 83 EC 30 48 C7 44 24 20 FE FF FF FF 48 8B D9 48 8B 0D ?? ?? ?? ?? 48 8B 01", 21, 0x36D760, "host (capture target)"},
    {"the name pools", kNamePools, Kind::Data, "38 49 63 46 0C 48 8D 2D ?? ?? ?? ?? 33 FF 85 C0", 8, 0x81A11, "mod (reflection)"},
    {"GMalloc", kGMalloc, Kind::Data, "B9 98 00 00 00 48 8B 0D ?? ?? ?? ?? 48 85 C9 75", 8, 0xB7683, "mod (engine strings)"},
    {"appMalloc", kAppMalloc, Kind::Function,
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B F1 41 8B D8 48 8B 0D ?? ?? ?? ?? 8B FA 48 85 C9 75 0C "
        "E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B CB 44 8B C7 8B D6",
        0, 0, "mod (engine strings)"},
    {"appFree", kAppFree, Kind::Function,
        "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 8B DA 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 0C E8 ?? ?? ?? ?? "
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 44 8B C3 48 8B D7 48 8B 5C 24 30 48 83 C4 20 5F 48 FF 60 18",
        0, 0, "mod (engine strings)"},
    {"GNatives", kGNatives, Kind::Data, "74 13 0F B7 C0 48 8D 0D ?? ?? ?? ?? 48 8B 04 C1", 8, 0xCA8A1, "mod (diagnostics)"},
    // ME1's render levers' natives on ME3 (research/sequel-render-levers.md "ME3"); the names are LE1's rows'.
    {"FSceneRenderer::Render", kSceneRender, Kind::Function,
        "48 8B C4 55 41 56 41 57 48 8D 68 A1 48 81 EC A0 00 00 00 48 C7 45 17 FE", 0, 0, "render levers"},
    {"FSceneRenderer deleting destructor", kDeleteRenderer, Kind::Function,
        "4C 8B DC 57 41 56 41 57 48 83 EC 50 49 C7 43 B8 FE FF FF FF 49 89 5B 08 49 89 6B 10 49 89 73 18 8B EA 4C 8B", 0, 0,
        "render levers"},
    {"FSceneRenderTargets::Allocate", kAllocateTargets, Kind::Function,
        "48 8B C4 57 41 56 41 57 48 83 EC 50 48 C7 40 D8 FE FF FF FF 48 89 58 10 48 89 68 18 48 89 70 20 48 8B D9 BD", 0, 0,
        "render levers"},
    {"SetUmbraCommander / wireframe state", kSetCommander, Kind::Leaf, "0F B6 C1 48 89 05 ?? ?? ?? ?? C3 CC CC CC CC CC", 0, 0,
        "render levers"},
    {"FSceneRenderer::UmbraResolve", kUmbraResolve, Kind::Function,
        "48 8B C4 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 C7 40 B0 FE FF FF FF 48 89 58 10", 0, 0, "render levers"},
    {"FSceneRenderer::RenderDpgLightTargets", kDpgLightTargets, Kind::Function, "4C 8B DC 55 53 56 57 41 56 49 8D 6B A1 48 81 EC", 0,
        0, "render levers"},
    {"RenderUmbraBasePass", kPrepassViews, Kind::Function, "48 8B C4 55 41 56 41 57 48 8B EC 48 83 EC 60 48 C7 45 E0 FE", 0, 0,
        "render levers"},
    {"translucency check", kTranslucencyCheck, Kind::Function, "48 89 5C 24 08 4C 63 59 68 BB 01 00 00 00 4D 85", 0, 0,
        "render levers"},
    {"before-lights pass", kBeforeLights, Kind::Function,
        "48 8B C4 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 28 FF", 0, 0, "render levers"},
    {"DPG pass", kDpgPass, Kind::Function,
        "48 89 5C 24 20 55 56 57 48 83 EC 40 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 30 48", 0, 0, "render levers"},
    {"DPG lights", kDpgLights, Kind::Function, "4C 8B DC 55 56 57 41 56 49 8D 6B A9 48 81 EC A8", 0, 0, "render levers"},
    {"foreground pass", kForeground, Kind::Function,
        "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E9 ?? ?? ?? ?? 00 00 00 48 C7 45 AF FE FF FF", 0, 0,
        "render levers"},
    {"scene render finish", kFinish, Kind::Function, "40 56 57 48 83 EC 48 33 F6 48 89 5C 24 60 48 89", 0, 0, "render levers"},
    {"UmbraCommander::command", kCommanderCommand, Kind::Function, "40 55 48 81 EC 90 00 00 00 48 8B 05 ?? ?? ?? ??", 0, 0,
        "render levers"},
    {"UmbraCommander::IssueOcclusionQueries", kUmbraIssueQueries, Kind::Function,
        "40 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 C7 44 24 50 FE FF FF FF 48 89 9C 24 B8 00 00", 0, 0,
        "render levers"},
    {"UmbraCommander visible instance", kUmbraInstanceVisible, Kind::Function,
        "40 53 55 56 57 41 54 41 56 41 57 48 81 EC A0 00 00 00 48 C7 44 24 48 FE", 0, 0, "render levers"},
    {"Umbra read of one query result", kUmbraReadResult, Kind::Leaf, "89 91 CC 01 00 00 BA 06 00 00 00 44 89 81 C8 01", 0, 0,
        "render levers"},
    // Its only reference has an immediate after the displacement (tail 1): tools/rva-patterns.py ...=0x1767208:data+1.
    {"Umbra command gate", kUmbraCommandGate, Kind::Data, "C4 48 89 44 24 70 83 3D ?? ?? ?? ?? 00 48 8B E9", 8, 0x3E8B92,
        "render levers", 1},
    {"FD3D11DynamicRHI::GetQueryData", kGetQueryData, Kind::Function,
        "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 60 48 8B F9 0F", 0, 0, "render levers"},
    {"FD3D11BoundShaderState constructor", kBoundShaderStateCtor, Kind::Function, "4C 8B DC 4D 89 4B 20 49 89 53 10 49 89 4B 08 55",
        0, 0, "render levers"},
    {"scene capture's Umbra camera thunk", kCaptureUmbraCamera, Kind::Leaf, "4C 8B D2 48 8B D1 48 8B 09 48 85 C9 74 18 48 8B", 0, 0,
        "render levers"},
    {"USkeletalMeshComponent::UpdateBounds", kSkelUpdateBoundsNative, Kind::Function,
        "40 55 53 48 8D AC 24 08 FF FF FF 48 81 EC F8 01", 0, 0, "mod (body bounds)"},
    {"UPrimitiveComponent::UpdateUmbraBounds", kPrimitiveUmbraBoundsNative, Kind::Function,
        "40 53 48 81 EC C0 00 00 00 48 83 B9 14 02 00 00", 0, 0, "mod (body bounds)"},
    {"GSceneRenderTargets", kSceneRenderTargets, Kind::Data, "33 DB 39 59 38 48 8D 0D ?? ?? ?? ?? 0F 95 C3 8D", 8, 0x163CA2,
        "render levers"},
    {"scene frame counter", kSceneFrame, Kind::Data, "C4 20 5B C3 CC 48 8D 05 ?? ?? ?? ?? C3 CC CC CC", 8, 0x36111B,
        "render levers"},
    {"depth dirty flags", kDepthDirty, Kind::Data, "39 35 ?? ?? ?? ?? 8B D8 8B 0D ?? ?? ?? ?? 89 4D E0 75 08 48 8B CF", 10,
        0x36875B, "render levers"},
    {"GSkipBeforeLights", kSkipBeforeLights, Kind::Data, "89 75 E4 E8 ?? ?? ?? ?? 39 35 ?? ?? ?? ?? 8B D8 8B 0D", 10, 0x368753,
        "render levers"},
    {"show flags' bits per word", kShowFlagBitsPerWord, Kind::Data, "48 8B 06 74 63 0F AF 0D ?? ?? ?? ?? 44 2B C9 48", 8, 0x170369,
        "render levers"},
    {"Umbra commanders", kCommanders, Kind::Data, "39 7B 68 7E 3E 48 8D 35 ?? ?? ?? ?? 48 63 C7 48", 8, 0x3E5705,
        "render levers"},
    {"Umbra commander index", kCommanderIndex, Kind::Data, "00 48 03 53 60 48 8B 05 ?? ?? ?? ?? 48 8B 0C C6", 8, 0x3E571A,
        "render levers"},
    {"Umbra resolve start stamp", kUmbraResolveStart, Kind::Data, "66 0F 5A C0 F3 0F 11 05 ?? ?? ?? ?? E9 ?? ?? ?? ?? BA 01 00", 8,
        0x3E8C09, "render levers"},
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
