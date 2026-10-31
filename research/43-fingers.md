# 43: Shepard's fingers follow the controllers (2026-10-31)

Goal: in first person on foot with tracked controllers, each hand's fingers move with that controller's analog
inputs, so a squeezed grip closes the hand and a pulled trigger bends the index finger, on top of whatever the game's
animation posed. With the weapon drawn the right hand keeps the animation's hold of the gun and only the index finger
moves: it rests along the frame, off the trigger, until the trigger is squeezed, when it is back on the trigger as
animated. The left hand does the same as a free hand while it is off the gun; held on the foregrip by the support
grip ([research/19](19-body-holsters.md) sections 9-11, `game/SupportGrip`) it keeps the animated hold.

Builds on [14](14-vrik.md) (route 2: the pose written after the animation, the hand carried rigidly with everything
below it, the hand frame from the finger bases), [38](38-combat-stance.md) (`core/ik/PoseGraft`: a bone put back in
its parent's frame with its children following) and [18](18-camera-and-hands.md) (the `Camera` and `God` bones are
never written).

## The finger bones

From the skeleton the mod logs at the first arm IK (`skeleton:` lines, `LE1MEVR.log`; the player's body mesh
`BIOG_HMM_ARM_MED_R.MEDb.HMM_ARM_MEDb_MDL`, 110 bones): each wrist (`LeftWrist` 59, `RightWrist` 82) carries five
finger chains of three bones each, the base bone (the knuckle, the metacarpophalangeal joint), a `1` (the middle
joint) and a `2` (the tip segment):

| Finger | Left (parent) | Right (parent) |
|---|---|---|
| thumb | `LeftThumbFinger` 60 (59), `LeftThumbFinger1` 61 (60), `LeftThumbFinger2` 62 (61) | `RightThumbFinger` 83 (82), `RightThumbFinger1` 84, `RightThumbFinger2` 85 |
| index | `LeftIndexFinger` 66, `LeftIndexFinger1` 67, `LeftIndexFinger2` 68 | `RightIndexFinger` 89, `RightIndexFinger1` 90, `RightIndexFinger2` 91 |
| middle | `LeftMiddleFinger` 69, `LeftMiddleFinger1` 70, `LeftMiddleFinger2` 71 | `RightMiddleFinger` 92, `RightMiddleFinger1` 93, `RightMiddleFinger2` 94 |
| ring | `LeftRingFinger` 72, `LeftRingFinger1` 73, `LeftRingFinger2` 74 | `RightRingFinger` 95, `RightRingFinger1` 96, `RightRingFinger2` 97 |
| little | `LeftPinkFinger` 63, `LeftPinkFinger1` 64, `LeftPinkFinger2` 65 | `RightPinkFinger` 86, `RightPinkFinger1` 87, `RightPinkFinger2` 88 |

The little finger is spelled `Pink`. No metacarpal bones, no end or nub bones: the tip segment has no child, so its
own direction can't be read from a child and its hinge is taken as the finger's. Other children of the wrists:
`Prop02` under the left (59), `Prop01` under the right (82, the gun hangs on it) with `LHIK` under `Prop01` (98). None
of these is under a finger, so curling never moves the gun or the off-hand goal. `Camera` (104) and `God` (0) are off
the root, outside every hand's subtree.

