<!-- SPDX-License-Identifier: MIT -->
# Galaxy projector parity: implementation and research handoff

> **The work list is [docs/trilogy-plan.md](trilogy-plan.md)** (since 2026-11-16: one list for all three games, keyed by
> feature). The galaxy map is its feature T42, which follows this document's "Remaining work" 1-5 in order. This
> document stays as the record of the projector work: every attempt, the native facts and the ordered steps.

Updated 2026-11-17 (lane GX). The 2026-11-15 checkpoint (0f02fd38, landed as ccc9325d) is on main; its worktree is gone.
This is a resumption document, not a release or acceptance claim.

## 2026-11-17 (night): one flow for both sequels, the opening's wait cut (lane GX4)

research/le3-galaxy.md "One flow for both sequels" and research/le2-galaxy-table.md "The flow's rules on ME2" have the runs:
- **ME3 is on `game/GalaxyProjectorFlow`**, its adapter `game/Le3GalaxyCapture` (about 55 lines: the CIC's art, the retention's
  calls, the map's level). Every GX3 rule is the flow's now, so ME2 has them: the interlude, the on-foot reservation, the opening
  and closing bridges, the recenter kept inside the map, the lens flare, the opening's probe, `ship`/`flares`. A cinematic straight
  from the map that outlasts the closing's 3 s becomes the map's interlude (ME1's RememberTable after its ClosingHold); the closing
  is held by ME1's rule (a cinematic within 2 s of the held map's end), whatever the map's level.
- **The opening's 2.5 s wait is cut as ME1's**: the GalaxyMap sequence's own longest delay (ME3 `SeqAct_Delay_1`, ME2
  `BioSeqAct_Delay_0`, both 2.5 s) written to 0.05 s in the CIC, journalled, put back away from it and on unload
  (`mevr galaxy capture opening-keep|opening-cut`). ME3 opens in 0.11-0.12 s (was 2.56), ME2 in 0.19 s; the map camera's first
  frame on ME3 is the same as shipped (snapshots 0.27 s and 0.32 s in). Every ME3 opening held (0 unheld main views).
- **Found by the move:** ME3's room lease (20 s from arming) ran out inside the now 0.1 s opening when armed 20 s before the use;
  it now waits from the player's last moment at the projector and through the opening (`le3_galaxy_retention::Refresh`).
- **ME3 travel and arrival** on the shared flow as GX3 saw them (relay movie alone, arrival on the disc); 12 unheld main views in
  23,532, all in multi-second game-thread stalls under another lane's cold builds (the host's 500 ms freshness).
