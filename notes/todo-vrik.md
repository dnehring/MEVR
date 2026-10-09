# VRIK to-do (from the 2026-10-08 headset session)

Direction: our work is VRIK; Halcyon's core is touched only for bugs (see CLAUDE.md). Prior art:
Overlord (MW2 VR, GPL-3.0, `reference/Overlord`). Its rule: the controller decides where things
are, never the animation.

## 1. The gun is upside down (done 2026-10-08, to test: research/14 "Gun roll")

- **Cause:** `game/Weapon` takes the gun's "up" from the mesh/socket Z axis, measured in the hand's
  axes. That is unreliable across weapons. The 15:56 log shows two grip measurements, probably the
  pistol and then the rifle, with the same barrel but "up" about 90° apart:
  - `up (-0.160, 0.151, -0.975)`
  - `up (0.968, 0.214, -0.130)`
- **Fix, as Overlord does it:**
  - Don't trust model axes for roll.
  - Hold every gun at a fixed angle relative to the Touch controller's grip pose, using Overlord's
    Touch grip reference (20.6° pitch, ±7 mm, 102 mm offset, already in research/14).
  - Place the hand on the gun: the gun is the master, the hand follows. The gun's roll then always
    follows the wrist.
  - Keep the barrel and muzzle measurement (it works: shots leave the muzzle, follow error 0.01
    units).

## 2. Shepard's arms don't match the player's reach (done 2026-10-08, to test: research/14 "Reach")

- **Symptom:** with the arms fully extended, Shepard's hands don't line up with the controllers.
- **Cause:** `game/ArmIk` keeps Shepard's shoulders where the combat animation puts them (rotated,
  leaning into the gun stance) and keeps his arm length fixed (upper arm 28.9 + forearm 26.3 units).
  A target out of reach from that shoulder gives a straight arm that stops short
  (`core/ik/ArmPose`: "out of reach, the straight arm points at the target").
- **Fix, in order:**
  1. **Shoulders from the headset, not the animation.** Overlord's weapon arms are rebased at the
     native view anchor, so the shoulders sit where the player's are. For us:
     - turn and translate Shepard's spine and clavicles so his shoulders sit under the HMD;
     - use the body yaw from head and controllers, not his combat twist;
     - `core/ik/Ik.hpp` `BodyFrame` already estimates shoulders from the head (`shoulderDown`,
       `shoulderHalfWidth`).
  2. **Hands always on the controllers.** Allow arm stretch up to 1.25× like Overlord
     (`reference/Overlord/src/client/component/vr/gameplay/hands/pose_solver.hpp`,
     `max_arm_stretch_ratio`; it scales both segments by
     `clamp((distance + .001) / (upper + lower), 1, 1.25)`).
  3. **Optional: arm-length calibration.** Measure the player's arm held out straight once and
     scale reach, so the player's full extension is Shepard's full extension. Overlord doesn't do
     this; full-body VRIK mods do.
- **Also check:** the first aim ray started 2.12 m from the head (`hand aim: first aim ray
  (tracking space): muzzle (0.201, -2.141, 0.061) m`), so the body anchor and the tracking origin
  aren't lined up (`view_anchor` / `TrackingFrame`, hand targets measured from the head bone). This
  is likely part of the reach mismatch.
- **Forearm twist:** Overlord found that twist bones that are siblings of the wrist never get the
  wrist's rotation (`docs/vr-arm-twist-investigation.md`). Check LE1's forearm twist bones when the
  sleeves look wrong.

## 3. Later

- Two-handed grip (Overlord `support_grip` acquire/release hysteresis); what the zoom button does
  in first person.
- Holo sight; Overlord's lens magnifier for sniper rifles.
- Colour controls (saturation/contrast/brightness) in the eye blit, if still wanted. Depth of
  field is left on, since Halcyon found turning it off washes the colours out.
