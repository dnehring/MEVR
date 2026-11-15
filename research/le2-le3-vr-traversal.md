<!-- SPDX-License-Identifier: MIT -->
# Ladders, vaulting and mantling in ME2 and ME3 VR

2026-11-14. P5 design research and offline package inspection. The interaction
and comfort choices below are proposals for headset testing.

## What the VR references establish

| Reference | What it does | What MEVR can use |
| --- | --- | --- |
| CircuitLord's Titanfall 2 VR | The publisher releases a playable campaign mod with motion controllers. The MIT source in the public repo is the installer. The gameplay mod is closed source. | Use the campaign's fast parkour as a comparison for legibility and comfort. The public repo supplies no ladder, mantle or wall-run implementation to port. [Author's README and license scope](https://github.com/CircuitLord/CircuitLordVRModInstaller#license) |
| TinyBlackDog's separate Titanfall 2 VR | The public copy identifies itself as an MIT Northstar plugin, version `v0.1.1`. It presents jump, crouch, sprint and move as stock gamepad actions. Its controller guide explicitly retains the game's slide and wall-run behavior. The inspected controls and camera files did not establish hand-over-hand climbing or a dedicated VR mantle action. | Keep native traversal and add VR intent and presentation around it. Read the source for camera composition and input ownership before considering any code reuse. [Controls](https://github.com/kyus2001/titanfall2vr/blob/main/CONTROLS.md#vr-controllers), [license](https://github.com/kyus2001/titanfall2vr/blob/main/LICENSE) |
| Half-Life 2: VR Mod | Grip and pull to move on a ladder. Near an exit, an icon shows the destination, and releasing both grips commits the dismount. Stick climbing is available through Comfort settings. Two-handed grip climbing requires a free weapon hand. | Make attachment, direction and dismount legible. Give the same authored route a seated and one-handed option. [Source VR Mod Team manual](https://halflife2vr.com/manual/#ladders) |
| Half-Life: Alyx | Valve ships blink, shift and continuous locomotion. Its ladder update adds an option requiring hands-on climbers to reach the top before releasing both hands can move them to the exit. | Treat rung motion and the final dismount as distinct phases. Observe the real exit condition before handing input back. [Valve locomotion deep dive](https://www.youtube.com/watch?v=TX58AbJq-xo), [Valve update notes](https://store.steampowered.com/oldnews/?appgroupname=Half-Life%3A+Alyx&appids=546560&enddate=1743490800&feed=steam_community_announcements) |
| Assassin's Creed Nexus VR | Offers manual climbing, grip-assisted climb teleport, a visualizer for available parkour actions, and optional Auto-Parkour. It also offers grip toggles and hand smoothing for ledge interactions. | Mark the specific reachable ledge or ladder exit. Offer physical input as an option and give assisted traversal the same destinations. [Ubisoft accessibility manual](https://www.ubisoft.com/en-gb/game/assassins-creed/news/4Yb9RvSj2ouXX2ZvSiebie/assassins-creed-nexus-accessibility-comfort-feature-manual) |

The TinyBlackDog camera source documents a stair lurch caused by composing an
old game camera position with a fresh tracked head offset. It publishes the
offset separately and applies it to the base latched on the render frame.
The normal tracked path takes pitch and roll from the headset and composes
body heading separately. For MEVR, check camera timing at step-ups, top-outs,
landings and transition back to gameplay before tuning motion curves. The
source supports this timing lesson. Its behavior during individual Titanfall
wall-run or mantle animations still needs observation. [Camera composition](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/camera_hook.cpp#L720-L768), [stair fix](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/camera_hook.cpp#L916-L945)

These shipped projects use several comfort approaches. Meta's guidance adds
reasons to keep head tracking responsive, limit artificial acceleration and
peripheral optic flow, and offer a lower-effort alternative to climbing by
hand. A large positional discontinuity can also disorient the player. Use a
world-space destination cue and preserve the view heading through the handoff.
[Meta locomotion comfort guidance](https://developers.meta.com/vr/design/locomotion-comfort-usability/)

## What the Mass Effect games own

| Game | Observed source path | Port implication |
| --- | --- | --- |
| LE2 | `SFXPlayerController.TryClimbOrMantleCover` starts from a live cover link and slot and tries climb before mantle. The action binding also handles use, cover and sprint. `TryExitCover` has its own conditions and can cancel reload. [LE2 movement source](le2-movement-source.md) | Keep the cover state required for traversal even if VR suppresses cover-camera snapping. A physical peek or a hand near a wall is insufficient to authorize a vault. Start by preserving the ordinary action and classify the native result. |
| LE3 ladder | `BioPlayerController.CanUseLadder` checks the enabled node and its destination reach spec, including occupancy. `TryUseLadder` runs the normal use event and node action on authority. `SFXNav_LadderNode.OnUse` checks facing and local pawn clearance, then selects ascent, descent or a short drop. It supplies the reach spec to the custom action. | Preserve native entry, authored path, blocked exits, action choice and use events. A generic `PHYS_Ladder` check alone misses this reach-spec route. A successful `TryUseLadder` return still needs observed custom-action entry, because the node can decline the move. |
| LE3 vault and mantle | `BioPlayerController.CanPerformMantle` is native. The inspected `BioPawn.TryEarlyMantle` starts the early mantle action, and `VerifyLandingClear` checks the path and estimated landing. The game exposes separate ordinary, early, up and down mantle actions. EA's original PC manual also distinguishes vault and evade. [EA ME3 PC manual](https://eaassets-a.akamaihd.net/eahelp/manuals/mass-effect-3-manuals_PC.pdf) | Treat a running approach, vault from cover, climb up and drop as distinct cases. Use their native admission and completion. Keep evade and cover movement out of the ladder/vault gesture even if they share a flat-game control. |

LE3 findings come from LegendaryExplorerCore inspecting installed base
`SFXGame.pcc`, SHA-256
`1d8f7acb6f28e78030982c9f2d935cddec16877272aba314ce1c21dff5ed2d18`.
The private retargeted copy of the existing scanner and raw outputs are in
`local-tools/le3-traversal-survey`. Native helper internals, mounted overrides,
live timing and player reachability still require verification. Halcyon's LE3
reference also tracks crouch and cover independently. Carry that distinction into
the observer and qualify each game's state against its own executable.
[Halcyon LE3 cover observations](https://github.com/jrush64/MassEffect3-VR/blob/cfd40019450d3469b44a9b155f03e0263a61f787/src/engine_probe.cpp#L2168-L2176)

## Recommended first implementation

**One deliberate contextual action.** At an eligible ladder or obstacle,
show a small world-space cue at the authored interaction point. Label the
action and direction, such as Climb up, Climb down or Vault. Preview the exit
when the native route can identify it. Give existing use actions priority in
ambiguous contexts. A fresh press commits a single native action. Proximity,
normal walking, looking at the target, a held grip from reload and returning
from a menu must never commit it. Leave the same action reachable one-handed,
seated and with either dominant hand.

**Keep the authored move and separate the view.** The game owns the pawn,
collision, root motion, timing, events, protection and final stance. During a
short vault, prototype a level, tracked first-person view that follows the
essential translation without animation-driven pitch, roll or bob. Compare
that with a brief blink or vignette at the vault. For long climbs, tall mantles
or hard drops, offer an assisted comfort presentation that masks rapid motion
while the actual native action runs. If the move is long enough that a blackout
would hide the scene or combat for too long, test a room-fixed view of the
authored move using the existing screen presentation. Preserve physical head
tracking in every presentation and retain world heading where legally possible.
Restore control from observed native completion and valid placement, with both
eyes using the same state. A blink masks a move already in progress. It must
preserve real collision, elapsed time, path triggers and save behavior.

**Add physical control only at a verified seam.** On a ladder with genuine
incremental native movement, a valid free-hand grip anchors to an authored
rung or calibrated point near it. Pulling along the ladder axis drives bounded
climb intent; sideways movement, head motion and loss of tracking contribute
zero travel. Stop cleanly on handover or loss of grip and hand back only at a
legal exit. Give a soft grip confirmation, an exit cue and an assisted route
for people who prefer fewer reaches. On a canned climb, use the grip as an
optional deliberate start for the existing action. A hand-over-hand prototype
can wait until each phase can preserve the native path and triggers. For
mantles, a free-hand brace can be an optional alternate commit gesture once
normal button traversal works. Never move the pawn from hand IK or a hit
against arbitrary scenery.

**Share presentation and input policy.** Reuse the ME1 comfort turn ownership,
head/body separation, calibrated hands, floor alignment and world-space cues.
ME1 already gives the game's ladder state priority over artificial turn. Use
the [renderer interface](../src/plugin/Renderer.hpp) for presentation and
separate per-game observation and action adapters for native states. Use a
single owner during traversal so artificial turning, body follow, reload,
weapon grabs and sprint cannot keep writing incompatible input. Sample the
native interruption outcome before rearming held controls. Reset ownership
when the pawn or level changes. [ME1 comfort behavior](31-comfort.md),
[ME2 continuation plan](../docs/le2-continuation-plan.md)

## Proof order and acceptance

1. **Native control and fixtures.** Use a protected copy of the proven
   [Freedom's Progress Save 4](le2-validation-evidence.md#freedoms-progress-save-4-pistol-baseline-2026-11-14-utc)
   as a route into LE2 cover combat. The SpacePort package
   `BioD_ProFre_100SpacePort` has two cover links with mantle-enabled slots,
   including `CoverLink_10`. This is an authored candidate for observation.
   Acquire a natural checkpoint immediately before a legal, reproducible
   vault or mantle, then pick a blocked or misaligned comparison. For LE3,
   installed packages contain reciprocal
   placed ladder endpoints in Vancouver (`BioD_ProEar_200`) and Mars
   (`BioD_ProMar_240ExteriorWalk` and `BioD_ProMar_710Chase`). Acquire a
   pre-ladder save using the normal route and confirm the active streamed
   nodes. These are package-backed leads, with fixture reachability pending.
   Add one LE3 early mantle from a running approach and one vault from cover. Take
   the same action through to the next normal trigger, save and fresh reload.
2. **Instrumentation.** Record eligibility, request and acceptance, target
   identity, entry/active/exit action, pawn transform and collision result,
   camera anchor and head pose, both-eye events, elapsed time and return of
   movement and aim. Test blocked or occupied exits, cancellation where the
   native action allows it, tracking loss, pause, reload or holster overlap,
   checkpoint retry, and dismount from either direction. Check for one action
   per fresh press, no camera recoil from the body animation, no stale grip
   after recovery, no wall clipping and no skipped or repeated mission trigger.
   Verify save persistence through the normal game. The [LE2 QA runner](../tools/qa-le2/README.md)
   handles the protected native baseline. Use the [Halcyon LE2 test loop](../tools/le2-halcyon-run/README.md)
   for repeatable pose/input replay and eye captures when the observers exist.
3. **Headset comparison.** Compare the same route with level first-person,
   masked and assisted presentation. Start with one short movement and extend
   to consecutive vaults and a taller ladder. Include seated and standing
   play, dominant-hand swap and one-handed operation. Ask players to identify
   the target and exit before using it. Record first-attempt success, unintended
   activations, time to regain orientation and aim, perceived vertical lurch,
   discomfort and arm fatigue. A fake headset can reproduce input and expose
   camera and state regressions. Comfort, reach and disorientation require
   physical headset review.

The first implementation gate is a native LE2 vault with scripted repeatability
and a comfortable tracked presentation. Prove one LE3 ladder up/down and quick
vault next. Add physical climbing after the corresponding action offers a
verified incremental movement path.

## ME2: the first gate

Built and checked on the fake headset 2026-11-14 (lane E): [research/le2-traversal.md](le2-traversal.md). In
short: with no cover snap in VR (the owner), the double A standing at the obstacle commits ME2's own out-of-cover
mantle (`m_bCanMantleOutsideOfCover`, set only for the game's own admission call; then `MantleCover`), the same
vault three times in a row from a script, one native action each; ME2's camera stays level and smooth through it,
so the presentation option (`[Traversal] view = level`, `mask = vignette | blink`) is a guard and a comfort
choice, off by default. Left: arm IK across the move's root-motion steps, the grips on ME2, the owner's choices.
