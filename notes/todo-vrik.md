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
  in first person. (Built 2026-10-08: see "Next" below.)
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

- **Superseded by research/18 (2026-10-21):** the camera hangs off the bone named `Camera`
  (index 104, a child of the root `God`), not the head. 489ef51 looped because it wrote every
  bone, `Camera` included. See "Camera hook guard" below.
- Halcyon builds its first-person camera from Shepard's head bone *as written*, about 44 units
  away from his animated eyes (log: `arm IK: body anchor: first person ..., view 44 units`).
  (Wrong: from the `Camera` bone, research/18.)
- So the torso must not chase the view. Moving the body toward the camera moved the camera too:
  the anchor then flipped between first and third person every frame (view 41 <-> 76-109 units),
  and the user saw heavy judder whenever they moved physically (commit 489ef51, reverted).
- Leaning toward the camera (25-35 degrees) put both shoulders under the view, and the arms seemed
  to come from the middle of the chest.
- Now (first person): the torso leans by the headset's move from where it was recentered
  (`TorsoGoal::relative`), not toward the view itself. Until 2026-10-21 the hidden head also kept
  its animated transform (`TorsoGoal::keepBone`); that's dropped (research/18: it wasn't what
  kept the camera still), and the head leans with the spine.
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

## Next: two-handed grip = focused fire (agreed with the user, 2026-10-08; built the same day, to test)

**Built** (research/14 "Two-handed grip", test steps in notes/next-vr-session.md step 8; needs a
host redeploy, HostServices v7):
- Grab with Overlord's hysteresis (new squeeze within 0.12 m, release past 0.22 m or grip open);
  LB elsewhere; no surprise LB after a release by distance.
- Focused fire: LT forced while held (not for sniper rifles by default, `sniperAims = 0`); the
  left trigger is behind `[Controls] leftTrigger = lt | none` (default `lt`, unchanged).
- Two-hand aim: Overlord's swing of the grip-to-foregrip span onto the hands' line, roll from the
  right wrist; hands closer than 8 cm let go; pistols (cup under the grip) keep the right hand's aim.
- Left hand: solved to the foregrip as written (the native LHIK runs before our right-arm write).
- Foregrip per gun: an off-hand socket if one is named so, else LHIK, else the animated left hand,
  else under the grip; logged.
- Zoom: by the code Halcyon's FOV fill replaces the game's projection each frame, so LT doesn't
  zoom [verify].
- **Open:** the left trigger's new use (a power or squad command); LE1's weapon socket names; the
  thresholds and the pistol point once seen in the headset.

The agreed plan:

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

## Aimed fire comes from posture (user, 2026-10-21)

- Aimed fire is not the left trigger ("zoom") any more. It needs both hands on the gun (the
  support grip) **and** the gun at the shoulder: the right grip 5-35 cm under the eyes, within
  30 cm to the side, -15 to +40 cm ahead (`ik::Shouldered`, 5 cm hysteresis).
- Settings, `[Controls]` in `ASI\LE1MEVR.ini`:
  - `aimAtShoulder = 1`; 0 = both hands alone are enough;
  - `leftTrigger = none` by default; `lt` puts the old LT back.
- **[verify in headset]:** the zone's bounds (cheek weld vs held out); log
  `support grip: aimed fire on/off`.

## Headset session 2026-10-21 21:06

- **Two-handed grip works.** It was taken at 0.11 m from the foregrip (the LHIK bone), two-hand
  aim swung the barrel 22 degrees, and "aimed fire on" came at the shoulder.
- **Fixed (7b80f5d):** the left grip opened the weapon wheel (LB) unless the hand was at the
  foregrip. The left grip now only takes the gun; the left trigger is the weapon wheel
  (`[Controls] leftTrigger = lb`, `leftGripAway = none`).
