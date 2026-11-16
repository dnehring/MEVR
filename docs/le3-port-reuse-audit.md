<!-- SPDX-License-Identifier: MIT -->
# ME3 port: existing-system audit

Audited 2026-11-15 against committed main `7a3a1158`. Scope: Legendary Edition single-player ME3 and included DLC.
This is an offline source audit, not a new game or headset test. Uncommitted work in the main checkout was excluded
from the baseline. The [implementation plan](le3-port-implementation-plan.md) turns these findings into work packages.

## What “unified” means here

MEVR has one codebase and common interaction implementations, compiled into `LE1MEVR.asi`, `LE2MEVR.asi` and
`LE3MEVR.asi`. Three game-specific entry points are necessary; three copies of the menu, recognizer, IK solver,
gesture machines, reload logic or testing framework are not. Keep native types, parameter layouts, object lookup,
game-mode mappings and renderer hooks behind the existing adapter/host boundaries.

The historical [trilogy design](trilogy-design.md) describes intent, not the current directory layout or completed
features. Current shared code lives in `src/core`, `src/game`, `src/plugin`, `src/engine` and `src/ue3`; do not create
a parallel `games/` framework just because that old proposal drew one.

## Foundation evidence

| Inspected source | Current finding | Consequence |
| --- | --- | --- |
| [CMakeLists.txt](../CMakeLists.txt), `mevr_add_plugin`; [ModLoader.cpp](../src/plugin/ModLoader.cpp), `BuildServices` | LE3 builds, but has no integrated Halcyon LE3 target/bridge. Shared renderer services are populated for the LE1 and LE2 Halcyon hosts; LE3 gets the legacy path and base/crash services. | A successful trilogy build does not establish ME3 VR parity. Integrate the host before enabling dependent modules. |
| [GameAdapter.hpp](../src/engine/GameAdapter.hpp), `ReflectionLayout`, `ReflectionGlobalsForGame`, `AllocatorGlobalsForGame`; [Runtime.cpp](../src/ue3/Runtime.cpp), `Locate` | LE3 has no layout/globals/allocator definition. `Locate` refuses that game. `Ready()` also applies the module rollout gate; `Up()` only reports the engine layer. | Discover and cross-check LE3 reflection first. Do not switch `kModulesOnLayer` globally to make modules appear supported. |
| [Addresses.hpp](../src/engine/Addresses.hpp), `le3` | Five basic function addresses exist. Skeleton, selection, cover, conversation, allocator and reflection globals include zero entries. | Audit each required hook and its original owner; zero remains unsupported, not a callable address. |
| [GameAdapter.hpp](../src/engine/GameAdapter.hpp), `kFrame`, `IsFlatScreenMode` | LE3's packed script frame differs. Flat modes are already mapped as 7–12 for LE3, versus 5–10 in LE2. | Extend this existing adapter to semantic contexts; remove downstream assumptions that conversation is 5 or galaxy/orbit are 9/10. |
| `reference/MassEffect3-VR` at `cfd40019450d3469b44a9b155f03e0263a61f787` | Local GPL-3.0 renderer source exists; it is not imported into the unified host. | Reuse this pinned upstream renderer with provenance and focused MEVR seams, as for LE2. Never run a second OpenXR or Present owner beside it. |

The installed original `MassEffect3.exe` has fixed file version **2.0.0.48602**, size **26,896,672 bytes**, SHA-256
`72047e65a819878397c25650ccd9c1dc646db6b2517f88a69acacdc6215b9ec7`. Its string version reports `2.0.0.0`;
record the fixed version and hash when matching the executable.

The existing read-only `tools/verify_rvas.py` found `.pdata` function starts at ProcessEvent `0xF4F80`, CallFunction
`0xF2C30`, GameEngineTick `0x5502C0`, CalcSceneView `0x6CFFD0` and ViewportDraw `0x6D1E30`. Draw's exclusive end
is `0x6D40DB`, matching the table's end marker; it is correctly not inside a function. These results establish
boundaries only, not function identity, ABI, valid hook chains or runtime behavior. No executable dump, decompilation,
game launch, deployment or save mutation was performed for this audit.

