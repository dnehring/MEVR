---
type: Audit
title: ME3 rendering and stereo against ME1's contracts
description: How ME3's renderer reproduces ME1's picture; life-size stereo, each eye's own frustum, the colour, dither, tonemap, FSR and HDR answer are the shared modules and ME3 adds a lens-flare fix and has the motion-blur guard ME2 lacks, but both eyes are full engine renders with no one-Draw path, the render size is a fixed 3072x2216 for every headset, and the body's culling pad, the kept locks, Quest pacing and the performance modes are not there.
tags: [audit, me3, parity, rendering, stereo, colour, upscale, performance]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/rendering-and-stereo.md
commit: 583591c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/rendering-and-stereo.md). Reading only; the runs cited are the ones the
ledger and the research notes record (fake headset, Save 31, the renderer preview), none was made for this audit.

As on ME2 this is where the port leans hardest on shared code and where most differences are forced by another
renderer tree (`src/halcyon/le3`). The picture pipeline is one implementation for both sequels (`plugin/EyeCopy`,
`Colour`, `Upscale`, `HdrDisplayPolicy`), life-size stereo and the eye's own frustum are the shared plan and header, and
ME3 is ahead of ME2 in two places: it has the native motion-blur guard, and it fixes what ME1 lists as a known limit of
mode 2 for its lens flares (`plugin/FlareWindow`).

1. **Both eyes are full engine renders.** No one-Draw path, so ME1's 20% and what rides on it (replay matrices, ring
   shadows, the health rule) is absent; `plugin/DrawReplay` is compiled in and never fed. ME3 runs with two levers ME1
   keeps off (`perEyeViewState = 1`, `umbraShareResults = 1`), by measurement.
2. **Nothing owns the render size.** 3072x2216 for every headset (the owner's answer to question 17, a code default and
   the installed ini), no first-start rule, no performance modes; FSR scales that to the headset's eye size.
3. **Three ME1 guards are absent without a reason the game gives:** the body's culling pad (both functions found, the
   constants left 0), the kept locks at quit, and Quest pacing recovery.

| rating | count | ids |
|---|---|---|
| same | 7 | REN-12, 19, 20, 21, 27, 33, 34 |
| same core, other inputs | 8 | REN-03, 04, 05, 10, 14, 17, 18, 24 |
| adapter | 7 | REN-01, 02, 22, 25, 26, 32, 35 |
| partial | 3 | REN-11, 29, 36 |
| missing | 9 | REN-06, 07, 08, 09, 13, 15, 16, 23, 28 |
| n/a | 2 | REN-30, 31 |

