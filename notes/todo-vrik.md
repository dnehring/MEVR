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
  - **Fixed 2026-10-22, to verify** (research/15 "Scaleform's blend modes and the doubled wheel"):
    the part left in the eyes was most likely Scaleform's multiply-blended draws (`761A1BD7`,
    `11B5823A`: DEST_COLOR/ZERO with Scaleform's DEST_ALPHA alpha factor), which the redirect
    skipped. They go to the panel now (colour + a tint texture for their effect on the world), and
    the wheel itself has a panel of its own at the hand (below).
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


## Body holsters (user, 2026-10-22; built the same day, to test)

The user: switch weapons by reaching to places on the body, so the weapon wheel isn't needed for
Shepard. Research, the native switch path and the design: research/19. Test steps:
notes/next-vr-session.md step 11.
- **Host change, needs a redeploy:** HostServices v9:
  - `hapticPulse`, an OpenXR vibration action on our action set, applied after the sync;
  - `rightGripFree`, the mapping's count of squeezes kept off RB;
  - `gamepad::kFlagRightGripFree`: a new right-grip squeeze near a slot isn't RB until it opens.

  Without the redeploy, grabs still work, but RB reaches the game and there is no buzz (both
  logged once).
- **Mod (hot reload):**
  - `core/ik/BodySlots` (geometry and the grab/holster machine, unit-tested);
  - `game/Holsters`: `SFXGameModeDefault.SwitchToWeapon(type)` / `TryHolster()` through
    ProcessEvent; quick slots from `BioEquipment`;
  - `[Holsters]` in LE1MEVR.ini.
- **Defaults:** holsters on, the weapon wheel kept (`weaponWheel = 1`), and holstering by squeeze
  and release at the gun's own slot.
- **[verify in headset]:**
  - the switch from our tick matches the number keys;
  - the slot positions and radii;
  - no RB and no switches by accident in a fight;
  - the yaw hold when looking down.
- **Open, for the user:** see "The grip holds the gun" below (the user's decisions of the same day).

## The grip holds the gun, contact haptics, eye height from the floor (user, 2026-10-22; built, to test)

The user decided: the right grip is "hold the gun"; squad weapons stay on the game's wheel (plus a
proposal); haptics as contact feedback, no visual marker, no seated preset; the eye height measured
from the floor. Research and design: research/19 sections 4-9, docs/squad-weapons-design.md. Test
steps: notes/next-vr-session.md step 11.
- **Host change, needs a redeploy:** HostServices v10 (`servicesSize`, `hapticPulseHz`,
  `hapticContinuous`, `floorStateSize`, `floor`); the pad mapping's `kFlagRightGripHolds` and RB
  source bits; `core/vr/Haptics` (the continuous buzz as refreshed chunks with an expiry);
  `core/xr/Floor` (STAGE, or LOCAL_FLOOR if listed); the test loop's chord blocked while the grips
  hold the gun with both hands.
- **Mod:** `ik::body_slots` grip-hold machine (`gripHolds`, `ReturnArmed`/`Return`, the grace,
  never mid-burst), the contact haptics model, `StandingHeight`; `game/Holsters` wires them;
  `[Holsters] gripHolds = 1`, `releaseGrace = 0.3`, `powerWheel = r3`, `eyeHeightFromFloor = 1`
  (appended to existing files).
