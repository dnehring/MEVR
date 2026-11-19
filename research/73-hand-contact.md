<!-- SPDX-License-Identifier: MIT -->
# 73. Hand contact with other characters

The owner, 2026-11-18 (ME2 in the headset): "I want to have my hand collide with another NPC or companion ... build out the
basics of a collision system for all of the games so that I can't have my hand go through other characters." Asked how it
should behave, they chose: **the drawn hand stops at the person's body and slides along it while the controller goes on, with
a haptic pulse on contact; nobody is pushed** (the other choices were "also nudge them" and "buzz only"). Half-Life: Alyx and
Boneworks-lite games do the same for hands against the world.

Built 2026-11-18, every game, on by default (`[HandContact] on = 1`). Not yet seen in a game or the headset ([verify] below).

## 1. What it does

- Each mod tick in which arm IK drives the hands first person (`game/ArmIk` `Drive`, `targets.anchor == View`), the people
  near the player become capsules: every other visible `BioPawn` (`Engine.WorldInfo.PawnList`, `Engine.Pawn.NextPawn`,
  `Engine.Actor.bHidden`; the walk stops at the first dead entry, as `LivePawns` does) within `[HandContact] rangeM` (2.5 m)
  of Shepard's `Location`, nearest first, at most `maxPeople` (8). Squadmates, NPCs and enemies alike; corpses too (a body is
  solid). The player's own pawn and vehicles (not BioPawns) are left out.
- A person's capsules come from their own skeleton (`engine::skeleton::ReadRefSkeleton`, cached per mesh asset and dropped at
  a new world) by bone name (`ik::hand_contact::PartsFor`, rig-agnostic tokens through `ik::NameTokens`): the spine segments
  (radius 13 cm), the neck (6 cm) and the skull above the head bone (10 cm, 12 cm long), each collar to its shoulder (7 cm),
  upper arms (6 cm), forearms (5 cm), the hands past the wrist (4.5 cm, 9 cm long), thighs (8.5 cm) and shins (6 cm).
  Fingers, the face, props, IK and camera helpers, twist bones and the root are never colliders; a twist bone between two
  joints is passed over to its parent. ME1's human rig (and ME2's, the same: research/le2-vrik.md) gives 12 parts a side
  (tests/HandContactTests.cpp). Each tick their `SpaceBases` go to the world through their `LocalToWorld` and into arm IK's
  request in its component space (`engine::bone_pose::Request::contact`).
- The writer (`engine/BonePose` `ApplyContact`, between the gun kick and `PoseArm`, after the body scale's unscale of goals and
  capsules alike) pushes each hand's final goal out of them: a sphere of `handRadiusM` (4.5 cm) at the palm (halfway from
  the hand bone to the middle finger's knuckle as the animation holds the hand, turned with the goal's rotation; the wrist when
  the fingers module has no bones), out along the line from the nearest capsule axis, four passes for capsules that overlap
  (`ik::hand_contact::PushOut`). A hand pushed past a capsule's axis keeps the side it came from (the point it was pushed to
  last call), so pushing a hand through someone's chest leaves it on the chest's front instead of popping out of their back.
  A left hand the support grip holds on the gun moves by as much as the right (the gun is one thing).
- The controller is untouched: the aim ray, the shots, the holsters and every gesture still follow it. Only the drawn hand
  (and the gun in it) stops.
- The hand that touches buzzes (`game/HandContact` `Tick`, after holsters' tick, with the host's `hapticPulseHz`,
  `hapticContinuous`): a 25 ms click at 180 Hz on contact, then a 90 Hz hum while pressed, from 0.15 to 0.6 of full as the
  controller goes in up to two hand radii (`ik::hand_contact::PressAmplitude`), stopped on release.

## 2. Settings, logs, tools

- `[HandContact]` in `ASI\<GAME>MEVR.ini`, appended once with comments: `on` (1), `handRadiusM` (0.045), `rangeM` (2.5),
  `maxPeople` (8), `haptic` (1). Re-read once a second.
- Log `hand contact:` (the settings, and every heartbeat: who's near, capsules, each hand's touches, deepest and how far the
  hand was held off, at which part).
- Snapshot `"mod"."handContact"` (status, people, capsules, each hand's touching, depthM, movedM, part).
- Console `mevr contact [on|off|auto|people]` on all three games (`people`: each nearby pawn's parts and capsules, and each
  hand's contact now).

## 3. Not done, [verify]

1. **Nothing run in a game yet.** First check on the fake headset: a hand replay (`tools/make-walk-recording.py --hands view`)
   standing next to a squadmate, `mevr contact people` lists them with 12+ capsules a side, and a snapshot with a hand pushed
   into them shows `mod.handContact.hands[*].touching` and the drawn wrist off its controller by `movedM`.
2. **Other rigs' bone names** (turians, krogan, asari, salarians, geth, the Collectors, ME3's reapers' troops): only ME1's
   human names are tested. A rig whose names the tokens miss gets fewer (or no) capsules: `mevr contact people` shows it.
   Radii are an adult human's; a krogan's hump and an elcor are far bigger.
3. **Stale poses**: a person whose mesh hasn't been posed lately (off screen) keeps last frame's bones. Near the player they
   are on screen, but a person just behind the player may lag.
4. **Hands against the player's own body, and against the world** (walls, crates): not part of this. The same `PushOut`
   would take world geometry as boxes or traces later.
5. **Who's in reach**: Shepard's `Location` within 2.5 m; a very tall or long pawn (the Thresher Maw, the Mako) is out of scope.
6. **Headset**: whether the hand stopping feels right (sliding along a body, no snap), whether the hum is too strong, and
   whether holding the gun into someone looks wrong (the gun stops too; the aim still follows the controller).
