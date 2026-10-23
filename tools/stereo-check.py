# SPDX-License-Identifier: MIT
"""Measure a snapshot's stereo against the geometry it declares (research/30).

For distant scenery both eyes look the same way, so a far point that sits at pixel x in the left eye's world
image must sit where the right eye's declared FOV puts that same direction. This script finds each sample
patch of the left image in the right one (normalised cross-correlation, as the sights job's disparity check),
compares the offset with the one the declared FOVs and rects predict, and turns the difference into degrees
per eye (- = each eye's image shifted outward: the far scenery needs the eyes to diverge; + = crossed, nearer).

Pick points on distant scenery (pass them as x,y): a point D metres away has a true crossed disparity of ipd / D
(about +2 px at 20 m, +24 px at 2 m on the PS VR2's 766 px per tangent unit), so nearby surfaces read positive
in any correct mode. Upstream's Stereo 2 with sfr2Convergence 0.012 and swap on measured about -34 px on far
scenery (-1.27 degrees per eye, snapshot 2026-10-22_23-51-45-233; this script's default grid: median -28 px).
Life-size stereo should read about 0 there.

Usage: python tools/stereo-check.py [stamp|latest] [x,y ...]
  stamp: a snapshot in %LOCALAPPDATA%/MEVR/snapshots (default: the newest with world images)
  x,y:   left-image pixels to sample (default: a grid over the upper third, where the far scenery usually is)
Needs numpy and Pillow.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image

SNAPSHOTS = os.path.join(os.environ.get("LOCALAPPDATA", ""), "MEVR", "snapshots")


def newest_stamp():
    stamps = sorted(f[: -len("-left-world.png")] for f in os.listdir(SNAPSHOTS) if f.endswith("-left-world.png"))
    if not stamps:
        sys.exit("no snapshot with world images in " + SNAPSHOTS)
    return stamps[-1]


def tans(fov):
    return (math.tan(math.radians(fov["leftDeg"])), math.tan(math.radians(fov["rightDeg"])))


def main():
    args = sys.argv[1:]
    stamp = args.pop(0) if args and "," not in args[0] else "latest"
    if stamp == "latest":
        stamp = newest_stamp()
    meta = json.load(open(os.path.join(SNAPSHOTS, stamp + ".json"), encoding="utf-8"))
    views = {l["name"]: l for l in meta["layers"] if l.get("type") == "projection"}
    if "left" not in views or "right" not in views:
        sys.exit("the snapshot has no projection layer")
    left_img = np.asarray(Image.open(os.path.join(SNAPSHOTS, stamp + "-left-world.png")).convert("L"), dtype=np.float32)
    right_img = np.asarray(Image.open(os.path.join(SNAPSHOTS, stamp + "-right-world.png")).convert("L"), dtype=np.float32)
    lt, rt = tans(views["left"]["fov"]), tans(views["right"]["fov"])
    wl, wr = left_img.shape[1], right_img.shape[1]
    px_per_tan = wl / (lt[1] - lt[0])
    stereo = meta.get("stereo") or {}
    print(f"{stamp}: stereo mode {stereo.get('mode', '?')} ({stereo.get('modeName', 'no stereo block: an older build')})")
    print(f"  declared left fov {views['left']['fov']}, right fov {views['right']['fov']}; images {wl} / {wr} px wide")

    if args:
        points = [tuple(int(v) for v in a.split(",")) for a in args]
    else:
        h = left_img.shape[0]
        points = [(int(wl * fx), int(h * fy)) for fy in (0.15, 0.25, 0.33) for fx in (0.3, 0.45, 0.6, 0.75)]

    half, vsearch, spread = 60, 6, 140
    residuals = []
    for cx, cy in points:
        t = lt[0] + cx / wl * (lt[1] - lt[0])            # the direction of that pixel in the left eye
        xr = (t - rt[0]) / (rt[1] - rt[0]) * wr           # where the right eye's declared FOV puts it
        expected = cx - xr
        p = left_img[cy - half:cy + half, cx - half:cx + half]
        if p.shape != (2 * half, 2 * half) or p.std() < 4.0:
            print(f"  ({cx}, {cy}): skipped (edge or featureless)")
            continue
        p = (p - p.mean()) / (p.std() + 1e-6)
        best = (-9.0, 0, 0)
        for dy in range(-vsearch, vsearch + 1):
            for dx in range(int(expected) - spread, int(expected) + spread):
                x = cx - dx
                q = right_img[cy + dy - half:cy + dy + half, x - half:x + half]
                if q.shape != p.shape:
                    continue
                q = (q - q.mean()) / (q.std() + 1e-6)
                score = float((p * q).mean())
                if score > best[0]:
                    best = (score, dx, dy)
        score, dx, dy = best
        residual = dx - expected                          # + = the right image's copy sits further left
        per_eye_deg = math.degrees(math.atan(residual / 2.0 / px_per_tan))
        tag = "" if score > 0.6 else "  (weak match)"
        print(f"  ({cx}, {cy}): offset {dx} px (dy {dy}), declared geometry {expected:.1f} px, "
              f"residual {residual:+.1f} px = {per_eye_deg:+.2f} deg per eye, match {score:.2f}{tag}")
        if score > 0.6:
            residuals.append(residual)
    if residuals:
        med = float(np.median(residuals))
        print(f"  median residual {med:+.1f} px = {math.degrees(math.atan(med / 2.0 / px_per_tan)):+.2f} deg per eye "
              f"({len(residuals)} good matches); near 0 (a few px for scenery tens of metres away) = metric")


if __name__ == "__main__":
    main()