- **ME3 reload with the map open** (ME3 autosaves 2 s after a travel's A, in the map): the save loads into mode 11 with the CIC's
  art (`BioA_Nor_210CIC`) neither loaded nor requested, so there is no projector to re-arm on: the flat panel until X (ME1's rule),
  logged once with the world's streaming levels. The flow carries the old room's disc and view and re-arms from them if the
  CIC's art is there after a load (`flow::Adapter::armRetentionInMap`); it wasn't.
- **The opening's first 2-7 frames were a cinematic** (the 3D cinema screen) on both sequels until the mod's bridge published; the
  on-foot reservation now carries an opening cue the host holds (`Scene::holdPad[kOpeningCue]`, `plugin/GalaxyHologram`). Built,
  not yet seen in the game.
- **ME2**: the map's A and B are GUI events (`mevr guikey 12` worked where a replayed A didn't); the galaxy view's cursor stuck
  short of a cluster, so the relay jump wasn't reached (`mevr galaxy capture tab-left|tab-right` added for it). In a relaunch the
  console stopped opening the map at all (A on the selected console, mode 0): open, not ours. A hot reload with ME2's map open
  faulted the render thread (the depth of field restored on unload): the flow restores it only on foot now.
- The owner (2026-11-17 night): ME3 on by default; DOF off near the projector accepted; ray picking and ME2's scanner by ray are
  the next lane's (`flow::Adapter::pointAt`, not built).

Left: ME2's travel and arrival (a relay jump; `tab-right` then `mevr guikey 12`), the opening cue seen on both sequels, ME2's
console that wouldn't open after a relaunch, ME3's scan and orbit, the captures' post-process chain, the headset.

## 2026-11-17 (later): travel, the opening and closing, the band, reload, recenter (lane GX3)

research/le3-galaxy.md "Travel, the opening and closing, the band" has the runs and the facts; in short, on ME3:
- **Travel works on the projector.** The relay's travel stays in game mode 11: ME3 plays its relay movie over the map, which the
  renderer shows flat; the hold and the window never dropped (97,116 main views held at Shepard's, none from the map camera) and
  the arrival showed the new system on the disc at once. The window is now drawn only over a world frame (it stood beside the
  flat movie with old captures). The map's interludes that do change the mode (a conversation, a cinematic, a GUI question, a
  movie mode, a load) keep the table reserved and re-open it from the measured disc: ME1's rule (research/51 section 8.4) in
  `galaxy_projector::MapInterlude` and the retention's `Kept()`.
- **No flat or wrong-camera frame at the opening**: the opening's cinematic (0 -> 8 -> 11, 2.5 s) is held at Shepard's view and the
  first map frame is held (an on-foot reservation); `galaxy hologram: the map's main views ...` counts them (0 unheld in five
  openings). X closes 11 -> 0; a closing cinematic would be held 3 s (ME1's ClosingHold).
- **The blue band was the projector's own lens flare**, uncovered when its hologram mesh is hidden: hidden with it now.
- **The retention re-arms** when its 20 s for the map ran out at the console (the map opened without the room before).
- **Recenter inside the map keeps the hold and the disc** (the held view and the app space move together); it went flat before.
  The null headset's recenter moves nothing, so the disc's place after a real one is the headset's check.
- **A reload with the map open** forgets the old room; ME3 loads the save into the map's mode, which the renderer then shows
  flat (no on-foot view in the new world: ME1's rule); X goes to the room and A opens the projector map again.
- **The opening's 2.5 s are a Kismet delay** (`biod_nor_203cic` `Main_Sequence.GalaxyMap.SeqAct_Delay_1`, Duration 2.50), not
  loading: the map's level is loaded 0.06-0.11 s in and made visible as the delay ends. A journalled `Duration` write (ME1 cut
  its `BioSeqAct_Delay_0` to 0.05 s) would shorten it: the owner's choice, not done.
- ME3 autosaved once during these runs (on an X exit); the runner's restore put the save back.
- Tools: `galaxy-steps.ps1` (the map's controls replayed, `steer` to a target), `mevr galaxy capture ship|flares`,
  `make-walk-recording.py`'s `lt`, `rt`, `stick@DEG` and `+` combinations, the read-only opening probe's log line.

Left on ME3: the system scan (a replayed LT started none: the trigger's route on the fake headset is open), orbit (mode 12) and the probe (the late-game fixture has no scannable planet in reach;
the code treats 12 as 11), moving ME3's adapter onto GX2's shared `game/GalaxyProjectorFlow` (with these rules in it),
the captures' post-process chain ("Remaining work" 1 below), the headset (alignment, legibility, comfort, GPU cost).

## 2026-11-17: ME3's projector flow, on ME1's shared code (lane GX)

**What runs now on ME3** (renderer preview on the fake headset, the career's original AutoSave at the CIC console, loaded
read-only; runs `20261117-174710-211-LE3-gx1`, `-175453-896-LE3-gx2`, `gx3`; script `tools/le2-halcyon-run/galaxy.ps1`):
on foot the mod measures the projector every 2 s and arms the room's retention near it; one A at the console opens the
map (mode 11); the view stays Shepard's at the console, the CIC in stereo around it (no flat panel: the renderer's
galaxy demand stands down); the game's map is a stereo disc lying on the projector (2.64 m, 24 degrees off flat, ME1's
`windowScale` 0.7 of the measured 1.88 m radius); the map's Flash UI (system name, planet labels, Scan, Exit, the mission
card) is on our HUD panel on the same disc; the room's own projector hologram is hidden meanwhile; X exits, the hold, the
window and the retention end, the projector shows again. Snapshots: the session folders' `galaxy-*` and
`%LOCALAPPDATA%\MEVR\snapshots\*galaxy-gx*`.

