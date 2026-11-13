<!-- SPDX-License-Identifier: MIT -->
# 71. Halcyon as a liability; the game's camera shakes in VR

Two questions from the owner on 2026-11-12:

1. How much of a liability is Halcyon's renderer for the project, and should we have our own (section 1)?
2. Two player reports: the view shakes when firing (section 2), and sprinting puts the run's jog on the view (section 3).

This note is written as an implementation guide. **[verify]** marks what has not been seen in the running game. Follow
CLAUDE.md: engine access only through `src/ue3/`, a `engine/ReflectCheck` row for every resolved name, a
`write_journal::Wrote` after every write into engine memory, nothing that runs UnrealScript off the game thread.

## 1. Halcyon: how much is still Halcyon's, and where the bugs come from

### 1.1 Measured (2026-11-12, main at 5b30e5b7)

| What | Number |
|------|--------|
| Halcyon's code, `src/halcyon/le1` | about 34,000 lines (`d3d_capture.cpp` 8k, `xr_session.cpp` 6.7k, `head_aim.cpp`, `render_hook.cpp`, `vr_config.cpp`, `vr_menu.cpp`, `pchud.cpp`, ...) |
| Our code (`src`, without `src/halcyon`) | about 169,000 lines, five times as much |
| Our changes to its files since the import | about +3,000 / -160 lines |
| `[MEVR]` seams in its files | 438, **209 of them in `xr_session.cpp`** |
| Commits that touch `src/halcyon` | 102 in total, 53 in the last two weeks |
| Gotchas (`docs/knowledge/gotchas`) that involve Halcyon | 8 of 27 |

Commands that reproduce the numbers:

```bash
git ls-files src/halcyon/le1 | xargs wc -l | tail -1
git grep -c "\[MEVR\]" -- src/halcyon | sort -t: -k2 -n
git log --oneline -- src/halcyon | wc -l
git log --since=2026-10-29 --oneline -- src/halcyon | wc -l
```

What we no longer use of Halcyon's own: its menu (ours, research/28), its HUD (our HUD panel and reticle, research/15),
its stereo convergence (metric stereo, research/30), its two renders a frame (one-Draw stereo, research/37), its cinematic
handling (the 3D screen, research/52), the front end (research/56), colour, upscaling, the controllers, the pacing. Halcyon
is no longer an upstream we track: it is our fork.

### 1.2 Where the bugs come from

Not careless code: two of its design decisions conflict with what we build.

1. **The presentation decision is made from the game's mode.** `xr_session.cpp` decides each frame between the world, the
   flat panel, the cinema and the menu from the game-mode byte and the camera's name. Every feature of ours that keeps the
   player in the world where the game wouldn't (the game's screens in the room, research/53; the galaxy map on the table,
   research/51; first-person conversations and moments, research/59 and 69; the omni-tool, research/57) has to override
   that decision through a seam. That is where "it fell to the flat panel for a frame", "the 3D screen flashed" and "the
   galaxy map went full screen" come from, and why half the seams are in that one file.
2. **Its first person.** It owns a table of camera modes (`FpOwnAndWrite`), hides the head and body itself, and draws the
   eye 0.43 m behind the game's camera (the "view back" framing). From it: the body gone on far conversation shots (its
   unreadable-mode branch cleared the hides), third person after 16 loads (the table full: `FpPruneDeadOwned`), the body
   culled at the Council's podium (gotchas/culling-camera-ahead-of-the-eye), heads looking past the player (research/69
   "Heads at the drawn eyes"), the camera at Shepard's knees (game/CameraCollision).

Further gotchas with Halcyon at their root: ProcessEvent from the Present thread (research/36), the C-locale ini readers,
the shared view state that made motion blur smear one eye (research/35, 62), the swapchain budget (gotchas/openxr-swapchain-limit).

The quiet part, about 60% of its lines and the hardest to rewrite: the D3D11 capture and Present hooks
(`d3d_capture.cpp`, `dxgi_proxy.cpp`), the render hooks (`render_hook.cpp`), the OpenXR session and swapchains, its config
and logger.

### 1.3 Recommendation: own it piece by piece, no rewrite now

