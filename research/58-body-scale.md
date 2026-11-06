<!-- SPDX-License-Identifier: MIT -->
# 58. The body's scale: Shepard at the player's size

The user (2026-11-05, after the first headset QA of 0.1.0): the rifles looked "very small and not life-sized"; then, asked
whether to size the gun or the body: "I'd like you to tackle that bigger job of the body scale. I think this will be vital
for release." This note is the investigation: what decides how big Shepard's drawn body is against the player's real one,
measured, and the design that makes them match.

## 1. What decides the size today

- **The world is life-size** (research/30): Halcyon's Stereo 2 at 100 units per metre (`leanGain = 2`, its 50 x lean gain), the
  eyes 5.9 units apart. The user's active profile is `Custom 1` in `MELEVR.ini`.
- **The view's height is Shepard's, not the player's.** In first person the view is the game camera (the explore eye,
  `constantEye`, research/38) plus the headset's movement since the recenter (`first world view: ... head at (0, 0, 0) m`), plus
  Halcyon's camera offset. The user's profile lowers it 8.2 cm and moves it 7.8 cm right (`cameraOffsetEnabled = 1`,
  `cameraOffsetUp = -0.082`, `cameraOffsetRight = 0.078`, `flatScreenDistance = 0.674`): tuned by hand against Shepard's head.
  The player's real eye height (calibrated `standEyeM = 1.840`, research/25) plays no part in where the eyes are.
- **The body stands under the view at its own size** (research/29, `core/ik/BodyFollow`): the whole body slides and turns
  under the view and the spine leans the head bone to the view's pivot. Nothing scales it.
- **The hands are reach-scaled, not placed** (`game/ArmIk`, `core/ik/ArmLength`): each wrist's offset from its shoulder is
  scaled by Shepard's arm length over the player's (calibrated `armLengthM = 0.735`, shoulder to grip), except within
  `reachNearFaceM` (0.30) of the eyes, so the player's full extension is Shepard's full extension. With Shepard's arm about
  0.55 m (upper 28.9 + lower 26.3 units, `arm IK: first write`), the user's hands are drawn at about three quarters of their
  reach: on the fake headset's poses both drawn wrists sat about 12 cm nearer the eyes than the controllers (research/57
  section 3).