`ik::fingers::FindHand` finds them by name tokens (research/14's `NameTokens`: `RightIndexFinger1` is `right | index
| finger | 1`): a finger token (thumb, index, middle, ring, pinky / pink / pinkie / little), the last digit token as the
joint (none: the knuckle; 1, 2), an ancestor chain reaching the hand bone, each joint the child of the one before (a
gap ends the chain). End, nub and tip bones are never joints but are carried as children.

## The flexion axis, measured, not assumed

Each finger bends toward the palm about a hinge that lies in the palm plane, across the finger. The hinge is measured
from the skeleton's own pose in the hand bone's frame (`ik::fingers::MeasureAxes`), the same way research/14's
`GunUpInHand` read the thumb side from the finger bases:

1. **The palm plane** is spanned by the knuckle line, index knuckle to little-finger knuckle (`across`), and the
   middle finger's direction, its knuckle to its first joint (`direction`). Their cross product is the plane's normal,
   sign still open. (The knuckles must not lie along the fingers: a 0.2 sine floor.)
2. **The palm side** is where the thumb's base sits: the thumb's knuckle is displaced off the knuckle plane toward the
   palm by about 4 units on this rig (left hand: thumb y -24.7 against the knuckles' -28.5 to -29.2 in component space
   with the arm hanging, palm toward the body, +Y for the left hand; right hand: 25.7 against 28.7 to 30.2, palm -Y).
   Without a thumb the rest pose's own slight curl decides: each finger's second segment bends off its first toward the
   palm (left index: the first segment's across-the-palm component is 0.6 of 3.9 units, the second's 0.9 of 2.3). Both
   are logged (`palm normal ... from the thumb's base; the rest pose's own curl agrees on N finger(s)`): on this rig all
   four fingers agree with the thumb. With neither, the hand's fingers stay animated.
3. **Each finger's axis** is its own direction (knuckle to first joint) crossed with the palm normal: a positive turn
   about it (right-handed, as `ik::Rotate` applies rotations) carries the finger toward the palm. For the left hand
   above that is +X of the component frame (fingers along -Z, palm +Y). **The thumb's axis** turns its direction toward
   the middle finger's first joint, so a positive turn closes it across the palm.

The axes are kept in the hand bone's frame and measured once per skeleton asset, on the first tick the fingers are
driven, before anything of ours is in the fingers (the hand is written by arm IK every frame, but the knuckles are
rigid children of the wrist, so the knuckle line and the thumb's base are the animation's whatever the arm did; only a
finger already curled by us would tilt its direction, and a curl about the hinge leaves the hinge where it is).

## The curl

`ik::fingers::Apply` turns each joint of a finger by `curl x fingerCurlDeg` about the axis through the joint's own
origin (the knuckle stays put; `R' = Q(axis) x R`, carried out in the joint's local frame so the parent-first
recomposition carries every bone below the joint, chain or not), the thumb by 0.35 of that. Three joints at 70 degrees
make a fist from a relaxed hand; the real hand's knuckle, middle and tip joints close about 90, 100 and 70 degrees, and
the animation already holds the fingers slightly curled, so 70 per joint is the starting value (`[Hands]
fingerCurlDeg`, 0-120). A negative curl extends.

The curl is **additive to the animation's pose**: it has no reference "open" or "closed" hand. A free hand's animation
is relaxed, so a full grip closes it; the drawn right hand's fingers are already around the gun, so they are left
alone and only the index finger moves. A left hand off the gun with a rifle drawn is still animated by the off-hand
IK as if on the foregrip (research/18, `LeftArm`), so its curls add to a curled hand: a learned open and closed
reference per hand (as research/38 learns the combat arm pose) would make the curl absolute. Not done; see "In the
headset".

