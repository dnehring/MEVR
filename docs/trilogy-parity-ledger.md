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
94 rows.

| game | shared | adapted | forked | missing | n/a | present and verified (V) | present, unverified (U) |
|---|---|---|---|---|---|---|---|
| LE2 | 18 | 55 | 2 | 13 | 5 | 59 | 16 |
| LE3 | 18 | 54 | 3 | 11 | 7 | 36 | 39 |

Cells not in the ledger's form (`**kind** V|U ...`):

- G18 LE2: "**adapted** (part) V: since 0601ef40 sec"
- G18 LE3: "**adapted** (part) V: the constant eye ("
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
3. **The galaxy map: one design, one flow per ME1 and the sequels** (Forks 3, G25).
   - ME1: projector hologram and stereo window (`game/GalaxyMap`, `plugin/GalaxyHologram`) on the shared
     `game/GalaxyProjector`.
   - ME2: since 2026-11-17 (lane GX2) ME1's flow through the sequels' `game/GalaxyProjectorFlow` (ME2's adapter
     `game/Le2GalaxyCapture`); its opt-in flat table (`NativeTablePanel` via `PlaceGalaxyPanel`) retired.
   - ME3: since 2026-11-17 (lane GX4) on the same `game/GalaxyProjectorFlow` (ME3's adapter `game/Le3GalaxyCapture`: the
     CIC's art, its room lease `Le3GalaxyRetention`, the map's level); GX3's rules (travel, interlude, reservation, bridges,
     recenter) are the flow's for both sequels. Still ME3's own copy of the old table placement and its own input lease in
     `HalcyonLe3Bridge`.
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
     the right stick click's hold opens the power wheel, a tap casts nothing (ME1's rule, 2026-11-17), and that press never reaches the game as R3 (the owner, 2026-11-17: the sequels' R3 opens the area map).
   - All of this code exists for ME1; the sequels need adapters, not designs.
6. **Squad weapons and the sequel power roster were forked** (Forks 5, 6). **Unified 2026-11-16 (lane GF):** one menu
   tick (`SquadWeapons.cpp` `TickCustom`) over a per-game `native` adapter, and one roster walk
   (`game/SequelPowers.hpp`). **Since 2026-11-18 there is no drawn menu at all** (the owner): the left trigger is the game's
   own weapon wheel on every game (`squad_menu::RouteTrigger`); what is left is the in-game check on each sequel (G30).
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

The first-person conversations (G36) were the largest gap by line count (about 10,000 `Ready()`-gated lines of ME1's), left
off this list because the owner had deferred them; since 2026-11-19 they are release scope on both sequels (docs/trilogy-plan.md
T49), built on the sequels' own module and ME1's shared rules rather than ME1's file.

## Renderer and performance

**Groups** say what porting an ME1 technique takes:
- **1**: already game-independent (D3D11/OpenXR/maths); it runs on any host once its call site exists.
- **2**: the same technique, needing a per-game hook point, address table or renderer seam.
- **3**: a fix for Halcyon ME1's own bug or for our LE1 workaround; port it only if the bug shows there.

The [technique groups](#renderer-techniques-by-group) summary follows the table.

| id | technique (LE1 code) | group | LE2 | LE3 | check / next step |
|---|---|---|---|---|---|
| R01 | Two-pass stereo: Halcyon's Stereo 2, two `FViewportClient::Draw` per present (`src/halcyon/le1` render_hook.cpp, xr_session.cpp) | 2 | **adapted** V: upstream ME2 SFR (`src/halcyon/le2/calcview_hook.cpp` DrawDetour, `t_replay`), pinned by `ApplyMevrBaseline`; separate eyes in run `20261115-165553-786-LE2-renderer-preview` (d93d39de). Its `[NOOPRETRY]` draws pass 1 again on 4-20% of frames (a pass-1 Draw under 100 us that did render), a third scene render of 2-9 ms: `[Performance] sfrReplayRetry=0` (lane SP) removes it, no frame lost its second eye (research/sequel-render-levers.md "Performance research"). With the world paused the retry fires on 94-98% of frames (lane SP2: render work 12.9 ms a frame against 3 with it off; the wheels and our menu pause the world). **`sfrReplayRetry = 0` is ME2's default since 2026-11-17** (the owner's yes; lane DEF, `RenderLeversTable.hpp`'s `sfrReplayRetryDefault`) | **adapted** V: upstream ME3 SFR (le3 calcview_hook.cpp DrawDetour); 304 first-eye captures for 304 presents, run `20261115-162209-606` (research/le3-renderer-preview-evidence.md) | Physical fusion in a headset. Add an LE3 line to notes/headset-checklist.md (it has none). |
| R02 | One-Draw stereo, `[Performance] sharePass = 5`. `plugin/RenderLevers.cpp`: SceneRenderHook on FSceneRenderer::Render `0x76EE20`, FinishHook `0x76DD10` → SynthesiseSecondView, tail start `0x77BE00`, deleter `0x4ED700`. Seams `RecordPass1View`, `g_oneDrawStereo` | 2 | **missing**: DrawDetour always runs both Draws. The module is in the host, every row is in ME2's table (lane R2; the stride is 0x11A0), and `sharePass` stands down for want of the renderer's seams | **missing**: same; lane R3 found every row and layout (the stride is 0x11B0) | The host side is shared (`RenderLeversTable.hpp`). Per game it needs: the rows of the `sharePass` group under LE1's names; `sharePassVerified`; and in Halcyon's tree a RecordPass1View in the SFR branch, a skip of DrawDetour's replay Draw, `StereoFrameActive` and a pass-0 capture between the finish and the replay (research/sequel-render-levers.md "One Draw a frame on the sequels"). The per-eye frustum (R06) first. Then A/B with R26. |
| R03 | Second-eye D3D replay (`plugin/DrawReplay.cpp`, sharePass 2/3/5; `Record*` seams in le1 d3d_capture.cpp) | 1 (recorder) / 2 (tail and finish) | **missing**: `plugin/DrawReplay` is compiled in and never attached (nothing feeds it) | **missing**: same | Port with R02: the `Record*` seams in each sequel `d3d_capture.cpp` draw hook. |
| R04 | Replay screen-space matrices (`core/vr/ScreenMatrix`, `core/d3d11/ShaderReflect`, seam `NotePixelShaderCreated` on CreatePixelShader, `replayScreenMatrices`) | 1 + seam | **missing** (moot without R03) | **missing** | Not determined: whether ME2/ME3 shaders take `ScreenToShadowMatrix`/`ScreenToWorld`. |
| R05 | Replay buffer-wrap shadows (DrawReplay's Map hook, `ShadowFor`) | 1 | **missing** (moot without R03) | **missing** | Comes with R03. |
| R06 | Each eye's own off-centre frustum (`oneDrawWindow = 0`, `sfr2MetricEyeFrustum`; render_hook.cpp `ApplyMetricFrustumSEH` at view +0x1D0) | 2 | **adapted** V: `src/halcyon/MevrMetricView.h` `ApplyEyeFrustum` in the SFR branch (one header for both sequel trees), the window widened to hold both eyes (`[LINKFOV2]`'s cover on every runtime with life-size); fake headset, Save 4: `pass 0 frustum ... the CPU window +-1.0000 x +-1.1250 holds it`, far scenery +2 to +7 px, the HUD panel pixel-identical (research/30 9.3, lane LS 2026-11-16); a view whose CPU window can't hold the eyes' frusta (ME2's narrow view without a weapon) renders that pair on the symmetric window, and the latch never takes upstream's mode 0 (research/30 9.4, lane FXR 2026-11-17, `tests/MetricViewTests.cpp`) | **adapted** V: the same header and cover, **on by default again since the owner's answer to question 17** (2026-11-17, lane DEF2: `MELE3VR.ini [VR] MetricEyeFrustum`, LE1's `sfr2MetricEyeFrustum`, 1 by default, not pinned; lane DEF's 0 lasted the afternoon) with lane FL's flare fix on by default (`plugin/FlareWindow`, ME3's host only: `0x26B780` unprojects through `+0x210`, the eye's in mode 2, so for that one call it gets the CPU window's own inverse; `[Stereo] flareWindow`, 1 when missing; research/le3-flares-and-window.md) and a 3072x2216 render (`RenderW/RenderH`' code default; `src/halcyon/le3/runtime/MELE3VR.ini`, installed by the deploy and Setup). Save 31 with the PS VR2's eyes replayed (run `def2-lane\runs\LE3-20261117-144209`): `stereo mode 2`, each eye 19.2 x 14.6 px/deg, the lamps' flares on their lamps in both eyes (off again with `flareWindow = 0`), disparities as FL's and FXR's. The eyes' union window (`[Stereo] windowEyes`, both trees) stays off until a HUD check (question 17 (b)) | The PS VR2's eyes replayed on both (lane FXR, research/30 9.4): ME2 parallel (+0.06 deg per eye), nothing missing at either outer edge; ME3 in mode 2 crossed +0.74 deg in a 2.4 m room, its lens flares on their lamps only through `flareWindow` (research/le3-flares-and-window.md). With the aspect window at 3072x2216 ME3's CPU (culling) window is +-1.8418 x +-2.6571, twice the eyes' height: `windowEyes` after a look at the HUD panel in a scene with a HUD (question 17 (b)). Then the headset: a lamp-lit room, nothing popping at an outer edge; the wider culling window's cost (lane SP). LE1's `ApplyMetricFrustumSEH` is still its own older copy of the same edit. |
| R07 | Umbra levers: `umbraEyeMatrix` (IssueOcclusionQueries `0x8B6550`), `umbraQueryWait` (`0x8C34C0`), `perEyeOcclusion` (view state +0xD10), `umbraSharedVisibility`, `umbraSecondEyeClearBits` | 2 | **adapted** V: the shared module on ME2's rows (lane R2). On since run `20261116-141434-005-LE2-pl-levers`: the commander's timing and the query-read counts, the idle `umbraEyeMatrix` hook. `umbraQueryWait` works and is left at ME1's 1. Measured there: the second eye spins 2 to 4.5 ms a frame for Umbra results that are never ready; 0 removes it (research/sequel-render-levers.md "ME2"). Lane SP (stretches `LE2-20261116-162623`, `-170323`): the spin is not the per-eye state (`perEyeViewState=0`, now a lever on the sequels with a per-game default, keeps it and adds UE3's wait); `umbraQueryWait=0` halves the render work (10.4 -> 5.0 ms at Freedom's Progress). Lane SP2's batch on a quiet machine after lane LS (stretch `LE2-20261116-201015`): `umbraShareResults=1` with `sfrReplayRetry=0` best and the only option good in both rounds (render work 11.0 -> 5.8 ms, frame p99 21.4 -> 14.3, frames over the period 50% -> 6%, the second eye still occluded), the same picture as today's at a still pose, dynamic shadows included | **adapted** V: the same on ME3's rows (lane R3), run `20261116-143006-726-LE3-pl-levers`; the same spin, 3 to 5 ms a frame; the command gate read 1. Lane SP (stretch `LE3-20261116-174821`, other lanes compiling), repeated by lane SP2 on a quiet machine after lane LS (`LE3-20261116-202316`): `umbraShareResults=1` 11.5 -> 4.8 ms, frames over the period 52% -> 12%, with the second eye still occluded (686 visible instances against 667; `umbraQueryWait=0` 850), noise-level against today at a still pose's occluder edges; the shared state no cure | **The defaults since 2026-11-17** (the owner's yes to research/sequel-render-levers.md "The batch before the headset test"; lane DEF: per-game `umbraShareResultsDefault` and `sfrReplayRetryDefault` in each `kLayout`, an ini key still wins): ME2 `umbraShareResults=1` and `sfrReplayRetry=0`, ME3 `umbraShareResults=1`, ME1 unchanged. Seen with no key in the file on both (stretches `LE2-20261117-120024`, `LE3-20261117-112905`); ME2 at Freedom's Progress against main's old values in the same stretch: render work 10.55 -> 5.80 ms, frames over the period 46% -> 6%, dynamic shadows on (research/sequel-render-levers.md "The defaults landed"). The headset: an occluder's edge while the head turns (the first eye's results stand in for the second eye's for a frame). `perEyeOcclusion` has nothing to cure there; `umbraSharedVisibility` stays unported. |
| R08 | `keepBoundShaderStates`: hook on the FD3D11BoundShaderState constructor `0x9A89F0`, AddRef through GDynamicRHI `0x17938C8` vtable slot 0x8A0 | 2 | **shared** V: on, ME1's default (`boundShaderStateVerified`); run `20261116-141434-005-LE2-pl-levers`: 22-45 states built a frame in heavy views before, 0.0-0.1 after, 161 kept, about 0.4 ms off the first eye there; the reference count 0 -> 1 through the live RHI's slot 0x8A0 (RVA `0x2B2640`, not the `0x992830` of lane R2's recipe) | **shared** V: on; run `20261116-143006-726-LE3-pl-levers`: 86-122 a frame before, 0.0-0.1 after, 436 kept, about 0.8 ms off the second eye in heavy views; AddRef at RVA `0x2CB940` as lane R3's recipe says | Measure again in a heavy scene of each game (research/sequel-render-levers.md "Still to measure"). |
| R09 | `minScreenRadiusPx` (UmbraInstanceVisibleHook `0x8BE0B0`) | 2 | **missing**: hooked and measured in a try build (73-81 of 520-585 instances under 8 px in heavy views, no measurable gain), left off (`umbraInstanceVerified`) until a still-head snapshot pair shows no pop-in | **missing**: the same (102-219 of 315-911; the cull answers only the marking call, `Layout::instanceCallAlsoKeepsBooks`) | Lane SP's still pairs (research/sequel-render-levers.md "Performance research"): at 8 px ME2 loses the moon's glow and a beacon light, ME3 the lights' lens-flare streaks (1.2-1.3% of the pixels). **Stays off on both**; the flag stays false. ME1 runs at 8: worth a look whether its lights lose their flares too. |
| R10 | Motion-blur guard (`plugin/MotionBlurGuard.cpp`: MotionBlurProxy vtable swap, LE1 5 pairs, installed in `Entry.cpp`) | 2 (3 on LE1: settings re-enabled the blur, research/62) | **adapted** V: 5 audited ME2 proxy pairs (`Addresses.hpp` LE2 `kMotionBlurProxyPairs`, lane AU6, d9ba2593); Save 4: installed, 81 conversions in 40 s, 0 unknown, `motionBlurProtected` served (research/le2-motion-blur-guard.md) | **adapted** V: 7 audited ME3 proxy pairs (`Addresses.hpp` LE3 `kMotionBlurProxyPairs`); a different reason (a null velocity buffer crashed the recenter); run `20261115-180239-899`, 2,825 guarded conversions, 0 unknown (research/le3-recenter-transition-failure.md) | The headset: no one-eye smear on a fast turn, on both sequels. |
| R11 | `LightEnvironmentShadows False` while the second eye is a replay (`game/SystemSettings.cpp` `Wanted()`) | 3 (our replay's workaround) | **shared** V: since 2026-11-16 (lane GF2) the entry follows the host's word (HostServices v43 `secondEyeIsReplay` = `render_levers::SecondEyeIsReplay()`: sharePass 2, 3 or 5 where the game has the lever), not the file; where the second eye is its own render the player's own `GamerSettings.ini` value is put back (the game had persisted our False into `BIOEngine.ini`). Seen: `system settings: scale set ..., LightEnvironmentShadows True` on ME2 (session `20261116-155312-554-LE2-gf2-run3`). **What it changes on screen is not shown:** at Freedom's Progress a same-pose A/B of the key alone changes nothing visible, with or without dynamic shadows; the squadmates' shadows on the ground there are `DynamicShadows` (R12's ME1 value, False, keeps them off; on, they are right in both eyes). PL's earlier "contact shadows" pair was not same-pose (research/sequel-render-levers.md "Character shadows on the sequels", 2026-11-16 correction) | **shared** U: the same rule; ME3's `sharePass` stands down, so it gets the player's value; not run | ME3: the `system settings:` line with `LightEnvironmentShadows True` in a run. A scene where the key shows at all (indoors, a light environment's shadow) for a same-pose pair. `DynamicShadows` on the sequels: measured by lane SP2 with each second-eye option, right in both eyes on both games, about 0.4 ms (research/sequel-render-levers.md "The batch before the headset test"); **on by default on ME2 and ME3 since 2026-11-17** (the owner's yes; lane DEF: `engine::adapter::kVrDynamicShadows`, ME1 keeps its cost lever off; ME2 Save 4: Miranda's and her gun's shadows on the snow in both eyes, each at its own place, run `LE2-20261117-120024`). **Off again the same night, and character shadows off always on ME2 and ME3** (the owner's headset test: the shadows differed between the eyes; `kVrDynamicShadows` false for every game, `kVrCharacterShadowsOff` true on the sequels, 9f556c1a; seen off on the fake headset). |
| R12 | SystemSettings applied by the mod (`game/SystemSettings`: `scale set` after each level change, each boolean through its opposite) | 1 | **adapted** V: `#if MEVR_GAME_LE2` waits 3 s into gameplay; A/B runs A–J 2026-11-14 (research/le2-renderer-integration "The first-gameplay-frame fault", 87af915d) | **shared** V: `scale set` acts on LE3 (lane SP2, stretch `LE3-20261116-202316`): the log's `system settings: scale set DynamicShadows True, ...` and a same-pose A/B in which Shepard's shadow appears on the grating in both eyes (0.8% of the pixels); `MotionBlur` and `FoliageDrawRadiusMultiplier` there still unshown (research/sequel-render-levers.md "The batch before the headset test") Lane AU1 (2026-11-18, not run): "out of gameplay" through `engine::adapter::IsHudGameplayMode` (it was `mode > 4`, ME1's range; ME3's gameplay runs to 6). | LE3: an A/B of `MotionBlur` and of `FoliageDrawRadiusMultiplier` in a scene where each shows. The built-in `DynamicShadows` is per game since 2026-11-17 (`engine::adapter::kVrDynamicShadows`: False on ME1, True on ME2 and ME3; `vr-game-settings.ps1` and Setup write False for LE1 only; ctests `mevr-le2-system-settings`, `mevr-le3-system-settings`). |
| R13 | FSR 1 eye upscale (`plugin/Upscale`, `core/vr/Upscale`; seam `EyeUpscale` in BlitTextureToEyeWithFill) | 1 + seam | **adapted** V: the same module and passes (`upscale::Passes`) from `plugin/EyeCopy` (`upscale::Copy` inside `Blit::Run`); eye images larger than the backbuffer through the new seam `MevrSeams::EyeImageSize` in `EnsureAerSwapchains`; at 1280x720 the eyes were 1912x1076, FSR-scaled, twice the bilinear fill's gradient energy at the same mean luma, 0.08 ms of GPU per eye, session `20261116-155924-548-LE2-ey-picture` (research/27 9.5) | **adapted** U: same path; not engaged in the preview (its 3072x1728 backbuffer is wider than the recommended eye); ctest `mevr-eye-copy-warp` | LE3: a run with a backbuffer below the recommended size. The sequels' render-size row in the menu (research/27 9.4). |
| R14 | Colour per headset (`plugin/Colour`, `core/vr/Colour`; LE1 seams `EyeColourShaders/Shader/Done` in Halcyon's eye blit) | 1 | **adapted** V: the same core in `plugin/EyeCopy`'s corrected shader (`MEVR_EYE_OUT`); research/le2-picture.md; 2026-11-16 Quest and a custom grade 100% within one step of the CPU reference, neutral the raw copy bit for bit (research/27 9.5) | **adapted** V: same path; headset set in `HalcyonSequelBridge`'s `OnSessionCreated`; Quest and the custom grade 100% within one step of the CPU reference, neutral bit for bit, session `20261116-161710-117-LE3-ey-me3`; the HUD panel's colour hooks on since 2026-11-16 (`panelColour`), unseen (the preview's panel is off) | LE1 keeps its own call shape onto the shared core (research/27 9.3). |
| R15 | HDR tonemap (`MEVR_TONE` in le1 xr_session.cpp `ToEye`; `[Colour] tonemap`, `toneHue`) | 1 + seam | **adapted** U: `MEVR_TONE` in EyeCopy's corrected `PSPlain` (the identity for an 8-bit source); without LE1's SDR-white scale (`colorScale.x`) and without the clip in the neutral profile (research/27 9.2) | **adapted** U: same | Moot while `hdrDisplay = -1`, the default on all three; the white scale only if the owner brings HDR back. |
| R16 | HDR display answer (LE1 `HalcyonBridge.cpp` `HdrDisplayDesc`, through Halcyon LE1's GetDesc1 patch) and the float frame copied into the eyes | 1 | **adapted** V: one policy, `hdr_display::Answer` in `plugin/HdrDisplayPolicy`, which LE1's seam forwards to (with LE1's one-time move of 0 to -1) and the sequels' own GetDesc1 hook (from `OnFactory`) calls; + `plugin/EyeCopy`; answered SDR, format 28 (research/le2-hdr.md) | **adapted** V: same modules; 8-bit backbuffer in run `20261115-162209-606`; ctest `mevr-eye-copy-warp` | The answer's log line in the next run of each game (`hdr display: ... answered`). |
| R17 | Swapchain budget (`plugin/Swapchains`, `core/vr/SwapchainBudget`) | 1 | **shared** V: `swapchains: gun status ... made` (stretch `20261114-212412-lane-b2-layers`) | **shared** U | LE3: a `[Debug] swapchainCap = N` run, as LE1's of 2026-11-06. |
| R18 | HUD panel (`plugin/HudPanel`; LE1 has 11 `HudPanel*` seams in d3d_capture.cpp via HalcyonOverlays) | 1 (core) / 2 (UI-draw classification) | **adapted** V: one `MevrHudPanelDraw` seam in le2 d3d_capture.cpp's three draw hooks, `PcHud::SetAuthoredLayout`; stretch `20261114-2152..2206-lane-b3-hud` (e03a7364) | **adapted** V: the same `HudPanelDraw` site (`EmitUiDraw`), `hudPanelGameplay` on and ME1's `Hud()` defaults; gameplay modes by `engine::adapter::IsHudGameplayMode`; Save 31: `game UI goes to the body-locked HUD panel`, eyes' world layers without UI, mode 2 by itself (research/le3-hud-panel.md, lane HU) | LE3: the combat HUD (no combat fixture); the crosshair is hidden since lane CH (R19), unseen in combat (T14). |
| R19 | Hand reticle, with the game's crosshair hidden (`plugin/HandReticle`; `game/ScreenReticle`, `hideScreenReticle`) | 1 | **adapted** V: crosshair `_alpha=0` (stretch `20261115-001659-lane-b4-m2`, 30902fc6) Lane AU1 (2026-11-18, not run): the vehicle gate through `engine::adapter::GameMode` (ME2's raw 1 is its power wheel). | **adapted** U: ring through `SequelWeaponLayers`, on LE1's `ControllerAimActive` since lane U3 (seen on the replayed hands, 2026-11-16). Crosshair hidden since lane CH (2026-11-17): `screenReticleHide` on, the sequels' one 500 ms request (`plugin/ScreenReticleRequest.hpp`), and ScreenReticle's LE3 adapter zeroes `_root._alpha` of the GUI manager's `MovieTag_Reticle` movie (`SFXSFHandler_Reticle`), the scope view left to the game (research/le3-hud-panel.md "The crosshair hidden"). On Save 31 the movie was found, zeroed in mode 0 and given back in mode 9 (session `20261117-080352-885-LE3-ch-crosshair`), but the Normandy has no weapon, so no crosshair was seen go: U | LE3: the combat save's check in research/le3-combat-fixtures.md ("The crosshair hidden"): a drawn gun with no crosshair on the `hud` layer, back with `mevr reticles` after the request ends, a sniper's scope kept. |
| R20 | Overlay layer composition (`plugin/HalcyonOverlays.cpp` OverlayLayers) | 1 (modules) | **adapted** V: `plugin/SequelOverlays.cpp` OverlayLayers (one composition for both sequels: capabilities + ME2's adapter in `HalcyonLe2Bridge.cpp`) + `SequelWeaponLayers`/`SequelSquadLayers`/`MenusLe2`; stretch `20261116-150654-945-LE2-u2-le2` (Save 4: both eyes, the `hud` layer, every seam group in the log) | **adapted** U: the same `SequelOverlays.cpp` with ME3's adapter in `HalcyonLe3Bridge.cpp`; ran to the title on the native panel (preview `20261116-151138-682-LE3-u2-le3`), no save loaded since the merge | Forks 1: LE1's own composition is the remaining copy. |
| R21 | Cinema screen, its backdrop and the transition fade (`plugin/CineScreen`, `core/vr/CineScreen`, `CinemaBackdrop`, `TransitionFade`; LE1 seams in xr_session.cpp and PlanStereo's screen branch) | 1 + seams | **adapted** V: `plugin/SequelCinema` through `CineScreenWanted`/`HalfSeparationUU`/`Layers`/`TransitionFadeLayer`; run `20261115-042015-m5-cinema` (research/le2-cinema.md, 1834c240) | **adapted** U: the same seams wired (le3 me2_xr.cpp), Halcyon's own VR cine forced off; no separate-eye cinema acceptance Lane AU1 (2026-11-18, not run): the screen admitted for every corroborated cine whatever its mode (it was 7 and 8 only), and offered by our menu (`renderer::Caps::cineScreen`, read by the menu and `MevrSeams::CineScreenWanted`). | LE3: snapshots in a mode-7 conversation (the LE2 run's script). |
| R22 | Front end and presentation hold (`plugin/PresentationHold`; seams `FrontEndHold`, `HoldPlainVr`, `FrontEndOnScreen`, `FrontEndBeforeTitle`) | 3 | **shared** V: the same module through the common seams (`MevrSeams::FrontEndHold` ... in me2_xr.cpp, `GetMenuMode` answering its state); the title on ME1's 3D screen, the boot and loads flat, plus `HoldLoadTail` (ME2's loads end in 0.3 s of mode 6, shown before as a black 3D screen); lane FE, research/56 "The sequels" | **shared** V: the same, decided first in `d3d_capture.cpp UpdatePresentationState` (`MENU_MONO enter: front end` for its flat part); ME3's title is its Movie mode over a live scene, the front end's GUI by its adapter; the screen's submit doesn't wait for a captured pair | The owner's headset look (launch, title, main menu, a load). `HoldPlainVr` is called by neither sequel: no gameplay blip seen in their front ends. |
| R23 | Metric (life-size) stereo (`plugin/MetricStereo`, `core/vr/MetricStereo`, `PlanStereo`, `NoteStereoSubmit`; `sfr2Metric`) | 2 (seam) on a group-1 core | **adapted** V: the same `plugin/MetricStereo` and core in the host, the common seams `PlanStereo`/`NoteStereoSubmit`/`LifeSizeStereo`; the facts in me2_xr.cpp `MevrPlanStereo`, pinned in `ApplyMevrBaseline` (PosScale 2, the lean from the eyes' centre at 1:1). Fake headset, Save 4: upstream -72 px (-0.90 deg per eye divergent), life-size +2 to +6 px (research/30 9.3, lane LS 2026-11-16) | **adapted** V: the same, pinned in le3 `LoadValues`. Renderer preview, Save 31: upstream +1.86 deg per eye crossed (the walls at a metre), life-size +0.66 (0.65 expected at 2.8 m) (research/30 9.3) | The owner's headset check (notes/headset-checklist.md section 8). |
| R24 | Frame pacing (`plugin/QuestPacing`; seams `EarlyFrameWait`, `FullFrameLoop`, `QuestPacing*`; `pacingRecovery`) | 1 (core) + 2 | **missing**: upstream StereoFramePacing | **missing**: upstream PaceDisplayLocked | Needs an xrWaitFrame seam in each sequel RunFrame (le2 me2_xr.cpp ~1587, le3 ~1395). Test on a streamed Quest. |
| R25 | Test loop: snapshots, recording, replay (`plugin/TestLoop`, seam `WrapSessionFunctions`) | 1 + seam | **adapted** V: run `20261114-191506-lane-b-testloop` (04fa43a2); waitFrame/beginFrame not wrapped | **adapted** V: every protected run, e.g. `20261115-182253-344` (14 captures) | — |
| R26 | Perf probe (`plugin/PerfProbe`, `core/vr/FrameProbe`; seams `PerfPresentScope`, `PerfPass0Captured`, `PerfPoint`) | 1 (core) + seams | **adapted** V: the same `plugin/PerfProbe.cpp` with the sequels' adapter (`PerfProbeSequel.cpp`); 10 captures in run `20261116-141434-005-LE2-pl-levers` (frames bounded by xrEndFrame, GPU timestamps, ticks). With lane U1's `MevrSeams::Perf*` calls (run `LE2-pl-seams`, 14:54): frames bounded by Present, 961 draws a frame; `PerfFirstEyeCaptured` comes 61 draws before Present, so it doesn't split the eyes yet | **adapted** V: same; 10 captures in run `20261116-143006-726-LE3-pl-levers`. With lane U1's seams a capture never started (the Present seam gives no device context); the probe now falls back to the context it finds itself, not yet run in the game | ME3: a run with the probe's context fallback. ME2: `PerfFirstEyeCaptured` at the pass boundary (research/sequel-render-levers.md "The landed configuration with lane U1's seams"). |
| R27 | Delayed per-eye pose history (LE1: Halcyon's EYETAG code in xr_session.cpp; sequels: `core/vr/StereoPoseTag.hpp`, `core/vr/EyePublication`) | 1 + seams | **adapted** V: run `20261115-165553-786` (research/sequel-eye-pose-tags.md) | **adapted** V: runs `20261115-165123-287`, `20261115-174634-476` | Physical fusion. |
| R28 | Recenter: yaw-only, plus a removed tilt (seam `RecenterOrientation` → `plugin/RecenterTilt`, `core/vr/Recenter`) | 1 + seam | **shared** V: `RecenterAppSpace` asks `MevrSeams::RecenterOrientation` (`plugin/RecenterTilt` in the LE2 host, lane U1). The seam ran in session `20261116-123754-740-LE2-u1-le2` (`[MEVR-RECENTER] yaw-only recenter (upstream), automatic`). The correction itself needs a tilted base space, which the fake headset does not give: ME1 code, unseen on ME2 | **shared** U: `RecenterAppSpace` asks `MevrSeams::RecenterOrientation` (`plugin/RecenterTilt` in the LE3 host, lane U1, 2026-11-16). Built; no run since (the yaw-only recenter ran in `20261115-180239-899`) | Call `RecenterOrientation` in each sequel's `RecenterAppSpace`. SteamVR's upside-down start reaches every game (docs/le2-from-le1.md 3.1). |
| R29 | Body bounds for the culling camera (`engine/BodyBounds`: post-hook `0x58EEC0`, Umbra hand-over `0x25CFE0`; ArmIk's `bConsiderAllBodiesForBounds`) | 2 + 3 (cause: Halcyon LE1's GPU-only view offset) | **shared** V (lane AU4, 2026-11-18): `kSkelUpdateBounds`/`kPrimitiveUmbraBounds` are lane R2's `0x798350` and `0x3146C0`, the module on `runtime::Up()`; Save 4 with replayed hands: `hooked USkeletalMeshComponent::UpdateBounds`, both body meshes flagged, `padded by 150 units` (the cap: the sequel host's view offset up is 175 units). Whether ME2 culled the body without it was not checked | **shared** U: lane R3's `0x793850`, `0x32EC10`; the table's rows at their RVAs at attach; the hook not reached in the runs (no tracked hands in the combat fixture) | A look down at the feet with the head turned on each sequel (gotchas/culling-camera-ahead-of-the-eye). |
| R30 | Spectator mirror (`game/Spectator`, `plugin/Spectator`, seam `SpectatorMirror`) | 1 (host draw) + 2 | **missing** | **missing** | — |
| R31 | Scene-capture plumbing (RenderLevers CaptureSceneHook `0x742C80`, BeginBackBufferHook `0x773D30`; UiWorldFigure's Umbra skip `0x7526C0`) | 2 | **missing**: the rows are in ME2's table (`0x329B90`, `0x354570`; lane R2), the hooks stand down (`sceneCaptureVerified`: where ME2's final pass gets its viewport is unread), and nothing in ME2 makes a capture | **forked** V: `plugin/Le3SceneCapture` (its own RVAs; shares `plugin/SceneCaptureTarget.hpp`, `core/vr/SceneCaptureScope`); raw map captures in run `20261115-220823-943` (research/le3-galaxy.md) | Forks 9. |
| R32 | Pinned renderer baseline (LE1 `MevrBridge::ModernBaseline`: stereo, flat menus, first person, no head aim) | per renderer | **adapted** V: `ApplyMevrBaseline` (le2 me2_menu.cpp) after every ini load; first run 2026-11-14 (research/le2-renderer-integration) | **adapted** V: `ApplyMevrBaseline` (le3 me2_menu.cpp) after every ini load since 2026-11-17 (lane FP3), ME2's list: stereo, the Quest image fix, the view filled, lean, look speed 1, flat menus, first person with the head hidden (upstream ME3 started in third person; `engine_probe.cpp`'s first person now on from the start), no head aim, no stick pitch, no VR conversations or cutscenes, no profile hotkeys, no R recenter, the menu key `\`; K still toggles first person; since lane FX3 (2026-11-17) also first person in cover (upstream's `CoverThirdPerson`, default 1, went third person there; U: no cover on Save 31). Run on Save 31: `[MEVR-BASELINE] 3 setting(s) ...` (the preview's ini already had the rest), first person in every snapshot (research/trilogy-first-person.md). The upstream experimental controls stay compiled out (`#if !defined(MEVR_ASI)`) | Per renderer by nature. |
| R33 | Hotkeys only with the game in front (`MevrBridge::KeyDown`) | 1 | **adapted** U: `MevrBridge::HotkeyDown` (le2 mevr_bridge.h) | **adapted** U: `KeyDown` in le3 d3d_capture.cpp | Type the key in another window during a run; nothing should toggle. |
| R34 | Halcyon's script calls queued to the game thread (seam `QueueProcessEvent`, `plugin/GameThreadCalls`; research/36) | 3 (Halcyon's Present-thread ProcessEvents; the sequels have the same pattern) | **shared** V: "stereo and the queue work", first run 2026-11-14 (CLAUDE.md) | **shared** U: le3 engine_probe.cpp:282 queues | LE3: the host's `process event: off the game thread:` count in a run log. |
| R35 | Performance modes and the first start's eye size (`core/vr/PerformanceModes`, `MainMenuSettings` Display > Performance, `FirstStartEyeSize`) | menu | **adapted** V for the eye size (lane AU4, 2026-11-18): `FirstStartEyeSize` writes 0.85 of the eye at ME2's shape (`core/vr/RenderTarget`: sides in multiples of 8, at least 16:9, ME2 draws no HUD on a near-square frame) into `MELE2VR.ini` and GamerSettings for the next launch (`renderer::SetRenderTarget`); seen: 1912x1952 recommended -> 2944x1656, the next launch rendered it with the whole HUD; Display's Render resolution and Upscale rows work. V (lane AU9): 16:9 kept over 4:3 (at 3264x2448 the power wheel at the hand cut out the frame's top band; 5% less GPU; research/le3-flares-and-window.md section 6.1). **missing**: the Performance modes (ME1's levers) | **adapted** U: the same rule, sides in multiples of 8 (ME3 draws its HUD at 1624x1656, combat fixture); its write through `MevrSetRenderTarget` not run (the preview pins `eyeSizeSet`); the size itself seen. **missing**: the modes | The PS VR2's size in a headset. |

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
- The spectator's host draw, and the scope capture source's texture search (`ScopeCaptureSource`: UTexture.Resource +0xA0 → +0x8).

**Group 2 (per-game hook, address or seam).**
- One-Draw stereo (RenderLevers: `0x76EE20`, `0x76DD10`, `0x77BE00`, `0x4ED700`; views field and clone stride) and the
  replay's tail and finish boundaries. The address table is in place: `plugin/RenderLeversTable.hpp` finds each row of
  `engine/Addresses.hpp` by LE1's row name in the current game's table.
- The per-eye frustum (a CalcSceneView seam; the view offsets match in all three trees).
- The Umbra levers (`0x8B6550`, `0x8C34C0`, `0x9A04B0`, view state +0xD10) and `minScreenRadiusPx` (`0x8BE0B0`).
- `keepBoundShaderStates` (`0x9A89F0`, GDynamicRHI `0x17938C8` slot 0x8A0).
- The motion-blur guard (a per-game proxy-pair table; done for LE3 and LE2).
- Metric stereo (a seam in the SFR branch).
- Frame pacing (a seam at xrWaitFrame, which depends on the RunFrame order).
- The perf probe's seams; the HUD's UI-draw classification and crops.
- Body bounds (`0x58EEC0`, `0x25CFE0`); the scene-capture hooks (`0x742C80`, `0x773D30`, `0x7526C0`; LE3 has its own,
  `plugin/Le3SceneCapture`).

**Group 3 (Halcyon ME1's bugs or our LE1 workarounds): port only if the bug shows.**
- **The presentation hold.** Halcyon ME1's unreadable mode byte, logos movie and title camera-flight cine latch. The
  sequels showed it too (lane FE, 2026-11-18): ME2's start began in VR for 0.2 s and its loads ended on a black 3D screen, and
  neither put its title on the 3D screen. Ported: R22.
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
| G01 | VRIK arm IK (`game/ArmIk`, `engine/BonePose`, `core/ik`; docs/vrik-porting-guide.md) | **adapted** V: LE2 `kUpdateSkelPose 0x79B8A0`; research/le2-vrik.md "The run" 2026-11-14 (141fd1d5, 965321e2): wrists 0.07 m off, skeleton audit within 3 cm. Root-motion steps (shared, `core/ik/GoalStep`): the goals follow the `Camera` bone; three vaults with every tick within 0.08 m of the controllers (0.70-1.04 m before; stretch `20261115-010524-lane-c2-vault`, research/le2-vrik.md). The anchor (lane ANC, 2026-11-18, shared, U on every game, research/29 "The crouch that went third person"): with the floor lock (or a crouch on foot) only the horizontal distance to the animated head decides (`ik::FirstPersonAnchor`); every crouch of the owner's ME2 session had left first person at 75 units and the drawn body jumped 0.5-0.76 m (his "almost magnetic" cover) | **adapted** V: LE3 layout, `kUpdateSkelPose 0x796EB0`, `MenuBodyEligible`; runs `20261115-182253-344`, `20261115-183117-290` (research/le3-body-pose.md). The root-motion rule is the shared one, U on ME3 | Headset: hands on the controllers (snapshot `mod.armIk.hands`). ME2: `tools/le2-halcyon-run/crouch-anchor.ps1 -Phases button,physical` (no `body anchor: third person` through a crouch). |
| G02 | Hand aim and the aim ray (`game/HandAim`) | **adapted** V: `SFXWeapon.GetAdjustedAim` post-handler; 34 shots 0.4–0.8° from the barrel (le2-vrik) | **adapted** U: LE3 branches, on our layer only (no LExSDKv2 fallback on either sequel since T04) | LE3: a combat fixture (research/le3-combat-fixtures.md). |
| G03 | Fingers (`game/Fingers`, `core/ik/Fingers`) | **shared** U | **shared** U | Snapshot `mod.fingers` with a replayed trigger and grip on each game's skeleton. |
| G04 | The drawn gun (`game/Weapon`) | **adapted** V: muzzle against `GetMuzzleLoc`, 0.00 units (le2-vrik) | **adapted** U: LE3 weapon classes, the SMG as its own `RangedType` | LE3: a combat fixture. |
| G05 | Support grip, two-handed (`game/SupportGrip`, `plugin/PadMerge` v7/v11) | **adapted** U: no LT from the grip on the sequels (`engine::adapter::kSupportGripHoldsLt`); the shouldered shot's own calls zoomed (`game/ZoomedShots`, lane AU8, be1a47ba), V with the rule forced: 1.25 -> 0.375 aim error per pistol shot | **adapted** U: the same; V forced: the Avenger 3.63 -> 0.40, the Black Widow's damage 257 -> 514 | A replay with the left grip in the zone, then snapshot `supportGrip`. |
| G06 | Sights: red dot, holo, ADS, alignment assist (`game/Sights`, `plugin/SightLayers`, `core/vr/Ads`) | **shared** V: the shoulder lock only with both hands on a two-handed gun on every game since 2026-11-18 (`vr::ads::MayShoulder`, research/21 section 23; the owner); one path with ME1 since lane C2's step 2 (the sight line's trace, convergence, the assist, a scope's aimed fire through `SFXWeapon.GetAimError` with `SFXWeapon_NativeBase.bIsZoomed` set for the call; `SFXWeapon.ZoomFOV`). Run on the fake headset 2026-11-16 (the pistol's dot in the right eye, shots on what the sight line hits; the sniper rifle's ADS, scope picture and aimed fire 10.0 -> 0.0): research/le2-vrik.md. The headset is the checklist's | **shared** U: the same path (`SFXWeapon.GetPlayerAimError(Accuracy)`); zoom steps (`AimModes`) not read; the SMG borrows the rifle profile | ME2: `tools/le2-halcyon-run/sights.ps1` (run 2026-11-16, LE1 at the same pose unchanged). ME3: a weapon fixture, then the same script's steps. research/le2-le3-vr-optics.md steps 1–3. Since 2026-11-18 (lane AU2) the sniper rifle shows ME1's 3.5x and 7x after a flick on both sequels (the owner: one rule); the zoomed render (scope capture) is ME1's only. |
| G07 | Scope: picture-in-picture, virtual eyepiece, scope parts, scope capture (`game/ScopeParts`, `game/ScopeCapture`, `plugin/ScopeCaptureSource`) | **adapted** V: ME1's ScopeCapture on every game since 0601ef40 (lane AU3), the host serves `setScopeCapture`; Save 4's Mantis: `sights.pictureSource` `capture` at 3.5x and 7x. Adapter: no post-process chain on the sequels' captures (`kCapturePostProcess`), the pose the publish before the latest (no pair arm). `ScopeParts` shared (U) | **adapted** U: the same code (ScopeCapture on every game, LE3's host serves `setScopeCapture`); not seen in ME3: the combat fixture carries a Black Widow since lane AU5 (`me3-combat.ps1 -Loadout '2 1 all'`), but ADS is never reached: arm IK stays third person in the combat pose (the view 53 units from Shepard's head bone, G18) | Per-model optic profiles; a pair-arm seam for the capture's pose; the post chain once R31's final-pass hook is ME2's. |
| G08 | Holsters and "only holsters draw" (`game/Holsters`, `core/ik/BodySlots`) | **adapted** V: one module, ME2's names in its adapter (the inventory chain by family, `SwitchWeapon` + `SetActionState(1)`, `TryHolster`, `ActionState`); only holsters draw by one rule (`RefuseGameDraw`: `IsCombatEnabled` answered false inside FireWeapon, TightAim, TryReload, Next/PrevWeapon). Run 2026-11-16: the rifle at the shoulder and the pistol at the hip drawn and holstered, four trigger pulls with the weapon away refused, the right grip holding the gun through three vaults (research/le2-vrik.md "Holsters on the sequels"); ME1 at the same pose unchanged. Since 2026-11-16 (lane WP): the release grace seen (back after 0.20 s, holstered after 0.93 s); the SMG's slot `thighRight` (a fifth `BodySlots` slot, off on ME1; not run: Save 4's Soldier carries no SMG). Since 2026-11-17 (lane SG): `[Holsters] smgSlot = lowerBack` the other place, from the games' own lower-back socket (`EASlot_LowerBack`, the shotgun's; the games hang the SMG at the pistol's holster, the left side of the waist), a right hand lost there kept 0.75 s; `thighRight` the default until the owner chooses (research/le2-vrik.md "The SMG at the small of the back"). Since 2026-11-17 (lane FX2, research/le2-vrik.md "The sequels' controls after the review of 57ae5b4e"): ME1's right stick click hold opens the power wheel (`SFXGameModeDefault.EnterPowerWheel`, the click never RB: a tap no longer quick-casts power 2), the grip holds in the two wheels as on ME1, a grab where `IsCombatEnabled` is false is refused (the Normandy, Save 36), a holster TryHolster would refuse (in combat) is felt as a refusal, not the click; run before and after on Save 4. Since 2026-11-17 (lane CTL, the owner's answers to questions 13-15, research/le2-vrik.md "The right stick click, the heavy weapon and ME1's refused draw"): the click's press the mod owns never reaches the game as R3 (ME2's `ShowAreaMap` and objective radar; the host's `keepPowerClick`), the map on the omni-tool's Map entry; the refused draw is one rule with ME1's (ME1 now refuses with the buzz too); ME2's heavy weapon needs no rule (its `SwitchWeapon` keeps it). U since 2026-11-18 (lane CV2, the owner's firefights, research/le2-vrik.md "A gun let go in a fight"): a let-go in combat holsters by TryHolster's own last step (`SetActionState(0)`; `[Holsters] combatHolster`), a busy let-go waits up to 4 s, a draw nobody asked for (the wheels' close, a fight's start, a script) is put back unless a power needs it | **adapted** V (2026-11-18, lane AU2, on the combat fixture: the rifle put away at the shoulder in 0.95 s, four trigger pulls refused, the pistol drawn at the hip and put away again, the release grace; `[Holsters] le3PutAway = 1` by default, the owner):  no holstered state on a mission (its script): TryHolster never called (it only leaves cover); the holster is the game's put-down into no weapon (`SFXInventoryManager.SetCurrentWeapon(None)`) behind `[Holsters] le3PutAway`, 0 by default; only holsters draw is ME2's rule with "away" = weapons carried, none equipped; Save 31 run: names resolve (`reflect: 710 of 710`), the non-combat pawn carries nothing, nothing drawn or refused. Since 2026-11-17 (lane FX2): the right stick click's hold opens the power wheel (`SFXGameModeBase.EnterPowerWheel`, closed at the release or when it opens late), a grab where `IsCombatEnabled` is false is refused; not run. A heavy weapon has no slot, so a grab in a slot switched away and ME3 threw it (`SFXHeavyWeapon.PutDownWeapon`); since 2026-11-17 (lane CTL, the owner's question 13) a grab at a slot is refused while `SFXHeavyWeapon` is in hand (`body_slots::Input::switchLosesHeld`, unit-tested; not run: no combat fixture), and the click's press the mod owns never reaches the game as R3 (ME3's point of interest and area map) | LE2: the SMG with a class that carries one, at `thighRight` and at `lowerBack`. LE3: a power cast, cover and a reload with the gun away; the menu's Holster button (greyed until the owner has seen it). |
| G09 | Gun kick (`game/GunKick`, `core/ik/GunKick`) | **adapted** V: recoil from `SFXWeapon.GetWeaponRecoil`; `gun kick: first kick` (le2-vrik) | **adapted** U: ME2's branch (`SFXWeapon.GetWeaponRecoil`, the shots from ViewLevel's `SetWeaponRecoil` handler) since 2026-11-16; builds, ReflectCheck rows (`CheckLe3ViewLevel`); not run in ME3 | LE3: a combat fixture with shots (research/le3-combat-fixtures.md): `reflect:` agrees on the new rows, `view level: first shot seen`, `gun kick: first kick`. |
| G10 | View level, aim assist zeroed (`game/ViewLevel`) | **adapted** V: pitch held in 4,723 of 4,723 ticks, yaw kick removed (le2-vrik); the aim-assist zeroing unmeasured | **adapted** U: the same handlers as ME1 and ME2 since 2026-11-16 (PlayerTick, `SFXInventoryManager.SetWeaponRecoil`, aim assist's `ViewFriction`/`ViewAdhesion`), ME2's yaw-kick removal; ME3 has all 29 names; builds, ReflectCheck rows; not run in ME3 | LE3: the same fixture: `view level: handlers on ...`, the pitch held while shooting (the heartbeat's `view level:` line), whether ME3 queues a yaw kick, and which PlayerTick functions it found (none: it levels a frame late from the mod's tick). |
| G11 | Camera shakes off (`game/CameraShake`) | **adapted** V: ME2's branch sets `CameraModifier.bDisabled`; modifiers off in the run. Which gun shakes exist is [verify] | **adapted** U: ME2's branch since 2026-11-16 (`SFXPlayerCamera.ScreenShakeModifier`, `Camera.CameraShakeCamMod`, `SFXCameraMode.bIsCameraShakeEnabled`: all in ME3's SDK); builds, ReflectCheck rows; not run in ME3 | LE3: `camera shake: ... off in VR` names each modifier it found; `mevr shake` in a combat fixture. |
| G12 | Camera collision off in first person (`game/CameraCollision`) | **n/a**: Halcyon ME2's first person clears `SFXCameraMode.bCollisionEnabled` itself on the mode it writes and on an interpolation (`FpApplyOffset`, upstream). Save 4, 2026-11-17 (lane FP3): `mevr camera trace` read the flag 0 in every tick, and the game camera stayed 30.0 uu ahead of the pawn while the stick pressed Shepard into geometry forward and backward (research/trilogy-first-person.md) | **n/a**: the same in Halcyon ME3 (the current mode and an interpolation's To; the Interpolate object's own flag is left, [verify] during a draw on a combat fixture). Save 31: the flag 0 in every tick, the camera 35.0 uu from the pawn unchanged while pressed into the galaxy map's ring and the corridor's end | Nothing to port: ME1's module stays LE1's (its Halcyon never cleared the flag). The trace's `modeCollision`/`toCollision` columns check it again. |
| G13 | Body follow: torso, legs keep the run, steady idle, the body down to the head (`game/BodyFollow`, `core/ik/BodyFollow`, `core/ik/TorsoPose`'s `crouch`) | **adapted** V: skeleton audit (le2-vrik). Conversation `BodyAway` is LE1-only. `crouchBody` (2026-11-18) is the shared core, U on every game (offline on LE1's skeleton, research/29). `crouchBodyUp` on by default since 2026-11-19 (the owner's yes; an old file's 0 moved once, `settings_file::MoveDefault`): an L3 crouch with the player standing brings the body up 0.56 m on ME2, the hands on the controllers, VR cover still judged from the crouched head (V on ME2: `crouch-anchor.ps1 -Phases button`, `crouched-cover.ps1`; research/29 "The body up to a standing head"). Backing up with the weapon away the legs run 120 degrees off the motion, as on ME1 (no backward run exploring; trilogy-plan question 19). Lane ANC (2026-11-18, the shared core, U on every game, research/29 "A run never lowers the body"): `TorsoGoal::crouchMaxDrop` (`ik::CrouchDropLimit`): the body goes down no deeper than the player's own head; ME2's run and the steps after it stood Shepard's head 13-15 cm higher than standing and the body went down 12-18 cm mid-run in the owner's session | **adapted** V: shared follower plus a head-only solve on grip loss, run `20261115-182253-344`. "Legs keep the run" has its Velocity read since T04 (U: built; the handle's ReflectCheck row is L3-3's) | LE3: a walk under replay showing `torso: legs:`. ME2: `tools/le2-halcyon-run/crouch-anchor.ps1 -Phases run` (no `crouching with you` past 3 cm running with the head standing). |
| G14 | Body scale (`game/BodyScale`, `core/ik/BodyScale`) | **shared** U | **shared** U | Needs a recorded floor fixture: the fake headset's floor is at its head. |
| G15 | Floor lock (`plugin/FloorLock`, `core/vr/FloorLock`, seam `FloorViewTranslation`) | **adapted** V: the correction measured on Save 4 (`final eye 1.7526 m` = the STAGE eye, correction +12.26) and held through the vault's root-motion steps (shared `vr::floor_lock::MoveHold`, the mod's `traversalMove`): three vaults with every tick's wrists within 0.15 m, no third-person frame (research/le2-traversal.md "The floor lock through the vault", 2026-11-16); manual height not wired | **adapted** V: `stageEyeM = finalEyeM = 1.7526`, run `20261115-170957-963`; menu continuity `20261115-183117-290` (research/null-headset-height.md); since 2026-11-18 (lane AU2) an evade is no floor move and ME3's action cameras keep the lock: a roll on the combat fixture held the eye at 1.7526 m (research/le3-combat-fixtures.md "A combat fixture by console") | LE2: the calibration's manual height once wired; the headset. LE3: a traversal move under the hold. |
| G16 | Guided calibration and hand calibration (`plugin/Calibration`, `CalibrationDraw`, `ArmSample`, `game/BodyCalibration`, `game/HandCalibration`; one file for the three games, `core/vr/CalibrationFile`) | **adapted** U: `MEVR_SEQUEL_MENU_SOURCES`, `WithMenus` v15, glue in `plugin/MenusLe2`. **The measurements are shared** since 2026-11-18 (lane AU7, 88e9a775; research/25 section 17.1): `%LOCALAPPDATA%\MEVR\calibration.ini`, taken once from the games' own inis. V: on Save 4 ME2's host took ME1's sections and the mod applied ME1's values character for character, no prompt (`[Debug] calibrationKey`) | **adapted** U: same; the shared file U (same code, not run on ME3) | A headset run: the fake headset has no interaction profile. ME3: the shared file on Save 31 with `[Debug] calibrationKey`. |
| G17 | Comfort: snap/smooth turn, vignette, walk toward the hand (`game/Comfort`, `plugin/Vignette`) | **shared** U: the comfort run timed out; the vignette layer path was seen (`20261114-212412`) | **adapted** U: `NativeLocomotionAllowed` over `player::MovementRefused` (ME3's one admission read, Forks 11) | docs/le2-m6-native-checks.md comfort row: two right-stick flicks and a hand-yawed walk. |
| G18 | Weapon drawn like exploring: explore stance, constant eye, linear walk and stick, draw rate (`game/CombatStance`, `core/vr/CombatStance`, seam `ConstantFpEye`) | **adapted** (part) V: since 0601ef40 section C runs: `Engine.Pawn.AccelRate` by ME1's rule (no speed ramps on ME2: 750 -> 6000; a straight push at 398 u/s in 0.1 s, was 49), the start/stop/skid flags off, `movement: response:` measured. V: the constant eye through the shared seam `MevrSeams::ConstantFpEye` in `FpApplyOffset` (lane AU5, Save 4: Combat hooks (-30, 0, 100), Offset now (-65, 0, 15), the camera where exploring has it). V (lane AU6, c8f6987e): the speed from the push on `BioPawn.fMoveMag` after `PlayerMoveCombatOrExplore` (half a push 167 u/s, was 75), the stop at once exploring (the move function's 0.5 s blend skipped: 0 in 65 ms, was 0.5 s), the draw rate (`BioAnimNodeBlendByWeaponEquip` nodes' `CB_Draw*`/`CB_Holster*`: the rifle in hand 0.16 s, was 0.62; holstered 0.16 s, was 1.13). U (lane M2W, 2026-11-18): the explore tree's start and stop nodes' root motion answered with RMM_Ignore as on ME3 (`UpdateStartSteps` on both sequels; the owner's session: turned starts 0, 0, 7, 206-284 u/s with RootMotionMode 3, research/38 "ME2's late start"). Left: the stance (off on ME1 too) | **adapted** (part) V: the constant eye (lane AU5): Combat's Offset (5, 20, 35), the eye as exploring (combat fixture). V (lane AU6, dadb7461): the explore eye at Shepard's face (the owner's answer: the renderer's rows (-35, 0, 65)): 34 units ahead of the head bone exploring, 26 in combat, arm IK first person in a fight. V (c8f6987e): the speed from the push after `PlayerMoveExplore`/`PlayerMoveCombat` (half a push 146 u/s, was 75), the draw rate (slot channels' `CB_Draw*`/`CB_Holster*`: holstered 0.16 s, was 0.66-0.95). V (lane AU9): the start and stop steps (ME1's `startStopAnims = 0`): the explore tree's `BioAnimNodeLocomotionStart`/`-Stop` nodes' root motion requests (RMM_Accel) answered with RMM_Ignore (`UpdateStartSteps`): a full push runs at 0.1 s exploring (0, 389, 389 u/s; was 0, 3-12, 152-253), half a push 189 at once, the stop stays a stop (the stop animation had walked the pawn on 16-38 units). U: section C (AccelRate by ME1's rule). Left: the stance (off on ME1 too) | The stance on the sequels after ME1's headset verdict; the eye in the headset on both sequels (the A/B is built: Advanced > Tracking > Eye position, `[Hands] eyeTest`, lane AU9). |
| G19 | Our main menu (`plugin/MainMenu`, `MainMenuSettings`, `src/plugin/menu`, `assets/menu`) | **shared** V: run `20261114-222813-lane-d-menu` (f9809ed3); unported rows greyed | **shared** V: menu open and resume, run `20261115-183117-290` | notes/headset-checklist.md §5. |
| G20 | Menu pause (`game/MenuPause`) | **adapted** V: `WorldInfo.bPlayersOnly` (same run) | **adapted** U: `BioWorldInfo.PauseGame` through `UnambiguousMethod` (research/le3-menu-pause.md) | — |
| G21 | Game menu and Holster buttons (`game/GameMenuRequest`); since 2026-11-17 Leave on the sequels (question 16) | **adapted** V: Game menu opened the Mission Computer; Holster not exercised; **Leave** V (lane LV, 2026-11-17, research/28 "Leave"): `LeaveWorld` (ME2's B tap) on the top bar, shown only on foot, out of combat, with an armed `SFXSeqEvt_LeaveWorld` loaded (HostServices v44 `setGameMenuOffers`); hidden on Save 4 and the Normandy (no event), during `BioP_N7Mine`'s squad selection (not on foot); offered there on foot, pressed: `LeaveWorld` called, the event's `TriggerCount` 0 -> 1 | **adapted** V (pages): squad, journal, map and Close checked on Save 31 (lane OM, 2026-11-16: ME3's screens act on B's release, so every back-out sends B and its release); Holster greyed on ME3's menu bar (`main.rml`: "Not on ME3 yet"; lane OM2, 2026-11-16) until lane WP's `holsters::PutAway()` and `[Holsters] le3PutAway` on by default; since lane OMX (2026-11-17) a back-out on ME3 gives each screen one B and waits for the screen that takes its place (`vr::game_menu::StepBackOut`: the area map's B release starts its close animation and the old B every 12 ticks restarted it, so Close from the map stayed in the Mission Computer); Close checked on Save 31 from the map, squad record, journal and Mission Computer, after the page switches and after the game's own Start and `ShowAreaMap` (research/le3-native-gui.md "Close from the area map"); **Leave** U: the same rule (ME3's B tap is `OnTap 0.3 TryMelee \| LeaveWorld`; combat from `SFXGRI.InCombat`); its events are all the Citadel DLC's (party, dance floors, casino race, combat simulator), none reachable from this machine's saves | ME3's Leave in a Citadel DLC level (a save there). ME1: n/a (its only `LeaveWorld` is the Mako's X; same-pose identical). |
| G22 | Omni-tool rail, wrist tap, gauntlet (`plugin/OmniToolRail`, `RailArt`, `game/OmniTool`, `game/GameArt`) | **adapted** V: run `20261114-234544-lane-d2-rail` (1a8d3b39). `GameArt` LE1-only (ME1's art cache, recoloured) | **adapted** V: rail shared, in ME3's Mission Computer colours (theme `me3`, never ME1's art); the gauntlet is ME3's own client effect (`RvrClientEffectManager.Start` of `OmniTool_LeftFull_VCFX_M`), seen on Shepard's forearm on Save 31 (lane OM, 2026-11-16); since lane FX3 (2026-11-17) the display the screens project from is the player's own live `OmniHand_Loop` (not a pooled, stopped or squadmate's one). `GameArt` stays LE1-only: ME3's frames and tabs are bitmap fills and its font library isn't a live object | LE3: research/le3-native-gui "The omni-tool on ME3". |
| G23 | Game screens in the room and on the wrist; the UI-world figure (`game/WorldUi`, `plugin/WorldUi`, `game/UiWorldFigure`) | **adapted** V: `SetMovieVisibility`; runs `20261115-043419-wrist-visual` and the Save 4 pointer run (827cd3af). UiWorldFigure missing | **adapted** V: `setWorldUi` served (lane OM2, 2026-11-16, Save 31): the Mission Computer and map on the wrist, War Assets in the room toward the War Terminal, the squad record (3D Shepard) on the native panel; ME3's adapter reads the focused `SFXGUIMovie` and turns its UI world off (`SetRequiresUIWorld(false)`) while ours stand; ME3's `d3d_capture` stays out of MENU_MONO (`MevrSeams::WorldUiOwnsGui`). Shepard's view on foot held at render time behind them (ME1's hold, shared: `plugin/WorldUi` `HoldViewBegin`, `MevrSeams::WorldViewHoldBegin/End`; the head hidden, the body too for a room screen: `WorldViewHeld`; lane OM3, 2026-11-17). Lane FX3 (2026-11-17): a room change forgets the old room's movies without a call (every kept movie or panel checked for its class first, every game), a movie whose script asks for the UI world again is told again, the first state of an opening already holds the view (every game) and ME3's renderer keeps the last clean eyes while an opening is reserved (`MevrSeams::WorldUiHoldFrame`, LE1's guard), and the Atlas and injured Shepard hold no view on foot (`FootViewMode`); seen on Save 31 except the Atlas (a load with the Mission Computer up, the A/B of the first state's hold; ME1 same-pose unchanged). Lane AU1 (2026-11-18, both sequels, not run): the sequels' own screens by tag (choice screens, stores, terminals, the weapon bench, ME2's minigames on the wrist; research/le2-menus.md "The sequels' own screens"), ME3's flat panel at ME1's 1.4 m / 2.35 m. UiWorldFigure missing | LE3: research/le3-native-gui "The movie's mouse and the view behind a screen". |
| G24 | Omni pad for the bypass minigame (`plugin/OmniPad`, `core/vr/OmniPad`) | **missing**: ME2's hack and bypass minigames are different games (docs/le2-validation-coverage.md "Hacking and bypass") | **n/a**: ME3 has no bypass minigame (game knowledge, not checked in its data) | — |
| G25 | Galaxy map: hologram, stereo window, projector (`game/GalaxyMap`, `game/GalaxyProjector`, `plugin/GalaxyHologram`, `game/SceneCaptureRig`) | **adapted** V: ME1's flow on the sequels' one flow since 2026-11-17 (lane GX2): `game/GalaxyProjectorFlow` with ME2's adapter `game/Le2GalaxyCapture` (`BioA_Nor_110`; the projector and console have ME3's tag and mesh); ME2's renderer: `GetMenuMode` not flat in modes 9/10 while `GalaxyOwnsPresentation`, `WorldViewHoldBegin/End` in its calcview hook, the hides at the held eyes; the depth of field off near the projector (with it on the map's opening faulted the render thread, exe+0x98C25D); no pointer (the map's own controls: a GUI-event click on Exit did nothing); the opt-in flat table retired. Runs `20261117-192445-759-LE2-gx2` (`galaxy-full1`, `galaxy-final`, Save 36 read only): A opens the map with the CIC in stereo, the map a stereo disc on the projector, its UI on the panel there, X back to the room. Lane GX4 (2026-11-17, session `20261117-212856-914-LE2-gx4-me2`): ME3's rules on the same flow (reservation, bridges, interlude), ME1's opening wait cut (`GalaxyMap.BioSeqAct_Delay_0` 2.5 s to 0.05: 0 -> 6 -> 9 in 0.19 s, held); the map's A/B are GUI events (`mevr guikey 12`). Lane GXR (2026-11-18): the ray picks (`game/GalaxyRay` + `core/vr/GalaxyRay`, one module for ME2 and ME3: the selectors projected through the map camera, the trigger puts the galaxy view's cursor on a cluster then A; the Normandy flown to a system or planet on the map's own stick, never moved; the orbit's scanner reticle aimed where the ray meets the planet); V for the system view: stretch `20261118-012745-525-LE2-gxr-me2` (Save 36), a planet picked by ray and trigger, flown to in 2.3 s, selected, A; second stretch `*-LE2-gxr-me2b`: the relay picked into the galaxy view, a cluster picked there (the map's cursor put on it, the relay jump), a planet picked to its planet view; the orbit's scanner not reached by script (its "Start Scanning" didn't answer the GUI A or `HandleSelectPlanet`). Not seen: travel (the galaxy view's cursor stuck short of a cluster; a relaunch's console wouldn't open the map at all, open), orbit and scan, the headset | **adapted** V: ME1's flow, one copy since 2026-11-17 (lane GX): `game/GalaxyProjector` (the held view, the render hold, ME1's `StepWindow` as `StereoWindow`) with ME3's adapter `game/Le3GalaxyCapture` (the measured projector, `Le3GalaxyRetention`'s lease for the map's length, modes 11/12, hiding the room's projector); the host's hold through `MevrSeams::WorldViewHoldBegin`, `GalaxyOwnsPresentation` in ME3's presentation decision, the window and the UI panel on the disc in `plugin/SequelOverlays`. Runs `20261117-174710-211-LE3-gx1`, `-175453-896-LE3-gx2` (AutoSave at the console): A opens the map with the CIC in stereo, the map a stereo disc on the projector, its Flash UI on the panel there, X back to the room. Captures without the post-process chain (any chain left them black). Lane GX3 (2026-11-17, run `20261117-185230-030-LE3-gx3a`): the relay's travel and arrival on the projector (the movie plays inside mode 11; the window only over world frames), the opening's cinematic held at Shepard's (0 unheld main views in five openings), ME1's interlude rule (`galaxy_projector::MapInterlude`), the retention re-armed, the projector's own lens flare (the blue band) hidden with the hologram. Not seen: orbit and the probe, picks, the headset. Lane GX4 (2026-11-17, session `20261117-205840-548-LE3-gx4-me3`): ME3 on the sequels' one `game/GalaxyProjectorFlow` (its adapter `Le3GalaxyCapture`: the CIC's art, the lease, the map's level); the opening wait cut as ME1's (`GalaxyMap.SeqAct_Delay_1` 2.5 s to 0.05: 0 -> 8 -> 11 in 0.11 s, the first map frame as shipped, every opening held); the lease refreshed through the shorter opening; travel and arrival again on the flow; a reload in the map has no CIC art (the flat panel until X, reported); the opening's first cinematic frames held by a cue the host reads (built, not seen). Lane GXR (2026-11-18): the ray picks on the same `game/GalaxyRay` as ME2's (`m_aSelectableActors`; ME3's A is `BioSFHandler_GalaxyMap.SelectGalaxyItem`, not a GUI event); V: AutoSave of `John_12_Vanguard`, a station picked into its orbit view (the Normandy flown there on the map's stick), a system flown into from the cluster view; the galaxy view not reached | docs/galaxy-projector-handoff.md "2026-11-17" and steps 3-5; Forks 3. |
| G26 | Game pointer and GUI events (`plugin/GamePointer`, `game/GuiCursor`, `core/vr/GuiEvents`) | **adapted** V: the Save 4 pointer run, 2026-11-15 (research/le2-menus.md "Game screens and pointer") | **adapted** V: pointer on the native GUI quad; `takeGuiEvents` served on LE3 (`WithMenus`): a replayed ray click on the Mission Computer's Squad went in as `SFXGameModeGUI.GuiKey(31)` and opened the squad record, no SendInput (lane OM, Save 31, 2026-11-16). On a world screen (wrist or room) the pointer's moves, clicks and wheel go to the focused movie through `SFXGUIMovie.SendMouseEvent` (GuiKey's mouse is dropped in the pad mode): a ray and a fingertip opened the Journal from the Mission Computer, War Assets' entries respond (lane OM3, Save 31, 2026-11-17) | LE3: research/le3-native-gui "The movie's mouse and the view behind a screen". |
| G27 | Interaction: selection by gaze, label at the object (`engine/SelectionGaze`, `game/Interact`, `HudPieceLayers::Interaction`) | **adapted** V: gaze at `0xAC0530` (ME3's void(this) search); a band per game (`hp::DefaultLabelCrop` `Me2`), on by default; Save 4: the door selected ahead, nothing at 60 degrees, its label (research/40 section 6, lane HU) | **adapted** V: gaze at `0xA9BF90`; run `20261115-190610-153` (A opens War Assets); band `Me3`; Save 31: War Terminal 1.35 m, the label at the console, nothing at 70 degrees (research/40 section 6) | Both: A's use after a gaze pick in the same run, a long name, combat targets, the headset. |
| G28 | HUD pieces: wrist status, gun panel (`game/HudPieces`, `plugin/HudPieceLayers`) | **adapted** V: `Layout::Me2` crops; run `20261115-002101-lane-b4-m2b` (f71e7a71); ammo mount 2a108759 | **missing**: `setHudPieces` not served (`#if !defined(MEVR_WITH_HALCYON_LE3)` in ModLoader.cpp); `game/HudPieces` stands down by name on ME3 (no fall-back to ME1's placement) | LE3: docs/me3-overall-goal-handoff.md L3-4 "native wrist crop". |
| G29 | Power wheel at the left hand, lock and push, self-cast gestures, coach (`game/PowerWheel`, `plugin/PowerWheel`) | **adapted** V: ME2 roster (`SFXPowerManager`); the whole wheel on Save 4 by replayed controllers, 2026-11-17 (lane SQ, `tools/le2-halcyon-run/powers.ps1`, research/55 "The whole wheel on Save 4"): every tile by the hand's travel or the stick, the tap, No power, the coach card, Adrenaline Rush on the chest (also with the weapon away), Disruptor Ammo slapped onto the rifle, Slam locked on a hostile drone from the level's own factory (`-Drone`) and pushed (`ActivatePower` took it). The coach's controls line was ME1's grenade throw: the sequels' card names the wheel alone since. Not seen: Slam's effect on the target, evolved powers, other weapons, the headset | **adapted** U: `game/Le3Powers.hpp` (`SFXPowerCustomActionBase`). Save 31 (the Normandy, a non-combat Vanguard, 2026-11-17): the roster reads Pull, Shockwave, Incendiary and Cryo Ammo, Biotic Charge (an unnamed label split into words since), Discharge, Unity (disabled), Flare; the wheel stays shut where combat is disabled, as ME1's. Nothing cast yet | ME3: the combat save's checks in research/le3-combat-fixtures.md (`powers.ps1 -Game LE3`); ME2: research/55 "The whole wheel on Save 4"'s "Not established"; L3-6. |
| G30 | Squad weapons: the left trigger is the game's own weapon wheel (`game/SquadWeapons`, `core/vr/SquadMenu` `RouteTrigger`) | **adapted** U: the owner, 2026-11-18 ("just remove the custom menu and just always default to the classic menu"): our drawn menu at the hand, ME2's only style until then (`TickCustom` over `game/Le2SquadMenu.inc`'s paged inventory; V for one tile on 2026-11-16, lane GF2), is removed with `plugin/SquadPanel`; the left trigger is the game's own LB with the left hand open and with both hands on the gun (`RouteTrigger`: never `OpensWheel` on a sequel; `TestTriggerRoute`), whose hold opens ME2's weapon wheel, shown at the hand (G57). What stays: the roster (each henchman's inventory chain) for G31's squad context, orders and voice, and `GiveWeapon` (`OrderWeaponSwitch`, `game/Le2SquadWeapons.inc`). The owner saw the wheel from the trigger with both hands on the gun (2026-11-18); with the hand open not seen yet. ME1: unchanged by default (its pull opens the game's wheel itself, `EnterWeaponWheel`; with both hands on the gun the game's LB) | **adapted** U: the same rule; ME3's renderer knows its wheel by LB held (Halcyon's `[WHEELVR]`), which the game's LB gives | A pull of the left trigger on foot with the left hand open, then with both hands on the gun: `squad: left trigger pulled: the game's own LB (this game's wheel is opened by its own button)` each time, the game's weapon wheel up at the hand (a `wheel` layer), never a `squad` layer. |
| G31 | Squad context and gesture orders (`game/SquadContext`, `game/SquadOrders`, `core/vr/Squad*`) | **adapted** U: `game/Le2Squad.hpp` (`HenchmanOrderAttackTarget`, `SquadOrderUsePower`); the flat move order by the left grip seen dispatched on Save 4 (2026-11-17), with placement's move marker since; attack and power orders not seen | **adapted** U | M6 C; L3-6. |
| G32 | Squad placement by an arc (`game/SquadPlace`, `plugin/SquadPlaceDraw`) | **adapted** V: on by default as ME1's (`[SquadOrders] place = 1`) since 2026-11-17 (lane SQ, `tools/le2-halcyon-run/place.ps1`, research/54 "ME2 placement and orders on Save 4"): the squad wheel at the right hand, the arc and its named ring, Miranda 0.38 m and Jacob 0.27 m from the ring and holding, Miranda alone by the left tile (0.22 m), a double B calls both back. The hold order needs the squadmate's move marker placed first (`BioPlayerController.UpdateMoveToIndicator`, `le2_squad::PlaceMoveIndicator`): without it nobody moved. Until 2026-11-17 B also reached the game (`Console_Melee`: every hold and double tap was a `TryMelee`); since lane MEL `game/Melee` keeps B from the game on foot in VR on every game, as on ME1, and placement stands down only while B really reaches the game (`melee::BIsGameButton`, no longer the ini key alone); `place.ps1` judges a TryMelee call trace and its flat-order verdict (a hard-coded PASS until then) Lane AU1 (2026-11-18, not run): placement through the one dispatcher (`squad_orders::Give`/`GiveAt`; `GiveLe2Order` gone): a placed hold is a standing order, counted. | **adapted** U: the same order path with the marker; the squad's console lines (`mevr place`, `squad`, `order`, `say`, `context`) on ME3 since 2026-11-17; no squad on Save 31; B kept from the game as on ME2 (`melee-b.ps1 -Game LE3`) Lane AU1 (2026-11-18, not run): the same. | ME3: `place.ps1 -Game LE3` on the combat save (research/le3-combat-fixtures.md). |
| G33 | Voice orders, the model tier, the Voice page (`game/Voice`, `plugin/Voice`, `plugin/VoiceModel`) | **shared** U: the protected voice run timed out | **shared** U: the model tier compiles into LE3 wherever its host compiles `plugin/VoiceModel.cpp` (until 2026-11-16 only when LE2 was built too) | A microphone run, or `voice.request wav`, per game. |
| G34 | Squad acknowledgement (`game/SquadAck`) | **adapted** V (lane AU4, 2026-11-18): the AI's own answer heard (`TriggerVocalizationEvent`, its parameters or the calling frame's Locals; ReceivedOrder 2..10), no line of ours played, a 3 s window; Save 4: moves answered with 7 after 1.0-1.5 s, follows with 8, 4 of 4 (research/54 "The squad's answer on the sequels") | **adapted** U: the same listener, ME3's functions found and hooked; no squad in its fixtures | ME3: a squad order on a save with a squad. ME1's chatter functions are in neither sequel. |
| G35 | Thespian mode (`game/Thespian`, `plugin/Thespian`, `core/voice/ScriptMatch`) | **adapted** U: replies by native slot; no mute or `SkipNode` on ME2 (research/70 "ME2 cinema adapter") | **adapted** V: reader on three native wheels, run `20261115-203330-839`. Spoken selection unverified | M6 E; L3-4. |
| G36 | Conversations in the world: flow, moments, keep-in-place (`game/ConversationWorld`, `ConversationFlow`, `AuthoredMoment`, `engine/ConversationHooks`, `plugin/ConversationWorld`) | **forked** U: release scope since 2026-11-19 (the owner changed priorities); the sequels' module `game/SequelConversationWorld` with ME2's reader (ME1's `m_oCurrentConversation`, `m_SpeakerList`), v33 served on LE2, ME2's renderer seams (`me2_xr.cpp`'s decision as the galaxy map's held cinematic, `calcview_hook.cpp`'s held view, `engine_probe.cpp`'s hides), ME1's near plane, the right stick to the wheel; experimental, off by default (lane FPC) | **forked** U: ME1's core slice on ME3 (`game/SequelConversationWorld`, the owner 2026-11-19, for the hair headset run; ME1's near plane added by lane FPC): the held host view, letterbox cleared, ME1's opening rule (`cw::DecideOpening`), Shepard pinned, the left stick walks (`cw::StepPlayer`/`KeepClear`), the right stick to the wheel, the UI fixed in the room, DOF off; `mevr convo world`; upstream's ConvoFirstPerson pinned off; seen in the headset at Miranda's apartment; no flow, moments, people kept in place or circle | research/le2-conversations-in-world.md section 4 (both sequels on one module; the walking scenes, looks, marks, circle, moments); since lane FPC2 (2026-11-20, section 6) both sequels withhold at the source (`engine/ConversationHooks`: ME2 0xA2D180, 0xB06390, 0x6B9C50; ME3 0xA29DF0, 0x6BAFF0), play ME1's moments (Tali's re-intro held in first person on ME2 by script), send the generated presence table's scenes to the 3D screen (`cw::NoPlaceForPlayer`) and draw the floor marker (`cw::StepMarker`, all three games); since lane FPC3 (section 7) the eyes are ME1's (the view on foot kept at the opening, the first-person eyes from his head bone learned on foot `cw::NoteEyeFromHead`, their height the player's `cw::EyeHeightStep`), rides carry the pin (`RideBase`), the game's fade to black moves the player (`worldBlackMoves`) and a GUI screen after a held conversation keeps the world (`ConversationHandover`); since lane FPC5 (section 10) ME1's circle and squad formation (`game/SequelCircle.inc`, `game/AiWalk`), the subtitle under the speaker and the wheel in front (`plugin/ConversationUi`), the marker's waits source (`game/SceneWaits`, all three games) and the core's end facing (`cw::FacingOnFoot`, ME1's too), V on ME2 (fake headset: Tali's, Samara's), U on ME3; since lane FPC-ME3 (section 11) the presence rules at the opening (`cw::PresenceAtOpening`, the owner's rule (a): everyone but Shepard hidden, V on ME3 with Khalisah's interview), no scale set while the world isn't drawn (`game/WorldDrawn`), ME1's black move and seat in moments, the face axis +X on both sequels, ME3's Prothean moment held with Shepard's line walks withheld (0 findings); since lane FPC6 (section 12) the subtitle under the speaker V on ME3 (the career's option was off; the band the frame's width), a Cut's eyes wait for his mesh after the stage's teleport, ME3's Save_0090 fixtures by ride (four conversations and Purgatory's drinks as moments held), the first circle walk-up V on ME2; since lane FPC7 (section 13, research/69 item 45) the player's head drives Shepard in every held scene with the view with him on all three games (core `cw::StepHeadDrive`, the torso's wider bend, his body hidden where it can't reach), V on ME3 (Purgatory's drink: his head 0.10-0.18 m from the eyes, was 1.05); left: section 12's lists and T49. The fake-headset runs move U to V. People leaving a scene (`game/ConversationExit`, `engine/ExitHooks`, the shared rule `core/vr/ConversationExit`; lane EXIT, 2026-11-20): ME1 built, run on the fake headset (Udina walks off with the game's AI, applied once out of sight); ME2 **shared** V (fake headset: Tali and Prazza held after her reintroduction, walked off with the game's AI, applied under the dip; the broken run caught by `vanished-in-view`; since lane EXIT3 the walk is the Kismet's `SFXAI_Core.MoveToGoal` along the AI's path to a hidden navigation point: both out of sight in 2-3 s, no dip, research/conversation-exits.md section 12), ME3 **shared** V since lane EXIT3 (fake headset at the Citadel's embassies: the Prothean's moment, three stunt actors held in view on the script path, the script went on, the broken run caught; hub people are stunt actors, never walked; crew members by `hench_` tag are squadmates; research/conversation-exits.md section 11), since lane EXIT2 (the same module with a sequel adapter; ME2 `Engine.Actor.SetActive` `0x4BC510` through ProcessEvent, ME3 `BioSeqAct_SetActive`'s script call on `0x4BFAF0`; research/conversation-exits.md section 9); ME3's line visibility tracks counted, not held (owner question 6). |
| G37 | Two-ear audio: OpenAL Soft with HRTF (`plugin/OpenAlSoft`) | **n/a**: ME2 plays through Wwise, not ISACT/OpenAL (research/69's audio note; [verify] the exe's imports) | **n/a**: same | Check `MassEffect2.exe`/`MassEffect3.exe` imports for OpenAL32 before closing the row. |
| G38 | Sound placement: voices, people near, the gun (`game/SoundPlace`, `engine/VoicePlace` on ISACT `0x7BA90`) | **missing**: `ready`; `kSoundSourceUpdate = 0` | **missing** | Wwise needs a placement hook of its own. |
| G39 | No cover snapping, VR cover (`engine/CoverAcquire`, `game/VrCover`, `core/vr/VrCover`) | **adapted** V: `FindPlayerCover` post-handler; VrCover on `SFXGame.ModifyDamage`; research/23 §7.4, 2026-11-14 (61705db8). The shields' faster recharge behind cover (ME2's own rule, the owner's yes 2026-11-16): `SFXShield_Base.Tick`'s two questions answered while the head is hidden from every awake hostile (`vr::cover::Shelter`), V on the fake headset: the 5.00 s wait 3.51 s behind cover, 5.02 s with the rule off or in the open; research/23 §7.5. U since 2026-11-18 (lane CV2, research/23 §7.6-7.7): the owner's run had no cover entry of the game's (the "magnetic" pull is not ME2's cover; a cover watch logs what moves Shepard near cover) and 0 of 164 hits covered (the eye line open over barriers to hovering drones): the level look (`[Cover] judge = either`), each hit's detail logged. Since 2026-11-19 (lane CCV, the owner's yes, research/23 §7.8; every game, ME1 too): crouched by the game (L3, the physical crouch, a seated player's L3), every look traces from Shepard's crouched head over his floor (`vr::cover::JudgingEyes`: his animated head bone, 0.88 m; ME2's low cover tops 0.94-0.98 m), never above the real eyes; each hit's line names the eyes. V on the fake headset at the owner's calibration (`tools/le2-halcyon-run/crouched-cover.ps1`): behind CoverLink_28's crate and the courtyard barrier, standing open (shields 257.5 -> 209.0), crouched by L3 or by the physical crouch covered (unchanged), `mevr cover crouch off` open | **adapted** U: no snapping by default since 2026-11-17 (lane FP3; ME1's and ME2's `[Hands] noCover = 1`, the owner's rule 7): `SFXGameModeDefault.FindCover` answered false inside the mode's `TryAcquireCover`; cover already taken is left to the game. VR cover's real-slot damage rule (`game/Le3Cover.inc`, ModifyDamage on SFXPawn_PlayerParty) follows the refusal, so it is on by default too, each answer logged (`vr cover: LE3 answered IsInCover TRUE ...`). Save 31 has no cover (0 CoverLinks; `TryAcquireCover` returns before `FindCover` on the Normandy's non-combat pawn): neither was seen in a fight. Since lane FX3 (2026-11-17) the refusal is the one rule of the three games (`ik::posture::RefuseCoverSnap`: VR on foot with noCover, kept from tick to tick; it had also asked for the host's controls and native movement and failed open during wheels, wrist screens, a roll's end or a late tick), and the take-cover prompt is hidden as ME2's (`SFXGUIInteraction.ShowCoverWidget` post-handler, `HideCoverWidget(oPlayerController)`); U: Save 31 has no cover. ME3 (and ME1) have no faster recharge in cover: n/a. Since 2026-11-18 the level look and each hit's detail apply here too (the shared `JudgeHit`, lane CV2, research/23 §7.7), U; since 2026-11-19 the crouched head too (lane CCV, §7.8), U | M6 table, cover/crouch/sprint row; L3-8. LE3: research/le3-combat-fixtures.md's first check (refusal, A, covered and exposed hits) and FX3's (no snap with a wheel open or at a roll's end, the prompt, first person in cover). |
| G40 | Physical crouch (`game/Posture`, `core/ik/Posture`) | **adapted** V: `Engine.Pawn.ShouldCrouch`; research/23 §7.4 step 1. U (lane M2W, 2026-11-18, research/23 §8): the crouch follows the player (`[Hands] crouchFollow`, shared core, every game), L3 the mod's crouch toggle (`[Hands] crouchClick`, kept from the game), the margin and the lean on by default since the owner's yes of 2026-11-18 (`crouchMinDropM` 0.25, `crouchLeanRatio` 1.0, every game) | **adapted** U: LE3 native admission; M2W's follow, L3 and the off settings as ME2's | `tools/le2-halcyon-run/posture-walk.ps1 -Phases crouch,l3` (the crouch phase needs the replayed head in the posture's floor sample) |
| G41 | Vent, weapon heat (`game/Vent`, `core/ik/Vent`); the sequels' counterpart, the reload by hand (`game/Reload`, `core/vr/ReloadGesture`, `PhysicalReloadRequest`) | **n/a** for the vent (thermal clips); the reload by hand **adapted** V: the pouch a left-hand body slot, the vent's receiver zone, the coordinator, the button's own `SFXGameModeDefault.TryReload`; run 2026-11-16 (lane WP): 12/0/60 -> one shot 11/1/60 -> the replayed gesture 12/0/59, a full magazine refused (research/le2-vrik.md "The reload by hand: the game's side"). **Off by default** since 2026-11-16 (the owner: "don't enable that flow"; `[Reload] enabled = 0`, an older file's 1 moved once), the gesture to be redesigned with the owner | **n/a** for the vent; the reload **adapted** U: the same module, ME3's ammo fields on `SFXWeapon_NativeBase`; names resolve on Save 31 (no gun there); off by default as on ME2 | The gesture's redesign with the owner first (off until then). Nothing is seen yet: the clip in the hand and the gesture's options for the owner, research/thermal-clips.md. Then LE2: empty, no-reserve and cancelled reloads; LE3: a combat fixture, then `reload.ps1 -Game LE3` (with `enabled = 1`). |
| G42 | Physical grenades (`game/Throwables`, `core/ik/Throwing`) | **n/a**: ME2 has no grenades | **n/a**: ME3's grenades are powers (research/le3-powers.md) | — |
| G43 | Melee gesture (`game/Melee` `Strike`, `core/ik/Melee` `Gestures`; ME1's feed in `game/Throwables`) | **adapted** V: ME1's rules and defaults since 2026-11-17 (lane MEL, the owner: melee is ME1's fist gesture; research/20 "The trilogy's one path"): fed by `melee::Tick`; the adapter `TryMelee` (the held weapon's `MeleePowerName`, Pistol_Whip on the heavy pistol) and its BioPower's cooldown. Seen on Save 4 (`tools/le2-halcyon-run/melee.ps1`): a strike with the pistol out hit an assault drone (100% -> 5%); a replayed open left hand with the right grip on the pistol: `TryMelee`, the game's melee script run; a replayed right fist with the pistol out refused (the trigger fires the gun, ME1's rule). **Forced by the game:** no punch with the weapon away (holstered, ME2 holds no weapon and its melee is the held weapon's); the fist is refused by name. B kept from the game (`melee-b.ps1`, with its canary) | **adapted** U: the same dispatcher and gesture core since 2026-11-17 (Forks 12 closed): ME3's native admission and `TryMelee`/`TryHeavyMelee` as its adapter, its staged rules (neutral priming, the heavy chamber) kept; **the stroke melee on by default since 2026-11-17** (the owner's "All recommendations", lane DEF2: `mevr melee` requests and gestures on at start, `[Throwables] melee` the off switch as on ME1 and ME2), the heavy chamber still opt-in (question 7); new for ME3: ME1's enemy in reach and the shared cooldown; **with the weapon away the right fist starts the class's heavy melee action (custom action 58, the omni-blade; lane AU8, 2c549a7a)**, V by console on the combat fixture: the Soldier's punch killed a trooper | ME3: research/le3-combat-fixtures.md "Melee on the first combat save" (a strike, its admission, impact and recovery, now with the defaults). |
| G44 | The Mako: aim, controls, cockpit, gunner, panel, hazard (`game/Mako*`, `game/Cockpit*`, `core/vr/MakoControls`) | **missing**: the Hammerhead, with ME1's Mako controls and cockpit, has its own later pass (continuation plan M6) | **n/a**: no player vehicle; the Atlas has no adapter (LE3 plan: "safe fallback") | — |
| G45 | Focus pause off (`game/FocusPause`) | **missing** | **missing** | First check whether ME2/ME3 freeze without window focus (`bPlayersOnly`). |
| G46 | Our ASI loader (`src/loader`) | **adapted** U: `LoaderCore.cpp` maps MassEffect2.exe, but every ME2 run used LEBinkProxy | **adapted** U: maps MassEffect3.exe | A run with our `bink2w64.dll` in that game's Binaries. |
| G47 | Installer, report a problem, release packaging (`MEVR-Setup`, `src/install`, `plugin/ReportRequest`, `tools/package-release.ps1`) | **adapted** U: one installer, the game a parameter (`install/Game.hpp`'s `GameSpec`: folder, exe, `-game 2`, `MELE2VR.ini` next to the exe, its GamerSettings keys; its own `ASI\MEVR-install.json`); install, "verify files" and Repair, the settings and their Restore, the report (`LE2MEVR.log`, `MELE2VR_Log.txt`), uninstall byte for byte pass on the fake game (`tools/setup/test-fake-game.ps1 -Games LE2`, 2026-11-16) and in `mevr-tests` (`TestGames`); `ReportRequest` in the host. Never installed into the real game; **not offered** (MEVR-Setup.ini `[Setup] games`, `package-release.ps1 -Games`); the players' build links no LExSDKv2 since T04 and the package refuses one that does; our loader in the launcher starts only ME1 (the picker shows) | **adapted** U: the same by the same code (`-Games LE3`); no release `MELE3VR.ini` (the renderer writes its own); its players' build links no LExSDKv2 since T04 | One install, repair and uninstall of each on the real game; T51 (the launcher's `-game 2`/`3`: `launch::GameArguments`, `kOurLauncherBoots`); the owner's word to offer one (`-Games`). M7.1, M7.2; L3-10. |
| G48 | Crash breadcrumbs, first-chance report, quit safety (`plugin/CrashBreadcrumbs`, `plugin/KeptLocks`, the ExitProcess hook) | **shared** V: breadcrumbs in every host; a first-chance report in an ME2 log (research/le2-menus "The checks"); KeptLocks in every host since lane AU4 (2026-11-18), and the renderer's `[EXITKILL]` hands the quit to the host's hook (`MevrSeams::HostEndsProcess`), which before never ran: each console quit since, `kept locks: 41 deletion(s) of the game's static locks skipped`, no `crash-exit.txt` | **shared** V: the same; two quits, `47 deletion(s) ... skipped` after its `[VRRES] exit` write | Whether the deleted locks were ever taken after deletion on the sequels (no fault was seen before either). |
| G49 | Controller input: Touch as pad 0, profiles, our actions, haptics (`plugin/PadMerge`, `game/GamepadMapping`, `plugin/XrHost`, `core/xr/Input`) | **adapted** V: `MergeMotionControllers` (conversation and orbital triggers, R3 gate, R3 kept while the mod owns the click: `Renderer::keepPowerClick`, 2026-11-17, ME1's R3 unchanged); stretch `20261115-001659-lane-b4-m2`; ctest `mevr-pad-merge`. U: L3 kept while it is the mod's crouch toggle (`Renderer::keepCrouchClick`, `kFlagModCrouchClick`, lane M2W 2026-11-18; ME1's L3 unchanged) | **adapted** V: semantic modes, map lease, traversal yaw; replayed controllers in run `20261115-182253-344` | Headset: the L3+R3 hold. |
| G50 | Head tracking and the view anchor (`game/HeadTracking`, `game/ViewAnchor`) | **adapted** V: `hostView.valid` and the replayed turn (le2-vrik). Stands down when the layer is off (no LExSDKv2 fallback since T04) | **adapted** V: head look in the pose-tag runs (through `engine::legacy` then). On our layer since T04: its rows agree in the engine probe (`CheckLe3EngineAccess`, 621 of 621), not yet seen in a renderer run | LE3: a renderer run on the T04 code. |
| G51 | Reflection (mirror) fix (`game/Reflections`) | **n/a**: stands down whenever the host owns VR (`src/mod/Mod.cpp`), as on LE1 under Halcyon | **n/a** | — |
| G52 | Engine access: reflection layer, ReflectCheck, addresses, the game adapter (`src/ue3`, `engine/ReflectCheck`, `engine/Addresses.hpp`, `engine/GameAdapter.hpp`) | **adapted** V: `reflect: 637 of 637`, session `20261116-125123-331-LE2-t04`; a monolithic build to gameplay without LExSDKv2 | **adapted** V: 621 of 621, session `20261116-125541-116-LE3-t04` | T04 closed the LExSDKv2 part (research/34 section 6.4). `kModulesOnLayer` stays false on both: the 10 `ready` modules left (BodyBounds, CoverAcquire, VoicePlace, SoundPlace, AuthoredMoment, ConversationFlow, ConversationWorld, GalaxyDump, GalaxyMap, SquadAck) are ME1 features on ME1's names and addresses; each moves to `kEveryGame` and `Up()` with its own feature's port. |
| G53 | Host/mod split, hot reload, HostServices (`plugin/Entry`, `plugin/ModLoader`, `src/mod`) | **shared** V: every LE2 run above used a host and mod pair | **shared** V: the protected LE3 runs | — |
| G54 | Save tools and scripted runs (`tools/le1-save.py`, `fake-headset-session.ps1`, `launch-game.ps1`, the conversation tools) | **forked** V: `tools/le2-save.py` (recipes and receipts), `tools/le2-halcyon-run` (`load.ps1`, 3e8609b7), `tools/qa-le2`, a diagnostic LE2 host | **forked** V: `tools/le3-save.py` (imports le2-save), le2-halcyon-run's LE3 mode; 6 saves round-trip byte for byte | Forks 10. |
| G55 | Console request, call trace, dev request files (`game/ConsoleRequest`, `game/CallTrace`, `core/DevChannels`) | **adapted** V: `mevr load` and `mevr omni squad` in runs. CallTrace runs (`Up()`; V: `mevr calltrace 2 updatecamera`, 144 lines, T04's session) | **adapted** V: `mevr thespian cards`, run `20261115-203330-839`. CallTrace as ME2: U | LE3: one `mevr calltrace` in a run. |
| G56 | Classic menu's VR hands tab and menu pointer (`plugin/MevrMenuTab`, `plugin/MevrMenuPointer`) | **missing**: Halcyon ME2's own menu, without our tab | **missing** | Low value: our main menu replaces it. |
| G57 | The game's wheels cut onto a hand quad (`core/vr/WheelCrop`, `setWheelPanel`, the native squad-wheel style) | **shared** U: `plugin/WheelPanel` (LE1's code moved out of `HalcyonOverlays.cpp`, lane AU1, 2026-11-18) composed by `SequelOverlays.cpp`; the wheel's mode through `engine/GameAdapter.hpp`; not seen in the game | **shared** U: the same | With G30. Check: a snapshot with the game's weapon wheel up shows a `wheel` layer at the left hand and `[MEVR-WHEEL] measured`. |
| G58 | VR sprint (`game/Sprint`, `core/vr/Sprint`) | **shared** V: one module and rule for the three (lane GR, 2026-11-16, the owner's "Unified sprint"): `SFXGameModeDefault.StormOn` runs (the binding's hold reaches it through ProcessEvent), its intent `BioPlayerController.bWantsToStorm` is withdrawn by both hooks' post-handlers, the faster run raises `GroundSpeed`/`CombatGroundSpeed` by `StormSpeed` over `Pawn.GetMaxSpeed`; ME2's adapter: not in the game's own cover. No ME2 fact forces the native Storm. Run on the fake headset (Save 4): withdrawn, x1.31, peak 525 u/s, the run 400 u/s; research/trilogy-sprint.md. ME1 too (the old skip never fired since 11-06) | **shared** V: the same, with ME3's native admission (`player::MovementRefused`, `IsStormDisabled`, the fresh-owner check); Save 31: withdrawn, x1.12, peak 450 u/s | The sprint's heading and feel in a headset on each game; ME3 off the Normandy; a sprint with a weapon drawn. |
| G59 | Hand contact with other characters (`game/HandContact`, `core/ik/HandContact`, `engine/BonePose` `ApplyContact`; research/73) | **shared** U: one module and core for the three (2026-11-18, the owner's design: the drawn hand stops at a person's body and slides along it, a buzz, nobody pushed); built, unit-tested (tests/HandContactTests.cpp on ME1's human rig, which is ME2's); not run in a game | **shared** U: the same; ME3's NPC rigs' bone names unchecked | All: on the fake headset beside a squadmate with a hand replay, `mevr contact people` (12+ capsules a side) and a snapshot's `mod.handContact.hands[*].touching` with the drawn wrist off its controller; then the headset (research/73 section 3). |

### Sequel-only systems

These are not ME1 systems, so the [totals](#totals) don't count them. Their form still matters for unification.

| sequel-only | LE2 | LE3 | notes |
|---|---|---|---|
| Traversal, vault (`game/Traversal`, `plugin/TraversalCue`, `core/vr/Traversal`; ME1 has no vault) | origin V: research/le2-traversal.md "Measured (fake headset, 2026-11-14)"; U since 2026-11-18 (lane CV2, "The vault Freedom's Progress requires"): asked with the head's facing (`[Traversal] facing = head`) and with the weapon away as drawn (`holstered = 1`). V since 2026-11-19 (lane RLD, the owner's "C", research/le2-traversal.md "The step to the vault"): a double A within 1.5 m and 60 degrees steps Shepard to the slot by the game's own walk, then vaults (shared `vr::traversal::StepWalk`, unit-tested; the owner's barrier three of three from 1.2 m, 45 degrees); the cue on by default, shown exactly where a double A acts | forked U: `game/Le3Traversal.cpp`; no step yet (its double tap is an opt-in), its ladder cue only with the opt-in since 2026-11-19 | Forks 4. |
| The float frame into the eyes (`plugin/EyeCopy`) | V (R16) | V (R16) | Halcyon ME2/ME3 assumed an 8-bit swap chain. Since 2026-11-16 the same module also upscales (R13) and tonemaps (R15). |
| CIC residency during the map (`game/Le3GalaxyRetention`) | — | V: 541/541, run `20261115-214310-005` | ME1's map doesn't unload the CIC. |
| Native GUI accessors (`game/Le3Gui`) | — | adapter | ME3's GUI is SFXGUIMovie, not BioSFManager. |
| Diagnostic host (`MEVR_LE2_DIAGNOSTIC`: `engine/Le2DiagnosticHost`, `Le2ControlHost`, `core/qa`) | test-only host | — | Forks 10. |

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
     - `game/Le3GalaxyCapture.cpp`: ME3's adapter of the sequels' `game/GalaxyProjectorFlow` since 2026-11-17 (lane GX4; its own
       copy of the flow gone, and before it, lane GX, its copy of `StepWindow`).
   - **Unified 2026-11-17 (lane GX):** ME1's `StepWindow` is `galaxy_projector::StereoWindow::Step` and its held view
     `galaxy_projector::HeldView`/`PutRenderHold` (`game/GalaxyProjector`), which ME1's `GalaxyMap` and ME3's
     `Le3GalaxyCapture` both call; the host's hold is `galaxy_hologram::HoldViewBegin/End` for LE1's seam and the sequels'
     `MevrSeams::WorldViewHoldBegin`; the window's and the UI panel's composition is `plugin/SequelOverlays`' for both sequels.
   - **To unify:**
     - `vr::galaxy_map::ReadTableSettings` and one host helper for both bridges;
     - LE2 adopts LE3's input lease;
     - ME1's `game/GalaxyMap` and the sequels' `game/GalaxyProjectorFlow` share `game/GalaxyProjector` (the held view, the
       window) but not the flow around it (ME1's console use, its hologram on foot, its picks): joining them is ME1's code.
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
5. **The squad weapons tick. Unified 2026-11-16 (lane GF); the drawn menu removed 2026-11-18** (the owner: "just remove the
   custom menu and just always default to the classic menu"). `TickCustom`, the `native` adapter's menu half (`Read`,
   `Pause`, `Unpause`, `Stop`, `TileName`, `NextPage`), the paged inventory and `plugin/SquadPanel` are gone; the left
   trigger's pull goes through one rule, `core/vr/SquadMenu` `RouteTrigger` (ME1's direct open, or the game's own LB: the
   sequels always), and the sequels keep only `EquipLe2`/`FollowLe2` for the squad orders' `GiveWeapon`
   (`game/Le2SquadWeapons.inc`). The history below is the record of the unified tick.
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
12. **Melee routing. Unified 2026-11-17 (lane MEL; research/20 "The trilogy's one path").** Until then `game/Melee.cpp`
    (LE3) and `game/Throwables` `ik::throwing::RouteMelee` (LE1) were two gesture-to-dispatch paths over the same swing
    detector, and ME2 had none. Now `ik::melee::Gestures` is the one gesture core (rules `Physical`: ME1's two detectors
    exactly, compared in `tests/MeleeTests.cpp`; `Staged`: ME3's opt-in rules) and `game/Melee` the one dispatcher
    (`Strike`: ME1's order of reasons) over a per-game adapter; `game/Throwables` keeps only ME1's feed (its body frame, the
    grenade hand suppressed). ME3 keeps its staged rules until the owner answers question 7 (docs/trilogy-plan.md).
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
- LE2: `JournalFloorViewWrite`, `CineScreenHalfSeparationUU`, `OnSwapChainPresent`, and the inline `HotkeyDown` and
  `WorldUiOwnsGui` (the common `MevrSeams::WorldUiOwnsGui` since ME3 took it, lane OM2, 2026-11-16; defined in
  `plugin/MenusLe2.cpp` for both sequel hosts).
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
| R02-R09, R13, R15, R22-R24, R26, R30, R35 | **none** for LE2. LE3: docs/me3-overall-goal-handoff.md §4 and its step 7 (performance only, generally) |
| R10 | LE3: research/le3-recenter-transition-failure.md (other scenes, stale-copy injection); LE2: research/le2-motion-blur-guard.md (the headset) |
| R11 | research/le2-renderer-integration.md "Defaults from ME1" ("Also noted", [verify]); LE3 **none** |
| R12 | docs/knowledge/gotchas/me2-scale-set-during-load; LE3 **none** |
| R14, R16 | research/le2-hdr.md (HDR dropped by the owner 2026-11-14); research/le3-hdr-eye-copy-test.md; docs/le3-port-implementation-plan.md ledger "HDR reuse" |
| R17, R25, R32-R34 | — (done; checks only) |
| R18, R19, R20 | research/le2-renderer-integration.md "What is left of M2"; research/le3-shared-presentation.md "crop gates"; research/le3-native-renderer-audit.md |
| R21 | docs/le2-continuation-plan.md M5 (Veetor's timed interrupts); research/le3-dialogue-cinema.md "LE3 stereo cinema candidate"; L3-4 |
| R28, R29, R31 | **none** (R31: docs/galaxy-projector-handoff.md for LE3's own) |
| G01-G05, G13-G16 | docs/le2-continuation-plan.md M3; docs/vrik-porting-guide.md §5-6; research/le2-vrik-audit.md; docs/le2-port-implementation-plan.md P3; LE3: research/le3-body-pose.md "Native gates still required", L3-3 |
| G06, G07 | research/le2-le3-vr-optics.md; L3-5 |
| G08, G18 | docs/le3-port-reuse-audit.md "Holsters", "Locomotion and comfort"; docs/le2-port-implementation-plan.md P6 (holsters), P5; **none** for combat stance on LE2 |
| G09-G12 | research/le2-vrik-audit.md (LE2); **none** for LE3 |
| G17, G39, G40, G58 | docs/le2-continuation-plan.md M6 table; docs/le2-m6-native-checks.md; L3-8; research/le3-locomotion-audit.md |
| G19-G23, G26 | docs/le2-continuation-plan.md M4; research/le2-menus.md; research/le3-native-gui.md; research/le3-menu-pause.md; L3-4 |
| G24 | docs/le2-validation-coverage.md "Hacking and bypass" |
| G25 | docs/galaxy-projector-handoff.md "Remaining work" 1-5; docs/le2-m6-implementation-guide.md G; research/le3-galaxy.md; L3-9 |
| G27, G28 | docs/le2-m6-implementation-guide.md A; docs/le2-continuation-plan.md M6 table (wrist/gun, labels); L3-4 |
| G29-G35, G57 | docs/le2-m6-implementation-guide.md B-E; docs/le2-m6-squad-implementation.md; research/55; research/70; research/le3-powers.md; research/le3-squad.md; L3-6 |
| G36, G38 | research/le2-conversations-in-world.md (G36, release scope since 2026-11-19); docs/le2-m6-implementation-guide.md F; **none** for sound placement |
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

- **The reflection's class index outlived a load (every game). Fixed 2026-11-17 (lane MEL):** ME2 crashed in the console's
  `mevr instances` after a save's reload: `FindClass` fell back to a level's freed class kept in the by-name index (rebuilt only
  when GObjects' count changed). Entries are checked (alive, a class, that name) and the index dropped on every world change
  (`ue3::runtime::NoteWorld`); `TestStaleClassIndex`; gotcha `stale-class-index`.

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
  - The reticle's came 2026-11-17 (lane CH, R19): ME3's `SFXSFHandler_Reticle` movie through `SFXGUIInteraction.GetMovie`,
    its own ReflectCheck rows (`CheckLe3ScreenReticle`); still none of ME1's or ME2's names in the LE3 build.
- **LE3's voice model tier needed LE2 in the build. Fixed 2026-11-16 (lane GF):** the llama block in `CMakeLists.txt`
  now asks whether the game's host compiles `plugin/VoiceModel.cpp` (checked: an LE3-only configure has
  `MEVR_HAVE_LLAMA` and `core/voice/Llm.cpp`).
- **Two configuration values nobody reads.**
  - (fixed by lane U1: `RendererLe2::RecenterTiltDeg` is read by `plugin/RecenterTilt` since the LE2 tree calls
    `RecenterOrientation`.)
  - `PowerWheel::StatusJson` read the controller through `Ready()`, which is null on the sequels. **Fixed 2026-11-16
    (lane GF):** it asks `Up()`. The value shown was never empty there, though: the sequels' `MappedPower` ignores
    the controller and names the wheel's own selection.
- **The sequels' scope aim error left `bIsZoomed` set on a fault (G06). Fixed 2026-11-17 (lane FX2):** `Weapon::ZoomedAimError`
  journalled the write but put the flag back after the script call, outside any `__finally`; now as the vault's one-call
  writes (set and put back in a `__finally`).
- **LExSDKv2's generated LE3 header lists no `Engine.Actor.Location`**, though every game has it, so the generated
  "names" table has false entries. ReflectCheck, against the running game, is the authority; the table is a lead.
- **The sequels' life-size stereo could render a frame of upstream's (R23, R06). Fixed 2026-11-17 (lane FXR, research/30
  9.4):** the first pass after the 3D cinema screen and a process's first stereo frame latched the screen's or no plan
  (mode 0: half eye, `SwapEyes`, the convergence shift), and a pair rendered for the screen was routed with `SwapEyes` at
  the submit after it; a narrow view ME2 leaves unwidened rendered the eye's whole frustum through a narrow CPU window
  (edges culled); a frame shown flat while the flags said stereo (ME3's wait for a fresh pair, ME2's loading movie) showed
  one eye's off-centre frustum. The menus, maps and flat cinematics the review named never latch a plan: both calcviews
  return the raw view for them first.
- **`umbraEyeMatrix` wrote FViewInfo +0x260 on the sequels' render thread without SEH, and ME3's command gate was outside
  the address table (R07). Fixed 2026-11-17 (lane FXR):** both guarded; the gate is the LE3 row `Umbra command gate`
  (`exe::Entry::tail` for its `cmp [rip+X], 0`).

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
| LE1 | halcyon-le1 | 72 | 71 | 57 | run 76, out 4, ungated 28, unreferenced 6, unbuilt 4 | 22 of 54 | 89 |
| LE2 | halcyon-le2 | 72 | 61 | 54 | run 64, ready 8, out 15, ungated 25, unreferenced 2, unbuilt 4 | 48 of 54 | 4 |
| LE3 | halcyon-le3 | 74 | 62 | 56 | run 63, ready 8, out 14, ungated 27, unreferenced 3, unbuilt 3 | 49 of 54 | 2 |
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
| `ConversationUi` | - | yes | yes |
| `ConversationWorld` | yes | - | - |
| `CrashBreadcrumbs` | yes | yes | yes |
| `DrawReplay` | yes | yes | yes |
| `Entry` | yes | yes | yes |
| `EyeCopy` | - | yes | yes |
| `FlareWindow` | - | - | yes |
| `FloorLock` | yes | yes | yes |
| `GalaxyHologram` | yes | yes | yes |
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
| `KeptLocks` | yes | yes | yes |
| `Le3SceneCapture` | - | - | yes |
| `MainMenu` | yes | yes | yes |
| `MainMenuSettings` | yes | yes | yes |
| `MenusLe2` | - | yes | yes |
| `MetricStereo` | yes | yes | yes |
| `MevrMenuPointer` | yes | - | - |
| `MevrMenuTab` | yes | - | - |
| `ModLoader` | yes | yes | yes |
| `MotionBlurGuard` | yes | yes | yes |
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
| `PresentationHold` | yes | yes | yes |
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
| `WheelPanel` | yes | yes | yes |
| `WorldPanels` | yes | yes | yes |
| `WorldUi` | yes | yes | yes |
| `WristBeam` | yes | yes | yes |
| `XrHost` | yes | yes | yes |

- LE1: also `engine/CodeDump`, `game/GamepadMapping`, `ue3/Pattern`, `ue3/SafeNames`
- LE2: also `engine/CodeDump`, `game/GamepadMapping`, `ue3/Pattern`, `ue3/SafeNames`
- LE3: also `engine/CodeDump`, `game/GamepadMapping`, `ue3/Pattern`, `ue3/SafeNames`
- LE1 mod extras: `core/Json`
- LE2 mod extras: `core/Json`, `core/vr/GalaxyRay`
- LE3 mod extras: `core/Json`, `core/vr/GalaxyRay`, `game/Le3Gui`
<!-- /parity:hosts -->

### Host core files per game

<!-- parity:core -->
In all three hosts (58): `Exe`, `ExeCheck`, `Hook`, `Hotkey`, `Ini`, `Json`, `Log`, `PngFile`, `Version`, `Window`, `d3d11/ShaderReflect`, `gfx/Raster`, `gfx/Swf`, `gfx/SwfFont`, `voice/ScriptMatch`, `vr/AimRay`, `vr/Calibration`, `vr/CineScreen`, `vr/Colour`, `vr/CombatStance`, `vr/Comfort`, `vr/Controllers`, `vr/ConversationWorld`, `vr/FloorLock`, `vr/FrameProbe`, `vr/GalaxyHologram`, `vr/GalaxyMap`, `vr/GameMenuRequest`, `vr/GamePointer`, `vr/GuiEvents`, `vr/Haptics`, `vr/HostView`, `vr/HudPieces`, `vr/ImageConvert`, `vr/InteractLabel`, `vr/LayerComposite`, `vr/MenuPointer`, `vr/MetricStereo`, `vr/OmniRail`, `vr/OverlayPlacement`, `vr/PoseRecording`, `vr/PowerWheel`, `vr/PresentationHold`, `vr/Recenter`, `vr/ScopeArt`, `vr/ScopeCapture`, `vr/ScreenMatrix`, `vr/Sights`, `vr/SquadMenu`, `vr/SquadPlace`, `vr/TestRequest`, `vr/Thespian`, `vr/TransitionFade`, `vr/Upscale`, `vr/WheelCrop`, `vr/WorldUi`, `xr/Floor`, `xr/Input`.

| src/core (host) | LE1 | LE2 | LE3 |
|---|---|---|---|
| `d3d11/Blit` | - | yes | yes |
| `vr/CockpitHazard` | yes | - | - |
| `vr/ColourStrip` | yes | - | - |
| `vr/FlareWindow` | - | - | yes |
| `vr/MakoControls` | yes | - | - |
| `vr/MakoGunner` | yes | - | - |
| `vr/OmniPad` | yes | - | - |
| `vr/QuestPacing` | yes | - | - |
| `vr/RenderTarget` | - | yes | yes |
| `vr/Spectator` | yes | - | - |
| `vr/Sprint` | yes | - | - |
| `vr/SquadContext` | yes | - | - |
| `vr/SquadGesture` | yes | - | - |
| `vr/SquadParser` | yes | - | - |
| `vr/SquadRetreat` | yes | - | - |
| `vr/SquadStanding` | yes | - | - |
| `vr/Traversal` | - | yes | yes |
<!-- /parity:core -->

### HostServices per game

<!-- parity:services -->
Helpers applied, in order:

- LE1: `BaseServices`, `WithCrashBreadcrumbs`, `WithRenderer`, `WithVignette`, `WithSights`, `WithPadMerge`, `WithHudPieces`, `WithFloorLock`, `WithVoice`, `WithPowerWheel`, `WithLe1Modules`, `WithGameMenuRequest`, `WithScopeCapture`, `WithInteractLabel`, `WithMakoControls`, `WithSpectator`, `WithGalaxyMap`, `WithGalaxyHologram`, `WithWorldUi`, `WithLe1ExtraUi`
- LE2: `BaseServices`, `WithCrashBreadcrumbs`, `WithRenderer`, `WithVignette`, `WithSights`, `WithPadMerge`, `WithHudPieces`, `WithFloorLock`, `WithVoice`, `WithPowerWheel`, `WithTraversal`, `WithGalaxyHologram`, `WithScopeCapture`, `WithMenus`
- LE3: `BaseServices`, `WithCrashBreadcrumbs`, `WithGalaxyHologram`, `WithRenderer`, `WithVignette`, `WithSights`, `WithPadMerge`, `WithHudPieces`, `WithFloorLock`, `WithTraversal`, `WithVoice`, `WithScopeCapture`, `WithPowerWheel`, `WithMenus`

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
| `logWorldUiStats` | WithWorldUi | WithMenus | WithMenus |
| `mainMenu` | WithLe1Modules | WithMenus | WithMenus |
| `motionBlurProtected` | WithLe1ExtraUi | BuildServices | BuildServices |
| `processExiting` | BaseServices | BaseServices | BaseServices |
| `publishCameraOffset` | WithLe1ExtraUi | - | - |
| `recordWrite` | WithCrashBreadcrumbs | WithCrashBreadcrumbs | WithCrashBreadcrumbs |
| `reserveWorldUi` | WithWorldUi | WithMenus | WithMenus |
| `rightGripFree` | WithPadMerge | WithPadMerge | WithPadMerge |
| `secondEyeIsReplay` | WithRenderer | WithRenderer | WithRenderer |
| `setAimRay` | WithRenderer | WithRenderer | WithRenderer |
| `setConversationWorld` | WithLe1ExtraUi | BuildServices | BuildServices |
| `setFloorLockSample` | WithFloorLock | WithFloorLock | WithFloorLock |
| `setGalaxyHologram` | WithGalaxyHologram | WithGalaxyHologram | WithGalaxyHologram |
| `setGalaxyMap` | WithGalaxyMap | - | - |
| `setGameMenuOffers` | - | WithMenus | WithMenus |
| `setHandAim` | WithRenderer | WithRenderer | WithRenderer |
| `setHudPieces` | WithHudPieces | WithHudPieces | WithHudPieces |
| `setInteractLabel` | WithInteractLabel | BuildServices | BuildServices |
| `setMakoControls` | WithMakoControls | - | - |
| `setPowerLock` | WithPowerWheel | WithPowerWheel | WithPowerWheel |
| `setPowerWheel` | WithPowerWheel | WithPowerWheel | WithPowerWheel |
| `setProcessEventCallback` | BaseServices | BaseServices | BaseServices |
| `setScopeCapture` | WithScopeCapture | WithScopeCapture | WithScopeCapture |
| `setSight` | WithSights | WithSights | WithSights |
| `setSpectator` | WithSpectator | - | - |
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
| `setWorldUi` | WithWorldUi | WithMenus | WithMenus |
| `sightView` | WithSights | WithSights | WithSights |
| `statusWanted` | WithRenderer | WithRenderer | WithRenderer |
| `supportGripHeld` | WithPadMerge | WithPadMerge | WithPadMerge |
| `takeGalaxyPick` | WithGalaxyHologram | WithGalaxyHologram | WithGalaxyHologram |
| `takeGameMenuRequest` | WithGameMenuRequest | WithMenus | WithMenus |
| `takeGameUiRequest` | WithWorldUi | WithMenus | WithMenus |
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
| `engine/BodyBounds` | run | run | run |
| `engine/BonePose` | run | run | run |
| `engine/CallFunction` | ungated | ungated | ungated |
| `engine/ClipPose` | ungated | ungated | ungated |
| `engine/CodeDump` | ungated | ungated | ungated |
| `engine/ConversationHooks` | run | run | run |
| `engine/CoverAcquire` | run | ready | ready |
| `engine/Engine` | run | run | run |
| `engine/ExitHooks` | ungated | ungated | ungated |
| `engine/HairPose` | ungated | ungated | ungated |
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
| `game/AiWalk` | run | run | run |
| `game/AnimDriver` | run | run | run |
| `game/ArmIk` | run | run | run |
| `game/AuthoredMoment` | run | ready | ready |
| `game/BodyCalibration` | ungated | ungated | ungated |
| `game/BodyFollow` | run | run | run |
| `game/BodyProbe` | ungated | unreferenced | unreferenced |
| `game/BodyScale` | ungated | ungated | ungated |
| `game/CallTrace` | run | run | run |
| `game/CameraCollision` | run | out | out |
| `game/CameraProbe` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/CameraShake` | run | run | run |
| `game/CockpitPanel` | run | out | out |
| `game/CockpitProbe` | run | out | out |
| `game/CombatStance` | run | run | run |
| `game/Comfort` | run | run | run |
| `game/ConsoleRequest` | run | run | run |
| `game/ConversationExit` | run | run | run |
| `game/ConversationFlow` | run | ready | ready |
| `game/ConversationWorld` | run | ready | ready |
| `game/DepthOfFieldHold` | unreferenced | ungated | ungated |
| `game/Fingers` | ungated | ungated | ungated |
| `game/FocusPause` | run | out | out |
| `game/GalaxyDump` | run | ready | ready |
| `game/GalaxyMap` | run | ready | ready |
| `game/GalaxyProjector` | ungated | ungated | ungated |
| `game/GalaxyProjectorFlow` | unreferenced | run | run |
| `game/GalaxyRay` | out | run | run |
| `game/GameArt` | run | out | out |
| `game/GameMenuRequest` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/GamepadBridge` | ungated | ungated | ungated |
| `game/GamepadMapping` | ungated | ungated | ungated |
| `game/GuiCursor` | run | run | run |
| `game/GunKick` | run | run | run |
| `game/Hair` | run | run | run |
| `game/HairLook` | run | run | run |
| `game/HandAim` | run | run | run |
| `game/HandCalibration` | ungated | ungated | ungated |
| `game/HandContact` | run | run | run |
| `game/HandTargets` | ungated | ungated | ungated |
| `game/HeadSim` | ungated | ungated | ungated |
| `game/HeadTracking` | run | run | run |
| `game/HoloProbe` | run | out | out |
| `game/Holsters` | run | run | run |
| `game/HudPieces` | ungated | run | ungated |
| `game/Interact` | run | run | run |
| `game/InventoryProbe` | run | unreferenced | unreferenced |
| `game/Le2GalaxyCapture` | unreferenced | run | unreferenced |
| `game/Le2Traversal` | out | run | out |
| `game/Le3GalaxyCapture` | ungated | ungated | ungated |
| `game/Le3GalaxyRetention` | out | out | run |
| `game/Le3Gui` | unbuilt | unbuilt | run |
| `game/Le3Traversal` | out | out | run |
| `game/Mako` | run | out | out |
| `game/MakoControls` | run | out | out |
| `game/MakoGunner` | run | out | out |
| `game/Melee` | run | run | run |
| `game/MenuPause` | run | run | run |
| `game/OmniTool` | run | run | run |
| `game/Player` | run | run | run |
| `game/Posture` | run | run | run |
| `game/PowerWheel` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/Reflections` | run | run | run |
| `game/Reload` | ungated | run | run |
| `game/SceneCaptureRig` | run | run | run |
| `game/SceneWaits` | run | run | run |
| `game/ScopeCapture` | run | run | run |
| `game/ScopeParts` | run | run | run |
| `game/ScreenReticle` | run | run | run |
| `game/SequelConversationWorld` | unreferenced | run | run |
| `game/Sights` | run | run | run |
| `game/SkeletonProbe` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/SoundPlace` | run | ready | ready |
| `game/Spectator` | run | out | out |
| `game/Sprint` | run | run | run |
| `game/SquadAck` | run | run (ME1-only handles) | run (ME1-only handles) |
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
| `game/WorldDrawn` | unreferenced | run | run |
| `game/WorldUi` | run | run (ME1-only handles) | run (ME1-only handles) |
| `game/ZoomedShots` | unreferenced | run | run |
<!-- /parity:modules -->

### The three MevrBridge vocabularies

<!-- parity:seams -->
The common vocabulary (`namespace MevrSeams`, `src/halcyon/MevrSeams.h`): `yes` where the tree calls it (through an old name's forwarder too), `-` where it does not yet.

| MevrSeams seam | LE1 | LE2 | LE3 |
|---|---|---|---|
| `CineScreenFullRate` | yes | yes | yes |
| `CineScreenLayers` | - | yes | yes |
| `CineScreenWanted` | yes | yes | yes |
| `ConstantFpEye` | - | yes | yes |
| `ConversationHandover` | - | - | - |
| `ConversationInWorld` | - | yes | yes |
| `ConversationWorldOrComing` | - | yes | yes |
| `CopyIntoImage` | - | yes | yes |
| `EyeImageSize` | - | yes | yes |
| `FloorViewTranslation` | yes | yes | yes |
| `FrontEndBeforeTitle` | - | yes | yes |
| `FrontEndHold` | - | yes | yes |
| `FrontEndOnScreen` | - | yes | yes |
| `GalaxyOwnsPresentation` | - | yes | yes |
| `HideBodyForAction` | - | - | yes |
| `HoldLoadTail` | - | yes | - |
| `HoldPlainVr` | - | - | - |
| `HostEndsProcess` | - | yes | yes |
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
| `WorldUiHoldFrame` | - | - | yes |
| `WorldUiOwnsGui` | - | yes | yes |
| `WorldViewHeld` | - | yes | yes |
| `WorldViewHoldBegin` | - | yes | yes |
| `WorldViewHoldEnd` | - | yes | yes |
| `WrapSessionFunctions` | yes | yes | yes |
| `XrRuntimeIsSteamVr` | - | yes | yes |

Old names still forwarded to the common seams (inline, in the tree's `mevr_bridge.h`; removed once nobody calls them):

- LE2: `HotkeyDown`, `WorldUiOwnsGui`

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

- LE2 `game/AnimDriver`: `SFXGame.SFXModule_Gestures`, `SFXGame.SFXStuntActor`, `SFXGame.SFXStuntActor:BodyMesh`, `SFXGame.SFXStuntActor:HairMesh`
- LE3 `game/GameMenuRequest`: `Engine.BioSFHandler.HandleInputEvent`, `Engine.BioSFManager:m_FocusPanel`, `Engine.BioSFPanel:m_DefaultHandler`, `Engine.BioSFPanel:nmTag`, `Engine.PlayerController:m_oCachedScaleFormManager`, `SFXGame.MassEffectGuiManager.GetBrowserHandler`, `SFXGame.MassEffectGuiManager:m_oBWHandler`
- LE3 `game/GuiCursor`: `Engine.BioSFManager:m_FocusPanel`, `Engine.PlayerController:m_oCachedScaleFormManager`
- LE2 `game/Hair`: `SFXGame.SFXStuntActor`, `SFXGame.SFXStuntActor:BodyMesh`, `SFXGame.SFXStuntActor:HairMesh`, `SFXGame.SFXStuntActor:HeadMesh`
- LE2 `game/OmniTool`: `Engine.StaticMesh:LODInfo`
- LE3 `game/PowerWheel`: `SFXGame.BioPawn:m_oBehavior`
- LE2 `game/SquadContext`: `SFXGame.BioPawn.IsInCombatActionState`
- LE3 `game/SquadContext`: `SFXGame.BioActorBehavior:ActorGameName`, `SFXGame.BioPawn.IsInCombatActionState`, `SFXGame.BioPawn:m_oBehavior`
- LE3 `game/SquadWeapons`: `SFXGame.BioActorBehavior:ActorGameName`, `SFXGame.BioBaseSquad.GetMember`, `SFXGame.BioBaseSquad.GetSquadSize`, `SFXGame.BioPawn:m_oBehavior`
- LE3 `game/Thespian`: `Engine.BioSFHandler:oPanel`, `Engine.BioSFPanel.InvokeMethod`, `SFXGame.BioConversation.GetEntryText`, `SFXGame.BioConversation.GetReplyText`, `SFXGame.BioWorldInfo.EndCurrentConversation`
- LE3 `game/WorldUi`: `Engine.BioSFManager:m_FocusPanel`, `Engine.BioSFManager:m_lstPanels`, `Engine.BioSFPanel:m_DefaultHandler`, `Engine.BioSFPanel:nmTag`, `Engine.PlayerController:m_oCachedScaleFormManager`
<!-- /parity:names -->
