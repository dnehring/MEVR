<!-- SPDX-License-Identifier: MIT -->
# ME3 melee: shared interaction, native action adapter

Source audit, 2026-11-15, baseline `7a3a1158`. Companion to the [ME3 plan](../docs/le3-port-implementation-plan.md).
No ME3 melee action was executed. Native function declarations below are investigation leads, not verified calls.
The follow-up installed-package audit below establishes six base class mappings and script-level routing;
live admission, mounted override coverage and headset presentation remain unverified.

## What exists and what is new

ME1 already has physical swing recognition in `src/core/ik/Throwing.{hpp,cpp}` (`SwingDetector`, `RouteMelee`) and
game dispatch in `src/game/Throwables.cpp` (`Strike`). Its hand history, deliberate-stroke detection, cooldown,
enemy observation and post-strike damage observation are useful. The production adapter is LE1-only. Its armed
`TryMelee` and unarmed pistol-whip/fist behavior must not be relabeled as ME3 heavy melee.

ME3 needs explicit light/heavy intent, native action and class mapping, plus presentation through action motion.
A single “omni-blade” animation is not a correct class-independent implementation. There is no reason to duplicate
controller sampling, the IK solver, haptics, gesture history or native damage simulation to add this capability.

## Native evidence

Paths in this table are under `external/LExSDKv2/Src/LESDK/LE3/`. Line references describe the inspected SDK snapshot.
This initial table records SDK declarations. The later installed-package section records the separate script/default-data inspection.

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| `SFXGame_classes.hpp:31105`, `SFXGameModeDefault.TryMelee()` and `TryHeavyMelee()` | Light and heavy requests have separate native entry points. Both return void. | A ProcessEvent return cannot prove admission, impact, damage or action completion. |
| `SFXGameContent_classes.hpp:1103–1209`, pistol/rifle light-melee action families | Native light attacks have multiple chained action classes. | The legal timing for the next stroke or the exact weapon/class routing. |
| `SFXGame_classes.hpp:36824`, heavy-melee base | Native heavy action owns passive-power and impact behavior, with omni-weapon plot identity. | A universal class-to-animation or effect table. |
| `SFXGameContent_classes.hpp:1895`, `AdeptPunch` | At least one concrete class-heavy specialization exists. | Complete mappings for Soldier, Adept, Engineer, Infiltrator, Sentinel and Vanguard. Inventory installed class/DLC packages before publishing that table. |
| `SFXGameContent_classes.hpp:912–963`, cover-grab, cover-melee and mantle-melee actions | Contextual synchronized actions exist separately from ordinary strikes. | That a physical swing should start a cover grab, snap into cover, or bypass destination checks. |
| `SFXGame_classes.hpp:36382`, melee passive fields; `:27140`, weapon melee modifiers | Damage/force/radius/cone and equipment effects are native concerns. | Permission for VR to write damage or treat hand speed as an extra damage multiplier. |
| `SFXGame_classes.hpp:36882`, player-melee movement/presentation fields | Melee can involve synchronization and authored movement, not only a hand animation. | Safe VR camera/body behavior without observing the complete action. |
| `SFXGame_classes.hpp:31160`, Atlas game mode | A non-Shepard actor has separate melee handling. | That Shepard's light/heavy requests work in the Atlas. |

## Installed base-package follow-up

Read-only inspection, 2026-11-15. Reused the existing LegendaryExplorerCore conversation scanner and the private
LE3 retarget from the traversal survey. No game, closed mod binary, save or package was modified. Raw exports,
properties and decompiled scripts remain ignored in the engine worktree's `local-tools/le3-melee-audit/`.

The traversal scanner's private build has SHA-256
`90460f06f91061c44f229eedac0d4770862c80ec727be49bd21da3cb92aab3bb`. Its legacy CLI selector is `--game LE2`,
but its inspected `InspectionContext.Game` maps that selector to **MEGame.LE3** and rejects a package of another
game. It requires the Legendary Explorer **Nightly** assemblies recorded at build time; the separate stable local
assemblies produced a method-version failure and were not used for successful inspection.

