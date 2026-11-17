<!-- SPDX-License-Identifier: MIT -->
# Trilogy parity ledger

**As of 2026-11-16, main `ccacd667`.** This was a read-only audit of the code and the docs: no game runs and no code changes.

**Why it exists.** The owner, 2026-11-16: "We likely need to unify approaches to ensure consistency among all games. We
would find that some work would not leverage the prior art in ME1 and it lead to a lot of waste."

ME1 (LE1) is the reference. ME2 (LE2) and ME3 (LE3) continue it: ME1's systems and defaults first. This page shows in one
place which ME1 system exists for which game, and in what form. It also lists where the sequels copied instead of
sharing, and the seams and plans that would let the next lane reuse instead of redo.

## How to read it

**Cells.** Each row is one ME1 system. Its LE1 code is named in the system column. Each LE2 and LE3 cell starts with
one of these:
- **shared**: the same code runs on that game. At most a name in `engine/GameAdapter.hpp` differs.
- **adapted**: shared core plus a small per-game adapter: `#if defined(MEVR_GAME_LEx)` branches, a per-game address, or
  a thin game file.
- **forked**: a game-specific copy of what ME1 (or the other sequel) has. This is the waste to remove; see [Forks](#forks).
- **missing**: not on that game.
- **n/a**: the game lacks the mechanic. The cell says what the game has instead.

**Verification marks.** A present cell (shared, adapted or forked) then carries one of:
- **V**: verified. The cell names the check: a commit, a research note with its section and date, or a run's stamp under
  `%LOCALAPPDATA%\MEVR\sessions` or the protected runner's receipts.
- **U**: unverified. The code builds and the offline tests pass, but nothing in the game showed it working.

Sequel V marks are fake-headset (SteamVR null driver) or protected runs. No LE3 system and few LE2 systems have a
physical-headset result.

**Check / next step.** The last column says what would move the cell, usually to V. Where it adds work, it names the files.

**What is generated and what isn't.** `python tools/parity-ledger.py` generates the facts at the [end](#generated-facts)
from the sources:
- which host files each game compiles;
- which HostServices each host serves;
- each `src/game` and `src/engine` module's status per game (run, ready, out, ungated, unreferenced, unbuilt; defined in
  the tool's header);
- the common seam vocabulary (`MevrSeams`) per tree, and each tree's own `MevrBridge` seams;
- the engine names a sequel build uses that the game lacks.

It also counts this page's cells into the [totals](#totals). The cells themselves are judgement and cite those facts.

**Keeping it current.** ctest `mevr-parity-ledger` runs `--check`. It fails whenever a host list, a service, a module's
gate or a seam changes without `python tools/parity-ledger.py --write`. When it fails, rerun `--write`, then reread the
rows the change touches.

## Totals

<!-- parity:totals -->
93 rows.

| game | shared | adapted | forked | missing | n/a | present and verified (V) | present, unverified (U) |
|---|---|---|---|---|---|---|---|
| LE2 | 14 | 50 | 2 | 23 | 4 | 50 | 16 |
| LE3 | 14 | 49 | 4 | 20 | 6 | 30 | 37 |
<!-- /parity:totals -->

LE1 is the reference, so it has no cells. A few LE1 systems are themselves n/a under Halcyon's renderer: the reflection
fix (G51), and the classic menu's tab, which our menu replaced. [Sequel-only systems](#sequel-only-systems) aren't
counted.

## The ten biggest forks and gaps

These are ordered by wasted or blocked work. Each is detailed under [Forks](#forks) or its row.

1. **Overlay composition exists twice** (Forks 1; three times until lane U2, 2026-11-16). LE1 has
   `plugin/HalcyonOverlays.cpp` `OverlayLayers` (1,790 lines). Both sequels share `plugin/SequelOverlays.cpp`
   `OverlayLayers`, driven by the capability set and each game's adapter. One `OverlayFrame`. A new quad is wired twice
   (LE1's file and the sequels') until LE1 moves onto the shared function, one layer at a time.
2. **The sequel bridges were merged** (Forks 2, lane U2, 2026-11-16): `plugin/HalcyonSequelBridge.cpp` has the session,
   frame, view, recenter and controller-merge seams, the galaxy settings and the cinema screen once;
   `HalcyonLe2Bridge.cpp` and `HalcyonLe3Bridge.cpp` are each game's adapter, its `PlacePanel` and the eye-image seams.
   ME2 ran with a save; ME3 to its title.
3. **The galaxy map has three designs** (Forks 3, G25).
   - ME1: projector hologram and stereo window (`game/GalaxyMap`, `plugin/GalaxyHologram`).
   - ME2: an opt-in flat table (`NativeTablePanel` via `PlaceGalaxyPanel`).
   - ME3: `Le3GalaxyCapture` re-implements ME1's `StepWindow`, plus a copy of LE2's table placement and its own input
     lease.

   `docs/galaxy-projector-handoff.md` "5. Apply the same shared flow to ME2" is the plan, still to do.
4. **The sequels have only the first of ME1's renderer performance work** (R02-R09, R13, R23, R24).
   - Since 2026-11-16 the render levers, the draw replay and the perf probe are **one module in all three hosts**
     (`plugin/RenderLevers`, `DrawReplay`, `PerfProbe`), each lever reading its addresses from the game's rows of
     `engine/Addresses.hpp` by name (`plugin/RenderLeversTable.hpp`; research/sequel-render-levers.md). A lever without
     its rows, its verified layout or its A/B stands down. Every address is a row now (lanes R2, R3). On for both
     sequels since their first measured runs: the accounting, the Umbra timing, `keepBoundShaderStates`. Measured
     there: the second eye spins 3 to 5 ms a frame for Umbra query results that are never ready (R07), which
     `umbraQueryWait = 0` removes; the owner's decision. One-Draw waits for that, the per-eye frustum and the
     renderer's seams.
   - Still missing outright: FSR, metric stereo and Quest pacing.
   - The sequels render the whole frame twice every present. On LE1, one-Draw saved 2.3 ms (20%) at the heaviest cell
     (research/37).
5. **ME3 lacks ME1's VRIK presentation layer; ME2 lacks holsters** (G08-G12, G18). **Holsters adapted 2026-11-16 (lanes
   C3, C4):** one module for the three games, run on ME2 (G08).
   - LE3 has the holsters' adapter (unrun); no camera collision or combat stance. Its gun kick, view level and camera shakes are ME2's
     branches since 2026-11-16 (lane GF; G09-G11), built and ReflectChecked but not yet run in ME3.
   - ~~LE2 has no holsters, and its right grip is RB, which opens ME2's wheel.~~ The right grip holds the gun on foot in VR;
     RB is the right stick click (ME2's own hold and tap).
   - All of this code exists for ME1; the sequels need adapters, not designs.
6. **Squad weapons and the sequel power roster were forked** (Forks 5, 6). **Unified 2026-11-16 (lane GF):** one menu
   tick (`SquadWeapons.cpp` `TickCustom`) over a per-game `native` adapter, and one roster walk
   (`game/SequelPowers.hpp`). What is left is the in-game check on each sequel (G30).
7. **Traversal and LE3's movement admission are forked** (Forks 4, 11).
   - LE2's native block in `game/Traversal.cpp` and `game/Le3Traversal.cpp` are two observers over one core. Still open.
   - LE3 copied the same movement-admission predicate six times (Comfort, ArmIk, Posture, Sprint, Melee, Le3Traversal).
     **Unified 2026-11-16 (lane GF):** `game/Player` `MovementRefused`.
8. **The test tooling is forked** (Forks 10, G54, R26).
   - LE2 has a diagnostic host (`MEVR_LE2_DIAGNOSTIC`), `tools/qa-le2`, `tools/le2-halcyon-run`, three launchers and a
     second save contract (`le2-save.py` recipes and receipts beside `le1-save.py` rewinds).
   - LE1 has `fake-headset-session.ps1`.
   - The perf probe is in all three hosts since 2026-11-16 (R26); the sequels' captures lack what their renderers have
     no seam for yet (the eyes' split, the draw count).
9. **Neither sequel can ship yet** (G47, G50, G02).
   - Setup and report-a-problem take the game as a parameter since 2026-11-16 (G47: tested on fake games only), but the
     sequels are not offered: one switch (`package-release.ps1 -Games`), the owner's call.
   - Since T04 (2026-11-16) no sequel players' build links LExSDKv2: a monolithic build of all three games configures
     without the submodule (`MEVR_LE2_LESDK`, `MEVR_LE3_LESDK` off), and no module reads through `engine::legacy`
     (research/34 section 6.4; the generated module table has no "LExSDKv2" note left). Seen in the game: ME2 development
     `reflect: 637 of 637`, ME2 monolithic to gameplay, ME3's engine probe `621 of 621`; a monolithic ME3 run is left.
   - What is left for M7.2 (ME2) and L3-10 (ME3) is an install of each on the real game and the owner's word to offer one.
10. **The eye-image pipeline. One core since 2026-11-16 (lane EY, research/27 section 9)** (Forks 7, 8; R11, R13-R16).
    - The sequels get ME1's FSR (`plugin/Upscale`) and tonemap (`MEVR_TONE`) in `plugin/EyeCopy`, on eye images
      `MevrSeams::EyeImageSize` makes larger than the backbuffer; the HDR answer is one policy for the three.
    - LE1 keeps its own call shape (its blit's `EyeColour*`/`EyeUpscale` seams) onto the same core.
    - ~~`LightEnvironmentShadows False`, a workaround for LE1's replay, is applied on both sequels~~ fixed 2026-11-16
      (lane GF2, R11): it follows the host's word that the second eye is a replay (HostServices v43).

The deferred first-person conversations (G36, G38) are the largest gap by line count: about 10,000 `Ready()`-gated
lines. They are left off this list because the owner deferred them, not because the work went to waste.

## Renderer and performance

**Groups** say what porting an ME1 technique takes:
- **1**: already game-independent (D3D11/OpenXR/maths); it runs on any host once its call site exists.
- **2**: the same technique, needing a per-game hook point, address table or renderer seam.
- **3**: a fix for Halcyon ME1's own bug or for our LE1 workaround; port it only if the bug shows there.

The [technique groups](#renderer-techniques-by-group) summary follows the table.

| id | technique (LE1 code) | group | LE2 | LE3 | check / next step |
|---|---|---|---|---|---|
| R01 | Two-pass stereo: Halcyon's Stereo 2, two `FViewportClient::Draw` per present (`src/halcyon/le1` render_hook.cpp, xr_session.cpp) | 2 | **adapted** V: upstream ME2 SFR (`src/halcyon/le2/calcview_hook.cpp` DrawDetour, `t_replay`), pinned by `ApplyMevrBaseline`; separate eyes in run `20261115-165553-786-LE2-renderer-preview` (d93d39de). Its `[NOOPRETRY]` draws pass 1 again on 4-20% of frames (a pass-1 Draw under 100 us that did render), a third scene render of 2-9 ms: `[Performance] sfrReplayRetry=0` (lane SP) removes it, no frame lost its second eye (research/sequel-render-levers.md "Performance research"). With the world paused the retry fires on 94-98% of frames (lane SP2: render work 12.9 ms a frame against 3 with it off; the wheels and our menu pause the world) | **adapted** V: upstream ME3 SFR (le3 calcview_hook.cpp DrawDetour); 304 first-eye captures for 304 presents, run `20261115-162209-606` (research/le3-renderer-preview-evidence.md) | Physical fusion in a headset. Add an LE3 line to notes/headset-checklist.md (it has none). |
| R02 | One-Draw stereo, `[Performance] sharePass = 5`. `plugin/RenderLevers.cpp`: SceneRenderHook on FSceneRenderer::Render `0x76EE20`, FinishHook `0x76DD10` Ã¢â€ â€™ SynthesiseSecondView, tail start `0x77BE00`, deleter `0x4ED700`. Seams `RecordPass1View`, `g_oneDrawStereo` | 2 | **missing**: DrawDetour always runs both Draws. The module is in the host, every row is in ME2's table (lane R2; the stride is 0x11A0), and `sharePass` stands down for want of the renderer's seams | **missing**: same; lane R3 found every row and layout (the stride is 0x11B0) | The host side is shared (`RenderLeversTable.hpp`). Per game it needs: the rows of the `sharePass` group under LE1's names; `sharePassVerified`; and in Halcyon's tree a RecordPass1View in the SFR branch, a skip of DrawDetour's replay Draw, `StereoFrameActive` and a pass-0 capture between the finish and the replay (research/sequel-render-levers.md "One Draw a frame on the sequels"). The per-eye frustum (R06) first. Then A/B with R26. |
| R03 | Second-eye D3D replay (`plugin/DrawReplay.cpp`, sharePass 2/3/5; `Record*` seams in le1 d3d_capture.cpp) | 1 (recorder) / 2 (tail and finish) | **missing**: `plugin/DrawReplay` is compiled in and never attached (nothing feeds it) | **missing**: same | Port with R02: the `Record*` seams in each sequel `d3d_capture.cpp` draw hook. |
| R04 | Replay screen-space matrices (`core/vr/ScreenMatrix`, `core/d3d11/ShaderReflect`, seam `NotePixelShaderCreated` on CreatePixelShader, `replayScreenMatrices`) | 1 + seam | **missing** (moot without R03) | **missing** | Not determined: whether ME2/ME3 shaders take `ScreenToShadowMatrix`/`ScreenToWorld`. |
| R05 | Replay buffer-wrap shadows (DrawReplay's Map hook, `ShadowFor`) | 1 | **missing** (moot without R03) | **missing** | Comes with R03. |
| R06 | Each eye's own off-centre frustum (`oneDrawWindow = 0`, `sfr2MetricEyeFrustum`; render_hook.cpp `ApplyMetricFrustumSEH` at view +0x1D0) | 2 | **adapted** V: `src/halcyon/MevrMetricView.h` `ApplyEyeFrustum` in the SFR branch (one header for both sequel trees), the window widened to hold both eyes (`[LINKFOV2]`'s cover on every runtime with life-size); fake headset, Save 4: `pass 0 frustum ... the CPU window +-1.0000 x +-1.1250 holds it`, far scenery +2 to +7 px, the HUD panel pixel-identical (research/30 9.3, lane LS 2026-11-16) | **adapted** U: the same header and cover; mode 2 since its HUD panel takes the UI (`[MEVR-STEREO] stereo mode 2` on Save 31, research/le3-hud-panel.md), the window widened (research/30 9.3) | Asymmetric eyes (PS VR2, Quest) in a headset: nothing popping at an eye's outer edge; the wider culling window's cost (lane SP). LE1's `ApplyMetricFrustumSEH` is still its own older copy of the same edit. |
| R07 | Umbra levers: `umbraEyeMatrix` (IssueOcclusionQueries `0x8B6550`), `umbraQueryWait` (`0x8C34C0`), `perEyeOcclusion` (view state +0xD10), `umbraSharedVisibility`, `umbraSecondEyeClearBits` | 2 | **adapted** V: the shared module on ME2's rows (lane R2). On since run `20261116-141434-005-LE2-pl-levers`: the commander's timing and the query-read counts, the idle `umbraEyeMatrix` hook. `umbraQueryWait` works and is left at ME1's 1. Measured there: the second eye spins 2 to 4.5 ms a frame for Umbra results that are never ready; 0 removes it (research/sequel-render-levers.md "ME2"). Lane SP (stretches `LE2-20261116-162623`, `-170323`): the spin is not the per-eye state (`perEyeViewState=0`, now a lever on the sequels with a per-game default, keeps it and adds UE3's wait); `umbraQueryWait=0` halves the render work (10.4 -> 5.0 ms at Freedom's Progress). Lane SP2's batch on a quiet machine after lane LS (stretch `LE2-20261116-201015`): `umbraShareResults=1` with `sfrReplayRetry=0` best and the only option good in both rounds (render work 11.0 -> 5.8 ms, frame p99 21.4 -> 14.3, frames over the period 50% -> 6%, the second eye still occluded), the same picture as today's at a still pose, dynamic shadows included | **adapted** V: the same on ME3's rows (lane R3), run `20261116-143006-726-LE3-pl-levers`; the same spin, 3 to 5 ms a frame; the command gate read 1. Lane SP (stretch `LE3-20261116-174821`, other lanes compiling), repeated by lane SP2 on a quiet machine after lane LS (`LE3-20261116-202316`): `umbraShareResults=1` 11.5 -> 4.8 ms, frames over the period 52% -> 12%, with the second eye still occluded (686 visible instances against 667; `umbraQueryWait=0` 850), noise-level against today at a still pose's occluder edges; the shared state no cure | The owner decides (research/sequel-render-levers.md "The batch before the headset test"): ME2 `umbraShareResults=1` and `sfrReplayRetry=0`, ME3 `umbraShareResults=1`, prepared unlanded on branch `sp2-second-eye-defaults` (per-game defaults in each `kLayout`). `perEyeOcclusion` has nothing to cure there; `umbraSharedVisibility` stays unported. |
| R08 | `keepBoundShaderStates`: hook on the FD3D11BoundShaderState constructor `0x9A89F0`, AddRef through GDynamicRHI `0x17938C8` vtable slot 0x8A0 | 2 | **shared** V: on, ME1's default (`boundShaderStateVerified`); run `20261116-141434-005-LE2-pl-levers`: 22-45 states built a frame in heavy views before, 0.0-0.1 after, 161 kept, about 0.4 ms off the first eye there; the reference count 0 -> 1 through the live RHI's slot 0x8A0 (RVA `0x2B2640`, not the `0x992830` of lane R2's recipe) | **shared** V: on; run `20261116-143006-726-LE3-pl-levers`: 86-122 a frame before, 0.0-0.1 after, 436 kept, about 0.8 ms off the second eye in heavy views; AddRef at RVA `0x2CB940` as lane R3's recipe says | Measure again in a heavy scene of each game (research/sequel-render-levers.md "Still to measure"). |
| R09 | `minScreenRadiusPx` (UmbraInstanceVisibleHook `0x8BE0B0`) | 2 | **missing**: hooked and measured in a try build (73-81 of 520-585 instances under 8 px in heavy views, no measurable gain), left off (`umbraInstanceVerified`) until a still-head snapshot pair shows no pop-in | **missing**: the same (102-219 of 315-911; the cull answers only the marking call, `Layout::instanceCallAlsoKeepsBooks`) | Lane SP's still pairs (research/sequel-render-levers.md "Performance research"): at 8 px ME2 loses the moon's glow and a beacon light, ME3 the lights' lens-flare streaks (1.2-1.3% of the pixels). **Stays off on both**; the flag stays false. ME1 runs at 8: worth a look whether its lights lose their flares too. |
| R10 | Motion-blur guard (`plugin/MotionBlurGuard.cpp`: MotionBlurProxy vtable swap, LE1 5 pairs, installed in `Entry.cpp`) | 2 (3 on LE1: settings re-enabled the blur, research/62) | **missing**: blur is off only through R12's `MotionBlur False`; `motionBlurProtected` not served | **adapted** V: 7 audited ME3 proxy pairs (`Addresses.hpp` LE3 `kMotionBlurProxyPairs`); a different reason (a null velocity buffer crashed the recenter); run `20261115-180239-899`, 2,825 guarded conversions, 0 unknown (research/le3-recenter-transition-failure.md) | LE2: a head-turn snapshot pair looking for one-eye smear (LE1's symptom) before porting. |
| R11 | `LightEnvironmentShadows False` while the second eye is a replay (`game/SystemSettings.cpp` `Wanted()`) | 3 (our replay's workaround) | **shared** V: since 2026-11-16 (lane GF2) the entry follows the host's word (HostServices v43 `secondEyeIsReplay` = `render_levers::SecondEyeIsReplay()`: sharePass 2, 3 or 5 where the game has the lever), not the file; where the second eye is its own render the player's own `GamerSettings.ini` value is put back (the game had persisted our False into `BIOEngine.ini`). Seen: `system settings: scale set ..., LightEnvironmentShadows True` on ME2 (session `20261116-155312-554-LE2-gf2-run3`). **What it changes on screen is not shown:** at Freedom's Progress a same-pose A/B of the key alone changes nothing visible, with or without dynamic shadows; the squadmates' shadows on the ground there are `DynamicShadows` (R12's ME1 value, False, keeps them off; on, they are right in both eyes). PL's earlier "contact shadows" pair was not same-pose (research/sequel-render-levers.md "Character shadows on the sequels", 2026-11-16 correction) | **shared** U: the same rule; ME3's `sharePass` stands down, so it gets the player's value; not run | ME3: the `system settings:` line with `LightEnvironmentShadows True` in a run. A scene where the key shows at all (indoors, a light environment's shadow) for a same-pose pair. `DynamicShadows` on the sequels: measured by lane SP2 with each second-eye option, right in both eyes on both games, about 0.4 ms (research/sequel-render-levers.md "The batch before the headset test"); on or off is the owner's call. |
| R12 | SystemSettings applied by the mod (`game/SystemSettings`: `scale set` after each level change, each boolean through its opposite) | 1 | **adapted** V: `#if MEVR_GAME_LE2` waits 3 s into gameplay; A/B runs AÃ¢â‚¬â€œJ 2026-11-14 (research/le2-renderer-integration "The first-gameplay-frame fault", 87af915d) | **shared** V: `scale set` acts on LE3 (lane SP2, stretch `LE3-20261116-202316`): the log's `system settings: scale set DynamicShadows True, ...` and a same-pose A/B in which Shepard's shadow appears on the grating in both eyes (0.8% of the pixels); `MotionBlur` and `FoliageDrawRadiusMultiplier` there still unshown (research/sequel-render-levers.md "The batch before the headset test") | LE3: an A/B of `MotionBlur` and of `FoliageDrawRadiusMultiplier` in a scene where each shows. |
| R13 | FSR 1 eye upscale (`plugin/Upscale`, `core/vr/Upscale`; seam `EyeUpscale` in BlitTextureToEyeWithFill) | 1 + seam | **adapted** V: the same module and passes (`upscale::Passes`) from `plugin/EyeCopy` (`upscale::Copy` inside `Blit::Run`); eye images larger than the backbuffer through the new seam `MevrSeams::EyeImageSize` in `EnsureAerSwapchains`; at 1280x720 the eyes were 1912x1076, FSR-scaled, twice the bilinear fill's gradient energy at the same mean luma, 0.08 ms of GPU per eye, session `20261116-155924-548-LE2-ey-picture` (research/27 9.5) | **adapted** U: same path; not engaged in the preview (its 3072x1728 backbuffer is wider than the recommended eye); ctest `mevr-eye-copy-warp` | LE3: a run with a backbuffer below the recommended size. The sequels' render-size row in the menu (research/27 9.4). |
| R14 | Colour per headset (`plugin/Colour`, `core/vr/Colour`; LE1 seams `EyeColourShaders/Shader/Done` in Halcyon's eye blit) | 1 | **adapted** V: the same core in `plugin/EyeCopy`'s corrected shader (`MEVR_EYE_OUT`); research/le2-picture.md; 2026-11-16 Quest and a custom grade 100% within one step of the CPU reference, neutral the raw copy bit for bit (research/27 9.5) | **adapted** V: same path; headset set in `HalcyonSequelBridge`'s `OnSessionCreated`; Quest and the custom grade 100% within one step of the CPU reference, neutral bit for bit, session `20261116-161710-117-LE3-ey-me3`; the HUD panel's colour hooks on since 2026-11-16 (`panelColour`), unseen (the preview's panel is off) | LE1 keeps its own call shape onto the shared core (research/27 9.3). |
| R15 | HDR tonemap (`MEVR_TONE` in le1 xr_session.cpp `ToEye`; `[Colour] tonemap`, `toneHue`) | 1 + seam | **adapted** U: `MEVR_TONE` in EyeCopy's corrected `PSPlain` (the identity for an 8-bit source); without LE1's SDR-white scale (`colorScale.x`) and without the clip in the neutral profile (research/27 9.2) | **adapted** U: same | Moot while `hdrDisplay = -1`, the default on all three; the white scale only if the owner brings HDR back. |
| R16 | HDR display answer (LE1 `HalcyonBridge.cpp` `HdrDisplayDesc`, through Halcyon LE1's GetDesc1 patch) and the float frame copied into the eyes | 1 | **adapted** V: one policy, `hdr_display::Answer` in `plugin/HdrDisplayPolicy`, which LE1's seam forwards to (with LE1's one-time move of 0 to -1) and the sequels' own GetDesc1 hook (from `OnFactory`) calls; + `plugin/EyeCopy`; answered SDR, format 28 (research/le2-hdr.md) | **adapted** V: same modules; 8-bit backbuffer in run `20261115-162209-606`; ctest `mevr-eye-copy-warp` | The answer's log line in the next run of each game (`hdr display: ... answered`). |
| R17 | Swapchain budget (`plugin/Swapchains`, `core/vr/SwapchainBudget`) | 1 | **shared** V: `swapchains: gun status ... made` (stretch `20261114-212412-lane-b2-layers`) | **shared** U | LE3: a `[Debug] swapchainCap = N` run, as LE1's of 2026-11-06. |
| R18 | HUD panel (`plugin/HudPanel`; LE1 has 11 `HudPanel*` seams in d3d_capture.cpp via HalcyonOverlays) | 1 (core) / 2 (UI-draw classification) | **adapted** V: one `MevrHudPanelDraw` seam in le2 d3d_capture.cpp's three draw hooks, `PcHud::SetAuthoredLayout`; stretch `20261114-2152..2206-lane-b3-hud` (e03a7364) | **adapted** V: the same `HudPanelDraw` site (`EmitUiDraw`), `hudPanelGameplay` on and ME1's `Hud()` defaults; gameplay modes by `engine::adapter::IsHudGameplayMode`; Save 31: `game UI goes to the body-locked HUD panel`, eyes' world layers without UI, mode 2 by itself (research/le3-hud-panel.md, lane HU) | LE3: the combat HUD (no combat fixture) and the crosshair on the panel (`screenReticleHide` false: T14). |
| R19 | Hand reticle, with the game's crosshair hidden (`plugin/HandReticle`; `game/ScreenReticle`, `hideScreenReticle`) | 1 | **adapted** V: crosshair `_alpha=0` (stretch `20261115-001659-lane-b4-m2`, 30902fc6) | **adapted** U: ring through `SequelWeaponLayers`, on LE1's `ControllerAimActive` since lane U3 (seen on the replayed hands, 2026-11-16). Crosshair hide missing: `hideScreenReticle = nullptr` in BuildServices, and ScreenReticle compiles LE1 field names in its `#else` | LE3: a ScreenReticle LE3 branch (ME3's SDK list has no `BioSFHandler_Reticule`, see the generated names). |
| R20 | Overlay layer composition (`plugin/HalcyonOverlays.cpp` OverlayLayers) | 1 (modules) | **adapted** V: `plugin/SequelOverlays.cpp` OverlayLayers (one composition for both sequels: capabilities + ME2's adapter in `HalcyonLe2Bridge.cpp`) + `SequelWeaponLayers`/`SequelSquadLayers`/`MenusLe2`; stretch `20261116-150654-945-LE2-u2-le2` (Save 4: both eyes, the `hud` layer, every seam group in the log) | **adapted** U: the same `SequelOverlays.cpp` with ME3's adapter in `HalcyonLe3Bridge.cpp`; ran to the title on the native panel (preview `20261116-151138-682-LE3-u2-le3`), no save loaded since the merge | Forks 1: LE1's own composition is the remaining copy. |
| R21 | Cinema screen, its backdrop and the transition fade (`plugin/CineScreen`, `core/vr/CineScreen`, `CinemaBackdrop`, `TransitionFade`; LE1 seams in xr_session.cpp and PlanStereo's screen branch) | 1 + seams | **adapted** V: `plugin/SequelCinema` through `CineScreenWanted`/`HalfSeparationUU`/`Layers`/`TransitionFadeLayer`; run `20261115-042015-m5-cinema` (research/le2-cinema.md, 1834c240) | **adapted** U: the same seams wired (le3 me2_xr.cpp), Halcyon's own VR cine forced off; no separate-eye cinema acceptance | LE3: snapshots in a mode-7 conversation (the LE2 run's script). |
| R22 | Front end and presentation hold (`plugin/PresentationHold`; seams `FrontEndHold`, `HoldPlainVr`, `FrontEndOnScreen`, `FrontEndBeforeTitle`) | 3 | **missing**: Halcyon ME2's flat panel (MonoMenus) | **missing** | Check first whether the sequels flip between views at start-up: snapshots from launch to the title. Port only if they do. |
| R23 | Metric (life-size) stereo (`plugin/MetricStereo`, `core/vr/MetricStereo`, `PlanStereo`, `NoteStereoSubmit`; `sfr2Metric`) | 2 (seam) on a group-1 core | **adapted** V: the same `plugin/MetricStereo` and core in the host, the common seams `PlanStereo`/`NoteStereoSubmit`/`LifeSizeStereo`; the facts in me2_xr.cpp `MevrPlanStereo`, pinned in `ApplyMevrBaseline` (PosScale 2, the lean from the eyes' centre at 1:1). Fake headset, Save 4: upstream -72 px (-0.90 deg per eye divergent), life-size +2 to +6 px (research/30 9.3, lane LS 2026-11-16) | **adapted** V: the same, pinned in le3 `LoadValues`. Renderer preview, Save 31: upstream +1.86 deg per eye crossed (the walls at a metre), life-size +0.66 (0.65 expected at 2.8 m) (research/30 9.3) | The owner's headset check (notes/headset-checklist.md section 8). |
| R24 | Frame pacing (`plugin/QuestPacing`; seams `EarlyFrameWait`, `FullFrameLoop`, `QuestPacing*`; `pacingRecovery`) | 1 (core) + 2 | **missing**: upstream StereoFramePacing | **missing**: upstream PaceDisplayLocked | Needs an xrWaitFrame seam in each sequel RunFrame (le2 me2_xr.cpp ~1587, le3 ~1395). Test on a streamed Quest. |
| R25 | Test loop: snapshots, recording, replay (`plugin/TestLoop`, seam `WrapSessionFunctions`) | 1 + seam | **adapted** V: run `20261114-191506-lane-b-testloop` (04fa43a2); waitFrame/beginFrame not wrapped | **adapted** V: every protected run, e.g. `20261115-182253-344` (14 captures) | Ã¢â‚¬â€ |
| R26 | Perf probe (`plugin/PerfProbe`, `core/vr/FrameProbe`; seams `PerfPresentScope`, `PerfPass0Captured`, `PerfPoint`) | 1 (core) + seams | **adapted** V: the same `plugin/PerfProbe.cpp` with the sequels' adapter (`PerfProbeSequel.cpp`); 10 captures in run `20261116-141434-005-LE2-pl-levers` (frames bounded by xrEndFrame, GPU timestamps, ticks). With lane U1's `MevrSeams::Perf*` calls (run `LE2-pl-seams`, 14:54): frames bounded by Present, 961 draws a frame; `PerfFirstEyeCaptured` comes 61 draws before Present, so it doesn't split the eyes yet | **adapted** V: same; 10 captures in run `20261116-143006-726-LE3-pl-levers`. With lane U1's seams a capture never started (the Present seam gives no device context); the probe now falls back to the context it finds itself, not yet run in the game | ME3: a run with the probe's context fallback. ME2: `PerfFirstEyeCaptured` at the pass boundary (research/sequel-render-levers.md "The landed configuration with lane U1's seams"). |
| R27 | Delayed per-eye pose history (LE1: Halcyon's EYETAG code in xr_session.cpp; sequels: `core/vr/StereoPoseTag.hpp`, `core/vr/EyePublication`) | 1 + seams | **adapted** V: run `20261115-165553-786` (research/sequel-eye-pose-tags.md) | **adapted** V: runs `20261115-165123-287`, `20261115-174634-476` | Physical fusion. |
| R28 | Recenter: yaw-only, plus a removed tilt (seam `RecenterOrientation` Ã¢â€ â€™ `plugin/RecenterTilt`, `core/vr/Recenter`) | 1 + seam | **shared** V: `RecenterAppSpace` asks `MevrSeams::RecenterOrientation` (`plugin/RecenterTilt` in the LE2 host, lane U1). The seam ran in session `20261116-123754-740-LE2-u1-le2` (`[MEVR-RECENTER] yaw-only recenter (upstream), automatic`). The correction itself needs a tilted base space, which the fake headset does not give: ME1 code, unseen on ME2 | **shared** U: `RecenterAppSpace` asks `MevrSeams::RecenterOrientation` (`plugin/RecenterTilt` in the LE3 host, lane U1, 2026-11-16). Built; no run since (the yaw-only recenter ran in `20261115-180239-899`) | Call `RecenterOrientation` in each sequel's `RecenterAppSpace`. SteamVR's upside-down start reaches every game (docs/le2-from-le1.md 3.1). |
| R29 | Body bounds for the culling camera (`engine/BodyBounds`: post-hook `0x58EEC0`, Umbra hand-over `0x25CFE0`; ArmIk's `bConsiderAllBodiesForBounds`) | 2 + 3 (cause: Halcyon LE1's GPU-only view offset) | **missing**: `ready`; `kSkelUpdateBounds`/`kPrimitiveUmbraBounds` are 0 (lane R2 found both functions, `0x798350` and `0x3146C0`: rows of ME2's table). ArmIk's flag part runs | **missing**: same; lane R3 found ME3's two (`0x793850`, `0x32EC10`) | First check whether the sequels cull the body at all. LE1's repro: look down with the head turned (gotchas/culling-camera-ahead-of-the-eye). |
| R30 | Spectator mirror (`game/Spectator`, `plugin/Spectator`, seam `SpectatorMirror`) | 1 (host draw) + 2 | **missing** | **missing** | Ã¢â‚¬â€ |
| R31 | Scene-capture plumbing (RenderLevers CaptureSceneHook `0x742C80`, BeginBackBufferHook `0x773D30`; UiWorldFigure's Umbra skip `0x7526C0`) | 2 | **missing**: the rows are in ME2's table (`0x329B90`, `0x354570`; lane R2), the hooks stand down (`sceneCaptureVerified`: where ME2's final pass gets its viewport is unread), and nothing in ME2 makes a capture | **forked** V: `plugin/Le3SceneCapture` (its own RVAs; shares `plugin/SceneCaptureTarget.hpp`, `core/vr/SceneCaptureScope`); raw map captures in run `20261115-220823-943` (research/le3-galaxy.md) | Forks 9. |
| R32 | Pinned renderer baseline (LE1 `MevrBridge::ModernBaseline`: stereo, flat menus, first person, no head aim) | per renderer | **adapted** V: `ApplyMevrBaseline` (le2 me2_menu.cpp) after every ini load; first run 2026-11-14 (research/le2-renderer-integration) | **adapted** U: compile-time `#if !defined(MEVR_ASI)` disables in le3 me2_menu.cpp | Per renderer by nature. |
| R33 | Hotkeys only with the game in front (`MevrBridge::KeyDown`) | 1 | **adapted** U: `MevrBridge::HotkeyDown` (le2 mevr_bridge.h) | **adapted** U: `KeyDown` in le3 d3d_capture.cpp | Type the key in another window during a run; nothing should toggle. |
| R34 | Halcyon's script calls queued to the game thread (seam `QueueProcessEvent`, `plugin/GameThreadCalls`; research/36) | 3 (Halcyon's Present-thread ProcessEvents; the sequels have the same pattern) | **shared** V: "stereo and the queue work", first run 2026-11-14 (CLAUDE.md) | **shared** U: le3 engine_probe.cpp:282 queues | LE3: the host's `process event: off the game thread:` count in a run log. |
| R35 | Performance modes and the first start's eye size (`core/vr/PerformanceModes`, `MainMenuSettings` Display > Performance, `FirstStartEyeSize`) | menu | **missing**: `FirstStartEyeSize` is empty off LE1 (`#if !defined(MEVR_MENU_LE1)`), and the levers it bundles don't exist there | **missing** | After R08 and R13. |

### Renderer techniques by group

**Group 1 (already game-independent): bring the call site, not the code.**
- The draw recorder and replay (`plugin/DrawReplay.cpp`: d3d11 vtable slots, no engine address).
- Its screen-space matrix rewrite (`core/vr/ScreenMatrix`, `core/d3d11/ShaderReflect`) and buffer-wrap shadows.
- `game/SystemSettings` (console commands through reflection).
- FSR (`core/vr/Upscale`), colour per headset (`core/vr/Colour`), the HDR tonemap (shader text).
- The HDR display answer and EyeCopy (DXGI/D3D11 only).
- The swapchain budget, the HUD panel core, the hand reticle, the overlay modules, the cinema screen core, backdrop and fade.
- Per-eye pose tags (`core/vr/StereoPoseTag.hpp`).
- The test loop core and `core/vr/FrameProbe`, Quest pacing's core (OpenXR extensions), recenter tilt (`core/vr/Recenter`).
- The spectator's host draw, and the scope capture source's texture search (`ScopeCaptureSource`: UTexture.Resource +0xA0 Ã¢â€ â€™ +0x8).

**Group 2 (per-game hook, address or seam).**
- One-Draw stereo (RenderLevers: `0x76EE20`, `0x76DD10`, `0x77BE00`, `0x4ED700`; views field and clone stride) and the
  replay's tail and finish boundaries. The address table is in place: `plugin/RenderLeversTable.hpp` finds each row of
  `engine/Addresses.hpp` by LE1's row name in the current game's table.
- The per-eye frustum (a CalcSceneView seam; the view offsets match in all three trees).
- The Umbra levers (`0x8B6550`, `0x8C34C0`, `0x9A04B0`, view state +0xD10) and `minScreenRadiusPx` (`0x8BE0B0`).
- `keepBoundShaderStates` (`0x9A89F0`, GDynamicRHI `0x17938C8` slot 0x8A0).
- The motion-blur guard (a per-game proxy-pair table; done for LE3).
- Metric stereo (a seam in the SFR branch).
- Frame pacing (a seam at xrWaitFrame, which depends on the RunFrame order).
- The perf probe's seams; the HUD's UI-draw classification and crops.
- Body bounds (`0x58EEC0`, `0x25CFE0`); the scene-capture hooks (`0x742C80`, `0x773D30`, `0x7526C0`; LE3 has its own,
  `plugin/Le3SceneCapture`).

**Group 3 (Halcyon ME1's bugs or our LE1 workarounds): port only if the bug shows.**
- **The presentation hold.** Halcyon ME1's unreadable mode byte, logos movie and title camera-flight cine latch. The
  sequels classify views their own way; whether they flip is not determined.
- **`LightEnvironmentShadows False`.** Our replay's workaround; on the sequels only if one ever replays an eye (R11,
  fixed 2026-11-16).
- **The body-bounds padding.** Caused by Halcyon LE1's GPU-only view offset of 0.43 m back.
- **The motion-blur guard's LE1 reason.** Settings paths re-enabled the blur. LE3 needed the guard for a different crash.
- **The HUD panel's tint-blend path.** Scaleform's non-"over" blends stayed in the eyes (the doubled wheel).
- **The game-thread queue.** Halcyon's Present-thread ProcessEvents. This one did show in the sequels: the seam is in all
  three (R34).
- **The `MELEVR.ini` save safety and ImGui's `HasGamepad` flag.** Halcyon LE1's own; the sequels' menus are upstream's.

## Gameplay and UI

| id | system (LE1 code) | LE2 | LE3 | check / next step |
|---|---|---|---|---|
| G01 | VRIK arm IK (`game/ArmIk`, `engine/BonePose`, `core/ik`; docs/vrik-porting-guide.md) | **adapted** V: LE2 `kUpdateSkelPose 0x79B8A0`; research/le2-vrik.md "The run" 2026-11-14 (141fd1d5, 965321e2): wrists 0.07 m off, skeleton audit within 3 cm. Root-motion steps (shared, `core/ik/GoalStep`): the goals follow the `Camera` bone; three vaults with every tick within 0.08 m of the controllers (0.70-1.04 m before; stretch `20261115-010524-lane-c2-vault`, research/le2-vrik.md) | **adapted** V: LE3 layout, `kUpdateSkelPose 0x796EB0`, `MenuBodyEligible`; runs `20261115-182253-344`, `20261115-183117-290` (research/le3-body-pose.md). The root-motion rule is the shared one, U on ME3 | Headset: hands on the controllers (snapshot `mod.armIk.hands`). |
| G02 | Hand aim and the aim ray (`game/HandAim`) | **adapted** V: `SFXWeapon.GetAdjustedAim` post-handler; 34 shots 0.4Ã¢â‚¬â€œ0.8Ã‚Â° from the barrel (le2-vrik) | **adapted** U: LE3 branches, on our layer only (no LExSDKv2 fallback on either sequel since T04) | LE3: a combat fixture (research/le3-combat-fixtures.md). |
| G03 | Fingers (`game/Fingers`, `core/ik/Fingers`) | **shared** U | **shared** U | Snapshot `mod.fingers` with a replayed trigger and grip on each game's skeleton. |
| G04 | The drawn gun (`game/Weapon`) | **adapted** V: muzzle against `GetMuzzleLoc`, 0.00 units (le2-vrik) | **adapted** U: LE3 weapon classes, the SMG as its own `RangedType` | LE3: a combat fixture. |
| G05 | Support grip, two-handed (`game/SupportGrip`, `plugin/PadMerge` v7/v11) | **shared** U: ctest `mevr-pad-merge` only | **shared** U | A replay with the left grip in the zone, then snapshot `supportGrip`. |
| G06 | Sights: red dot, holo, ADS, alignment assist (`game/Sights`, `plugin/SightLayers`, `core/vr/Ads`) | **shared** V: one path with ME1 since lane C2's step 2 (the sight line's trace, convergence, the assist, a scope's aimed fire through `SFXWeapon.GetAimError` with `SFXWeapon_NativeBase.bIsZoomed` set for the call; `SFXWeapon.ZoomFOV`). Run on the fake headset 2026-11-16 (the pistol's dot in the right eye, shots on what the sight line hits; the sniper rifle's ADS, scope picture and aimed fire 10.0 -> 0.0): research/le2-vrik.md. The headset is the checklist's | **shared** U: the same path (`SFXWeapon.GetPlayerAimError(Accuracy)`); zoom steps (`AimModes`) not read; the SMG borrows the rifle profile | ME2: `tools/le2-halcyon-run/sights.ps1` (run 2026-11-16, LE1 at the same pose unchanged). ME3: a weapon fixture, then the same script's steps. research/le2-le3-vr-optics.md steps 1Ã¢â‚¬â€œ3. |
| G07 | Scope: picture-in-picture, virtual eyepiece, scope parts, scope capture (`game/ScopeParts`, `game/ScopeCapture`, `plugin/ScopeCaptureSource`) | **adapted** U: picture from the eye copy. `ScopeParts` shared (HideBone on every game; U); `ScopeCapture` is `out`; ScopeCaptureSource is linked but `setScopeCapture` isn't served | **adapted** U: same | A sniper-rifle fixture per game. Then ScopeCapture (it needs R31 on LE2). |
| G08 | Holsters and "only holsters draw" (`game/Holsters`, `core/ik/BodySlots`) | **adapted** V: one module, ME2's names in its adapter (the inventory chain by family, `SwitchWeapon` + `SetActionState(1)`, `TryHolster`, `ActionState`); only holsters draw by one rule (`RefuseGameDraw`: `IsCombatEnabled` answered false inside FireWeapon, TightAim, TryReload, Next/PrevWeapon). Run 2026-11-16: the rifle at the shoulder and the pistol at the hip drawn and holstered, four trigger pulls with the weapon away refused, the right grip holding the gun through three vaults (research/le2-vrik.md "Holsters on the sequels"); ME1 at the same pose unchanged. Since 2026-11-16 (lane WP): the release grace seen (back after 0.20 s, holstered after 0.93 s); the SMG's slot `thighRight` (a fifth `BodySlots` slot, off on ME1; not run: Save 4's Soldier carries no SMG) | **adapted** U: no holstered state on a mission (its script): TryHolster never called (it only leaves cover); the holster is the game's put-down into no weapon (`SFXInventoryManager.SetCurrentWeapon(None)`) behind `[Holsters] le3PutAway`, 0 by default; only holsters draw is ME2's rule with "away" = weapons carried, none equipped; Save 31 run: names resolve (`reflect: 710 of 710`), the non-combat pawn carries nothing, nothing drawn or refused | LE2: the SMG with a class that carries one. LE3: a combat fixture (none on the machine: research/le3-combat-fixtures.md), then `holsters.ps1 -Game LE3` with `le3PutAway = 1`, then its default. |
| G09 | Gun kick (`game/GunKick`, `core/ik/GunKick`) | **adapted** V: recoil from `SFXWeapon.GetWeaponRecoil`; `gun kick: first kick` (le2-vrik) | **adapted** U: ME2's branch (`SFXWeapon.GetWeaponRecoil`, the shots from ViewLevel's `SetWeaponRecoil` handler) since 2026-11-16; builds, ReflectCheck rows (`CheckLe3ViewLevel`); not run in ME3 | LE3: a combat fixture with shots (research/le3-combat-fixtures.md): `reflect:` agrees on the new rows, `view level: first shot seen`, `gun kick: first kick`. |
| G10 | View level, aim assist zeroed (`game/ViewLevel`) | **adapted** V: pitch held in 4,723 of 4,723 ticks, yaw kick removed (le2-vrik); the aim-assist zeroing unmeasured | **adapted** U: the same handlers as ME1 and ME2 since 2026-11-16 (PlayerTick, `SFXInventoryManager.SetWeaponRecoil`, aim assist's `ViewFriction`/`ViewAdhesion`), ME2's yaw-kick removal; ME3 has all 29 names; builds, ReflectCheck rows; not run in ME3 | LE3: the same fixture: `view level: handlers on ...`, the pitch held while shooting (the heartbeat's `view level:` line), whether ME3 queues a yaw kick, and which PlayerTick functions it found (none: it levels a frame late from the mod's tick). |
| G11 | Camera shakes off (`game/CameraShake`) | **adapted** V: ME2's branch sets `CameraModifier.bDisabled`; modifiers off in the run. Which gun shakes exist is [verify] | **adapted** U: ME2's branch since 2026-11-16 (`SFXPlayerCamera.ScreenShakeModifier`, `Camera.CameraShakeCamMod`, `SFXCameraMode.bIsCameraShakeEnabled`: all in ME3's SDK); builds, ReflectCheck rows; not run in ME3 | LE3: `camera shake: ... off in VR` names each modifier it found; `mevr shake` in a combat fixture. |
| G12 | Camera collision off in first person (`game/CameraCollision`) | **missing** | **missing** | First check whether Halcyon ME2/ME3's first person pulls back near geometry. LE1's symptom was the eye behind the knees (research/69). |
| G13 | Body follow: torso, legs keep the run, steady idle (`game/BodyFollow`, `core/ik/BodyFollow`) | **adapted** V: skeleton audit (le2-vrik). Conversation `BodyAway` is LE1-only | **adapted** V: shared follower plus a head-only solve on grip loss, run `20261115-182253-344`. "Legs keep the run" has its Velocity read since T04 (U: built; the handle's ReflectCheck row is L3-3's) | LE3: a walk under replay showing `torso: legs:`. |
| G14 | Body scale (`game/BodyScale`, `core/ik/BodyScale`) | **shared** U | **shared** U | Needs a recorded floor fixture: the fake headset's floor is at its head. |
| G15 | Floor lock (`plugin/FloorLock`, `core/vr/FloorLock`, seam `FloorViewTranslation`) | **adapted** V: the correction measured on Save 4 (`final eye 1.7526 m` = the STAGE eye, correction +12.26) and held through the vault's root-motion steps (shared `vr::floor_lock::MoveHold`, the mod's `traversalMove`): three vaults with every tick's wrists within 0.15 m, no third-person frame (research/le2-traversal.md "The floor lock through the vault", 2026-11-16); manual height not wired | **adapted** V: `stageEyeM = finalEyeM = 1.7526`, run `20261115-170957-963`; menu continuity `20261115-183117-290` (research/null-headset-height.md) | LE2: the calibration's manual height once wired; the headset. LE3: a traversal move under the hold. |
| G16 | Guided calibration and hand calibration (`plugin/Calibration`, `CalibrationDraw`, `ArmSample`, `game/BodyCalibration`, `game/HandCalibration`) | **adapted** U: `MEVR_SEQUEL_MENU_SOURCES`, `WithMenus` v15, glue in `plugin/MenusLe2` | **adapted** U: same | A headset run: the fake headset has no interaction profile. |
| G17 | Comfort: snap/smooth turn, vignette, walk toward the hand (`game/Comfort`, `plugin/Vignette`) | **shared** U: the comfort run timed out; the vignette layer path was seen (`20261114-212412`) | **adapted** U: `NativeLocomotionAllowed` over `player::MovementRefused` (ME3's one admission read, Forks 11) | docs/le2-m6-native-checks.md comfort row: two right-stick flicks and a hand-yawed walk. |
| G18 | Weapon drawn like exploring: explore stance, constant eye, linear walk and stick, draw rate (`game/CombatStance`, `core/vr/CombatStance`, seam `ConstantFpEye`) | **missing**: `out`; ME2 lacks `BioPawnBehavior`'s ramp fields (research/le2-vrik-audit.md) | **missing** | Port per item. The linear walk and stick is the one players notice (research/38). Decide per game what stands in for the ramp fields. |
| G19 | Our main menu (`plugin/MainMenu`, `MainMenuSettings`, `src/plugin/menu`, `assets/menu`) | **shared** V: run `20261114-222813-lane-d-menu` (f9809ed3); unported rows greyed | **shared** V: menu open and resume, run `20261115-183117-290` | notes/headset-checklist.md Ã‚Â§5. |
| G20 | Menu pause (`game/MenuPause`) | **adapted** V: `WorldInfo.bPlayersOnly` (same run) | **adapted** U: `BioWorldInfo.PauseGame` through `UnambiguousMethod` (research/le3-menu-pause.md) | Ã¢â‚¬â€ |
| G21 | Game menu and Holster buttons (`game/GameMenuRequest`) | **adapted** V: Game menu opened the Mission Computer; Holster not exercised | **adapted** V (pages): squad, journal, map and Close checked on Save 31 (lane OM, 2026-11-16: ME3's screens act on B's release, so every back-out sends B and its release); Holster missing (`#if !LE3`, waits for lane WP's ME3 holster call) | Ã¢â‚¬â€ |
| G22 | Omni-tool rail, wrist tap, gauntlet (`plugin/OmniToolRail`, `RailArt`, `game/OmniTool`, `game/GameArt`) | **adapted** V: run `20261114-234544-lane-d2-rail` (1a8d3b39). `GameArt` LE1-only (ME1's art cache, recoloured) | **adapted** V: rail shared, in ME3's Mission Computer colours (theme `me3`, never ME1's art); the gauntlet is ME3's own client effect (`RvrClientEffectManager.Start` of `OmniTool_LeftFull_VCFX_M`), seen on Shepard's forearm on Save 31 (lane OM, 2026-11-16). `GameArt` stays LE1-only: ME3's frames and tabs are bitmap fills and its font library isn't a live object | LE3: research/le3-native-gui "The omni-tool on ME3". |
| G23 | Game screens in the room and on the wrist; the UI-world figure (`game/WorldUi`, `plugin/WorldUi`, `game/UiWorldFigure`) | **adapted** V: `SetMovieVisibility`; runs `20261115-043419-wrist-visual` and the Save 4 pointer run (827cd3af). UiWorldFigure missing | **missing**: no `setWorldUi`. The whole native GUI frame goes on a `WorldPanels` quad (`PlaceNativePanel`) | LE3: research/le3-native-gui gates 1Ã¢â‚¬â€œ4. |
| G24 | Omni pad for the bypass minigame (`plugin/OmniPad`, `core/vr/OmniPad`) | **missing**: ME2's hack and bypass minigames are different games (docs/le2-validation-coverage.md "Hacking and bypass") | **n/a**: ME3 has no bypass minigame (game knowledge, not checked in its data) | Ã¢â‚¬â€ |
| G25 | Galaxy map: hologram, stereo window, projector (`game/GalaxyMap`, `plugin/GalaxyHologram`, `game/SceneCaptureRig`) | **forked** U: `HalcyonLe2Bridge::PlaceGalaxyPanel` + `vr::galaxy_map::NativeTablePanel`, opt-in `[Galaxy] nativeTable = 1`. GalaxyMap and GalaxyHologram aren't on LE2 | **forked** V: `Le3GalaxyCapture` (re-implements GalaxyMap's `StepWindow`), `PlaceNativePanel` (a copy of LE2's), `Le3GalaxyRetention`, `NativeMapInputLease`. Navigation run `20261115-194205-273`, retention `20261115-214310-005`, raw eye captures `20261115-220823-943`; the post-effect fix never ran (`20261115-221710-284` timed out) | docs/galaxy-projector-handoff.md "Remaining work" 1Ã¢â‚¬â€œ5; Forks 3. |
| G26 | Game pointer and GUI events (`plugin/GamePointer`, `game/GuiCursor`, `core/vr/GuiEvents`) | **adapted** V: the Save 4 pointer run, 2026-11-15 (research/le2-menus.md "Game screens and pointer") | **adapted** V: pointer on the native GUI quad; `takeGuiEvents` served on LE3 (`WithMenus`): a replayed ray click on the Mission Computer's Squad went in as `SFXGameModeGUI.GuiKey(31)` and opened the squad record, no SendInput (lane OM, Save 31, 2026-11-16) | LE3: on the wrist with G23. |
| G27 | Interaction: selection by gaze, label at the object (`engine/SelectionGaze`, `game/Interact`, `HudPieceLayers::Interaction`) | **adapted** V: gaze at `0xAC0530` (ME3's void(this) search); a band per game (`hp::DefaultLabelCrop` `Me2`), on by default; Save 4: the door selected ahead, nothing at 60 degrees, its label (research/40 section 6, lane HU) | **adapted** V: gaze at `0xA9BF90`; run `20261115-190610-153` (A opens War Assets); band `Me3`; Save 31: War Terminal 1.35 m, the label at the console, nothing at 70 degrees (research/40 section 6) | Both: A's use after a gaze pick in the same run, a long name, combat targets, the headset. |
| G28 | HUD pieces: wrist status, gun panel (`game/HudPieces`, `plugin/HudPieceLayers`) | **adapted** V: `Layout::Me2` crops; run `20261115-002101-lane-b4-m2b` (f71e7a71); ammo mount 2a108759 | **missing**: `setHudPieces` not served (`#if !defined(MEVR_WITH_HALCYON_LE3)` in ModLoader.cpp); `game/HudPieces` stands down by name on ME3 (no fall-back to ME1's placement) | LE3: docs/me3-overall-goal-handoff.md L3-4 "native wrist crop". |
| G29 | Power wheel at the left hand, lock and push, self-cast gestures, coach (`game/PowerWheel`, `plugin/PowerWheel`) | **adapted** V: ME2 roster (`SFXPowerManager`); chest Adrenaline Rush and rifle Disruptor Ammo, 2026-11-15 05:25 (483ac2fb; research/55 "Mass Effect 2") | **adapted** U: `game/Le3Powers.hpp` (`SFXPowerCustomActionBase`); metadata only | research/55 "Still to verify"; L3-6. |
| G30 | Squad weapons wheel (`game/SquadWeapons`, `plugin/SquadPanel`) | **adapted** U: ME1's menu tick (`TickCustom`: gates, pointing, cards by standing side, haptics, logs) over ME2's `native` adapter (`game/Le2SquadMenu.inc`: a paged inventory, `OrderWeaponSwitch`, `Pawn.Weapon`); custom style only. **V on ME2 for one tile** (lane GF2, session `20261116-153741-665-LE2-gf2-run2`, Freedom's Progress, Save 4): six replayed pulls with a flick down, `squad: menu #1 opened ... cards: left Jacob, right Miranda`, `menu #1 confirmed (trigger let go): Miranda gets the pistol`, `Miranda: requested weapon observed equipped` (SMG to pistol), 6 of 6. The names are ME2's own since then (`SFXPawn.GetPrettyName` through the game info's `GetSimpleString`, `game/Le2Squad.hpp` `ActorName`; the behaviour's `ActorGameName` is the class default "BioPawn"). Not shown: Jacob, a second tile, pointing at a squadmate, the page tile. The fake headset's view and controllers drop out under machine load (91 stand-down lines in one run, 182 in the next, when a pull's menu closed before the flick) | **adapted** U: the same adapter with ME3's roster and input-method parameter | A replayed left-trigger pull with two squadmates (ME2: Freedom's Progress): `squad: menu #1 opened ... cards: left .., right ..`, a right-stick flick, the release, then `requested weapon observed equipped`. |
| G31 | Squad context and gesture orders (`game/SquadContext`, `game/SquadOrders`, `core/vr/Squad*`) | **adapted** U: `game/Le2Squad.hpp` (`HenchmanOrderAttackTarget`, `SquadOrderUsePower`) | **adapted** U | M6 C; L3-6. |
| G32 | Squad placement by an arc (`game/SquadPlace`, `plugin/SquadPlaceDraw`) | **adapted** U: opt-in `[SquadOrders] place = 1` | **adapted** U | M6 table, squad roster and placement row: Miranda and Jacob on Freedom's Progress. |
| G33 | Voice orders, the model tier, the Voice page (`game/Voice`, `plugin/Voice`, `plugin/VoiceModel`) | **shared** U: the protected voice run timed out | **shared** U: the model tier compiles into LE3 wherever its host compiles `plugin/VoiceModel.cpp` (until 2026-11-16 only when LE2 was built too) | A microphone run, or `voice.request wav`, per game. |
| G34 | Squad acknowledgement (`game/SquadAck`) | **missing**: `ready` | **missing**: `ready` | Port with G31. `SquadHandleChatterEvent` and `PlaySoundSet` are in neither sequel's SDK list (generated names). |
| G35 | Thespian mode (`game/Thespian`, `plugin/Thespian`, `core/voice/ScriptMatch`) | **adapted** U: replies by native slot; no mute or `SkipNode` on ME2 (research/70 "ME2 cinema adapter") | **adapted** V: reader on three native wheels, run `20261115-203330-839`. Spoken selection unverified | M6 E; L3-4. |
| G36 | Conversations in the world: flow, moments, keep-in-place (`game/ConversationWorld`, `ConversationFlow`, `AuthoredMoment`, `engine/ConversationHooks`, `plugin/ConversationWorld`) | **missing**: deferred by the owner 2026-11-15; `ready` | **missing**: deferred; `convo_fp` hard-off | M6 F (deferred); LE3 plan "remain deferred". |
| G37 | Two-ear audio: OpenAL Soft with HRTF (`plugin/OpenAlSoft`) | **n/a**: ME2 plays through Wwise, not ISACT/OpenAL (research/69's audio note; [verify] the exe's imports) | **n/a**: same | Check `MassEffect2.exe`/`MassEffect3.exe` imports for OpenAL32 before closing the row. |
| G38 | Sound placement: voices, people near, the gun (`game/SoundPlace`, `engine/VoicePlace` on ISACT `0x7BA90`) | **missing**: `ready`; `kSoundSourceUpdate = 0` | **missing** | Wwise needs a placement hook of its own. |
| G39 | No cover snapping, VR cover (`engine/CoverAcquire`, `game/VrCover`, `core/vr/VrCover`) | **adapted** V: `FindPlayerCover` post-handler; VrCover on `SFXGame.ModifyDamage`; research/23 §7.4, 2026-11-14 (61705db8). The shields' faster recharge behind cover (ME2's own rule, the owner's yes 2026-11-16): `SFXShield_Base.Tick`'s two questions answered while the head is hidden from every awake hostile (`vr::cover::Shelter`), V on the fake headset: the 5.00 s wait 3.51 s behind cover, 5.02 s with the rule off or in the open; research/23 §7.5 | **adapted** U: `game/Le3Cover.inc` (ModifyDamage on SFXPawn_PlayerParty); no-snap only as a diagnostic opt-in. ME3 (and ME1) have no faster recharge in cover: n/a | M6 table, cover/crouch/sprint row; L3-8. |
| G40 | Physical crouch (`game/Posture`, `core/ik/Posture`) | **adapted** V: `Engine.Pawn.ShouldCrouch`; research/23 §7.4 step 1 | **adapted** U: LE3 native admission | — |
| G41 | Vent, weapon heat (`game/Vent`, `core/ik/Vent`); the sequels' counterpart, the reload by hand (`game/Reload`, `core/vr/ReloadGesture`, `PhysicalReloadRequest`) | **n/a** for the vent (thermal clips); the reload by hand **adapted** V: the pouch a left-hand body slot, the vent's receiver zone, the coordinator, the button's own `SFXGameModeDefault.TryReload`; run 2026-11-16 (lane WP): 12/0/60 -> one shot 11/1/60 -> the replayed gesture 12/0/59, a full magazine refused (research/le2-vrik.md "The reload by hand: the game's side") | **n/a** for the vent; the reload **adapted** U: the same module, ME3's ammo fields on `SFXWeapon_NativeBase`; names resolve on Save 31 (no gun there) | LE2: empty, no-reserve and cancelled reloads. LE3: a combat fixture, then `reload.ps1 -Game LE3`. |
| G42 | Physical grenades (`game/Throwables`, `core/ik/Throwing`) | **n/a**: ME2 has no grenades | **n/a**: ME3's grenades are powers (research/le3-powers.md) | — |
| G43 | Melee gesture (`game/Throwables` `RouteMelee`, the fist) | **missing** | **forked** U: `game/Melee` (LE3-only) on `core/ik/Throwing`, a second gesture-to-dispatch path; opt-in | Forks 12; research/le3-melee-audit.md; L3-7. |
| G44 | The Mako: aim, controls, cockpit, gunner, panel, hazard (`game/Mako*`, `game/Cockpit*`, `core/vr/MakoControls`) | **missing**: the Hammerhead, with ME1's Mako controls and cockpit, has its own later pass (continuation plan M6) | **n/a**: no player vehicle; the Atlas has no adapter (LE3 plan: "safe fallback") | Ã¢â‚¬â€ |
| G45 | Focus pause off (`game/FocusPause`) | **missing** | **missing** | First check whether ME2/ME3 freeze without window focus (`bPlayersOnly`). |
| G46 | Our ASI loader (`src/loader`) | **adapted** U: `LoaderCore.cpp` maps MassEffect2.exe, but every ME2 run used LEBinkProxy | **adapted** U: maps MassEffect3.exe | A run with our `bink2w64.dll` in that game's Binaries. |
| G47 | Installer, report a problem, release packaging (`MEVR-Setup`, `src/install`, `plugin/ReportRequest`, `tools/package-release.ps1`) | **adapted** U: one installer, the game a parameter (`install/Game.hpp`'s `GameSpec`: folder, exe, `-game 2`, `MELE2VR.ini` next to the exe, its GamerSettings keys; its own `ASI\MEVR-install.json`); install, "verify files" and Repair, the settings and their Restore, the report (`LE2MEVR.log`, `MELE2VR_Log.txt`), uninstall byte for byte pass on the fake game (`tools/setup/test-fake-game.ps1 -Games LE2`, 2026-11-16) and in `mevr-tests` (`TestGames`); `ReportRequest` in the host. Never installed into the real game; **not offered** (MEVR-Setup.ini `[Setup] games`, `package-release.ps1 -Games`); the players' build links no LExSDKv2 since T04 and the package refuses one that does; our loader in the launcher starts only ME1 (the picker shows) | **adapted** U: the same by the same code (`-Games LE3`); no release `MELE3VR.ini` (the renderer writes its own); its players' build links no LExSDKv2 since T04 | One install, repair and uninstall of each on the real game; T51 (the launcher's `-game 2`/`3`: `launch::GameArguments`, `kOurLauncherBoots`); the owner's word to offer one (`-Games`). M7.1, M7.2; L3-10. |
| G48 | Crash breadcrumbs, first-chance report, quit safety (`plugin/CrashBreadcrumbs`, `plugin/KeptLocks`, the ExitProcess hook) | **adapted** V: breadcrumbs in every host; a first-chance report in an ME2 log (research/le2-menus "The checks"). KeptLocks LE1-only | **adapted** U: same | Check whether ME2/ME3 fault at quit (LE1: a deleted lock at exe+0x1BA22C0). |
| G49 | Controller input: Touch as pad 0, profiles, our actions, haptics (`plugin/PadMerge`, `game/GamepadMapping`, `plugin/XrHost`, `core/xr/Input`) | **adapted** V: `MergeMotionControllers` (conversation and orbital triggers, R3 gate); stretch `20261115-001659-lane-b4-m2`; ctest `mevr-pad-merge` | **adapted** V: semantic modes, map lease, traversal yaw; replayed controllers in run `20261115-182253-344` | Headset: the L3+R3 hold. |
| G50 | Head tracking and the view anchor (`game/HeadTracking`, `game/ViewAnchor`) | **adapted** V: `hostView.valid` and the replayed turn (le2-vrik). Stands down when the layer is off (no LExSDKv2 fallback since T04) | **adapted** V: head look in the pose-tag runs (through `engine::legacy` then). On our layer since T04: its rows agree in the engine probe (`CheckLe3EngineAccess`, 621 of 621), not yet seen in a renderer run | LE3: a renderer run on the T04 code. |
| G51 | Reflection (mirror) fix (`game/Reflections`) | **n/a**: stands down whenever the host owns VR (`src/mod/Mod.cpp`), as on LE1 under Halcyon | **n/a** | Ã¢â‚¬â€ |
| G52 | Engine access: reflection layer, ReflectCheck, addresses, the game adapter (`src/ue3`, `engine/ReflectCheck`, `engine/Addresses.hpp`, `engine/GameAdapter.hpp`) | **adapted** V: `reflect: 637 of 637`, session `20261116-125123-331-LE2-t04`; a monolithic build to gameplay without LExSDKv2 | **adapted** V: 621 of 621, session `20261116-125541-116-LE3-t04` | T04 closed the LExSDKv2 part (research/34 section 6.4). `kModulesOnLayer` stays false on both: the 10 `ready` modules left (BodyBounds, CoverAcquire, VoicePlace, SoundPlace, AuthoredMoment, ConversationFlow, ConversationWorld, GalaxyDump, GalaxyMap, SquadAck) are ME1 features on ME1's names and addresses; each moves to `kEveryGame` and `Up()` with its own feature's port. |
| G53 | Host/mod split, hot reload, HostServices (`plugin/Entry`, `plugin/ModLoader`, `src/mod`) | **shared** V: every LE2 run above used a host and mod pair | **shared** V: the protected LE3 runs | Ã¢â‚¬â€ |
| G54 | Save tools and scripted runs (`tools/le1-save.py`, `fake-headset-session.ps1`, `launch-game.ps1`, the conversation tools) | **forked** V: `tools/le2-save.py` (recipes and receipts), `tools/le2-halcyon-run` (`load.ps1`, 3e8609b7), `tools/qa-le2`, a diagnostic LE2 host | **forked** V: `tools/le3-save.py` (imports le2-save), le2-halcyon-run's LE3 mode; 6 saves round-trip byte for byte | Forks 10. |
| G55 | Console request, call trace, dev request files (`game/ConsoleRequest`, `game/CallTrace`, `core/DevChannels`) | **adapted** V: `mevr load` and `mevr omni squad` in runs. CallTrace runs (`Up()`; V: `mevr calltrace 2 updatecamera`, 144 lines, T04's session) | **adapted** V: `mevr thespian cards`, run `20261115-203330-839`. CallTrace as ME2: U | LE3: one `mevr calltrace` in a run. |
| G56 | Classic menu's VR hands tab and menu pointer (`plugin/MevrMenuTab`, `plugin/MevrMenuPointer`) | **missing**: Halcyon ME2's own menu, without our tab | **missing** | Low value: our main menu replaces it. |
| G57 | The game's wheels cut onto a hand quad (`core/vr/WheelCrop`, `setWheelPanel`, the native squad-wheel style) | **missing**: `setWheelPanel` is LE1-only | **missing** | With G30. |
| G58 | VR sprint (`game/Sprint`, `core/vr/Sprint`) | **shared** V: one module and rule for the three (lane GR, 2026-11-16, the owner's "Unified sprint"): `SFXGameModeDefault.StormOn` runs (the binding's hold reaches it through ProcessEvent), its intent `BioPlayerController.bWantsToStorm` is withdrawn by both hooks' post-handlers, the faster run raises `GroundSpeed`/`CombatGroundSpeed` by `StormSpeed` over `Pawn.GetMaxSpeed`; ME2's adapter: not in the game's own cover. No ME2 fact forces the native Storm. Run on the fake headset (Save 4): withdrawn, x1.31, peak 525 u/s, the run 400 u/s; research/trilogy-sprint.md. ME1 too (the old skip never fired since 11-06) | **shared** V: the same, with ME3's native admission (`player::MovementRefused`, `IsStormDisabled`, the fresh-owner check); Save 31: withdrawn, x1.12, peak 450 u/s | The sprint's heading and feel in a headset on each game; ME3 off the Normandy; a sprint with a weapon drawn. |

### Sequel-only systems

These are not ME1 systems, so the [totals](#totals) don't count them. Their form still matters for unification.

| sequel-only | LE2 | LE3 | notes |
|---|---|---|---|
| Traversal, vault (`game/Traversal`, `plugin/TraversalCue`, `core/vr/Traversal`; ME1 has no vault) | origin V: research/le2-traversal.md "Measured (fake headset, 2026-11-14)" | forked U: `game/Le3Traversal.cpp` | Forks 4. |
| The float frame into the eyes (`plugin/EyeCopy`) | V (R16) | V (R16) | Halcyon ME2/ME3 assumed an 8-bit swap chain. Since 2026-11-16 the same module also upscales (R13) and tonemaps (R15). |
| CIC residency during the map (`game/Le3GalaxyRetention`) | Ã¢â‚¬â€ | V: 541/541, run `20261115-214310-005` | ME1's map doesn't unload the CIC. |
| Native GUI accessors (`game/Le3Gui`) | Ã¢â‚¬â€ | adapter | ME3's GUI is SFXGUIMovie, not BioSFManager. |
| Diagnostic host (`MEVR_LE2_DIAGNOSTIC`: `engine/Le2DiagnosticHost`, `Le2ControlHost`, `core/qa`) | test-only host | Ã¢â‚¬â€ | Forks 10. |

## Forks

Each item: the copies, then what unifying would take. Line counts are as of `ccacd667`.

1. **Overlay composition.**
   - Copies: LE1 `plugin/HalcyonOverlays.cpp` `OverlayLayers` (and its own label quad, `gLabelSwapchain`/`LabelWanted`);
     the sequels' `plugin/SequelOverlays.cpp` `OverlayLayers` (one for LE2 and LE3 since lane U2, 2026-11-16; before it
     one in each sequel bridge), with `SequelWeaponLayers.cpp`, `SequelSquadLayers.cpp`, `SequelCinema.cpp` and
     `MenusLe2.cpp`'s `PanelLayers`. research/trilogy-seams.md "One overlay composition": its order, the capabilities it
     reads, each game's adapter, and the five things LE1 needs before it can call it.
   - The frame is one type since lane U1 (`MevrSeams::OverlayFrame`, `src/halcyon/MevrSeams.h`: the sequels' fields and
     LE1's), and all three composers are `MevrSeams::OverlayLayers`, one definition per host.
   - **To unify the rest:** move LE1's layers into `SequelOverlays.cpp` (then `plugin/Overlays`) one at a time, each
     behind a capability and with an LE1 same-pose pair: the HUD panel's inputs, the game's wheel quad, LE1's label quad
     as `HudPieceLayers::Interaction` with ME1's default crop, the LE1-only layers, the menus' glue (Forks 13).
2. **The sequel bridges.**
   - **Merged by lane U2 (2026-11-16):** `plugin/HalcyonSequelBridge.cpp` defines the session, frame, view, recenter,
     cinema-screen, HUD-draw and controller-merge seams and the galaxy settings once; each game implements the adapter in
     `plugin/HalcyonSequelBridge.hpp` (`sequel_bridge::game`: `Facts` and hooks) in `HalcyonLe2Bridge.cpp` (408 lines)
     and `HalcyonLe3Bridge.cpp` (389 lines). research/trilogy-seams.md "One sequel bridge" lists the adapter per game and
     every behaviour the merge changed.
   - **ME3 as ME1 (lane U3, 2026-11-16, the owner: "everything consistent"):** the adapter's `menusHideGameplay`,
     `weaponsNeedPublishedAim` and `cueOnFootOnly` are gone (none was forced by ME3's game; the ring takes LE1's
     `ControllerAimActive` on both sequels), and ME3's recenter drops nothing published: one recenter rule for all three,
     each adapter resetting only its own state. research/trilogy-seams.md "ME3 as ME1", "One recenter rule"; run on ME3
     (renderer preview, Save 31) and ME2 (Save 4) with replayed hands, a recenter and our menu ("Lane U3's checks").
   - Still two: `PlacePanel` (ME2 places only the table, ME3 every native panel: needs ME2's renderer to take
     `panelsInRoom`), the native map's input (pointer and latch on ME2, a lease on ME3: Forks 3), and the eye-image
     seams (lane EY).
   - **Next:** the same shared file for LE1's bridge, seam by seam (`OnViewBuilt` and `OnRecentered` are closest).
3. **The galaxy map.**
   - ME1: `game/GalaxyMap.cpp` (`StepWindow`, the hologram, the view hold) and `plugin/GalaxyHologram`.
   - LE2: `HalcyonLe2Bridge::PlaceGalaxyPanel` + `gGalaxyTable` (`vr::galaxy_map::NativeTablePanel`), its own
     `gGalaxyInputMode`/`gGalaxyTriggerLatch`.
   - LE3:
     - `HalcyonLe3Bridge.cpp` `ReadGalaxySettings`/`PlaceNativePanel`: the same `[Galaxy] nativeTable`,
       `tableDistanceM` 0.70, `tableBelowM` and `tableRadiusM` keys and clamps as LE2's;
     - `NativeMapInputLease`;
     - `game/Le3GalaxyCapture.cpp`, whose comment says "Same parallel-camera/crop policy as ME1 StepWindow"; it repeats
       `vr::cine_screen::CropFraction`/`BaselineUU` with a hard-coded 1024Ãƒâ€”576 target.
   - **To unify:**
     - `vr::galaxy_map::ReadTableSettings` and one host helper for both bridges;
     - LE2 adopts LE3's input lease;
     - a shared `capture_rig::StereoWindow::Step(pov, convergeUU, widthM, depth, options)` that ME1's `StepWindow` and
       `Le3GalaxyCapture` both call;
     - then docs/galaxy-projector-handoff.md "Remaining work" 5: ME1's flow on ME2 with ME2's native adapter.
4. **Traversal's native observer.**
   - Copies: `game/Le3Traversal.cpp` (626 lines) and ME2's observer, `game/Le2Traversal.cpp` (until 2026-11-16 the
     `#if defined(MEVR_GAME_LE2)` block of `game/Traversal.cpp`).
   - **Step 1 done 2026-11-16 (lane GF2):** ME2's block moved unchanged to `game/Le2Traversal.cpp` behind
     `traversal::le2` (`Le2Traversal.hpp`, the shape of `Le3Traversal.hpp`: `TryStartup`, `Tick`, `Moving`, `StatusJson`,
     `LogStats`, `Command`, `Shutdown`). `game/Traversal.cpp` keeps the settings, the one command surface and the dispatch;
     `traversal::Moving()` means what it did. Checked: the three builds, ctest, ME2's vault fixture 3 of 3 before and after
     (sessions `20261116-153741-665-LE2-gf2-run2`, `20261116-173826-670-LE2-gf2-run4`), LE1's same-pose run (identical).
   - Both watch the camera update and the custom action's start and end over `core/vr/Traversal`. Only LE3 uses
     `ActionLifecycle`/`AuthoredYaw`.
   - **To unify:** ~~move LE2's block to `game/Le2Traversal.cpp`~~ (step 1, done), and put both on
     `ActionLifecycle`. Keep the per-game admission: `CanPerformMantle`/`MantleCover` on ME2; ladders and evade on ME3.
   - **Still open (lane GF looked, 2026-11-16).** The two are not one observer written twice. ME2's polls the
     controller's `CurrentCustomAction` byte (`Observe`, `BeginMove`/`EndMove`, a `Move` record with the level view, the
     hands and the press's judgement), measured on the vault fixture. ME3's runs `ActionLifecycle` from
     `StartCustomAction`/`EndThisCustomAction`/`InterruptThisCustomAction` hooks on the pawn's action object, with its
     own yaw presentation. What they share beyond `core/vr/Traversal` is the camera update's sample (pawn, POV,
     collision, physics) and the POV write. The steps, each a landing with ME2's vault fixture run
     (`tools/le2-halcyon-run/vault.ps1`) before and after: (1) the block move above, mechanical; (2) one camera-update
     handler in `Traversal.cpp` that reads the sample once and hands it to the game's adapter; (3) ME2's begin and end
     through `ActionLifecycle` (`Request` at the commit, `Observe` from the poll), keeping its `Move` record as the
     report. `Le3Traversal.cpp` was another session's file while this lane ran, so only its `Guard` changed (Forks 11).
5. **The squad weapons tick. Unified 2026-11-16 (lane GF).**
   - Was: `game/Le2SquadMenu.inc` `TickLe2`/`EquipLe2` (LE2 and LE3, 116 lines) beside `game/SquadWeapons.cpp` `Tick`
     (LE1). The `.inc` re-implemented the pointing pre-selection (a world-space angle in place of
     `menu::PickPointed`), the input assembly, the haptics and the pending-weapon follow-up. They shared only `gMachine`.
   - Now: one drawn-menu tick, `SquadWeapons.cpp` `TickCustom` (ME1's, moved out of `#if MEVR_GAME_LE1` unchanged),
     over a `native` adapter per game with one interface (`Read`, `Pause`, `Unpause`, `Stop`, `Equip`, `Follow`,
     `TileName`, `NextPage`): ME1's in `SquadWeapons.cpp` (quick slots, the weapon wheel's own order), the sequels' in
     `Le2SquadMenu.inc` (a paged inventory of weapon objects, `OrderWeaponSwitch`, `Pawn.Weapon`). ME1's native wheel
     (`TickNative`) stays ME1's.
   - What changed on the sequels with it (ME1's behaviour, in place of the copy's): the pointing is
     `menu::PickPointed` in tracking space; not pointing, the squadmate nearest the ray is preselected (was the first);
     the two cards are ordered by who stands further left (was squad order); ME1's haptics per event (was one pulse)
     and ME1's log lines; not on foot, no world view or sleeping controllers close the menu through `Inactive` (was an
     unusable machine). Still to check in each sequel's game: G30.
   - ME1 checked before and after on the fake headset (sessions `2026-11-16_13-57-19-gf-squad-before` at 6329fb0c
     and `2026-11-16_13-47-06-gf-squad-after`; a squad save with Garrus and Tali, a paused snapshot under the same
     replayed pose, then `style = custom` and a replayed left-trigger pull, right-stick flick and release): `hostView`
     and `mod.squad` identical, the HUD, the wrist and both eyes pixel for pixel, and the same `squad:` lines from
     `menu #1 opened` to `TALI holds the assault rifle`.
6. **The sequel power roster. Unified 2026-11-16 (lane GF).**
   - Was: `game/Le2Squad.hpp` `le2_squad::Powers` and `game/PowerWheel.cpp`'s roster read each walked
     `BioPawn.PowerManager`'s `Powers` and resolved ME2's evolved instances with their own copies of the handles.
   - Now: `game/SequelPowers.hpp` (`sequel_powers::ForEach`, and ME2's `Learned`/`Enabled`/`Seconds` over one set of
     `SFXPower` handles), beside `game/Le3Powers.hpp`, whose `State` stays ME3's per-instance read. Both consumers keep
     their own filters (the wheel shows a power the game won't state as dim; the squad leaves it out).
7. **The HDR display answer. Unified 2026-11-16 (lane EY).**
   - Was: LE1 `plugin/HalcyonBridge.cpp` `HdrDisplayDesc` and the sequels' `plugin/HdrDisplayPolicy.cpp`, two copies of
     one policy (the sequels' read the key as an integer and gave no luminance with an HDR10 answer).
   - Now: `hdr_display::Answer(desc, moveOldDefault)` in `plugin/HdrDisplayPolicy.cpp`; LE1's seam forwards to it, the
     sequels' GetDesc1 hook calls it. The `hdrDisplay` migration (0 to -1) is LE1's flag on that one function.
8. **The eye-image pipeline. One core, two call shapes since 2026-11-16 (lane EY).**
   - Was: LE1 applied colour, upscale and the tonemap in Halcyon's blit; the sequels colour only, in
     `plugin/EyeCopy.cpp`'s `kCorrectedShader`.
   - Now: `plugin/Upscale` is in all three hosts, its passes one function; the sequels reach it from `EyeCopy`, LE1 from
     `EyeUpscale`. `EyeCopy`'s corrected pass runs `MEVR_TONE`. The colour core was already one.
   - Left: LE1's blit onto `CopyIntoImage` needs a fill argument (`fillUv`, `imageRect`, the white scale) and a same-pose
     check of its four paths (research/27 9.3).
9. **Scene-capture hook scope.**
   - Copies: `plugin/Le3SceneCapture.cpp` (CaptureHook, RenderScoped, BackbufferHook) re-implements RenderLevers'
     capture-depth and thread-local scope. The target logic is already shared (`plugin/SceneCaptureTarget.hpp`,
     `core/vr/SceneCaptureScope`).
   - **To unify:** one scope helper parameterised by each game's RVAs. LE2's scope capture (G07) and galaxy window then
     come with it.
10. **Test tooling.**
    - Launchers: `tools/launch-game.ps1`, `tools/le2-halcyon-run/launch.ps1`, `tools/qa-le2/launch.ps1`.
    - Save contracts: `tools/le1-save.py` (rewind) and `tools/le2-save.py` (recipes and receipts; `le3-save.py` imports it).
    - Runners: `fake-headset-session.ps1`, and for the sequels `tools/le2-halcyon-run` plus `run-lock.ps1`.
    - The LE2 diagnostic host's own pad and observers.
    - **To unify:** the LE3 plan's rule 6 ("extend existing runners and save tools rather than introducing another
      launcher, recorder, screenshot format or writable gameplay ledger"). One runner with a `-Game` parameter, one save
      tool interface (`rewind` and `recipe` both). Retire the diagnostic host once the Halcyon host covers its observers.
11. **LE3's movement admission predicate, six times. Unified 2026-11-16 (lane GF).**
    - Was: `Comfort.cpp` `NativeLocomotionAllowed`, `ArmIk.cpp` `MenuBodyEligible`, `Posture.cpp` `NativeMovement`,
      `Sprint.cpp` `NativeAllowed`, `Melee.cpp` `Refusal` and `Le3Traversal.cpp` `Guard` each read mode 0,
      `CurrentCustomAction == 0`, `IsMoveInputIgnored`, `IsDead`, physics and cover with handles of their own.
    - Now: `game/Player` `player::MovementRefused(controller, pawn, wanted)` returns the first of the wanted facts
      (`player::admission::kOnFoot`, `kNoAction`, `kWalking`, `kOutOfCover`, `kMoveInput`, `kAlive`, ...) that doesn't
      hold, failing closed on anything unreadable; `MovementAdmissionReady()` is the one startup check of the names'
      shapes. Each caller asks for the facts it asked before, in its own order of reasons, so each one's answer is the
      same. `Le3Traversal.cpp` keeps its own handles for its startup check and its samples (the file is another
      session's; only `Guard` changed).
    - LE2 could use it too: the names need `python tools/le2-name-audit.py --game LE2` first (ME2's custom action is on
      the controller, `SFXPlayerController.CurrentCustomAction`).
12. **Melee routing.** `game/Melee.cpp` (LE3) and `game/Throwables` `ik::throwing::RouteMelee` (LE1) are two
    gesture-to-dispatch paths over the same swing detector. **To unify:** LE1 moves onto `ik::melee::Gestures` with a
    per-game dispatch.
13. **Menu glue.** `plugin/MenusLe2.cpp` (LE2 and LE3) says in its header that it is LE1's menu glue: the
    `HalcyonBridge` `OnFrameBegun`/`OnMenuKey`/merge parts, plus HalcyonOverlays' layer composition. **To unify:**
    `HalcyonBridge` calls `MenusLe2` (renamed `Menus`).
14. **Upstream's renderer trees.** `src/halcyon/le2` and `le3` are upstream's two ME2/ME3 renderers, so their
    differences aren't our waste. Our `[MEVR]` seams are re-applied in each (pose tags, HDR `OnFactory`, the floor
    translation, the ProcessEvent queue). The common seam header below keeps that to one declaration.
15. **Necessarily per renderer, not waste.** `ApplyMevrBaseline` (le2 `me2_menu.cpp`) against LE1's `ModernBaseline`;
    `RendererLe1/2/3.cpp`; Halcyon ME2's `convo_fp.cpp`.

Not forks, thin adapters (checked): `game/Le3Gui.cpp`, `game/Le3Cover.inc` (included inside VrCover),
`game/Le3Powers.hpp`, `game/Le3WeaponAmmo.hpp`, `game/Le2Squad.hpp`'s field tables, `game/Le3GalaxyRetention`.

## Seams

Since 2026-11-16 (lane U1) the three renderers share **one seam vocabulary**: `src/halcyon/MevrSeams.h`, namespace
`MevrSeams`, one signature per function. [research/trilogy-seams.md](../research/trilogy-seams.md) has each signature's
reasoning, where each game's host defines each seam, and the steps to add one. The generated
[seam tables](#the-three-mevrbridge-vocabularies) list which tree calls which common seam, and each tree's own.

**Before** (the audit of `ccacd667`): LE1 declared 111 `MevrBridge` functions, LE2 32 and LE3 29. They shared 18 names
and six of those differed in signature (`OnViewBuilt`, `WrapSessionFunctions`, `OnEyeCopied`, `CopyIntoImage`,
`HudPanelDraw`, `QueueProcessEvent`), so no shared module could be written against them once. There were two
`OverlayFrame` structs and three panel-placement seams.

**The common set (32 functions, six groups).** Every tree includes `MevrSeams.h` and calls `MevrSeams::X`:

1. **Session and frame (7):** `StaticXrGetInstanceProcAddr`, `WrapSessionFunctions` (seven slots; a renderer passes
   null for one it lacks), `OnSessionCreated`, `OnSessionStopped`, `OnFrameBegun`, `OnRecentered`, `XrRuntimeIsSteamVr`.
2. **View (3):** `OnViewBuilt` (the head look in UU, the head quaternion and position, the view offset),
   `FloorViewTranslation`, `RecenterOrientation` (ME1's tilt removal, R28, for every renderer that calls it).
3. **Eye image (5):** `OnEyeSource`, `CopyIntoImage(context, image, source, shiftX, eye)`,
   `OnEyeCopied(eye, image, index, acquired, waited)`, `OnEyeReleased`, `OnFactory`. LE1's blit does not call these yet
   (Forks 7, 8: the eye-image lane).
4. **Composition (7):** `OverlayLayers(OverlayFrame)` over **one** frame struct, `HudPanelDraw(context, draw, user,
   textBatch, afterLeftEye)`, `CineScreenWanted`, `CineScreenFullRate`, `CineScreenLayers`, `TransitionFadeLayer`, and one
   `PlacePanel(kind, frame, viewPosition, widthM, heightM, imageReady)`.
   - `PlacePanel` replaces LE1's `PlacePanel`, LE2's `PlaceGalaxyPanel` and LE3's `PlaceNativePanel`.
   - LE1's ten `HudPanel*` draw seams stay LE1's own until its draw hooks go through `HudPanelDraw`.
5. **Input and threads (6):** `MergeMotionControllers`, `KeyDown`, `OnMenuKey(ownMenuOpen)`, `MainMenuOpen`,
   `OnGameThread`, `QueueProcessEvent(object, function, params, size)`.
6. **Frame-breakdown probe (4, optional):** `PerfPresentBegin`, `PerfPresentEnd`, `PerfFirstEyeCaptured`, `PerfDraw`,
   called by both sequels' trees (lane PL's probe). LE1 keeps its own `PerfPresentScope`, `PerfPass0Captured`, `PerfPoint` and
   `g_perfDraws` until they move.

**Against the audit's 26:** `MainMenuOpen` and `CineScreenFullRate` were added (already in all three trees with one
signature), and the perf group. `OnViewBuilt` takes the sequels' signature, which already carried the quaternion (the
audit read it as yaw and pitch only). `WrapSessionFunctions` has seven slots, not five.

**Capabilities.** `mevr::host::renderer::Capabilities()` (`plugin/Renderer.hpp`, filled by each `RendererLeN.cpp`):
the common groups a renderer's tree calls (`eyeImageSeams`, `hudPanelDraw`, `hostCineScreen`, `recenterTilt`,
`panelsInRoom`, `galaxyTable`, `perfSeams`) and the per-renderer extensions (`oneDraw`, `perEyeFrustum`, `metricStereo`,
`hudPanelGameplay`, `hudPieces`, `worldUi`, `galaxyWindow`, `frontEnd`, `conversationWorld`, `eyeColourInBlit`,
`screenReticleHide`, `floorViewJournal`, `headArmSequence`, `ownMenuRows`). Shared host modules and `BuildServices` ask it
in place of `#if defined(MEVR_WITH_HALCYON_LEx)`; `WithRenderer` and `WithHudPieces` do so far. This is the LE3 plan's
architecture-contract rule 3, "populate a service only when its renderer ... exist", made checkable.

**The host side.** The seams with one answer for every game are implemented once, in `plugin/Seams.cpp`
(`OnGameThread`, `QueueProcessEvent`, `KeyDown`, `OnMenuKey`, `MainMenuOpen`, `CineScreenWanted`, `CineScreenFullRate`,
`XrRuntimeIsSteamVr`). The rest are still per game, in `namespace MevrSeams` of each bridge: merging those is Forks 1
and 2 (the overlay composition and the sequel bridges).

**Per renderer, not waste.** What stays in a tree's own `mevr_bridge.h`:
- LE1 (about 90): the performance work (one-Draw's `Record*`, the perf points, Quest pacing), the HUD panel's draw
  seams, metric stereo, the galaxy view swaps, conversations in the world, the front end, the eye blit's colour and
  upscale, Halcyon's own menu.
- LE2: `JournalFloorViewWrite`, `CineScreenHalfSeparationUU`, `OnSwapChainPresent`, `WorldUiOwnsGui`, and the inline
  `HotkeyDown`.
- LE3: `CineScreenHalfSeparationUU`, `OnPresent`.

**State of the migration (2026-11-16).** Done: the header, the host's definitions in `namespace MevrSeams`,
`plugin/Seams.cpp`, the capability set, and **all three trees call the common names** (`MevrSeams::X`); the old names
and their forwarders are gone, and `SequelPresentation.h` with them. The sequels' recenters ask `RecenterOrientation`
(R28), and `PlacePanel` replaced `PlaceGalaxyPanel` and `PlaceNativePanel`. The perf group is wired on both sequels over lane PL's `mevr::host::perf` (capability `perfSeams`). Open: LE1 does
not call the eye-image group, `HudPanelDraw` or `CineScreenLayers` (the eye-image lane and Forks 1). In-game checks (research/trilogy-seams.md
"Checks"): ME2 ran on the fake headset (the session, Save 4, both eyes and the `hud` layer); LE1's same-pose pair matched
(`hostView`, the layers, the HUD panel pixel for pixel) on a noisy day; ME3 was not run (the runner could not adopt the
game that day).

## Plans

This maps where each row's open work is written today. It is the input for merging the plans into one trilogy work list
later. No plan was merged or removed here. Rows marked **none** have no plan anywhere: that work is new.

**That work list now exists: [docs/trilogy-plan.md](trilogy-plan.md)** (2026-11-16). Every row here, the **none** rows
included, belongs to one of its features (T01-T53; its "Where the old plans' items went" lists them row by row). The
table below stays as the map of where each row's work was written before.

| rows | open work lives in |
|---|---|
| R01, R27 | notes/headset-checklist.md (LE2 lines; **none** for LE3); research/sequel-eye-pose-tags.md |
| R02-R09, R13, R15, R22-R24, R26, R30, R35 | **none** for LE2. LE3: docs/me3-overall-goal-handoff.md Ã‚Â§4 and its step 7 (performance only, generally) |
| R10 | LE3: research/le3-recenter-transition-failure.md (other scenes, stale-copy injection); LE2 **none** |
| R11 | research/le2-renderer-integration.md "Defaults from ME1" ("Also noted", [verify]); LE3 **none** |
| R12 | docs/knowledge/gotchas/me2-scale-set-during-load; LE3 **none** |
| R14, R16 | research/le2-hdr.md (HDR dropped by the owner 2026-11-14); research/le3-hdr-eye-copy-test.md; docs/le3-port-implementation-plan.md ledger "HDR reuse" |
| R17, R25, R32-R34 | Ã¢â‚¬â€ (done; checks only) |
| R18, R19, R20 | research/le2-renderer-integration.md "What is left of M2"; research/le3-shared-presentation.md "crop gates"; research/le3-native-renderer-audit.md |
| R21 | docs/le2-continuation-plan.md M5 (Veetor's timed interrupts); research/le3-dialogue-cinema.md "LE3 stereo cinema candidate"; L3-4 |
| R28, R29, R31 | **none** (R31: docs/galaxy-projector-handoff.md for LE3's own) |
| G01-G05, G13-G16 | docs/le2-continuation-plan.md M3; docs/vrik-porting-guide.md Ã‚Â§5-6; research/le2-vrik-audit.md; docs/le2-port-implementation-plan.md P3; LE3: research/le3-body-pose.md "Native gates still required", L3-3 |
| G06, G07 | research/le2-le3-vr-optics.md; L3-5 |
| G08, G18 | docs/le3-port-reuse-audit.md "Holsters", "Locomotion and comfort"; docs/le2-port-implementation-plan.md P6 (holsters), P5; **none** for combat stance on LE2 |
| G09-G12 | research/le2-vrik-audit.md (LE2); **none** for LE3 |
| G17, G39, G40, G58 | docs/le2-continuation-plan.md M6 table; docs/le2-m6-native-checks.md; L3-8; research/le3-locomotion-audit.md |
| G19-G23, G26 | docs/le2-continuation-plan.md M4; research/le2-menus.md; research/le3-native-gui.md; research/le3-menu-pause.md; L3-4 |
| G24 | docs/le2-validation-coverage.md "Hacking and bypass" |
| G25 | docs/galaxy-projector-handoff.md "Remaining work" 1-5; docs/le2-m6-implementation-guide.md G; research/le3-galaxy.md; L3-9 |
| G27, G28 | docs/le2-m6-implementation-guide.md A; docs/le2-continuation-plan.md M6 table (wrist/gun, labels); L3-4 |
| G29-G35, G57 | docs/le2-m6-implementation-guide.md B-E; docs/le2-m6-squad-implementation.md; research/55; research/70; research/le3-powers.md; research/le3-squad.md; L3-6 |
| G36, G38 | docs/le2-m6-implementation-guide.md F (deferred); LE3 plan "remain deferred"; **none** for sound placement |
| G41, G42, G43 | docs/le2-port-implementation-plan.md P4 (reload); research/le3-melee-audit.md, L3-7 |
| G44 | docs/le2-continuation-plan.md M6 ("the Hammerhead has its own later pass"); docs/le2-from-le1.md 3.8; P8 |
| G45, G48, G51, G56 | **none** |
| G46, G47, G50 | docs/le2-continuation-plan.md M7.1, M7.2; docs/me3-overall-goal-handoff.md L3-10 |
| G49 | notes/headset-checklist.md; L3-10 |
| G52 | docs/le2-continuation-plan.md M1; research/le2-engine-access-discovery.md; research/le3-engine-access-discovery.md; L3-1 |
| G54, G55 | docs/le3-port-implementation-plan.md "Architecture contract" rule 6, "Reuse the save/editor and test flow"; docs/le2-scripted-testing.md |
| Forks 1, 2, 13, seams | **none**: the LE3 plan's architecture contract states the rule, but no plan schedules the refactor |

The plans and handoffs this draws on (none was changed):
- **ME2:** docs/le2-continuation-plan.md (M1-M7), docs/le2-m6-implementation-guide.md (A-G), docs/le2-from-le1.md,
  docs/le2-port-implementation-plan.md (P0-P9), docs/le2-port-reuse-map.md, docs/le2-integration-contract.md,
  docs/le2-validation-coverage.md.
- **ME3:** docs/le3-port-implementation-plan.md (L3-0..L3-10, architecture contract), docs/le3-port-reuse-audit.md,
  docs/me3-overall-goal-handoff.md, docs/galaxy-projector-handoff.md.
- **Both:** docs/vrik-porting-guide.md, research/le2-vrik-audit.md, research/le2-renderer-integration.md,
  notes/headset-checklist.md.

## Found while auditing

These are concrete, each with its check. None was fixed here (read-only audit).

- **The sequels turned character shadows off for nothing (R11). Fixed 2026-11-16 (lane GF2):** the entry follows the
  host's `secondEyeIsReplay` (HostServices v43), and elsewhere the player's own value is put back. On ME2 the key alone
  showed no visible change in the one scene tried; the ground shadows there are `DynamicShadows` (off on every game).
- **CameraShake compiled ME1's handles into the sequels. Fixed 2026-11-16 (lane GF2):** the weapon-stats, Mako and
  `SFXCameraShakeBase` handles and their scaling exist only in the LE1 build; the sequels keep their camera-modifier
  adapter (G11), and their warning names their own fields.
- **EyeCopy had no tonemap (R15).** Fixed 2026-11-16 (lane EY): `MEVR_TONE` in its corrected pass.
- **`umbraSecondEyeClearBits = 2` does nothing at the shipped defaults (LE1).** RenderLevers applies it only when
  `umbraSharedVisibility != 0`.
- **Latent "not LE2 means LE1" defaults on LE3. Fixed 2026-11-16 (lane GF).**
  - `game/ScreenReticle.cpp` compiled ME1's reticle handler names in its `#else`, and with them ME1's and ME2's
    BioSFPanel names, which ME3 lacks (14 names in the generated list below). `game/HudPieces.cpp` fell to ME1's placement (the
    muzzle offset, the Mako's displays, ME1's bone names). Both were inert only because LE3 serves neither service.
  - Now both stand down by name in the LE3 build (`reticles: no crosshair adapter on ME3 yet`, `hud pieces: not on ME3
    yet`), and none of ME1's or ME2's names compile into it. ME3's own handles and ReflectCheck rows come with its
    adapters (G28; the reticle with R18).
- **LE3's voice model tier needed LE2 in the build. Fixed 2026-11-16 (lane GF):** the llama block in `CMakeLists.txt`
  now asks whether the game's host compiles `plugin/VoiceModel.cpp` (checked: an LE3-only configure has
  `MEVR_HAVE_LLAMA` and `core/voice/Llm.cpp`).
- **Two configuration values nobody reads.**
  - (fixed by lane U1: `RendererLe2::RecenterTiltDeg` is read by `plugin/RecenterTilt` since the LE2 tree calls
    `RecenterOrientation`.)
  - `PowerWheel::StatusJson` read the controller through `Ready()`, which is null on the sequels. **Fixed 2026-11-16
    (lane GF):** it asks `Up()`. The value shown was never empty there, though: the sequels' `MappedPower` ignores
    the controller and names the wheel's own selection.
- **LExSDKv2's generated LE3 header lists no `Engine.Actor.Location`**, though every game has it, so the generated
  "names" table has false entries. ReflectCheck, against the running game, is the authority; the table is a lead.

## Generated facts

`python tools/parity-ledger.py --write` regenerates this part. Don't edit between the markers. Module statuses are
defined in the tool's header:
- `run`: its engine access runs there.
- `ready`: stands down off LE1.
- `out`: the engine part is compiled out.
- `ungated`: no ue3 access in any game.
- `unreferenced` and `unbuilt`: as named.
- "ME1-only handles": default-reach handles compiled into a sequel build, which never resolve there.

### Summary

<!-- parity:summary -->
| game | renderer | host plugin files | host core files | services served | src/game + src/engine modules by status | common seams called | own seams |
|---|---|---|---|---|---|---|---|
| LE1 | halcyon-le1 | 73 | 71 | 58 | run 67, out 4, ungated 25, unbuilt 4 | 22 of 37 | 89 |
| LE2 | halcyon-le2 | 68 | 57 | 49 | run 44, ready 10, out 19, ungated 21, unreferenced 2, unbuilt 4 | 35 of 37 | 5 |
| LE3 | halcyon-le3 | 71 | 57 | 50 | run 44, ready 10, out 18, ungated 23, unreferenced 2, unbuilt 3 | 35 of 37 | 2 |
<!-- /parity:summary -->

### Host plugin files per game

<!-- parity:hosts -->
| src/plugin | LE1 | LE2 | LE3 |
|---|---|---|---|
| `ArmSample` | - | yes | yes |
| `Calibration` | yes | yes | yes |
| `CalibrationDraw` | yes | yes | yes |
| `CineScreen` | yes | yes | yes |
| `CinemaBackdrop` | yes | yes | yes |
| `CinemaBackdropDraw` | yes | yes | yes |
| `Colour` | yes | yes | yes |
| `ConversationWorld` | yes | - | - |
| `CrashBreadcrumbs` | yes | yes | yes |
| `DrawReplay` | yes | yes | yes |
| `Entry` | yes | yes | yes |
| `EyeCopy` | - | yes | yes |
| `FloorLock` | yes | yes | yes |
| `GalaxyHologram` | yes | - | yes |
| `GalaxyHologramDraw` | yes | yes | yes |
| `GamePointer` | yes | yes | yes |
| `GameThreadCalls` | yes | yes | yes |
| `Glow` | yes | yes | yes |
| `Halcyon` | yes | - | - |
| `HalcyonBridge` | yes | - | - |
| `HalcyonLe2` | - | yes | - |
| `HalcyonLe2Bridge` | - | yes | - |
| `HalcyonLe3` | - | - | yes |
| `HalcyonLe3Bridge` | - | - | yes |
| `HalcyonOverlays` | yes | - | - |
| `HalcyonSequelBridge` | - | yes | yes |
| `HandReticle` | yes | yes | yes |
| `HdrDisplayPolicy` | yes | yes | yes |
| `HudPanel` | yes | yes | yes |
| `HudPieceLayers` | yes | yes | yes |
| `KeptLocks` | yes | - | - |
| `Le3SceneCapture` | - | - | yes |
| `MainMenu` | yes | yes | yes |
| `MainMenuSettings` | yes | yes | yes |
| `MenusLe2` | - | yes | yes |
| `MetricStereo` | yes | yes | yes |
| `MevrMenuPointer` | yes | - | - |
| `MevrMenuTab` | yes | - | - |
| `ModLoader` | yes | yes | yes |
| `MotionBlurGuard` | yes | - | yes |
| `OmniPad` | yes | - | - |
| `OmniToolRail` | yes | yes | yes |
| `OpenAlSoft` | yes | - | - |
| `PadMerge` | yes | yes | yes |
| `PerfProbe` | yes | yes | yes |
| `PerfProbeLe1` | yes | - | - |
| `PerfProbeSequel` | - | yes | yes |
| `PowerIcons` | yes | yes | yes |
| `PowerWheel` | yes | yes | yes |
| `PowerWheelDraw` | yes | yes | yes |
| `PresentHook` | yes | yes | yes |
| `PresentationHold` | yes | - | - |
| `ProcessEventHook` | yes | yes | yes |
| `QuadSwapchain` | yes | yes | yes |
| `QuestPacing` | yes | - | - |
| `RailArt` | yes | yes | yes |
| `RecenterTilt` | yes | yes | yes |
| `RenderLevers` | yes | yes | yes |
| `RendererLe1` | yes | - | - |
| `RendererLe2` | - | yes | - |
| `RendererLe3` | - | - | yes |
| `ReportRequest` | yes | yes | yes |
| `ScopeCaptureSource` | yes | yes | yes |
| `Seams` | yes | yes | yes |
| `SequelCinema` | - | yes | yes |
| `SequelOverlays` | - | yes | yes |
| `SequelSquadLayers` | - | yes | yes |
| `SequelWeaponLayers` | - | yes | yes |
| `SetupRequest` | yes | yes | yes |
| `SightLayers` | yes | yes | yes |
| `SightPictures` | yes | yes | yes |
| `Spectator` | yes | - | - |
| `SquadPanel` | yes | yes | yes |
| `SquadPanelDraw` | yes | yes | yes |
| `SquadPlaceDraw` | yes | yes | yes |
| `SteamVrSmoothingRequest` | yes | - | - |
| `Swapchains` | yes | yes | yes |
| `TestLoop` | yes | yes | yes |
| `Thespian` | yes | yes | yes |
| `TransitionFade` | yes | yes | yes |
| `TraversalCue` | - | yes | yes |
| `UiWorldFigure` | yes | - | - |
| `Upscale` | yes | yes | yes |
| `Vignette` | yes | yes | yes |
| `Voice` | yes | yes | yes |
| `VoiceModel` | yes | yes | yes |
| `WorldPanels` | yes | yes | yes |
| `WorldUi` | yes | yes | yes |
| `WristBeam` | yes | yes | yes |
| `XrHost` | yes | yes | yes |

- LE1: also `engine/CodeDump`, `game/GamepadMapping`, `ue3/Pattern`, `ue3/SafeNames`
- LE2: also `engine/CodeDump`, `game/GamepadMapping`, `ue3/Pattern`, `ue3/SafeNames`
- LE3: also `engine/CodeDump`, `game/GamepadMapping`, `ue3/Pattern`, `ue3/SafeNames`
- LE3 mod extras: `core/Json`, `game/Le3Gui`
<!-- /parity:hosts -->

### Host core files per game

<!-- parity:core -->
In all three hosts (55): `Exe`, `ExeCheck`, `Hook`, `Hotkey`, `Ini`, `Json`, `Log`, `PngFile`, `Version`, `Window`, `d3d11/ShaderReflect`, `gfx/Raster`, `gfx/Swf`, `gfx/SwfFont`, `voice/ScriptMatch`, `vr/AimRay`, `vr/Calibration`, `vr/CineScreen`, `vr/Colour`, `vr/Comfort`, `vr/Controllers`, `vr/ConversationWorld`, `vr/FloorLock`, `vr/FrameProbe`, `vr/GalaxyHologram`, `vr/GalaxyMap`, `vr/GameMenuRequest`, `vr/GamePointer`, `vr/GuiEvents`, `vr/Haptics`, `vr/HostView`, `vr/HudPieces`, `vr/ImageConvert`, `vr/InteractLabel`, `vr/LayerComposite`, `vr/MenuPointer`, `vr/MetricStereo`, `vr/OmniRail`, `vr/OverlayPlacement`, `vr/PoseRecording`, `vr/PowerWheel`, `vr/Recenter`, `vr/ScopeArt`, `vr/ScopeCapture`, `vr/ScreenMatrix`, `vr/Sights`, `vr/SquadMenu`, `vr/SquadPlace`, `vr/TestRequest`, `vr/Thespian`, `vr/TransitionFade`, `vr/Upscale`, `vr/WorldUi`, `xr/Floor`, `xr/Input`.

| src/core (host) | LE1 | LE2 | LE3 |
|---|---|---|---|
| `d3d11/Blit` | - | yes | yes |
| `vr/CockpitHazard` | yes | - | - |
| `vr/ColourStrip` | yes | - | - |
| `vr/CombatStance` | yes | - | - |
| `vr/MakoControls` | yes | - | - |
| `vr/MakoGunner` | yes | - | - |
| `vr/OmniPad` | yes | - | - |
| `vr/PresentationHold` | yes | - | - |
| `vr/QuestPacing` | yes | - | - |
| `vr/Spectator` | yes | - | - |
| `vr/Sprint` | yes | - | - |
| `vr/SquadContext` | yes | - | - |
| `vr/SquadGesture` | yes | - | - |
| `vr/SquadParser` | yes | - | - |
| `vr/SquadRetreat` | yes | - | - |
| `vr/SquadStanding` | yes | - | - |
| `vr/Traversal` | - | yes | yes |
| `vr/WheelCrop` | yes | - | - |
<!-- /parity:core -->

### HostServices per game

<!-- parity:services -->
Helpers applied, in order:

- LE1: `BaseServices`, `WithCrashBreadcrumbs`, `WithRenderer`, `WithVignette`, `WithSights`, `WithPadMerge`, `WithHudPieces`, `WithFloorLock`, `WithVoice`, `WithPowerWheel`, `WithLe1Modules`, `WithGameMenuRequest`, `WithScopeCapture`, `WithInteractLabel`, `WithMakoControls`, `WithSpectator`, `WithGalaxyMap`, `WithGalaxyHologram`, `WithWorldUi`, `WithLe1ExtraUi`
- LE2: `BaseServices`, `WithCrashBreadcrumbs`, `WithRenderer`, `WithVignette`, `WithSights`, `WithPadMerge`, `WithHudPieces`, `WithFloorLock`, `WithVoice`, `WithPowerWheel`, `WithTraversal`, `WithMenus`
- LE3: `BaseServices`, `WithCrashBreadcrumbs`, `WithGalaxyHologram`, `WithRenderer`, `WithVignette`, `WithSights`, `WithPadMerge`, `WithHudPieces`, `WithFloorLock`, `WithTraversal`, `WithVoice`, `WithPowerWheel`, `WithMenus`

| HostServices field | LE1 | LE2 | LE3 |
|---|---|---|---|
| `askVoiceModel` | WithVoice | WithVoice | WithVoice |
| `beginScriptCall` | WithCrashBreadcrumbs | WithCrashBreadcrumbs | WithCrashBreadcrumbs |
| `calibration` | WithLe1Modules | WithMenus | WithMenus |
| `controllers` | WithRenderer | WithRenderer | WithRenderer |
| `endScriptCall` | WithCrashBreadcrumbs | WithCrashBreadcrumbs | WithCrashBreadcrumbs |
| `floor` | WithRenderer | WithRenderer | WithRenderer |
| `hapticContinuous` | WithRenderer | WithRenderer | WithRenderer |
| `hapticPulse` | WithRenderer | WithRenderer | WithRenderer |
| `hapticPulseHz` | WithRenderer | WithRenderer | WithRenderer |
| `hideScreenReticle` | WithRenderer | WithRenderer | WithRenderer |
| `keepLoaded` | BaseServices | BaseServices | BaseServices |
| `leftGripAway` | WithPadMerge | WithPadMerge | WithPadMerge |
| `logWorldUiStats` | WithWorldUi | WithMenus | - |
| `mainMenu` | WithLe1Modules | WithMenus | WithMenus |
| `motionBlurProtected` | WithLe1ExtraUi | - | BuildServices |
| `processExiting` | BaseServices | BaseServices | BaseServices |
| `publishCameraOffset` | WithLe1ExtraUi | - | - |
| `recordWrite` | WithCrashBreadcrumbs | WithCrashBreadcrumbs | WithCrashBreadcrumbs |
| `reserveWorldUi` | WithWorldUi | WithMenus | - |
| `rightGripFree` | WithPadMerge | WithPadMerge | WithPadMerge |
| `secondEyeIsReplay` | WithRenderer | WithRenderer | WithRenderer |
| `setAimRay` | WithRenderer | WithRenderer | WithRenderer |
| `setConversationWorld` | WithLe1ExtraUi | - | - |
| `setFloorLockSample` | WithFloorLock | WithFloorLock | WithFloorLock |
| `setGalaxyHologram` | WithGalaxyHologram | - | WithGalaxyHologram |
| `setGalaxyMap` | WithGalaxyMap | - | - |
| `setHandAim` | WithRenderer | WithRenderer | WithRenderer |
| `setHudPieces` | WithHudPieces | WithHudPieces | WithHudPieces |
| `setInteractLabel` | WithInteractLabel | BuildServices | BuildServices |
| `setMakoControls` | WithMakoControls | - | - |
| `setPowerLock` | WithPowerWheel | WithPowerWheel | WithPowerWheel |
| `setPowerWheel` | WithPowerWheel | WithPowerWheel | WithPowerWheel |
| `setProcessEventCallback` | BaseServices | BaseServices | BaseServices |
| `setScopeCapture` | WithScopeCapture | - | - |
| `setSight` | WithSights | WithSights | WithSights |
| `setSpectator` | WithSpectator | - | - |
| `setSquadMenu` | WithLe1Modules | BuildServices | BuildServices |
| `setSquadPlace` | BuildServices | BuildServices | BuildServices |
| `setStatus` | WithRenderer | WithRenderer | WithRenderer |
| `setSupportGrip` | WithPadMerge | WithPadMerge | WithPadMerge |
| `setThespian` | WithLe1ExtraUi | BuildServices | BuildServices |
| `setTraversal` | - | WithTraversal | WithTraversal |
| `setTraversalPresentation` | - | - | BuildServices |
| `setVignette` | WithVignette | WithVignette | WithVignette |
| `setVoiceKeyterms` | WithVoice | WithVoice | WithVoice |
| `setVoiceListen` | WithVoice | WithVoice | WithVoice |
| `setWheelPanel` | WithLe1Modules | - | - |
| `setWorldUi` | WithWorldUi | WithMenus | - |
| `sightView` | WithSights | WithSights | WithSights |
| `statusWanted` | WithRenderer | WithRenderer | WithRenderer |
| `supportGripHeld` | WithPadMerge | WithPadMerge | WithPadMerge |
| `takeGalaxyPick` | WithGalaxyHologram | - | WithGalaxyHologram |
| `takeGameMenuRequest` | WithGameMenuRequest | WithMenus | WithMenus |
| `takeGameUiRequest` | WithWorldUi | WithMenus | - |
| `takeGuiEvents` | WithLe1ExtraUi | WithMenus | WithMenus |
| `takeVoiceLine` | WithVoice | WithVoice | WithVoice |
| `takeVoiceModelAnswer` | WithVoice | WithVoice | WithVoice |
| `thespianInputBlocked` | - | - | BuildServices |
| `view` | WithRenderer | WithRenderer | WithRenderer |
| `voiceCommand` | WithVoice | WithVoice | WithVoice |
| `voiceState` | WithVoice | WithVoice | WithVoice |
<!-- /parity:services -->

### Game and engine modules per game

<!-- parity:modules -->
| module | LE1 | LE2 | LE3 |
|---|---|---|---|
| `engine/BodyBounds` | run | ready | ready |
| `engine/BonePose` | run | run | run |
| `engine/CallFunction` | ungated | ungated | ungated |
| `engine/CodeDump` | ungated | ungated | ungated |
| `engine/ConversationHooks` | ungated | ungated | ungated |
| `engine/CoverAcquire` | run | ready | ready |
| `engine/Engine` | run | run | run |
| `engine/Le2ControlHost` | unbuilt | unbuilt | unbuilt |
| `engine/Le2DiagnosticHost` | unbuilt | unbuilt | unbuilt |
| `engine/Le2DiagnosticObservation` | unbuilt | unbuilt | unbuilt |
| `engine/Legacy` | ungated | ungated | ungated |
| `engine/NameLookup` | ungated | ungated | ungated |
| `engine/NativesDump` | run | run | run |
| `engine/ProcessEvent` | ungated | ungated | ungated |
| `engine/ReflectCheck` | run | run | run |
| `engine/SceneView` | ungated | ungated | ungated |
| `engine/SelectionGaze` | ungated | ungated | ungated |
| `engine/Skeleton` | run | run | run |
| `engine/VoicePlace` | run | ready | ready |
| `engine/XrToUnreal` | ungated | ungated | ungated |
| `game/ArmIk` | run | run | run |
| `game/AuthoredMoment` | run | ready | ready |
| `game/BodyCalibration` | ungated | ungated | ungated |
| `game/BodyFollow` | run | run | run |
| `game/BodyProbe` | ungated | unreferenced | unreferenced |
| `game/BodyScale` | ungated | ungated | ungated |
| `game/CallTrace` | run | run | run |
| `game/CameraCollision` | run | out | out |
| `game/CameraProbe` | run | run (ME1-only handles) | out |
| `game/CameraShake` | run | run | run |
| `game/CockpitPanel` | run | out | out |
| `game/CockpitProbe` | run | out | out |
| `game/CombatStance` | run | out | out |
| `game/Comfort` | run | run | run |
| `game/ConsoleRequest` | run | run (part) | run (part) |
| `game/ConversationFlow` | run | ready | ready |
| `game/ConversationWorld` | run | ready | ready |
| `game/Fingers` | ungated | ungated | ungated |
| `game/FocusPause` | run | out | out |
| `game/GalaxyDump` | run | ready | ready |
| `game/GalaxyMap` | run | ready | ready |
| `game/GameArt` | run | out | out |
| `game/GameMenuRequest` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/GamepadBridge` | ungated | ungated | ungated |
| `game/GamepadMapping` | ungated | ungated | ungated |
| `game/GuiCursor` | run | run | run |
| `game/GunKick` | run | run | run |
| `game/HandAim` | run | run | run |
| `game/HandCalibration` | ungated | ungated | ungated |
| `game/HandTargets` | ungated | ungated | ungated |
| `game/HeadSim` | ungated | ungated | ungated |
| `game/HeadTracking` | run | run | run |
| `game/HoloProbe` | run | out | out |
| `game/Holsters` | run | run | run |
| `game/HudPieces` | ungated | run | ungated |
| `game/Interact` | run | run | run |
| `game/InventoryProbe` | run | unreferenced | unreferenced |
| `game/Le2Traversal` | out | run | out |
| `game/Le3GalaxyCapture` | ungated | ungated | ungated |
| `game/Le3GalaxyRetention` | out | out | run |
| `game/Le3Gui` | unbuilt | unbuilt | run |
| `game/Le3Traversal` | out | out | run |
| `game/Mako` | run | out | out |
| `game/MakoControls` | run | out | out |
| `game/MakoGunner` | run | out | out |
| `game/Melee` | out | out | run |
| `game/MenuPause` | run | run | run |
| `game/OmniTool` | run | run | run |
| `game/Player` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/Posture` | run | run | run |
| `game/PowerWheel` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/Reflections` | run | run | run |
| `game/Reload` | ungated | run | run |
| `game/SceneCaptureRig` | run | out | run |
| `game/ScopeCapture` | run | out | out |
| `game/ScopeParts` | run | run | run |
| `game/ScreenReticle` | run | run | out |
| `game/Sights` | run | run | run |
| `game/SkeletonProbe` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/SoundPlace` | run | ready | ready |
| `game/Spectator` | run | out | out |
| `game/Sprint` | run | run | run |
| `game/SquadAck` | run | ready | ready |
| `game/SquadContext` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/SquadOrders` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/SquadPlace` | run | run | run |
| `game/SquadWeapons` | run | run | run |
| `game/SupportGrip` | ungated | ungated | ungated |
| `game/SystemSettings` | ungated | ungated | ungated |
| `game/Thespian` | run | run | run |
| `game/Throwables` | run | out | out |
| `game/Traversal` | ungated | run | ungated |
| `game/UiWorldFigure` | run | out | out |
| `game/Vent` | run | out | out |
| `game/ViewAnchor` | ungated | ungated | ungated |
| `game/ViewLevel` | run | run | run |
| `game/Voice` | run | run | run |
| `game/VrCover` | run | run | run |
| `game/Weapon` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/WorldUi` | run | run (ME1-only handles) | run (ME1-only handles) |
<!-- /parity:modules -->

### The three MevrBridge vocabularies

<!-- parity:seams -->
The common vocabulary (`namespace MevrSeams`, `src/halcyon/MevrSeams.h`): `yes` where the tree calls it (through an old name's forwarder too), `-` where it does not yet.

| MevrSeams seam | LE1 | LE2 | LE3 |
|---|---|---|---|
| `CineScreenFullRate` | yes | yes | yes |
| `CineScreenLayers` | - | yes | yes |
| `CineScreenWanted` | yes | yes | yes |
| `CopyIntoImage` | - | yes | yes |
| `EyeImageSize` | - | yes | yes |
| `FloorViewTranslation` | yes | yes | yes |
| `HudPanelDraw` | - | yes | yes |
| `KeyDown` | yes | yes | yes |
| `LifeSizeStereo` | - | yes | yes |
| `MainMenuOpen` | yes | yes | yes |
| `MergeMotionControllers` | yes | yes | yes |
| `NoteStereoSubmit` | yes | yes | yes |
| `OnEyeCopied` | - | yes | yes |
| `OnEyeReleased` | - | yes | yes |
| `OnEyeSource` | - | yes | yes |
| `OnFactory` | - | yes | yes |
| `OnFrameBegun` | yes | yes | yes |
| `OnGameThread` | yes | yes | yes |
| `OnMenuKey` | yes | yes | yes |
| `OnRecentered` | yes | yes | yes |
| `OnSessionCreated` | yes | yes | yes |
| `OnSessionStopped` | yes | yes | yes |
| `OnViewBuilt` | yes | yes | yes |
| `OverlayLayers` | yes | yes | yes |
| `PerEyeViewState` | - | yes | yes |
| `PerfDraw` | - | yes | yes |
| `PerfFirstEyeCaptured` | - | yes | yes |
| `PerfPresentBegin` | - | - | - |
| `PerfPresentEnd` | - | - | - |
| `PlacePanel` | yes | yes | yes |
| `PlanStereo` | yes | yes | yes |
| `QueueProcessEvent` | yes | yes | yes |
| `RecenterOrientation` | yes | yes | yes |
| `StaticXrGetInstanceProcAddr` | yes | yes | yes |
| `TransitionFadeLayer` | yes | yes | yes |
| `WrapSessionFunctions` | yes | yes | yes |
| `XrRuntimeIsSteamVr` | - | yes | yes |

Old names still forwarded to the common seams (inline, in the tree's `mevr_bridge.h`; removed once nobody calls them):

- LE2: `HotkeyDown`

Each renderer's own seams (`MevrBridge`, its tree's `mevr_bridge.h`): the per-renderer extensions.

| MevrBridge seam | LE1 | LE2 | LE3 |
|---|---|---|---|
| `BuildMenuTabs` | yes | - | - |
| `CineScreenHalfSeparationUU` | - | yes | yes |
| `CineScreenPointerPlane` | yes | - | - |
| `CineScreenQuadsNow` | yes | - | - |
| `CinemaBackdropLayer` | yes | - | - |
| `CockpitLook` | yes | - | - |
| `ConstantFpEye` | yes | - | - |
| `ControllerAimActive` | yes | - | - |
| `ConversationHandover` | yes | - | - |
| `ConversationInWorld` | yes | - | - |
| `ConversationWorldOrComing` | yes | - | - |
| `DrawMenuPointer` | yes | - | - |
| `EarlyFrameWait` | yes | - | - |
| `EyeColourActive` | yes | - | - |
| `EyeColourDone` | yes | - | - |
| `EyeColourShader` | yes | - | - |
| `EyeColourShaders` | yes | - | - |
| `EyeUpscale` | yes | - | - |
| `FindPass1View` | declared | - | - |
| `FirstPersonAtBoot` | yes | - | - |
| `FloorLockEnabled` | yes | - | - |
| `FloorViewLook` | yes | - | - |
| `ForwardLeanEmphasis` | yes | - | - |
| `FrontEndBeforeTitle` | yes | - | - |
| `FrontEndHold` | yes | - | - |
| `FrontEndOnScreen` | yes | - | - |
| `FullFrameLoop` | yes | - | - |
| `GalaxyHeld` | yes | - | - |
| `GalaxyHologram` | yes | - | - |
| `GalaxyProjectionSwapBegin` | yes | - | - |
| `GalaxyProjectionSwapEnd` | yes | - | - |
| `GalaxyViewSwapBegin` | yes | - | - |
| `GalaxyViewSwapEnd` | yes | - | - |
| `HdrDisplayDesc` | yes | - | - |
| `HoldPlainVr` | yes | - | - |
| `HudPanelArmed` | yes | - | - |
| `HudPanelBlend` | yes | - | - |
| `HudPanelCapturesEarlyUi` | yes | - | - |
| `HudPanelDrawsLastFrame` | yes | - | - |
| `HudPanelIsFloat` | yes | - | - |
| `HudPanelNoWrites` | yes | - | - |
| `HudPanelPresentBegin` | yes | - | - |
| `HudPanelPresentDesktop` | yes | - | - |
| `HudPanelPresentDraw` | yes | - | - |
| `HudPanelPresentEnd` | yes | - | - |
| `HudPanelTarget` | yes | - | - |
| `HudPanelTintBlends` | yes | - | - |
| `HudPanelTintTarget` | yes | - | - |
| `InterpolateDestinationWritten` | yes | - | - |
| `JournalFloorViewWrite` | yes | yes | - |
| `MenuMouseFromOs` | yes | - | - |
| `ModernBaseline` | yes | - | - |
| `NewestPass1View` | declared | - | - |
| `NoteCineScreenSubmit` | yes | - | - |
| `NoteDrawArm` | yes | - | - |
| `NoteHudPanelDraw` | yes | - | - |
| `NoteHudPanelTint` | yes | - | - |
| `NotePixelShaderCreated` | yes | - | - |
| `OnFlatQuad` | yes | - | - |
| `OnGalaxyHologram` | yes | - | - |
| `OnMenuQuad` | yes | - | - |
| `OnPresent` | - | - | yes |
| `OnSwapChainPresent` | - | yes | - |
| `Pass1Records` | declared | - | - |
| `PerEyeViewState` | yes | - | - |
| `PerfPass0Captured` | yes | - | - |
| `PerfPoint` | yes | - | - |
| `PerfPresentBegin` | declared | - | - |
| `PerfPresentEnd` | declared | - | - |
| `ProbeTextureAlpha` | yes | - | - |
| `QuestPacingEnabled` | yes | - | - |
| `QuestPacingFrame` | yes | - | - |
| `QuestPacingInit` | yes | - | - |
| `QuestPacingNextFrame` | yes | - | - |
| `QuestPacingStopped` | yes | - | - |
| `RecordClearDepthStencilView` | yes | - | - |
| `RecordDraw` | yes | - | - |
| `RecordDrawAuto` | yes | - | - |
| `RecordDrawIndexed` | yes | - | - |
| `RecordDrawIndexedInstanced` | yes | - | - |
| `RecordDrawInstanced` | yes | - | - |
| `RecordPass1View` | yes | - | - |
| `RecordUpdateSubresource` | yes | - | - |
| `RequestHudPanelReplace` | yes | - | - |
| `SampleMenuPointer` | yes | - | - |
| `SetGameWindow` | yes | - | - |
| `SfrReplayRetry` | - | yes | - |
| `SpectatorMirror` | yes | - | - |
| `StereoFrameActive` | declared | - | - |
| `WorldUiActive` | yes | - | - |
| `WorldUiHoldFrame` | yes | - | - |
| `WorldUiOwnership` | yes | - | - |
| `WorldUiOwnsGui` | - | yes | - |
| `WorldUiPresent` | yes | - | - |
<!-- /parity:seams -->

### Engine names the sequels lack

Needs LExSDKv2 (the submodule, or the main checkout's). This lists only what a sequel build itself tries: names it
declares with `ue3::kEveryGame` handles that the game's SDK list lacks.

The rest moves with every ME1 edit, so it is not committed. `python tools/parity-ledger.py --names` prints it:
- each sequel build's other literals that the game lacks: ME1-reach handles, inert there, and CallFunction paths;
- ME1's build's names that the sequel lacks: the porting gap, per module. `python tools/le2-name-audit.py --game LE3
  game/<Module>` gives the same for one module, with the sequel's names of the same short name.

<!-- parity:names -->
Handles a sequel build declares `ue3::kEveryGame` whose name that game's SDK list lacks (the lookup is tried there: a failure, or a gap in the generated SDK list; ReflectCheck decides):

- LE3 `game/GameMenuRequest`: `Engine.BioSFHandler.HandleInputEvent`, `Engine.BioSFManager:m_FocusPanel`, `Engine.BioSFPanel:m_DefaultHandler`, `Engine.BioSFPanel:nmTag`, `Engine.PlayerController:m_oCachedScaleFormManager`, `SFXGame.MassEffectGuiManager.GetBrowserHandler`, `SFXGame.MassEffectGuiManager:m_oBWHandler`
- LE3 `game/GuiCursor`: `Engine.BioSFManager:m_FocusPanel`, `Engine.PlayerController:m_oCachedScaleFormManager`
- LE2 `game/OmniTool`: `Engine.StaticMesh:LODInfo`
- LE3 `game/PowerWheel`: `SFXGame.BioPawn.IsHostile`, `SFXGame.BioPawn:m_oBehavior`
- LE2 `game/SquadContext`: `SFXGame.BioPawn.IsInCombatActionState`
- LE3 `game/SquadContext`: `SFXGame.BioActorBehavior:ActorGameName`, `SFXGame.BioPawn.IsHostile`, `SFXGame.BioPawn.IsInCombatActionState`, `SFXGame.BioPawn:m_oBehavior`
- LE3 `game/SquadWeapons`: `SFXGame.BioActorBehavior:ActorGameName`, `SFXGame.BioBaseSquad.GetMember`, `SFXGame.BioBaseSquad.GetSquadSize`, `SFXGame.BioPawn:m_oBehavior`
- LE3 `game/Thespian`: `Engine.BioSFHandler:oPanel`, `Engine.BioSFPanel.InvokeMethod`, `SFXGame.BioConversation.GetEntryText`, `SFXGame.BioConversation.GetReplyText`, `SFXGame.BioWorldInfo.EndCurrentConversation`
- LE3 `game/VrCover`: `SFXGame.BioPawn.IsHostile`
- LE3 `game/WorldUi`: `Engine.BioSFManager:m_FocusPanel`, `Engine.BioSFManager:m_lstPanels`, `Engine.BioSFPanel:m_DefaultHandler`, `Engine.BioSFPanel:nmTag`, `Engine.PlayerController:m_oCachedScaleFormManager`
<!-- /parity:names -->
