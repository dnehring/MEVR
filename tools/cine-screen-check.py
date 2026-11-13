# SPDX-License-Identifier: MIT
"""Measure the 3D cinema screen's stereo in a snapshot: where each part of the picture appears in depth (research/52).

The screen shows each eye's own render on its own quad (eye visibility LEFT/RIGHT, one pose). A feature at quad
column u in the left eye's image and u + d in the right's is seen at d metres of on-screen separation: d > 0 is
uncrossed (behind the screen), d < 0 crossed (in front), d >= the eye distance makes the eyes diverge. This script
finds each tile of the left quad image in the right one (normalised cross-correlation along the row, then the
vertical offset at that column), and gives per tile:

  - the disparity as a share of the screen's width and in degrees of vergence relative to the screen plane
    (+ behind, - in front), and the distance the eyes fuse it at;
  - the UI's depth against the scene under it: pixels both eyes' renders drew identically (the game's UI is drawn at the
    same pixel in both; the far scene too) are matched apart from the rest of the tile;
  - the vertical offset in pixels (two eyes' images must agree vertically; a few arcminutes are felt).

Then a verdict against the usual stereo comfort limits (about 1 degree in front of or behind the screen; never
diverging; vertical under 0.1 degree), with the UI's depth against the scene under it called out, and the screen's
edge tiles checked for crossed content cut by the frame (a window violation).

Usage: python tools/cine-screen-check.py [stamp|latest|all] [--since HH:MM] [--grid 16x9] [--png folder]
  stamp: a snapshot in %LOCALAPPDATA%/MEVR/snapshots with a "cineScreen" block and its quad0/quad1 images
  all:   every such snapshot (with --since: those taken that day after the time), one summary line each
  --png: write a picture per snapshot, the left eye's image with each tile tinted by its depth (red in front, blue behind)
Needs numpy and Pillow.
"""
import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

SNAPSHOTS = os.path.join(os.environ.get("LOCALAPPDATA", ""), "MEVR", "snapshots")
COMFORT_DEG = 1.0      # the usual limit for content in front of or behind a stereo screen
VERTICAL_DEG = 0.1     # vertical disparity a person starts to feel
MIN_CORR = 0.55        # a tile's best match must be at least this good to count



def screen_snapshots():
    out = []
    for name in sorted(os.listdir(SNAPSHOTS)):
        if not name.endswith("-quad1.png"):
            continue
        stamp = name[: -len("-quad1.png")]
        meta_path = os.path.join(SNAPSHOTS, stamp + ".json")
        if os.path.exists(meta_path):
            out.append(stamp)
    return out


def load(stamp):
    meta = json.load(open(os.path.join(SNAPSHOTS, stamp + ".json"), encoding="utf-8"))
    cs = meta.get("cineScreen") or {}
    if not cs.get("active"):
        return None
    quads = {l.get("eyeVisibility"): l for l in meta.get("layers", []) if l.get("type") == "quad"}
    if 1 not in quads or 2 not in quads:
        return None
    left = np.asarray(Image.open(os.path.join(SNAPSHOTS, stamp + "-quad0.png")).convert("L"), dtype=np.float32)
    right = np.asarray(Image.open(os.path.join(SNAPSHOTS, stamp + "-quad1.png")).convert("L"), dtype=np.float32)
    eyes = meta.get("eyes", [])
    e0 = np.array(eyes[0]["pose"]["position"]) if len(eyes) > 1 else np.zeros(3)
    e1 = np.array(eyes[1]["pose"]["position"]) if len(eyes) > 1 else np.array([0.064, 0, 0])
    ipd = float(np.linalg.norm(e1 - e0)) or float(cs.get("plan", {}).get("ipdM", 0.064))
    quad = quads[1]
    distance = float(np.linalg.norm(np.array(quad["pose"]["position"]) - (e0 + e1) / 2))
    return {"meta": meta, "cs": cs, "left": left, "right": right, "ipd": ipd, "distance": distance,
            "widthM": float(quad["size"][0]), "kind": cs.get("kind", "?")}


def normalise(a):
    s = a.std()
    return (a - a.mean()) / (s + 1e-6), s


