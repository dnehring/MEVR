# SPDX-License-Identifier: MIT
"""Generate the route's synthetic controller pulses or a 30-second head turn; no private recordings required."""
import argparse
import importlib.util
import json
import math
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from mevrrec import HEADER, FRAME

spec = importlib.util.spec_from_file_location('walk', TOOLS / 'make-walk-recording.py')
walk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(walk)
# Synthetic Quest-like asymmetric frusta used by the route. Not a measured display calibration.
LEFT_FOV = tuple(math.radians(x) for x in (-54, 40, 44, -55))
RIGHT_FOV = tuple(math.radians(x) for x in (-40, 54, 44, -55))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('out', type=Path)
    p.add_argument('--seconds', type=float, default=3)
    p.add_argument('--yaw', type=float, default=0)
    p.add_argument('--pitch', type=float, default=0)
    p.add_argument('--x', type=float, default=0)
    p.add_argument('--y', type=float, default=0)
    p.add_argument('--lead', type=float, default=0)
    p.add_argument('--tail', type=float, default=0)
    p.add_argument('--look-turn', action='store_true')
    a = p.parse_args()
    if not 0 < a.seconds <= 120 or not 0 <= a.lead <= 10 or not 0 <= a.tail <= 10:
        p.error('seconds must be in (0,120], lead/tail in [0,10]')
    if not all(math.isfinite(v) for v in (a.yaw, a.pitch, a.x, a.y)) or max(abs(a.x), abs(a.y)) > 1:
        p.error('angles must be finite and sticks within [-1,1]')
    frames = []
    for i in range(round((a.lead + a.seconds + a.tail) * 90)):
        t = i / 90
        yaw = 360 * t / a.seconds if a.look_turn else a.yaw
        q = walk.head_orientation(yaw, a.pitch)
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        eyes = [(*q, k * .032 * c, 0, k * .032 * s) for k in (-1, 1)]
        if a.look_turn:
            hands = [(0, 0, 0, 1, k * .22, -.75, -.05) for k in (-1, 1)]
        else:
            hands = [(*q, k * .15 * c + .3 * s, -.35, k * .15 * s - .3 * c) for k in (-1, 1)]
        moving = a.lead <= t < a.lead + a.seconds and not a.look_turn
        frames.append(FRAME.pack(int(t * 1e9), 3, *eyes[0], *LEFT_FOV, *eyes[1], *RIGHT_FOV,
                                 48, 0., 0., a.x if moving else 0., a.y if moving else 0., *hands[0], *hands[0],
                                 48, 0., 0., 0., 0., *hands[1], *hands[1]))
    a.out.write_bytes(HEADER.pack(b'MEVRREC\0', 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)) + b''.join(frames))
    a.out.with_suffix('.json').write_text(json.dumps(vars(a), default=str, indent=2), encoding='utf-8')
    print(f'{a.out}: {len(frames)} frames, {len(frames)/90:g} seconds')


if __name__ == '__main__':
    main()
