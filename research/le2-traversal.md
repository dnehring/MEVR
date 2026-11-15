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

## What is left

- **Arm IK across root-motion steps** (above): the goals should stay in the world when the mesh jumps; lane C's writer.
- **The grips on ME2:** a held right grip is the pad's RB on ME2 today (the power wheel after 0.3 s; the LE2 host has no `gripHolds`
  yet), and the left grip LB (the weapon wheel). A vault with the gun held by a grip waits for the holsters' and the support grip's
  host services on LE2; nothing in the vault needs a free hand (it is A and where the player stands).
- **The owner's choices:** the pawn's facing vs the head's; the holstered vault; the cue's and the comfort mask's defaults.
- **The game's take-cover prompt** (UpdateCover's `ShowCoverWidget`): lane F hides it while the block applies. Checked at the
  fixture (stretch `*-lane-e-hud`, Shepard 0.60 m in front of `CoverLink_10` slot 0 facing it, standing hands replayed): the HUD
  panel (`-hud.png`) and both eyes show no take-cover prompt with the block on (`2026-11-15_00-01-40-971`) **and none with `mevr
  cover snap on` either** (`00-01-52-028`): the weapon panel (12 / 60), the squad's portraits, the power bar and the crosshair only.
  So this pair doesn't prove the hide: the prompt's own condition isn't met standing there (FindPlayerCover's distance or the
  camera's angle), or it isn't drawn into the panel. Lane F's to settle (a pawn walking toward the cover, `mevr cover acquire`).
- **Climb up** (`ClimbCover`, custom action 9): no slot climbs in this level; the code path is the same (`CanPerformClimb`), unseen.
- **ME3:** ladders (`TryUseLadder`) and its vaults wait for Halcyon's ME3 renderer in our host; `core/vr/Traversal` is game-independent.
