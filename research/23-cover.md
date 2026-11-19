# 23. Cover and crouch in VR (LE1; LE2 in section 7)

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
  (`SFXGameConfig.bCoverProtectedCone`, true in `BIOGame.ini`). Read again 2026-11-05 (the script
  decompiled locally with Legendary Explorer's core, the difficulty table from `Coalesced_INT.bin`):
  for the player and the squad (classifications 5 and 6), **in cover and not leaning out**
  (`IsInCover() && !IsInCoverLeaning()`), a hit whose shot comes from the cover's far side (its direction
  against the wall's normal: `dot(shot, -CoverNormal) < -0.3`, within about 72 degrees of the normal)
  and from more than 300 units (3 m) away has its damage multiplied by `1 - CoverDamageReduction`. The
  difficulty handler's `CoverDamageReduction` (`SFXDifficultyHandler`, `Level<N>DifficultyData`) is 1.0
  on Casual and Normal and 0.9 from Veteran up: **no damage at all from the front on the two lower
  difficulties, a tenth of it above.** With `noCover` the player is never in cover, so a VR player has
  none of it (the squad keeps it). What still protects is the level's geometry: an enemy's shot is a
  trace from its muzzle toward a bone of the target (`BioAiController.GetAimLocation`: the target's
  `GetTargetZoneLocation`, native, its zones the appearance's `m_aTargetZone`; or the target's location
  plus a step-out offset when the native `GetStepOutRelativeLocationToTarget` gives one **[verify]**) plus a random error of the weapon's drift angle (doubled in a
  combat action state), so a wall between the muzzle and a lowered Shepard stops it. The physical crouch
  is the game's own `ShouldCrouch`, which lowers the skeleton and the collision as low cover's crouch
  does, and halves the noise Shepard makes (`BioPawn.m_fCrouchNoiseModifier` 0.5). Since 2026-11-05 VR cover gives the
  cut back with the head as the game's "leaning out" (section 6). Level scripts
  (tutorial hints) weren't searched **[verify]**: none should wait for the player to take cover.
- **[verify]** Whether the explore-mode stand on move input feels wrong in VR (crouch, then step: the
  game stands Shepard; the physical posture acts again only on the next stand and crouch).

## 6. VR cover: the game's cover protection, taken with the body (2026-11-05)

