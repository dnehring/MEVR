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
// Where the right grip holds the gun (game/Holsters, ME1's rule): on foot and in the two wheels the game opens from there
// (paused in them: a held grip must not turn into RB, nor the click that opened the power wheel). ME1's modes 0, 2, 3.
inline constexpr bool IsFootOrWheelMode(int mode) {
    Mode const context = GameMode(mode);
    return context == Mode::OnFoot || context == Mode::PowerWheel || context == Mode::WeaponWheel;
}
inline constexpr bool IsGalaxyMode(int mode) { return GameMode(mode) == Mode::Galaxy; }
inline constexpr bool IsOrbitalMode(int mode) { return GameMode(mode) == Mode::Orbital; }
inline constexpr bool IsMapMode(int mode) { return IsGalaxyMode(mode) || IsOrbitalMode(mode); }
// Gameplay whose HUD goes on the body-locked HUD panel (ME1's rule, plugin/HudPanel): on foot, the vehicle, the power and
// weapon wheels, the squad command, and ME3's Atlas and injured Shepard; not conversations, cinematics, GUI screens, movies,
// the maps or a minigame. ME1's and ME2's numbers 0..4 exactly.
inline constexpr bool IsHudGameplayMode(int mode) {
    switch (GameMode(mode)) {
    case Mode::OnFoot: case Mode::Vehicle: case Mode::Atlas: case Mode::PowerWheel: case Mode::WeaponWheel:
    case Mode::Command: case Mode::Injured:
        return true;
    default:
        return false;
    }
}
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
// showed"). LE3's are read from its captured native code (research/le3-engine-access-discovery.md).
// Engine access does not enable the separately gated gameplay modules.
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
#elif defined(MEVR_GAME_LE3)
    ue3::Layout l;
    // ~UObject 0x121250; IsPendingKill 0x879D0; GetPathName 0x127F60; execIsA 0xE2BA0.
    l.objectIndex = 0x08;
    l.objectFlags = 0x0C;
    l.objectOuter = 0x40;
    l.objectName = 0x48;
    l.objectClass = 0x50;
    l.pendingKillFlag = 1ull << 61;
    l.processEventSlot = 0x230; // ProcessEvent at UObject vtable 0x10774F8 +0x230.
    l.fieldSuper = 0x60;
    l.fieldNext = 0x68;
    // ProcessEvent 0xF4F80 parameter loops and CallFunction 0xF2C30.
    l.structChildren = 0x70;
    l.structPropertySize = 0x78;
    l.propertyArrayDim = 0x70;
    l.propertyElementSize = 0x74;
    l.propertyFlags = 0x78;
    l.propertyOffset = 0x94;
    l.propertyTyped = 0xD0; // execLetBool 0xE98A0: BoolProperty mask / ArrayProperty inner.
    l.functionFlags = 0xE0;
    l.functionNative = 0xE4;
    l.functionParmsSize = 0xE6;
    l.functionReturnOffset = 0xE8;
    l.functionFunc = 0xD8; // Precedes flags, unlike LE1/LE2.
    // AppendString 0x90580; entry text 0x904E0 / comparison 0x8E570.
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

// Hosts before HostServices v43 didn't say whether the frame's second eye is a replay of the first's draws; the mod
// read [Performance] sharePass itself. Only LE1's hosts ever replayed (plugin/RenderLevers' sharePass), so only there
// does a newer mod on such a host keep the file's rule (game/SystemSettings' LightEnvironmentShadows entry).
#if defined(MEVR_GAME_LE1)
inline constexpr bool kOlderHostReplaysSecondEye = true;
#else
inline constexpr bool kOlderHostReplaysSecondEye = false;
#endif

// The engine's dynamic shadows in VR (game/SystemSettings' built-in `DynamicShadows`; a player's [SystemSettings]
// DynamicShadows still wins). Off on every game. ME1: its cost lever since research/35 (366 shadow draws and 0.6 ms a
// frame; with the grass radius the render thread at a heavy Eden Prime spot went from 9.35 to 6.88 ms), whatever sharePass
// says. The sequels had them on from the morning of 2026-11-17 (0.2-0.3 ms on ME2, each eye a render of its own:
// research/sequel-render-levers.md) until the owner's headset test the same night: the shadows differed between the left
// and the right eye on ME2, as ME1's did, and the owner asked for them off in both eyes.
inline constexpr bool kVrDynamicShadows = false;

// The light environments' character shadows (game/SystemSettings' `LightEnvironmentShadows`; a player's [SystemSettings]
// value still wins): off on every game. The sequels since the owner's 2026-11-17 ("disable the shadows entirely for both
// eyes"; DynamicShadows False alone left ME1's character shadows drawn, research/37 2026-11-13); ME1 too since 2026-11-18
// (the owner: "for all three games, we need to just have shadows permanently disabled"), where until then they were off
// only while the host's second eye was a replay (HostServices v43; sharePass 5 by default, so off for most players).
inline constexpr bool kVrCharacterShadowsOff = true;

