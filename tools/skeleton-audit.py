# SPDX-License-Identifier: MIT
"""The skeleton audit (research/72): what the game's animation does to Shepard's skeleton that our body, arms or view don't
account for. Reads the CSVs `mevr camera trace <seconds> <label>` writes (game/CameraProbe: each mod tick the pawn, the
game camera and 14 bones in the mesh component's space) and prints, per trace:

  - the game camera against the pawn (place and rotation, peak-to-peak): a sway of the view itself;
  - Root in the mesh's space (LE1's Root sits at hip height and carries the whole body): the body follower puts it under
    the view, so its motion comes back as feet sliding under a player standing still (research/72 "The idle sway");
  - every other bone against Root: motion the follower doesn't remove (the spine's lean and bob reach the shoulders, and
    with the hands on the controllers, the arms).

Anything over --flag cm (default 3) is marked. Without paths, the newest --last labelled traces.

    python tools/skeleton-audit.py [trace.csv ...] [--last 6] [--flag 3]
"""
import argparse
import csv
import glob
import math
import os

BONES = ['Pelvis', 'LowerBack', 'Chest2', 'Neck', 'Head', 'LeftCollar', 'RightCollar', 'LeftHip', 'RightHip', 'LeftAnkle',
         'RightAnkle', 'Camera']


def number(row, key):
    value = row.get(key, '')
    return float(value) if value not in ('', None) else None


def peak(values):
    v = [x for x in values if x is not None]
    return (max(v) - min(v)) if len(v) > 2 else float('nan')


def label_of(path):
    name = os.path.basename(path)[:-4]
    parts = name.split('-')
    # camera-trace-YYYY-MM-DD_hh-mm-ss[-label]
    return parts[7] if len(parts) > 7 else 'trace'


def summarise(path, flag):
    rows = [r for r in csv.DictReader(open(path, newline='')) if r.get('onFoot') == '1']
    if len(rows) < 10:
        print(f'{os.path.basename(path)}: {len(rows)} rows on foot: skipped')
        return
    span = float(rows[-1]['t']) - float(rows[0]['t'])
    speed = max(math.hypot(number(r, 'velX'), number(r, 'velY')) for r in rows)
    forward, side, up = [], [], []
    for r in rows:
        yaw = number(r, 'pawnYaw') * math.pi / 32768.0
        dx, dy = number(r, 'camX') - number(r, 'pawnX'), number(r, 'camY') - number(r, 'pawnY')
        forward.append(dx * math.cos(yaw) + dy * math.sin(yaw))
        side.append(-dx * math.sin(yaw) + dy * math.cos(yaw))
        up.append(number(r, 'camZ') - number(r, 'pawnZ'))
    deg = 360.0 / 65536.0
    print(f'== {label_of(path)}: {span:.1f} s, the pawn up to {speed:.0f} u/s')
    moving = speed > 5.0
    print(f'   game camera against the pawn, p-p: forward {peak(forward):.1f} side {peak(side):.1f} up {peak(up):.1f} cm; '
          f'pitch {peak([number(r, "camPitch") for r in rows]) * deg:.1f} yaw {peak([number(r, "camYaw") for r in rows]) * deg:.1f} '
          f'roll {peak([number(r, "camRoll") for r in rows]) * deg:.1f} deg'
          + ('  (moving: the camera\'s update lags the pawn\'s, read the bones)' if moving else ''))
    print(f'   Root in the mesh\'s space, p-p: X {peak([number(r, "RootX") for r in rows]):.1f} '
          f'Y {peak([number(r, "RootY") for r in rows]):.1f} Z {peak([number(r, "RootZ") for r in rows]):.1f} cm')
    print('   against Root, p-p cm:       X      Y      Z')
    for bone in BONES:
        values = []
        for axis in 'XYZ':
            rel = [(number(r, bone + axis) - number(r, 'Root' + axis))
                   if number(r, bone + axis) is not None and number(r, 'Root' + axis) is not None else None for r in rows]
            values.append(peak(rel))
        mark = f'  <-- over {flag:g} cm' if any(v > flag for v in values if not math.isnan(v)) else ''
        print(f'   {bone:12s}           {values[0]:6.1f} {values[1]:6.1f} {values[2]:6.1f}{mark}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('paths', nargs='*')
    parser.add_argument('--last', type=int, default=6)
    parser.add_argument('--flag', type=float, default=3.0)
    args = parser.parse_args()
    paths = args.paths or sorted(glob.glob(os.path.expandvars(r'%LOCALAPPDATA%\MEVR\camera-trace-*-*-*_*-*-*-*.csv')))[-args.last:]
    for path in paths:
        summarise(path, args.flag)


if __name__ == '__main__':
    main()