- Drop "touch it as little as possible" for the two areas above; we maintain them already.
- **Step A, presentation:** one decision function of ours in `core/vr` (unit-tested): the game mode plus every feature's
  claim (world UI, galaxy, conversation world, moment, cine screen, front end, menu) in, one of world / panel / screen /
  menu out. Halcyon's decision code is deleted, not patched around. Removes the largest class of bugs; mostly logic, so it
  is tested offline.
- **Step B, the first-person view:** the eyes placed by us from the head and the body, no offset behind the game's camera;
  the head and body hidden by us; the culling camera on the eye.
- **Leave the session, swapchains and D3D hooks** until LE2/LE3 need them: one VR system for the trilogy means bringing in
  Halcyon's ME2/ME3 code or our own; that is the decision for LE2, not now.
- Each step is days, not weeks, but changes what every scene shows: a headset pass each, never right before a release.
- The licence doesn't change: Halcyon's files are GPL-3.0, as the project already is.

Not done yet: classifying the last weeks' bug-fix commits by source, which would give each area's share of the bugs.

## 2. The view shakes when firing

### 2.1 How LE1 shakes the view (from the class layouts; the script itself not read)

LE1 has **two** separate shake systems, and a shot can use both:

| System | Data | How it reaches the view |
|--------|------|-------------------------|
| **Screen shake** | `SFXGame.SFXCameraShakeBase.TheShake`, a `ScreenShakeStruct`: `RotAmplitude`, `RotFrequency`, `LocAmplitude`, `LocFrequency` (vectors), `FOVAmplitude`, `TimeDuration`, `ShakeName` | `SFXPlayerCamera.AddScreenShake(shake)` puts a copy in `SFXCameraModifier_ScreenShake.Shakes`; the modifier's `ModifyCamera` adds the sine offsets to the POV's rotation, place and FOV each camera update |
| **Body camera shake** | `SFXGame.BioCameraShake` (`m_intensity`, `m_duration`, fades, `Type`: Grenade, Rumble, Ambient, **Run**, FireLoop, **Rifle, Pistol, Sniper, Shotgun**, Biotics) | played by the player's AnimTree nodes `CameraShake1 > CameraShake2 > CameraShake3` (`BioAnimNodeCameraShake`, at the very top of `BIOG_Player_O`, research/38 section A), through the pawn's `BioActorBehavior.m_cameraShakeInterface` (`BioPawnCameraShakeInterface`, `ComputeCameraRotationDelta()` returns a rotator); a camera mode applies it only while its **`SFXCameraMode.bIsCameraShakeEnabled`** is set **[verify]** |

The shot's screen shakes are on the weapon's stats object: `BioWeaponRanged.Stats` (`SFXWeaponStatsBase`)
`.FiringShake` and `.TightAimFiringShake` (research/24 section 2: `SFXPawn_Player.OnWeaponFired` plays them with the recoil
and the rumble). The Mako has its own: `BioVehicleWheeled.JumpShake`, `BoostShake`. Animations can also shake through
`SFXAnimNotify_ScreenShake.CameraShake` (a heavy landing, a blast; not handled yet).

The recoil is not the shake: research/24 section 4 already keeps ControlRotation level and the gun kicks instead (section 8).

### 2.2 Built (2026-11-12): `game/CameraShake`, `[Comfort] screenShake`

- `[Comfort] screenShake` in `ASI\LE1MEVR.ini`: the share of the game's screen shake while VR is on, 0..1. **Default 0**
  (off). The menu's Comfort > Movement > **Camera shake**: Off (0), Low (0.3), Full (1).
- Each mod tick while the view is head-tracked: the held weapon's `Stats.FiringShake` and `TightAimFiringShake`, and in the
  Mako `JumpShake` and `BoostShake`, get their `TheShake.RotAmplitude`, `LocAmplitude` and `FOVAmplitude` set to the
  original times the setting. The originals are kept per object and put back when the setting is 1, VR is off, or the mod
  unloads. Shake objects freed with a level are forgotten. Scaling at the source means every shake played after that is
  smaller; one already playing keeps its copy.
