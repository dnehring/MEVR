# SPDX-License-Identifier: MIT
"""Generated animation for MEVR: NVIDIA ARDY text-to-motion, retargeted onto a Mass Effect skeleton (tools/motion/README.md).

The flow, one subcommand per step (each writes into the clip's folder, default %LOCALAPPDATA%\\MEVR\\motion\\<name>):

  generate   ARDY (in WSL2, the game-asset-pipeline's install) from a prompt and seed -> <name>.npz + generate.json
  skeleton   a skeletal mesh's reference skeleton from the player's game files (tools/motion/skeleton-export) -> <mesh>.tsv
  retarget   the .npz onto that skeleton -> <name>.mevrclip.json (the clip), <name>-check.png, retarget.json (the checks)
  metrics    walk measures of a .npz: hip sway, pelvis roll, feet crossing, pace, the best one-stride loop
  self-test  the retarget on synthetic skeletons and motion, no game or GPU (exit 1 on a failure)

Needs numpy, scipy and matplotlib (the pipeline's py310 env has them): set MEVR_MOTION_PYTHON or run it with that python.
Clips are our generated content (ARDY's outputs, NVIDIA Open Model Agreement); skeleton TSVs are the player's game data and
stay local.

Example (Miranda's ME3 body, the first clip of 2026-11-19):
  python tools/motion/ardy-clip.py generate --name walk-sway --seed 23 --duration 6 \\
      --prompt "a woman walks forward with a seductive, feminine walk, swaying her hips"
  python tools/motion/ardy-clip.py skeleton --game LE3 --package BioD_CitHub_Miranda.pcc --mesh HMF_ARM_MIRa_MDL
  python tools/motion/ardy-clip.py retarget --name walk-sway --skeleton HMF_ARM_MIRa_MDL --loop auto
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import subprocess
import sys
from pathlib import Path

FORMAT = "mevr-clip"
FORMAT_VERSION = 1
ARDY_MODELS = {"core": "ARDY-Core-RP-20FPS-Horizon40", "core40": "ARDY-Core-RP-20FPS-Horizon40", "core8": "ARDY-Core-RP-20FPS-Horizon8"}
ARDY_LICENSE = "NVIDIA Open Model Agreement (checkpoints); Apache-2.0 (code)"

# ARDY's CoreSkeleton27 (ardy/skeleton/definitions.py), pre-order: the joint index order of its .npz arrays.
CORE = [("Hips", None), ("Spine", "Hips"), ("Spine1", "Spine"), ("Spine2", "Spine1"), ("Spine3", "Spine2"), ("Neck", "Spine3"),
        ("Head", "Neck"), ("RightShoulder", "Spine3"), ("RightArm", "RightShoulder"), ("RightForeArm", "RightArm"),
        ("RightHand", "RightForeArm"), ("RightHandEnd", "RightHand"), ("RightHandThumb1", "RightHand"),
        ("LeftShoulder", "Spine3"), ("LeftArm", "LeftShoulder"), ("LeftForeArm", "LeftArm"), ("LeftHand", "LeftForeArm"),
        ("LeftHandEnd", "LeftHand"), ("LeftHandThumb1", "LeftHand"), ("RightUpLeg", "Hips"), ("RightLeg", "RightUpLeg"),
        ("RightFoot", "RightLeg"), ("RightToeBase", "RightFoot"), ("LeftUpLeg", "Hips"), ("LeftLeg", "LeftUpLeg"),
        ("LeftFoot", "LeftLeg"), ("LeftToeBase", "LeftFoot")]
CI = {name: i for i, (name, _) in enumerate(CORE)}

# The Mass Effect human skeleton (ME1-ME3 bodies share these names) driven from Core: (ME bone, Core joint, Core child that
# gives the bone's direction, ME child that gives it). Without a child the bone takes Core's rotation change only.
# Fingers, face, twist bones, props and hair keep their rest pose relative to their parents.
MAP = [
    ("Pelvis", "Hips", None, None),
    ("LowerBack", "Spine", "Spine1", "Chest"),
    ("Chest", "Spine1", "Spine2", "Chest1"),
    ("Chest1", "Spine2", "Spine3", "Chest2"),
    ("Chest2", "Spine3", "Neck", "Neck"),
    ("Neck", "Neck", "Head", "Head"),
    ("Head", "Head", None, None),
    ("RightCollar", "RightShoulder", "RightArm", "RightShoulder"),
    ("RightShoulder", "RightArm", "RightForeArm", "RightElbow"),
    ("RightElbow", "RightForeArm", "RightHand", "RightWrist"),
    ("RightWrist", "RightHand", "RightHandEnd", "RightMiddleFinger"),
    ("LeftCollar", "LeftShoulder", "LeftArm", "LeftShoulder"),
    ("LeftShoulder", "LeftArm", "LeftForeArm", "LeftElbow"),
    ("LeftElbow", "LeftForeArm", "LeftHand", "LeftWrist"),
    ("LeftWrist", "LeftHand", "LeftHandEnd", "LeftMiddleFinger"),
    ("RightHip", "RightUpLeg", "RightLeg", "RightKnee"),
    ("RightKnee", "RightLeg", "RightFoot", "RightAnkle"),
    ("RightAnkle", "RightFoot", "RightToeBase", "RightToe"),
    ("LeftHip", "LeftUpLeg", "LeftLeg", "LeftKnee"),
    ("LeftKnee", "LeftLeg", "LeftFoot", "LeftAnkle"),
    ("LeftAnkle", "LeftFoot", "LeftToeBase", "LeftToe"),
]

# The ARDY install (the game-asset-pipeline's motion lane: runners/motion_runner.py), overridable like there.
ARDY_WSL_DISTRO = os.environ.get("ARDY_WSL_DISTRO", "Ubuntu")
ARDY_DIR_WSL = os.environ.get("ARDY_DIR_WSL", "/mnt/i/ml/repos/ardy")
ARDY_CONDA_ENV = os.environ.get("ARDY_CONDA_ENV", "ardy")
ARDY_CONDA_SETUP = os.environ.get("ARDY_CONDA_SETUP", "$HOME/miniconda3/etc/profile.d/conda.sh")

REPO = Path(__file__).resolve().parents[2]
GAME_FOLDERS = {"LE1": "ME1", "LE2": "ME2", "LE3": "ME3"}


def motion_root() -> Path:
    return Path(os.environ.get("MEVR_MOTION_DIR") or Path(os.environ.get("LOCALAPPDATA", ".")) / "MEVR" / "motion")


def clip_dir(name: str) -> Path:
    d = motion_root() / name
    d.mkdir(parents=True, exist_ok=True)
    return d


def skeleton_path(mesh: str) -> Path:
    p = Path(mesh)
    if p.suffix.lower() == ".tsv" and p.exists():
        return p
    return motion_root() / "skeletons" / (mesh + ".tsv")


def to_wsl(path: Path) -> str:
    p = str(path.resolve())
    return "/mnt/" + p[0].lower() + p[2:].replace("\\", "/")


# ---- generate ----------------------------------------------------------------------------------------------------------

def cmd_generate(a) -> int:
    out = clip_dir(a.name)
    stem = out / a.name
    npz = stem.with_suffix(".npz")
    if npz.exists() and not a.force:
        print(f"{npz} exists (--force regenerates)")
        return 0
    extra = (f"--diffusion_steps {a.steps} " if a.steps else "") + (f"--cfg_weight {a.cfg} " if a.cfg else "")
    cmd = (f'source "{ARDY_CONDA_SETUP}" 2>/dev/null || true; cd "{ARDY_DIR_WSL}" && conda run -n {ARDY_CONDA_ENV} '
           f'python scripts/generate.py "$ARDY_PROMPT" --model {a.model} --duration {a.duration} --seed {a.seed} {extra}'
           f'--output {to_wsl(stem)}')
    env = dict(os.environ)
    env["ARDY_PROMPT"] = a.prompt  # through the environment: bash never word-splits the prompt
    env["WSLENV"] = "ARDY_PROMPT/u" + (":" + env["WSLENV"] if env.get("WSLENV") else "")
    print(f"ARDY {a.model}: {a.duration} s, seed {a.seed}: {a.prompt!r}")
    proc = subprocess.run(["wsl", "-d", ARDY_WSL_DISTRO, "--", "bash", "-lc", cmd], env=env, text=True,
                          capture_output=True, encoding="utf-8", errors="replace")
    if proc.returncode != 0 or not npz.exists():
        print(proc.stdout[-1500:], proc.stderr[-1500:], sep="\n", file=sys.stderr)
        print("ARDY generation failed", file=sys.stderr)
        return 1
    meta = {"generator": "ARDY", "model": ARDY_MODELS.get(a.model, a.model), "license": ARDY_LICENSE, "prompt": a.prompt,
            "seed": a.seed, "duration": a.duration, "steps": a.steps, "cfg": a.cfg,
            "generated": datetime.datetime.now().isoformat(timespec="seconds")}
    (out / "generate.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print(f"-> {npz}")
    return cmd_metrics(argparse.Namespace(npz=str(npz), name=None))


# ---- skeleton ----------------------------------------------------------------------------------------------------------

def cmd_skeleton(a) -> int:
    package = Path(a.package)
    if not package.exists():
        cooked = Path(a.game_root) / "Game" / GAME_FOLDERS[a.game] / "BioGame" / "CookedPCConsole"
        package = cooked / a.package
    if not package.exists():
        print(f"no package {package}", file=sys.stderr)
        return 1
    out = skeleton_path(a.mesh)
    out.parent.mkdir(parents=True, exist_ok=True)
    project = REPO / "tools" / "motion" / "skeleton-export"
    args = ["dotnet", "run", "--project", str(project), "-c", "Release", "--", str(package), a.mesh, str(out), "--game", a.game]
    return subprocess.run(args).returncode


# ---- the retarget ------------------------------------------------------------------------------------------------------

def load_skeleton(path: Path):
    import numpy as np
    names, parents, pos, quat = [], [], [], []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        names.append(f[1]); parents.append(int(f[2]))
        pos.append([float(x) for x in f[3:6]]); quat.append([float(x) for x in f[6:10]])
    return names, np.array(parents), np.array(pos), np.array(quat)


def forward_kinematics(parents, pos, rots):
    """World (component-space) rotations and positions from local ones: W_b = W_p * L_b, P_b = P_p + W_p(t_b)."""
    import numpy as np
    n = len(parents)
    world = [None] * n
    points = np.zeros((n, 3))
    for i in range(n):
        p = parents[i]
        if p < 0:
            world[i] = rots[i]; points[i] = pos[i]
        else:
            world[i] = world[p] * rots[i]; points[i] = points[p] + world[p].apply(pos[i])
    return world, points


def swing(a, b):
    import numpy as np
    from scipy.spatial.transform import Rotation as R
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b)
    axis = np.cross(a, b); s = np.linalg.norm(axis); c = float(np.dot(a, b))
    if s < 1e-9:
        if c > 0:
            return R.identity()
        ortho = np.cross(a, [1.0, 0, 0]) if abs(a[0]) < 0.9 else np.cross(a, [0, 1.0, 0])
        return R.from_rotvec(np.pi * ortho / np.linalg.norm(ortho))
    return R.from_rotvec(axis / s * np.arctan2(s, c))


def ardy_to_ue(posed_first_frame):
    """ARDY is Y up, +Z forward, metres; UE is X forward, Y right, Z up, units (cm). The side is read from the legs."""
    import numpy as np
    right_minus_left_x = posed_first_frame[CI["RightUpLeg"], 0] - posed_first_frame[CI["LeftUpLeg"], 0]
    return np.array([[0, 0, 1.0], [1.0 if right_minus_left_x > 0 else -1.0, 0, 0], [0, 1.0, 0]])


# The posture correction (README "Posture"): these bones' average deviation from the skeleton's own rest pose (upright),
# measured in the body's heading frame over the loop, is taken out; the motion around it stays. ARDY's walks lean back from
# the hips with the head pushed forward (the owner, 2026-11-19: "the posture is bad").
POSTURE_BONES = ("Pelvis", "LowerBack", "Chest", "Chest1", "Chest2", "Neck", "Head")


def heading(rot_pelvis, rest_pelvis):
    """The body's heading at a frame: a rotation about up (Z) by the yaw of the pelvis's rest forward (+X) as posed."""
    import numpy as np
    from scipy.spatial.transform import Rotation as R
    f = (rot_pelvis * rest_pelvis.inv()).apply([1.0, 0.0, 0.0])
    return R.from_euler("z", float(np.arctan2(f[1], f[0])))


def parse_posture_bones(text):
    """'Pelvis=0,LowerBack=0.3,...' -> per-bone strengths (bones not named: 1)."""
    strengths = {b: 1.0 for b in POSTURE_BONES}
    for part in (text or "").split(","):
        if "=" in part:
            name, value = part.split("=", 1)
            name = name.strip()
            if name not in strengths:
                raise SystemExit(f"--posture-bones: {name} isn't one of {', '.join(POSTURE_BONES)}")
            strengths[name] = float(value)
    return strengths


def retarget(G, P, names, parents, pos, quat, posture=1.0, window=None, strengths=None, hip_sway=1.0):
    """Core world rotations G [T,27,3,3] and positions P [T,27,3] (ARDY axes) onto the skeleton. Returns (frames, world
    points per frame, worst direction error in degrees, hips scale, posture correction per bone in degrees). A frame:
    {"root": xyz, "local": [xyzw per bone]}. `posture` (0..1) takes that share of POSTURE_BONES' average deviation from rest
    out, the average over `window` (frame range, default all)."""
    import numpy as np
    from scipy.spatial.transform import Rotation as R
    M = ardy_to_ue(P[0])
    index = {n: i for i, n in enumerate(names)}
    missing = [b for row in MAP for b in (row[0], row[3]) if b and b not in index]
    if missing or "Root" not in index:
        raise SystemExit(f"the skeleton lacks {sorted(set(missing + ([] if 'Root' in index else ['Root'])))}")
    rest_local = [R.from_quat(q) for q in quat]
    rest_world, rest_points = forward_kinematics(parents, pos, rest_local)

    def core_rest_dir(joint, child):
        # Core's rest world rotations are identity (BVH style): its rest bone direction is the child's offset.
        offset = G[0, CI[joint]].T @ (P[0, CI[child]] - P[0, CI[joint]])
        return M @ offset

    align = {me: (swing(rest_points[index[mc]] - rest_points[index[me]], core_rest_dir(cj, cc)) if cc else R.identity())
             for me, cj, cc, mc in MAP}
    scale = rest_points[index["Pelvis"], 2] / (P[0, CI["Hips"], 1] * 100.0)
    T = G.shape[0]
    targets = [{me: R.from_matrix(M @ G[t, CI[cj]] @ M.T) * align[me] * rest_world[index[me]] for me, cj, cc, mc in MAP}
               for t in range(T)]
    corrections = {}
    if posture > 0:
        lo, hi = window if window else (0, T)
        rest_pelvis = rest_world[index["Pelvis"]]
        heads = [heading(targets[t]["Pelvis"], rest_pelvis) for t in range(T)]
        for b in POSTURE_BONES:
            rest_b = rest_world[index[b]]
            rel = [heads[t].inv() * targets[t][b] * rest_b.inv() for t in range(T)]  # deviation from rest, heading frame
            mean = R.concatenate(rel[lo:hi]).mean()
            fix = R.from_rotvec(-mean.as_rotvec() * posture * (strengths or {}).get(b, 1.0))
            corrections[b] = float(np.degrees(np.linalg.norm(mean.as_rotvec())))
            for t in range(T):
                targets[t][b] = heads[t] * fix * rel[t] * rest_b
    # Hip sway (README "Hip sway"): the pelvis's swing about its average (roll, twist, tilt) and the hips' side-to-side
    # travel, both scaled by `hip_sway`; the chest and head keep their own motion (their world rotations are Core's), the
    # legs their directions.
    sway_side = np.zeros(T)
    if abs(hip_sway - 1.0) > 1e-6:
        lo, hi = window if window else (0, T)
        rest_pelvis = rest_world[index["Pelvis"]]
        heads = [heading(targets[t]["Pelvis"], rest_pelvis) for t in range(T)]
        rel = [heads[t].inv() * targets[t]["Pelvis"] * rest_pelvis.inv() for t in range(T)]
        mean = R.concatenate(rel[lo:hi]).mean()
        for t in range(T):
            about_mean = (mean.inv() * rel[t]).as_rotvec() * hip_sway
            targets[t]["Pelvis"] = heads[t] * mean * R.from_rotvec(about_mean) * rest_pelvis
        # The hips' deviation from their own path (a moving average over one stride, in world space: the walk's travel,
        # drift and turns kept), projected onto each frame's side axis.
        hips = np.array([M @ P[t, CI["Hips"]] for t in range(T)]) * 100.0 * scale
        span = max(1, (hi - lo) // 2)
        path = np.array([hips[max(0, t - span):t + span + 1].mean(axis=0) for t in range(T)])
        side = np.array([heads[t].inv().apply(hips[t] - path[t])[1] for t in range(T)])
        sway_side = side * (hip_sway - 1.0)
    frames, points_per_frame, posture_trace = [], [], []
    for t in range(T):
        target = targets[t]
        world = [None] * len(names); local = [None] * len(names); points = np.zeros((len(names), 3))
        for i, name in enumerate(names):
            p = parents[i]
            if name in target:
                world[i] = target[name]
                local[i] = world[p].inv() * world[i] if p >= 0 else world[i]
            else:
                local[i] = rest_local[i]
                world[i] = world[p] * local[i] if p >= 0 else local[i]
            points[i] = pos[i] if p < 0 else points[p] + world[p].apply(pos[i])
        shift = (M @ P[t, CI["Hips"]]) * 100.0 * scale - points[index["Pelvis"]]
        if sway_side[t] != 0.0:
            shift = shift + heading(world[index["Pelvis"]], rest_world[index["Pelvis"]]).apply([0.0, sway_side[t], 0.0])
        points = points + shift
        points_per_frame.append(points)
        # The posture as drawn, in the body's heading frame: the trunk's lean (pelvis to neck, degrees from vertical, +
        # forward) and the head ahead of the upper chest (cm).
        head = heading(world[index["Pelvis"]], rest_world[index["Pelvis"]])
        trunk = head.inv().apply(points[index["Neck"]] - points[index["Pelvis"]])
        posture_trace.append((float(np.degrees(np.arctan2(trunk[0], trunk[2]))),
                              float(head.inv().apply(points[index["Head"]] - points[index["Chest2"]])[0])))
        # The Root bone's position in component space (not its local offset: Root hangs from God, which needn't sit at the
        # origin; a local offset here sank Miranda to her hips in the game, 2026-11-19).
        frames.append({"root": points[index["Root"]].tolist(), "local": [r.as_quat().tolist() for r in local]})
    worst = 0.0
    for t, points in enumerate(points_per_frame):
        for me, cj, cc, mc in MAP:
            if not cc or me in corrections:
                continue  # a corrected bone differs from Core's on purpose
            a = points[index[mc]] - points[index[me]]
            b = M @ (P[t, CI[cc]] - P[t, CI[cj]])
            worst = max(worst, float(np.degrees(np.arccos(np.clip(np.dot(a, b) / np.linalg.norm(a) / np.linalg.norm(b), -1, 1)))))
    lo, hi = window if window else (0, T)
    measured = np.array(posture_trace[lo:hi])
    corrections["trunkLeanDeg"] = float(measured[:, 0].mean())
    corrections["headForwardCm"] = float(measured[:, 1].mean())
    return frames, points_per_frame, worst, scale, corrections


def best_loop(local_rot_mats, fps):
    """The one-stride loop whose end pose is nearest its start's (frame range, seconds, mean rotation-matrix distance)."""
    import numpy as np
    L = local_rot_mats; T = len(L)
    lo, hi = int(0.9 * fps), int(2.25 * fps)  # a stride of 0.9-2.25 s
    best = None
    for a in range(int(fps), T - lo):
        for k in range(lo, hi + 1):
            b = a + k
            if b >= T:
                break
            d = float(np.mean(np.linalg.norm(L[a, 1:] - L[b, 1:], axis=(1, 2))))
            if best is None or d < best[0]:
                best = (d, a, b)
    return None if best is None else {"start": best[1], "end": best[2], "seconds": (best[2] - best[1]) / fps, "distance": best[0]}