- **Bug: the quick menus (weapon and power wheels) are doubled.**
  - Seen: an outline of the wheel's selector stays locked to the view, while the normal
    stationary wheel shows too.
  - Cause, probably: our HUD-panel redirect sends part of the wheel's UI draws to the panel
    (`[MEVR-HUD] to the HUD panel ps=693326A9` appeared when it opened), and other draws stay in
    the eyes, head-locked.
  - Next: a snapshot (End) with the wheel open shows which part is where. Possible fixes: send
    every UI draw to the panel while a wheel is open, or none (Halcyon's head-locked HUD) while it
    is open.
- **Bug: world scale and close-up parallax are wrong.** The pistol looked massive, and the user
  went cross-eyed up close.
  - Settings in play: life-size movement on (leanGain 2, 100 units/m), stereo half-eye 3.4
    units (a 68 mm IPD at 100 units/m, life-size by Shepard's skeleton), `sfr2SwapEyes` on
    (flips the offset and the routing together), `sfr2Convergence` 0.012 (shifts the images
    inward a little), and the camera moved back 0.674 m onto Shepard's head bone.
  - Next: snapshot with the pistol held about 30 cm from the face. In correct stereo a near
    object sits further right in the left eye's image than in the right eye's; reversed means
    pseudoscopic.
  - A/B in the menu's VR tab: "Swap eyes" (sfr2SwapEyes) and the convergence (0). The research
    on the Camera bone (research/18) also bears on where the view sits relative to Shepard.


## World-locked flat panels (user, 2026-10-21; built the same day, to test)

The user: the HUD and flat panels should stay placed in space, not move with the head (the intro's
"view box" rode the head). Host change (redeploy). Test steps: notes/next-vr-session.md step 10.
- **Seam:** `MevrBridge::PlacePanel` (`mevr_bridge.h`, `src/plugin/WorldPanels.cpp`) is asked for
  the space and pose of Halcyon's flat screen and of its Insert menu each frame, given the
  head-locked pose Halcyon computed (distance, offsets, its nearest-quad pull-in) and the head.
  Off: VIEW and that pose, upstream bit for bit. On: the app space, the head-locked offset applied
  in a level frame at the head's position and yaw when the panel appeared.
- **Rules** (`core/vr/OverlayPlacement` `WorldPanel`, unit-tested): placed afresh when it appears
  (not shown for more than 5 frames), when the flat screen's kind changes (game menu, galaxy map,
  conversation, cutscene/movie), on recenter (Halcyon's R re-origins the app space; `OnRecentered`
  and a changed app space handle both re-place) and once the head is found after a blind placement;
  eases round (yaw and eye position) when the head stays turned past `panelFollowDeg` (70) for a
  second; a re-placement that would move it under 5 degrees and 5 cm keeps it, so a flickering kind
  doesn't nudge it.
- **Settings:** `MELEVR.ini` `worldLockedPanels = 1`, `panelFollowDeg = 70` ([MEVR] fields in
  Halcyon's config, like `hudPanel*`), checkbox and slider in the menu's HUD tab.
- **HUD panel:** `hudPanelFollowDeg` default 50 -> 70 (the deploy migrates a saved 50.0).
- **Pointer:** `menu_pointer::Quad` is now a pose in the tracking space (`InTracking` composes a
  VIEW-space quad with the head); `OnMenuQuad` passes the placed pose. The test loop names and
  places the menu quad by its submitted position and space.
- **[verify in headset]:** no jump at the movie -> menu and cutscene -> conversation boundaries
  beyond the intended re-place; the menu still the nearest quad on SteamVR (its pull-in is
  measured from where the head was when it opened; a menu opened from another spot or heading
  than the flat screen's can end up behind it); the pointer on the app-space menu.

## Camera hook guard and native-state logs (research/18 proposals 1-5, built 2026-10-21, to test)

Mod only (hot reload, no host redeploy). Test steps: notes/next-vr-session.md steps 8 and 9.
- **Camera hook guard:** `engine/BonePose` never writes the bones named `Camera` and `God`
  (`Request::keep`, found by `ik::FindCameraHookBones`); their SpaceBases entries stay the
  animation's bit for bit, whatever the torso and arms did. Once per skeleton arm IK logs whether
  any subtree we write contains them (it shouldn't: they hang off the root); the heartbeat warns
  if a solve ever moved one. **Rule:** any future whole-body move (489ef51's idea) must move every
  bone except `God` and `Camera` (research/14, "Camera hook bones").
- **keepBone dropped** in first person; `relative` lean kept (eyes move by the headset's own
  move). Absolute lean wasn't brought back: the view sits at the Camera bone plus Halcyon's
  per-state offset, calibrated so it's at the head bone on average, but the Camera bone's
  animation wanders against the head (−14..+44 units forward within a minute, 2026-10-08), so
  aiming the eyes at the view would make the spine follow the animation's camera track, not the
  player. **[verify in headset]:** no judder when leaning; nothing visible left behind by the head.
- **Camera mode log** (`game/CameraProbe`): class, HookName, HookOffset, Offset, LastHookPos, the
  hook bone against the game camera, and GetCameraLocation's formula against the mode's m_pov,
  on each change. **[verify]:** the formula's gap near 0 in game.
- **Native off-hand IK:** the `LeftArm` control's ControlStrength/StrengthTarget/
  bSetStrengthFromAnimNode, per gun and on each state change (off, blending, on), and in the
  heartbeat. **[verify]:** which stances weight `AddCombatIdleOffset` (research/18).
- **Aimed fire:** BioWeaponRanged.bIsZoomed on each change, with whether our grip forces LT.
  **[verify]:** bIsZoomed on a tick after `aimed fire on`.
- Not done: proposal 6 (a GetCameraLocation post-handler), only needed if a stance hooks another
  bone we write; optional `ControlStrength = 0` while gripping (saves the native solve only).