The existing class dumper excludes class defaults, so the initial six nondefault character-class inspections did
not establish the mapping. A new ignored private copy changed only that selection to `e.IsDefaultObject`, built
against the same Nightly assemblies, and inspected one exact class/package at a time. This private diagnostic has
SHA-256 `f730ea1def12533edfc4c74843589d6ae34dae5fb7d5ab5e20373d7ae0b2a766`. No second inspection framework was created.
Each of the following class-default queries returned exactly one result.

### Six declared class mappings

Each source is base `CookedPCConsole/SFXCharacterClass_<Class>.pcc`, export
`SFXGameContent.Default__SFXCharacterClass_<Class>`, property `CustomActionClasses` at the zero-based index shown.
The inspected base folder is `C:/Program Files (x86)/Steam/steamapps/common/Mass Effect Legendary Edition/Game/ME3/BioGame/CookedPCConsole`.
The action names below all have the `SFXGameContent.SFXCustomAction_` prefix. These are authored class defaults;
the adapter must observe the live pawn's class/action instance rather than hardcode this table as runtime authority.

| Class | Index 58, class heavy | Index 73, storm route | Relevant distinct contextual mapping |
| --- | --- | --- | --- |
| Adept | `AdeptPunch` | `AdeptPunch` | Index 62: `AdeptSyncCoverGrab` |
| Engineer | `EngineerPunch` | `EngineerPunch` | Index 62: `SyncCoverGrab` |
| Infiltrator | `InfiltratorPunch` | `InfiltratorPunch` | Index 61: `InfiltratorCloakPunch`; index 62: `SyncCoverGrab` |
| Sentinel | `SentinelPunch` | `SentinelPunch` | Index 62: `SyncCoverGrab` |
| Soldier | `SoldierPunch` | `SoldierPunch` | Index 62: `SyncCoverGrab` |
| Vanguard | `VanguardPunch` | `VanguardStormPunch` | Index 62: `AdeptSyncCoverGrab` |

All six declare `RifleMeleeOne` at index 75 and `PistolMeleeOne` at index 78. Index 61's presence for Infiltrator
does not establish when the cloaked route is selected; do not bypass ordinary class admission to invoke it.

Package SHA-256 receipts:

| Base package | SHA-256 |
| --- | --- |
| `SFXGame.pcc` | `1d8f7acb6f28e78030982c9f2d935cddec16877272aba314ce1c21dff5ed2d18` |
| `SFXCharacterClass_Adept.pcc` | `67d91f10d3b0251409f6c2138c03ba557db34ec119724fc02f2007475edd800a` |
| `SFXCharacterClass_Engineer.pcc` | `758386ee09145e66824c3550a644f99f1d67b2d2db994055578f45746c4ab742` |
| `SFXCharacterClass_Infiltrator.pcc` | `4277409b4cd674762cdc64a23043547dd2178784e52f74d48981fb9c469afaea` |
| `SFXCharacterClass_Sentinel.pcc` | `98837f24d988f1c7b0860d06203052abb036d4927010ab2adf49940ce2194bde` |
| `SFXCharacterClass_Soldier.pcc` | `28cf6107da226dcd160818f903cd359c6030b0e55073deb704c4a9f37a1c7430` |
| `SFXCharacterClass_Vanguard.pcc` | `f9092974fefa4ef46fa6286d775a63d6e8fa241a78932c0e539c21f092357a56` |

### Admission and lifecycle implications

The following are findings from exact script exports, not copied implementations or runtime results:

