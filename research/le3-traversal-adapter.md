<!-- SPDX-License-Identifier: MIT -->
# ME3 native traversal adapter checkpoint

2026-11-15. Implements the native request/observation portion of L3-8 in `game/Le3Traversal`, reached through
the existing `game/Traversal` lifecycle and command surface. **Implemented offline, not natively accepted.**
Requests and gesture binding default off. No game launch, deployment or saved-game mutation was performed in
this lane. Camera level/yaw presentation is not implemented by this checkpoint and L3-8 is not complete.

## Native routes and evidence

Exact exports were inspected with the existing private LegendaryExplorerCore scanner/LE3 retarget. Scanner and
base-package hashes are recorded in [the melee audit](le3-melee-audit.md). Scripts stay ignored. All exports below
are in original base `SFXGame.pcc` except the explicitly identified Adept package exports. These are script/SDK
findings, not reconstructed game code or runtime acceptance.

| Request | Native route preserved | What must be observed |
| --- | --- | --- |
| `ladder` | `BioPlayerController.FindLadderNode` supplies a live `SFXLadderReachSpec`; its `ReachSpec.Start` must be a ladder node and its end must match `LadderDest`. Native `CanUseLadder`, then `TryUseLadder`, retain use events and native node `OnUse`. | Native discovery must select the nearby approach, including the correct side. The outer true return does not prove start: `OnUse` may reject facing/blockage. Its selected action can be up (46), down (47), or jump-down (30). |
| `vault` | `SFXGameModeDefault.MantleCover` uses normal `BioPawn.StartCustomAction`; the mounted action's `SFXCustomAction_MantleOverCoverBase.InternalCanDoCustomAction` uses its owned `MyMantleInfo`. The inspected chain is `CanPerformMantleSlow` → `FindMantleDistance` → `VerifyLandingClear`. | Action 31/32, native landing/collision, ordinary end or interruption, return to movement. No temporary mod-owned `MantleInfo` is allocated: its embedded arrays stay native-owned. |
| `climb` | Native `FindCover` supplies its own `CovPosInfo`; the unchanged, reflected POD result goes to native `CanPerformClimb`. Only an affirmative result reaches `ClimbCover`. In `SFXCharacterClass_Adept.pcc`, `SFXCustomAction_PlayerMantleUp.InternalCanDoCustomAction` resolves an authored reach path; native stop clears its starting-slot/anchor state. | Action 33, actual mounted action/path, native landing/recovery. A legal out-of-cover approach may still be refused by the current native guard/config. The mod does not override the out-of-cover flag or stick. |
| `evade` | `TryRoll` handles native left/right/backward input; `TryForwardRoll` handles forward input. Current `RawJoyRight/RawJoyUp` selects the entry point using native precedence. | One action 54–57, blocked and allowed paths, native actor eligibility and recovery. Installed Adept `PlayerEvadeForward/Left.InternalCanDoCustomAction` preserves the native `bCanRoll` restriction. |

The `FindCover` result is **native ME3 output**, not a synthetic LE2 cover structure. Its source/destination reflected
struct identities and widths must agree before copying the POD query result between parameter blocks. No
`SetLocation`, `SetRotation`, root-motion writes, forced action ID, collision bypass, direct `StartCustomAction`,
cover snap, input spoof or direct ladder-end call is made. A separate 1.5 m approach cap supplements native ladder
discovery; it can conservatively refuse a fixture and is not claimed to reproduce native reach limits.

Installed `TryForwardRoll` can attempt cover acquisition while storming. Both evade functions and the outer
mantle/climb requests can cancel reload before later admission. The adapter therefore rechecks live mode, compatible
pawn/world, death, ignored movement, walking physics, current action, cover/peek, storm, reload, zoom and host ownership
before dispatch. Weapon/input readiness is checked additionally for evade. Authored seating always refuses requests;
physical seated-play calibration does not. No native `Try*` request is used as a continuous eligibility probe.

## Shared lifecycle and presentation

`core/vr/Traversal::ActionLifecycle` separates requested, started, native-end observed, native-interrupt observed,
unclassified exit, no-start-observed and cancelled observation. The request boolean never changes the lifecycle
to started. A matching native action pointer and kind must be observed. A load/actor/world/mode/generation change
invalidates the observation even if an address is reused.

The native adapter observes `BioPawn.StartCustomAction`, `BioCustomAction.EndThisCustomAction` and
`BioCustomAction.InterruptThisCustomAction` through both existing ProcessEvent and CallFunction hooks. End/interrupt
correlation is to the exact observed action, not every action on the pawn. An interrupt wraps the common end, so
its later callback overrides the common-end classification. An exit without the matching callback remains
**unclassified**; a half-second polling deadline means **no start observed**, not proven refusal. The normal-end
callback is evidence of the native end route, not proof that a destination, mission trigger or visual transition
was correct. Overrides not reaching these common callbacks need additional targeted hooks after observation.

The adapter reuses `MoveRecord` for native camera/pawn movement and collision, the shared press policy and the
existing comfort mask/cue channel. `status` includes action name/ID, request serial/kind, native phase, recovery,
camera yaw/pitch and measured movement. Recovery requires the native action slot to clear, walking physics and
movement input to return. A long-running native action is never forcibly stopped by a VR timeout or tracking loss.

The renderer's existing two-row cue atlas now has a third **Use ladder** row with a ladder icon. It avoids
promising ascent/descent before native selection, particularly at a top node that may choose jump-down. The cue
describes discovery, not successful native admission. Both-eye visual review remains open.