- Log `camera shake: a shot's screen shake <object>: rotation (...), place (...), FOV ...; at N% in VR` once per object (the
  amplitudes are what to compare between guns); `camera shake: [Comfort] ...` when the settings change.
- Snapshot `"mod"."cameraShake"`: the settings, what is active, every scaled shake with its original amplitudes.
- Console: `mevr shake screen <0..1>`, `mevr shake view on|off`, `mevr shake status` (this session only).

### 2.3 [verify] in the game

1. With a gun drawn, `tools/console.ps1 "mevr shake status"`: the gun's shakes listed with non-zero amplitudes.
2. `mevr shake screen 1`, fire (`mevr kick` makes no real shot: fire for real or with a replay with `-Inputs`), take
   snapshots during the burst; then `mevr shake screen 0` and the same. The world should stand still in the second set.
3. If the view still shakes at 0, the shake comes from the body system (section 3's lever covers it) or from another route:
   `tools/console.ps1 "mevr calltrace 5 shake"` while firing names every function with "shake" in its path.

## 3. Sprinting puts the run's jog on the view

### 3.1 What our sprint is

Since 2026-11-06 the sprint button is a faster run (`game/Sprint`, `core/vr/Sprint`): `SFXGameModeDefault.StormOn` is
skipped, so the game's storm posture, its camera mode and its FOV change never come. The run animation simply plays faster.

### 3.2 Candidates for the jog on the view

1. **The body camera shake of type Run** (section 2.1): `BioCameraShake.Type` has a `Run` value and the AnimTree's three
   `CameraShake` nodes sit above everything, so a run shake plays while Shepard runs and is applied by the camera mode
   while `bIsCameraShakeEnabled` is set. The faster the run, the more it shows. **The most likely source** [verify].
2. **The Camera bone.** In the Standing and Crouching postures a `Filter-CameraBone` node keeps the Camera bone at the idle
   pose, but the **Storm** posture has no such filter (research/38 section A's tree). If the game's own sprint ever runs
   (`[Hands] sprint = game`, or a route other than `StormOn`: `game/Sprint` logs `the game sprinted by a route other than
   StormOn`), the Camera bone bobs with the sprint animation and Halcyon's first person, hooked on it, bobs too.
   Floor lock (research/58) replaces the eye's height on foot, so only the sideways part and the rotation would remain.
3. Body follow (research/29) moves Shepard's bones under the view, never the Camera bone: not a candidate.

### 3.3 Built (2026-11-12): `[Comfort] viewShake`

- `[Comfort] viewShake = 0` (default): while the view is head-tracked on foot, the current camera mode (and an
  interpolation's target) of Halcyon's first-person classes (Explore, ExploreStorm, Combat, CombatStorm, TightAim; never
  Vehicle, Sniper or HipAim) gets `bIsCameraShakeEnabled` cleared, journalled, as `game/CameraCollision` does for
  `bCollisionEnabled`. Put back with `viewShake = 1`, a view that isn't head-tracked, or the unload. This also removes the
  body system's weapon, grenade and biotic shakes in first person.
- The menu's Comfort > Movement > **Running sway**: on = 1, off = 0.
- Log `camera shake: <mode class> shook the first-person view with the body's camera shakes ...` at the first clear.

### 3.4 [verify] in the game, and what to do if it isn't this

1. Run, then sprint, with `mevr shake view on`, snapshots every 0.1 s (`tools/snapshot.ps1 -Count 10 -Interval 0.1`); then
   `mevr shake view off` and the same. Compare the eye's roll and sideways place between the snapshots' JSON (`head`, the
   host view) or the world images.
2. If the sway stays with `view off`: check `sprint:` in the log for the other route (candidate 2). The fix there is a
   second filter on the Camera bone in the storm posture: in `engine/bone_pose`'s pre-stage, the Camera bone's local
   transform written back to its idle pose while the posture is Storm (never write the Camera or God bone's world pose:
   CLAUDE.md). Or keep the eye's sideways place from the pawn instead of the bone (a horizontal counterpart of floor lock,
   host side: `plugin/FloorLock`).
3. Write what was found here, under this step.

## Log

- 2026-11-12: sections 1-3 written; `game/CameraShake` built (both settings off by default, menu rows); not yet run in the
  game.
