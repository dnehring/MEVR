<!-- SPDX-License-Identifier: MIT -->
# 72. The skeleton audit: what the game's animation does that our body, arms and view must handle

Started 2026-11-14 from a report relayed by the owner: an idle sway "carried through to our movement/avatar". The owner then
asked for an audit of "what could be manipulated in the skeleton in the same way, so we can avoid this type of problem in
the future". This note is that audit: how to measure, what was measured, what each source does to the player, what the mod
does about it, and what is still to trace.

## Why this matters

In first person the player's body is Shepard's skeleton posed by the game's AnimTree, then rewritten by us:
- the body follower (research/29) turns and slides `Root`'s subtree under the view (`Root` sits at hip height and carries
  the legs and the spine; `God` and `Camera` hang beside it and are never written);
- the torso solver (`core/ik/TorsoPose`) turns and leans the spine as one block about its bottom joint;
- arm IK puts the hands on the controllers, fingers on top;
- the view hangs off the `Camera` bone plus the camera mode's offsets (research/18, 38 B), floor-locked in height (58).

Whatever the animation does that these don't replace reaches the player: motion of `Root` (comes back as the feet sliding,
since the follower pins `Root` under the view), bends inside the spine and the collars (move the shoulders, so the elbows,
since the hands are pinned), motion of the `Camera` bone (the view itself), and anything on the legs.

## How to measure (repeatable)

- **The trace:** `mevr camera trace <seconds> [label]` (`game/CameraProbe`): each mod tick a CSV row in
  `%LOCALAPPDATA%\MEVR\camera-trace-<stamp>[-label].csv` with the pawn's place, yaw and velocity, the game camera's place and
  rotation, and 14 bones in the mesh component's space: `Camera`, `God`, `Root`, `Pelvis`, `LowerBack`, `Chest2`, `Neck`,
  `Head`, both collars, hips and ankles.
- **The summary:** `python tools/skeleton-audit.py [csv ...] [--last N] [--flag cm]`: per trace, the camera against the
  pawn, `Root` in the mesh's space, every other bone against `Root`, peak-to-peak in cm (100 units a metre), marked over
  3 cm.
- **The run:** `tools\fake-headset-session.ps1 -Tag skeleton -Run tools\skeleton-audit.ps1`: the Presidium (John02 slot 89),
  god mode, idle twice, hits, a run by a replayed stick, standing after it.
- **In the headset:** with the owner playing a build with `[Debug] requests = 1`, `tools\console.ps1 "mevr camera trace 15
  <label>"` from here while the owner does the action (draw, crouch, cast), then the summary.
- **Read with the arms on:** without tracked controllers (the fake headset's own run) arm IK and the body follower don't
  run, so a trace shows the game's animation alone: the input our pipeline must handle. With controllers (a replay with
  hands, or the headset) it shows what the player gets.

## What was measured (2026-11-14, the Presidium, fake headset, no controllers)

