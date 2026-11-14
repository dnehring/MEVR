<!-- SPDX-License-Identifier: MIT -->
# LE2 movement, traversal and cover: offline source findings

Inspected 2026-11-13 while ME1 owns the native session. This is read-only package
and configuration research, not runtime proof. No game or renderer was launched.
Private decompiler output remains in
`local-tools/le2-validation-evidence/movement-source/`; only this original summary
is committed. LegendaryExplorerCore through the [scanner](../tools/conversation-scan/README.md)
read installed LE2 `SFXGame.pcc`, SHA256
`681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`.
The English `Coalesced_INT.bin` hash is
`a9adef3e0f18201dd8f841a6edecdf6ea2392c88dabed372284dc26220383869`;
its previously unpacked private `BIOInput.ini`/`BIOGame.ini` supplied the binding
and configuration leads. Runtime profile overrides and mounted changes must be
checked before relying on these defaults.
The private `movement-source/manifest.json` binds package/configuration/tool and
output hashes. Grep mode can skip failed decompilations; positive inspected
functions support the findings below, but the output is not a complete catalog.

## Action routing and traversal are coupled

The inspected `SFXGameModeBase` input configuration maps the controller action
alias through use, climb/mantle, cover acquisition, release-to-stop-sprint,
hold-to-sprint and tap-to-exit-cover commands. The PC action alias has the same
sequence. Authored hold/tap thresholds differ (0.2/0.3 seconds); these are config
values, not measured input timing. `Exclusive` appears on use and cover acquisition;
its exact native parser/consumption behavior remains to be checked. `Console_Action`
is a configured alias: an exact function lookup found no such script function.

`SFXPlayerController.TryClimbOrMantleCover` is the actual script owner, rather
than `SFXGameModeDefault`. It requires a pawn and current cover link, constructs
the current cover-slot position, then tries climb before mantle through native
eligibility/action helpers. The inspected wrapper does not provide those helpers'
native collision or navigation implementation. A blanket cover-acquisition ban
can remove the state this traversal entry needs. Preserve a verified traversal
route even when ordinary VR locomotion suppresses camera snapping.

`SFXGameModeDefault.TryExitCover` takes a required `bool bForceExit`. Without the
forced path, movement and the current lean/peek action gate exiting. A successful
exit cancels weapon reload before breaking cover and can enable exit acceleration.
Calling it with an LE1 parameter layout or treating it as a harmless camera reset
would be incorrect. Reload cancellation can transfer ammo: see [reload research](le2-ammo-fixture-source.md).

## Sprint is more than a speed field

`SFXGameModeDefault.StormOn` requires positive remapped forward input. Cover action
can prevent it; allowed cover exits happen before sprint intent is set. The
script clears the deferred stop timer and emits a tutorial event when admitting
that intent. `StormOff` schedules `TurnStormOff` using `InputDelayStormExit`, and
the latter clears sprint intent. `BioPlayerController.CheckStormToCover` can
attempt cover acquisition while sprinting when its separate enable flag is set.

`BioPlayerController.CanStorm` is **native**, so the package exposes a declaration
and does not establish its internal eligibility rules. Inspecting the exact
native implementation is a possible later executable-analysis task if normal
observation plus existing callers cannot answer a concrete integration question.
No native address or copied parameter layout is established by this report.

`BioPlayerController.PlayerWalking.PlayerMoveCombatOrExplore` separates combat,
exploration, crouch, sprint and free-camera movement. It uses native maximum
speed, acceleration and locomotion-module state. Combat walking can use different
speed attributes from exploration. Sprint constrains facing and movement; cover
can replace facing with the slot rotation. It also writes aim direction after
movement calculations. A controller-aim adapter must verify its ordering against
that write: a correctly oriented gun mesh alone is insufficient.

`BioPlayerInput.PostProcessInput` coordinates zoom with reload, sprint intent,
weapon transitions, powers, cover action, empty weapons and custom actions.
Bypassing the native sprint/cover states can therefore change aiming and combat
behavior as well as locomotion. The current MEVR `game/Sprint.cpp` and
`game/Posture.cpp` implementations are guarded for LE1. Their speed override and
cover-suppression behavior are reuse candidates, not existing LE2 implementations.

## Damage and shield benefits must be considered separately

`SFXGame.ModifyDamage` processes more than cover: weapon modifiers, cinematic and
plot protection, friendly damage rules, body-part damage, effects, sprint and
inventory behavior surround it. A port should retain that native path.

For player/henchman pawns, the inspected cover portion distinguishes leaning,
in-cover and out-of-cover states. Leaning uses the configured lean multiplier.
An in-cover frontal hit beyond 300 game units can be suppressed, using the
current cover slot's rotation and a directional threshold near -0.3. **LE2 also
checks the damage type's `bIgnoresCoverDirection` flag before that suppression.**
The older cross-game table omitted this LE2 exception; it is not unique to LE3.
Remaining in-cover damage uses the cover multiplier; out-of-cover damage can
instead use a passive resistance effect. Sprint has an additional multiplier.
Do not replace this with a universal zero-damage predicate.

The full protection path expects an actual cover link/slot. Merely returning
true from `IsInCover` for a free-moving VR pawn can make the damage path access
missing native cover state. The existing pure `core/vr/VrCover` proximity/cone
math can help classify physical cover, but the LE2 adapter must preserve native
exceptions and supply a safe, scoped integration. Never fabricate a permanent
world cover link just to satisfy this predicate.

`SFXShield_Base.Tick` resolves the older regeneration question: when
`bFastCoverRegen` is enabled, a positive shield-delay timer gets an additional
decrement for a pawn in cover and not leaning, before its normal decrement if
still positive. The inspected config multiplier is 1.5. This shortens the wait
before recharge; it does not directly multiply shield capacity or the later
recharge amount. Crossing the timer boundary is tick-dependent. The flag's
runtime applicability and actual timing remain unverified. Matching cover damage
alone would not establish matching shield recovery.

## Next bounded P5 proof

1. Establish read-only state observation through the [P1 discovery](le2-engine-access-discovery.md)
   and [observer contract](le2-runtime-observer.md): movement/cover mode, current
   link validity, sprint intent/actual sprint, input ignored flags, reload phase,
   aim direction, damage type and shield-delay state. No LE1 offsets transplanted.
2. Capture a normal native use/sprint/stop/cover-entry/exit/climb/mantle baseline
   on a natural fixture. Include gamepad and keyboard/mouse action routing,
   release after pause/menu, blocked forward input, combat versus exploration,
   reload-to-cover transitions and a required traversal gate.
3. Compare controller-directed locomotion and late weapon aim against that
   baseline. Include seated/standing physical crouch, actual collision and camera
   recovery after a forced movement action. Verify hits independently of visuals.
4. For physical cover, compare front/flank/above hits, near/far instigators,
   leaning and damage that ignores direction; retain difficulty/effect settings.
   Measure shield-delay/recharge separately. Preserve native protections and
   resource rules outside the scoped cover adaptation.

All four steps require owned native access before acceptance. Source names and
config defaults alone do not authorize parameter calls, prove animation behavior
or establish that a mission's traversal remains reachable.
