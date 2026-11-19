<!-- SPDX-License-Identifier: MIT -->
# ME2: the first traversal gate (a native vault, scripted, with a tracked presentation)

Date: 2026-11-14, lane E of [the continuation plan](../docs/le2-continuation-plan.md). The spec is the owner's design,
[research/le2-le3-vr-traversal.md](le2-le3-vr-traversal.md) (its proof order steps 1 and 2 for ME2, then the first presentation).
ME2's script and natives were read as in [research/le2-movement-source.md](le2-movement-source.md); the decompiled text stays in a
scratch folder, never in git. Everything here is ME2's exe 2.0.0.48602 (`MassEffect2.exe`, PE timestamp `0x60AEEFA5`).

## The owner's requirements (2026-11-14)

- "The conventional button is double a, just to note. We need to be able to vault while holding our weapons."
- "We won't snap to cover for VR, it's not a realistic experience. It risks the same problems of our skeleton not matching our
  players position in space." (lane F blocks ME2's cover acquisition for the player in VR; [research/le2-vrik-audit.md](le2-vrik-audit.md)
  "ME1's cover system on ME2")
- So: a double tap of the right controller's A, standing next to an obstacle the game can vault, facing it, commits one vault from
  where the player stands; a single A does nothing of ours; the gun stays in the hand.

## What ME2 does

**The binding.** A is `Console_Action` (BIOInput.ini): `Exclusive Used | TryClimbOrMantleCover | Exclusive TryAcquireCover |
OnRelease StormOff | OnHold 0.2 StormOn | OnTap 0.3 TryExitCover 1`. The game's double A is A into cover (`TryAcquireCover`, which
also switches explore to combat) and A again in cover (`SFXPlayerController.TryClimbOrMantleCover`). Used comes first: a selected
usable object takes the press.

**`TryClimbOrMantleCover`** (script, an exec function) needs the pawn's `BioPawn.CurrentLink`; it builds a `CoverLink.CovPosInfo` from
the pawn's cover link and slots, tries `BioPlayerController.CanPerformClimb` then `CanPerformMantle` (natives), and calls
`SFXPlayerController.ClimbCover` or `MantleCover` (script), which cache the cover info on the custom action and call
`DoCustomAction(9)` (climb) or `DoCustomAction(10)` (mantle).

**`CanPerformMantle`** (exec RVA `0xB38E40`, its body `0xABD8E0`), in our words:
1. the pawn is a BioPawn in the **combat** action state (`BioPawn.ActionState` = 1), not leaning, not in a blind-fire cover action,
   not busy with another custom action; **out of cover only when `BioPlayerController.m_bCanMantleOutsideOfCover` is set** (a config
   flag, `BIOGame.ini`: `m_bCanMantleOutsideOfCover=false ; set true to mantle from outside of cover`);
2. the left stick pushed forward: `RemappedJoyUp >= DeadZoneThreshold` and `|RemappedJoyRight| <= RemappedJoyUp`;
3. the slot has `bCanMantle`, and its `MantleTarget` slot is enabled on a link that isn't disabled;
4. the pawn within `m_fClimbMantleDistanceThreshold` (BIOGame.ini 50 units, measured from the pawn's centre to the slot point, in 3D)
   and facing within `m_fClimbMantleFaceAngleThreshold` (0.7: the dot of the pawn's facing and the slot's direction) of it;
5. the landing on the far side clear of other pawns (an overlap query at the target).

`BioPlayerController.UpdateCover` (`0xACE2A0`, the HUD's prompts) asks the same for the cover `FindPlayerCover` finds: out of cover
the mantle prompt never shows (the flag is off), the "take cover" prompt does.

**The move.** `BioCustomAction_CoverMantle` (`BioCustomAction_CoverClimbMantleBase`): `StartCustomAction` refuses unless the
behaviour is exploring or in combat, turns the pawn's actor blocking off (`SetCollision(bCollideActors, false)`) for
`m_fCollisionOffPerioud` (0.40 s), plays a body stance with root motion (`FromCover` 3 if the pawn was in cover, else `FromExplore`
1 or `FromCombat` 2), sets the physics to none and the velocity to 0. The anim's end (`BodyStanceAnimEndNotification`) calls
`EndCustomAction` and walking physics again; the player's stance blends out over 1.0 s. Nothing in the script uses the pawn's cover
state, so the move plays from wherever the pawn stands, along its facing.

## The route in VR (no cover snap)

`game/Traversal` (LE2 only; LE1 and LE3 stand down):
1. **Eligibility:** every 2 s it walks `WorldInfo.CoverList` (`CoverLink.NextCoverLink`) for slots with `bCanMantle` or
   `bCanClimbUp` (and `bEnabled`), their place and direction from the natives `CoverLink.GetSlotLocation` / `GetSlotRotation`, their
   target from `MantleTarget`. Each tick it takes the slot nearest the pawn (within `[Traversal] reachM`, 1.5 m) and every 0.1 s asks
   **the game's own `CanPerformMantle`** (or `CanPerformClimb`) with `CovPosInfo{Link, slot, slot, 0}`, with
   `m_bCanMantleOutsideOfCover` set and `RemappedJoyUp = 1`, `RemappedJoyRight = 0` **for that one call only** (each journalled,
   `write_journal::Wrote`, and put back in a `__finally`: `CallWithOneCallWrites`). The game's whole admission decides: distance,
   facing, slot flags, the target, the landing.
2. **Commit:** on the second press of a double A (core/vr/Traversal `PressTracker`), if the game admits it, the mod calls
   `SFXPlayerController.MantleCover(CovPosInfo)` (or `ClimbCover`) on the game thread through `ue3::runtime::Call`: the game's own
   custom action and authored move, from where the pawn stands. No cover state, no snap, no `CurrentLink`.
3. If the game began its own move on that press (cover not blocked: the in-cover A), the mod stands down.
4. A single A goes to the game as before: with lane F's block (`game/Posture` `OnFindPlayerCover`, `[Hands] noCover = 1`, the
   default; main 19d54a39) it uses what is selected or nothing. The first three stretches ran before that block landed, with a test
   stand-in, `set BioPlayerController MaxCoverEnterDist 0` through the console; `vault.ps1` now runs against the real block
   (`mevr cover snap auto`).

## The fixture

The VR Shepard career's Save 4 (Freedom's Progress, `BioP_ProFre`, the heavy pistol drawn), loaded by `tools/le2-halcyon-run/load.ps1`.
The spaceport (`BioD_ProFre_100SpacePort`) is loaded with it: 49 cover links, 10 slots that vault and none that climb.
`CoverLink_10` slots 0 and 1 vault over a low wall to `CoverLink_9` slots 1 and 0 (and back), `CoverLink_0` slots 3-5 and
`CoverLink_4` slots 0-2 over another (about 28 m and 56 m from where the save starts).