- **The gun is the game's model on Shepard's hand**: the assault rifle's muzzle 39.3 units ahead of its origin (`weapon:
  barrel`), against about 55 cm on a real rifle.

So the player sees the world life-size from Shepard's eye height, and Shepard's body, hands and gun at Shepard's size, while
their real body is the player's. The user is calibrated at 1.84 m eye height (about 1.95 m tall) with a 0.735 m arm and a
1.74 m T-pose span.

## 2. Measurements (2026-11-05 23:41-23:44, fake headset, BIOA_UNC20, male Shepard HMM_ARM_MEDb, replayed poses)

`mevr body` (`game/BodyProbe`, mod only, reads only) prints Shepard's drawn key bones in tracking space and component space,
his lowest bone as the floor, the headset's height above that floor, the drawn arm segments and the shoulder width, the held
gun's length as drawn and the player's calibration.

| | Shepard, drawn | the user (calibrated, PS VR2) | ratio |
|---|---|---|---|
| eyes above the floor, holstered, standing | 1.69 m (`Eye_Left`/`Eye_Right` 1.694/1.691) | 1.84 m (`standEyeM`) | 1.089 |
| the same, assault rifle drawn | 1.53 m (the combat stance's lower head) | | |
| shoulder joint to wrist | 0.552 m (upper 0.289, lower 0.263) | | |
| grip to grip in a T pose | about 1.61 m (shoulder joints 0.374 apart, + 2 x (0.552 + the 7-unit wrist-to-grip)) | 1.736 m (`tPoseSpanM`) | 1.08 |
| the view above Shepard's floor | 1.75 m (the user's `Custom 1` profile, 8.2 cm down, included) | | |
| the assault rifle, origin to muzzle | 39.3 units | | |

Height and span agree: the user is 8-9% larger than Shepard in every direction, so one uniform scale fits. (The calibrated
`armLengthM` 0.735 isn't comparable with the drawn 0.552: it is measured from shoulders placed `shoulderWidthM` 0.266 apart,
Shepard's joints are 0.374 apart.) The Camera and God bones sit at 0.84 m (the pawn's centre), unmoved by the body.

**The experiment** (`mevr body scale <s>`, `engine/BonePose`: after every solve, every bone but the kept ones scaled by s about
the floor under Root, its own `FBoneAtom::scale` multiplied):
- **The skinning honours the scale**: at 1.15 the forearm and hand are visibly thicker and longer in the eye images, the
  joints where expected (eyes 1.69 -> 1.94 m, arms 0.552 -> 0.635 m, the toes still on the floor at 0.000).
- **Attached components inherit it**: the held assault rifle measured 39.3 -> 45.2 units origin to muzzle (x 1.150). The gun
  grows with the hand, so a body at the player's size carries a gun at the player's size.
- Camera and God stay put (the view didn't move); with the view not raised, Shepard's eyes then sat 18 cm above it and the
  chest came up to the view: **the view has to rise with the body**.

## 3. The design space

- **A. World scale** (fewer units per metre, the player becomes Shepard's size): consistent, but the world is no longer
  life-size, which research/30 was built for. Not chosen.
- **B. Body scale** (the user's ask): Shepard drawn at the player's size, `s = player's standing eye height / Shepard's`, and
  the view raised to the player's real eye height, so the world stays life-size and the body, hands and gun come with it.
  Done in `engine/BonePose`'s write as a uniform scale of every bone about the floor under the pawn (never Camera or God, the
  view hangs off them), with arm IK's and the torso's goals mapped through its inverse so they land where they are meant to
  after it; reach scaling then comes near 1. Both engine questions are answered yes (section 2).

## 4. Built (2026-11-06 00:00) and checked on the fake headset

`core/ik/BodyScale` (the factor, the scale and its inverse; `tests/BodyScaleTests.cpp`), `engine/BonePose` (`Request::bodyScale`,
`Request::eyes`: the writer measures Shepard's unscaled eyes above his floor, maps every goal through the inverse before the
solves and scales the finished pose, Camera and God kept; `UnscaledBody()`), `game/BodyScale` (`[Body] scale = auto | off |
<number>`, auto by default; the slow-falling estimates; at most 0.1 per second of change; `mevr body scale <s>|auto|off`;
heartbeat `body scale:`; snapshot `"mod"."bodyScale"`), `game/ArmIk` (fills the request on foot in first person; with the body
scaled the hands go on the controllers 1:1, the arms stretching up to `[Hands] armStretch` for the rest).

Checked (23:54-23:56, the user's `Custom 1` view, 1.75 m above the floor): auto settled at 1.048 (Shepard's standing eyes
measured 1.674 m, the view 1.755 m; his eyes then 1.76 m, at the view); both drawn wrists on their controllers to the
millimetre (7 cm behind each grip along its -Z, the designed wrist-from-grip offset; at scale 1 the reach scaling had drawn the
raised left wrist 6 cm short); the assault rifle drawn, its grip measured unchanged in the hand's axes (muzzle 49.2 units from
the wrist), the aim ray along the drawn barrel, body and gun in proportion in the eye image. `game/weapon`'s follow check
drops the bone's scale by design (diagnostics), so it reads the scaled gun as 1.9 units of error.

**The view's height decides the size.** Auto puts Shepard's eyes at the view; the view is the game's first-person camera
(about 1.83 m above his floor) plus Halcyon's camera offset, and the user's profile lowers it 8.2 cm, so auto came to 1.048
rather than the 1.089 their calibrated 1.84 m eye height asks. With `cameraOffsetUp` back near 0 the view is at about their
real eye height and auto goes to about 1.08. Next: set the view's height from the calibrated standing eye height (a host seam),
so it needs no hand tuning. [verify] in the headset: the body and hands at the player's size, the gun's feel, the sights (the
calibrated stance's shift is computed on the unscaled gun: at 1.09 about 5 mm off, the sight assist takes the last centimetre).

## 5. The plan as designed (B)

1. **s from the two eye heights**: the player's standing eye height (the calibration's `standEyeM`, else the holsters' floor
   sample, else `[Holsters] eyeHeightM`) over Shepard's, measured live from his eye bones while he stands holstered and
   unscaled (a different skeleton, Femshep, measures its own), clamped 0.85-1.25; `[Body] scale = auto | off | <number>`.
2. **The writer scales last** (`engine/BonePose`, as the experiment): about the floor point under the head, so the head goes
   straight up.
3. **The goals go through the inverse first**: arm IK's wrist targets, the torso's head goal and the gun shift are mapped by
   `P + (x - P) / s` before the solves, so after the scale they land where they were meant to (the hands on the
   controllers); reach scaling compares the player's arm with Shepard's scaled arm (it comes near 1).
4. **The view rises to the player's eye height** (host, Halcyon's first-person eye seam): the view's height above the floor
   becomes the player's standing eye height plus the headset's movement, so the world stays life-size and Shepard's scaled
   eyes are where the player's are. The user's hand-tuned `cameraOffsetUp` (-8.2 cm) then has to be revisited.
5. Gun-unit constants measured on the models (a scope's eyepiece offsets, `OpticFor`) are multiplied by s where they're used
   in the world; whatever reads the gun through the engine (muzzle, sockets, `BoneWorld`) already includes it.
6. Only on foot in first person; third person, conversations and the Mako draw Shepard as the game does.
