# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (tools/mevrrec.py) for the omni-tool's rail standing alone on the left wrist (research/57,
research/le2-menus.md "The rail on ME2"): make-wrist-recording.py's player (the left wrist raised before the chest, the head
looking at it), with wrist taps and right-fingertip presses on points of the rail, at given times.

Usage: python tools/make-rail-press-recording.py out.mevrrec seconds "t:tap,t:u:v,..." railX railY railZ yawDeg width height
  "t:tap"    a tap on the wrist's target at t seconds (opens the rail; a second one closes it)
  "t:u:v"    a press at t seconds on the rail's document point (u, v: fractions of its 360 x 640 canvas, v down; with the
             game's art, Squad is about 0.53:0.15, Close 0.53:0.84: mevr-menu-preview --gameart's rail-alone.png)
  the rail:  its centre (app space, metres), its yaw in degrees (its normal toward the eyes is (sin yaw, 0, cos yaw)) and its
             size, from a snapshot's `omnirail` layer while this recording's wrist is raised (the fake headset's: centre
             -0.13 0 -0.29, yaw 24.15, 0.18 x 0.32 m).
The fingertip (the right aim pose's ray, 0.035 m ahead) comes along the rail's normal from 8 cm in front to 1 cm behind its
surface, rests 0.3 s, and goes back. Then: tools/replay.ps1 -Path out.mevrrec (poses only, no -Inputs)."""
import importlib.util
import math
import os
import sys
import time

_spec = importlib.util.spec_from_file_location("mwr", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                   "make-wrist-recording.py"))
_mwr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mwr)
MAGIC, HEADER, FRAME, IDENTITY = _mwr.MAGIC, _mwr.HEADER, _mwr.FRAME, _mwr.IDENTITY
WRIST_OFFSET, TIP_AHEAD, lerp, smooth, head_quat = _mwr.WRIST_OFFSET, _mwr.TIP_AHEAD, _mwr.lerp, _mwr.smooth, _mwr.head_quat

out = sys.argv[1]
seconds = float(sys.argv[2])
presses = []
taps = []
for item in sys.argv[3].split(','):
    parts = item.split(':')
    if parts[1] == 'tap':
        taps.append(float(parts[0]))
        continue
    t, u, v = (float(x) for x in parts)
    presses.append((t, u, v))
cx, cy, cz, yaw_deg, width, height = (float(x) for x in sys.argv[4:10])
yaw = math.radians(yaw_deg)
xaxis = (math.cos(yaw), 0.0, -math.sin(yaw))
normal = (math.sin(yaw), 0.0, math.cos(yaw))
eye_h, hz, ipd = 0.0, 90.0, 0.064
fov = (-0.785, 0.785, 0.785, -0.785)
left_rest = (-0.22, eye_h - 0.60, -0.15)
left_up = (-0.10, eye_h - 0.32, -0.34)
wrist_up = tuple(a + b for a, b in zip(left_up, WRIST_OFFSET))
right_rest = (0.22, eye_h - 0.60, -0.15)
flat = math.hypot(wrist_up[0], wrist_up[2])
hyaw = math.degrees(math.atan2(-wrist_up[0], -wrist_up[2]))
hpitch = math.degrees(math.atan2(wrist_up[1] - eye_h, flat))


def point(u, v, depth):
    x = (u - 0.5) * width
    y = (0.5 - v) * height
    return tuple(c + x * a + y * up + depth * n for c, a, up, n in zip((cx, cy, cz), xaxis, (0.0, 1.0, 0.0), normal))


def aim_for(tip):
    return (tip[0], tip[1], tip[2] + TIP_AHEAD)  # identity aim: the ray points -Z, the tip TIP_AHEAD along it


frames = []
n = int(seconds * hz)
for i in range(n):
    t = i / hz
    raise_f = smooth((t - 1.0) / 1.5)
    left = lerp(left_rest, left_up, raise_f)
    head = head_quat(hyaw * raise_f, hpitch * raise_f)
    right = right_rest
    target = (wrist_up[0], wrist_up[1] - 0.035, wrist_up[2])
    tap_on = (target[0], target[1], target[2] + TIP_AHEAD)
    tap_near = (tap_on[0] + 0.15, tap_on[1] - 0.03, tap_on[2])
    for tp in taps:
        d = t - tp
        if -1.0 <= d < -0.25:
            right = lerp(right_rest, tap_near, smooth((d + 1.0) / 0.75))
        elif -0.25 <= d < 0.0:
            right = lerp(tap_near, tap_on, smooth((d + 0.25) / 0.25))
        elif 0.0 <= d < 0.2:
            right = tap_on
        elif 0.2 <= d < 0.45:
            right = lerp(tap_on, tap_near, smooth((d - 0.2) / 0.25))
        elif 0.45 <= d < 1.2:
            right = lerp(tap_near, right_rest, smooth((d - 0.45) / 0.75))
    for (tp, u, v) in presses:
        d = t - tp
        front = aim_for(point(u, v, 0.08))
        on = aim_for(point(u, v, -0.01))
        if -1.0 <= d < -0.3:
            right = lerp(right_rest, front, smooth((d + 1.0) / 0.7))
        elif -0.3 <= d < 0.0:
            right = lerp(front, on, smooth((d + 0.3) / 0.3))
        elif 0.0 <= d < 0.3:
            right = on
        elif 0.3 <= d < 0.6:
            right = lerp(on, front, smooth((d - 0.3) / 0.3))
        elif 0.6 <= d < 1.4:
            right = lerp(front, right_rest, smooth((d - 0.6) / 0.8))
    eyes = [(*head, side * ipd / 2, eye_h, 0.0) for side in (-1, 1)]
    left_grip = (*IDENTITY, *left)
    left_aim = (*IDENTITY, left[0], left[1] + 0.05, left[2] - 0.02)
    right_grip = (*IDENTITY, right[0], right[1] - 0.05, right[2] + 0.02)
    right_aim = (*IDENTITY, *right)
    buttons = (1 << 4) | (1 << 5)
    frames.append(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov, buttons, 0.0, 0.0, 0.0, 0.0, *left_grip, *left_aim,
                             buttons, 0.0, 0.0, 0.0, 0.0, *right_grip, *right_aim))
with open(out, 'wb') as f:
    f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
    for fr in frames:
        f.write(fr)
for (tp, u, v) in presses:
    print(f"press at {tp} s on ({u:.2f}, {v:.2f}): fingertip at {tuple(round(c, 3) for c in point(u, v, 0.0))}")
