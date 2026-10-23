# 25: The guided calibration: aim, hands and body, measured once per controller type

The user's idea (2026-10-22, PSVR 2 on SteamVR, before that Quest): a calibration screen where the
player demonstrates their arms and stance, and "grab an orb representing a pistol/rifle and have them
aim it. Where they naturally aim the weapon would be considered calibrated so we would know definitely
where the scope should be drawn for them. but also it would allow us to automatically calibrate things
like pitch of the individual controllers". The complaints it answers:

- Looking through the rifle's scope, it "wasn't really lined up very well with my eye".
- The left hand missed the foregrip grab: near-misses 15-21 cm from it in the logs.
- Switching controllers (Quest, then PSVR 2) changed how the guns sat, and nothing adapted.

Built: `core/vr/Calibration` (the math, unit-tested), `plugin/Calibration` and `plugin/CalibrationDraw`
(the sequence in the headset, host side), `game/BodyCalibration` (the mod reading and applying it),
`tools/calibration-preview` (the look, offline). No third-party code: the solve is ours. Nothing here
was tried in a headset yet: everything below is **[verify]** until step 22 of
`notes/next-vr-session.md` has been run.

## 1. What was guessed before

| What | Where | The guess |
|------|-------|-----------|
| The gun's barrel | `game/ArmIk` | the runtime's OpenXR **aim** pose (`TouchReference`'s 20.6 degree pitch without one). Its angle to the hand differs per controller and runtime: Touch through Oculus and through SteamVR, PSVR 2 Sense through SteamVR bindings |
| The hand on the controller | `[Hands] handPitch/Yaw/Roll` | one manual trim, mirrored for both hands (the user's is pitch 90: the hand model) |
| The sight against the eye | `[Sights] assist*` (research/21 section 8) | slide the gun up to 10 cm once raised to the face: corrects after the fact |
| The foregrip | `ik::MakeSupportZone` | a capsule along the gun's bore and a ball at the game's foregrip; the left hand's natural place on a controller-held rifle is elsewhere |
| Two-handed aim | `ik::CapturedSpan` | the line captured at the grab: whatever the right hand's (aim-pose) angle was then is kept |
| At the shoulder | `ik::Shouldered` | fixed: 5-35 cm below the eyes, within 30 cm sideways, -15..+40 cm ahead |
| Crouch | `[Hands] crouchBelow/standAbove` | 0.75 / 0.85 of the standing eye height |
| Arm length | `[Arms] armLengthM` | a T pose with an assumed 0.36 m shoulder width |

## 2. The sequence (about a minute)

Started from the VR hands tab (Calibrate), or from a one-time prompt the first time VR runs on foot with
controllers that have no calibration (`[Calibration] prompted` lists the keys it was shown for; either
answer, or 45 s without one, records it). It closes Halcyon's menu and runs in the world, world-locked
in the app space, laid out on the heading the player faced at step 1.

| Step | The panel says | Captured when (hold still 1 s: every pose within 2.5 cm and 4 degrees) | Recorded |
|------|----------------|------|----------|
| 1 | Stand tall, look ahead | the head level within 15 degrees | the eyes (anchor and heading); the eyes above the floor (host's STAGE / LOCAL_FLOOR) |
| 2 | Arms out to the sides | `TPose`: grips at least 1 m apart, the head between them, both within 35 cm of the head's height | the head and grips: the tab's arm-length sample (`[Arms] measure*`, `measureRequest` + 1), and the span |
| 2 | Both arms straight ahead | `ArmsForward`: both grips 45 cm or more ahead, shoulder height, 10-80 cm apart | shoulder width (across the body); arm length = (span - width) / 2 |
| 3 | Crouch down | 25 cm below standing (floor), or the head dropped 25 cm (no floor); 4 cm stillness | the crouched eye height |
| 4 | Take the pistol | the right grip squeezed (0.6) within 14 cm of the orange orb | |
| 4 | Aim the pistol | `OnTarget`: the head within 20 degrees of the orange target, the right grip within 18 cm of the line of sight and 12 cm ahead | three samples (right grip, head, both eyes, target); solved |
| 5 | Take the rifle | right grip at the near orb, left grip at the far one | |
| 5 | Shoulder the rifle and aim | the head on the target, the LEFT grip near the line of sight (25 cm) and 25 cm ahead, the right grip within 45 cm of the eyes | three samples with the left grip; solved |
| 6 | Check your aim | (buttons) | |

- **No button presses for captures**: nothing competes with the trigger and grip assignments. A
  haptic tick and a flash of the panel's frame mark each capture.
- **Why "the hand on the line of sight", not "the gun points within 25 degrees"** (the brief's
  wording): before the calibration the gun's direction is exactly what isn't known; the aim pose can be
  30+ degrees off on some controllers. A hand held on the eye-to-target line while the head looks at
  the target is the aim, whatever the controller's axes.
- **The three aims must agree**: their trims' largest angle from the mean must stay within 3 degrees,
  else "Those three aims were N degrees apart: once more, please" and the step starts over.
- **Targets**: centre, upper left, lower right on a board 5 m ahead (-1.5/+1.35 m: about 17 degrees
  across and 15 up or down). The orange one is current; taken ones fill.
- **Controls**: B goes back a step (in an aiming step, the last target taken); both sticks held cancels
  (instead of opening Halcyon's menu while it runs); the panel's buttons answer a controller's aim ray and
  its trigger (core/vr/MenuPointer): SKIP and REDO THIS STEP; at the check SAVE, SWITCH EYE, REDO BODY /
  PISTOL / RIFLE (a redo returns to the check). The panel sits ahead for the body steps and to the lower
  left (27 degrees left, 23 down) for the aiming ones, clear of the targets.
- **The look** (`plugin/CalibrationDraw`, on its own so the coming main menu can re-skin it) follows the
  user's menu mockup: background #08101A, surfaces #0E1926 / #132235, text #E6F2FB / #9DB3C6, hairlines
  rgba(140,200,240,0.18), cyan #7CCBF2 for done, orange #FF8F3F for now; chamfered corners with orange
  strokes on two opposite corners; a CALIBRATION kicker and a step chip, a six-segment progress bar
  (Height, Arms, Crouch, Pistol, Rifle, Check), the big uppercase instruction, one line of explanation,
  three target chips, a large hold-still ring, the button hints. The mockup's fonts (Rajdhani, Titillium
  Web; SIL OFL 1.1) are bundled in `third_party/fonts` (downloaded from google/fonts with the user's OK,
  2026-10-22) and deployed to `ASI\fonts`; without them Windows' Bahnschrift stands in. The preview takes
  the folder as its second argument (`mevr-calibration-preview <out> third_party/fonts`).
- **The check**: squeezing the right grip shows a red dot where the new pistol sight line (from the
  hand) meets the board; both grips, the rifle's. It should sit on what the player aims at.
- **While it runs** (and Halcyon's menu is closed): the game sees a neutral pad; the host publishes the
  controllers hidden (`calibration::Hidden`), so everything in the mod that follows them (holsters,
  grenades, melee, the squad trigger, the support grip, arm IK's hands, hand aim) stands down as on a
  tracking loss; HostServices v15 says `kCalibrating`, which also stops the physical crouch. Halcyon's
  menu (both sticks held) pauses it and gives the controllers back. A recenter lays it out again.
- **Its quads replace ours**: board, orbs, dot, panel (at most 5 of the 6 MEVR slots); the HUD panel,
  reticle, sights, squad menu and wheel aren't submitted meanwhile.

## 3. The solve (`vr::calibration::Solve`)

For each aim sample (tracking space: Halcyon's app space, meters, OpenXR axes):

- **Sight line** `d = normalize(target - eye)`, from the aiming eye.
- **Gun frame** `G`: -Z along `d`, +Y the world's up made perpendicular to it (no cant: the orb has
  none to measure).
- **Trim** `T_i = grip^-1 * G`, with `grip` the right controller's GRIP orientation (not the aim pose:
  the point is to stop depending on it). The stance's trim is the sign-aligned quaternion mean; the
  spread the largest angle of one `T_i` from it.
- **Where the sight belongs**: the line's point nearest the grip's origin, in the grip's frame, averaged
  (`line`); and how far ahead of the eye the grip is (`eyeRelief`).
- **The aiming eye**: the one whose eye-to-target line passes closest sideways (perpendicular to the
  line and to up) over the grip, on average; `[Calibration] eye = left | right` or Switch eye forces it.
- **Rifle extras**, in the gun frame with the right grip as origin: the left grip's position
  (`foregrip`, along and around the bore), its orientation (`leftTrim`), the unit right-to-left line
  (`handsLine`, with its spread); and the **stock**: the right grip from the eyes' centre in the head's
  level frame.

Unit tests (`TestCalibration` in `tests/Main.cpp`): synthetic aims from a known trim (35 degrees of
pitch, a little yaw and roll), a sight line 6.5 cm above and 1.2 cm beside the grip, eye offsets of
3.2 cm, 0.3 degrees and 2 mm of noise: the trim comes back within 0.6 degrees, the line within 6 mm,
the right eye is chosen (and a left-eyed player's left), a 6-degree outlier trips the spread, the rifle's
foregrip comes back within 8 mm and its left trim within 0.8 degrees; plus the stillness test, the step
criteria, the layout, the crouch thresholds, the shoulder zone and the ini text.

## 4. Where it goes: `[Calibration]` in `ASI\LE1MEVR.ini`

```
[Calibration]
use = 1                 ; 0: the mod ignores it (everything as before)
eye = auto              ; the next calibration's aiming eye: auto (measured), left, right
last = <key>            ; the last saved (an older host that can't say which controllers: this one)
prompted = <key>, ...   ; the first-run prompt was shown for these

[Calibration.<key>]     ; one per controller type, written whole by each save
profile, system, label  ; /interaction_profiles/oculus/touch_controller, the headset's system name
time                    ; written last: a section without it isn't a calibration
heading                 ; degrees (informational)
standEyeM, crouchEyeM, crouchDropM, tPoseSpanM, shoulderWidthM, armLengthM
eye, eyeMeasured
pistolTrim, pistolLine, pistolSpreadDeg, pistolEyeReliefM
rifleTrim, rifleLine, rifleSpreadDeg, rifleEyeReliefM
rifleForegrip, rifleLeftTrim, rifleHandsLine, rifleHandsSpreadDeg, rifleStock
```

- **The key** (`vr::calibration::ProfileKey`): the interaction profile the runtime bound (the host now
  asks `xrGetCurrentInteractionProfile` about once a second) and the headset's system name
  (`xrGetSystemProperties`), e.g. `oculus_touch_controller__meta_quest_3`. Switching to Quest and back
  keeps both. Through SteamVR, PSVR 2's Sense controllers are bound to one of the profiles we suggest
  (Touch, Index or simple), so the profile alone wouldn't tell them from a Quest over Steam Link; the
  system name does **[verify: the names SteamVR reports]**.
- Everything is relative to the body and the controllers, never per gun: any gun is placed by its own
  sight geometry, and LE2/LE3 can read the same section.

## 5. How the mod uses it (`game/BodyCalibration`, LE1)

| Result | Used by | How |
|--------|---------|-----|
| `pistolTrim` / `rifleTrim` | `game/ArmIk` | the held gun's barrel and top follow `grip * trim` (the pistol stance for pistols, the rifle stance for the shotgun, assault rifle and sniper rifle) instead of the aim pose; `[Hands] gunRoll` still rolls it on top. `game/HandAim`'s controller fallback and `game/Sights`' gun top follow it too |
| `pistolLine` / `rifleLine` | `game/ArmIk` (`StanceShift`) | the gun's own sight (its `[Sights]` place, or the type's typical one, `sights::SightInHand`) is moved across onto the calibrated line through `Request::gunShift`, at most 20 cm; the alignment assist adds the last centimetre on top. Computed from the pose being written, not measured from the drawn gun, so no lag or feedback |
| `rifleForegrip` | `game/SupportGrip` | a left grip within the grab radius of the calibrated foregrip point (tracking space, from the right grip now) takes the gun, besides the capsule along the gun (which keeps its generosity) |
| `rifleForegrip`, `rifleLeftTrim` | `game/ArmIk` (`CalibrateSupport`), `engine/BonePose` | the left wrist from the right at the calibrated stance, in the right hand bone's axes, is the span two-hand aim keeps for a hold that begins within 8 cm of that point (`Support::useCalibratedSpan`, instead of the grab's own line); the left hand takes the calibrated rotation on the gun when the animation offers none |
| `rifleStock` | `game/SupportGrip` | "at the shoulder" is a zone around it (`ik::ShoulderZoneAround`: 12 cm either way in drop and forward, forward 5 cm more, sideways at least 20 cm) |
| `standEyeM`, `crouchEyeM` | `game/Posture` | crouch below the midpoint between them, stand above 35% of the way down (fractions of the standing height, applied to the standing height learned from the floor, so a slightly different floor doesn't shift them) |
| `standEyeM` | `game/Holsters` | the slots scale with it (before: the floor's learned standing height, or `eyeHeightM`) |
| `armLengthM` | `game/ArmIk` | the reach scale's arm length (before: `[Arms] armLengthM`, from the same T pose with a 0.36 m shoulder width) |
| `*EyeReliefM`, `rifleHandsLine`, spreads | logs, the tab | recorded, not applied: the scope's eye relief stays `[Sights] eyeReliefM` |

Without a section for the current controllers, or with `use = 0`, none of these paths runs: the
behaviour is exactly the previous one.

## 6. HostServices v15

`calibrationStateSize` + `calibration(HostState*)`: `kCalibrating`, the current section key, and a
count of saves (the mod re-reads at once instead of waiting for the file's next check). A v14 mod on a
v15 host still stands down (the controllers are hidden), but doesn't apply anything; a v15 mod on an
older host reads `[Calibration] last`.

## 7. Seeing it without a headset

- `build\Release\mevr-calibration-preview.exe <folder>`: the panel's screens, the board, the orbs and
  the dot, and eye views composited as the runtime shows them (core/vr/LayerComposite), laid out by the
  same functions the host uses. Checked: the text is legible at 31-50 px on a 1024 px canvas (about 1
  degree per line at 1.4 m), the panel at the lower left clears the targets, the targets are 5 degrees
  across with a 1.6-degree "AIM HERE".
- Snapshots: the quads are `calibration`, `calibration-board`, `calibration-orb(-held)`,
  `calibration-dot`; the JSON's `"calibration"` has the step, the target, the samples, the stances and
  the anchor, and the mod's status its `"calibration"` (applied or not).

## 8. Risks and open questions

- **Untested in a headset.** The step criteria's thresholds (stillness 2.5 cm / 4 degrees, the T pose,
  the crouch's 25 cm) are guesses; the logs (`calibration:`) say what each capture saw.
- **The orb in the hand** is drawn small and cyan once held (5 cm), so it isn't a front sight to line up
  on; a player who still lines it up gets a sight line through it, which the check step shows.
- **The sight line moves Shepard's hand** (gunShift moves the wrist): with a big difference between the
  player's line and the gun's sight height, the virtual hand sits up to 20 cm off the controller.
- **Reach scaling** happens after the shift and moves an extended pistol along the arm (roughly along the
  line); at the face (rifles) the hands are 1:1.
- **Through SteamVR**, the grip pose's definition for PSVR 2 Sense controllers is SteamVR's; whatever it
  is, it is the same pose at calibration and in play, which is all the trim needs.
- **A recenter** during the sequence re-lays it out; the aims taken stay good (each sample is
  self-consistent); without a floor the body starts over (the standing height was in the old space).
