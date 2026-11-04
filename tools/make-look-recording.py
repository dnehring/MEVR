# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) of a player standing still and
looking around: the head turns a full circle at a steady rate (or sweeps between two yaws), level or pitched, the hands
hanging at the sides. For measuring a place on the fake headset whatever way the pawn faces (research/50, the crowd
survey): replayed in a loop, every direction is in view for the same share of a probe of `seconds` (one turn).

    python tools/make-look-recording.py out.mevrrec [seconds] [hz] [--sweep YAW] [--pitch DEG] [--eye-height M]

seconds: one full turn (default 20). --sweep YAW: back and forth between -YAW and +YAW degrees instead of a full turn (one
there-and-back per `seconds`). --pitch: the head pitched DEG degrees (+ up). --eye-height: the head's height in tracking
space (default 0: the fake headset's floor is at the head)."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)


def head_orientation(yaw_deg, pitch_deg):
    """xyzw: turned yaw (+ right) about +Y, then pitched (+ up) about the turned +X (OpenXR: -Z ahead)."""
    y, p = math.radians(-yaw_deg) / 2.0, math.radians(pitch_deg) / 2.0
    qy = (0.0, math.sin(y), 0.0, math.cos(y))
    qp = (math.sin(p), 0.0, 0.0, math.cos(p))
    return (qy[3] * qp[0] + qy[0] * qp[3] + qy[1] * qp[2] - qy[2] * qp[1],
            qy[3] * qp[1] - qy[0] * qp[2] + qy[1] * qp[3] + qy[2] * qp[0],
            qy[3] * qp[2] + qy[0] * qp[1] - qy[1] * qp[0] + qy[2] * qp[3],
            qy[3] * qp[3] - qy[0] * qp[0] - qy[1] * qp[1] - qy[2] * qp[2])


def option(name, default):
    return float(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def main():
    sweep = option("--sweep", 0.0)
    pitch = option("--pitch", 0.0)
    eye_h = option("--eye-height", 0.0)
    taken = {i + 1 for i, a in enumerate(sys.argv) if a in ("--sweep", "--pitch", "--eye-height")}
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and i not in taken]
    if not args:
        print(__doc__)
        return 2
    out = args[0]
    seconds = float(args[1]) if len(args) > 1 else 20.0
    hz = float(args[2]) if len(args) > 2 else 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    hand_q = (0.0, 0.0, 0.0, 1.0)  # pointing ahead
    valid = (1 << 4) | (1 << 5)  # gripValid, aimValid
    n = int(seconds * hz)
    frames = []
    for i in range(n):
        t = i / hz
        phase = t / seconds
        yaw = sweep * math.sin(2.0 * math.pi * phase) if sweep > 0.0 else 360.0 * phase
        head = head_orientation(yaw, pitch)
        right = (math.cos(math.radians(yaw)), 0.0, math.sin(math.radians(yaw)))  # the head's right, laid flat
        eyes = [(*head, side * ipd / 2 * right[0], eye_h, side * ipd / 2 * right[2]) for side in (-1, 1)]
        # The hands hang at the sides of a body facing the recording's start (they don't follow the head).
        left = (*hand_q, -0.22, eye_h - 0.75, -0.05)
        right_hand = (*hand_q, 0.22, eye_h - 0.75, -0.05)
        frames.append(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                                 valid, 0.0, 0.0, 0.0, 0.0, *left, *left,
                                 valid, 0.0, 0.0, 0.0, 0.0, *right_hand, *right_hand))
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for frame in frames:
            f.write(frame)
    print(f"{out}: {n} frames, {seconds} s at {hz} Hz, {'sweep +-%g deg' % sweep if sweep > 0 else 'a full turn'}, pitch {pitch:g}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
