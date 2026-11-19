<!-- SPDX-License-Identifier: MIT -->
# One sprint for the trilogy (2026-11-16, lane GR)

The owner, 2026-11-16 (docs/trilogy-plan.md open question 6): "Unified sprint." Before it: ME1 and ME3 ran `game/Sprint` (ME1's
faster run, research/71 section 3.1, `core/vr/Sprint`), ME2 kept the game's own Storm "by design" (the continuation plan's M6
table). This note is what the three games do, why no ME2 fact forces a difference, the one implementation, and what was measured.
The scripts were read with the scanners named in research/23 section 7 and research/le3-traversal-adapter.md; their output is game
code and stays in a scratch folder.

## What the three games do

All three bind the sprint the same way: A held. ME1 `Console_Action: ... OnRelease StormOff | OnHold 0.3 StormOn | OnTap 0.3
TapAction`; ME2 `... | OnRelease StormOff | OnHold 0.2 StormOn | OnTap 0.3 TryExitCover 1`; ME3 `... | OnRelease StormOff | OnHold 0.2
StormOn | Exclusive PressAction | OnTap 0.3 TapAction | OnHold 0.3 HoldAction` (research/le2-traversal.md for ME2 and ME3).
`SFXGameModeDefault.StormOn` (a script exec function without parameters in all three) does each game's checks and then sets
**`Outer.bWantsToStorm = 1`** (`BioPlayerController.bWantsToStorm`, a byte, `CPF_Input`); `StormOff` schedules `TurnStormOff` after
`InputDelayStormExit`, which sets it back to 0. The native movement turns that intent into the storm (`bStorming`: on the
controller in ME1 and ME2, on the pawn in ME3), its posture, camera mode and lowered gun.
- ME1's StormOn: only with `RemappedJoyUp > 0`, not in cover; clears `bToggledWalk`.
- ME2's: only with `RemappedJoyUp > 0`; out of a lean or peek it breaks the game's cover first; a tutorial event.
- ME3's: refused when `IsStormDisabled()`; sets `bNoCoverFromStorm`, a 0.25 s `CanEnterCover` timer; needs
  `MoveStickMag > 0.8` and `RawJoyUp > 0.707`; cover slips from a peek at an edge (custom actions 36-39).

**The binding's hold is a timer** (ME1, read in its native input: the `ONHOLD` branch of the alias parser parses the time and calls
`SetTimer(time, false, 'StormOn', <the game mode>)`; `UObject`'s timers fire through ProcessEvent). So StormOn arrives from native
code through **ProcessEvent**, never as a script call. The module's old route, a skip handler in the CallFunction hook (script calls
only), never saw it: on ME1 from 2026-11-06 to 2026-11-16 the game's own sprint ran whenever A was held, and the faster run never
started (the measurement below). ME2 and ME3 bind it the same way.

**No ME2 fact forces the native Storm.** ME2 has the same functions, the same intent field, `StormSpeed`, `GroundSpeed` and
`CombatGroundSpeed` (its `PlayerMoveCombatOrExplore` reads `GetMaxSpeed()`), and Halcyon's ME2 storm latch matters only with head aim,
which ME1's baseline turns off on ME2. ME2's only sprint-specific coupling is `CheckStormToCover` (the sprint into cover), which the
VR cover block refuses anyway (research/23 section 7.1). So ME2 takes ME1's rule.

## The one implementation (`game/Sprint`)

- StormOn and StormOff get **post-handlers on both hooks** (ProcessEvent and CallFunction): StormOn runs as the game wrote it
  (its own checks, cover breaks, timers), then, while the faster run applies, its intent is withdrawn (`bWantsToStorm = 0`,
  journalled `Sprint.wantsToStorm`) before the movement reads it; the press is noted (`gHeld`). StormOff's post ends the press.
- While the button is held and Shepard runs (over 200 u/s), `core/vr/Sprint`'s stamina allowing (6 s, refilled in 8 s), the run
  speeds the game reads are raised by one factor: the game's sprint speed (`BioPawn.StormSpeed`; ME1 with a weapon drawn its own
  `CombatStormSpeed`) over the run in effect (`Pawn.GetMaxSpeed`, read while nothing of ours is in the speeds), clamped to 1..2;
  both `Pawn.GroundSpeed` and `BioPawn.CombatGroundSpeed` are scaled (explore and combat), each put back if still ours.