## Feature reuse and actual remaining work

| Area | Reuse from the unified mod | ME3 adapter or new work |
| --- | --- | --- |
| Stereo, tracking, frame ownership | LE2 host integration pattern; `Renderer.hpp`, `XrHost`, `EyeCopy`, `Swapchains`, `TestLoop`, `FloorLock` | LE3 renderer import, one session/hook owner, metric stereo, HUD capture, eye timing, late camera updates and clean shutdown. Do not assume LE2's capture-order fixes apply unchanged. |
| Body and controller alignment | `core/ik`, `ArmIk`, `HandTargets`, `BodyCalibration`, `ManualCalibration`, `LiveCalibration`, `Fingers`, `SupportGrip` | LE3 skeletal-pose hook and bone/weapon sockets. `BonePose` currently supports LE1/LE2 layouts; LE3's pose-update address is zero. Check each body/armor/weapon family and both play positions. |
| Locomotion and comfort | `Comfort`, `PadMerge`, `CombatStance`, `Posture`, `Sprint`, vignette and close latches | LE3 semantic context mapping, roll/evade, native Storm and physical cover/damage rules. Head tracking must not inherit authored roll or lunge camera rotation. |
| Player menus and omni-tool | `MainMenu`, `MainMenuSettings`, `OmniToolRail`, `WorldUi`, `GamePointer`, `WristBeam`, parameterized theme | LE3 screen identifiers, native input/event adapters, crop/layout and palette. Preserve one document/settings schema; remove LE1-versus-LE2 binary switches where a third game needs explicit data. |
| HUD and interaction labels | `HudPanel`, `HudPieces`, `HudPieceLayers`, `InteractLabel` | ME3 ammo, shield/health and grenade/power displays; native target selection and measured prompt crops. ME2 floating labels still require an explicit crop and have no completed gaze hook. |
| Weapon aim and loadout | `Weapon`, `Le2WeaponObservation`, `HandTargets`, `SupportGrip`, `GunKick`, `ViewLevel` | Native LE3 weapon identity, firing and inventory changes. Preserve weapon weight, installed mods, damage/spread and power recharge rules. Cover charged, sustained-fire and regenerating-ammo exceptions through profiles. |
| Holsters | `core/ik/BodySlots`, `Holsters` reach geometry, calibration and hysteresis | The core still defines four LE1 weapon categories/slots. Separate a body slot from native weapon category and current inventory; add SMG and unusual loadouts without another holster system. |
| Thermal-clip reload | `PhysicalReloadRequest`, `tests/le2-reload-core`, shared hand ownership, [Overlord research](../research/le2-overlord-reload-reuse.md) | The request coordinator is a prototype, not a completed ME2 production reload. Discover LE3 native request/transfer/finish/cancel timing and build one shared production state machine with LE2/LE3 adapters. ME1 venting remains its own heat mechanic. |
| Per-weapon optics | `Sights`, `ScopeParts`, `ScopeCapture`, `ScopeCaptureSource`, `SightLayers`, `SightPictures`; [optics research](../research/le2-le3-vr-optics.md) | LE3 `AimModes`, `CurrentAimMode` and installed optic mods affect resolution. Existing model optics can supply anchors/apertures; generic scope remains a fallback. Rebuild a profile when the weapon, mod or mode changes. |
| Powers | `PowerWheel`, gesture detectors/coach, native ME2 activation and VR target/camera construction | LE3 uses `SFXPowerCustomActionBase`, not ME2's `SFXPower`. Activation retains the target/camera parameters used in ME2; verify the reflected ABI and admission. Preserve evolution, cooldown, target validation, consumable grenade powers and movement powers. |
| Squad and voice | `SquadWeapons`, shared panel/machine, `SquadContext`, `SquadOrders`, `SquadPlace`, `Voice`, `VoiceModel` | LE3 roster, inventory/equip and tactical order signatures, revive/heal rules, vocabulary and cooldowns. The ME2 adapter/header is a template for responsibilities, not a header to relabel. Keep scene generations and pause ownership. |
| Melee | `core/ik/Throwing` swing recognition, `game/Throwables` routing and hit observation | New LE3 light/heavy/class and contextual action adapter. Heavy intent, native combo/action completion, cover synchronization and tracked presentation require explicit work. See [melee audit](../research/le3-melee-audit.md). |
| Ladder, vault and evade | `Traversal`, double-A admission, cue, tracking/cancel rules; [traversal research](../research/le2-le3-vr-traversal.md) | Current shared kinds are None/Vault/ClimbUp. Add needed ladder/evade representations once, with LE3 native reach-spec and landing validation. Never reintroduce automatic cover snap. |
| Cinema and interrupts | `CineScreen`, backdrop, `WorldPanels`, native trigger mapping, transition fade | LE3 conversation/cinematic modes are 7/8. Verify its HUD/camera cuts and native interrupt windows. World first-person conversations remain deferred; authored seated scenes must prohibit traversal. |
| Spoken replies | `core/voice/ScriptMatch`, `core/vr/Thespian`, shared panel, listen ownership and lifecycle hooks | LE3 uses `BioConversationController`, obtained through `BioWorldInfo.m_pConversationManager`, and a different GUI handler. Full spoken text needs a verified conversation-data/TLK path; never treat a paraphrase as the full spoken line. Native playback remains until a tested LE3 mute/skip adapter exists. |
| Galaxy search and pursuit | ME1 `game/GalaxyMap` measured projector and room-reference architecture; shared `DiscPose`, `GalaxyHologram`, scoped main-view camera swap, pointer and trigger latches. `NativeTablePanel` is only an intermediate fallback. | Map/orbit are 11/12. Port LE3 native anchor/camera bindings so the map opens where the CIC preview hologram was, with the room retained. Add scan pulse, Reaper alert/pursuit, recovery and retrieval semantics. ME2 mining is not ME3 system search. The current whole-image table does not satisfy projector or stereo map parity. |
| Scripted actors and campaign | Native-state observation, scene journal, stable-screen fallback, existing Mako ownership patterns | Atlas/mounted weapons, injuries, forced equipment, tutorial actions, DLC minigames and nonstandard camera/player states. Reuse ownership/lifecycle principles; a Mako controller is not an Atlas adapter. |
| Tools and release | `console.ps1`, snapshots, recording/replay, perf probes, deploy script, QA evidence formats and Trilogy Save Editor | Adapt protected session/save handling and installer/package selection for LE3. No proven LE3 runner or fixtures exist merely because a script accepts a game name. See the plan's validation gates. |