```
tools\le2-halcyon-run\prep.ps1 -Holder <you> -BuildDir <worktree>\build-trilogy
tools\le2-halcyon-run\launch.ps1
tools\le2-halcyon-run\load.ps1
tools\le2-halcyon-run\vault.ps1 -Runs 3 -Mode tap      # or -Mode go (no controllers), -Mode single, -Snapshots
tools\le2-halcyon-run\launch.ps1 -Stop
tools\le2-halcyon-run\restore.ps1 -Holder <you>
```

`vault.ps1` stands Shepard 0.25 m in front of `CoverLink_10` slot 0 facing it (`mevr traversal place CoverLink_10 0 0.25`: a test
placement through `Actor.SetLocation`/`SetRotation`), waits 2 s, then replays the double A through the right controller
(`tools/make-vault-recording.py`: two 0.08 s presses 0.15 s apart, hands up in view) and writes each run's `traversal:` lines into the
stretch's session folder. Console: `mevr traversal slots | near | place <link> <slot> [backM] [sideM] [yawDeg] | go | try | pause <s> |
view | mask | cue | trace | commit`.

## Measured (fake headset, 2026-11-14)

Three stretches (`%LOCALAPPDATA%\MEVR\sessions\*-lane-e-vault`, `-vault2`, `-vault3`; each restored with `verification: 0
differences`), the build of this branch, the heavy pistol drawn by the save.

**The gate: the same vault three times in a row from a script, one native action each** (`vault.ps1 -Runs 3 -Mode tap`, stretch 2):

| | run 1 | run 2 | run 3 |
|---|---|---|---|
| presses (the double tap) | #1, #2 0.23 s apart | #3, #4 0.26 s | #5, #6 0.23 s |
| the rules' verdict | allowed | allowed | allowed |
| custom action, body stance | 10, FromCombat | 10, FromCombat | 10, FromCombat |
| the custom action / the view at rest after it | 0.92 s / 0.78 s | 0.92 / 0.78 | 0.92 / 0.79 |
| the pawn: flat, rise, end | 2.45 m, 1.12 m, +0.00 | 2.45, 1.12, +0.00 | 2.44, 1.12, +0.00 |
| it blocked no actors | 0.39 s | 0.39 s | 0.39 s |
| the game's camera: travel, rise, peak speed (vertical) | 2.45 m, 0.54 m, 3.27 m/s (1.19) | 2.45, 0.54, 3.30 (1.21) | 2.44, 0.54, 3.32 (1.20) |
| its pitch and roll | 0..0, 0..0 deg | 0, 0 | 0, 0 |
| movement and aim back | at the custom action's end | the same | the same |
| the weapon before / after | HeavyPistol / HeavyPistol | the same | the same |
| the drawn wrists within 0.15 m of the grips | 41 of 59 ticks (mean 0.16 m) | 41 of 59 | 42 of 59 |

The pawn didn't move before the vault began (it began at the placed `(-4783, 20876, -12209)` each time). No move began without a
double tap: 0 moves the rules wouldn't have committed, and the game's own `TryClimbOrMantleCover` ran on every A and began nothing
(no cover link). A single A (`-Mode single`) is a "first tap" and nothing happens. Lines: `traversal: A pressed (#2): double tap`,
`traversal: double tap: MantleCover(CoverLink_10 slot 0) began the game's custom action 10`, `traversal: the vault began +0.000 s
after press #2 went down: allowed`, `traversal: vault over: ...` (the table's numbers).

**Again against the real block** (main 19d54a39, lane F's `[Hands] noCover = 1`; stretch `*-lane-e-hud`, 2026-11-15 00:02,
`vault.ps1 -Runs 3 -Mode tap` with no build running on the machine): three double taps (0.20, 0.23, 0.22 s apart), three vaults
allowed, each custom action 10 FromCombat for 0.90-0.91 s and the view at rest 0.77-0.81 s after it; the pawn 2.45 m on, 1.12 m up,
ended level, out of cover throughout (no snap: the first A of each pair did nothing visible); the camera level (pitch and roll 0),
its rise 0.54 m, at most 3.4-4.3 m/s (1.2-1.6 m/s vertically), its largest frame step 0.06-0.13 m; the heavy pistol before and
after; the drawn wrists within 0.15 m of the grips in 73 of 87, 79 of 87 and 134 of 145 ticks (mean 0.10-0.13 m, the misses the
root-motion steps below). A stretch run while three builds compiled on the same machine (`*-lane-e-vault4`) dropped the game to a
few frames a second: the second press of one double tap fell between frames and nothing happened, and the next vault's numbers were
nonsense; never measure the vault with a build running.

**How close and square a standing player must be** (the game's own rule, `mevr traversal place ... [backM] [sideM] [yawDeg]` then
`near`): within 0.50 m of the slot point in 3D, and the pawn's centre stands 0.31 m above it, so **within 0.39 m flat** (0.38 m back
admitted at 0.49 m, 0.42 m back refused at 0.52 m; 0.3 m to the side admitted, 0.4 m refused), and **facing within 45 degrees**
of the slot's direction (dot 0.70: 44 degrees admitted, 46 refused). The cover's top here is 0.75 m over the floor (a trace: z
-12224, the slot point -12240, the pawn's centre -12209, the eyes -12134). The facing that counts is the **pawn's** (the stick's
turn), not the head's: a player who turns toward the wall in the room but not with the stick is refused. Widening the distance or
turning the pawn to the slot at the commit is the owner's choice (a player-facing change).

**With the weapon away the game refuses** (`TryHolster` by the console, then `near`): `CanPerformMantle` needs the combat action
state. Natively the A into cover switched explore to combat first; with no snap nothing does. Firing the gun (combat) admits it
again. A holstered vault (draw on the double tap, or an admission with the state supplied) is the owner's choice.

**The view through the move.** ME2's camera (the POV Halcyon renders from) stays level: pitch 0 and roll 0 in every camera update
of every vault. It follows the body smoothly: 2.45 m on, rising 0.54 m over the wall and back, at most 3.3 m/s (1.2 m/s vertically),
and at rest 1.7 s after the start. The pawn itself moves in **root-motion steps** (still, then 1.12 m up in one or two frames at 0.38
s, 2 m on at 0.58 s, down at 0.8 s: steps of 1.1-1.5 m), which the camera doesn't follow step by step; its largest single-frame move
is 0.12 m. Eye snapshots: mid-vault (`mevr traversal pause 0.45` then `go`, paused 0.61 s in) the view stands over the wall, level,
the far side ahead; after it, the pistol in the right hand and the left hand low in view. **Nothing to take out:** the animation
pitches, rolls and bobs the body, not the camera.

**The hands.** Traced (`[Traversal] trace = 1`, a line per camera update with the drawn wrists' distance from the grips): 0.01-0.18
m through the move and the blend-out (0.07 at rest), **except two frames at each root-motion step**: 0.38-0.43 m at the rise,
0.87-1.07 m at the 2 m step, 0.21-0.56 m at the drop. Cause: arm IK computes its goals in the mesh's component space in the mod's
tick, and the next `UpdateSkelPose` writes them; when the actor jumps in between (the step), the goals ride the actor's jump for
that frame while the body (the root bone counter-moved by the engine) stays put. The arms and the gun flash forward 0.4-1 m for
about 30 ms, three times a vault. **The fix is arm IK's** (engine/BonePose, lane C's writer): keep the goals where the request put
them in the world when the mesh's component moved since (or skip the write for that frame, the animated arms then hold). Not built
here; it touches LE1's writer.

**Presentation options** (`[Traversal] view`, `mask`; all measured on the same fixture):
- `view = game` (default, today's): the camera as above.
- `view = level`: the game's camera written back with the start's pitch (0) and no roll, its height optionally filtered
  (`levelTauS`, default 0: off). 36 writes through the move and the hand-back, the same numbers as `game` (the camera was already
  level). It is the guard for a move or stance whose camera pitches; `levelTauS = 0.03` would smooth the 0.12 m frame step at a
  lag of 2 tauS times the vertical speed (7 cm at 1.2 m/s).
- `mask = vignette`: the comfort vignette (HostServices v17) at `vignetteStrength` (0.85) while the custom action runs (peak 0.85).
- `mask = blink`: black after 0.06 s, held 0.35 s at most, back over 0.15 s (peak 1.0; `traversal cue: first blink`: a head-locked
  quad 4.0 m wide at 0.5 m).

**Recommendation for the default:** keep `view = game` and `mask = none` (today's). ME2's own camera already gives the level,
tracked, smooth view the design asks for, and the move is short (0.9 s, the view at rest 0.8 s later) at walking speeds. Offer the
vignette as the comfort choice, as ME1's comfort page does for turning, and keep the blink for players who want the rise masked.
The cue (`[Traversal] cue = 1`) is worth turning on by default once the owner has seen it: without it the 0.39 m and 45-degree
window is hard to find. All of these wait for the owner's headset (notes/headset-checklist.md, section 4).

## The cue

`plugin/TraversalCue` (LE2's host, HostServices v40 `setTraversal`): the mod sends, each tick the game admits a vault or climb, the
point over the cover's top halfway to the target slot, 18 units up (`core/vr/Traversal` `CuePoint`; the top from a downward
trace), in the room's tracking space. The host draws a label ("Vault" with a chevron, "Climb up" with an arrow; GDI text into one
512x256 swapchain, the dark glass plate and orange rim of our menus) there, upright and turned to the head, 2.6 degrees tall
(between 4 and 20 cm); not while a move runs, a menu is open, or A would use a selected object. Measured: `traversal: the cue 'Vault'
shows over CoverLink_10 slot 0: the world point (-4667, 20841, -12206) is (+0.08, -0.73, -0.91) m from the head`, the host's
`traversal cue: first shown: 'Vault' ... 0.21 x 0.05 m`. Off by default (`[Traversal] cue = 0`).

## LE1 unchanged

The shared edits (`mod/Mod.cpp`'s tick and status block, `ModApi` v40, `ModLoader`, `ConsoleRequest`, `ReflectCheck`) leave LE1
inert: `traversal::TryStartup` returns false there. Checked with lane B's same-pose script (20 s in the world, a standing replay
with the hands in view, the console's `pause`, two snapshots; `tools/fake-headset-session.ps1 -Tag lanee-le1pose -BuildDir build`,
session `2026-11-15_00-04-13-lanee-le1pose`, the build of main 9ed1ed91 with this work) against lane D's `laned-pose` run of
23:09: the same six layers at the same places (the omni-tool target 1.1 mm apart), `hostView` identical (-43.3 / -4.1 / -18.1 uu,
100 uu/m), the HUD panel identical pixel for pixel, the eyes 0.10% and 0.06% of pixels apart by more than 32/255 (what animates
between launches); the snapshot's new `"traversal"` block says `started: false`, and the log has no `traversal:` line.

## The floor lock through the vault (lanes C3 and C4, 2026-11-16)

**The problem.** Once the LE2 host published a floor sample (2026-11-15) the floor lock ran on ME2, and the vault's hands left the
controllers again: `vault.ps1 -Runs 3 -Mode tap` gave 102 of 135, 107 of 142 and 99 of 133 ticks within 0.15 m (mean 0.33 m, max
1.49 m), with `arm IK: body anchor: third person ... view 104 units from Shepard's animated head bone` mid-vault. The floor lock
puts the eye at `pawn.Location.z - CollisionHeight + unitsPerMeter * the headset's eye height`, solved again every frame, and the
mantle steps the pawn's place up to 1.1 m at a time while the game's camera travels smoothly over the wall
(gotchas/floor-lock-through-a-traversal-move).

**The rule** (`vr::floor_lock::MoveHold` in `core/vr/FloorLock`, unit-tested in `tests/FloorLockTests.cpp`; the host's
`plugin/FloorLock` `Translate`): the mod's floor sample carries `PawnSample::traversalMove` (`game/Traversal`'s `Moving()`, in the
struct's padding after `seated`: the size and the HostServices version are unchanged). From the tick before a move begins, the
pawn's floor is held where it stood below the game's camera (`floorZ - rawCameraZ`: 163.0 units on the fixture) and the eye is
solved from that floor with the headset's live eye height, so the eye is the game's camera plus a constant and a player who ducks
mid-vault still ducks. A lock that comes on mid-move holds nothing until the next move.

**The release** (lane C4). Released at the custom action's end (lane C3's first version, 36bc92aa), the eye dropped 52 units in one
frame: the game's camera is still 0.51 m high there and at rest about 0.8 s later (the stance blends out over 1.0 s). So the hold
lasts past the move until the pawn's floor is back within `releaseM` (0.02 m) of the held one, that is until the camera has settled
over the pawn, and the eye then joins the fresh solve with a step of at most 2 cm; a camera that hasn't come back after `waitS`
(1.5 s) is eased there at `easeMS` (0.5 m/s). The log (every line says "traversal", so `vault.ps1`'s run files keep them):
`floor lock: a traversal move began: the pawn's floor held 163.0 units below the game's camera, correction +12.260 ...`,
`floor lock: the traversal move ended after 0.92 s (65 frames): the held eye's largest step 2.1 units a frame ...; solved from the
pawn's floor it would have stepped up to 59.0 units a frame; the game's camera is 50.9 units off its place over the pawn before
the move: held until it settles`, `floor lock: the traversal hold released 0.61 s after the move's end (42 frames, the camera
settled): ... the release -2.2 units`. The host's snapshot block `"floorLock"` has `"moveHold": "none" | "moving" | "settling"`.

**Measured** (fake headset, Save 4, `CoverLink_10` slot 0, `vault.ps1 -Runs 3 -Mode tap`, `[Body] floorLock = 1`; stretches
`20261116-165309-803-LE2-lane-c4-floorlock`, `...-165808-464-LE2-lane-c4-trace` and `...-172613-568-LE2-lane-c4-scaled`, each restored with
`verification: 0 differences`):

| | no floor lock (C2, 2026-11-15) | the floor lock, no hold (C3) | held to the action's end (C3) | held until the camera settles (C4) |
|---|---|---|---|---|
| ticks within 0.15 m, three vaults | 145/145, 143/143, 146/146 | 102/135, 107/142, 99/133 | 134/135, 134/135, 77/79 | 136/136, 134/134, 133/133 |
| mean, max | 0.07, 0.08 m | 0.33, 1.49 m | 0.07, 0.51-0.52 m | 0.07; right 0.08, left 0.11 m |
| `body anchor: third person` during a vault | none | each vault | none | none |
| the eye's largest step a frame | the camera's | 40-56 units (the pawn's floor) | 2.0 units, then 52 at the release | 1.7-2.4 units; the release 2.1-2.4 |
| released after the action's end | - | - | at once | 0.59-0.61 s (42 frames), the camera settled each time |

**The left wrist's 0.11 m** is the landing, not the release: traced (`mevr traversal trace on`, one vault), the left wrist is 0.10-0.12
m from its grip for 0.12 s of the blend-out (0.18-0.30 s after the action's end, the game's camera still 0.2-0.3 m over its rest), the right
0.07-0.09; the same vault with `[Body] floorLock = 0` stays within 0.08 m. With the lock the eye is the player's height, 12.3 cm
over the unscaled Shepard's camera (`correction +12.260`; body scale waits for Shepard standing holstered, and Save 4 has the pistol
out: `body scale: 1.000 ... waiting for Shepard standing holstered`), so the replayed hands are 12 cm higher too, and the left arm in
the landing crouch falls 4 cm short of them. Two more vaults (the trace's and a fourth) gave the same: 134/134 and 135/135, left 0.11, right 0.08 m (the floor lock off: 95/95, both 0.08).
A scaled A/B waits for the holsters on ME2: `mevr body scale` is LE1's console only, and a holstered stand is what lets body scale
learn Shepard's eye height (then the body is 1.075 times as tall and the arms reach with it).

**LE1 unchanged** (no traversal move: `MoveHold` passes every result through untouched; the snapshot's `"floorLock"` gains
`"moveHold": "none"`): LE1RESULT

## The two-handed hold (lane C4, 2026-11-16)

The owner, 2026-11-16: "For VR we may need to allow for vault with two handed, but we may want to consider vaulting with one hand.
The controls could get sloppy." With the left hand on the gun (the support grip) ME2 sees LT held, LT zooms the weapon
(`SFXWeapon_NativeBase.bIsZoomed`), and `CanPerformMantle`'s pawn test refuses a zoomed weapon (research/le2-vrik.md "Vaulting with
the weapon held"): every two-handed vault was refused. `GameAdmits` (`game/Le2Traversal.cpp`) now clears the held weapon's zoom bit
for its one call, next to `m_bCanMantleOutsideOfCover` and the stick's push (journalled `Traversal.bIsZoomed cleared (one call)`, put
back in the same `__finally`; the heartbeat counts `asked with the weapon's zoom cleared`; logged once `traversal: the weapon is
zoomed (the support grip's aim): its bIsZoomed cleared for the game's CanPerformMantle call only`). The game zooms on as before; the
commit's own recheck goes through the same admission.

**Measured** (fake headset, Save 4, the pistol in the right grip; stretches `20261116-191210-770-LE2-lane-c4-holsters` for `-Grip` and
`20261116-204916-179-LE2-lane-c4-twohanded` for the two-handed ways, each restored with `verification: 0 differences`). Both ways replay the same double A; `-SupportRelease` (`make-vault-recording.py --support-release`)
lets the left grip go as the second press goes down and squeezes it again 1.8 s later.

| | the right grip alone (`-Grip`) | both hands kept on the gun (`-Grip -Support`) | the support grip let go for the mantle (`-Grip -Support -SupportRelease`) |
|---|---|---|---|
| the game's admission | admitted | refused before; admitted now (the zoom cleared) | admitted |
| the camera mode at the start | Combat | TightAim (the zoom) | TightAim |
| right wrist within 0.15 m of its grip | 137/137, 135/135, 134/134 (max 0.08) | 134/134 three times (max 0.10) | 94/145 to 95/145 (mean 0.28, max 0.80) |
| left wrist | its controller: max 0.11 | on the gun (0.14 from its controller at rest): 117-119 of 134 within 0.15, max 0.17-0.18 | 82-83 of 145 (mean 0.30, max 0.80) |
| arm IK's anchor | first person throughout | first person throughout | **third person for 0.7 s each vault** (`view 75-76 units from Shepard's animated head bone`) |
| the floor lock's hold | 163 units below the camera, released 0.59-0.61 s after | 148 below (TightAim's lower camera), released 0.61 s after, the eye's largest step 6.7 units a frame in the settle | the same 148, released 0.91 s after |

Letting go mid-move ends LT, the game leaves TightAim for its ordinary camera mode while the custom action plays, the view
jumps 0.75 m from Shepard's animated head and arm IK takes its third-person frame: both hands leave their controllers by up to
0.8 m. That is the sloppiness the owner expected. **Recommendation:** keep the hold through the mantle (what ships); letting go
needs the camera mode held through the move before it can be offered. Snapshots of both are in the stretch (`vault-205540`,
`vault-205606`: before the double tap and at the move's end). The plain vault after `holsters.ps1` was refused because that script
ends with the pistol holstered (ME2 needs the combat action state: "With the weapon away the game refuses" above).

## What is left

- **ME2's observer has its own file since 2026-11-16** (lane GF2, docs/trilogy-parity-ledger.md Forks 4 step 1):
  `game/Le2Traversal.cpp`, moved unchanged from `game/Traversal.cpp`, which keeps `[Traversal]`'s settings, the console's
  `mevr traversal` surface and the dispatch to `le2::` and `le3::`. Fixture after the move: `vault.ps1 -Runs 3 -Mode tap`
  vaulted 3 of 3 (session `20261116-173826-670-LE2-gf2-run4`: each `vault over: the custom action 0.90-0.91 s (stance
  FromCombat)`), as before it (`20261116-153741-665-LE2-gf2-run2`, 3 of 3). A run under heavy machine load in between
  (`...-gf2-run3`) vaulted 1 of 3: the second A of two double taps never reached the mod (the null headset's controllers
  dropped out), not a refusal.

- ~~Arm IK across root-motion steps~~ **done** (lane C2, 2026-11-15; research/le2-vrik.md "Arm IK across root-motion steps"): the
  writer follows the camera's bone; through three vaults both wrists stay within 0.08 m of the controllers (0.07 standing).
- ~~The floor lock through the vault~~ **done** (lanes C3 and C4, 2026-11-16; "The floor lock through the vault" above): the eye
  rides the game's camera through the move and joins the floor-locked height once the camera has settled.
- ~~**The two-handed hold**~~ **done** (lane C4, 2026-11-16; "The two-handed hold" above). The owner's pick between the hold kept
  through the mantle (today) and letting go for it is open.
- **The grips on ME2:** a held right grip is the pad's RB on ME2 today (the power wheel after 0.3 s; the LE2 host has no `gripHolds`
  yet), and the left grip LB (the weapon wheel). A vault with the gun held by a grip waits for the holsters' and the support grip's
  host services on LE2; nothing in the vault needs a free hand (it is A and where the player stands).
- **The owner's choices:** ~~the pawn's facing vs the head's; the holstered vault~~ taken 2026-11-18 for Freedom's Progress's barrier (the head's facing, the holstered vault: "The vault Freedom's Progress requires" below; each a key); the cue's and the comfort mask's defaults.
- **The game's take-cover prompt** (UpdateCover's `ShowCoverWidget`): lane F hides it while the block applies. Checked at the
  fixture (stretch `*-lane-e-hud`, Shepard 0.60 m in front of `CoverLink_10` slot 0 facing it, standing hands replayed): the HUD
  panel (`-hud.png`) and both eyes show no take-cover prompt with the block on (`2026-11-15_00-01-40-971`) **and none with `mevr
  cover snap on` either** (`00-01-52-028`): the weapon panel (12 / 60), the squad's portraits, the power bar and the crosshair only.
  So this pair doesn't prove the hide: the prompt's own condition isn't met standing there (FindPlayerCover's distance or the
  camera's angle), or it isn't drawn into the panel. Lane F's to settle (a pawn walking toward the cover, `mevr cover acquire`).
- **Climb up** (`ClimbCover`, custom action 9): no slot climbs in this level; the code path is the same (`CanPerformClimb`), unseen.
- **ME3:** ladders (`TryUseLadder`) and its vaults wait for Halcyon's ME3 renderer in our host; `core/vr/Traversal` is game-independent.

## The stick's direction: vault or roll (design for the owner, 2026-11-16, lane GR; nothing built)

The owner (2026-11-16, open question 7 of docs/trilogy-plan.md): "I'm curious if the analog direction will allow vault and roll."
Asked: can a double A (or the game's own evade press) with the left stick's direction choose between a vault (toward an obstacle
the game can mantle) and a roll or evade (any other direction), on ME3 and on ME2? **Yes on ME3, and it is the game's own idea:
ME3's A already reads the stick to choose between a mantle, a climb and four rolls. ME2 has no roll at all**, so there the
stick can only choose between a vault and nothing. Read from each game's script with the scanners named in this file's header
(ME3: `local-tools\le3-traversal-survey\scanner`, whose `--game LE2` selects LE3 on ME3's `CookedPCConsole`) and ME3's coalesced
`BioInput` (dumped with LegendaryExplorerCore's `CoalescedConverter.DecompileGame3ToAssets` into a scratch folder); the outputs
are game code and stay out of git.

### What each game does with A and the stick

**ME3.** A is `Console_Action`: `Exclusive TryStandingJump | Exclusive Used | OnRelease StormOff | OnHold 0.2 StormOn |
Exclusive PressAction | OnTap 0.3 TapAction | OnHold 0.3 HoldAction` (the same as `PC_Action`).
- On the press, `SFXGameModeDefault.PressAction`: `TryAcquireCover(FALSE, TRUE)` (stop if it took cover), then
  `TryCoverAction` (in cover only: the stick's `RawJoyUp > 0.8` with `CanPerformClimb` climbs, with `CanPerformMantle` and
  `BioPawn.CanPerformMantleSlow` mantles; a peek with the stick sideways past 0.88 swat-turns or rolls), else `TryRoll`.
- `TryRoll` (script, an exec, returns bool, no parameters): refused while move input is ignored, without a weapon, zoomed or
  falling, or unless the pawn is `bCombatPawn`; it **cancels the reload before its last checks**; with the weapon not equipping
  or putting down: `BioPlayerInput.RawJoyRight > 0.707` starts custom action 55 (roll right), `< -0.707` 54 (left), `RawJoyUp <
  -0.707` 57 (back; not in an aim-back). Sideways wins over back. Forward does nothing here.
- On a tap (released within 0.3 s), `TapAction`: `TryAcquireCover`, `TryCoverSlip`, `TryForwardRoll`, `TryExitCover`.
  `TryForwardRoll` has `TryRoll`'s guards and its reload cancel; while storming with `RawJoyUp > 0.707` it tries cover instead;
  otherwise `RawJoyUp > 0.707`, out of a high or mid cover, starts 56 (forward roll).
- Out of cover the mantle has its own route: `CanPerformMantleSlow` finds an anchor through `FindCover(wide)` when the pawn
  isn't in cover, and `BioPawn.TryEarlyMantle` (an event, action 32) is the sprint's vault, started by native code [verify
  where].
- So ME3's own rule is already "A, and the stick says which": forward at cover mantles or climbs, sideways or back rolls on the
  press, forward rolls on the tap. **In VR today a single A with the stick pushed sideways or back rolls** (the pad's A reaches
  the game and `PressAction` runs: away from cover, or with the opt-in no-snap, `TryAcquireCover` takes nothing and `TryRoll`
  follows), and a forward tap rolls forward [verify in the game].
- Our ME3 adapter (`game/Le3Traversal`, research/le3-traversal-adapter.md) calls `TryRoll` or `TryForwardRoll` itself on
  `mevr traversal evade`, choosing by the same 0.707 thresholds from the native stick; its `Link.evadeDown` has no binding.
  The double A requests a ladder or a vault only with `le3DoubleTap`, off.

**ME2.** No player roll or evade exists: no `TryRoll`, no roll custom action, no `Dodge`; the only evade is the AI's Kismet
action `SFXSeqAct_CoverUserEvade`. ME2's A ("What ME2 does" above) is use, climb or mantle in cover, cover, sprint on hold, exit
on tap. Its `CanPerformMantle` itself requires the stick forward (`RemappedJoyUp >= DeadZoneThreshold`, `|RemappedJoyRight| <=
RemappedJoyUp`); our vault sets the stick forward for that one call, so today the stick's real direction plays no part.

**The stick the game sees.** In VR the left stick reaches the game as the pad's (Touch as pad 0), turned by the host for the
head's or the hand's yaw (`walk = head | hand`) before the game reads it, so `RawJoyRight`/`RawJoyUp` are the push in the game's
camera frame: "forward" is where the player walks, the frame the game also uses for its rolls (relative to the pawn, which faces
the camera in combat). [verify] that the turn happens before `RawJoy*` (step 3 below).

### The rule proposed

**One traversal press, the double A (the owner's convention); the left stick chooses what it does.** At the second press of a
double A (`core/vr/Traversal` `PressTracker`, `doubleTapS` 0.45 s):
1. A selected usable takes the press, as today (`Exclusive Used` comes first in both games' bindings).
2. Read the stick as the game will see it (`RawJoyRight`, `RawJoyUp`). Below 0.35 of a full push it is **centred**; from the
   game's own 0.707 on an axis it is **pushed**; in between it counts as centred (no accidental roll).
3. **Centred:** vault (or climb) if the game admits one here, exactly today's ME2 rule (within 0.39 m, the pawn facing within 45
   degrees, the game's own `CanPerformMantle`); else nothing.
4. **Pushed toward an admitted obstacle** (the push's world direction, the stick turned by the camera's yaw, within 45 degrees of
   the slot's direction: the game's own facing window, `m_fClimbMantleFaceAngleThreshold` 0.7): vault or climb. If the game
   refuses it (its facing, its landing), **nothing**: never a roll into the obstacle.
5. **Pushed anywhere else:** ME3 rolls that way with the game's own calls and precedence (`TryRoll` for right, left and back,
   sideways first; `TryForwardRoll` for forward), the adapter's existing `evade` route. ME2: nothing.
6. **A single A never rolls** in VR (ME3): `TryRoll` and `TryForwardRoll` are script calls from `PressAction`, `TryCoverAction`
   and `TapAction`, without parameters and returning a bool, so `engine/call_function` answer handlers can answer them `FALSE`
   for the player's mode when the caller is one of those three and the press isn't ours, VR on foot, out of the game's cover. The
   reload is then not cancelled either (the cancel is inside the answered call). Our double press then calls the same functions.

**The alternative the owner may prefer:** keep ME3's single-A roll as the game has it (step 6 left out). It is faster (no
second press) and the game's own feel, but in VR every A pressed while strafing with nothing selected rolls, and the first press
of a double A with the stick pushed already rolls, so a pushed double A could no longer vault: the stick would then only choose
on a centred double A. That is why the proposal withholds the single-A roll.

### What the player feels

- Standing at a crate, gun in hand: double-tap A and Shepard vaults it (today's ME2). Push the stick toward the crate and
  double-tap: the same vault. Push left and double-tap: a roll to the left; back: a roll back; forward with no obstacle ahead:
  a forward roll (ME3). A single A uses what is selected and otherwise does nothing of ours.
- ME2: the same vault in every case where it vaults today; a pushed double tap away from an obstacle does nothing.
- Holding A is the sprint (`OnHold 0.2 StormOn` in both sequels), so a double tap must be two taps; today's measured double
  taps are 0.23-0.26 s apart.
- Holstered: neither (ME3's rolls need a weapon and `bCombatPawn`; ME2's mantle needs the combat stance); the owner's earlier
  question about a holstered vault stays open.
- A roll moves the body about 3 m in under a second. The comfort mask (`[Traversal] mask = vignette | blink`) and the level view
  (`view = level`, measured but not applied on ME3) apply as for the vault.

### Conflicts and limits

- **Use:** a selected object always wins (step 1).
- **Sprint:** a press held past 0.2 s is the sprint; while it runs (A held) the double tap can't happen.
- **Reload:** the game's rolls cancel the reload before their own last checks; the adapter's admission (reload, zoom, storm,
  cover, custom action, ignored input, physics) runs first, as `mevr traversal evade` does today.
- **Cover:** in the game's own cover (ME3 with snapping allowed) nothing of ours runs: `PressAction`'s own cover rules stay.
- **Whose facing:** the game's vault admission checks the pawn's facing; step 4 checks the stick's direction. With the stick
  toward the crate but the pawn turned away (combat faces the camera), the game refuses and nothing happens (step 4): open
  question 3 of the plan (turn the pawn to the obstacle at the commit, or not) decides whether that case vaults.
- **Seated play:** the same rule; the stick and A don't depend on standing.
- **First person through a roll:** ME3 has its own camera modes for it (`SFXCameraMode_Roll`, `SFXCameraMode_ForwardRoll`);
  whether Halcyon's ME3 first person keeps them first person is unknown [verify].

### What to verify before building

1. `mevr calltrace 5 roll` on ME3 while pressing A with the stick pushed: `TryRoll` and `TryForwardRoll` arrive through
   CallFunction with `PressAction` / `TapAction` / `TryCoverAction` as the caller (else step 6 needs another seam).
2. With step 6's answer on, a single A pushed sideways starts no custom action 54-57 and doesn't cancel a reload; the cover
   and use parts of `PressAction` and `TapAction` still run.
3. The stick the game reads against the controller's: push each way with `walk = head` and `walk = hand` and read
   `BioPlayerInput.RawJoyRight`/`RawJoyUp` (`mevr props`).
4. At an ME3 vault fixture (none known yet: T05's) and ME2's Save 4 crate (`vault.ps1`): a double A centred, toward, left,
   right, back and forward with no obstacle; each must start exactly one action (ME3 31/32 or 33, 55, 54, 57, 56; ME2 10, or
   nothing), and a single A none.
5. In the headset: whether a roll is comfortable in first person, with and without the mask.

### Where it would live

A pure chooser in `core/vr/Traversal` (stick, the nearest admitted slot's direction and the thresholds in; centred, toward a
slot, or a roll direction out; unit-tested), called by the per-game adapters at the commit: `game/Le2Traversal` (vault or
nothing) and `game/Le3Traversal` (vault, climb, the rolls, and step 6's answer handlers). Those files belong to lanes GF2 and C4
today; the owner decides first.

## The vault Freedom's Progress requires (lane CV2, 2026-11-18)

The owner, from the headset (2026-11-18, ME2 at Freedom's Progress): "There's what seems to be a game-stopping bug where when
we're at the very beginning and I need to move Shepard into position, I'm never able to get into position."

**Where.** The run's last three minutes (20:48:34 to 20:51:33, then the owner quit) were spent at one barrier: Shepard at
(-10372, 4971, -13609), next to `BioD_ProFre_400Turrets`' only cover link, `CoverLink_5` at (-10322, 5070, -13642), whose three
slots all mantle over to `BioD_ProFre_300Drones`' `CoverLink_3` at (-10180, 4673). The squad stood beyond it at y 4400-4700 (the
owner had placed them there: `squad place: move for Jacob ... at (-10987, 4666, -13699)`), on the warehouse's side
(`BioD_ProFre_500Warhouse`'s links at y 3585-3820). No door or usable object was in sight in those minutes (`interact:` named only
a combat target). The barrier is the way on, and ME2's way over it is the cover mantle (read from the levels with `conversation-scan
<cooked> <out> --game LE2 --class-dump CoverLink --only <level>.pcc`; the dumps are game data, kept in the lane's scratch folder).
"The very beginning" is read as the game's first mission: the owner's ME2 career (`VR_31`) begins at Freedom's Progress (Save 4,
its spaceport). **The owner is asked to confirm the spot** (the barrier the squad went over, before the warehouse).

**Why the vault never came** (`traversal: next to CoverLink_5 slot N ...`, every approach in those minutes):
- admitted twice (20:48:37 at 0.46 m and 20:48:40 at 0.33 m, the pawn facing 17 degrees off, the pistol out), but no double A
  then: the A presses came 20 s later and single (`traversal: A pressed (#24): first tap: nothing to vault or climb from here`, #25
  2.9 s after it, #26 and #27 1.03 s apart);
- refused at 0.32-0.37 m for the pawn's facing (55 degrees off: the game reads `Actor.Rotation`, the stick's turn, never the head);
- refused from 20:49:54 on whatever the distance or facing: the rifle had been let go and holstered, and `CanPerformMantle` needs
  the combat action state (`BioPawn` +0xA9D == 1, in its pawn test `0xABD700`). Natively A into cover drew the weapon first; with
  no snap and only holsters drawing, nothing does.
So with the weapon away the barrier couldn't be crossed at all, and with it out the player had to turn the pawn with the stick
(invisible in first person) and double-tap inside a 0.39 m window, with no cue. Question 3 (docs/trilogy-plan.md) had listed both
as the owner's choices.

**The fix** (question 3's first two choices taken so the mission can be finished; each a key, the old rule one setting away):
- `[Traversal] holstered = 1` (default): the game is asked with the combat action state written for its one call (journalled, put
  back in the same `__finally` as the other one-call writes), so a holstered player vaults; the move itself plays from exploring
  (`BioCustomAction_CoverMantle`'s `FromExplore` body stance) and nothing is drawn (only holsters draw). 0: refused as before.
- `[Traversal] facing = head` (default): the game is asked with the head's facing (the pawn's `Rotation.Yaw` written to the head's
  for the call, the game's own 45-degree window), and at the commit the pawn is turned to the slot's direction (`Actor.SetRotation`)
  before `MantleCover`, so the move goes over the barrier and not along the stick's last heading. `facing = pawn`: as before.
- The double A, the 0.39 m window and the cue stay as they were (rule 8; the cue's default is still the owner's choice: off).
Logs: `traversal: next to ... the game admits it (...); asked with the head's facing, N deg off; the weapon away: asked as drawn`,
`traversal: double tap: the pawn turned +N deg to CoverLink_X's direction before the move (admitted with the head's facing)`; the
settings line `traversal: [Traversal] ... facing head, holstered 1`.

**Test without the headset:** `tools\le2-halcyon-run\vault.ps1 -Holster` (the weapon put away before each run by `mevr weapon away`,
the holsters' own let-go, now on ME2 too) and `-YawDeg 60` (the pawn turned off the slot by `mevr traversal place`'s fifth word),
Save 4's `CoverLink_10`. Not run: the owner was in the headset (`owner-testing.flag`). **[verify]** that the mantle plays from
exploring (its `FromExplore` stance), the head-facing admission with the pawn turned, and the owner's barrier.
