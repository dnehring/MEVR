<!-- SPDX-License-Identifier: MIT -->
# ME3 melee: shared interaction, native action adapter

Source audit, 2026-11-15, baseline `7a3a1158`. Companion to the [ME3 plan](../docs/le3-port-implementation-plan.md).
No ME3 melee action was executed. Native function declarations below are investigation leads, not verified calls.

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
Only declarations were inspected in this pass; the next task checks narrowly selected original-game implementations.

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

Inventory the six single-player class action families from mounted original packages. Record native request routing,
required weapon/stance, movement distance, action interruption and visual attachment for each. Preserve native biotic,
tech or omni-weapon effects as applicable; do not invent the missing class mapping from the historical design table.
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
