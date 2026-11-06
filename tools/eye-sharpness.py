# SPDX-License-Identifier: MIT
"""Each snapshot's two eyes compared on the part of the scene both see: the right eye's image is shifted onto the left
eye's (the best horizontal and vertical offset of a downsampled copy), and the sharpness (mean |Laplacian| over the local
contrast) is taken on that shared region only, so content and brightness differences between the eyes don't count.
Motion blur under Halcyon's stereo smears one eye and not the other (docs/knowledge/gotchas/motion-blur-in-stereo.md): an
L/R far from 1.00 in some frames is its signature, but ONLY with two engine renders (`[Performance] sharePass` 0-3): under
one-Draw stereo (sharePass 5, the default) the second eye replays the first's render, blur included, and both smear alike
(2026-11-06: 0.99-1.00 in 12 frames while the owner saw the flicker; use tools/flicker-grab.py). Comparing the eyes' whole
images misleads too: with the PS VR2's offset eye views the two middles hold different content.

    python tools/eye-sharpness.py %LOCALAPPDATA%/MEVR/snapshots <stamp> [<stamp> ...]   (snapshots with -left/-right-world.png)"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

folder = Path(sys.argv[1])


def load(path):
    return np.asarray(Image.open(path).convert("L"), dtype=np.float32)


def lap(a):
    return a[1:-1, 1:-1] * 4 - a[:-2, 1:-1] - a[2:, 1:-1] - a[1:-1, :-2] - a[1:-1, 2:]


def best_shift(left, right, step=8):
    sl, sr = left[::step, ::step], right[::step, ::step]
    h, w = sl.shape
    best = None
    for dy in range(-6, 7):
        for dx in range(-w // 2, 2):  # the right eye sees the scene shifted left (its frustum and the eye distance)
            a = sl[max(0, dy):h + min(0, dy), max(0, -dx):w + min(0, -dx)]
            b = sr[max(0, -dy):h + min(0, -dy), max(0, dx):w + min(0, dx)]
            if a.size < 0.3 * sl.size:
                continue
            e = float(np.mean(np.abs(a - b)))
            if best is None or e < best[0]:
                best = (e, dx * step, dy * step)
    return best


for s in sys.argv[2:]:
    left = load(folder / f"{s}-left-world.png")
    right = load(folder / f"{s}-right-world.png")
    err, dx, dy = best_shift(left, right)
    h, w = left.shape
    a = left[max(0, dy):h + min(0, dy), max(0, -dx):w + min(0, -dx)]
    b = right[max(0, -dy):h + min(0, -dy), max(0, dx):w + min(0, dx)]
    # the middle of the shared part, away from its edges
    hh, ww = a.shape
    a = a[hh // 8: -hh // 8, ww // 8: -ww // 8]
    b = b[hh // 8: -hh // 8, ww // 8: -ww // 8]
    ra = float(np.mean(np.abs(lap(a)))) / (float(a.std()) + 1e-3)
    rb = float(np.mean(np.abs(lap(b)))) / (float(b.std()) + 1e-3)
    print(f"{s}  shared {a.shape[1]}x{a.shape[0]} (shift {dx},{dy}, match err {err:.1f})  left {ra:.3f}  right {rb:.3f}  "
          f"L/R {ra / rb:.2f}  mean {a.mean():.1f}/{b.mean():.1f}")