| Inspected export | Finding and implementation consequence |
| --- | --- |
| `SFXGameModeDefault.TryMelee`, `TryHeavyMelee` in base `SFXGame.pcc` | Both are contextual requests, not unconditional light/heavy actions. They reject ignored movement input and require a weapon and non-zoomed state for their normal paths. Ordinary light routing distinguishes pistol/SMG from other weapons. Cover state and storming can select synchronized cover or sprinting actions instead of the ordinary strike. The VR dispatcher must preflight these contexts and require separate intentional contextual input. Unarmed success is not established. |
| Same two exports | The scripts cancel weapon reload before all later custom-action admission is known. A rejected or inappropriate swing request can therefore still interrupt reload. Reject gestures while the hand owns a clip/support/menu and revalidate immediately before dispatch. Never call these functions as harmless eligibility probes. The heavy entry also reads pawn cover state before its later pawn-presence branch: require a live compatible pawn before calling. |
| `BioPlayerController.StartCustomActionWithSyncPartner` in base `SFXGame.pcc` | Resolves the requested action instance and asks it for the victim before delegating the action request. Hand proximity is not the native targeting rule. Preserve native victim selection unless a separately verified aim/target adapter supports changing it. |
| `BioPawn.CanDoCustomAction`, `StartCustomAction` in base `SFXGame.pcc` | Admission depends on an available declared action, its instantiated eligibility and current-action override rules. The latter returns a boolean, unlike the outer `Try*` calls, but a start result still does not establish impact or completion. Observe actual action identity and lifecycle rather than calling these lower-level functions to bypass normal input routing. |
| `SFXCustomAction_PlayerHeavyMeleeBase.StartCustomAction`, `MeleeImpact`, `StopCustomAction` in base `SFXGame.pcc` | Heavy actions use the pawn's melee-passive power, native target/area effects, animation registration and temporary effects on affected actors. Impact can break stealth; stop removes the action's temporary effects and animation registration. Preserve that cleanup on cancellation. Do not directly invoke impact, replay damage, or abandon a native action because the VR gesture ended. |
| `SFXGameContent.SFXCustomAction_RifleMeleeOne.RifleMeleeSecondary`, `NoHeavyMelee`, `HeavyMelee`, `DoRifleMeleeTwo`, `StopCustomAction` in `SFXCharacterClass_Adept.pcc` | The secondary route arms a roughly 0.2-second heavy timer; its alternate callback cancels the active timer and requests the next light action. Heavy/next-light callbacks depend on a living sync partner, and stop clears the timer. Repeated `TryMelee` calls are not yet proven equivalent to native combo input. Inspect the remaining input/timeline binding before choosing a gesture-to-combo protocol; do not copy the 0.2-second timer into a second VR damage system. |
| `SFXGameContent.Default__SFXCustomAction_RifleMeleeOne` in `SFXCharacterClass_Adept.pcc` | Declares separate ordinary/impact timelines and `SFXCameraMode_Melee`, plus action override and camera transition data. This reinforces separate admission, impact and recovery observations. Native camera presentation must be reconciled with tracked head orientation; seeing the weapon animation alone is insufficient. |

### Scope still open

The installed DLC tree had no same-name `SFXGame.pcc` or `SFXCharacterClass_*.pcc` replacements and no `.sfar`
archives in this inventory. This is **not** a complete mounted export/config override audit: differently named DLC
packages, merged configuration and the live character's supplied arrays may still affect routing. No conclusion
about every campaign or DLC checkpoint follows from these seven base packages.

`BioPawn.LoadCharacterClassData` was inspected but does not establish how character-class defaults are copied into
the live pawn's action arrays. That assignment, class-specific animation/effect differences, Infiltrator cloak
selection, input/timeline combo binding, victim search and procedural-sync collision/admission remain targeted
follow-ups. Executable-native internals and runtime action observation are still pending. This closes the missing
**base CDO mapping** evidence, not six-class gameplay or VR acceptance.

Do not invoke impact callbacks directly. Ask the native game to start a legal action, then observe its outcome.
Record current mode, player/weapon/class identity, native action identity, target, actual damage, action end and
recovery. A miss is a legitimate result; log it separately from rejected admission or broken dispatch.

## Proposed interaction contract

Keep three layers:

1. **Shared intent:** extend the existing swing detector/routing with `Light` and `Heavy` intent, a unique stroke ID,
   owning hand, scene generation and readiness state. The semantic melee action is configurable; it is not tied to
   a new hardcoded controller button. Preserve LE1's current behavior while adding explicit per-game capabilities.
2. **Native adapter:** ask whether this actor/context supports that request, resolve the active game-mode function
   and issue one request on the game thread. Track the native action through admission, active motion, recovery and
   interruption. Native gameplay owns targets, collisions, combos, damage, class effects, equipment bonuses and cooldown.
3. **Presentation:** show the correct native/class-specific effect and provide acknowledgement haptics. Reuse tracked
   hand/body presentation; coordinate authored root motion with the shared traversal/comfort policy. Effects never
   become the authority for impact or damage.