**The design is ME1's, one copy** (Forks 3):
- `game/GalaxyProjector` (new, every game): `HeldView` + `Remember` (Shepard's view on foot with the floor lock's base,
  `host_view::HoldBaseZ`), `RenderHold` + `PutRenderHold` (the POV's addresses for the host's swap), `StereoWindow` (ME1's
  `StepWindow` core: the two `capture_rig::Rig`s, their size from the game window's aspect, CineScreen's parallel cameras,
  the scene's window fields and camera), `GameWindow`. ME1's `game/GalaxyMap` calls all of it (its `Hold` derives from
  `HeldView`); ME3's `game/Le3GalaxyCapture` is now only ME3's adapter: the projector (`Le3GalaxyProjector`), the room's
  retention (`Le3GalaxyRetention::Arm(pc, production)`: no 120 s cap while the map is open), its map modes 11/12, the
  map camera's centre, hiding the room's GalaxyMapProxy and its emitters.
- Host: `plugin/GalaxyHologram` `HoldViewBegin/End` and `Holds` (moved from LE1's GamePointer; the POV writes in
  `plugin/PovSwap.hpp`), called by LE1's `GalaxyViewSwapBegin` and by the common seam `MevrSeams::WorldViewHoldBegin`
  (ME3's calcview hook, galaxy first, then the world UI); `MevrSeams::WorldViewHeld` answers 2 for the galaxy (Shepard's
  head and body hidden at the held eyes); new seam `MevrSeams::GalaxyOwnsPresentation(mode)` in ME3's `d3d_capture.cpp`
  (`[MEVR]`): no galaxy or input-owner mono while the map is held. `plugin/SequelOverlays` composes the window under the
  panel and puts the panel on the disc (`hud_panel::Frame::screenUi` at `UiPanelPose`, the window's whole frame wide); the
  panel is the game pointer's surface where a game's map is picked by clicking (`Facts::galaxyUiPointer`; none on ME3).
- **The post-process chain leaves ME3's captures black**, any chain (the player's, `Engine.DefaultPostProcess`,
  `DefaultUIScenePostProcess`), although `plugin/Le3SceneCapture` logs the final post effect binding the capture's own target;
  without a chain the map looks as the game shows it (gx2's `map-on-raw` snapshot). ME3 captures without a chain by default
  (`mevr galaxy capture on-post|on-default|on-ui` are the diagnostics). Why the chain's output is lost is open (Ghidra notes
  below: 0x3D2D60/0x3D1F80 choose 0x36DA30, 0x36DAC0 or 0x36D760 by the family's +0x48, the effect's final flag and the
  upscale test at 0x3B1EC0; the finish 0x3534B0 draws scene color into the target unless the view's +0x4B4 is set).

**ME1 unchanged** (the same-pose check, fake headset, sessions `2026-11-17_18-22-02-gx-mine2` and
`2026-11-17_18-25-42-gx-base2`, main 1b1b702c against this change: `open BIOA_NOR00`, Shepard put at the CIC's console,
`mevr galaxy use`, `exitGalaxyMap`): every layer's pose, `hostView`, `galaxyHologram` and `gamePointer` equal; on foot and after
the exit both eyes and the HUD pixel for pixel; with the map open the eyes within 0.12 of 255 on average (no pixel over 8),
the HUD 0.13% of pixels, the window's captures 4% (the map's opening still moving); `mevr-galaxy-preview`'s twelve PNGs byte
for byte. Saves unchanged in all sessions. (`open BIOA_NOR10`, which tools/launch-game.ps1 names, opens nothing: NOR00 is
the Normandy's persistent level.)

**ME3's map is flown, not picked.** Its system map moves the Normandy with the left stick (or mouse deltas:
`PC_GalaxyMouseMovement`/`Strafe`), A travels or buys fuel, LT scans, X exits. With the panel as the pointer's surface the
movie took the pointer's mouse (`GalaxyMap.SendMouseEvent(6)` at the answered cursor) and the trigger clicked, but a click
on the Pax label (`gx4`) or on Exit (`gx5`) changed nothing, and a ray on the panel kept LT from the map (the pointer owns
the triggers). So ME3 keeps its own controls on the projector and the panel is no pointer surface
(`Facts::galaxyUiPointer` 0). Whether the ray should steer the ship (a new interaction) is the owner's call.

**Seen and not explained:** with the map open a blue horizontal light band crosses the pit at the disc's height in the eye
images (`gx1`..`gx5`, `-left-world.png`: the world layer itself), not there on foot; the room's GalaxyMapProxy and its emitter
are hidden then, so it is something else the map's mode shows (the map level's own flare or effect seen from the CIC?
[verify] with `mevr find` near the projector while the map is open).

**Not done on ME3** (in order): travel and its movie, orbit (mode 12) and
the scan, the opening and closing transitions (a frame of the native presentation before the hold publishes is
possible: not measured), recenter and session changes inside the map, save reload, then the headset.
**ME2**: since 2026-11-17 (lane GX2) the same flow; the next section.

## 2026-11-17: ME2's projector flow, on the sequels' one flow (lane GX2)

**What runs on ME2** (fake headset, John's `Save_0036` read only, stretch `20261117-192445-759-LE2-gx2`, runs `galaxy-full1-200157`
and `galaxy-final-201028`; research/le2-galaxy-table.md "The projector flow"): Shepard put at the CIC's elevator arrival streams
the CIC in, at the console A opens the map (mode 9), the view stays Shepard's with the CIC in stereo, the game's map is a stereo
disc on the projector (2.64 m across, 24 degrees off flat), its Flash UI on the panel on the same disc, the room's hologram
hidden; X exits and the view is the game's again. The map's opening keeps the CIC loaded (no retention on ME2).