The existing ME2 `LevelView` removes pitch/roll but follows native yaw. Applying it unchanged does not prove
stable headset orientation across a native ME3 roll/ladder turn. This checkpoint measures that yaw; it neither
writes camera/pawn transforms nor claims `[Traversal] view=level` is applied on LE3. Stable base-yaw ownership,
tracked-head composition and a non-snapping handback are the next presentation increment. Cue/mask are usable
independently. Normal locomotion, sprint/physical-cover behavior and movement powers remain other L3-8 acceptance
requirements; this checkpoint does not declare them complete.

## Coordinator wiring

1. Add `src/game/Le3Traversal.cpp` to the existing shared game source list. Existing `Traversal` public entry points
   delegate on LE3. Call startup **before installing** CallFunction/ProcessEvent hooks. A missing reflected field,
   class or signature refuses startup. No global module gate changes are required or made here.
2. Populate `traversal::Link.allowNativeTraversal` with a callback that reads the **current** native/host context,
   menu, input focus, hand ownership and loading/scene restriction at dispatch. Null refuses. Supply monotonically
   changing `sceneGeneration`, and `authoredSeated` independently of physical seated play. Existing tracked view,
   cue, vignette, controller and right-A fields remain shared.
3. Keep `le3DoubleTap=false` initially. When enabled, the shared double A requests a discovered ladder or a vault;
   selected noncombat Use targets take precedence. Climb-up currently has an explicit diagnostic command rather
   than a guessed automatic height classifier. `evadeDown` is a separate semantic input; it must not be mapped to
   the same action/use press. It requires neutral release after lost ownership. Replays must drive native stick
   direction before evade; the adapter does not synthesize it.
4. Pair with the updated host `plugin/TraversalCue.cpp` for the new cue row. Current settings `enabled`, `commit`,
   `doubleTapS`, `cue`, and mask settings are shared. `view` is still measured/native on LE3, as explained above.

LE3 reflection check additions for the coordinator (no SDK wrappers in gameplay):

- `BioPawn.CurrentCustomAction` int; `CoverType/CoverAction` bytes; `bStorming/bCombatPawn` bools;
  `BioCustomAction.m_oPawn`, `Pawn.Weapon`, `PlayerController.PlayerInput`, `Actor.WorldInfo` object fields;
  `Actor.Physics` byte and `bBlockActors` bool; inherited `PlayerInput.RawJoyUp/RawJoyRight` floats.
- `ReachSpec.Start` object, `End` `ActorReference` struct (24 bytes) and its `Actor` member;
  `SFXNav_LadderNode.LadderDest` object; existing selection object/target/combat flag.
- `FindLadderNode`: out object at 0, float `MinDistance` at 8, bool return at 12, total 16.
  `CanUseLadder/TryUseLadder`: object `LadderNode` at 0, bool return at 8, total 12.
- `FindCover`: `FoundCover` struct at 0 (56), bool `bWideCheck` at 56, bool return at 60, total 64.
  `CanPerformClimb`: same struct `CoverInfo` at 0, bool return at 56, total 60.
- `MantleCover/ClimbCover`, `EndThisCustomAction/InterruptThisCustomAction`: no parameters.
  `TryRoll/TryForwardRoll`: bool return, size 4. The mode/action/state/guard query signatures are the ones
  listed in the [melee adapter checkpoint](le3-melee-audit.md#staged-ordinary-request-adapter-2026-11-15).
  Observe `BioPawn.StartCustomAction` and `Engine.Camera.UpdateCamera`; do not invoke lower-level starts.

## Exact protected native probe

Use the existing protected runner, command channel, snapshot and replay tools under root's exclusive game window.
Never create a new launch/control framework or force-stream a fixture.

1. Check `mevr traversal status` reports the LE3 observer ready, requests off; verify the new reflection rows.
   With a live on-foot pawn and fresh host guard wired, run `mevr traversal requests on`.
2. At an ordinary reachable ladder, run `mevr traversal probe`, capture the named approach/end, then
   `mevr traversal ladder`. Poll `status` across started → native end/interruption → recovered, with snapshots
   and movement/collision telemetry. Repeat upward, downward/jump-down and a blocked/misaligned approach.
3. At a natural vault, run `mevr traversal vault` and the same observation. At a natural climb-up approach,
   supply normal forward intent then `mevr traversal climb`. If `CanPerformClimb` refuses a valid out-of-cover
   route, record live flag/input/native guard results; inspect that native function before any override.
4. In a legal combat fixture, drive one normal stick direction and run `mevr traversal evade`. Repeat all four
   directions plus native `bCanRoll=false`, reload, storm, zoom, menus, authored seating and pending-action refusals.
5. Verify ordinary native input as the comparison, normal mission trigger continuation, save/reload and scene
   cancellation. `mevr traversal requests off` blocks new requests while an already-started native action is
   allowed to finish. Physical-headset camera comfort remains a separate gate.

Offline checks: changed core/game/renderer translation units and traversal tests compile with strict MSVC warnings
for LE1/LE2/LE3; SDK-only static assertions confirm the native parameter sizes/offsets above; executable traversal
and melee intent tests pass. Lifecycle tests distinguish dispatch/start, duplicate requests, nested interruption,
unclassified action disappearance, timeout and scene-generation reuse. No native acceptance is inferred from them.
