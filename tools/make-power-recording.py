# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) for the hand-cast powers and the squad's
placement by an arc on the fake headset (research/55, research/54 "Placement by an arc"): the power wheel at the left hand
picked by the hand's travel or the left stick, a self-cast gesture (the open left hand on a snapshot's gesture target), the lock
and the push, and the right B's arc with the squad wheel at the right hand. Every game takes the same recording.

    python tools/make-power-recording.py out.mevrrec [--seconds S] [--yaw DEG] EVENT ...
    tools/replay.ps1 -Path out.mevrrec -Inputs

The head stands at the origin looking straight ahead (OpenXR: -Z), the eyes 64 mm apart; the left hand rests at
(-0.20, -0.15, -0.37) m and the right at (0.15, -0.34, -0.31), both open, tracked, sticks centred. Times are seconds.
EVENTs (repeatable, any order):
  x:A:B               the left X (primary) held from A to B (a hold under the wheel's tapS 0.30 s is a tap: the wheel stays)
  wheel:A:B:TILE:N    X held from A to B and the left hand moved 0.07 m toward tile TILE of N (power_wheel::Angles: N entries
                      on the wheel, its last "No power") between A+0.3 and A+0.7, back to rest after B
  stick:A:B:TILE:N    X held from A to B and the left stick pushed (0.95) toward that tile between A+0.3 and B-0.1
  ltrigger:A:B        the left trigger pulled from A to B (the tapped wheel's pinch)
  lhand:A:B:X,Y,Z     the left hand moved from where it is to X,Y,Z (tracking space, metres) between A and B, and kept there;
                      a gesture target from a snapshot's mod.powerWheel.gestureTarget (A-B 0.7 s: a dwell; 0.13 s: a slap)
  lrest:A:B           the left hand back to its rest between A and B
  push:A              the left hand pushed 0.28 m along its ray (and 0.10 m in) in 0.115 s from A (2.6 m/s): the lock's push
  lgrip:A:B           the left grip squeezed from A to B
  rb:A:B              the right B (secondary) held from A to B: the squad's placement arc
  rhand:A:B:X,Y,Z     the right hand moved to X,Y,Z between A and B, and kept there (the squad wheel picks by its travel:
                      2.5 cm lights a tile, 7 cm picks it; left = the first squadmate, up = the squad, right = the second)
  rpitch:A:B:DEG      the right hand's aim (and grip) pitched to DEG (negative: down) between A and B, kept: where the arc lands
  lpitch:A:B:DEG      the same for the left hand (the flat squad order's ray on the floor)
  rrest:A:B           the right hand back to its rest and level between A and B
  walk:A:B:X,Y        the left stick held at X,Y (-1..1; Y forward) from A to B: Shepard walks (with the wheel closed)
  a:A:B               the right A (primary) held from A to B: the game's use
--yaw DEG turns the whole recording (head and hands) about the vertical axis by DEG (positive: to the left), to face a target
the view's yaw doesn't. --seconds: the length (default 12)."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)
HZ = 90.0
LEFT_REST = (-0.20, -0.15, -0.37)
RIGHT_REST = (0.15, -0.34, -0.31)
VALID = (1 << 4) | (1 << 5)  # gripValid, aimValid
PRIMARY, SECONDARY = 1 << 1, 1 << 2


def angles(count):
    """core/vr/PowerWheel's Angles() with its LayoutSettings defaults: degrees counter-clockwise from +x, entry 0 first."""
    gap_centre, gap, min_step, max_step = -122.5, 107.0, 34.0, 52.0
    if count <= 0:
        return []
    step = 0.0 if count <= 1 else max(min_step, min(max_step, (360.0 - gap) / (count - 1)))
    if step * count > 360.0:
        step = 360.0 / count  # a full ring, no gap left
    centre = gap_centre + 180.0
    out = []
    for i in range(count):
        a = centre + step * ((count - 1) * 0.5 - i)
        while a > 180.0:
            a -= 360.0
        while a <= -180.0:
            a += 360.0
        out.append(a)
    return out


def lerp(a, b, w):
    return tuple(x + (y - x) * w for x, y in zip(a, b))


def weight(t, a, b):
    if b <= a:
        return 1.0 if t >= a else 0.0
    return min(max((t - a) / (b - a), 0.0), 1.0)


def quat_yaw_pitch(yaw, pitch):
    """A quaternion (xyzw) turning by yaw about +Y, then pitch about the turned +X (OpenXR: -Z forward)."""
    cy, sy = math.cos(yaw / 2), math.sin(yaw / 2)
    cp, sp = math.cos(pitch / 2), math.sin(pitch / 2)
    # q = q_yaw * q_pitch
    return (cy * sp, sy * cp, -sy * sp, cy * cp)


def turn(p, yaw):
    """A point turned about +Y by yaw (radians, positive to the left: -Z toward -X)."""
    c, s = math.cos(yaw), math.sin(yaw)
    return (c * p[0] + s * p[2], p[1], -s * p[0] + c * p[2])


def parse(args):
    seconds, yaw = 12.0, 0.0
    events = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--seconds":
            seconds = float(args[i + 1])
            i += 2
            continue
        if a == "--yaw":
            yaw = float(args[i + 1])
            i += 2
            continue
        parts = a.split(":")
        kind = parts[0]
        nums = parts[1:]
        if kind in ("x", "ltrigger", "lgrip", "rb", "lrest", "rrest", "a") and len(nums) == 2:
            events.append((kind, float(nums[0]), float(nums[1])))
        elif kind in ("wheel", "stick") and len(nums) == 4:
            events.append((kind, float(nums[0]), float(nums[1]), int(nums[2]), int(nums[3])))
        elif kind in ("lhand", "rhand", "walk") and len(nums) == 3:
            events.append((kind, float(nums[0]), float(nums[1]), tuple(float(v) for v in nums[2].split(","))))
        elif kind in ("rpitch", "lpitch") and len(nums) == 3:
            events.append((kind, float(nums[0]), float(nums[1]), float(nums[2])))
        elif kind == "push" and len(nums) == 1:
            events.append((kind, float(nums[0])))
        else:
            raise SystemExit(f"can't read {a!r} (see --help)")
        i += 1
    return seconds, yaw, events


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help") or args[0].startswith("--"):
        print(__doc__)
        return 2
    out = args[0]
    seconds, yaw_deg, events = parse(args[1:])
    yaw = math.radians(yaw_deg)
    fov = (-0.785, 0.785, 0.785, -0.785)
    n = int(seconds * HZ)
    # Positions are keyframed per hand: each moving event starts from wherever the hand was at its start.
    ordered = sorted(events, key=lambda e: e[1])
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for k in range(n):
            t = k / HZ
            left, right = LEFT_REST, RIGHT_REST
            right_pitch = left_pitch = 0.0
            lb = rb = VALID
            ltrig = lgrip = 0.0
            sx = sy = 0.0
            for e in ordered:
                kind, start = e[0], e[1]
                if kind == "x" and start <= t < e[2]:
                    lb |= PRIMARY
                elif kind == "ltrigger" and start <= t < e[2]:
                    ltrig = 1.0
                elif kind == "lgrip" and start <= t < e[2]:
                    lgrip = 1.0
                elif kind == "rb" and start <= t < e[2]:
                    rb |= SECONDARY
                elif kind == "a" and start <= t < e[2]:
                    rb |= PRIMARY
                elif kind == "walk" and start <= t < e[2]:
                    sx, sy = e[3][0], e[3][1]
                elif kind == "wheel":
                    end, tile, count = e[2], e[3], e[4]
                    if start <= t < end:
                        lb |= PRIMARY
                    a = math.radians(angles(count)[tile])
                    target = (LEFT_REST[0] + 0.07 * math.cos(a), LEFT_REST[1] + 0.07 * math.sin(a), LEFT_REST[2])
                    if t >= start:
                        left = lerp(left, target, weight(t, start + 0.3, start + 0.7))
                    if t >= end:
                        left = lerp(left, LEFT_REST, weight(t, end + 0.2, end + 0.6))
                elif kind == "stick":
                    end, tile, count = e[2], e[3], e[4]
                    if start <= t < end:
                        lb |= PRIMARY
                    if start + 0.3 <= t < end - 0.1:
                        a = math.radians(angles(count)[tile])
                        sx, sy = 0.95 * math.cos(a), 0.95 * math.sin(a)
                elif kind == "lhand" and t >= start:
                    left = lerp(left, e[3], weight(t, start, e[2]))
                elif kind == "lrest" and t >= start:
                    left = lerp(left, LEFT_REST, weight(t, start, e[2]))
                elif kind == "push" and t >= start:
                    left = lerp(left, (left[0] + 0.10, left[1], left[2] - 0.28), weight(t, start, start + 0.115))
                elif kind == "rhand" and t >= start:
                    right = lerp(right, e[3], weight(t, start, e[2]))
                elif kind == "rrest" and t >= start:
                    w = weight(t, start, e[2])
                    right = lerp(right, RIGHT_REST, w)
                    right_pitch = right_pitch * (1.0 - w)
                elif kind == "rpitch" and t >= start:
                    right_pitch = right_pitch + (math.radians(e[3]) - right_pitch) * weight(t, start, e[2])
                elif kind == "lpitch" and t >= start:
                    left_pitch = left_pitch + (math.radians(e[3]) - left_pitch) * weight(t, start, e[2])
            head_q = quat_yaw_pitch(yaw, 0.0)
            eyes = [(*head_q, *turn((side * 0.032, 0.0, 0.0), yaw)) for side in (-1, 1)]
            lpose = (*quat_yaw_pitch(yaw, left_pitch), *turn(left, yaw))
            rpose = (*quat_yaw_pitch(yaw, right_pitch), *turn(right, yaw))
            f.write(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                               lb, ltrig, lgrip, sx, sy, *lpose, *lpose,
                               rb, 0.0, 0.0, 0.0, 0.0, *rpose, *rpose))
    print(f"{out}: {n} frames at {HZ:g} Hz ({seconds:g} s), turned {yaw_deg:g} deg; events: "
          + ", ".join(":".join(str(v) for v in e) for e in events))
    return 0


if __name__ == "__main__":
    sys.exit(main())