- Per-game adapters: ME3's native admission and fresh-owner check (game/Player's movement admission, `IsStormDisabled`; unchanged);
  ME2: not while the pawn is in the game's own cover (its StormOn's cover break stays the game's); ME1: its drawn sprint speed.
- `[Hands] sprint = speed` (default) / `game`, `sprintStaminaS`, `sprintRefillS` as before. Logs `sprint:` (the routes at the
  start, the first withdrawal, `running fast: x<f> (the run in effect .., the game's sprint ..; GroundSpeed .. -> ..)`, `back to the
  run after .. s: the pawn's peak .. u/s`); `mevr sprint` (the status JSON, also the snapshot's `"mod"."sprint"`); ReflectCheck rows
  `bWantsToStorm`, `CombatGroundSpeed`, `Pawn.GetMaxSpeed` (and ME2's StormOn, StormOff, StormSpeed, GroundSpeed).

## Measured (fake headset, 2026-11-16 19:40-20:08, lane GR)

Before = main `4e7fe15f` built apart (`build-before2`), after = the same plus this change (`f141b918`); each LE1 session
`tools/fake-headset-session.ps1` with one run script (20 s in the world, a standing replay with the hands in view, `pause`, two
snapshots; then the sprint replay with `-Inputs`: `make-walk-recording.py --plan stand:3,sprint:2.5,stand:2,back:3.5,stand:2,
forward:2.5,stand:3 --hands view --stick 1.0`, `sprint` being A held with the stick forward). ME2: Save 4 (Freedom's Progress),
ME3: the renderer preview's Save 31 (the Normandy), `tools/le2-halcyon-run`, restores `verification: 0 differences`.

| | LE1 before | LE1 after | ME2 after | ME3 after |
|---|---|---|---|---|
| StormOn seen | never (`0 presses taken`) | `the sprint button (the game's binding, through ProcessEvent)` | the same | the same |
| the game's sprint | ran: `walking mode ExploreStorming (holstered), speed 450 u/s`, camera `SFXCameraMode_ExploreStorm`, `the game sprints by a route other than StormOn` | withdrawn; no storm camera mode or warning | withdrawn; none | withdrawn; none |
| the faster run | none | `running fast: x1.16 (the run in effect 400 u/s, the game's sprint 462 u/s; GroundSpeed 400 -> 462, CombatGroundSpeed 400 -> 462)` | `x1.31 (400 -> 525, CombatGroundSpeed 400 -> 525)` | `x1.12 (400 -> 450, CombatGroundSpeed 350 -> 394)` |
| speed after the push (0, 0.1, 0.2, 0.4 s) | 0, 355, 400, 535 (the storm) | 0, 347, 400, 462 | peak 525 u/s in 0.7 s | peak 450 u/s in 1.5 s |
| the run (stick alone, back and forward) | 400 u/s | 400 u/s | 400 u/s | 400 u/s |

Each sprint ended against the level's geometry (Shepard's start faces a wall 2-3 m ahead on LE1's save; ME2's against the crate
of research/23 section 7.4, after a first attempt where the save's selected object took A: `traversal: A pressed: A uses the selected
object first`, so StormOn never ran). Notes: on ME2, with the head turned 90 degrees in the room the binding still called StormOn
but its own `RemappedJoyUp > 0` check refused the intent (nothing to withdraw), and the faster run went on (the press is ours either
way). On ME3 the faster run began 4.3 s after the press (the native admission or a walk against the Normandy's corridor wall:
not separated) [verify]. The LE1 same-pose pairs (first pass: snapshots `19-42-40-167`/`19-42-43-179` before,
`19-45-59-680`/`19-46-02-685` after; second pass `19-50-24-232`/`19-50-27-232`, `19-53-56-402`/`19-53-59-405`): `hostView` identical
(-43.3 / -4.1 / -18.1 uu, 100 uu/m), the layers at the same places (the omni-tool target 0.2 and 0.7 mm apart), the HUD panel and both
world layers identical pixel for pixel, the composited eyes 0.03-0.12% of pixels apart by more than 32/255 (the omni-tool target's
ring). **What differs on LE1 is the sprint, on purpose: the faster run now runs and the game's storm doesn't; nothing else.**
Not measured: the sprint's feel and heading in a headset on any game; ME3 away from the Normandy; a sprint with a weapon drawn
(LE1's `CombatStormSpeed`, the sequels' `CombatGroundSpeed` scaled).