- **RB:** ME1's RB on foot is the power wheel (hold) and the mapped power (tap); in the Mako, the
  cannon. Moved to the right stick click (its own job, a scope's zoom step, does nothing in VR),
  with guards for Halcyon's double-click recenter, its both-sticks menu and clicks while turning.
- **[verify in headset]:**
  - holding the grip through fights; the 0.3 s grace; no holster mid-burst;
  - R3 as the power wheel: hold and steer with the same stick; the mapped power's ~0.35 s delay;
    no cast on a double-click recenter (else try `powerWheel = y`);
  - the contact buzz's feel and strength; whether the frequency changes anything;
  - the standing eye height from the floor (SteamVR / Steam Link and Oculus);
  - two-handed aim, crouch and R3 while aiming; the new chord rule.
- **Open:**
  - squad weapons the VR way: built the same day (below);
  - turn the weapon wheel off once holsters feel right (it stays for squad weapons until then);
  - a waist bend moves the low slots down (the anchor follows the head).

## Squad weapons at the left hand, snapshot on a stick tap (user, 2026-10-22; built, to test)

The user approved option A of docs/squad-weapons-design.md with a UI concept, and asked for the
snapshot chord to become a quick tap of both stick clicks. Design and decisions:
docs/squad-weapons-design.md "As built"; the game's path: research/19 section 9. Test steps:
notes/next-vr-session.md step 12.
- **Host change, needs a redeploy:** HostServices v11 (`squadMenuSize`, `setSquadMenu`,
  `leftGripAway`); `gamepad::kFlagSquadMenu` (the sticks, A, B and stick clicks kept from the game
  while open and until released); `plugin/SquadPanel` (its own ImGui context, texture and swapchain,
  the "squad" quad in snapshots); Halcyon's OverlayLayers seam takes three quads; the both-sticks
  chord (`gamepad::UpdateStickChord`: tap = snapshot, hold = Halcyon's menu); L3 held back 100 ms
  (`gamepad::GateClick`); the grip chords removed from the test loop.
- **Mod (hot reload):** `core/vr/SquadMenu` (pointing, the radial, the machine, the placement;
  unit-tested), `game/SquadWeapons` (the squad, the apply step, haptics, logs, status), `[Squad]`
  in LE1MEVR.ini (appended to existing files).
- **Decisions:** the game keeps running (`pause = 1` pauses like the wheel); opening at nobody needs
  the grip held 0.35 s (a missed foregrip grab mustn't open it); the left grip away from the gun is
  never LB while the menu is on; recording has no controller chord (Home or the menu).
- **[verify in headset]:** the panel's place and size; the names from the class; the AI switch with
  Shepard's weapon out (animated? how long?) and the instant one with it away; the other squadmate's
  orders after `ApplyTacticalOrders`; the tap (no crouch, no menu) and the hold (menu, no snapshot);
  the crouch's 0.1 s delay.
- **Open:** voice orders (option C) and a squad status panel on the wrist (B) remain proposals; the
  game's TLK name (string refs) isn't read; Halcyon's double-click recenter can fire on two quick
  taps pressed right stick first.


## The game's own weapon wheel at the hand (user, 2026-10-22; built the same day, to test)

The user: the squad menu should be ME1's actual UI, pause and slow down like the flat game's wheel,
and show only weapons the squadmate can equip. Design: docs/squad-weapons-design.md "The game's own
wheel"; the game's side: research/19 section 10; the panel: research/15. Test steps:
notes/next-vr-session.md step 13.
- **Host change, needs a redeploy:** HostServices v12 (`wheelPanelSize`, `setWheelPanel`); the pad's
  `kFlagNativeWheel`, `kFlagWheelPressA` and wheel stick bits (`gamepad::WheelStick`); the wheel panel
  (`HalcyonOverlays`, `core/vr/WheelCrop`: cut out of the HUD panel, a quad at the hand, "wheel" in
  snapshots); Scaleform's non-"over" blend modes into the HUD panel (the doubled wheel); Halcyon fix:
  decoupled pitch no longer cuts the wheels' steering (`vr_menu.cpp` `MenuOwnsLook`, [MEVR]);
  OverlayLayers takes four quads.
- **Mod (hot reload, but needs the v12 host):** `[Squad] style = native` (default):
  `EnterWeaponWheel` / `ExitWeaponWheel` through ProcessEvent, A on release, preselect by pointing
  (the HUD's `nLastAngle`), highlight ticks from the HUD's `nAngle` (`squad_menu::wheel`, unit-tested);
  new `[Squad]` keys appended (`style`, `wheelStick`, `wheelPreselect`, `wheelPanel`, `weaponWheelAt`,
  `powerWheelAt`, `wheelCrop`). `style = custom` is the drawn menu.
- **Decisions:** call the execs rather than hold LB (LB's tap would swap Shepard's weapon, and its
  hold adds 0.3 s); letting go gives the highlighted entry (an A), B closes without; the wheel is cut
  out by place (it's part of the HUD movie), measured once per session; the power wheel goes to the
  right hand.
- **[verify in headset]:** nothing of the wheel left in the eyes; the crop (measured vs the default
  guess; the squad portraits inside); A from our pad queues while paused (the `queued:` log line);
  the preselect; all twelve entries reachable; the steering gate; the power wheel at the right hand.
- **Open, for the user:** opening the wheel draws Shepard's holstered weapon (ME1 does that); a stick
  remap so the pointed squadmate's arc fills the whole stick (not built: the flat game's absolute
  directions kept); the native wheel always pauses (the custom one can run on).

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