# Contract by contract

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| REN-01 | adapter | Halcyon's ME3 renderer as the static library `halcyon-le3` in the host, started by `plugin/HalcyonLe3` | another tree with its own seam file and the common `MevrSeams`; SFR: two Draws a present, always | `CMakeLists.txt:271-289`, `:1143-1193`; ledger R01 (304 first-eye captures for 304 presents) |
| REN-02 | adapter | `ApplyMevrBaseline` after every ini load | a third hand-kept pin list: stereo mode 4, Quest image fix, FOV fill, lean, look speed 1, flat menus, first person with the head hidden and the body shown, first person in cover, no head aim, no stick pitch, Halcyon's VR cines off, no profile hotkeys, no R recenter, the `\` key. Not pinned: `MoveFollowsHead` (ME2 pins it), `MetricEyeFrustum`, the flat panel's distance and size. No render-size rewrite | le3 `me2_menu.cpp:470-533` |
| REN-03 | same core, other inputs | `plugin/MetricStereo` and its core through `MevrSeams::PlanStereo`; the lean from the eyes' centre | the same 100 uu per metre; upstream measured +1.86 degrees per eye crossed (walls at a metre), life-size +0.66 where 0.65 was expected | le3 `me2_xr.cpp:877`, `:889`, `:2186`; ledger R23; research/30 9.3 |
| REN-04 | same core, other inputs | `src/halcyon/MevrMetricView.h` in the SFR branch | governed by `MELE3VR.ini [VR] MetricEyeFrustum` (1 by default, not pinned), where ME1 pins `sfr2MetricEyeFrustum`; mode 2 begins once the HUD panel is armed, mode 1 at a load and after any flat frame | le3 `me2_xr.cpp:156`, `:2884-2885`; research/le3-hud-panel; ledger R06 |
| REN-05 | same core, other inputs | the same mode-2 geometry and the same limit | ME3 goes further than ME1: its lens-flare reflections are unprojected through the CPU's own window for that one call (`[Stereo] flareWindow`, 1 when missing), so the lamps' flares sit on their lamps in both eyes. Other CPU-projected sprites: not looked at | `FlareWindow.cpp:110-145`; research/le3-flares-and-window |
| REN-06 | missing | none | both Draws always run; `sharePassVerified` is not set for ME3 | `RenderLeversTable.hpp:235-262`; ledger R02 |
| REN-07 | missing | none | follows REN-06 | ledger R02 |
| REN-08 | missing | none | moot without a replay | ledger R04 |
| REN-09 | missing | none | moot without a replay | ledger R05 |
| REN-10 | same core, other inputs | `game/SystemSettings`' `LightEnvironmentShadows` entry | off always on the sequels (`kVrCharacterShadowsOff`), not "while the second eye is a replay"; the owner's decision came from ME2's headset test, ME3 follows it unseen | `GameAdapter.hpp:269-277`; `SystemSettings.cpp:141-143` |
| REN-11 | partial | the shared hook on ME3's row | installed and "idle there" in the measured run | `RenderLeversTable.hpp:253-257` |
| REN-12 | same | the shared lever, on | 436 states kept in the run, the reference through RVA 0x2CB940 | `RenderLeversTable.hpp:253-257` |
| REN-13 | missing | hooked in a try build, left off | 102-219 of 315-911 instances under 8 px, no measurable gain; `umbraInstanceVerified` false | `RenderLeversTable.hpp:255-256`; ledger R09 |
| REN-14 | same core, other inputs | the same levers with per-game defaults | ME3: `perEyeViewState` 1, `umbraShareResults` 1; `sfrReplayRetry` keeps the code's 1 (ME2 has 0). ME1's "must stay 0" list does not hold on a tree with a view state per pass | `RenderLeversTable.hpp:243`, `:258-260` |
| REN-15 | missing | upstream's `PaceDisplayLocked` | `plugin/QuestPacing` is LE1-only; a streamed Quest after a load on ME3 is unmeasured | `CMakeLists.txt:914-915`; le3 `me2_xr.cpp:1475`; ledger R24 |
| REN-16 | missing | none | 3072x2216 from the code default and the installed ini, for every headset; `FirstStartEyeSize` empty off LE1; the classic menu's ladder is upstream's | le3 `me2_menu.cpp:1353-1354`; `runtime/MELE3VR.ini`; ledger R35 |
| REN-17 | same core, other inputs | `upscale::Copy` inside `plugin/EyeCopy`; eye images at the headset's size through `MevrSeams::EyeImageSize` | the same passes and keys; the source is a fixed size nothing ties to the headset (REN-16). A run: each eye 19.2 x 14.6 px per degree with a PS VR2's eyes replayed | `Seams.cpp:41-51`; le3 `me2_xr.cpp:1164`; ledger R06, R13 |
| REN-18 | same core, other inputs | `plugin/Colour` in `EyeCopy`'s shader and on the HUD panel (`panelColour` true) | the same core and profiles | `HalcyonLe3Bridge.cpp:124`; `SequelOverlays.cpp:126-135`; ledger R14 |
| REN-19 | same | in the same shader | | ledger R14 |
| REN-20 | same | `colour::HoldCompare` from the shared menu | | shared menu |
| REN-21 | same | `hdr_display::Answer` from the sequels' `GetDesc1` hook (`OnFactory`) | | `HalcyonLe3Bridge.cpp:285-287`; research/le3-hdr-eye-copy-test |
| REN-22 | adapter | `MEVR_TONE` in `EyeCopy`'s shader; the flat panel's copy converts too | as ME2: without LE1's SDR-white scale; unverified | le3 `me2_xr.cpp:1241`, `:1933`; ledger R15 |
| REN-23 | missing | none | the Display group is greyed | ledger R35 |
| REN-24 | same core, other inputs | `game/SystemSettings` | no 3 s wait (the guard is LE2-only); the "not in gameplay" test is `mode > 4`, ME1's range, while ME3's gameplay runs to 6 (squad command and injured Shepard count as not gameplay); a run showed `scale set` acting on ME3 | `SystemSettings.cpp:183`, `:237-243`; ledger R12 |
| REN-25 | adapter | `plugin/MotionBlurGuard` with 7 audited ME3 proxy pairs | installed for another reason than ME1's (a null velocity buffer crashed the recenter); 2,825 guarded conversions, 0 unknown in a run. ME2 has none | `Entry.cpp:138-140`, `:320-321`; ledger R10; research/le3-recenter-transition-failure |
| REN-26 | adapter | `vr-game-settings.ps1 -Game LE3`; the deploy installs `runtime/MELE3VR.ini` when there is none | Setup's LE3 spec lags the tool (settings-and-defaults.md) | `tools/vr-game-settings.ps1`; `runtime/MELE3VR.ini` |
| REN-27 | same | `MevrSeams::RecenterOrientation` to `plugin/RecenterTilt`; 60 degrees | R is ME3's reload, not a recenter; the correction itself unseen (no tilt on the fake headset) | `RendererLe3.cpp:166-168`; le3 `me2_xr.cpp:765-782` |
| REN-28 | missing | none | `kSkelUpdateBounds` and `kPrimitiveUmbraBounds` are 0 for ME3 although both functions were found (0x793850, 0x32EC10). The file's comment says the pad answers Halcyon LE1's view offset; whether ME3's first person draws the eye away from the culling camera was not measured | `Addresses.hpp:468-471`; ledger R29 |
| REN-29 | partial | ArmIk's flag part | without the pad; reading only | ledger R29 |
| REN-30 | n/a | Halcyon ME3's first person clears `bCollisionEnabled` on the modes it owns and sets it back | not measured on ME3 as it was on ME2 | le3 `engine_probe.cpp:165`, `:240-241`, `:266` |
| REN-31 | n/a | no first-person conversation holds a view | | conversations-and-scenes.md |
| REN-32 | adapter | `plugin/FloorLock` in the LE3 host | contracts in body-and-ik | `CMakeLists.txt:1153` |
| REN-33 | same | `plugin/Swapchains` | no `swapchainCap` run on ME3 (ledger R17 "U") | `CMakeLists.txt:1154` |
| REN-34 | same | `plugin/GameThreadCalls`; ME3's first-person hides are queued | the off-thread count in a run log is unread (ledger R34 "U") | `CMakeLists.txt:1153` |
| REN-35 | adapter | `KeyDown` in le3 `d3d_capture.cpp`; `\` pinned | unverified in a run (ledger R33) | le3 `me2_menu.cpp:528-530` |
| REN-36 | partial | the ExitProcess hook is every host's | `kept_locks::Install` is under `MEVR_GAME_LE1`; whether ME3's teardown faults at quit is not recorded | `Entry.cpp:314-321`; ledger G48 |

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| No one-Draw stereo | same |
| Nothing owns the render size | same; ME3's fixed size is at least an owner's answer (3072x2216), ME2's is upstream's 4096x2304 |
| No motion-blur guard | fine: ME3 has it |
| No kept locks at quit | same |
| The body's culling pad not wired though the addresses are found | same |
| `perEyeViewState` and `umbraShareResults` at 1 against ME1's "must stay" | same, by ME3's own measurement |
| `sfrReplayRetry = 0` | different: ME3 keeps 1; no owner's answer recorded for ME3 |
| The 3 s wait before `scale set` | different: none on ME3; unknown whether its loads need it |
| `ConvoFirstPerson` and the vehicle's head mode unpinned | different: ME3's unpinned keys are `MoveFollowsHead`, `MetricEyeFrustum` and the panel size |
| REN-05 unknown on ME2 | fine on ME3 for lens flares (`FlareWindow`); ME2 should be looked at with the same snapshot pair |
| Black eyes on a float swap chain | fine: ME3 came after `EyeCopy` and the SDR answer |

# Reinvented

| ME3 code | ME1 or shared code | what differs | what unifying takes |
|---|---|---|---|
| `ApplyMevrBaseline` in le3 `me2_menu.cpp` | `MevrBridge::ModernBaseline`; ME2's `ApplyMevrBaseline` | a third list; each leaves other keys unpinned | one table of "what MEVR supports" with a setter per renderer and a test that every row has one |
| `plugin/Le3SceneCapture.cpp` | `plugin/RenderLevers`' capture hooks (`sceneCaptureVerified`) | ME3's own RVAs and module (ledger R31 "forked") | ME3's rows in the levers' table |
| The `mode > 4` test in `game/SystemSettings.cpp` | `engine::adapter::IsHudGameplayMode` | ME1's range used as "gameplay" | call the adapter |
| Upstream's `PaceDisplayLocked` | `plugin/QuestPacing` | not ours; ME1's Quest fix absent | the wait seam the ledger names (le3 `me2_xr.cpp` about 1395) |

# Rediscovered problems

| problem on ME3 | when and cost | ME1 already held it | sources |
|---|---|---|---|
| Upstream's stereo was crossed (+1.86 degrees per eye) | measured 2026-11-16 | "Upstream's Stereo 2 with swap-eyes was divergent" | [research/30](../../../../research/30-metric-stereo.md) section 9 |
| Every lamp's flare off its lamp, differently per eye, in mode 2 | lanes DEF, FL, DEF2, 2026-11-17: the default flipped to mode 1 for an afternoon | REN-05, ME1's recorded limit of mode 2 (the sun's sprite) | [research/le3-flares-and-window](../../../../research/le3-flares-and-window.md) |
| The recenter crashed on a null velocity buffer in the blur's pass | 2026-11-15 | ME1's motion blur in stereo, five times under five names | [research/le3-recenter-transition-failure](../../../../research/le3-recenter-transition-failure.md) |
| ME3 ran upstream's third-person configuration until its pins landed | to 2026-11-17 | REN-02: one supported configuration | le3 `me2_menu.cpp:470-479` |
| A scene capture's final post pass bound the wrong target (black window layers) | lane GX, open | ME1's capture hooks (`0x742C80`, `0x773D30`) | [research/le3-galaxy](../../../../research/le3-galaxy.md) "The projector flow" item 4 |

# Forced by the game

- Another renderer tree with a scene view state per pass, so `perEyeViewState = 1`, and no seams for one-Draw.
- ME3's lens flares unproject through the view's own matrix, hence `FlareWindow`.
- R is ME3's reload; 1-4 are its power keys (no profile hotkeys).
- ME3's motion-blur proxies are 7 pairs at its own addresses.
- Halcyon ME3 clears the camera collision itself.

# Not forced

- No body culling pad, no kept locks, no `QuestPacing`.
- No owner for the render size by headset; no performance modes.
- `MoveFollowsHead` and `MetricEyeFrustum` unpinned.
- `mode > 4` as ME3's gameplay test.
- `sfrReplayRetry` different on the two sequels without a recorded reason.

# What to do

1. **Decide the render size's owner** (owner's question: does 3072x2216 stand for every headset, or was it a stand-in
   until the headset rule?). If the rule: `main_menu::FirstStartEyeSize` writes `RenderW` / `RenderH`, the Display rows
   un-greyed. Check: `first VR start:` in the log and the eye-copy line's source size.
2. **Measure whether ME3 culls the body under the eye** (a snapshot looking down at the feet at the head angles ME1
   failed at); if it does, put lane R3's two RVAs in `engine/Addresses.hpp` with patterns. Check: `body bounds: the
   body's boxes are padded`.
