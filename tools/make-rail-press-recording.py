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
surface, rests 0.3 s, and goes back. Then: tools/replay.ps1 -Path out.mevrrec (poses only, no -Inputs).

Any quad, not only the rail: `yawDeg` may be a quaternion "qx,qy,qz,qw" (a snapshot layer's pose.orientation: a game screen on
the wrist faces the eyes, tilted, research/le2-menus.md "ME2's screens on the wrist"); its x, y and normal (+Z) are then the
quaternion's axes.

Shepard's drawn fingertip (research/le2-menus.md "The tap on Shepard's drawn arm"): once the mod sends his arm bones, the rail,
the game's screens and the wrist tap are touched by his drawn right index fingertip, which arm IK puts 11-20 cm from the
controller's point above, so presses aimed at the controller's point miss. Two flags, as make-wrist-recording.py's:
  --probe u:v   no taps or presses: from 2 s the right fingertip holds 8 cm in front of (u, v) to the end; a snapshot then
                records the drawn fingertip ("omniRail"."target"."tip"), the drawn target ("at") and the replayed right aim pose.
  --body FILE   such a snapshot's JSON: every press and tap moves the right aim pose by (the aim pose - the drawn fingertip),
                so the drawn fingertip comes where the controller's would have; a tap aims at the snapshot's drawn target.
                Probe near where you press (the offset changes across the reach)."""
import importlib.util
import json
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

args = list(sys.argv)
probe = None
body = None
if "--probe" in args:
    i = args.index("--probe")
    probe = tuple(float(x) for x in args[i + 1].split(':'))
    del args[i:i + 2]
if "--body" in args:
    i = args.index("--body")
    body = args[i + 1]
    del args[i:i + 2]
out = args[1]
seconds = float(args[2])
presses = []
taps = []
for item in args[3].split(','):
    if not item.strip() or probe is not None:
        continue
    parts = item.split(':')
    if parts[1] == 'tap':
        taps.append(float(parts[0]))
        continue
    t, u, v = (float(x) for x in parts)
    presses.append((t, u, v))
cx, cy, cz = (float(x) for x in args[4:7])
width, height = float(args[8]), float(args[9])
up_axis = (0.0, 1.0, 0.0)
if ',' in args[7]:
    # A quaternion (x, y, z, w): the quad's own axes.
    qx, qy, qz, qw = (float(x) for x in args[7].split(','))

    def rotate(v):
        # v + 2 q.xyz x (q.xyz x v + w v)
        tx = 2.0 * (qy * v[2] - qz * v[1])
        ty = 2.0 * (qz * v[0] - qx * v[2])
        tz = 2.0 * (qx * v[1] - qy * v[0])
        return (v[0] + qw * tx + (qy * tz - qz * ty), v[1] + qw * ty + (qz * tx - qx * tz), v[2] + qw * tz + (qx * ty - qy * tx))
    xaxis, up_axis, normal = rotate((1.0, 0.0, 0.0)), rotate((0.0, 1.0, 0.0)), rotate((0.0, 0.0, 1.0))
else:
    yaw = math.radians(float(args[7]))
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
    return tuple(c + x * a + y * up + depth * n for c, a, up, n in zip((cx, cy, cz), xaxis, up_axis, normal))


# --body: the drawn fingertip's offset from the right aim pose (a probe's snapshot), and its drawn target for the taps.
body_offset = None
body_target = None
if body is not None:
    with open(body, encoding="utf-8") as f:
        snap = json.load(f)
    target = (snap.get("omniRail") or {}).get("target") or {}
    aim = (((snap.get("controllers") or {}).get("right") or {}).get("aim") or {}).get("position")
    if not target.get("tip") or not target.get("at") or not aim:
        sys.exit(f"{body}: no omniRail.target.at/tip or controllers.right.aim.position (a snapshot taken while a --probe replayed)")
    body_offset = tuple(p - a for p, a in zip(target["tip"], aim))
    body_target = tuple(target["at"])
    print(f"{body}: the drawn fingertip {tuple(round(c, 3) for c in body_offset)} m from the right aim pose "
          f"({math.sqrt(sum(c * c for c in body_offset)) * 100:.1f} cm){'' if target.get('onBody') else ' (warning: onBody false)'}")


def aim_for(tip):
    if body_offset is not None:
        return tuple(t - o for t, o in zip(tip, body_offset))  # the drawn fingertip on `tip`
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
    tap_on = (target[0], target[1], target[2] + TIP_AHEAD) if body_target is None else aim_for(body_target)
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
    if probe is not None:
        # --probe u:v: 8 cm in front of the point from 2 s (once the wrist is up) to the end.
        front = aim_for(point(probe[0], probe[1], 0.08))
        d = t - 2.0
        if 0.0 <= d < 1.0:
            right = lerp(right_rest, front, smooth(d))
        elif d >= 1.0:
            right = front
    eyes =[(*head, side * ipd / 2, eye_h, 0.0) for side in (-1, 1)]
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
if probe is not None:
    print(f"probe: the right aim pose held at {tuple(round(c, 3) for c in aim_for(point(probe[0], probe[1], 0.08)))} from 3 s, 8 cm "
          f"in front of ({probe[0]:.2f}, {probe[1]:.2f}): snapshot it, then --body <the snapshot's .json>")