def match_tile(left, right, y0, y1, x0, x1, max_dx, max_dy, ignore=None):
    """The tile [y0, y1) x [x0, x1) of the left image searched for in the right one, the search reaching past the tile.
    ignore: a mask of left-image pixels left out of the match (the UI, when the scene under it is wanted)."""
    H, W = right.shape
    y0, y1 = max(y0, max_dy), min(y1, H - max_dy)
    x0, x1 = max(x0, max_dx), min(x1, W - max_dx)   # an edge tile is trimmed so the whole search stays in the image
    if y1 - y0 < 16 or x1 - x0 < 16:
        return None
    a = left[y0:y1, x0:x1]
    keep = np.ones_like(a) if ignore is None else (~ignore[y0:y1, x0:x1]).astype(np.float32)
    if keep.mean() < 0.3:
        return None
    mean = (a * keep).sum() / keep.sum()
    spread = math.sqrt(float((((a - mean) * keep) ** 2).sum() / keep.sum()))
    if spread < 5.0:
        return None
    a = (a - mean) / spread * keep / keep.mean()
    h, w = a.shape
    curve = np.full(2 * max_dx + 1, -1.0)
    for i, dx in enumerate(range(-max_dx, max_dx + 1)):
        if x0 + dx < 0 or x0 + dx + w > W:
            continue
        b, _ = normalise(right[y0:y0 + h, x0 + dx:x0 + dx + w])
        curve[i] = float((a * b).mean())
    best = int(np.argmax(curve))
    corr = curve[best]
    dx = best - max_dx
    dy, corr_v = 0, corr
    for v in range(-max_dy, max_dy + 1):
        b, _ = normalise(right[y0 + v:y0 + v + h, x0 + dx:x0 + dx + w])
        c = float((a * b).mean())
        if c > corr_v:
            corr_v, dy = c, v
    return {"dx": dx, "dy": dy, "corr": max(corr, corr_v)}


UI_SHARE = 0.03        # a tile whose pixels are this much identical-in-both-eyes edges has UI (or far scenery) on it


def identical_edges(left, right, crop_px):
    """Edge pixels the two renders drew identically: at the crop's offset in the shown images (the UI is drawn at the same
    pixel in both renders, as is the far scene). Returns the mask (grown by 3 px) and the offset."""
    if not crop_px:
        return np.zeros(left.shape, bool), None
    gy, gx = np.gradient(left)
    edge = np.hypot(gx, gy) > 25.0
    h, w = left.shape
    best, best_dx = None, None
    for dx in (int(math.floor(crop_px)), int(math.ceil(crop_px))):
        m = np.zeros(left.shape, bool)
        m[:, :w - dx] = (left[:, :w - dx] == right[:, dx:]) & edge[:, :w - dx]
        if best is None or m.sum() > best.sum():
            best, best_dx = m, dx
    grown = best.copy()
    for r in range(1, 4):
        grown[r:, :] |= best[:-r, :]
        grown[:-r, :] |= best[r:, :]
        grown[:, r:] |= best[:, :-r]
        grown[:, :-r] |= best[:, r:]
    return grown, best_dx


def degrees(dx_px, s):
    """Vergence relative to the screen plane, + behind, for an on-screen separation of dx_px quad pixels."""
    w = s["left"].shape[1]
    sep = dx_px / w * s["widthM"]
    d, ipd = s["distance"], s["ipd"]
    screen = 2 * math.atan(ipd / 2 / d)
    if sep >= ipd:
        return math.degrees(screen), float("inf")   # parallel or diverging
    fused = d * ipd / (ipd - sep)
    return math.degrees(screen - 2 * math.atan(ipd / 2 / fused)), fused


