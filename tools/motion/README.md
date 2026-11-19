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

ME stores bone rotations as `W_child = W_parent * L` (checked 2026-11-19 on Miranda: her rest stands with the head at 166 cm
and the feet on the floor; the conjugated reading lies on its side).

## The clip file (`mevr-clip`, version 1)

JSON: `format`, `version`, `name`, `fps`, `frames`, `skeleton` (`bones`, `parents`, the mesh it was made for), `loop`
(`start`, `end` frames: one stride whose end pose is nearest its start), `source` (generate.json), `retarget` (the map,
hips scale, worst direction error), and `clip`: per frame `root` (the Root bone's position, game units) and `local` (each
bone's local rotation, xyzw as the package stores it). Bone order is the skeleton's: a player matches by name and refuses a
skeleton whose mapped bones are missing.

Not yet: the in-game player (the mod posing a body from a clip through the bone hook the hair simulation uses, and
`mevr anim play` taking clips), and a compact binary form if clips ship.

## Licences and data

- ARDY: code Apache-2.0, Core checkpoints the NVIDIA Open Model Agreement (as read 2026-11-19: commercial use allowed, outputs
  not claimed). Its outputs are our generated content; recheck the agreement's wording before a clip ships, and list it in
  `NOTICE.md` then.
- Skeleton TSVs come from the player's game files: they stay local, never committed or shipped. A shipped clip carries only
  bone names and our rotations.
- `skeleton-export` links Legendary Explorer's library: GPL-3.0-only, like `tools/conversation-scan`.