Rules (`game/Fingers`, each tick from arm IK's drive, `[Hands] fingers = 1`):

| Hand | Weapon | Rule |
|---|---|---|
| either | away | index = trigger; middle, ring, little = grip; thumb = grip (x 0.35 in the solver) |
| right | drawn (a verified ranged weapon hangs in the right hand, `weapon::Key()`, or arm IK aims it) | middle, ring, little and thumb untouched; index = -0.5 x (1 - trigger): half a curl extended off the trigger, back onto the animated trigger finger as the trigger closes |
| left | drawn, the support grip holds (`support_grip::Held()`) | untouched (the animated hold of the foregrip); nothing written for this hand |
| left | drawn, off the gun | as a free hand |

Only with tracked controllers (`controllers.active`, the hand's grip or aim pose valid), in first person (arm IK's
view anchor), on foot (arm IK drives). A hand without finger bones or without a measurable palm is skipped and the
status says why.

## Where it runs

The writer (`engine/BonePose`, the `UpdateSkelPose` post-hook) applies the curls to the component-space pose after
the torso, the arms and the support grip are solved and before the one journalled SpaceBases write
(`BonePose.SpaceBases`), so the fingers ride on the hand as written this frame and no new write into engine memory
is added. The bones travel in the request by copy (`Request::fingers`: the hand and up to 15 joints per hand with
their RefSkeleton names, the axes, the curls, `fullCurlDegrees`) and are checked by name on every call like the arm
chains (a mismatch skips the hand and counts a rejection, `skeleton changed (finger bone names)`). The kept bones
(`Camera`, `God`) are never under a hand; the writer's skip of them stands anyway.

Cost: per hand per frame, up to five subtree walks over the 110-bone parent array and 16 relative transforms; nothing
measurable next to the arm solve.

## Settings, logs, snapshot

- `[Hands] fingers = 1` (default on), `fingerCurlDeg = 70` in `ASI\LE1MEVR.ini` (appended to an older file as
  defaults; `game/HandCalibration`, re-read within a second).
- Logs `fingers:`: once per skeleton the bones per hand (`left hand LeftWrist(59): 5 of 5 fingers: thumb
  LeftThumbFinger(60) > ...`) and the palm and axes (`palm normal (...) in the hand bone's axes (from the thumb's base;
  the rest pose's own curl agrees on 4 finger(s), disagrees on 0); flexion axes: thumb (...), index (...)`); each hand's
  mode on change (`right hand holding the gun: the index finger off the trigger until it's squeezed`); the status on
  change (`driving: left free, right gun (70 degrees per joint at full curl)`); a heartbeat line (`fingers: left free
  (trigger 0.00, grip 0.85, 16 bones); right gun (...)`). `hands: fingers follow the controllers (70 degrees ...)` when
  the settings are read.
- Snapshot JSON `"mod"."fingers"`: `enabled`, `curlDeg`, `driving`, `status`, and per hand `mode` (off / free / gun /
  held), `why`, `trigger`, `grip`, `curl` (thumb, index, middle, ring, little; -1..1), `fingersFound`, `axes`,
  `palmFromThumb`, `bonesWritten`, `skipped`.
- Unit tests (`tests/Main.cpp` `TestFingers`): a synthetic right hand with the LE1 names, turned in component space:
  the bones found and chained (a gap ends a chain), the palm from the thumb and from the rest curl, the axes
  perpendicular to the palm, curl 0 writes nothing, curl 1 turns each joint by exactly `fingerCurlDeg` (the thumb by
  0.35 of it) about the hand-frame axis through its own origin with the end bone carried and the wrist, `Prop01`,
  `Camera` and the root untouched, the tip's height over the palm and its angle from rest grow monotonically with the
  curl, a negative curl lifts it, a curl past 1 is clamped, bad input writes nothing, and a Y-mirrored left hand gets
  the mirrored palm and axis and still closes toward its palm.

## In the headset (not verified without one)

- Whether the measured palm side is right on the real rig: a squeezed grip must close the fingers into the palm, not
  bend them backward. If backward, the thumb heuristic is wrong for this rig and the log's `palm normal ... from the
  thumb's base` line plus the rest-curl agreement count say which sign to trust; `fingerCurlDeg` negative is NOT the fix
  (it would also invert the trigger finger).
- The amount: 70 degrees per joint from the relaxed animation may over-close (fingers through the palm) or under-close;
  tune `fingerCurlDeg`.
- The drawn right hand: the index finger at -0.5 curl should lie along the frame above the trigger, not through the
  trigger guard or hyperextended; the fraction is a constant (`kIndexRest` in `game/Fingers.cpp`) to tune once seen.
- The left hand on a rifle's foregrip after the support grip lets go: its animated hold plus a grip squeeze may
  over-close (the additive curl); a learned open reference would fix it.
- Whether the explore-stance graft (research/38, off by default) and the fingers compose: the graft runs before arm IK,
  the fingers after, both relative to the hand, so they should.
- The thumb: 0.35 of the curl toward the middle finger's first joint; it may want its own axis for a natural close.
