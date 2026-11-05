# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py): a standing player who raises the
left wrist in front of the chest as if reading a watch, looks down at it, and taps it with the right index fingertip at the
given times (research/56, the omni-tool: the tap opens the rail and the game's omni-tool, a second tap closes them). Between
taps the right hand rests low beside the body. For checking the omni-tool's wrist screens and rail on the fake headset.

Usage: python tools/make-wrist-recording.py out.mevrrec [seconds 60] [taps 4] [eye height 0] [pitch -30]
  taps:       the tap times in seconds, comma-separated ("4" opens once; "4,40" opens, then closes)
  eye height: the head's height in tracking space (0 on the fake headset, whose null driver puts the head at the origin)
  pitch:      the head's pitch while looking at the wrist, degrees (negative is down)
The tap (core/vr/OmniRail's Tap): the fingertip, 0.035 m along the right aim ray, comes from farther than 0.07 m to within
0.035 m of the wrist point (the left grip pose plus [Hud] wristOffset, -0.03 0.06 0.05 in its axes by default).
Then: tools/replay.ps1 -Path out.mevrrec -Loop (poses only; no -Inputs: no buttons), with [Pointer] setCursor = 0 so the
replayed rays never move the desktop's cursor; tools/snapshot.ps1 after the tap; tools/replay.ps1 -Stop."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)

IDENTITY = (0.0, 0.0, 0.0, 1.0)  # looking along -Z (OpenXR: -Z forward, +Y up, +X right)
WRIST_OFFSET = (-0.03, 0.06, 0.05)  # [Hud] wristOffset's default, in the left grip's axes (identity here)
TIP_AHEAD = 0.035  # plugin/OmniToolRail's kTipAheadM


def lerp(a, b, f):
    return tuple(x + (y - x) * f for x, y in zip(a, b))


def smooth(f):
    f = min(max(f, 0.0), 1.0)
    return f * f * (3.0 - 2.0 * f)


def pitch_quat(deg):
    half = math.radians(deg) / 2.0
    return (math.sin(half), 0.0, 0.0, math.cos(half))


def main():
    out = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
    taps = [float(t) for t in (sys.argv[3] if len(sys.argv) > 3 else "4").split(",") if t.strip()]
    eye_h = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
    pitch = float(sys.argv[5]) if len(sys.argv) > 5 else -30.0
    hz = 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    # The left hand: low at the side, then raised before the chest, the wrist's back up (a watch read).
    left_rest = (-0.22, eye_h - 0.60, -0.15)
    left_up = (-0.10, eye_h - 0.32, -0.34)
    wrist_up = tuple(a + b for a, b in zip(left_up, WRIST_OFFSET))
    # The right hand: low at the side; tapping, the aim pose sits TIP_AHEAD behind the wrist along -Z (identity: the ray
    # points -Z), so the fingertip is on the wrist point; it comes from 0.15 m to the right.
    right_rest = (0.22, eye_h - 0.60, -0.15)
    right_on = (wrist_up[0], wrist_up[1], wrist_up[2] + TIP_AHEAD)
    right_near = (right_on[0] + 0.15, right_on[1] - 0.03, right_on[2])
    frames = []
    n = int(seconds * hz)
    for i in range(n):
        t = i / hz
        raise_f = smooth((t - 1.0) / 1.5)  # the wrist comes up over 1-2.5 s and stays
        left = lerp(left_rest, left_up, raise_f)
        head = pitch_quat(pitch * raise_f)
        right = right_rest
        for tap in taps:
            # 1.0 s before the tap: over to near the wrist; 0.25 s in; 0.2 s on it; 0.25 s out; back to rest by +1.2 s.
            d = t - tap
            if -1.0 <= d < -0.25:
                right = lerp(right_rest, right_near, smooth((d + 1.0) / 0.75))
            elif -0.25 <= d < 0.0:
                right = lerp(right_near, right_on, smooth((d + 0.25) / 0.25))
            elif 0.0 <= d < 0.2:
                right = right_on
            elif 0.2 <= d < 0.45:
                right = lerp(right_on, right_near, smooth((d - 0.2) / 0.25))
            elif 0.45 <= d < 1.2:
                right = lerp(right_near, right_rest, smooth((d - 0.45) / 0.75))
        eyes = [(*head, side * ipd / 2, eye_h, 0.0) for side in (-1, 1)]
        left_grip = (*IDENTITY, *left)
        left_aim = (*IDENTITY, left[0], left[1] + 0.05, left[2] - 0.02)
        right_grip = (*IDENTITY, right[0], right[1] - 0.05, right[2] + 0.02)
        right_aim = (*IDENTITY, *right)
        buttons = (1 << 4) | (1 << 5)  # gripValid, aimValid
        frame = FRAME.pack(int(t * 1e9), 3,
                           *eyes[0], *fov, *eyes[1], *fov,
                           buttons, 0.0, 0.0, 0.0, 0.0, *left_grip, *left_aim,
                           buttons, 0.0, 0.0, 0.0, 0.0, *right_grip, *right_aim)
        frames.append(frame)
    header = HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000))
    with open(out, "wb") as f:
        f.write(header)
        for fr in frames:
            f.write(fr)
    print(f"{out}: {n} frames, {seconds} s at {hz} Hz; the left wrist up from 2.5 s at ({wrist_up[0]:.2f}, {wrist_up[1]:.2f}, "
          f"{wrist_up[2]:.2f}) m, the head pitched {pitch:.0f} degrees; taps at {', '.join(f'{t:g}' for t in taps)} s")


if __name__ == "__main__":
    main()
