# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) whose right hand works the Mako
cockpit's repair button and extraction handle (research/48 section 27), from the panel's layout as the mod prints it
(`mevr cockpit panel`'s "layout": tracking space, metres; the fake headset's head sits at the tracking origin, which the
cockpit's seat is taken from on entering the Mako, so the layout holds for any replay that keeps the head there).

    tools/mako-start.ps1
    tools/console.ps1 "mevr cockpit panel" > panel.txt
    python tools/make-panel-recording.py panel.txt out.mevrrec
    tools/replay.ps1 -Path out.mevrrec -Inputs        # -Inputs: the recorded squeeze (without it the buttons are live)
    tools/snapshot.ps1 -Count 4 -Interval 3 -Delay 3

The input file is that console output, or the status JSON, or the layout object alone. The timeline (seconds):
  0-2     the fingertip 4 cm in front of the cap (game/CockpitPanel's tip: 4 cm ahead of the aim pose), the head looking at it
  2-2.5   pushed 2.5 mm in (past 60% of the 2.8 mm travel); held to 6.5: the 2 s hold fills, the repair runs at about 4.5
  6.5-7   back out; 7-8 the hand to the handle's grip, the head looking up at it; 8.5 the grip squeezed
  9-10    swung 0 -> 34 degrees about the hinge (+ about its axis: out, toward the pilot); held to 14 (LeaveWorld at about
          10.2, the game's "Return to Normandy?" box); 14 let go
Measured on 2026-11-05 07:56 (research/48 section 27): both ran as timed. Prints a RESULT line with the places used."""
import importlib.util
import json
import math
import pathlib
import sys
import time

_spec = importlib.util.spec_from_file_location('mdr', pathlib.Path(__file__).with_name('make-drive-recording.py'))
mdr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mdr)

TIP_M = 0.04       # game/CockpitPanel: the fingertip this far ahead of the aim pose along its -Z
HOVER_M = 0.04     # the fingertip's start in front of the cap
PRESS_M = 0.0025   # how far it pushes in
SWING_DEG = 34.0   # past the handle's 30-degree pull
LEFT_REST = (-0.362, -0.446, -0.286)  # make-drive-recording's lever grab point: the left hand resting, not squeezing


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def mul(a, s):
    return tuple(x * s for x in a)


def lerp(a, b, t):
    return add(a, mul(sub(b, a), max(0.0, min(1.0, t))))


def rotate(q, v):
    x, y, z, w = q
    cx, cy, cz = y * v[2] - z * v[1], z * v[0] - x * v[2], x * v[1] - y * v[0]
    c2x, c2y, c2z = y * cz - z * cy, z * cx - x * cz, x * cy - y * cx
    return (v[0] + 2 * (w * cx + c2x), v[1] + 2 * (w * cy + c2y), v[2] + 2 * (w * cz + c2z))


def about(axis, degrees, v):
    half = math.radians(degrees) / 2.0
    s = math.sin(half)
    return rotate((axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half)), v)


def read_layout(path):
    text = pathlib.Path(path).read_text(encoding='utf-8-sig', errors='replace')
    data = json.loads(text[text.index('{'):text.rindex('}') + 1])
    layout = data.get('layout', data)
    if not layout or 'buttonTop' not in layout:
        raise SystemExit(f'{path}: no panel layout (not riding with the cockpit up when it was printed?)')
    return layout


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    layout = read_layout(sys.argv[1])
    out = sys.argv[2]
    top, inward = tuple(layout['buttonTop']), tuple(layout['buttonIn'])
    hinge, axis, grip = tuple(layout['hinge']), tuple(layout['hingeAxis']), tuple(layout['grip'])
    aim = (0.0, 0.0, 0.0, 1.0)  # the aim pose straight ahead: the tip 4 cm forward of it
    tip_offset = (0.0, 0.0, -TIP_M)
    hover = sub(top, mul(inward, HOVER_M))
    pressed = add(top, mul(inward, PRESS_M))
    pulled = add(hinge, about(axis, SWING_DEG, sub(grip, hinge)))
    hz, seconds, ipd = 90.0, 16.0, 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)
    valid = (1 << 4) | (1 << 5)  # gripValid, aimValid
    look_button = mdr.head_orientation(27.0, -35.0)
    look_handle = mdr.head_orientation(0.0, 20.0)
    frames = []
    for i in range(int(seconds * hz)):
        t = i / hz
        squeeze = 0.0
        if t < 7.0:
            if t < 2.0:
                tip = hover
            elif t < 2.5:
                tip = lerp(hover, pressed, (t - 2.0) / 0.5)
            elif t < 6.5:
                tip = pressed
            else:
                tip = lerp(pressed, hover, (t - 6.5) / 0.5)
            hand = sub(tip, tip_offset)
            head = look_button
        else:
            if t < 8.0:
                hand = lerp(sub(hover, tip_offset), grip, (t - 7.0) / 1.0)
            elif t < 9.0:
                hand = grip
            else:
                hand = add(hinge, about(axis, SWING_DEG * max(0.0, min(1.0, t - 9.0)), sub(grip, hinge)))
            squeeze = 1.0 if 8.5 <= t < 14.0 else 0.0
            head = look_handle
        eyes = [(*head, *rotate(head, (side * ipd / 2, 0.0, 0.0))) for side in (-1, 1)]
        right = (*aim, *hand)
        left = (*aim, *LEFT_REST)
        frames.append(mdr.FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                                     valid, 0.0, 0.0, 0.0, 0.0, *left, *left,
                                     valid, 0.0, squeeze, 0.0, 0.0, *right, *right))
    with open(out, 'wb') as f:
        f.write(mdr.HEADER.pack(mdr.MAGIC, 1, mdr.HEADER.size, mdr.FRAME.size, 0, int(time.time() * 1000)))
        for frame in frames:
            f.write(frame)
    r = lambda v: tuple(round(x, 3) for x in v)  # noqa: E731
    print(f'RESULT {out}: {len(frames)} frames, {seconds:.0f} s; the tip from {r(hover)} to {r(pressed)}, the grip {r(grip)} '
          f'swung to {r(pulled)}')


if __name__ == '__main__':
    main()