// Who is an enemy and who is down (the hand power wheel's lock, the squad context, VR cover's shooters, the squad's orders,
// melee's enemy in reach): ME1's and ME2's BioPawn answers IsHostile itself; ME3's BioPawn has none, so its question is
// Engine.Pawn's (as game/Melee found; with BioPawn's name ME3's lock, squad context and VR cover saw no hostile at all,
// lane AU2 on the combat fixture, 2026-11-18). The death question: ME1's BioPawn.IsDead, the sequels' Engine.Pawn.IsDead.
// Both take `Other` / nothing and return a bool on every game.
#if defined(MEVR_GAME_LE3)
inline constexpr char const* kIsHostileFunction = "Engine.Pawn.IsHostile";
#else
inline constexpr char const* kIsHostileFunction = "SFXGame.BioPawn.IsHostile";
#endif
#if defined(MEVR_GAME_LE1)
inline constexpr char const* kIsDeadFunction = "SFXGame.BioPawn.IsDead";
#else
inline constexpr char const* kIsDeadFunction = "Engine.Pawn.IsDead";
#endif

// The game's own "a fight is on" (its combat music and AI state): what a standing squad order's "calm" waits out
// (game/SquadContext's scene.combat, core/vr/SquadStanding; the ME3 audit's PWR-31). ME1 asks BioPawn.IsInCombatActionState
// (the weapon drawn, which ME1 does in every fight). ME2 has no such function and ME3 keeps a gun in the hand on a mission,
// so the sequels read the flag the game keeps: ME2's on its GameInfo (WorldInfo.Game, SFXGame.SFXGame.bInCombat), ME3's on its
// replication info (WorldInfo.GRI, SFXGame.SFXGRI.bInCombat).
#if defined(MEVR_GAME_LE3)
inline constexpr char const* kInCombatClass = "SFXGame.SFXGRI";
inline constexpr char const* kInCombatHolder = "GRI";
#elif defined(MEVR_GAME_LE2)
inline constexpr char const* kInCombatClass = "SFXGame.SFXGame";
inline constexpr char const* kInCombatHolder = "Game";
#endif

// ME3's transient action cameras (SFXCameraMode_Roll, _Melee, _HitReaction, _EnterCover; vr::IsActionCamera): the game
// swaps to one for about half a second on a roll, a melee, a hit or a cover entry. The owner (2026-11-18, docs/trilogy-plan.md
// "Answered 2026-11-18"): the player stays in first person there, the view level and sliding with the body, the floor held,
// the snap turn still the player's, the camera shakes off. ME1 and ME2 have no such cameras (no such classes).
#if defined(MEVR_GAME_LE3)
inline constexpr bool kActionCameras = true;
#else
inline constexpr bool kActionCameras = false;
#endif

// The shouldered gun (game/SupportGrip, game/ZoomedShots; the ME2 and ME3 audits' WPN-09, docs/trilogy-plan.md "Answered
// 2026-11-18, later": option B). kSupportGripHoldsLt: both hands on the gun at the shoulder hold LT, the game's own aimed fire
// (ME1: its accuracy, no camera of its own on a rifle). The sequels' LT is SFXGameModeDefault.TightAim -> SFXWeapon.SetZoomed
// (true): the camera's zoom, a sniper's sway, ME2's sniper time dilation and the refusals of melee and the roll; there the grip
// holds no LT, and the shot's own calculations below are made as zoomed instead, each for its own call only.
// kZoomedCalls: those calculations, script functions a script calls (engine/CallFunction's scoped handlers): the per-tick
// accuracy (UpdatePlayerAccuracy: the zoomed base inaccuracy, fire penalty and recovery; its object the player's inventory
// manager), the spread's range (GetWeaponAimErrorRange) and ME3's sniper damage (unzoomed, SniperRifleDamagePenalty). ME3's
// shotgun cone (SFXWeapon_Shotgun_Base.PollLatentFire's ZoomAccuracyBonus) is a timer's call, which ProcessEvent makes: no
// CallFunction call to bracket (research/le2-vrik.md "The shouldered gun on the sequels").
#if defined(MEVR_GAME_LE1)
inline constexpr bool kSupportGripHoldsLt = true;
#else
inline constexpr bool kSupportGripHoldsLt = false;
struct ZoomedCall {
    char const* function;
    bool onWeapon;  // the call's object is the weapon (else the player's inventory manager: the held weapon's flag)
};
// kAccuracyOwner: the class whose `Accuracy` UpdatePlayerAccuracy writes (the measurement of a shouldered shot's accuracy).
#if defined(MEVR_GAME_LE3)
inline constexpr ZoomedCall kZoomedCalls[] = {
    {"SFXGame.SFXPlayerInventoryManager.UpdatePlayerAccuracy", false},
    {"SFXGame.SFXWeapon.GetWeaponAimErrorRange", true},
    {"SFXGame.SFXWeapon_SniperRifle_Base.GetFireModeBaseDamage", true},
};
inline constexpr char const* kAccuracyOwner = "SFXGame.SFXPlayerInventoryManager";
#else
inline constexpr ZoomedCall kZoomedCalls[] = {
    {"SFXGame.SFXInventoryManager.UpdatePlayerAccuracy", false},
    {"SFXGame.SFXWeapon.GetWeaponAimErrorRange", true},
};
inline constexpr char const* kAccuracyOwner = "SFXGame.SFXInventoryManager";
#endif
#endif