def render_check(png, names, parents, points_per_frame, P, M, scale, fps):
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    T = len(points_per_frame)
    index = {n: i for i, n in enumerate(names)}
    skip = ("Hair", "Camera", "LFIK", "RFIK", "RHIK", "LHIK", "Prop")
    face_root = index.get("headBase", -9)
    shown = [0, T // 4, T // 2, 3 * T // 4, T - 1]
    fig, axes = plt.subplots(2, len(shown), figsize=(4 * len(shown), 8))
    for k, t in enumerate(shown):
        points = points_per_frame[t]
        core = (M @ P[t].T).T * 100.0 * scale
        for row, (u, v, label) in enumerate(((1, 2, "front (Y-Z)"), (0, 2, "side (X-Z)"))):
            ax = axes[row, k]
            for i, name in enumerate(names):
                p = parents[i]
                if p < 0 or names[p] == "God" or name.startswith(skip) or "Swivel" in name or p == face_root:
                    continue
                ax.plot([points[p, u], points[i, u]], [points[p, v], points[i, v]], "-", color="#d06", lw=1.5)
            for j, (name, parent) in enumerate(CORE):
                if parent:
                    ax.plot([core[CI[parent], u], core[j, u]], [core[CI[parent], v], core[j, v]], "-", color="#09c", lw=0.8, alpha=0.7)
            c = points[index["Pelvis"]]
            ax.set_xlim(c[u] - 90, c[u] + 90); ax.set_ylim(-5, 185); ax.set_aspect("equal")
            ax.set_title(f"frame {t} ({t / fps:.2f} s) {label}", fontsize=9)
    fig.suptitle("the skeleton (magenta) driven by the clip (blue: ARDY's own joints, scaled)")
    fig.tight_layout(); fig.savefig(png, dpi=90); plt.close(fig)


def cmd_retarget(a) -> int:
    import numpy as np
    source_folder = clip_dir(a.name)
    clip_name = a.clip_name or a.name  # one motion, a clip per skeleton (walk-sway on ME3's Miranda, walk-sway-me2 on ME2's)
    folder = clip_dir(clip_name)
    npz = Path(a.npz) if a.npz else source_folder / (a.name + ".npz")
    skel = skeleton_path(a.skeleton)
    if not npz.exists() or not skel.exists():
        print(f"missing {npz if not npz.exists() else skel}", file=sys.stderr)
        return 1
    data = np.load(npz, allow_pickle=True)
    G, P, fps = data["global_rot_mats"], data["posed_joints"], float(data["fps"])
    names, parents, pos, quat = load_skeleton(skel)
    loop = None
    if a.loop == "auto":
        loop = best_loop(data["local_rot_mats"], fps)
    elif a.loop and a.loop != "none":
        start, end = (int(x) for x in a.loop.split(":"))
        loop = {"start": start, "end": end, "seconds": (end - start) / fps}
    window = (loop["start"], loop["end"]) if loop else None
    if a.posture_window:
        window = tuple(int(x) for x in a.posture_window.split(":"))  # a jump: its standing frames, not the crouch
    frames, points, worst, scale, corrections = retarget(G, P, names, parents, pos, quat, a.posture, window,
                                                         parse_posture_bones(a.posture_bones), a.hip_sway)
    header = next((line for line in skel.read_text(encoding="utf-8").splitlines() if line.startswith("#")), "")
    meta_path = source_folder / "generate.json"
    source = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {"npz": str(npz)}
    clip = {"format": FORMAT, "version": FORMAT_VERSION, "name": clip_name, "motion": a.name, "fps": fps, "frames": len(frames),
            "units": "game units (cm), UE axes (X forward, Y right, Z up); local rotations xyzw as the package stores them",
            "skeleton": {"file": skel.name, "header": header.lstrip("# "), "bones": names, "parents": parents.tolist()},
            "loop": loop, "source": source, "retarget": {"method": "swing-aligned rest, Core world rotation change",
            "map": [list(row) for row in MAP], "hipsScale": scale, "worstDirectionDeg": worst,
            "posture": a.posture, "postureCorrectionDeg": corrections},
            "clip": frames}
    out = folder / f"{clip_name}.mevrclip.json"
    out.write_text(json.dumps(clip), encoding="utf-8")
    check = {"frames": len(frames), "fps": fps, "hipsScale": scale, "worstDirectionDeg": worst, "loop": loop,
             "posture": a.posture, "postureCorrectionDeg": corrections,
             "travelM": float(np.linalg.norm(points[-1][names.index("Pelvis")][:2] - points[0][names.index("Pelvis")][:2]) / 100)}
    (folder / "retarget.json").write_text(json.dumps(check, indent=2), encoding="utf-8")
    png = folder / f"{clip_name}-check.png"
    render_check(png, names, parents, points, P, ardy_to_ue(P[0]), scale, fps)
    print(f"{len(frames)} frames at {fps:.0f} fps onto {len(names)} bones, hips x{scale:.3f}, worst direction error {worst:.2f} deg"
          + (f", loop {loop['start']}..{loop['end']} ({loop['seconds']:.2f} s)" if loop else ""))
    print(f"-> {out}\n-> {png}")
    return 0 if worst < a.max_error else 1


# ---- metrics -----------------------------------------------------------------------------------------------------------

def cmd_metrics(a) -> int:
    import numpy as np
    from scipy.spatial.transform import Rotation as R
    npz = Path(a.npz) if a.npz else clip_dir(a.name) / (a.name + ".npz")
    d = np.load(npz, allow_pickle=True)
    P, G, fps = d["posed_joints"], d["global_rot_mats"], float(d["fps"])
    hips = P[:, CI["Hips"]]
    window = max(1, int(fps))
    sway = hips[:, 0] - np.convolve(hips[:, 0], np.ones(window) / window, "same")
    roll = np.degrees([R.from_matrix(g).as_euler("zyx")[0] for g in G[:, CI["Hips"]]])
    lf, rf = P[:, CI["LeftFoot"]], P[:, CI["RightFoot"]]
    crossing = float(np.mean(np.sign(lf[:, 0] - rf[:, 0]) != np.sign(lf[0, 0] - rf[0, 0])))
    speed = float(np.linalg.norm(np.diff(hips[:, [0, 2]], axis=0), axis=1).mean() * fps)
    edge = window // 2
    loop = best_loop(d["local_rot_mats"], fps)
    print(f"{npz.name}: {len(P)} frames at {fps:.0f} fps; hip sway {np.ptp(sway[edge:-edge]) * 100:.1f} cm, pelvis roll "
          f"{np.ptp(roll[edge:-edge]):.1f} deg, feet crossing {crossing * 100:.0f}% of frames, pace {speed:.2f} m/s"
          + (f"; best loop {loop['start']}..{loop['end']} ({loop['seconds']:.2f} s, distance {loop['distance']:.3f})" if loop else ""))
    return 0


# ---- self-test ---------------------------------------------------------------------------------------------------------

def cmd_self_test(a) -> int:
    """Synthetic Core motion onto a synthetic ME-named skeleton whose rest rotations are random and whose arms hang in an
    A pose: every aligned bone must point where Core's does (the retarget's one promise), the hips must land on Core's."""
    import numpy as np
    from scipy.spatial.transform import Rotation as R
    rng = np.random.default_rng(5)
    # Core rest offsets (ARDY axes: Y up, Z forward, the right side at -X), metres.
    off = {"Hips": [0, 0.95, 0], "Spine": [0, 0.1, 0], "Spine1": [0, 0.1, 0], "Spine2": [0, 0.1, 0], "Spine3": [0, 0.1, 0],
           "Neck": [0, 0.15, 0], "Head": [0, 0.1, 0], "RightShoulder": [-0.05, 0.08, 0], "RightArm": [-0.12, 0, 0],
           "RightForeArm": [-0.28, 0, 0], "RightHand": [-0.25, 0, 0], "RightHandEnd": [-0.1, 0, 0], "RightHandThumb1": [-0.03, 0, 0.03],
           "LeftShoulder": [0.05, 0.08, 0], "LeftArm": [0.12, 0, 0], "LeftForeArm": [0.28, 0, 0], "LeftHand": [0.25, 0, 0],
           "LeftHandEnd": [0.1, 0, 0], "LeftHandThumb1": [0.03, 0, 0.03], "RightUpLeg": [-0.09, -0.05, 0], "RightLeg": [0, -0.42, 0],
           "RightFoot": [0, -0.42, 0], "RightToeBase": [0, -0.05, 0.12], "LeftUpLeg": [0.09, -0.05, 0], "LeftLeg": [0, -0.42, 0],
           "LeftFoot": [0, -0.42, 0], "LeftToeBase": [0, -0.05, 0.12]}
    T = 24
    G = np.zeros((T, 27, 3, 3)); P = np.zeros((T, 27, 3))
    for t in range(T):
        for j, (name, parent) in enumerate(CORE):
            local = R.from_rotvec(rng.normal(0, 0.35, 3)) if t > 0 else R.identity()
            if parent is None:
                G[t, j] = local.as_matrix(); P[t, j] = np.array(off[name]) + [0, 0, 0.03 * t]
            else:
                pj = CI[parent]
                G[t, j] = G[t, pj] @ local.as_matrix(); P[t, j] = P[t, pj] + G[t, pj] @ np.array(off[name])
    # The ME-named skeleton: UE axes, cm, an A pose, random rest rotations (the retarget must not care).
    world_points = {"God": [0, 0, 90], "Root": [0, 0, 100], "Pelvis": [0, 0, 100], "LowerBack": [0, 0, 105], "Chest": [0, 0, 115],
                    "Chest1": [0, 0, 125], "Chest2": [0, 0, 135], "Neck": [0, 0, 150], "Head": [0, 0, 160],
                    "RightCollar": [0, 5, 145], "RightShoulder": [0, 16, 145], "RightElbow": [0, 34, 125], "RightWrist": [0, 50, 108],
                    "RightMiddleFinger": [0, 56, 102], "LeftCollar": [0, -5, 145], "LeftShoulder": [0, -16, 145],
                    "LeftElbow": [0, -34, 125], "LeftWrist": [0, -50, 108], "LeftMiddleFinger": [0, -56, 102],
                    "RightHip": [0, 10, 95], "RightKnee": [0, 10, 52], "RightAnkle": [0, 10, 9], "RightToe": [12, 10, 2],
                    "LeftHip": [0, -10, 95], "LeftKnee": [0, -10, 52], "LeftAnkle": [0, -10, 9], "LeftToe": [12, -10, 2]}
    parent_of = {"God": None, "Root": "God", "Pelvis": "Root", "LowerBack": "Root", "Chest": "LowerBack", "Chest1": "Chest",
                 "Chest2": "Chest1", "Neck": "Chest2", "Head": "Neck", "RightCollar": "Chest2", "RightShoulder": "RightCollar",
                 "RightElbow": "RightShoulder", "RightWrist": "RightElbow", "RightMiddleFinger": "RightWrist",
                 "LeftCollar": "Chest2", "LeftShoulder": "LeftCollar", "LeftElbow": "LeftShoulder", "LeftWrist": "LeftElbow",
                 "LeftMiddleFinger": "LeftWrist", "RightHip": "Pelvis", "RightKnee": "RightHip", "RightAnkle": "RightKnee",
                 "RightToe": "RightAnkle", "LeftHip": "Pelvis", "LeftKnee": "LeftHip", "LeftAnkle": "LeftKnee", "LeftToe": "LeftAnkle"}
    names = list(parent_of)
    parents = np.array([names.index(parent_of[n]) if parent_of[n] else -1 for n in names])
    world_rot = {n: R.from_rotvec(rng.normal(0, 1.0, 3)) for n in names}
    pos = np.zeros((len(names), 3)); quat = np.zeros((len(names), 4))
    for i, n in enumerate(names):
        p = parent_of[n]
        if p is None:
            pos[i] = world_points[n]; quat[i] = world_rot[n].as_quat()
        else:
            pos[i] = world_rot[p].inv().apply(np.array(world_points[n]) - world_points[p])
            quat[i] = (world_rot[p].inv() * world_rot[n]).as_quat()
    frames, points, worst, scale, _ = retarget(G, P, names, parents, pos, quat, posture=0.0)
    # The posture correction: the same motion with every spine joint leaned back a constant 20 degrees on top; corrected,
    # the posture bones' average deviation from rest in the heading frame must be gone (under a degree), the legs untouched.
    lean = R.from_euler("x", 20, degrees=True).as_matrix()  # about ARDY's X: a lean in the sagittal plane
    G2 = G.copy()
    for t in range(T):
        for j, (name, parent) in enumerate(CORE):
            if name in ("Spine", "Spine1", "Spine2", "Spine3", "Neck", "Head"):
                G2[t, j] = lean @ G2[t, j]
    corrected_frames, points2, _, _, corrections = retarget(G2, P, names, parents, pos, quat, posture=1.0)
    rest_local = [R.from_quat(q) for q in quat]
    rest_world, _ = forward_kinematics(parents, pos, rest_local)
    residual = 0.0
    # Checked on the clip's own frames: each posture bone's world rotation rebuilt from the corrected locals.
    for b in POSTURE_BONES:
        bi = names.index(b)
        devs = []
        for t in range(T):
            world = [None] * len(names)
            for i in range(len(names)):
                p = parents[i]
                lr = R.from_quat(corrected_frames[t]["local"][i])
                world[i] = world[p] * lr if p >= 0 else lr
            head = heading(world[names.index("Pelvis")], rest_world[names.index("Pelvis")])
            devs.append(head.inv() * world[bi] * rest_world[bi].inv())
        residual = max(residual, float(np.degrees(np.linalg.norm(R.concatenate(devs).mean().as_rotvec()))))
    legs_kept = max(float(np.linalg.norm(points2[t][names.index(n)] - points2[t][names.index("Pelvis")]
                                         - (points[t][names.index(n)] - points[t][names.index("Pelvis")])))
                    for t in range(T) for n in ("RightAnkle", "LeftAnkle"))
    posture_ok = residual < 1.0 and legs_kept < 0.5 and max(v for k, v in corrections.items() if k in POSTURE_BONES) > 5.0
    # Hip sway: at x2 the pelvis swings twice as far about its average; a leg's direction (hip to knee) stays Core's.
    def pelvis_swing(fr):
        worlds = []
        for t in range(T):
            world = [None] * len(names)
            for i in range(len(names)):
                p = parents[i]
                lr = R.from_quat(fr[t]["local"][i])
                world[i] = world[p] * lr if p >= 0 else lr
            worlds.append(world)
        pi = names.index("Pelvis")
        rel = [heading(w[pi], rest_world[pi]).inv() * w[pi] * rest_world[pi].inv() for w in worlds]
        mean = R.concatenate(rel).mean()
        return float(np.mean([np.linalg.norm((mean.inv() * r).as_rotvec()) for r in rel]))
    swayed, sway_points, _, _, _ = retarget(G, P, names, parents, pos, quat, posture=0.0, hip_sway=2.0)
    ratio = pelvis_swing(swayed) / pelvis_swing(frames)
    leg_turn = 0.0
    for t in range(T):
        for hip, knee in (("RightHip", "RightKnee"), ("LeftHip", "LeftKnee")):
            a = points[t][names.index(knee)] - points[t][names.index(hip)]
            b = sway_points[t][names.index(knee)] - sway_points[t][names.index(hip)]
            leg_turn = max(leg_turn, float(np.degrees(np.arccos(np.clip(np.dot(a, b) / np.linalg.norm(a) / np.linalg.norm(b), -1, 1)))))
    sway_ok = 1.6 < ratio < 2.4 and leg_turn < 0.01
    print(f"self-test: hip sway x2 swings the pelvis x{ratio:.2f} about its average, legs turned {leg_turn:.4f} deg: "
          + ("PASS" if sway_ok else "FAIL"))
    posture_ok = posture_ok and sway_ok
    print(f"self-test: posture correction removed up to {max(corrections.values()):.1f} deg, residual {residual:.2f} deg, "
          f"ankles moved {legs_kept:.3f} units: " + ("PASS" if posture_ok else "FAIL"))
    M = ardy_to_ue(P[0])
    hips_error = max(float(np.linalg.norm(points[t][names.index("Pelvis")] - (M @ P[t, CI["Hips"]]) * 100 * scale)) for t in range(T))
    rest_ok = abs(scale - 100 / 95) < 1e-6
    # The clip's root is the Root bone's component-space position (God sits at 90 here, Root at 100 at rest).
    root_error = max(float(np.linalg.norm(np.array(frames[t]["root"]) - points[t][names.index("Root")])) for t in range(T))
    root_height_ok = abs(frames[0]["root"][2] - (points[0][names.index("Pelvis")][2])) < 1e-6
    ok = (worst < 0.01 and hips_error < 1e-6 and rest_ok and root_error < 1e-9 and root_height_ok and len(frames) == T
          and len(frames[0]["local"]) == len(names) and posture_ok)
    print(f"self-test: worst direction error {worst:.4f} deg, hips error {hips_error:.2e} units, root error {root_error:.2e}, "
          f"hips scale {scale:.4f}: "
          + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate"); g.add_argument("--name", required=True); g.add_argument("--prompt", required=True)
    g.add_argument("--seed", type=int, default=23); g.add_argument("--duration", type=float, default=6.0)
    g.add_argument("--model", default="core"); g.add_argument("--steps", default=""); g.add_argument("--cfg", default="")
    g.add_argument("--force", action="store_true")
    s = sub.add_parser("skeleton"); s.add_argument("--package", required=True); s.add_argument("--mesh", required=True)
    s.add_argument("--game", default="LE3", choices=["LE1", "LE2", "LE3"])
    s.add_argument("--game-root", default=r"C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition")
    r = sub.add_parser("retarget"); r.add_argument("--name", required=True); r.add_argument("--skeleton", required=True)
    r.add_argument("--npz"); r.add_argument("--loop", default="auto", help="auto | none | <start>:<end> (frames; a jump: its window)")
    r.add_argument("--clip-name", help="the clip's name and folder (default the motion's): one motion onto several skeletons")
    r.add_argument("--max-error", type=float, default=0.5, help="fail above this worst bone direction error (degrees)")
    r.add_argument("--posture", type=float, default=1.0, help="0..1: take this share of the pelvis, spine, neck and head's "
                   "average lean (from the skeleton's upright rest, over the loop) out; 0 keeps ARDY's (README 'Posture')")
    r.add_argument("--posture-bones", help="per-bone strengths, e.g. 'Pelvis=0,LowerBack=0' keeps the walk's own pelvis "
                   "tilt and lower-back arch (its style) and straightens the rest (bones not named: 1)")
    r.add_argument("--hip-sway", type=float, default=1.0, help="scale the pelvis's swing about its average and the hips' "
                   "side-to-side travel (1: as generated; README 'Hip sway')")
    r.add_argument("--posture-window", help="<start>:<end> frames the posture is measured over (default the loop; a jump: "
                   "its standing frames before the take-off)")
    m = sub.add_parser("metrics"); m.add_argument("--name"); m.add_argument("--npz")
    sub.add_parser("self-test")
    a = ap.parse_args(argv)
    return {"generate": cmd_generate, "skeleton": cmd_skeleton, "retarget": cmd_retarget, "metrics": cmd_metrics,
            "self-test": cmd_self_test}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