**The code is one flow for the sequels** (Forks 3): `game/GalaxyProjectorFlow` (the shape of ME3's first adapter, lifted with a
per-game `flow::Adapter`: the CIC's art visible, an optional retention, the depth of field) and ME2's adapter
`game/Le2GalaxyCapture` (`BioA_Nor_110`; the projector and console have ME3's tag and mesh, so the finder is one rule).
**ME3's `Le3GalaxyCapture` moves onto it next** (after lane GX3's interlude and on-foot reservation land: carry both into the
flow so ME2 gets ME1's travel rule too). ME2's renderer: `GetMenuMode` not flat in modes 9/10 while
`GalaxyOwnsPresentation`, `WorldViewHoldBegin/End` in `calcview_hook.cpp` (map modes only), the first person's hides at the
held eyes (`WorldViewHeld` 2). The capture rig runs on LE2 without any capture hook (no post-process chain, as ME3's).

**ME2-only finding:** with the game's depth of field on (upstream's `MELE2VR.ini`), the first held frames faulted the render
thread at `MassEffect2.exe+0x98C25D` (the game hangs), with or without captures; the flow turns DOF off near the projector on
foot and back away from it (`depthOfFieldOff`, `game/DepthOfFieldHold`; gotcha me2-scale-set-during-load).

**No pointer** on ME2's map UI (`galaxyUiPointer` 0): a GUI-event click on Exit did nothing; the map keeps its own controls.
ME2's opt-in flat table is retired.

**Left on ME2:** travel (a relay's movie mid-map, ME1's 8.4 rule: GX3's interlude), orbit (mode 10) and the scanner in the
window (the scanner's own stick and triggers: one probe a press), a briefing interrupting the map, recenter inside it, the
headset; ray-aimed scanning is the owner's question (not built).

The user requested the 2026-11-15 checkpoint to preserve work before usage runs out; its record follows.

## Intended result and current truth

ME2 and ME3 should open their native galaxy maps over the Normandy's existing
preview projector, as ME1 does. The player remains in the CIC, sees the room in
stereo, and can look around while using the native map. A smaller floating flat
screen positioned from the head does **not** satisfy this requirement.

**ME3 is not there yet.** We have measured the physical projector, kept CIC art
resident through a normal map entry, and produced native map imagery in two
independent capture layers. A post-processing defect was isolated and its ME1
fix adapted, but that correction has not executed in an accepted native run.
The main camera still shows the native fullscreen presentation. The room-view
hold and coordinated label/input projection are the largest remaining pieces.

**ME2 also lacks the full ME1 projector flow in the sources inspected.** Both
the main checkout and this worktree contain `HalcyonLe2Bridge::PlaceGalaxyPanel`
and `NativeTablePanel`: an opt-in repositioning of the complete native flat
image, with the existing flat backdrop. It defaults off (`nativeTable = 0`).
It does not retain/render the CIC around the physical projector or create a
native stereo capture pair. `research/le2-galaxy-table.md` explicitly identifies
those as future increments. This is a source audit, not a new ME2 runtime test
or an inventory of every other agent's unfinished worktree.

The earlier conversational estimate of 60% was an informal engineering estimate
for this galaxy task, not the whole ME3 port and not a measured acceptance score.
Use the gates in this document instead of treating that number as proof.

## Where the work lives

- Active worktree: `.claude/worktrees/agent-a08810c1900a1136a` relative to the main
  repository; branch `worktree-agent-a08810c1900a1136a`.
- Last base before this capture checkpoint: `3033433d`, bounded CIC residency.
  Preceding commits: `84581f21` (unloading evidence), `a34160f8` (shared geometry
  and measured anchor), `8eed57d4` (corrected projector acceptance requirement).
- Find the subsequent capture checkpoint with
  `git log --oneline --all --grep="ME3 galaxy capture"`. Do not infer deployment
  or main integration from the existence of that commit.
- Integration update (2026-11-15): checkpoint `0f02fd38` and main's handoffs
  were reconciled and landed on local main as `ccc9325d`. Concurrent cockpit
  and launcher edits remain preserved and uncommitted. This is source integration,
  not runtime or player-installation acceptance.
- Runtime artifacts and decompilation are private under the existing MEVR data
  directory. Private local runners/recordings are in this worktree's ignored
  `local-tools/le3-native-tests`; keep them while resuming. Do not prune this
  worktree merely because the agent stopped. No game data belongs in git.

Read these before changing code:

1. [ME3 implementation ledger](le3-port-implementation-plan.md) and
   [reuse audit](le3-port-reuse-audit.md).
2. `research/le3-galaxy.md` in implementation checkpoint `0f02fd38`, especially the projector
   correction, retention, independent capture and camera contracts.
3. [ME1 galaxy research](../research/51-galaxy-map.md): projector placement,
   scoped camera hold, label projection, transition/suspension fixes.
4. [ME1 capture defect and fix](../research/47-cockpit-in-game.md), section
   “Why a capture took an eye”.
5. [ME2 table implementation](../research/le2-galaxy-table.md),
   [scanning readiness](../research/le2-scanning-readiness.md) and
   [fixture research](../research/le2-galaxy-fixture-source.md).
6. `CLAUDE.md`, `.claude/skills/fake-headset-run/SKILL.md`, and
   [protected runner](../tools/le2-halcyon-run/README.md).

## What was attempted and what it established

| Increment | Evidence and result | Limit |
|---|---|---|
| Native map navigation and flat table | Existing native image, native selection and transitions; shared table placement | Not physical projector parity |
| Physical anchor measurement | Unique live projector mesh/component; shared ME1 radius calculation | Measurement alone does not render a map |
| Streaming audit | Normal map entry unloads `BioA_Nor_210CIC`, retains `BioA_Nor_200`, loads `BioA_Nor_203aGalaxyMap` | Camera hold alone would point into missing room art |
| Bounded room residency | Protected run `20261115-214310-005`; 541 metadata checks; same projector remains loaded in mode 11 | Only this fixture and normal entry/exit proven |
| Native exit correction | B backs out of orbital view; X exits the tested system-map view | Use the actual mode's native binding |
| Shared capture rig integration | Two SceneCapture2DActors, two targets/resources, shared per-eye layer renderer | First post-processed images were entirely black |
| Reflection discrepancy | Initial 602/603 startup check stopped gameplay; native code proves the SDK bitfield layout is wrong | Do not weaken checks or edit SDK to hide discrepancy |
| Corrected metadata check | Run `20261115-220151-721`, 603/603; capture lifecycle and X cleanup pass | Zero snapshot errors did not mean visible content |
| Raw capture isolation | Run `20261115-220823-943`; both eye captures contain native galaxy imagery with post-processing disabled | Diagnostic appearance is too bright; not final stereo/comfort acceptance |
| Shared post-effect target fix | Native LE3 code confirms ME1's failure mechanism; adapter and shared helper compile | Final runtime attempt did not launch a game |

The post-processed baseline snapshot `2026-11-15_22-03-11-103` has **zero**
nonblack RGB pixels in either galaxy-window layer. Raw snapshot
`2026-11-15_22-09-43-357` is 1014x576 per displayed layer, with 210,314 nonblack
pixels left and 208,902 right; 227,956 pixels differ between eyes. Both images
were visually inspected. These numbers prove image production and difference,
not correct stereo depth, acceptable brightness or physical alignment.

The correction's attempted run `20261115-221710-284-LE3-navigation` timed out
after Steam was asked to launch: **no MassEffect3 process within 240 seconds**.
No corrected-image evidence exists from that attempt. The wrapper closed out,
restored the prepared files with zero differences and released the game lock.
Do not confuse its receipt message with an actual renderer/gameplay pass. Do
not repeatedly restart Steam/EA as a substitute for inspecting launch state.

## Implementation inventory and reuse boundary

| Files/system | Work in this checkpoint |
|---|---|
| `game/Le3GalaxyProjector` | Existing read-only native projector/streaming observer; no duplicate anchor algorithm |
| `game/Le3GalaxyRetention` | Existing bounded native streaming hook; new Armed/Active queries for capture lifetime |
| `game/SceneCaptureRig` | Existing ME1 rig enabled for LE3 after contract checks; LE3 reflection reach; missing particle flag excluded; spawn CDO flags restored in finally scope |
| `game/Le3GalaxyCapture` | Thin LE3 adapter: native map camera/centre, measured projector, shared baseline/crop, two shared rigs and scene publication |
| `engine/ReflectCheck` | Native capture signatures/fields plus camera location/rotation checks; independent native matrix-bit contract |
| `plugin/ModLoader` | Reuses the same galaxy scene publication service for LE1 and LE3; clears it on unload |
| `plugin/GalaxyHologram` | Existing per-eye renderer now accepts LE3 semantic map modes; no copied galaxy renderer |
| `plugin/HalcyonLe3Bridge` | Submits the shared per-eye window layers; does not yet remove native fullscreen output |
| `plugin/SceneCaptureTarget` | Extracts ME1's guarded native target read/surface bind/viewport operations |
| `plugin/RenderLevers` | ME1 calls that extracted helper; retains its existing capture/eye isolation behavior |
| `plugin/Le3SceneCapture` | LE3 capture/render scope and final-target correction, using the shared policy/helper |
| `engine/Addresses`, `plugin/Entry`, CMake | Checked native contracts, host-lifetime attachment, sources for appropriate game targets |

The capture command is development-only orchestration, not the final UX:

```
mevr galaxy capture on
mevr galaxy capture on-raw
mevr galaxy capture state
mevr galaxy capture off
```

Arm while on foot at the projector with valid floor tracking. It claims the
existing bounded retention lease; normal game input still enters the map.
The capture diagnostic waits up to the lease's entry deadline, then lasts at
most 120 seconds in map mode. It stops on lease/map exit, unavailable native
camera, invalid tracking floor, session/recenter change, or explicit off.
Status deliberately reports `roomViewHeld:false` and
`presentationVerified:false`. It is not enabled automatically.

It currently uses 1024x576 capture targets, matching the protected preview's
16:9 native image. Derive production aspect/resolution from the authoritative
native view before making this a default; do not hardcode headset aspect.

## Native facts needed to resume safely

- ME3 galaxy/orbit are semantic map modes **11/12**; ME2 uses **9/10**. ME3 mode
  9 is GUI. Do not broaden ME1's raw mode-9 checks into a trilogy implementation.
- Measured projector centre is approximately `(-0.48, 3166.42, 92.20)` world
  units, radius 188.2 units. These are evidence, not constants to ship. The live
  adapter must measure each valid instance and convert using the current floor.
- The projector actor/component are recreated on exit. Reacquire objects;
  pointer identity from the preceding opening is not ownership.
- Tier evaluator `0xAC67C0` always runs natively. The bounded hook only overrides
  the CIC art descriptor after native evaluation; it does not globally freeze
  streaming or alter campaign flags. Fresh native rebuilds own their requests.
- `Camera.GetCameraViewPoint` body `0x46FC60` reads CameraCache location `+0x448`
  and rotation `+0x454`. FOV may be locked: reuse `CameraFovEdit`'s actual getter
  selection, not an unconditional cache-FOV write.
- `SetCaptureParameters` `0x700500` uses matrix flag **+0x174 mask 1**. Generated
  LE3 C++ SDK layout incorrectly places it four bytes later. The live reflection
  row now compares against native evidence rather than the wrong C++ offset.
- Capture loop `0x368850` invokes leaf thunk `0x344540`; native capture renders
  use `0x365370` / `0x3685C0`. Final post effects `0x3D1F80` / `0x3D2D60` bind a
  global backbuffer through `0x36D760` and mark view `+0x4B4`, causing normal
  target finish `0x3534B0` to skip its copy. This explains the empty targets.
- `GDynamicRHI=0x1894018`; family target at renderer `+0x18`; target surface
  virtual `+0x8`, dimensions `+0x18/+0x20`, RHI binding/viewport `+0x520/+0x340`.
  Exact address-pattern gates precede the new hook installation. Capture depth
  attaches last, so a partial installation remains pass-through. Thread-local
  capture state is restored through finally scopes. Ordinary eyes stay native.

Keep raw decompilation private. The native names above describe our verified
contracts; they are not a claim that we possess official engine symbols.

## Remaining work, in order

### 1. Validate the final-target correction before adding camera behavior

**Status 2026-11-17 (GX):** ran. The correction binds (`LE3 scene capture: final post effect bound its own 1552x1120
target`), but every chain's picture stays black; ME3 captures without a chain now, which shows the map correctly
(the top section). The `navigation.ps1` named below went with its worktree: `tools/le2-halcyon-run/galaxy.ps1` replaces it.
Open: where the chain's output is lost (the top section's Ghidra notes), if bloom or the tone curve are wanted on the map.

Use the existing protected original-AutoSave fixture and normal A interaction.
Run `navigation.ps1 -OriginalAutoSave -ProjectorAudit -CaptureAudit -HoldSeconds 60`
from the ignored native-test directory in this worktree. The script already
backs up/restores settings/saves and verifies ownership. First inspect the game
lock and real processes. Never bypass another session's ownership.

Require exact host address acceptance, 603/603 native metadata checks, the log
`LE3 scene capture: shared final-target correction installed`, and actual final
post effects binding their own targets. Inspect both galaxy-window PNGs and
the main eye images. Require visible correctly colored map content and no
capture image corrupting the eye backbuffer. Compare against raw/baseline above.
Exercise native X exit and confirm capture destruction/publication cleanup.
If the images remain wrong, trace target resolve/viewport and post-chain order;
do not call a successful object spawn or layer submission a visual pass.

### 2. Hold the main view in the CIC using ME1's architecture

**Status 2026-11-17 (GX):** done on ME3 with ME1's code (`galaxy_projector::HeldView`, `PutRenderHold`; the host's
`galaxy_hologram::HoldViewBegin` through `MevrSeams::WorldViewHoldBegin` in ME3's calcview hook, which keeps its caller and
primary-player checks and its FOV guard; `GalaxyOwnsPresentation` in the presentation decision). Not yet measured: the
first frames of the opening and closing, a briefing interrupting the map, recenter inside it, tracking loss.

Reuse `game/GalaxyMap::OnUpdateCamera`, `core/vr/WorldViewHold`, and
`HostView::HoldBaseZ` patterns. Remember the unheld on-foot camera and room
reference before native entry. Preserve the corrected standing/seated base
without applying eye/head offsets twice. Shared null-headset height is 5 ft
9 in (1.7526 m); recentered replay head Y remains zero.

Observe the native map camera after its own update and use that untouched POV
for map captures, cursor selection and label projection. Refresh native POV
addresses through the current owned camera; do not store stale addresses over
reload. Only the primary main-view CalcSceneView should see a temporary CIC
POV/FOV swap. Reuse LE3's existing caller **and** primary-local-player checks,
and the verified FOV getter guard. Restore every changed value on all exits.
Auxiliary label/GUI scene views must retain native map projection.

Audit the renderer's full chain of map/menu/cinematic mono gates. Suppress the
flat map path only while an authoritative valid room-hold/window state owns
presentation. Avoid basing game-thread ownership on the previous Present's
success. ME1's later suspension fixes are essential for briefing interruptions.
On missing tracking/invalid world/capture failure, release coherently into the
existing native fallback rather than leaving a frozen room or blank screen.

### 3. Complete projector composition, labels and pointer input

**Status 2026-11-17 (GX):** the window on the measured disc (ME1's `windowScale` 0.7, round, backdrop 0.8), the Flash UI on
the HUD panel on the same disc (labels on their objects in `gx2`'s raw snapshot), the room's GalaxyMapProxy and its emitter
hidden. Picks: none on ME3 (its map is flown with the stick; `gx4`/`gx5` clicked a label and Exit through the movie's mouse
to no effect; the top section). Not decided (the owner): whether the ray should steer the ship (a new interaction) or ME3's
own controls stay, as now.

Use the live measured projector's centre, plane, orientation and radius.
Check the round mask against rectangular capture aspect: current displayed
width and inscribed disc diameter are not interchangeable. Match the physical
preview's size and orientation visually from several head positions.

Reuse ME1's native UI capture/panel placement and map-camera projection
contract. Labels, leaders, rings and cursor must use the exact camera sequence,
FOV and aspect of the captures; review research/51's moving-label fix. The
world-view hold must not contaminate native selection projections. Register
the pointer on the actual submitted surface, with existing ownership/close
latches. Remove the duplicate fullscreen presentation only once the room,
window and UI are coherent. Hide/restore the decorative preview only when
needed and only while our presentation owns it.

### 4. Prove lifecycle and native gameplay

- Repeated opening, normal X exit, galaxy/orbit/back transitions and recenter.
- Lost tracking, headset/session replacement, explicit off, timeout and mod reload.
- Same-map save reload and world/mission changes: no stale objects or owned flags.
- Briefing/cinematic interruption and return: native scene owns the view, held
  reference survives only when appropriate, no forced traversal during seated scenes.
- Native navigation, travel confirmation and labels at several zoom levels.
- ME3 scan pulse, alert/chase/escape, orbital scan/probe/reward, and native save
  persistence. Existing observer/requests are candidates, not accepted outcomes.
- Restore saves/INIs exactly, release the shared game lock, and retain receipts.

Only then enable by default and request a physical-headset check of alignment,
legibility, comfort and GPU cost. The null driver cannot establish real headset
frame pacing, motion comfort or intuitive physical interaction.

### 5. Apply the same shared flow to ME2 with its own native adapter

**Status 2026-11-17 (GX2):** items 1-4 done (the section "2026-11-17: ME2's projector flow"); item 5 (orbit and the scan) left.

**What ME2 needs, after GX's ME3 work (2026-11-17)** (the shared parts exist: `game/GalaxyProjector`, the host's
`galaxy_hologram::HoldViewBegin/Holds`, `plugin/SequelOverlays`' window and disc panel, ME2's host compiles them already):
1. ME2's renderer: call `MevrSeams::WorldViewHoldBegin/End` around the primary player's main view in its calcview hook, as
   ME3's `calcview_hook.cpp` does (`[MEVR]`), and `MevrSeams::GalaxyOwnsPresentation` where its presentation decides the
   galaxy's mono (`src/halcyon/le2/d3d_capture.cpp`); `MevrSeams::WorldViewHeld` for the first-person hides.
2. The capture rig on LE2 (`game/SceneCaptureRig` is LE1/LE3 only): ME2's rows for its classes and functions in
   `engine/ReflectCheck` (CheckLe3CaptureRig's list), then `#if` it in. ME3 showed that captures **without** a post-process
   chain need no target hook (the normal finish draws them), so try that before ME2's final-target addresses (R31).
3. ME2's adapter (an `Le2GalaxyCapture` beside ME3's, the shape of `Le3GalaxyCapture`): the CIC's projector (its actor,
   tag and mesh in ME2's Normandy packages: unread), whether the map's entry unloads the CIC (ME3's retention question), the
   map camera's centre (`BioCameraBehaviorGalaxy.m_pCenterObject` in ME2 too, [verify]), modes 9/10.
4. `Facts::galaxyUiSteered` for ME2 once its map's movie is seen to take the GUI events; ME2's opt-in table then retires.
5. Planet scanning (mode 10): the window holds the planet's capture the same way (the map modes include orbit); the scanner
   stays on ME2's own stick and triggers (its `NativeMapInput` latch), one probe a press; whether the ray should aim the
   scanner is the owner's call (a new interaction).

Audit other pending ME2 branches before writing another implementation. Reuse
the existing table controller, native input/trigger guards, shared capture rig,
stereo window renderer, target helper and room-hold policy. Discover ME2's
actual projector and streaming behavior on a normal Normandy save. Verify
ME2 spawn/component/camera/post-effect contracts independently before enabling
the rig or target hooks; do not copy LE3 RVAs, classes or tier-manager assumptions.

Wire ME2's publication service and semantic mode gates. Preserve native analog
orbital scanner controls and trigger-release behavior across entry. Use the
existing one-probe/zero-probe resource fixtures and save codec for deterministic
checks of mining yield/probe consumption and persistence. ME3's pursuit/resource
rules are not ME2 mining semantics. The final acceptance remains the physical
projector with CIC surroundings, not just the opt-in flat table.

## Validation, cleanup and integration notes

All three development host/module pairs and all three monolithic player builds
pass. The separate LE1 host/module/test build also passes after extracting the
common target helper. The 71-test run passed 68 in the sandbox; installer
temp-path and local HTTP fixture tests failed there. All three pass in the
focused unsandboxed rerun: **71/71 pass across the suite and justified rerun**.
SPDX, whitespace and knowledge-link checks pass. These checks do not establish
native rendering or physical-headset acceptance of the new target correction.
No game or build process from this checkpoint remains intentionally running.

Source integration is recorded above; no production installation is implied. Normal cleanup restores
the user's files; do not install this unfinished galaxy presentation for them.
Never run `streamlevelin`, never return the game slot without asking the owner,
and never link a DLL while the running game owns it. Use the existing scripts
and mod request channel; computer use is only a fallback for launch.

## Wider ME3 goal

The [overall ME3 goal handoff](me3-overall-goal-handoff.md) now records every
milestone, renderer/body/save/interaction work, native evidence, remaining gates
and the full resumption order. This document remains the detailed galaxy subtask
handoff; it was not intended to replace that broader status record.

This checkpoint only advances galaxy milestone L3-9. The full ME3 parity goal
remains unfinished. The implementation ledger is authoritative for the other
lanes: combat fixture and edited-save persistence; native VRIK/weapon/headset
acceptance; wrist/UI outcomes; power/squad outcomes; production reload/optics;
melee/traversal validation; installer/release and campaign coverage. Existing
HDR/per-eye work must not be regressed while changing capture routing.
First-person world conversations remain deferred; seated scenes reject traversal.
The Hammerhead is outside the requested current scope.

## Suggested resumption prompt

> Read `docs/galaxy-projector-handoff.md`, the current ME3 implementation ledger,
> and the ME1 reuse references before editing. Locate and audit the capture
> checkpoint in `worktree-agent-a08810c1900a1136a`; preserve other agents' work.
> First validate the shared post-effect target correction on the protected ME3
> Normandy fixture. Then implement ME1-style primary-view CIC camera hold,
> exact map/UI projection and lifecycle restoration. The acceptance target is
> the native map on the physical preview projector with visible stereo CIC
> surroundings, not a floating flat screen. Reuse shared mechanisms and keep
> game-specific native adapters. Extend the proven flow to ME2 after auditing
> its existing candidates and verifying its own engine contracts. Preserve
> saves/settings, use the request/replay tools, one native run at a time, and
> report offline, native and physical-headset evidence separately.