def analyse(stamp, grid, png_dir=None, verbose=True):
    s = load(stamp)
    if s is None:
        return None
    left, right = s["left"], s["right"]
    h, w = left.shape
    cols, rows = grid
    tw, th = w // cols, h // rows
    max_dx = max(24, int(0.035 * w))   # 3.5% of the width either way
    max_dy = 10
    px_deg = math.degrees(2 * math.atan(s["widthM"] / 2 / s["distance"])) / w
    plan = s["cs"].get("plan", {})
    crop_px = plan.get("crop", 0.0) * w / (1 - plan.get("crop", 0.0)) if plan.get("crop") else None
    ui, ui_dx = identical_edges(left, right, crop_px)
    tiles = []
    for ty in range(rows):
        for tx in range(cols):
            y0, y1, x0, x1 = ty * th, (ty + 1) * th, tx * tw, (tx + 1) * tw
            m = match_tile(left, right, y0, y1, x0, x1, max_dx, max_dy)
            if m is None or m["corr"] < MIN_CORR:
                tiles.append(None)
                continue
            deg, fused = degrees(m["dx"], s)
            m.update(tx=tx, ty=ty, deg=deg, fused=fused, ui=float(ui[y0:y1, x0:x1].mean()))
            # Pixels both renders drew identically (the UI, or the far scene): the scene under them, matched without them.
            if m["ui"] >= UI_SHARE:
                under = match_tile(left, right, y0, y1, x0, x1, max_dx, max_dy, ignore=ui)
                if under is not None and under["corr"] >= MIN_CORR:
                    m["underDx"] = under["dx"]
                    m["underDeg"], _ = degrees(under["dx"], s)
                    m["uiDeg"], _ = degrees(ui_dx, s)
            tiles.append(m)
    found = [t for t in tiles if t]
    if not found:
        if verbose:
            print(f"{stamp}: {s['kind']}: no tile matched (a dark or flat picture)")
        return {"stamp": stamp, "tiles": 0}
    degs = np.array([t["deg"] for t in found])
    front = [t for t in found if t["deg"] < -COMFORT_DEG]
    behind = [t for t in found if t["deg"] > COMFORT_DEG]
    diverge = [t for t in found if math.isinf(t["fused"])]
    vertical = [t for t in found if abs(t["dy"]) * px_deg > VERTICAL_DEG]
    edge_front = [t for t in found if t["deg"] < -0.25 and (t["tx"] in (0, cols - 1) or t["ty"] in (0, rows - 1))]
    # The UI over the scene: identical pixels (at the crop's disparity: what both renders drew at the same place) over a
    # scene the tile's other pixels put nearer.
    layered = [t for t in found if "underDeg" in t]
    ui_behind = [(t, t["uiDeg"] - t["underDeg"]) for t in layered if t["underDx"] < ui_dx - 3]
    summary = {
        "stamp": stamp, "kind": s["kind"], "tiles": len(found), "nearestDeg": float(degs.min()),
        "farthestDeg": float(degs.max()), "medianDeg": float(np.median(degs)), "front": len(front), "behind": len(behind),
        "diverge": len(diverge), "vertical": len(vertical), "edgeFront": len(edge_front), "layered": len(layered),
        "uiBehind": len(ui_behind), "baselineUU": plan.get("baselineUU"), "lensDeg": 2 * plan.get("halfHDeg", 0.0),
    }
    if verbose:
        print(f"{stamp}: {s['kind']} on a {s['widthM']:.2f} m screen at {s['distance']:.2f} m, eyes {s['ipd'] * 1000:.1f} mm apart; "
              f"the plan: depth {plan.get('depth', 0) * 100:.2f}%, the cameras {plan.get('baselineUU', 0):.2f} units apart for a "
              f"{2 * plan.get('halfHDeg', 0):.1f}-degree lens; 1 px = {px_deg * 60:.2f} arcmin")
        ui_note = f"; 'U' a tile with identical pixels (the UI) at {degrees(ui_dx, s)[0]:+.2f}, the scene under it shown" if ui_dx else ""
        print(f"  depth, degrees from the screen (+ behind, - in front), {cols}x{rows} tiles, '.' unmatched{ui_note}:")
        for ty in range(rows):
            cells = []
            for tx in range(cols):
                t = tiles[ty * cols + tx]
                if t is None:
                    cells.append("    .   ")
                elif "underDeg" in t:
                    cells.append(f"{t['underDeg']:+6.2f}U ")
                else:
                    cells.append(f"{t['deg']:+6.2f}  ")
            print("  " + " ".join(cells))
        print(f"  nearest {summary['nearestDeg']:+.2f} deg, farthest {summary['farthestDeg']:+.2f} deg (fused at "
              f"{min(t['fused'] for t in found):.2f}..{max(t['fused'] for t in found):.2f} m)")
        verdicts = []
        if front:
            verdicts.append(f"{len(front)} tiles more than {COMFORT_DEG:g} degree in front of the screen (nearest {min(t['deg'] for t in front):+.2f})")
        if behind:
            verdicts.append(f"{len(behind)} tiles more than {COMFORT_DEG:g} degree behind it")
        if diverge:
            verdicts.append(f"{len(diverge)} tiles make the eyes diverge")
        if vertical:
            verdicts.append(f"{len(vertical)} tiles with a vertical offset over {VERTICAL_DEG:g} degree (worst {max(abs(t['dy']) for t in vertical)} px)")
        if edge_front:
            verdicts.append(f"{len(edge_front)} edge tiles in front of the screen (cut by the frame)")
        if ui_behind:
            worst = max(d for _, d in ui_behind)
            verdicts.append(f"{len(ui_behind)} tiles where something drawn identically in both eyes (the UI) lies behind the scene "
                            f"it covers, by up to {worst:.2f} degrees")
        print("  " + ("; ".join(verdicts) if verdicts else "within the comfort limits"))
    if png_dir:
        os.makedirs(png_dir, exist_ok=True)
        img = Image.open(os.path.join(SNAPSHOTS, stamp + "-quad0.png")).convert("RGB")
        overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(overlay)
        for t in found:
            k = max(-1.0, min(1.0, t["deg"] / 1.5))
            colour = (int(255 * max(0.0, -k)), 40, int(255 * max(0.0, k)), 110)
            box = (t["tx"] * tw, t["ty"] * th, (t["tx"] + 1) * tw - 1, (t["ty"] + 1) * th - 1)
            draw.rectangle(box, fill=colour)
            draw.text((box[0] + 6, box[1] + 6), f"{t['deg']:+.2f}", fill=(255, 255, 255, 255))
        Image.alpha_composite(img.convert("RGBA"), overlay).convert("RGB").save(os.path.join(png_dir, stamp + "-depth.png"))
    return summary


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("stamp", nargs="?", default="latest")
    p.add_argument("--since", default=None, help="HH:MM: with 'all', only snapshots of the newest day after this time")
    p.add_argument("--grid", default="16x9")
    p.add_argument("--png", default=None)
    a = p.parse_args()
    grid = tuple(int(v) for v in a.grid.lower().split("x"))
    stamps = screen_snapshots()
    if not stamps:
        sys.exit("no 3D screen snapshot (quad0/quad1 images) in " + SNAPSHOTS)
    if a.stamp == "latest":
        stamps = [stamps[-1]]
    elif a.stamp == "all":
        if a.since:
            day = stamps[-1][:10]
            hhmm = a.since.replace(":", "-")
            stamps = [s for s in stamps if s[:10] == day and s[11:16] >= hhmm]
    else:
        stamps = [a.stamp]
    if len(stamps) == 1:
        if analyse(stamps[0], grid, a.png) is None:
            sys.exit(stamps[0] + ": not a 3D screen snapshot")
        return
    rows = [r for r in (analyse(s, grid, a.png, verbose=False) for s in stamps) if r and r.get("tiles")]
    print(f"{'snapshot':24} {'kind':12} {'lens':>5} {'base':>5} {'nearest':>8} {'median':>7} {'farthest':>8} "
          f"{'front>1':>7} {'edge':>5} {'uiBehind':>8} {'vert':>5}")
    for r in rows:
        print(f"{r['stamp']:24} {r['kind']:12} {r['lensDeg']:5.1f} {r['baselineUU'] or 0:5.2f} {r['nearestDeg']:+8.2f} "
              f"{r['medianDeg']:+7.2f} {r['farthestDeg']:+8.2f} {r['front']:7d} {r['edgeFront']:5d} {r['uiBehind']:8d} {r['vertical']:5d}")
    if rows:
        print(f"{len(rows)} snapshots: nearest of all {min(r['nearestDeg'] for r in rows):+.2f} deg; "
              f"{sum(1 for r in rows if r['front'])} with content over {COMFORT_DEG:g} degree in front, "
              f"{sum(1 for r in rows if r['uiBehind'])} with the UI behind the scene it covers")


if __name__ == "__main__":
    main()
