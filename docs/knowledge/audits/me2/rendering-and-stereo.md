---
type: Audit
title: ME2 rendering and stereo against ME1's contracts
description: ME2 runs Halcyon's ME2 renderer with ME1's life-size stereo, per-eye frustum, colour, FSR, HDR answer, swapchain budget and recenter through shared host code; one-Draw stereo and its replay, the motion-blur guard, frame pacing, performance modes, the first-start eye size, the body's culling pad and the quit-time lock guard are absent, and several lever defaults differ from ME1's "must stay" values by measurement.
tags: [audit, me2, parity, rendering, stereo, performance, colour, hdr]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/rendering-and-stereo.md
commit: 940a75c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/rendering-and-stereo.md). Reading only; the runs cited are the ones the
ledger and the research notes record, none was made for this audit.

This is the category where the port leans hardest on shared code and where the differences are mostly forced: ME2 has
another renderer tree (`src/halcyon/le2`), and everything ME1 built on top of its own tree's seams had to find a seam
again. The picture pipeline (colour, dither, tonemap, FSR, HDR answer), life-size stereo and each eye's own frustum are
shared and verified. The frame's cost is not ME1's: both eyes are full engine renders.

1. **No one-Draw stereo, so ME1's 20% and everything that rides on it (replay matrices, ring shadows, the health rule)
   is absent.** `plugin/DrawReplay` is compiled into the LE2 host and never fed. What ME2 has instead are two levers
   ME1 keeps off (`perEyeViewState = 1`, `umbraShareResults = 1`) and one of its own (`sfrReplayRetry = 0`).
2. **Nothing owns ME2's render size.** `FirstStartEyeSize` is empty off LE1, Performance modes are LE1's, and the fresh
   `MELE2VR.ini` renders 4096x2304 for every headset; FSR scales whatever that is to the headset's eye size.
3. **Three ME1 guards are not installed on ME2 for no reason the game gives:** the native motion-blur guard (ME3 has
   it), the kept locks at quit, and the body's culling pad (the addresses are found, the constants are 0).

| rating | count | ids |
|---|---|---|
| same | 7 | REN-12, 19, 20, 21, 27, 33, 34 |
| same core, other inputs | 7 | REN-03, 04, 10, 14, 17, 18, 24 |
| adapter | 6 | REN-01, 02, 22, 26, 32, 35 |
| partial | 3 | REN-11, 29, 36 |
| missing | 10 | REN-06, 07, 08, 09, 13, 15, 16, 23, 25, 28 |
| n/a | 2 | REN-30, 31 |
| unknown | 1 | REN-05 |

