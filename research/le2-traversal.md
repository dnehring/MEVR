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
4. A single A goes to the game as before: with lane F's block (`game/Posture` `OnFindPlayerCover`, `[Hands] noCover = 1`) it uses
   what is selected or nothing. Before that block landed the runs used a test stand-in: `set BioPlayerController MaxCoverEnterDist 0`
   through the console (`tools/le2-halcyon-run/vault.ps1` does it unless `-NoBlock`).

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

(filled in below)
