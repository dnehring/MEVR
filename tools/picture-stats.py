# SPDX-License-Identifier: MIT
"""Picture measurements of eye snapshots at one pose (research/27 7d's method, research/le2-picture.md).

    python tools/picture-stats.py <reference.png> [other.png ...] [--windows-from <png>] [--json out.json]

For each image: the whole frame's mean RGB, the share of pixels with a channel at 255 and with luma under 8 ("black"),
and in the darkest and brightest 400x400 windows (by mean luma on a 100-pixel grid, found in the first image or in
--windows-from, so every image is measured at the same place): distinct colours, mean luma, spread, black share, the
distinct luma levels used, the mean run of equal luma along rows and the mean non-zero step between neighbours (banding: a
smooth gradient on the 8-bit grid shows long flat runs with steps of one level; a lift of the shadows on an 8-bit source
makes those steps larger; dither breaks the runs). The same in the smoothest dim window (mean luma 12..90, the least
horizontal gradient: a sky or a wall, where bands show). With two images or more, also the mean absolute difference from
the first.
"""
import argparse
import json
import sys

import numpy as np
from PIL import Image

LUMA = np.array([0.2126, 0.7152, 0.0722])
WINDOW = 400
STEP = 100


def load(path):
    return np.asarray(Image.open(path).convert('RGB'))


def luma(img):
    return img.astype(np.float64) @ LUMA


def windows(img):
    y8 = luma(img)
    best = worst = None
    h, w = y8.shape
    for y in range(0, max(h - WINDOW, 0) + 1, STEP):
        for x in range(0, max(w - WINDOW, 0) + 1, STEP):
            m = y8[y:y + WINDOW, x:x + WINDOW].mean()
            if best is None or m > best[0]:
                best = (m, (y, x))
            if worst is None or m < worst[0]:
                worst = (m, (y, x))
    return best[1], worst[1]


def smooth_window(img, lo=12.0, hi=90.0):
    """The smoothest dim window (mean luma lo..hi, the least mean horizontal gradient): where bands would show."""
    y8 = luma(img)
    gx = np.abs(np.diff(y8, axis=1))
    best = None
    h, w = y8.shape
    for y in range(0, max(h - WINDOW, 0) + 1, STEP):
        for x in range(0, max(w - WINDOW, 0) + 1, STEP):
            m = y8[y:y + WINDOW, x:x + WINDOW].mean()
            if m < lo or m > hi:
                continue
            g = gx[y:y + WINDOW, x:x + WINDOW - 1].mean()
            if best is None or g < best[0]:
                best = (g, (y, x))
    return best[1] if best else None


def steps(y8):
    """Mean size of the non-zero steps between neighbours along rows, in 8-bit luma (rounded): how visible a band edge is."""
    q = np.rint(y8)
    d = np.abs(np.diff(q, axis=1))
    nz = d[d > 0]
    return round(float(nz.mean()), 2) if nz.size else 0.0


def run_length(y8):
    """Mean length of runs of equal (rounded) luma along rows."""
    q = np.rint(y8).astype(np.int32)
    runs = []
    for row in q:
        change = np.flatnonzero(np.diff(row) != 0)
        edges = np.concatenate(([0], change + 1, [row.size]))
        runs.append(np.diff(edges))
    allruns = np.concatenate(runs)
    return float(allruns.mean())


def patch(img, at):
    y, x = at
    p = img[y:y + WINDOW, x:x + WINDOW, :3]
    flat = p.reshape(-1, 3).astype(np.uint32)
    colours = len(np.unique(flat[:, 0] << 16 | flat[:, 1] << 8 | flat[:, 2]))
    y8 = luma(p)
    return {
        'colours': colours,
        'mean': round(float(y8.mean()), 2),
        'spread': round(float(y8.std()), 2),
        'black': round(float((y8 < 8).mean() * 100), 2),
        'levels': int(len(np.unique(np.rint(y8)))),
        'run': round(run_length(y8), 2),
        'step': steps(y8),
    }


def measure(img, bright, dark, ref=None, smooth=None):
    y8 = luma(img)
    out = {
        'meanRgb': [round(float(v), 1) for v in img.reshape(-1, 3).mean(0)],
        'clipped': round(float((img == 255).any(-1).mean() * 100), 3),
        'black': round(float((y8 < 8).mean() * 100), 2),
        'dark': patch(img, dark),
        'bright': patch(img, bright),
    }
    if smooth is not None:
        out['smooth'] = patch(img, smooth)
    if ref is not None and ref.shape == img.shape:
        out['diffFromFirst'] = round(float(np.abs(img.astype(np.int32) - ref.astype(np.int32)).mean()), 3)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('images', nargs='+')
    ap.add_argument('--windows-from')
    ap.add_argument('--json')
    a = ap.parse_args()
    first = load(a.images[0])
    base = load(a.windows_from) if a.windows_from else first
    bright, dark = windows(base)
    smooth = smooth_window(base)
    print(f'windows: darkest at {dark}, brightest at {bright}, smoothest dim at {smooth} (y, x; {WINDOW} px)')
    results = {}
    for path in a.images:
        img = load(path)
        r = measure(img, bright, dark, first if path != a.images[0] else None, smooth)
        results[path] = r
        d, b = r['dark'], r['bright']
        print(f"{path}\n  mean RGB {r['meanRgb']}, any 255 {r['clipped']}%, luma<8 {r['black']}%"
              + (f", |diff| from first {r['diffFromFirst']}" if 'diffFromFirst' in r else '')
              + f"\n  dark: {d['colours']} colours, mean {d['mean']}, spread {d['spread']}, black {d['black']}%, "
              f"{d['levels']} luma levels, mean run {d['run']} px, step {d['step']}"
              + f"\n  bright: {b['colours']} colours, mean {b['mean']}, spread {b['spread']}, "
              f"{b['levels']} luma levels, mean run {b['run']} px")
        if 'smooth' in r:
            s = r['smooth']
            print(f"  smooth: {s['colours']} colours, mean {s['mean']}, {s['levels']} luma levels, mean run {s['run']} px, "
                  f"step {s['step']}")
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump({'windows': {'dark': dark, 'bright': bright, 'smooth': smooth}, 'images': results}, f, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