// Our own scene captures (the scope's zoomed render, game/ScopeCapture) with the player's post-process chain. ME1: yes
// ([Sights] scopeCapturePost = 1 by default; the host's RenderLevers binds a capture's final pass to its own target,
// research/39). The sequels: never, whatever the setting says: ME3's captures came out black with any chain (lane GX3,
// research/le3-galaxy.md), and ME2's final pass has no hook yet that keeps it off the eye (the parity ledger's R31), so
// their galaxy captures run without one too (game/GalaxyProjectorFlow).
#if defined(MEVR_GAME_LE1)
inline constexpr bool kCapturePostProcess = true;
#else
inline constexpr bool kCapturePostProcess = false;
#endif

// The walk's fields (game/CombatStance section C, research/38 "Linear walking"; the ME2 and ME3 audits' MOV-14 to MOV-16).
// kWalkOwnerClass: the class whose object holds the move control's state (m_eAnimStartState, m_eAnimStopState,
// m_eAnimSkidState and, where they exist, the m_bEnable{Start,Stop,Skid}Anim flags): ME1's and ME2's pawn behaviour
// (BioPawn.m_oBehavior), ME3's pawn itself (its locomotion moved onto BioPawn and SFXModule_Locomotion).
// kWalkStepFlags: the game has ME1's three flags that turn its start, stop and skid steps off (ME1, ME2; ME3 has none of them,
// its start and stop are SFXModule_Locomotion's states). kWalkSpeedRamps: ME1's four m_fMaxDesSpd ramps on the behaviour (ME2
// and ME3 have none: only Engine.Pawn.AccelRate follows the linear walk there).
// (ME1's own handles spell its class out: the constant is the sequels'.)
#if defined(MEVR_GAME_LE3)
inline constexpr char const* kWalkOwnerClass = "SFXGame.BioPawn";
inline constexpr bool kWalkStepFlags = false;
#elif defined(MEVR_GAME_LE2)
inline constexpr char const* kWalkOwnerClass = "SFXGame.BioPawnBehavior";
inline constexpr bool kWalkStepFlags = true;
#else
inline constexpr bool kWalkStepFlags = true;
#endif
#if defined(MEVR_GAME_LE1)
inline constexpr bool kWalkSpeedRamps = true;
#else
inline constexpr bool kWalkSpeedRamps = false;
#endif

// The draw and holster animations (game/CombatStance section D, research/38 "The draw rate on the sequels"): ME1 plays them
// in its AnimTree's WeaponEquip node (children Draw and Holster), ME2 in four BioAnimNodeBlendByWeaponEquip nodes of the same
// form (WeaponEquip-NoCoverStand, -NoCoverCrouch, -HighCover, -LowCover), ME3 in AnimNodeSlot channels (Custom_Std_Upper,
// Custom_Std_Idle_Lower, Custom_Crouching_Upper...); the sequence's AnimSeqName is the draw's: these prefixes (their crouch and
// cover variants included: CB_Draw_Crouch, CB_Holster_Cover_Mid). Seen in the player's tree on both (`mevr animtree draw`).
#if !defined(MEVR_GAME_LE1)
inline constexpr char const* kDrawSequencePrefix = "CB_Draw";
inline constexpr char const* kHolsterSequencePrefix = "CB_Holster";
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
#elif defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    // LE3: one GObjects reference at 0x12128F; 43 name-pool matches all resolve to 0x17B33D0.
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
#elif defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    // ME3's 0xBF110 / 0xBF0D0 use the same ABI and slots as ME2; both load GMalloc at 0x17CD588.
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

// Hairstyles with a rigged stand-in the hair stage puts on instead (game/Hair, docs/trilogy-hair-plan.md section 4.5; the
// owner, 2026-11-19: ME3's older Miranda hair is acceptable). Object paths without the class, compared case-insensitively.
struct HairStandIn {
    char const* used;    // the shipped hair mesh (no hair bones)
    char const* rigged;  // a mesh of the same hairstyle with hair bones
};
#if defined(MEVR_GAME_LE3)
// research/trilogy-hair.md: the shipped mesh is weighted to the head and neck only; the unused _Old mesh has 13 hair bones.
inline constexpr HairStandIn kHairStandIns[] = {
    {"BIOG_HMF_HIR_PRO.Hair_PROMiranda.HMF_HIR_PROMiranda_MDL", "BIOG_HMF_HIR_PRO.Hair_PROMiranda.HMF_HIR_PROMiranda_MDL_Old"},
};
#else
inline constexpr HairStandIn kHairStandIns[] = {{nullptr, nullptr}};
#endif

} // namespace mevr::engine::adapter
