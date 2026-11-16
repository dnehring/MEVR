<!-- SPDX-License-Identifier: MIT -->
# Galaxy projector parity: implementation and research handoff

Updated 2026-11-15. Implementation checkpoint: **0f02fd38** on `worktree-agent-a08810c1900a1136a`. This main-checkout copy is documentation only; the implementation remains in that worktree. This is a resumption document, not a release or acceptance claim.
The user requested this checkpoint to preserve work before usage runs out.

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
- Main has concurrent work and substantial divergent history. Do not merge this
  entire old worktree wholesale or overwrite main's files. Audit the current
  diff and use the repository's reviewed landing workflow for selected changes.
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

No production installation or main merge is implied. Normal cleanup restores
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
