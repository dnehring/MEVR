<!-- SPDX-License-Identifier: MIT -->
# ME3 physical crouch, sprint and cover adapters

Read-only installed-package inspection on 2026-11-15 reused the existing private LegendaryExplorerCore
scanner described in [the melee audit](le3-melee-audit.md). Base `SFXGame.pcc` SHA-256 is
`1d8f7acb6f28e78030982c9f2d935cddec16877272aba314ce1c21dff5ed2d18`.
The inspected source was the original installed game, not a closed mod. Raw extracts remain ignored in
`local-tools/le3-locomotion-audit`; this note records consequences, not game implementation bodies.
Mounted DLC overrides, native physics and controller acceptance still need runtime proof.

## Reuse and current implementation

`game/Posture` reuses the existing `core/ik/Posture` physical-height detector, dwell/hysteresis,
external-crouch arbitration and guided/manual calibration. ME3's inspected `BioPawn.SetShouldCrouch`
delegates to `Engine.Pawn.ShouldCrouch`, which requests crouch through the pawn's desire flag. The adapter
calls that existing function and observes both `bWantsToCrouch` and `bIsCrouched`. It does not write capsule
height, movement mode or position: native physics must establish whether crouching/standing is possible.
A changed desire flag is not proof of changed collision or eye height.

ME3 requires explicit current host ownership, focus, no menu/calibration, a fresh callback and scene
generation. Every native request rechecks ordinary mode, walking physics, no custom action, living pawn,
no native cover and no native storm. Authored seated actions and nondefault modes remain excluded.
World/scene changes forget owned crouch and standing-height samples. A physical seated play preference
continues using the shared calibration policy; it is distinct from an authored shuttle scene.

`game/Sprint` was LE1-only; the ME2 module still has no faster-run implementation. Its ME3 adapter reuses
`core/vr/Sprint` stamina/exhaustion/release logic and the existing compare-before-restore GroundSpeed
ownership. ME3 declares `BioPawn.StormSpeed`; LE1's `CombatStormSpeed` is absent and is not assumed. LE3's `bStorming` belongs to `BioPawn`,
not `BioPlayerController`; the adapter changes both the reflected owner and read receiver.
`SFXGameModeDefault.StormOn` and `StormOff` are script functions with no parameters, enabling the existing
skip-handler mechanism after reflected shape validation. While eligible, a fresh ordinary on-foot press
can request the shared faster run, preserving hand-held weapon use. Native `IsStormDisabled`, movement
ignore, walking physics, cover, crouch, death and custom-action state all exclude this replacement.
A current host callback is checked at dispatch, not just the previous module tick. Stale held requests
are cleared on ownership/native-admission loss or controller/world changes. `sprint = game` preserves
the native route. Actual input routing, measured speed and animation remain native acceptance gates.

## Native cover differs materially

| Installed base export | Required consequence |
| --- | --- |
| `SFXGameModeDefault.TryAcquireCover` / `FindCover` | LE3 cover acquisition belongs to the mode, not ME2's controller. Admission includes animated-transition, cover permission, input direction and native enter-cover checks. `FindCover` is the bounded result seam before reload cancellation or acquisition. |
| `SFXGameModeDefault.TryExitCover` | No force parameter. It has a specific leave-cover latch and transition/zoom restrictions. Do not pass ME2's force argument or transplant its timed forced-exit fallback. |
| `SFXGameModeDefault.StormOn`, `CanEnterCover`, `StormOff`, `TurnStormOff` | Sprint controls also operate cover latches/timers and cover actions. A faster-run replacement is restricted to ordinary uncovered gameplay; it cannot blanket-skip all mode instances. |
| `SFXPawn_PlayerParty.ModifyDamage` | Native protection uses the real current cover slot's unsafe-cover flag, direction/distance eligibility, difficulty-owned cover and out-of-cover modifiers, projectile behavior and a cover-protection callback. ME2's `SFXGame.ModifyDamage` owner is absent. |
| `BioPawn.ModifyDamage`, `SFXPawn_Player.ModifyDamage`, `SFXGRI.ModifyDamage` | Other layers retain invulnerability, friendly-fire, authored scene and popup modifiers. Do not replace the entire damage pipeline with a generic VR multiplier. |

ME2's temporary empty `CoverLink` default object is **not a valid ME3 adapter**: the ME3 damage path indexes
its slot array before the direction query. This checkpoint therefore leaves ME3 native cover/protection
unchanged and does not claim no-snap or physical-cover parity. The next adapter must qualify a real slot
or establish a different narrowly scoped native seam, preserve unsafe-cover and projectile semantics,
and keep all temporary state within the exact damage call. No synthetic slot, arbitrary damage replay,
forced cover exit or direct transform movement is justified by this audit.

## Offline evidence and remaining acceptance

The existing `TestCrouchFraction`, `TestCrouchDetector`, `TestCrouchArbiter`, and `TestSprint` tests ran in a
small private driver linked to the unchanged shared core, with zero failures. The touched native adapters
passed MSVC C++ syntax/type checks for LE3; cross-game checks are recorded with the checkpoint handoff.
Metadata checks are coordinated through the existing LE3 ReflectCheck table, not a new native harness.
No game, deployment, VR runtime, package or save was changed for this work.

The coordinator must supply the new fail-closed Posture Link fields/callback and Sprint Tick ownership
arguments. Native checks must prove physical crouch collision and obstructed stand behavior, tracked eye
height without double lowering, native/script crouch arbitration, menu/focus/world recovery, sprint
press/release/stamina and restoration without clobbering a game-authored speed change. Cover acquisition,
unsafe slots, physical protection, out-of-cover bonuses and projectile behavior remain separate required
work; these modules must not be described as completed L3-8 acceptance.