## Facts agents must not silently inherit

- LE2's latest integration passed builds and 54 offline tests, but its new squad/voice/Thespian/galaxy behaviors still
  need native acceptance. Reusing those changes transfers their pending tests as well as their code.
- `Runtime::Up()`, a declared SDK function, a filled host-service pointer, and a successful ProcessEvent call are
  separate evidence. None proves that a weapon fired, a power landed, a reply advanced or resources persisted.
- A four-tile squad menu and four body holsters do not establish arbitrary LE3 loadout coverage.
- The local renderer reference and older reports are pinned evidence, not claims about current upstream releases.
- No source from a closed Titanfall mod is available for adoption. Reuse MEVR first and the already reviewed,
  license-compatible Overlord algorithms where they solve a demonstrated gap. Keep external engine hooks out.

## Native source inspection needed next

Use `external/LExSDKv2/Src/LESDK/LE3/{SFXGame_classes.hpp,SFXGame_f_structs.hpp,SFXGame_structs.hpp}` for declarations,
then inspect narrowly selected original-game script/native functions through the existing LegendaryExplorerCore
scanner and executable-analysis workflow. A declaration determines what to investigate, not a safe call contract.
Keep extracted scripts, dumps, saves and binaries ignored. Commit original findings, signatures, invariants and
proof results rather than game code bodies. Inspect mounted base/DLC overrides before accepting a class catalog.