| State | Camera vs pawn | `Root` in the mesh's space | Against `Root` | What reaches the player in first person |
|---|---|---|---|---|
| Idle, holstered (3 traces) | 0.0 cm, 0.0 deg | 2.1-2.6 cm side to side, 0.6-0.9 forward, 0.2 up | pelvis 0.0; spine, neck, collars under 1 cm; head 1.9 | the feet slide 2-3 cm the other way (the follower pins `Root`) |
| Idle fidget (one in three traces) | 0.0 | **15.8 cm** side to side | chest, neck, collars 2.8-3.7 cm | the feet slide 16 cm; the shoulders shift 3 cm |
| Hits (`mevr cover hit 1 0`, god mode) | 0.0 | as idle | as idle | none seen: no reaction played [verify without god mode] |
| Running (stick 0.9, 389 u/s) | (the camera lags the pawn's update: read the bones) | 4 cm X, 3 Y, **9 cm up** | **chest, neck, collars 13-16 cm forward and back**, 3-4 side, 2-3 up; ankles the stride | the shoulders (so the elbows) bob 13-16 cm with the run's lean; the view doesn't (the Camera bone is filtered) |

Facts behind the table:
- `Root` rests at the mesh's origin horizontally (idle mean (0.6, 0.7) cm, at 101 cm height), and `Pelvis` reads exactly as
  `Root`: on this skeleton the hips move only with `Root`.
- The feet stay planted in the mesh's space while idle (the ankles move 0.1 cm), so `Root`'s idle motion is a weight shift
  over still feet.
- The `Camera` bone and the game camera did not move in any state (the explore posture's `Filter-CameraBone`, research/38 A).

**A wrong first reading, corrected the same day:** the first trace had no `Root` column and read the pelvis's 5.7 cm in the
mesh's space as the pelvis moving against `Root`. The first fix built on it (hold the pelvis over the feet) would have put
the sway back into the hips. Always trace `Root` and read bones against it.

## Every source in the player's AnimTree, and where it stands

From research/38 A's tree (`BIOG_Player_O`) and the skeletal controls (research/18, 59):

| Source | What it moves | Measured | Reaches the player | The mod |
|---|---|---|---|---|
| `AddIdleOffset`, `AddIdleLoop` (idle weight shift, fidgets) | `Root` 2-16 cm | yes | feet slide | `[Hands] steadyIdle` plants the feet (below), off until the headset check |
| `CameraShake1-3` (types Run, Rifle...) | the camera | (research/71) | the view shakes | `[Comfort] viewShake = 0` (default) clears them in first person |
| NonCombat run, walk, leans, starts, stops, skids | spine lean and bob, `Root` up 9 cm | run: yes | shoulders and elbows bob while running | `startStopAnims = 0` (research/38) removes starts and stops; the run's lean not handled |
| `AddDamage0/1` (hit reactions) | the upper body | no reaction seen (god mode) | [verify] | none |
| `AddEyeballNoise`, `AddEyeBlinkNoise` | eyes | no | no (the head is hidden) | none needed |
| `CombatMode` > `AddCombatIdleOffset` (combat idle) | the whole body | not reached (no gun drawn without controllers) | [trace in the headset] | `exploreStance` (off; research/38 A) |
| `StandingGunAiming` (`BioAnimNodeBlendByAim`) | the spine toward the game's aim | not reached | shoulders turned toward the game's aim, not the hand's [trace] | none |
| `StandingAimTurn`, `CombatWalking/Running` | turn-in-place steps, the legs | not reached | the feet step under a still player [trace] | body follow's leg rule (research/29) |
| `WeaponEquip` (draw, holster), `Power` (casts), `CombatActionList` (fire, grenade) | arms and spine (additives) | not reached | the spine and collars during draws, casts, shots [trace] | `drawRate = 6` (shorter); arms replaced by IK |
| Crouching posture | the whole body | not reached | [trace] | physical crouch (research/23) |
| Storm posture (the game's sprint) | the Camera bone (no filter), `AddStormCamera` | not reached (our sprint skips it) | the view bobs [research/71 3.2] | `game/Sprint` skips `StormOn` |
| `Actions`: Gestures, Matinee, GetUp, Fall, Dead | the whole body | no | in conversations and scenes (the body away, research/69) | conversation_world's body and legs rules |
| Look-at chain (`HMM_PLR_lookAt_Def`: eyes 25, head, neck, chest 50 deg each) | neck and **chest** toward a target | no | the shoulders turn up to 50 deg toward who the game looks at [trace near people] | none on the player |
| Native foot IK (`RFIK`, `LFIK`) and `LeftArm` IK (research/18) | feet, left arm | no | under our leg and arm IK | our IK writes after |

## The fix for the idle sway: planted feet (`[Hands] steadyIdle`)

Standing still in first person, the torso solver (`ik::TorsoGoal::plantWeight`, `bodyRest`, `maxPlant`;
`TorsoResult::feetCorrection`, `bodySway`; unit test `TestIdleSway`) reports how far the feet must go back to stand where
the animation planted them: `Root`'s animated horizontal offset from its rest (the mesh's origin), turned with the body,
times the part of standing still (`1 - LegsState::weight`), at most 0.20 m. The writer moves the ankles by it and bends the
knees toward their places moved the same way (`bone_pose::Leg::planted`). The hips stay under the view as before. Not while
the body is away from the pawn, not in third person, not with `legsFollowMotion = 0`. Log `arm IK: standing still,
Shepard's feet stay where the idle animation planted them ...` once per stretch. **Off by default** until the owner checks
it in the headset.

## To trace next

1. In the headset (or with a replay that holds a gun: the holsters need tracked controllers): a gun drawn standing (combat
   idle, the aim blend), firing, a grenade, a cast, crouching, a draw and a holster.
2. Near people (the look-at chain on the player's chest): standing beside a squadmate, and in a conversation shown in the
   world.
3. Hits without god mode, with shields down (the damage additives).
4. With tracked hands: the run's 13-16 cm spine bob against the arms; decide whether the spine above `LowerBack` should be
   held to the follower's lean while moving.

## Log

- 2026-11-14 12:00: the trace (14 bones), the summary tool and the run script; measured idle, an idle fidget, hits (none),
  running; the idle-sway fix rebuilt as planted feet (the first, hips-over-feet, was withdrawn: it read the sway in the wrong
  bone); off by default.