The user asked whether crouching behind cover protects a VR player: with `noCover` it didn't, beyond the wall itself
stopping shots (section 5). Built the same day and measured in the game on the fake headset (section 6.2's last bullet).

### 6.1 What each game gives a player in cover

Read from each game's script (decompiled locally with Legendary Explorer's core) and its config (unpacked from the
game's coalesced file); the player's game data, none of it in the repository.

| | LE1 (`BioPawnBehavior.ProxyTakeDamage`) | LE2 (`SFXGame.ModifyDamage`) | LE3 (`SFXPawn_PlayerParty.ModifyDamage`) |
|---|---|---|---|
| Who | player, squad | player, squad | player's party |
| When | `IsInCover() && !IsInCoverLeaning()` | `IsInCover()`, not leaning | `IsInCover()`, not leaning, the slot not `bUnsafeCover` |
| Cone | the shot's direction (instigator to hit) against `-CoverNormal` below -0.3, more than 300 units | the same against the cover slot's rotation (`CurrentLink.GetSlotRotation`), unless the damage type `bIgnoresCoverDirection` | the same, unless the damage type `bIgnoresCoverDirection` |
| In the cone | damage x (1 - `CoverDamageReduction`): 1.0 on Casual and Normal (none), 0.9 above | zero | `Global_CoverMultiplier = -CoverDamageReduction` (1.0 on the two easiest levels, 0.9 above); a projectile is stopped |
| Otherwise in cover | whole | x `PlayerCoverDamageMultiplier` 0.75 | whole |
| Leaning out | whole | x `PlayerCoverLeanDamageMultiplier` 0.75 | whole |
| Out of cover | whole | whole | `Global_OutOfCoverMultiplier = NoCoverDamageBonus`: +0.4 from Normal up |

The cone is the same in all three: within about 72 degrees of the cover's normal, from beyond 3 m. LE2 also shortens the
shield regeneration's wait in cover (`PlayerCoverRegenTimerMultiplier` 1.5). The 2026-11-13
[LE2 script inspection](le2-movement-source.md) confirms an additional shield-delay timer decrement when
`bFastCoverRegen` is enabled and the pawn is in cover without leaning; this does not directly multiply recharge amount.
Runtime applicability/timing remain **[verify]**. That inspection also confirms LE2's damage-direction exception above.

### 6.2 The VR rule (all three games) and LE1's module

- **The rule** (`core/vr/VrCover`, unit-tested in `tests/VrCoverTests.cpp`): for one hit, trace from the headset's eyes
  toward the shooter's eyes; if a surface stands within `reachM` (1.2 m) of the eyes, the head is hidden behind cover from
  that shooter: in cover, not leaning. Traced from the eyes so the surface found is the player's own cover, not one near
  the shooter. The cover's facing for the game's cone is the surface's flattened normal, turned toward the player; a top
  or an overhang (normal steeper than 0.7) takes the shot's flattened direction; a shooter straight above gives none. The
  head is the game's "leaning out": raised over the cover to shoot, the eyes are in the open and the hit is whole.
- **LE1** (`game/VrCover`, mod side; `[Cover] protect`, `reachM`, `log` in `ASI\LE1MEVR.ini`, appended by the module):
  `engine/call_function` has answer handlers now (a call without parameters, script or native, skipped and its return
  value written, with the calling frame's Locals: `adapter::kFrame.locals`, 0x2C in LE1 and LE2, 0x30 in LE3). While
  `posture::RefusingCover()` (VR on foot with `noCover`) the module answers the damage call's `IsInCover` for the player's
  pawn: its frame gives the shot (`InstigatedBy`, `HitLocation`, `pDamageCausingActor`), `Actor.Trace` with actors (a
  pawn in the line is looked past, at most twice) finds the first surface, and when covered `IsInCover` answers true,
  `IsInCoverLeaning` false (that frame only) and `BioPawn.CoverNormal` holds the facing until the call returns (a post
  handler on `ProxyTakeDamage`, journalled; also put back each tick). The game's own code then applies its cone, distance
  and `CoverDamageReduction`. The shooter's eyes are its pawn's location plus `Pawn.EyeHeight`; with no instigator, the
  damage causer's location (the game's own fallback). Nothing changes outside the damage call: no cover state, camera or
  animation. Both natives have no opcode of their own (`NativeIndex` 0, flags `0x20401`) and `ProxyTakeDamage` is called
  from `BioActorBehavior.TakeDamage`'s script, so both calls pass through `UObject::CallFunction`.
- **Logs** `vr cover:` (a covered hit every 2 s at most, every judged hit with `log = 1`; the heartbeat's counts);
  snapshot `"mod"."vrCover"`. **Console** `mevr cover` (status), `mevr cover test` (every awake hostile judged now: the
  head hidden or not, by what, how far, and whether the game's cone would cut a hit on Shepard from it), `mevr cover pawns`
  (every awake pawn, nearest first, judged from its location) and `mevr cover hit <damage> <n>` (the game's own
  `BioActorBehavior.TakeDamage` on Shepard's behaviour from pawn n's place, without an instigator so no friendly-fire
  refusal, the damage object of pawn n's weapon or any live weapon's; Shepard's health and shields before and after, and
  what VR cover judged), `mevr cover on|off` (an A/B override until the mod reloads).
- **No menu row yet** (file only): it restores the game's own rule and is on by default.
- **The script hook goes in for it** (`Mod.cpp`): while posture refuses the game's cover the CallFunction hook is put in,
  controllers or not (it used to wait for hand aim, which the fake headset's untracked controllers never turn on).
- **Measured 2026-11-05** (fake headset, the VR career's save on UNC20, a Casual or Normal difficulty since the covered cut
  was whole; the squadmate frozen with the game's `DisableAi` and used as the shooter by `mevr cover hit 30 0`; Shepard
  moved with `mevr setloc`; `mevr cover probe` found the prefab module's walls): Shepard's eyes 0.70 m behind the module's
  far wall (`StaticMeshActor_76`, facing him), the squadmate 32.7 m away beyond the module: judged covered, the game's cone
  cuts it, and two hits of 30 left health 105 and shields 63 as they were. The same spot with `mevr cover off`: shields 63 to
  40.3 (the game's `PlayerDamageTaken` scales 30 to 22.7). 5.1 m from the wall: cover too far, 63 to 40.3. In the open, the
  squadmate 4.3 m away: exposed, 40.3 to 17.7. No `call function:` warning; posture refused no cover entry (the game's
  cover was never taken). Not measured: a hostile's own shot (the instigator branch: its controller's pawn and eye height),
  and the head raised over low cover (the fake headset's head is fixed at standing height) **[verify]** in combat.

### 6.3 LE2 and LE3

The same rule serves both; each needs its own hook into its damage code, and neither game's reflection layout is
measured yet (research/34: our layer stands down there), so neither is built. The plan:
- **LE2** (built and measured 2026-11-14: section 7; the second option was impossible, the shields take the hit inside
  the call): in `SFXGame.ModifyDamage`'s frame, answer `IsInCover` true and `IsInCoverLeaning` false for the player as
  LE1 does; its cone reads `TargetBP.CurrentLink.GetSlotRotation(TargetBP.CurrentSlotIdx)`, which has no link for a VR
  player, so either answer `GetSlotRotation` too (it has a parameter: the answer handler would need to consume it) or
  apply the cone ourselves and set the out parameter `Damage` after the call (a ProcessEvent or CallFunction post-handler
  that sees the parameters). Whether LE2's cover snapping is a problem in VR at all (it's a button there) **[verify]**.
- **LE3**: as LE2, plus the out-of-cover bonus (+40% from Normal up) must not hit a VR player who is behind cover:
  `Global_OutOfCoverMultiplier` is set in the `else` of `IsInCover()`, so answering it true also removes the bonus.

## 7. ME1's cover system on ME2 (2026-11-14, lane F)

The owner: "We have a cover system for me1 that we need to ensure transfers here", and later the same day "We won't snap
to cover for VR, it's not a realistic experience. It risks the same problems of our skeleton not matching our players
position in space." So all three of ME1's parts carry over with ME1's settings and defaults (`[Hands] noCover = 1`,
`physicalCrouch = 1`, `crouchBelow = 0.75`, `standAbove = 0.85`, `crouchDwellS = 0.2`; `[Cover] protect = 1`, `reachM =
1.2`, `log = 0`, all in `ASI\LE2MEVR.ini`). The spec was research/le2-vrik-audit.md "ME1's cover system on ME2". Same
modules as ME1 (`game/Posture`, `game/VrCover`, `core/vr/VrCover`, `core/ik/Posture`), with ME2's names where the game
forces them.

**How ME2 was read** (a weaker model can repeat it; the outputs are game code, kept in a scratch folder, never in git):
the script with `local-tools\le2-inspection-root\conversation-scan.exe "<ME2>\BioGame\CookedPCConsole" <out.txt> --game LE2
--script <Class.Function> --only SFXGame.pcc` (or `--script grep:<word>`; Engine's with `--only Engine.pcc`); the natives with
`tools\ghidra\decompile.ps1 -Game LE2 <rva|-Search name>`. RVAs are of `MassEffect2.exe` 2.0.0.48602 (PE timestamp
`0x60AEEFA5`).

### 7.1 No cover snapping

**ME2 has no automatic cover test for the player** (Ghidra, every native with "Cover" in its name, `-Search Cover`): the
only native that looks for the player's cover by itself is `ABioPlayerController::UpdateCover` (`0xACE2A0`, called from the
controller's tick `0xACD670` and from `execUpdateCover`). It calls FindPlayerCover's implementation (`0xB6E6C0`) and then
only ProcessEvents the HUD (the A prompt for cover, climb and mantle): it never sets `CurrentLink` or calls `AcquireCover`.
So engine/CoverAcquire (ME1's native test hook) stays LE1-only, and `posture::WantsCoverHook()` is false on ME2.

**ME2 takes cover only through one script function**, `BioPlayerController.TryAcquireCover` (an exec, no parameters, returns
bool): A's input alias runs it (an exec through ProcessEvent), and `BioPlayerController.CheckStormToCover` calls it from a
sprint (script, through CallFunction). Its body, in order: `FindPlayerCover(FoundCover, TRUE)` (or `FindPlayerCover(FoundCover)
|| FindPlayerCover(FoundCover, TRUE)` unless `CoverRespectsRotation`), and `return FALSE` if nothing was found; in exploration
`SetActionState(1)` and `return TRUE` (A next to cover draws the weapon); `CancelReload()`; `IsInAnimatedTransition()`,
`CanPerformEnterCover()`; then `AcquireCover(FoundCover)` (which calls `BioPawn.PlayerCoverAcquired`: `SetCovPosInfo`,
`CurrentLink.Claim`, `OnEnteredCover`, `GotoState('PlayerInCover')`). `FindPlayerCover` (`execFindPlayerCover` `0xB38800`:
`iNative 0`, flags `0x424401`, `native final`) has no other script caller.

**The block is one point** (`game/Posture`, `OnFindPlayerCover`): a CallFunction post-handler that sees the calling frame
(new in engine/call_function: `AddFramePostHandler`, the frame's Node and Locals after the call) makes FindPlayerCover's
answer false when its caller is `BioPlayerController.TryAcquireCover` and its context is the player's controller, while
posture refuses cover (VR on foot: game mode 0, the host's head-tracked view, not calibrating; `[Hands] noCover = 1`).
TryAcquireCover then returns FALSE at its first line: no snap, no reload cancelled, no explore-to-combat switch. What it
doesn't touch: the A button itself (use, and lane E's out-of-cover mantle: `CanPerformMantle`, `MantleCover`, `ClimbCover`,
`BioCustomAction_CoverMantle`, `TryClimbOrMantleCover`), any ProcessEvent call of FindPlayerCover (C++ through
`ue3::runtime::Call` never passes CallFunction), and UpdateCover's native call. Log `posture: refused a cover entry:
BioPlayerController.FindPlayerCover found cover for TryAcquireCover ...` (one press asks twice and counts once; a sprint against
cover asks every 0.3 s: logged at most every 2 s, the heartbeat counts the rest). The result write is journalled
(`Posture.findPlayerCover`).

**The HUD's "take cover" prompt** would offer what A no longer does. UpdateCover ProcessEvents
`MassEffectGuiManager.ShowCoverWidget` (or `HideCoverWidget`) on the GUI manager every tick (both are script events with
empty bodies; the GUI manager is `MassEffectGuiManager Transient.SFXEngine_0.SFXGameViewportClient_0.MassEffectGuiManager_0`).
A ProcessEvent post-handler on every `ShowCoverWidget` (`FindFunctionsNamed`: overrides included) calls the game's own
`HideCoverWidget` on the same object at once, under the same switch, before anything is drawn. The climb/mantle prompt
(`ShowClimbMantleWidget`, shown only in cover) is untouched, so lane E's cue is the only vault prompt. Logs `posture: the
HUD's cover prompt ... is hidden again` once, and the heartbeat's `posture: this period the HUD's cover prompt shown N times,
hidden again M times`.

**The sprint into cover:** `BioPlayerController.CheckStormToCover` (`if (bStorming && m_bEnableStormToCover)
TryAcquireCover()`, called from native code; `m_bEnableStormToCover` is on in ME2's config) goes through the same
TryAcquireCover, so the same block refuses it; the sprint itself (`bStorming`) is untouched.

Why not the other points: `TryAcquireCover` through ProcessEvent can't be skipped (the host's hook has post-handlers only);
its other calls (`IsInAnimatedTransition`, `CanPerformEnterCover`) come after `CancelReload` and the explore switch;
`AcquireCover` and `PlayerCoverAcquired` take a struct parameter (an answer handler skips only calls without parameters); a
native hook on FindPlayerCover's implementation would also blank the HUD's prompt and any C++ caller, lane E's route among
them.

A cover already taken (a script, or `[Hands] noCover = 0` before VR came on) is left as on ME1, with ME2's
`SFXGameModeDefault.TryExitCover(bool bForceExit)` called with `bForceExit = true` (without it, it leaves only with the move
stick pushed during a lean or peek), never while `BioPawn.IsInAnimatedTransition()` (a cover move under way); after 2 s
`BioPawn.LeaveCover` (which clears `CurrentLink` to None). ME2's IsInCover (`0xAA4CC0`): `CoverType (+0xAA2) != 0 &&
CurrentLink (+0x8AC) != None && CurrentSlotIdx (+0x9C0) >= 0`; IsInCoverLeaning (`0xAA4CF0`) adds `CoverAction (+0xAA3)` in
3..5.

Test tools: `mevr cover snap on|off|auto` (the snapping allowed, refused, or as the file says, until the mod reloads; it
applies from the next tick), `mevr cover acquire` (TryAcquireCover through ProcessEvent: what A does next to cover; its
answer, the refusals, Shepard before and after), `mevr cover where` (place, IsInCover, CoverType, CoverAction, CurrentLink,
CurrentSlotIdx, crouch, sprint, the storm-to-cover flag, prompts hidden), `mevr cover links [n]` (the nearest CoverLinks).
Recordings: `tools/make-walk-recording.py` plan segments `crouch` (the head and hands `--crouch-drop` m lower, default 0.6),
`a` (the right controller's A held: a tap with `a:0.15`) and `sprint` (A held, the left stick forward); replay with `-Inputs`
for the buttons.

### 7.2 Physical crouch

ME1's rules unchanged (`core/ik/Posture`'s detector and arbiter; research/23 section 4). The call is ME2's
`Engine.Pawn.ShouldCrouch(bool bCrouch)` (it only sets `bWantsToCrouch`): ME2 has no `BioPawn.ShouldCrouch` override (ME1's
also reset the mount attempt and the movement animation state); `BioPawn.SetShouldCrouch` is an event calling it;
`BioPawn.StartCrouch`/`EndCrouch` move the mesh by the height adjust as the engine crouches. **ME2 has no crouch button**
(no `SFXGameModeDefault.ToggleCrouch`), so nothing of the player's fights a physical crouch: the callers are the AI (cover,
firing), `PlayerInAimBack` (cover's aim-back), `BioSeqAct_Crouch` (Kismet) and, as on ME1,
`BioPlayerController.PlayerWalking.PlayerMoveCombatOrExplore`, which stands Shepard up on any move input out of combat. The
arbiter treats those as the game's own changes (kept until the next physical change). The sprint waits as on ME1
(`BioPlayerController.bStorming`). With no snap there is no cover crouch to fight either.

### 7.3 VR cover

ME2's rule (`SFXGame.ModifyDamage`, called from `SFXModule_Damage.SFXTakeDamage`; the player's module is
`SFXModule_DamagePlayer`), for `SFXPawn_Player` and `SFXPawn_Henchman`: `IsInCoverLeaning()` first (x
`PlayerCoverLeanDamageMultiplier`), else `IsInCover()`: if `!DamageType.default.bIgnoresCoverDirection && Normal(HitLocation -
InstigatedBy.Pawn.Location) Dot Vector(TargetBP.CurrentLink.GetSlotRotation(TargetBP.CurrentSlotIdx)) < -0.3` and `VSize(...)
> 300`, then `Damage = 0; Momentum = 0; return`; otherwise x `PlayerCoverDamageMultiplier`; out of cover, the
`SFXGameEffect_NonCoverDamageResistBonus` effect. **Everything after it in the same function, the shields and armour among
them** (`SFXInventoryManager.ProcessDamage` -> `SFXShield_Base.ApplyDamage`), runs inside ModifyDamage, so a Damage set after
the call would have reached the shields already: "apply the cone ourselves and set the out parameter after the call" can't
work. The cut must be the game's own `return` inside the call.

**The cone's facing.** A VR player has no `CurrentLink` (`LeaveCover` sets it to None, and with no snap it is never set),
and a call on None never reaches CallFunction (UE3's context opcode skips the call and zeroes the rotator: the cone would face
world +X). So for the one damage call:
1. `IsInCoverLeaning` (answer handler, caller ModifyDamage, context the player's pawn): the player's real cover is asked
   first through ProcessEvent (`IsInCover`); in the game's own cover nothing is answered (its rules). Else the VR rule judges
   the hit (`core/vr/VrCover`, the trace from the eyes, `reachM`), from the instigator's pawn (location plus EyeHeight); with
   no instigator nothing is answered (the game's cone has no shooter either). Covered: answered false (not leaning out).
2. `IsInCover` right after: answered true. Unless the damage type's default `bIgnoresCoverDirection` is set (read from its
   class default object; a damage class without one isn't answered), `BioPawn.CurrentLink` holds **CoverLink's class default
   object** (journalled, `VrCover.currentLink`): never in a level, and its `GetSlotRotation` (`0x490050`) returns its own
   Rotation for any slot of its empty Slots array (`SlotIdx < 0 || SlotIdx >= Slots.Num`).
3. `CoverLink.GetSlotRotation` (a frame post-handler, caller ModifyDamage, the same frame): its rotator replaced by the hiding
   surface's facing turned to point into the cover (yaw `atan2(-n.y, -n.x)`, pitch and roll 0: Vector() of it is ME1's
   `-CoverNormal`), journalled (`VrCover.slotRotation`), and `CurrentLink` put back to None at once.
4. ModifyDamage's post-handler and every tick put back anything still held.
The game's own code then applies its cone, its 300 units, zero or its in-cover multiplier, its `bIgnoresCoverDirection`
exception and its damage-calculation records. No cover state, camera or animation outside the call.

Test tools on ME2: `mevr cover [on|off | test | pawns | probe <yawDeg> [rangeM] | put <me|n> <x> <y> <z> | hit <damage> <n>]`.
`hit` is the player's damage module's own `SFXTakeDamage` (found on the module's class: `SFXModule_DamagePlayer`) with pawn
n's controller as the instigator (ME2's cone needs `InstigatedBy.Pawn`) and the pawn as the causer; a hostile's hit takes the
default damage type (`SFXDamageType_Default`), a friend's a loaded damage type whose defaults have `bDamagesFriends`,
`bHealthDamage` and not `bIgnoresCoverDirection` (ModifyDamage refuses a friend's hit otherwise), named in the answer. Health
is `SFXModule_Damage.CurrentHealth`, shields the sum of `SFXShield_Base.CurrentShields` over the pawn's inventory chain (as
SFXTakeDamage adds them). `put` is `Engine.Actor.SetLocation` (ME1's `mevr setloc`).

**Open, the owner's choice:** ME2 also shortens the wait before the shields recharge while in cover (`SFXShield_Base.Tick`:
with `bFastCoverRegen`, a pawn in cover and not leaning takes an extra decrement of the shield-delay timer,
`PlayerCoverRegenTimerMultiplier` 1.5). The VR rule answers only inside ModifyDamage, so a VR player hiding behind cover gets
the damage rule but not the faster recharge. Granting it would mean answering `IsInCover`/`IsInCoverLeaning` in
`SFXShield_Base.Tick` too, with a judgement that isn't per shot (from which shooter? the nearest hostile, or every awake one).
**Granted by the owner on 2026-11-16 ("Faster recharge behind cover, yes") and built: section 7.5.**

### 7.4 In the game (fake headset, 2026-11-14 23:12-23:20, Freedom's Progress, Save 4)

`tools/le2-halcyon-run` (`prep.ps1 -Holder me2-lane-f -BuildDir <worktree>\build-trilogy`, `launch.ps1`, `load.ps1`, the
console and replays below, `launch.ps1 -Stop`, `restore.ps1`: `verification: 0 differences`). `reflect: 357 of 357 checks
agree`, `posture: script functions found`, `vr cover: SFXGame.ModifyDamage's IsInCoverLeaning and IsInCover are answered
...`, `hooked UObject::CallFunction (7 handlers)` once posture was active. Each step, repeatable as written:

1. **Physical crouch:** `make-walk-recording.py crouch.mevrrec --plan stand:8,crouch:4,stand:8 --hands view`, `replay.ps1 -Path
   crouch.mevrrec -Wait`. No floor from the LE2 host yet, so the reference is the head's drop over `[Holsters] eyeHeightM`
   (1.65 m). At 8.2 s `posture: physical crouch: eyes 1.05 m = 0.64 of the standing 1.65 m, below 0.75 for 0.20 s` and `Shepard
   crouches (...): Engine.Pawn.ShouldCrouch(true)`; 0.75 s later `Shepard crouched yes` (`bIsCrouched`), the view 20 units lower
   against the pawn. At 12.2 s `physical stand ... above 0.85 for 0.20 s`, `ShouldCrouch(false)`, 0.76 s later `crouched no`.
2. **No snap, by the console:** `mevr cover links` (49 CoverLinks; CoverLink_28 7 m from the save's spot), `mevr cover put me
   -2295 20865 -11909` (next to its low crate). `mevr cover snap on`, `mevr cover acquire`: `TryAcquireCover returned TRUE`,
   after 2 s `in cover yes, CoverType 2, CoverAction 8, CurrentLink CoverLink_28, CurrentSlotIdx 1`, Shepard moved 0.93 m into the
   slot (-2295, 20865) -> (-2278, 20774), crouched (the game's own snap). `mevr cover snap off`: `Shepard is in low (crouching)
   cover (...): leaving it as A does`, out 0.03 s later, `CurrentLink None`. At the same spot `mevr cover acquire`:
   `TryAcquireCover returned FALSE (FindPlayerCover refused 1 times)`, Shepard at (-2295, 20865) before, after and 2 s later,
   `in cover no`.
3. **No snap, by A:** `make-walk-recording.py tapa.mevrrec --plan stand:3,a:0.15,stand:5`, `replay.ps1 -Inputs -Wait`: at 3 s
   `posture: refused a cover entry ...`, Shepard unmoved and out of cover. With `mevr cover snap on` the same replay put Shepard
   in CoverLink_28's slot 1 (the replayed A reaches the game's TryAcquireCover).
4. **The prompt:** near the crate with the snapping refused the heartbeat read `the HUD's cover prompt shown 142 times, hidden
   again 142 times` per 5 s; with `snap on` `shown 152 times, hidden again 0 times`. Looked at again with lane B3's HUD
   panel on main (23:52, the same spot): the panel's `-hud.png` shows the power bar, the reticle, the weapon panel and the
   squad's portraits, and **no cover prompt in either state**, snapping refused or allowed (the game's own `ShowCoverWidget`
   is an empty script event; whatever ME2 draws for it doesn't show at this spot on the fake headset). So the hide is
   proven by the calls, not by a picture: **[verify]** where lane E saw the prompt (CoverLink_10, the combat stance, the
   pad's glyphs) that it is gone with the snapping refused.
5. **The sprint:** `make-walk-recording.py sprint.mevrrec --plan stand:2,sprint:5,stand:3 --stick 1.0`, replayed with `-Inputs`
   from 1 m in front of the crate, the snapping refused: `sprinting yes` during it, Shepard ran into the crate and stopped there
   (0.97 m), `CheckStormToCover`'s TryAcquireCover refused every 0.3 s (5 refusals logged), never in cover.
6. **VR cover:** Jacob put beyond the crate (`mevr cover put 1 -2290 20200 -11909`: he stood at (-2290, 20303, -12109) when
   hit), Shepard at (-2290, 20800). The head lowered 0.9 m (`hide.mevrrec`: `--plan stand:2,crouch:60 --crouch-drop 0.9`):
   `mevr cover pawns`: `SFXPawn_Jacob_1 at 4.7 m: covered by StaticMeshCollectionActor_1364 0.35 m from the eyes, facing (0.15,
   0.99); the game's cone cuts it to nothing`. The squad is friendly, so `hit` took `SFXDamageType_CheatKill` (the first
   loaded damage type with `bDamagesFriends`, `bHealthDamage` and not `bIgnoresCoverDirection`).
   - covered, `protect` on: two hits of 30, health 257.5 and shields 257.5 unchanged; `judged 1, covered 1, the cone cut 1,
     the cover's facing given 1` each;
   - covered, `mevr cover off`: shields 257.5 -> 233.3 -> 209.0 (the game scales 30 to 24.2);
   - the head raised (replay stopped), `protect` on: `cover too far (StaticMeshCollectionActor_1364 1.70 m from the eyes)`,
     shields 257.5 -> 233.3 -> 209.0;
   - Miranda in the open, 4.5 m, `protect` on: `exposed`, shields 257.5 -> 233.3.
   Afterwards `CurrentLink None`: the link held for the cone was put back. The first build's log said `the cone cut 0` for the
   covered hits although the game cut them: ME2's `HitLocation` is an out parameter and the frame's own copy of it isn't the
   hit, so the log's cone now takes the player's place (fixed and measured again: `the cone cut 1`).

Not measured: a hostile's own shot (the instigator is then its AI controller; the friend rule doesn't apply), damage types
with `bIgnoresCoverDirection` (they take the in-cover three quarters, no link held), a shot from within 3 m.

**LE1 unchanged** (the same run script on LE1, `tools/fake-headset-session.ps1 -Tag lanef-le1`, the players' build of this
branch, the owner's career where it loads, 2026-11-14 23:28): `hooked ABioPawn::TryAcquireCover`, `vr cover: the damage
call's IsInCover and IsInCoverLeaning are answered ...`; the crouch replay `SFXGame.BioPawn.ShouldCrouch(true)` at 0.64 of the
standing height and `(false)` at 1.00, as before; `mevr cover hit 30 0` from a pawn 3.3 m away in the open: `exposed`,
shields 63.0 -> 39.5 -> 15.9 (protect on), then 15.9 -> 0 and health 141 -> 135 -> 116.4 (off): ME1's own damage path as
section 6.2 measured it (the covered case wasn't set up again: LE1's code paths are the same lines, moved).

### 7.5 The shields' faster recharge behind cover (2026-11-16, lane GR)

The owner, 2026-11-16 (docs/trilogy-plan.md open question 2): "Faster recharge behind cover, yes."

**Which games have the rule.** Only ME2. Read from each game's script (the scanners of section 7 and research/le3-traversal-adapter.md;
the outputs stay in a scratch folder):
- **ME2:** `SFXShield_Base.Tick(DeltaSeconds)`: `if (bFastCoverRegen && ShieldRegenTimer > 0 && BioPawn(Owner) != None &&
  !BioPawn(Owner).IsInCoverLeaning() && BioPawn(Owner).IsInCover()) ShieldRegenTimer -= (PlayerCoverRegenTimerMultiplier - 1) *
  DeltaSeconds;` then the ordinary `ShieldRegenTimer -= DeltaSeconds`; the recharge starts once the timer is 0. With the config's
  1.5 the wait in cover is two thirds of the delay (a 3 s delay: 2 s). `SFXShield_Base.ApplyDamage` sets the timer to
  `GetShieldRegenDelay()` on every hit, and `SFXModule_DamageParty.Regen` (the health's regeneration) waits while any shield's
  timer runs, so the health's wait shortens with it. `bFastCoverRegen` is the only reference to the flag in the package.
- **ME3:** `SFXShield_Base.Tick` counts its timer down with no cover question; the delay comes from the difficulty handler
  (`SFXShield_Player.GetShieldRegenDelay`, `PartialShieldRegenDelay`). No script function about shields or health regeneration
  calls `IsInCover`. No rule to grant.
- **ME1:** the shield's regeneration is native (`BioShield.Regenerate`, `UBioShield::execRegenerate` RVA `0xBA4F40` calls a virtual),
  and no script function that names regeneration calls `IsInCover` (34 script functions call it: AI, cover moves, the damage
  call, the HUD). The native body wasn't read; ME1 is not known to have such a rule. Nothing built.

**How "in cover" is judged outside a shot: against every awake hostile** (`core/vr/VrCover` `ShelterCount`, `Shelter`, unit-tested
in `tests/VrCoverTests.cpp` `TestShelter`). Each awake hostile within 40 m (the nearest 16) is judged as a shot from it would be
(section 6.2's `Judge`: the line from the headset's eyes to its eyes): the player is behind cover when the head is hidden from every
one of them, and by a surface within `[Cover] reachM` of the eyes from at least one. A hostile behind a far wall (`cover too far`)
neither grants nor denies it; one with a line to the head (or straight overhead) denies it; with no hostile judged there is
nothing to shelter from and the game's rule stays as for a player out of cover. Why this and not the nearest hostile alone:
- it is the damage rule's spirit: the damage rule asks "hidden from the one who shoots"; outside a shot every hostile might
  shoot, so the head must be hidden from each, and the faster recharge is what a hit from any of them would also respect;
- the nearest alone is wrong both ways: a nearest hostile in another room (behind a far wall) refuses a player crouched behind a
  crate in front of the real fight, and a nearest hostile hidden by the crate grants it while a second one sees the head;
- it is cheap: one trace per hostile, at most 16 traces every 0.25 s (the verdict is cached that long), and only while the
  shields' wait runs (the game asks only then: `bFastCoverRegen` and `ShieldRegenTimer > 0`), on the player's shield only.
What a player feels beyond the recharge: nothing; no cover state, camera or animation changes, as for the damage rule.

**Built** (`game/VrCover`, ME2's block; no host change): the same two answer handlers as the damage call's, for the frame of
`SFXShield_Base.Tick`: `IsInCoverLeaning` answers false and `IsInCover` true for the player's pawn while VR cover applies (`posture`
refusing the game's cover: VR on foot, `[Hands] noCover = 1`), `[Cover] protect = 1` and the new `[Cover] recharge = 1` (on by
default: the owner's yes; appended with the `[Cover]` block in a new file only), the game's own cover not taken (then its own
answers stand), and the player is behind cover by the rule above. The game's own code then takes its own extra decrement: no
timer or shield is written by us. The answers are read (not written) per call; nothing is held between ticks.
- **Logs:** `vr cover: SFXShield_Base.Tick's IsInCoverLeaning and IsInCover are answered ...` at the start; `vr cover: while the
  shields wait: <verdict> (N hostiles judged: ...)` when the verdict changes (at most every 2 s); `vr cover: the shields' wait
  before recharging: X s after a hit that set Y s, Z s of it behind cover (the game's fast regen in cover on)` after each wait,
  measured from the game's own `ShieldRegenTimer` (`core/vr/VrCover` `RegenWait`, unit-tested `TestRegenWait`); the heartbeat's
  `vr cover: the shields' recharge: ...`. Snapshot `"mod"."vrCover"."recharge"` (ME1 and ME3: `{"applies": false}`).
- **Console:** `mevr cover regen` (the player's shield, its timer, `bFastCoverRegen`, the shields, and the shelter judged now
  hostile by hostile); `mevr cover regen as <n>` judges against pawn n of `mevr cover pawns` instead of the hostiles (a fixture
  without hostiles: Save 4's squad), `as off`; `mevr cover recharge on|off` (an A/B until the mod reloads).
- **ReflectCheck rows** (ME2): `SFXShield_Base.ShieldRegenTimer`, `bFastCoverRegen`, `SFXShield_Base.Tick`.

**In the game** (fake headset, 2026-11-16 20:00-20:06, Freedom's Progress, Save 4, session `20261116-195519-020-LE2-gr-me2`,
restored `verification: 0 differences`): the player's shield is `SFXShield_Energy_Player_0`, its `bFastCoverRegen` **on**, the regen
delay after a hit **5.00 s**. The places of section 7.4 (Shepard with `mevr cover put me -2290 20480 -11909`, Jacob put at
`(-2290, 20200)`, standing on the lower ground at -12109, `mevr cover regen as <Jacob>`): each hit was `mevr cover hit 30 <Jacob>`
with `mevr cover off` for the hit (else the cone cuts it to nothing and no wait starts: case D) and `on` again 0.5 s later.

| case | the shelter | the wait before recharging |
|---|---|---|
| A: behind the crate, `recharge` on | `behind cover (1 hostile judged ... 1 hidden by cover)`, the crate 1.22 m from the eyes | **3.51 s**, 3.00 s of it behind cover |
| A again | the same | **3.67 s**, 2.67 s of it behind cover |
| B: the same place, `mevr cover recharge off` | (not asked) | **5.02 s** |
| C: Shepard 3.2 m back (`-2290 20800`), recharge on | `hidden only by things far away` (the crate 3.93 m from the eyes) | **5.02 s** |
| C': Jacob in the open, recharge on | `in the open to a hostile` | **5.50 s** |
| no hostile at all | `no awake hostile` | 5.01 s |
| D: protect on, behind the crate | (the damage call) | the cone cut the hit to nothing (shields 257.5 unchanged): no wait started |

The game's own rule is a 1.5x countdown while in cover: 0.5 s unsheltered (the toggle's gap) plus 4.5 s / 1.5 = 3.50 s, as measured
(A 3.51 s); the second A had the shelter answered later in the wait (2.67 s of it). **For this run `[Cover] reachM` was 1.5** (the
file's 1.2 put back after): on ME2's current main a replayed crouch no longer lowers the eyes (floor lock pins the eye at the live
STAGE height, 1.7526 m, `floor lock: ... correction +102.261, final eye 1.7526 m` while the replay's head was 0.9 m down), so the
head stayed at standing height and the crate's nearest point on the line to Jacob was 1.22-1.28 m away. That is a test-tool gap
(replays don't stand in for the STAGE floor query), not the rule's: the lane that owns floor lock was told. Not measured: a
hostile's own shots, several hostiles, a headset.

### 7.6 "Almost magnetic": what the owner's run shows (2026-11-18, lane CV2)

The owner, from the headset (ME2, Freedom's Progress from Save 4, the run 20:34-20:51 of 2026-11-18, `LE2MEVR.log` of commit
237aeb1b): "It has this behavior where almost like clicks and moves Shepard to be behind cover, where it's almost magnetic, and
so it's a really strange experience in VR. That needs to be completely disabled."

**The game's cover was never entered for the player** (each point is a `grep` on the run's log):
- posture asks the game's own `BioPawn.IsInCover` every tick (through ProcessEvent, which no answer handler of VR cover sees) and
  would log `posture: Shepard is in ... cover ...: leaving it as A does`: no such line in the run;
- `TryAcquireCover` (A's binding, a sprint into cover) found cover twice, both refused (`posture: refused a cover entry ...
  Shepard stays at (-10372, 4971, -13609)`, 20:50:37, and the press 1 s later), and found nothing 5 times; the HUD's take-cover
  prompt was shown and hidden again 14,819 times (UpdateCover's every tick near cover);
- the sprint never ran (`sprint: ... 0 presses`), and the camera's modes were only `SFXCameraMode_Explore`, `_Combat` and
  `_Interpolate` (no cover camera);
- the one `BioCustomAction_CoverMantle.StartCustomAction` (20:41:12) was a squadmate's (the traversal's handler logs every pawn's;
  the floor under Shepard didn't move and no double A or commit of ours preceded it).

So the pull is not ME2's cover. What did move the drawn body near low cover in this run is the physical crouch: each crouch behind
a barrier (20:46:18 to 20:49:28, the eyes at 0.82-0.91 of the standing 1.85 m; the calibrated threshold is 0.92) made Shepard
crouch (`Engine.Pawn.ShouldCrouch(true)`), and late in each crouch arm IK's anchor fell to third person (`arm IK: body anchor: third
person (tracking origin at Shepard's animated head), view 76 units from Shepard's animated head bone`), which stops the body sliding
under the view (`torso: third person: the hips turn ... body shift 0.0 units`): the drawn body jumps 0.5-0.76 m to where the game's
crouched pawn is, against the barrier. At 20:49:10 the game stood Shepard up while the player stayed down (`posture: Shepard's crouch
changed without us`), and the anchor stayed third person for 17 s. The owner turned the physical crouch off in our menu at 20:50:16
(`main menu: [Hands] physicalCrouch = 0`). The crouch, its threshold and the anchor are lane M2W's: section 8 (the lean
guard, the crouch that follows the player). **The owner's question:** was the pull felt while crouching behind a barrier, or while walking up
to one standing?

**Built, so the next run names whatever it is: the cover watch** (game/Posture, ME2, while the snapping is refused): `posture: cover
watch: the player's controller <state> -> <state> (...)` whenever the controller's state changes (`PlayerWalking`, `PlayerInCover`,
`PlayerExitingCover`, `PlayerInAimBack`...), `posture: cover watch: the pawn's cover fields are set (...)` when `CoverType` or
`CurrentLink` becomes set, and `posture: cover watch: Shepard carried N m in S s with the move stick at rest (...)` when the pawn moves
more than 0.25 m within 0.25 s with the move stick's raw axes at rest and no traversal of ours; each line with Shepard's place,
`CoverType`, `CoverAction`, `CurrentLink` and its slot; the heartbeat counts them (`posture: cover watch this period: ...`). The
refusals themselves are unchanged: every entry of ME2's into cover goes through `TryAcquireCover` (section 7.1), and a cover taken by
a script is left at once (`TryExitCover(true)`, then `LeaveCover`).

### 7.7 VR cover in the owner's firefights, and the level look (2026-11-18, lane CV2)

The owner: "We need to make sure the fire fights are solid, and the cover system needs to follow Shepard as best we can." In the run
the owner crouched behind barriers (20:46-20:48, the eyes 1.0-1.3 m over the floor against 1.85 m standing), yet `vr cover: 164 hits
judged since the start: 0 in cover (0 cut by the game's cone), 153 in the open, 11 with cover too far, 0 from overhead ...; last:
SFXPawn_AssaultDrone_8 at 15.9 m: exposed`. The shields' rule, the same judge per hostile, found the head hidden by cover within
1.2 m twice (`vr cover: while the shields wait: behind cover (3 hostiles judged: 2 hidden by cover within 1.20 m, ...)`, 20:44:13
and 20:47:20). The log had no per-hit detail (only covered hits were logged), so why each hit was open is read from the geometry:
- **the shooters:** the fights in `BioD_ProFre_300Drones` were drones (`SFXPawn_AssaultDrone`, `SFXPawn_RocketDrone`), which hover
  over the barriers, besides LOKI mechs at 13-26 m; the judge is the line from the drawn eyes to the shooter's eyes (`Location +
  EyeHeight`);
- **the eyes:** the drawn eyes (`view_anchor::Current().headWorld`): floor lock puts them at the pawn's floor plus the player's live
  height, and body scale (1.095 here) draws the player 0.16 m taller than Shepard, so they are not Shepard's head;
- a drone 2 m above the eyes 15 m away is 6.7 cm above them at a barrier 0.5 m ahead: with the eyes 5 cm under the barrier's top
  the line still clears it; and eyes at 1.3 m are over most of ME2's low cover (the spaceport crate's top is 0.75 m over the floor,
  section 7.4's trace).

**The game's own rule doesn't ask the shooter's height:** ME2's cone is `Normal(HitLocation - InstigatedBy.Pawn.Location) Dot
Vector(slot rotation) < -0.3` with the slot's rotation flat, so Shepard in low cover is protected from a drone above it (the cosine
of its elevation, 0.99 at 7 degrees). The eye line asks more than the game does.

**Built: the level look** (`core/vr/VrCover` `Level`, `Choose`, `ParseRule`; unit-tested, `tests/VrCoverTests.cpp`
`TestLevelLook`; the sequels' `game/VrCover`): besides the eye line, a line from the eyes toward the shooter, flat, `reachM` long
(`[Cover] levelDropM` below the eyes, 0 by default): a surface on it covers the head from that shooter whatever its height, and
gives the cone its facing as before. `[Cover] judge = either` (the default on ME2 and ME3: covered by either), `line` (the eye line
only, as before), `level` (the level look only); `mevr cover judge line|level|either|auto` until the mod reloads. ME1 keeps the eye
line, its code unchanged (the LE1 mod's `.text` identical before and after). The eyes must still be under the cover's top: a head
over it is in the open, the game's "leaning out". Both looks also judge the shields' faster recharge (section 7.5) and `mevr cover
pawns`.

**Per-hit detail** (the sequels): every judged hit gets a line, an open one at most once a second, a covered one every 2 s as
before (`[Cover] log = 1`: all): `vr cover: a hit from <shooter> at D m: <verdict> | the eyes H m over the floor, the shooter's eyes
Z m from theirs (A deg); the eye line met <surface> X m out, Y m from the eyes' height (<its verdict>); the flat line met ...`. The
heartbeat says how many were covered by the level look alone (`... in cover (N by the level look alone, ...)`). So the next firefight
says which test each hit failed and by how much.

**ME3** (`game/Le3Cover.inc`): the same judge (`JudgeHit` is shared), then a real cover slot within reach must qualify; the level look
and the detail apply there too. Not seen in a fight yet (research/le3-combat-fixtures.md).

**[verify] in the game:** a hit from a shooter above a barrier with the head ducked behind it (`mevr cover pawns` names both looks per
pawn; `mevr cover put <n> x y z` stands a squadmate on a crate, `mevr cover hit 30 <n>` the shot), and the owner's firefight.

## 8. The crouch in the owner's ME2 session (2026-11-18, lane M2W)

The owner, ME2 in the headset (MEVR 0.3.2, 237aeb1b; `LE2MEVR.log` 20:35-20:51, copied to scratch): "When my hand touches an NPC
or companion, I go into a kind of crouch"; "the button click to crouch doesn't seem to be functioning in game, because left click
now triggers the minimap locator"; and "We need to make sure the fire fights are solid, and the cover system needs to follow
Shepard as best we can."

### 8.1 A lean is not a crouch

- **What crouched him.** The physical crouch, from the calibration's threshold: `posture: standing eye height 1.85 m ...;
  Shepard crouches below 1.69 m and stands above 1.74 m (between your calibrated heights)` and, reaching toward a squadmate,
  `posture: physical crouch: eyes 1.69 m = 0.91 of the standing 1.86 m, below 0.92 for 0.20 s` (20:37:12, again 20:37:19 at
  1.68 m). The calibration (one file for the trilogy since 2026-11-18, `%LOCALAPPDATA%\MEVR\calibration.ini`) has the owner's
  crouch at 1.56 m for 1.87 m standing: 31 cm down (his other headsets' sections 26-28 cm). `vr::calibration::CrouchFromHeights`
  puts the threshold half way down (`crouchAt` 0.5), 15.5 cm, then caps it at 0.92 of standing; so 15 cm under "stand tall", and the
  owner's ordinary stance in the session was 1.77-1.82 m (the floor lock's STAGE eye every second): 8-13 cm of margin. ME1 reads the
  same file with the same rule, so ME1 crouches on the same lean. Uncalibrated (`[Hands] crouchBelow` 0.75) it is 46 cm.
- **What he saw.** Shepard in the game's crouch under a view at 1.69 m (the floor lock keeps the eyes at the real head): arm IK
  left first person (`arm IK: body anchor: third person ... view 75 units from Shepard's animated head bone`, 20:37:12.7), and
  ME2 stood him up again 2 s later when he walked (8.2).
- **The torso's own "crouching with you"** (`[Hands] crouchBody`, research/29 "The eyes inside the body"): 3-4 cm at the reach
  (the head 4 cm too high), and 14-17 cm three times within a second of a full-speed run (20:37:01.9, 20:37:02.9, 20:39:21.8) with
  the eyes at 1.84 m: while running, the gap the torso measures is the run pose's, not a crouch. Not this lane's rule; told in the
  report.
- **Built, off until the owner's yes** (`core/ik/Posture`, unit-tested in `tests/PostureOwnershipTests.cpp`):
  - `[Hands] crouchMinDropM` (0 = off; suggested 0.25): `ik::posture::WithMinimumDrop` moves the crouch threshold down until the
    eyes must be that far under the live standing height, never below the calibrated crouch + 3 cm (so it still crouches), the
    stand keeping its hysteresis gap. The owner's numbers at 0.25: the crouch at 1.61 m (0.866), the stand at 1.66 m; the leans at
    1.68-1.69 m don't crouch, his calibrated 1.56 m does.
  - `[Hands] crouchLeanRatio` (0 = off; suggested 1.0): `ik::posture::LeanGuard` keeps where the head stood (horizontally, in the
    room) while the eyes were in the standing band (at or above `standAbove`) and calls a drop a lean while the head has moved at
    least that many times as far horizontally as down; the detector's crouch dwell then starts over (it gets the threshold, not
    past it). Standing up is never held back. A reach (0.30 m forward for 0.09 m down) is a lean at 1.0, a squat (0.05 m forward,
    0.40 m down) is not. Log `posture: a lean, not a crouch: ...`. The anchor follows the standing band, so with the uncalibrated
    0.85 a lean is measured from where the head crossed 0.85, not from where it stood.
  - Both in the startup line (`posture: standing eye height ...`) and the settings line.

### 8.2 The crouch follows you (`[Hands] crouchFollow`, on: the owner's instruction)

- **What stood him up.** `posture: Shepard's crouch changed without us (L3, sprinting or a script): now standing` at 20:49:10.998
  with the player crouched at 1.06 m (0.57; crouched at 20:48:46, standing again for real at 20:49:28): the game's own stand, 18 s
  against his posture. The lines around it: the pistol holstered at 20:49:03.4 (`movement: walking mode ExploreCrouched
  (exploring) ...; CombatCrouched held 17.2 s, top speed 130 u/s`), then `walking mode ExploreRun ... ExploreCrouched held 7.6 s,
  top speed 0 u/s` at the stand and the pawn at 98 u/s 40 ms later (a slow push: no response window opened). The same at 20:37:14
  (`ExploreCrouched held 2.0 s, top speed 0` then the walk). So **ME2 stands a crouched Shepard up to walk with the weapon away**:
  its explore set has no crouched walk (ExploreCrouched's top speed 0), its combat set has (CombatCrouched 130 u/s). Which
  function does it is [verify] (`PlayerMoveCombatOrExplore` exploring, or the crouched explore speed 0 asking for a stand).
- **The rule** (`ik::posture::CrouchArbiter`, `ArbiterInput::follow` and friends): a change the game makes without us that goes
  against the player's real posture (the detector's state) is put back (`ArbiterStep::restoring`, a pending put-back) once the
  game takes it (not sprinting) and its passing reason is over (`canRestore`: the pawn under 30 u/s, no vault or climb
  (`traversal::Moving`), not in cover, and 0.15 s since the change); a change the crouch button made is kept as ME1 keeps L3
  (ME1: the game's change within 0.5 s after a left stick click through L3's gate, `gamepad::CrouchClickToggle` on the raw
  clicks, read before the crouch tick; a click seen in the 0.15 s after the change, which the host may have passed a frame
  sooner, cancels the put-back as the button's; the sequels: the mod's own L3, `CrouchArbiter::Pressed`); the game undoing a put-back again within 1.5 s twice in a row with no passing reason seen
  (`explained`: moving, sprinting, a vault) wins until the next physical crouch or stand (`ArbiterStep::insisted`: a script, a rule
  of the game's). `[Hands] crouchFollow = 0` is the old rule (every change of the game's kept until the next physical change).
- **Logs** (`game/Posture`): `posture: the game stood Shepard up while you are crouched (walking, 98 u/s): he crouches again once
  he stands still ([Hands] crouchFollow)`, `posture: putting back the crouch waits (...)`, the put-back's `crouch request accepted
  (putting back the crouch the game changed (...): your posture)`, `posture: the game stood Shepard up again soon after we put him
  back (...): it wins here ...`, `posture: Shepard's crouch changed by L3: ...`; the heartbeat counts put-backs and insists.
- **On ME2 that means:** crouched with the weapon away, a walk stands him (the game's rule), and he crouches again as soon as he
  stops; with a weapon drawn he crouch-walks (the game's CombatCrouched). [verify] in a fight on ME2 and on ME1 (where a crouched
  walk is the game's in both modes, so a put-back should be rare).

### 8.3 L3 crouches on the sequels (`[Hands] crouchClick`, on: the owner's)

- **The game's L3 there**: ME2's `BIOInput.ini` binds `XboxTypeS_LeftThumbstick` and `XboxTypeS_RightThumbstick` both to
  `Console_ShowMap` (`OnTap 0.3 ShowAreaMap | OnHold 0.3 TurnObjectiveRadarDisplayOn | OnRelease TurnObjectiveRadarDisplayOff`);
  ME3's `Coalesced.bin` (decoded with Legendary Explorer's `CoalescedConverter.DecompileGame3ToMemory`, a scratch program; the
  game data not kept) binds both to `Console_NavAssistanceOrCoverTurn` (`Exclusive TryCoverTurn | Exclusive ActivatePOI |
  OnRelease DeactivatePOI | ShowAreaMap`). ME1's L3 is `Console_Crouch` (`ToggleCrouch`), section 3. So on the sequels the owner's
  L3 opened the map and the objective radar; no game crouches on it.
- **Built** (ME1's semantics, the right stick click's pattern from 2026-11-17): in VR on foot on ME2 and ME3 the mod toggles the
  crouch itself (`game/Posture` `TickCrouchClick`: `Engine.Pawn.ShouldCrouch(!bWantsToCrouch)`, refused while sprinting as ME1's
  ToggleCrouch refuses; ME3's native admission inside `ShouldCrouch`), each press through L3's own gate
  (`gamepad::CrouchClickToggle` over `GateClick`: the both-sticks tap and hold never toggle, a quick click counts at its release, a
  held one at 100 ms), kept until the next physical crouch or stand (`CrouchArbiter::Pressed`). Not while the power wheel or the
  squad menu has the left hand. The mod tells the host it owns the click (`gamepad::kFlagModCrouchClick`, bit 28; no HostServices
  change) and the sequels' host keeps that press from the game (`Options::keepCrouchClick`, host-only, set by
  `plugin/HalcyonSequelBridge`; `Latches::leftClickKept`, decided when the press goes down, so no gate pulse after a kept tap).
  LE1's host leaves it off: ME1's L3 still reaches its game. The area map stays on the omni-tool's Map entry. Logs `posture: L3
  (the crouch button, as ME1's): crouch request accepted ...`, `halcyon bridge: first left stick click kept from the game ...`.
  Tests: `TestCrouchClick` (tests/PowerWheelTests.cpp). [verify] in the game: `tools/le2-halcyon-run/posture-walk.ps1 -Phases l3`
  (no `ShowAreaMap` in the call trace, the crouch toggling).
