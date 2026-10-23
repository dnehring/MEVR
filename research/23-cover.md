# 23. Cover and crouch in VR (LE1)

2026-10-22. Why Shepard snapped into cover against walls in VR first person (and the view dropped to
third person), how ME1 enters cover and crouches, how we turn the snapping off, and the physical
crouch that replaces it.

Sources, all read-only and local: `SFXGame.pcc` and `Engine.pcc` decompressed with the game's Oodle
DLL and read with the scratch UE3 bytecode disassembler ([20](20-melee-and-grenades.md) section 2);
`BIOGame.ini` and `BIOInput.ini` in `CookedPCConsole\Coalesced_INT.bin` ([19](19-body-holsters.md)
section 6); GhidraMCP on the LE1 project; the LExSDKv2 headers. RVAs for 2.0.0.48602, names recorded
in `tools/ghidra/symbols-le1.csv` (`_guess` = role inferred). Nothing extracted is kept or committed.

## 1. How the player enters cover

**There's no cover button.** A is `Console_Action` = `OnRelease StormOff | OnHold 0.3 StormOn | OnTap
0.3 TapAction`. `SFXGameModeDefault.TapAction` (bytecode) tries, in order: `TrySkipElevator`, `Used`
(use, talk, open, pick up), `TryFallOrClimb`, `TryEnterVehicle`, and last `TryExitCover`. So A only
*leaves* cover. Holding it is `StormOn` (sprint): only with the move stick pushed forward
(`RemappedJoyUp > 0`), and never in cover.

**Cover is taken by the pawn's native tick** (GhidraMCP):

- `ABioPawn::Tick_guess` (`0xAA0C20`) calls `TickCover_guess` (`0xAB0FC0`) first. For the player (and
  NPCs with `bTestNPCCover`) it counts `CoverTimer` (`+0x984`) up to `CoverPlayerInterval` (`+0x97C`,
  0.1 s in `BIOGame.ini`; NPCs `CoverNPCInterval`, 0.2 s), then:
  - out of cover: `TryAcquireCover_guess` (`0xAB1430`);
  - in cover: the trace again with the kept normal; `CoverType` updated, or, if it's gone, the script
    event `LeaveCover` (`0xAA48E0`).
- `TryAcquireCover_guess (this)`:
  1. `CoverTrace_guess` (`0xAA92F0`; this, `FVector* outNormal`, `BYTE* outType`, `BOOL keepNormal`): a
     pure query. Without move input (`Acceleration` about zero) there's no cover. Otherwise three line
     checks `CoverTestDistance` (80 units) ahead along the move, left and right of it (the cylinder's
     radius), each hitting a surface that faces back within the cover module's angle; then line checks
     stepping up the wall. If they reach the top still hitting: type 1 (standing, high cover); if one
     misses on the way up: type 2 (mid-level, low cover).
  2. The move's angle off the surface (`acos(dir2D · -normal)`) must be within `CoverTestAngle` (20
     degrees: "push the stick within this angle into cover to enter", says the ini).
  3. The camera's direction (a script event on the controller) must be within `CoverTestCameraAngle`
     (60 degrees) of it.
  4. Then: `CoverType` (`+0xA55`) = the type, `bInCover` (`+0xA40` bit `0x20`), the cover state
     (`0xAB2340`), **`ProcessEvent(PlayerAcquiredCover)`** with no parameters, and a tutorial/metrics
     notification `EnterCover`.
- `SFXPawn_Player.PlayerAcquiredCover` (bytecode): the default cover action and lean bias,
  `SFXModule_Cover.PlayerEnteredCover`, a 0.5 s `NotifyFinishedCoverAlign` timer,
  `ShrinkCollisionForCover`, the squad's `onPlayerEnterCover`, `SetAnimatedTransitionPending`, and
  `BioPlayerController.OnEnteredCover` → `GotoState('PlayerInCover')`. Its `BeginState` crouches the
  pawn in low cover (`CoverType == 2` → `ShouldCrouch(true)`).
- The camera: `SFXCameraSetup.GetCameraMode` picks `GetCoverCameraMode` while `IsInCover`; the cover
  modes hook the `CoverSlot` actor ([18](18-camera-and-hands.md)), and Halcyon's `FpStateForClass`
  (`head_aim.cpp`) leaves them third person. **That's the reported bug:** walking into a wall in VR
  pushes the stick into it, and the camera faces it, so the game snaps Shepard into cover and the view
  leaves first person.
- `IsInCover` (`0xA9C8E0`): for the player `CoverType != 0 && CoverAction != 13` (`CA_Aimback`); for NPCs
  `bInCover`.
- Scripts and Kismet: `BioSeqAct_UseCover`, `BioAI_*`, `BioBaseSquad` cover functions drive AI pawns
  only. Nothing else puts the player in cover.

**Leaving cover** (bytecode):
- `BioPlayerController.PlayerInCover.PlayerMove`: the stick pulled back for `CoverBreakTimeThreshold`
  (0.08 s) → `PushOffCover` (if the camera faces the slot) and `LeaveCover`; and **whenever the pawn is
  no longer `IsInCover`, `GotoState('PlayerWalking')`**, so the controller follows the pawn.
- `SFXGameModeDefault.TryExitCover` (A's tap): returns during an animated transition or with the
  scope up; else `PushOffCover` (`PlayerPushOffCover`: a short push away from the wall along
  `CoverNormal`) then `LeaveCover`.
- `BioPawn.LeaveCover` (event): if `bInCover`, `CoverType = 0`, `SetCoverAction(0)`, lean bias 0,
  `PlayerLeftCover`, `ClearCoverState`, and unless the controller is in `PlayerInAimBack`,
  `RestoreCollisionFromCover` and `ShouldCrouch(false)`.

## 2. Turning the snapping off

| Way | What it does | Verdict |
|-----|--------------|---------|
| Refuse `PlayerAcquiredCover` (a ProcessEvent pre-handler) | The native half (`CoverType`, `bInCover`, the cover state) is already written when the event fires: the pawn would be half in cover. And the host's hook only has post-handlers (a host change). | no |
| A game setting | None: `BIOGame.ini`, `BIOInput.ini` and the profile have no cover toggle. | none |
| Write `CoverTestCameraAngle` (or `CoverTestAngle`, `CoverTestDistance`, `CoverPlayerInterval`) on the player's pawn | Works without a hook, but writes config state on each new pawn and must be put back. | the fallback we didn't need |
| **Refuse the native acquisition test for the player's pawn** (a pre-hook on `0xAB1430`) | The game never enters cover and nothing is written; NPCs and the in-cover tick are untouched; A and sprint unchanged. | **chosen** |

The hook (`engine/cover_acquire`, mod side, hot-reloadable like `engine/bone_pose`) skips the test only
for the pawn `game/posture` names: the player's, in VR on foot, with `[Hands] noCover = 1`. To say when
an entry was refused, it runs the game's own trace (`0xAA92F0`, a pure query, called the same way the
test calls it) and the move-angle check; the camera check is left out. Logged once per change:
`posture: refused a cover entry: low (crouching) cover ahead, the move N degrees off its normal`.

**A cover Shepard is already in** (taken before the setting applied, or with the hook missing) is left
the A button's way: `SFXGameModeDefault.TryExitCover` through ProcessEvent, asked every 0.25 s while it
refuses (an animated transition, the scope); after 2 s `BioPawn.LeaveCover`, the game's own cover-lost
path (the controller then leaves `PlayerInCover` by itself). Nothing traps the player: the stick's pull
back and A still work as before.

## 3. How the player crouches

- **L3** is `Console_Crouch` = `SFXGameModeDefault.ToggleCrouch` (a toggle; keyboard: Left Ctrl). It
  returns while sprinting (`bStorming`) and in the lean cover actions (3, 4); else
  `Pawn.ShouldCrouch(!bIsCrouched)`.
- `BioPawn.ShouldCrouch(bool)`: `Pawn.ShouldCrouch` sets `bWantsToCrouch` (Pawn `+0x63C` bit `0x8`);
  crouching also resets the mount attempt and `m_oBehavior.SoftResetMovementAndAnimationState()`.
  The engine then crouches on its next move (`APawn::Crouch_guess`, `0x4A1BF0`, sets `bIsCrouched`,
  bit `0x10`; both virtual) and stands only where there's room (`UnCrouch_guess`, `0x4AEEE0`), with the
  `StartCrouch` / `EndCrouch` events lowering the eye height and the mesh.
- Other callers: `BioSeqAct_Crouch` (Kismet), low cover (above), `LeaveCover` and `OnExitedAimback`
  (stand), and **`PlayerWalking.PlayerMoveExplore`: with the weapon away, any move input stands Shepard
  up** (ME1 has no crouch-walking out of combat); `PlayerMoveCombat` allows it.
- Sprint keeps `bStormingWasCrouched`; ToggleCrouch refuses while sprinting.
- The camera: `SFXCameraSetup.GetCombatCamera` picks `CombatCrouchCam` / `CombatCrouchTightAim` while
  `bIsCrouched`, whose `HookOffset` is 45 units lower than the standing modes' (90 → 45,
  [18](18-camera-and-hands.md)). Exploration keeps `ExploreCam` (the `Camera` bone moves with the
  animation). Halcyon writes only the modes' `Offset`, so **in combat the game's crouch lowers the view
  about 45 units on top of Halcyon's lean** **[verify]**: the mod logs the view's height above Shepard
  0.75 s after each physical crouch or stand (`posture: ... the view is N units above Shepard's origin
  (was M: ±D)`).
- Crouching makes Shepard a smaller target and quieter (`m_fCrouchNoiseModifier` 0.5).

## 4. Physical crouch

The user's report: crouching for real lowers the view (Halcyon's lean: `[HEADCTRL] viewUU` up to −15
units at 50 units/m), but the game's crouch, the smaller target behind low cover, never engages.

**Measure** (`core/ik/Posture`, `game/posture`): the eyes' height as a fraction of the standing eye
height.
- With a floor (host v10, `core/xr/Floor`: STAGE or LOCAL_FLOOR): the head's height above it over the
  standing eye height learned the way the holsters learn it (`ik::body_slots::StandingHeight`: taken at
  a recenter, then a slow-rising maximum; reset for a new session).
- Without one: how far the head dropped below where it stood, in Halcyon's tracking space, over
  `[Holsters] eyeHeightM` (1.65 m). Halcyon's recenter re-origins that space at the head
  (`DoRecenter`), so the head stood at height 0 then; between recenters the reference is a slow-rising
  maximum too. The log names the source.

**Decide** (`ik::posture::CrouchDetector`, unit-tested): crouched once the fraction stays below
`crouchBelow` (0.75) for `crouchDwellS` (0.2 s); standing again once it stays above `standAbove` (0.85)
as long (hysteresis: hovering at one threshold, or between them, changes nothing).

**Act** (`ik::posture::CrouchArbiter`, unit-tested): `BioPawn.ShouldCrouch(true / false)` through
ProcessEvent, what L3's ToggleCrouch calls, **on changes of the physical posture only**:
- L3 (a toggle) and the game's own changes (explore movement, sprint, scripts) are kept until the next
  physical crouch or stand: the two never fight. A physical change that finds the game already there
  makes that posture "ours", so standing up releases a crouch L3 began while the player crouched.
- A change while sprinting waits until the sprint ends (ToggleCrouch refuses then too). A change the
  game doesn't take is asked 3 times, then dropped.
- Back from a menu, conversation or the Mako, a crouch of ours stands up if the player has; a crouch
  the game dropped meanwhile comes back if the player still crouches.
- A crouch of ours is released (Shepard stands) when `physicalCrouch` goes off, when VR has been gone
  for 2 s (no host view: headset asleep, VR off), and when the mod unloads.

**When:** VR on foot only: the host's head-tracked view, game mode 0 (not the Mako, conversations,
menus, cutscenes), the player a BioPawn. Halcyon's first-person toggle (K) isn't visible to the mod;
our deploy has first person on from launch.

**Settings** (`[Hands]` in `ASI\LE1MEVR.ini`, added to an existing file with their defaults):
`noCover = 1`, `physicalCrouch = 1`, `crouchBelow = 0.75`, `standAbove = 0.85`, `crouchDwellS = 0.2`.

## 5. Open questions

- **[verify]** The view in a combat crouch: the game's crouch camera (45 units lower) plus Halcyon's
  lean may put the eyes lower than the player's. If it feels like the floor jumps up, the fix is a
  view seam (the host keeping the standing hook height while the crouch is physical) or matching the
  crouch modes' `HookOffset` to the standing ones; both touch the view, so decide after the headset.
- **What the player loses with cover:** `SFXGame`'s script uses of the player's cover state are all
  gameplay: the camera, input (`BioPlayerInput.PostProcessInput`), movement, sprint, the wheels'
  move delay, aim snapping, and `BioPawnBehavior.ProxyTakeDamage`'s protected cone
  (`SFXGameConfig.bCoverProtectedCone`, for some pawn classifications) next to the difficulty's
  `CoverDamageReduction` (0.9 on the upper difficulties, 1.0 below). Physical cover still blocks shots
  (they're traces against the level), but that bonus is gone. Level scripts (tutorial hints) weren't
  searched **[verify]**: none should wait for the player to take cover.
- **[verify]** Whether the explore-mode stand on move input feels wrong in VR (crouch, then step: the
  game stands Shepard; the physical posture acts again only on the next stand and crouch).
