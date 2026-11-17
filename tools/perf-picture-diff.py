# SPDX-License-Identifier: MIT
"""Picture damage of a render lever (research/sequel-render-levers.md "Performance research"): compares two eye
snapshots taken at the same pose (the world paused with `tools/console.ps1 pause`, the head held still by a replayed
recording, `tools/make-look-recording.py out.mevrrec 10 --yaw <deg>`), one with the lever at each value. With nothing
moving, the only differences are what the lever changed: meshes it culled or let through, temporal effects.

Prints the share of pixels that changed by more than --threshold (0-255, any channel) and the grid cells (--cell px) where
most of them are, and writes a difference image (changed pixels white over the darkened first picture, half size) next
to --out.

    python tools/perf-picture-diff.py <a-left.png> <b-left.png> [--threshold 24] [--cell 64] [--out diff.png]
"""
import argparse
import sys

import numpy as np
from PIL import Image


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--threshold", type=int, default=24)
    ap.add_argument("--cell", type=int, default=64)
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--out")
    args = ap.parse_args(argv)
    a = np.asarray(Image.open(args.a).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(args.b).convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        print(f"different sizes: {a.shape} {b.shape}")
        return 1
    d = np.abs(a - b).max(axis=2)
    changed = d > args.threshold
    h, w = changed.shape
    share = changed.mean()
    print(f"{args.a}\n{args.b}\n  {w}x{h}: {100.0 * share:.3f}% of pixels changed by more than {args.threshold} "
          f"(mean difference {d.mean():.2f})")
    c = args.cell
    gh, gw = h // c, w // c
    cells = changed[:gh * c, :gw * c].reshape(gh, c, gw, c).mean(axis=(1, 3))
    order = np.dstack(np.unravel_index(np.argsort(-cells, axis=None), cells.shape))[0]
    print(f"  cells of {c}px with the most change (x, y in pixels; share changed):")
    for gy, gx in order[:args.top]:
        if cells[gy, gx] <= 0:
            break
        print(f"    ({gx * c:5d}, {gy * c:5d})  {100.0 * cells[gy, gx]:5.1f}%")
    print(f"  cells over 10% changed: {(cells > 0.10).sum()} of {gh * gw}")
    if args.out:
        base = (a.astype(np.float32) * 0.35).astype(np.uint8)
        base[changed] = (255, 255, 255)
        Image.fromarray(base).resize((w // 2, h // 2)).save(args.out)
        print(f"  {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