- Performance at the 3072x3188 stereo target: read the perf monitor CSV
  (`%LOCALAPPDATA%\MEVR\perf\`) from a stereo session; drop to `-Quality Low` (2048x2124) if
  needed. The 15:42 sample was GPU 53%, busiest game thread 91.5% of a core (likely CPU-bound).
- The HUD panel works in the headset; check text readability and whether fades or damage tints
  land on the panel.

## State as of this note

- **Settings:** Halcyon's settings are per profile slot. Custom 1 is the user's first-person
  profile: `fpEnabled`, `decoupledPitch`, no head aim. It's active, and every slot uses `\` for
  the menu with head aim off.
- **Menu:** the stick chord toggles the menu.
- **Stereo:** works with a borderless window at 3072x3188 (`[TARGETCHK] Stereo 2 target OK`).

## Open: Halcyon's first-person camera vs Shepard's eyes (2026-10-08 evening)

- Halcyon builds its first-person camera from Shepard's head bone *as written*, about 44 units
  away from his animated eyes (log: `arm IK: body anchor: first person ..., view 44 units`).
- So the torso must not chase the view. Moving the body toward the camera moved the camera too:
  the anchor then flipped between first and third person every frame (view 41 <-> 76-109 units),
  and the user saw heavy judder whenever they moved physically (commit 489ef51, reverted).
- Leaning toward the camera (25-35 degrees) put both shoulders under the view, and the arms seemed
  to come from the middle of the chest.
- Now (first person): the torso leans by the headset's move from where it was recentered
  (`TorsoGoal::relative`), not toward the view itself, and the hidden head keeps its animated
  transform (`TorsoGoal::keepBone`), so Halcyon's camera doesn't follow our writes.
  **[verify in headset]:** physical leans bend Shepard's torso without judder; the shoulders stay
  at the sides; no doubled view motion.
- Still open: Halcyon's camera sits about 44 units from Shepard's eyes (`[FPCAM]`: the camera mode
  offsets `fpCombat`/`fpExplore`/... fwd/right/up in `vr_config.h`, written in `head_aim.cpp`
  FpOwnAndWrite). If the arms still look offset from the player's shoulders, tune those offsets
  in MELEVR.ini so the camera sits at the eyes (a setting, not a code change).

## Verified in the headset (2026-10-08, 20:48)

- **Left-trigger camera alignment works.** It measured the camera 33.7 forward, -3.9 right and
  4.1 up (units) from Shepard's head bone. Halcyon's offsets are now: back 0.674 m, right 0.078 m,
  up -0.082 m (profile Custom 1).
- Life-size movement was turned on at 20:55 (`leanGain 2`, forward emphasis 1). How it feels isn't
  reported yet.
- **Still open:**
  - the upside-down world seen at the 20:44 launch (a 180-degree roll of the tracked head, from
    launch; retest after restarting SteamVR);
  - Quest Link performance (research/16).

## Next: two-handed grip = focused fire (agreed with the user, 2026-10-08)

1. **Grab:** the left grip squeezed with the left hand within about 10-15 cm of the gun's foregrip
   latches a support grip. Use hysteresis like Overlord's `support_grip` (acquire nearer than
   release). A grip squeezed anywhere else keeps its current mapping (LB).
2. **Focused fire:** while gripped, the game sees LT held (the aimed-fire accuracy bonus), without
   the zoom. One-handed = hip fire. The left trigger is freed from LT; for now leave it unmapped or
   mapped as today, behind a setting, until we pick a use (a power or squad command).
3. **Aim from both hands:** while gripped, the barrel points from the right hand toward the left;
   the roll stays with the right wrist.
4. **Left hand on the gun:** while gripped, stop overriding the left arm. Shepard's AnimTree
   already has a left-hand IK onto the gun (`LeftWrist` -> bone `LHIK`, research/08 H3), or put
   the left hand on the measured foregrip point.
5. **Verify first:** does holding LT change the field of view in Halcyon's VR view (zoom)? If
   yes, suppress it. Pistols: the grab point is under the grip.