# Contract by contract

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| REN-01 | adapter | Halcyon's ME2 renderer as the static library `halcyon-le2` in the host, started by `plugin/HalcyonLe2` | another tree with its own seams (`src/halcyon/le2/mevr_bridge.h`) and the common `MevrSeams`; SFR: two Draws a present, always | `CMakeLists.txt:259-272`, `:1083-1156`; ledger R01 |
| REN-02 | adapter | `ApplyMevrBaseline` after every ini load | a second hand-kept pin list (a third for ME3): stereo mode 4, Quest image fix, FOV fill, lean, look speed 1, flat menus, first person with the head hidden and the body shown, no head aim, no stick pitch, Halcyon's VR cines off, no profile hotkeys, no R recenter, the `\` key, PosScale 2. Not pinned: the vehicle's head mode, `ConvoFirstPerson`, the flat panel's size. No render-size rewrite | `me2_menu.cpp:490-545` |
| REN-03 | same core, other inputs | `plugin/MetricStereo` and `core/vr/MetricStereo` through `MevrSeams::PlanStereo`; PosScale 2 pinned | the same 100 uu per metre; upstream measured -0.90 degrees per eye divergent, life-size +2 to +6 px on Save 4 | `me2_menu.cpp:532-541`; ledger R23; research/30 section 9 |
| REN-04 | same core, other inputs | `src/halcyon/MevrMetricView.h` `ApplyEyeFrustum` in the SFR branch | the CPU window is widened to hold both eyes; a view whose window cannot (ME2's narrow view without a weapon) renders that pair on the symmetric window | ledger R06; research/30 9.3-9.4 |
| REN-05 | unknown | the same mode-2 geometry | whether ME2's sun sprite and glare land off their place was not looked at. A snapshot pair facing a sun (ME3 needed `plugin/FlareWindow`) settles it | research/le3-flares-and-window (ME3's finding) |
| REN-06 | missing | none | both Draws always run; `sharePass` stands down for want of the renderer's seams; every table row is found | ledger R02 |
| REN-07 | missing | none | follows REN-06 | ledger R02 |
| REN-08 | missing | none | moot without a replay | ledger R04 |
| REN-09 | missing | none | moot without a replay | ledger R05 |
| REN-10 | same core, other inputs | `game/SystemSettings`' `LightEnvironmentShadows` entry | on ME2 it is off always (`kVrCharacterShadowsOff`), not "while the second eye is a replay": the owner, 2026-11-17, after shadows differed between the eyes. The host's word (v43) is not what decides | `GameAdapter.hpp:269-277`, `SystemSettings.cpp:137-141` |
| REN-11 | partial | the shared hook on ME2's row, idle there | hooked and verified to install; "idle" in the measured run, so what it buys on ME2 is not shown | `RenderLeversTable.hpp:223-226`; ledger R07 |
| REN-12 | same | the shared lever, on | 22-45 states built a frame before, 0.0-0.1 after, 161 kept; another RVA for the RHI's AddRef | ledger R08 |
| REN-13 | missing | hooked in a try build, left off | 73-81 of 520-585 instances under 8 px, no measurable gain; the menu's chips and the Performance bundle do nothing on ME2 | `RenderLeversTable.hpp:225`; ledger R09 |
| REN-14 | same core, other inputs | the same levers with per-game defaults | ME2: `perEyeViewState` 1, `umbraShareResults` 1, `sfrReplayRetry` 0, `umbraQueryWait` 1. ME1's catalog says the first two "must stay" 0: on ME2 they are 1 by measurement (lane SP, SP2) and the owner's yes for `sfrReplayRetry` | `RenderLeversTable.hpp:218-231`; ledger R01, R07 |
| REN-15 | missing | upstream's `StereoFramePacing` and `FullRefreshPacing` | `plugin/QuestPacing` is LE1-only; whether a streamed Quest sticks at half rate after a load on ME2 is unmeasured | `CMakeLists.txt:936-937`; ledger R24 |
| REN-16 | missing | none | `FirstStartEyeSize` is empty off LE1; the fresh file's 4096x2304 for every headset; `vr-game-settings.ps1 -Game LE2` mirrors that size | ledger R35; `vr-game-settings.ps1:131-132` |
| REN-17 | same core, other inputs | `upscale::Copy` inside `plugin/EyeCopy`; the eye images take the headset's size through `MevrSeams::EyeImageSize` | the same passes and keys; the source is ME2's 16:9 backbuffer, so "below the eye size" depends on a size nothing sets (REN-16). The menu's Upscale row is greyed | `Seams.cpp:41-51`; ledger R13 |
| REN-18 | same core, other inputs | `plugin/Colour` in `EyeCopy`'s shader and on the HUD panel (`panelColour`) | the same core and profiles; the menu's Colour rows work on ME2 | `HalcyonLe2Bridge.cpp:79`, `SequelOverlays.cpp:126-135`; ledger R14 |
| REN-19 | same | in the same shader | | ledger R14 |
| REN-20 | same | `colour::HoldCompare` from the shared menu | | research/le2-menus "The rows on ME2" |
| REN-21 | same | one policy, `hdr_display::Answer`, from the sequels' own `GetDesc1` hook | found the hard way: black or stale eyes on the float swap chain first | `HalcyonLe2Bridge.cpp:271-273`; ledger R16 |
| REN-22 | adapter | `MEVR_TONE` in `EyeCopy`'s shader | without LE1's SDR-white scale and without the clip in the neutral profile; unverified | ledger R15 |
| REN-23 | missing | none | the Display group is greyed | ledger R35 |
| REN-24 | same core, other inputs | `game/SystemSettings` | ME2 waits until a level has been in gameplay 3 s (a `scale set` during the load hung the game); the same shadows and blur values since 9f556c1a | `SystemSettings.cpp:237`; ledger R12 |
| REN-25 | missing | none | blur is off only through the mod's `scale set`; ME3's host has the guard, ME2's does not | `CMakeLists.txt:942-943`, `:1171`; `Entry.cpp:314-317`; ledger R10 |
| REN-26 | adapter | `vr-game-settings.ps1 -Game LE2`; Setup's `GameSpec` | Setup's LE2 spec lags the tool (settings-and-defaults.md, SET-22) | `vr-game-settings.ps1:130-144` |
| REN-27 | same | `MevrSeams::RecenterOrientation` to `plugin/RecenterTilt`; 60 degrees | R is not a recenter key on ME2 (its reload); the correction itself was never seen on ME2 (the fake headset gives no tilt) | `RendererLe2.cpp:157-159`; ledger R28 |
| REN-28 | missing | none (the addresses are found, not wired) | `kSkelUpdateBounds` and `kPrimitiveUmbraBounds` are 0 on ME2 although lane R2 found both functions (0x798350, 0x3146C0) | `BodyBounds.cpp:28-33`; ledger R29 |
| REN-29 | partial | ArmIk's flag part runs | without REN-28's pad | ledger R29 |
| REN-30 | n/a | Halcyon ME2's first person clears `bCollisionEnabled` itself | measured: the flag 0 every tick, the camera 30 uu ahead while pressed into geometry | ledger G12 |
| REN-31 | n/a | no first-person conversation holds a view | becomes a contract when CNV is ported | conversations-and-scenes.md |
| REN-32 | adapter | `plugin/FloorLock` with ME2's journal seam and the traversal hold | contracts in body-and-ik.md | `CMakeLists.txt:1097-1098` |
| REN-33 | same | `plugin/Swapchains` | | ledger R17 |
| REN-34 | same | `plugin/GameThreadCalls` | "stereo and the queue work", first run 2026-11-14 | ledger R34 |
| REN-35 | adapter | `MevrBridge::HotkeyDown`; `\` pinned | unverified in a run | ledger R33 |
| REN-36 | partial | the ExitProcess hook is every host's | `kept_locks::Install` is under `MEVR_GAME_LE1`: whether ME2's teardown deletes a lock in use is not recorded either way | `Entry.cpp:310-317`; ledger G48 |

# Reinvented

| ME2 code | ME1 or shared code | what differs | what unifying takes |
|---|---|---|---|
| `ApplyMevrBaseline` (`me2_menu.cpp`), and ME3's copy | `MevrBridge::ModernBaseline` (`HalcyonBridge.cpp`) | three lists of pins kept by hand; ME2's leaves the vehicle's head mode, `ConvoFirstPerson` and the panel size unpinned | one table of "what MEVR supports" in shared code, each renderer mapping a row to its own setter; a test that every row has a setter per game |
| `plugin/EyeCopy` (sequels) | Halcyon LE1's blit with the `EyeColour*` and `EyeUpscale` seams | here the sequels have the cleaner path and LE1 the older one | research/27 section 9.3 says how to move LE1 |
| Upstream's `StereoFramePacing` | `plugin/QuestPacing` | not ours; ME1's fix for a streamed Quest is absent | call `QuestPacing`'s seams from `me2_xr.cpp`'s frame loop |

# Rediscovered problems

| problem on ME2 | when and cost | ME1 already held it | sources |
|---|---|---|---|
| Black or stale eyes with Windows HDR on (the game's float swap chain) | 2026-11-14, most of a day (research/le2-hdr is 46 KB) | "A blue or black headset image with Windows HDR on": answer the game SDR | [gotcha](../../gotchas/me2-float-swap-chain.md), [gotcha](../../gotchas/windows-hdr-desktop.md), [research/le2-hdr](../../../../research/le2-hdr.md) |
| Upstream's stereo was divergent (-0.90 degrees per eye) | measured 2026-11-16 | "Upstream's Stereo 2 with swap-eyes was divergent (1.27 degrees per eye)" | [research/30](../../../../research/30-metric-stereo.md) section 9 |
| A `scale set` at the wrong moment hung the game | 9 of 9 runs, 2026-11-14 | the precedence and per-level rules for engine settings | [gotcha](../../gotchas/me2-scale-set-during-load.md) |
| Shadows differed between the eyes | owner's headset test, 2026-11-17 (9f556c1a), after they were turned on that morning | "Character shadows drawn where the left eye sees them" and dynamic shadows as ME1's cost lever | `GameAdapter.hpp:260-277` |
| The body vanished unarmed | owner, 2026-11-17 (6f70774c) | ME1 draws the body in every first-person state | `me2_menu.cpp:514-517` |
| The second eye spins for Umbra results that never arrive | lanes SP, SP2, 2026-11-16 | ME1's research/35 attempts 34-37 on the same wait | [sequel-render-levers](../../../../research/sequel-render-levers.md) |

# Forced by the game

- Another renderer tree: ME2's SFR runs two Draws with a scene view state per pass, so `perEyeViewState = 1` is its
  nature, and one-Draw needs seams that tree does not have.
- ME2's eye source is the game's own backbuffer (16:9, and float on an HDR display), hence `plugin/EyeCopy`.
- ME2's first-gameplay-frame fault with depth of field, hence the 3 s wait.
- R is ME2's reload.
- Halcyon ME2 already clears the camera collision.

# Not forced

- No motion-blur guard, no kept locks, no body culling pad: host code and found addresses, not installed.
- No owner for the render size; the Upscale row greyed over a working `[Upscale]`.
- No `QuestPacing`.
- `minScreenRadiusPx` and the Performance bundle shown in ME1's terms though they do nothing.
- The baseline's unpinned keys.

# What to do

1. **Give the render size an owner.** Make `main_menu::FirstStartEyeSize` and `SetResolution` write `MELE2VR.ini`'s
   `RenderW` / `RenderH` (0.85 of the headset's eye width at 16:9, or measure which shape ME2 should render), un-grey
   Display's size and Upscale rows. Check: `first VR start:` in the log and the eye-copy line's source size.
2. **Install the motion-blur guard on ME2** (ME3's rows show the way; `plugin/MotionBlurGuard.cpp` is already in LE3's
   list). Check: `mevr-motion-blur-guard-tests` for LE2 and the guard's "healthy" line after a level change.
3. **Wire the body's culling pad**: put lane R2's two RVAs in `engine/Addresses.hpp` with patterns. Check: `body
   bounds: the body's boxes are padded` and a snapshot looking down at the feet at the head angles ME1 failed at.