3. **`mode > 4` to the adapter** in `game/SystemSettings.cpp`. Check: a unit test over the three tables; ME1 unchanged
   by compiled code.
4. **The quit guard**: one quit of ME3 with `crash-exit.txt` read; install `kept_locks` if it faults.
5. **Pin or publish `MoveFollowsHead`** so the menu's row and the renderer agree (settings-and-defaults.md).
6. **A streamed Quest's pacing after a load on ME3** before porting `QuestPacing`.
7. **One baseline table**; one-Draw stays the ledger's R02.

# Citations

- Catalog: [rendering-and-stereo.md](../../me1/rendering-and-stereo.md). ME2's audit:
  [rendering-and-stereo.md](../me2/rendering-and-stereo.md). Sibling: [settings-and-defaults.md](settings-and-defaults.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) R01-R35, G48. Disagreements: R11 no longer
  describes the sequels (the entry is forced off, not by the host's word); R29 "missing" understates that the addresses
  are known; R12 "shared V" hides the `mode > 4` test. `CLAUDE.md` still says ME2 and ME3 keep dynamic shadows `True`:
  the constant is false for every game.
- Research: [le3-renderer-integration](../../../../research/le3-renderer-integration.md),
  [le3-flares-and-window](../../../../research/le3-flares-and-window.md),
  [le3-recenter-transition-failure](../../../../research/le3-recenter-transition-failure.md),
  [le3-hdr-eye-copy-test](../../../../research/le3-hdr-eye-copy-test.md),
  [le3-renderer-preview-evidence](../../../../research/le3-renderer-preview-evidence.md),
  [sequel-render-levers](../../../../research/sequel-render-levers.md), [30 metric stereo](../../../../research/30-metric-stereo.md),
  [27 colour](../../../../research/27-colour.md).
- Source: [me2_menu.cpp](../../../../src/halcyon/le3/me2_menu.cpp), [me2_xr.cpp](../../../../src/halcyon/le3/me2_xr.cpp),
  [engine_probe.cpp](../../../../src/halcyon/le3/engine_probe.cpp), [MELE3VR.ini](../../../../src/halcyon/le3/runtime/MELE3VR.ini),
  [RendererLe3.cpp](../../../../src/plugin/RendererLe3.cpp), [RenderLeversTable.hpp](../../../../src/plugin/RenderLeversTable.hpp),
  [EyeCopy.cpp](../../../../src/plugin/EyeCopy.cpp), [FlareWindow.cpp](../../../../src/plugin/FlareWindow.cpp),
  [MotionBlurGuard.cpp](../../../../src/plugin/MotionBlurGuard.cpp), [Le3SceneCapture.cpp](../../../../src/plugin/Le3SceneCapture.cpp),
  [Seams.cpp](../../../../src/plugin/Seams.cpp), [HalcyonLe3Bridge.cpp](../../../../src/plugin/HalcyonLe3Bridge.cpp),
  [Entry.cpp](../../../../src/plugin/Entry.cpp), [SystemSettings.cpp](../../../../src/game/SystemSettings.cpp),
  [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp), [Addresses.hpp](../../../../src/engine/Addresses.hpp),
  [MevrMetricView.h](../../../../src/halcyon/MevrMetricView.h), [vr-game-settings.ps1](../../../../tools/vr-game-settings.ps1),
  [CMakeLists.txt](../../../../CMakeLists.txt).
