# SPDX-License-Identifier: MIT
"""Make a neutral-input, continuous oscillating head-turn loop using the cafeteria recording format."""
import argparse
import importlib.util
import math
from pathlib import Path
import time

spec = importlib.util.spec_from_file_location('movement', Path(__file__).resolve().parents[1] / 'qa-cafeteria/movement.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def write(path, yaw=130, pitch=-10, sweep=60):
    if not all(math.isfinite(x) for x in (yaw, pitch, sweep)) or not 0 < sweep <= 360 or abs(pitch) > 70:
        raise ValueError('invalid head turn')
    frames = []
    for i in range(2700):
        t = i / 90
        angle = yaw + .5 * sweep * math.sin(2 * math.pi * t / 30)
        q = m.walk.head_orientation(angle, pitch)
        c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
        eyes = [(*q, k * .032 * c, 0, k * .032 * s) for k in (-1, 1)]
        hands = [(0, 0, 0, 1, k * .22, -.75, -.05) for k in (-1, 1)]
        frames.append(m.FRAME.pack(int(t * 1e9), 3, *eyes[0], *m.LEFT_FOV, *eyes[1], *m.RIGHT_FOV,
                                  48, 0., 0., 0., 0., *hands[0], *hands[0],
                                  48, 0., 0., 0., 0., *hands[1], *hands[1]))
    Path(path).write_bytes(m.HEADER.pack(b'MEVRREC\0', 1, m.HEADER.size, m.FRAME.size, 0, int(time.time() * 1000)) + b''.join(frames))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('out', type=Path)
    p.add_argument('--yaw', type=float, default=130)
    p.add_argument('--pitch', type=float, default=-10)
    p.add_argument('--sweep', type=float, default=60)
    a = p.parse_args()
    write(a.out, a.yaw, a.pitch, a.sweep)