4. **Decide the quit guard**: one quit of ME2 with the first-chance handler's `crash-exit.txt` read; install
   `kept_locks` if it faults.
5. **Measure a streamed Quest's pacing after a load on ME2** before porting `QuestPacing`.
6. **One baseline table** for the three renderers.
7. **One-Draw on ME2** stays the ledger's R02 work; this audit adds nothing to it.

# Citations

- Catalog: [rendering-and-stereo.md](../../me1/rendering-and-stereo.md). Sibling: [settings-and-defaults.md](settings-and-defaults.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) R01-R35, G12, G48. Disagreements: R11 "shared V"
  no longer describes ME2 (the entry is forced off by `kVrCharacterShadowsOff`, not by the host's word); R29 "missing"
  understates that the addresses are known; R07 "adapted" should say ME2's defaults contradict ME1's "must stay" list.
  `CLAUDE.md` still says ME2 and ME3 keep dynamic shadows `True`: the constant is false for every game.
- Research: [le2-renderer-integration](../../../../research/le2-renderer-integration.md), [le2-hdr](../../../../research/le2-hdr.md),
  [le2-picture](../../../../research/le2-picture.md), [sequel-render-levers](../../../../research/sequel-render-levers.md),
  [30 metric stereo](../../../../research/30-metric-stereo.md), [27 colour](../../../../research/27-colour.md).
- Source: [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp), [me2_xr.cpp](../../../../src/halcyon/le2/me2_xr.cpp),
  [RendererLe2.cpp](../../../../src/plugin/RendererLe2.cpp), [RenderLeversTable.hpp](../../../../src/plugin/RenderLeversTable.hpp),
  [EyeCopy.cpp](../../../../src/plugin/EyeCopy.cpp), [Seams.cpp](../../../../src/plugin/Seams.cpp),
  [HalcyonLe2Bridge.cpp](../../../../src/plugin/HalcyonLe2Bridge.cpp), [Entry.cpp](../../../../src/plugin/Entry.cpp),
  [SystemSettings.cpp](../../../../src/game/SystemSettings.cpp), [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp),
  [BodyBounds.cpp](../../../../src/engine/BodyBounds.cpp), [MevrMetricView.h](../../../../src/halcyon/MevrMetricView.h),
  [CMakeLists.txt](../../../../CMakeLists.txt).