**Initial gesture proposal:** a deliberate ordinary stroke requests light melee; heavy melee requires an explicit
arming hold/modifier, a readiness haptic, then a deliberate stroke. A faster light swing alone must not become a heavy
attack. First prove both native actions through the existing command channel, then prove light gesture dispatch,
then add heavy arming. Keep an assisted semantic button/hold path for limited reach and testing.

The final physical binding must be tested against the existing control matrix before becoming a default: right hand
aim/fire, left-hand powers, support grip, reload object, squad menu/placement, radio gesture, omni-tool and native
interrupts already compete for controls. Use the same hand-ownership rules as these systems. No gesture can steal a
hand while it supports a gun, manipulates a clip or owns a menu. Require neutral release after ownership changes;
clear arming and stroke history on tracking discontinuity, load, death, stun, cinematic entry or actor replacement.

One admitted stroke may issue **one** request. A native combo window can admit a subsequent deliberate stroke;
remaining inside the reach volume or maintaining hand velocity must not repeatedly trigger attacks. The readiness
indicator reflects native eligibility, not merely an arbitrary VR timer.

## Class and context work

Use the six base defaults above as the starting inventory, then verify the effective mounted/live action families. Record native request routing,
required weapon/stance, movement distance, action interruption and visual attachment for each. Preserve native biotic,
tech or omni-weapon effects as applicable; do not infer unobserved animation/effect behavior from a class name or the historical design table.
An engineer's or adept's action is not “complete” because a generic blade appears near the controller.

Cover grabs, takedowns and mantle melee require native contextual admission and their own intentional gesture or
assisted action. They must not arise from an ordinary swing near cover. Retain the user's no-cover-snap policy;
mandatory traversal and a legal synchronized action are separate from automatic cover acquisition.

During an admitted lunge or synchronized action, keep head orientation tracked and avoid copying native camera roll,
shake or forced look into the headset. Preserve collision, destination and scripted completion. Measure when normal
IK/weapon ownership can resume without snapping the body. A stable-screen/fade option may be necessary for authored
movement, but must be checked through both eyes and in a real headset before being called comfortable.

Authored seated dialogue/shuttle states reject melee and traversal. Ordinary physically seated play remains supported
through reach calibration; it is not a reason to disable combat in normal gameplay. Atlas/mounted actors are separate
capabilities and initially refuse unsupported Shepard gestures.

## Implementation and acceptance order

| Step | Deliverable | Required proof |
| --- | --- | --- |
| M-A | Read-only action/class/weapon/context observer and native signature checks | Agrees with normal game input in a real hostile fixture; scene changes invalidate observations. |
| M-B | Light and heavy native request adapters behind a diagnostic capability | Correct action starts once; a miss, rejected request and completed hit are distinguishable. No direct impact/damage call. |
| M-C | Shared light intent and native combo/recovery observation | One stroke/one request, legal chained strikes, no duplicate damage from long strokes or low frame rate. |
| M-D | Explicit heavy arming and class effects | All six legal class fixtures, armed/unarmed conditions where native behavior allows them, denied actions and cancellation. |
| M-E | Contextual actions and comfort | Cover/mantle cases, blocked approaches, interruption and collision; stable tracked orientation and clean return to ordinary IK/input. |
| M-F | Trilogy regression and headset acceptance | LE1 melee unchanged; unsupported LE2 capabilities remain explicit; no gesture conflicts on Quest 3 through Virtual Desktop in standing or seated play. |

Use a real hostile checkpoint for native health/shield/armor effects. A shooting range can prove weapon alignment and
replay timing, but it cannot substitute for hostile admission, AI, synchronized actions or story behavior. Acquire
legal class saves; changing only a class string is not a validated six-class fixture generator.

Offline tests should cover observable invariants: deliberate arming, one request per stroke, rearm after release,
ownership changes, tracking loss, scene-generation rejection and denied native admission. Native tests add actual
effects, bonuses, difficulty, action completion and save/reload. Headset tests add reach, accidental gestures, motion
comfort, effect placement and hand/body alignment. Keep those three evidence categories separate.
