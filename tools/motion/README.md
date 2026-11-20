<!-- SPDX-License-Identifier: MIT -->
# Generated animation: ARDY clips on Mass Effect skeletons

Our own animations, generated with NVIDIA's ARDY (text-to-motion) and retargeted onto the games' human skeletons. First
use: the hair and jiggle bench (docs/trilogy-hair-plan.md); later gestures, idles and motions the games lack. Started
2026-11-19 with the owner's first prompt, a hip-swaying walk on ME3's Miranda.

`ardy-clip.py` is the flow; `skeleton-export/` reads a skeleton from the player's game files. Maintain both here: every
change to the flow goes into this file and the script's self-test.

## The flow

Run with a Python that has numpy, scipy and matplotlib (the game-asset-pipeline's `I:\miniconda3\envs\py310\python.exe`).
Everything is written under `%LOCALAPPDATA%\MEVR\motion` (`MEVR_MOTION_DIR` overrides): one folder per clip, `skeletons\`
for skeletons.

```powershell
$py = 'I:\miniconda3\envs\py310\python.exe'
# 1. Generate (ARDY in WSL2, the pipeline's install; about a minute with the model's load). Deterministic per seed.
& $py tools/motion/ardy-clip.py generate --name walk-sway --seed 23 --duration 6 --prompt "a woman walks forward with a seductive, feminine walk, swaying her hips"
# 2. The target skeleton, once per body mesh (dotnet; Legendary Explorer's nightly in mod-manager\).
& $py tools/motion/ardy-clip.py skeleton --game LE3 --package BioD_CitHub_Miranda.pcc --mesh HMF_ARM_MIRa_MDL
# 3. Retarget: the clip file, a check render and the checks. Exit 1 if a bone's direction is off by more than --max-error.
& $py tools/motion/ardy-clip.py retarget --name walk-sway --skeleton HMF_ARM_MIRa_MDL --loop auto
# Walk measures of any generated clip (also printed by generate).
& $py tools/motion/ardy-clip.py metrics --name walk-sway
# After any change to the script: the retarget on synthetic skeletons and motion, no game or GPU.
& $py tools/motion/ardy-clip.py self-test
```

A clip's folder: `<name>.npz` (ARDY's output), `generate.json` (prompt, seed, model, licence, date), `<name>.mevrclip.json`
(the clip), `<name>-check.png` (the skeleton driven by the clip beside ARDY's own joints, front and side, five frames: look
at it), `retarget.json` (frames, hips scale, worst bone direction error, loop, travel).

Generate takes `--model core|core8` (ARDY-Core-RP-20FPS-Horizon40 or Horizon8), `--duration` seconds, `--seed`, and
ARDY's `--steps` (diffusion steps) and `--cfg` (classifier-free guidance; about 3.5 holds locomotion more firmly). The WSL
side is the pipeline's (`ARDY_WSL_DISTRO`, `ARDY_DIR_WSL`, `ARDY_CONDA_ENV`, `ARDY_CONDA_SETUP`, as its
`runners/motion_runner.py`).

## How the retarget works

ARDY's Core skeleton has 27 joints (hips, four spine joints, neck, head, shoulders, arms, forearms, hands, legs, feet, toes);
its `.npz` holds each joint's world rotation (`global_rot_mats`) and position (`posed_joints`) per frame, Y up, +Z forward,
metres, 20 fps. Its rest pose has identity rotations (BVH style), so a rest bone's direction is its child's offset.

1. Axes: UE's X forward, Y right, Z up, centimetres. The right side is read from the legs (Core's right leg sits at -X).
2. Each mapped ME bone (`MAP` in the script: pelvis, the spine's four, neck, head, collars, shoulders, elbows, wrists, hips,
   knees, ankles) is swung in its rest pose so it points where Core's rest bone points (`swing`), then takes Core's world
   rotation: `Q(t) = R_core(t) * align * W_rest`. The rest poses never have to match (a T pose against an A pose).
3. Locals from the posed parents; bones not mapped (fingers, face, twists, props, hair) keep their rest locals. Hips height
   scales by the pelvis heights; the root carries the travel.
4. Checks: every mapped bone's direction against Core's, every frame (0.00 degrees on Miranda), and the render.

### CMU takes

ARDY does movement, not dance (2026-11-19: five dance prompts swung the hips 1.2-4.9 cm). Dances come from the CMU Graphics
Lab Motion Capture Database (http://mocap.cs.cmu.edu): a subject's `.asf` skeleton and a take's `.amc` motion (the site's
`.avi` files are previews, not motion). `import-cmu` turns a take into the same `.npz` as ARDY's (Core's 27 joints, ARDY axes,
metres; the dancer turned to face +Z, 120 fps thinned to `--fps`, default 30), so `retarget` and every option apply:

```powershell
& $py tools/motion/ardy-clip.py import-cmu --name cmu-05-10 --asf <05.asf> --amc <05_10.amc>
& $py tools/motion/ardy-clip.py retarget --name cmu-05-10 --skeleton HMF_ARM_MIRa_MDL --loop none --posture 0
```

The forward kinematics are the ASF/AMC standard (a bone's world rotation: its parent's times C M C^-1, C its `axis`, M its
`dof` angles, both extrinsic x, y, z; its end: its start plus that rotation applied to `direction` times `length`; lengths in
inches times the `:units length`). Each Core joint is the CMU bone that starts there (`CMU_MAP`); Spine3 sits half way up the
thorax; the Head point is the lower neck's end (CMU's neck is two jointed segments, and the retarget needs each pair carried
rigidly by its joint). Take 05_10 (subject 5, modern dance): 6.8 s, hips 14.7 cm of sway, every bone at 0.00 degrees. CMU asks
that work using the data credit it ("The data used in this project was obtained from mocap.cs.cmu.edu. The database was created
with funding from NSF EIA-0196217."; `generate.json` records it); takes stay local like every clip, and a clip that ships
names its source in `NOTICE.md` after its terms are checked again.

### Posture

ARDY's walks lean back from the hips with the head pushed forward (the hip-swaying walk: trunk 10 degrees back, head 4 cm
ahead of the upper chest), which reproduced exactly reads as bad posture (the owner, 2026-11-19). `--posture` (0..1, default
1) takes out that share of the pelvis, spine, neck and head's average deviation from the skeleton's own upright rest pose,
measured in the body's heading frame over the loop (`--posture-window a:b` instead: a jump's standing frames before the
take-off, not its crouch); the motion around the average stays. Straightening every bone also took the walk's style (its
arched lower back and tilted pelvis: "no longer feminine"), so `--posture-bones` sets a strength per bone:
`Pelvis=0,LowerBack=0` keeps the hips' tilt and the lower back's arch and straightens the chest, neck and head (option C of
2026-11-19: trunk 7 degrees back, the head 1.3 cm). `retarget.json` reports each bone's correction and the result,
`trunkLeanDeg` (pelvis to neck, + forward) and `headForwardCm`; her rest pose is -2.5 degrees and 1.3 cm.

### Hip sway

`--hip-sway <factor>` (1: as generated) scales the pelvis's swing about its average (roll, twist, tilt) and the hips'
side-to-side travel about their own path; the chest and head keep their motion, the legs their directions (at large
factors the feet slide a little: hidden walking in place). The walk's hips travel 11.5 cm side to side at 1, 19.5 at 1.5,
27.6 at 2.

ME stores bone rotations as `W_child = W_parent * L` (checked 2026-11-19 on Miranda: her rest stands with the head at 166 cm
and the feet on the floor; the conjugated reading lies on its side).

## The clip file (`mevr-clip`, version 1)

JSON: `format`, `version`, `name`, `fps`, `frames`, `skeleton` (`bones`, `parents`, the mesh it was made for), `loop`
(`start`, `end` frames: one stride whose end pose is nearest its start), `source` (generate.json), `retarget` (the map,
hips scale, worst direction error), and `clip`: per frame `root` (the Root bone's position, game units) and `local` (each
bone's local rotation, xyzw as the package stores it). Bone order is the skeleton's: a player matches by name and refuses a
skeleton whose mapped bones are missing.

`root` is the Root bone's position in component space (not its local offset: Root hangs from God, which needn't sit at the
origin; the first clip carried the local offset and sank Miranda to her hips in the game; the self-test now puts God at 90).

## Playing a clip in the game

`mevr anim clip <who> <clip> [rate] [walk m/s | auto] [range m]` (game/AnimDriver; `<clip>` a clip folder's name under the
motion folder, or a path to a `.mevrclip.json`, read again at every play): the core (`core/ik/Clip`, unit-tested in
`tests/ClipTests.cpp`) samples the loop (frames slerped; the root's straight travel taken out, its sway and bob kept), and
the stage (`engine/ClipPose`) poses the body right after the engine's own pose update and before the hair stage: the map's
bones take the clip's local rotations, Root its height and sway, every other bone (face, fingers, twists, hair) the game's
animation; the game's bone lengths stay. `auto` walks the character back and forth at the loop's own pace. `mevr anim
status` gives the pose updates, the bones found and Root before and after the clip. Today the character must be an ME3
conversation character (`SFXStuntActor`); checked 2026-11-19 with `walk-sway` on Miranda: 21 of 21 bones, written on every
update, her hair swinging 4-7 cm with the walk.

Pawns too since the same day (ME2's Miranda at Freedom's Progress: `mevr anim clip vixen walk-sway-me2 1 auto 2`). A clip is
made for one skeleton: retarget a motion once per body with `--clip-name` (`retarget --name walk-sway --clip-name
walk-sway-me2 --skeleton HMF_MIR_LGTa_MDL`). `mevr anim at <seconds>` holds a moment of the loop (snapshots of a landing).

The clips of 2026-11-19 (seed 23): `walk-sway`, `jump-place` (a 0.25 s jump, hips +-1 g at take-off and landing; play the
window `--loop 24:64`: the loop finder picks the quiet end), `jump-forward`, `run` (its best loop isn't seamless: a seam at each
wrap), `turn-walk`. `mevr-hair-bench` (tools/hair-bench) plays any of them offline for the hair or `--jiggle`.

`mevr anim clip <who> <clip> [rate] [walk] [range] [turn deg]`: `turn` faces the character that many degrees from where it
stood (90 with walk 0: walking in place side-on to the camera, the angle for judging hair); with a walk it also walks her
out of a fixed camera's view, so for footage walk in place. `mevr anim slowmo <f>` slows our motion only (the clip, the hair
simulation, the driver's walk) by f; the world's `TimeDilation` didn't reach a conversation character's pose updates.

Not yet: a blend in and out, playing a clip once (no loop), and a compact binary form if clips ship.

## Footage

Snapshots are 3-4 s apart, so motion can't be judged from them at full speed. `tools/footage.ps1` slows our motion (`mevr
anim slowmo`), takes snapshots at a steady interval, puts the speed back and crops the frames into a GIF timed at the
motion's real rate (each frame lasts its capture gap times the slow motion):

```powershell
# Miranda in front, then walking in place side-on (look at one snapshot first: is she in the crop?).
tools/console.ps1 "mevr hair bring miranda 0" "mevr anim clip miranda walk-sway 1 0 0 90"
tools/footage.ps1 -Frames 80 -Dilation 0.01 -Crop '0.30,0.42,0.40,0.58' -Scale 0.4 -Out miranda-walk.gif
```

`-Dilation 0.01` with the snapshots' ~3.8 s gives about 26 frames per second of motion; 80 frames take five minutes and show
two loops of the walk. The physics takes the same steps as at full speed, only spread out; the game's own animation (the
face, idle gestures) is not slowed. For an A/B, record the same again after `mevr hair off` (`auto` after). Output and
frames: `%LOCALAPPDATA%\MEVR\footage\<stamp>` (game images: local only). First run 2026-11-19, ME3's Miranda outside her
apartment (research/trilogy-hair.md "Footage").

## Licences and data

- ARDY: code Apache-2.0, Core checkpoints the NVIDIA Open Model Agreement (as read 2026-11-19: commercial use allowed, outputs
  not claimed). Its outputs are our generated content; recheck the agreement's wording before a clip ships, and list it in
  `NOTICE.md` then.
- Skeleton TSVs come from the player's game files: they stay local, never committed or shipped. A shipped clip carries only
  bone names and our rotations.
- `skeleton-export` links Legendary Explorer's library: GPL-3.0-only, like `tools/conversation-scan`.
