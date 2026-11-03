# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) that drives the Mako with the left
stick: a seated player looking ahead, both controllers resting at the seat, the stick pushed forward, then steering left, then
right, in a loop of `seconds`. For the cockpit riding in the Mako on the fake headset (research/47 "Riding in the Mako"):
`tools/mako-start.ps1`, then `tools/replay.ps1 -Path out.mevrrec -Loop -Inputs` (the sticks reach the game as pad 0 until a
Mako control is grabbed). Usage: python tools/make-drive-recording.py out.mevrrec [seconds] [hz]."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)


def main():
    out = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0
    hz = float(sys.argv[3]) if len(sys.argv) > 3 else 90.0
    eye_h = 1.20  # seated
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    q = (0.0, 0.0, 0.0, 1.0)  # looking straight ahead
    eyes = [(*q, side * ipd / 2, eye_h, 0.0) for side in (-1, 1)]
    # Hands on the knees, pointing ahead (OpenXR: -Z forward), away from the lever and the stick's grab points.
    left = (*q, -0.20, 0.75, -0.35)
    right = (*q, 0.20, 0.75, -0.35)
    valid = (1 << 4) | (1 << 5)  # gripValid, aimValid
    frames = []
    n = int(seconds * hz)
    for i in range(n):
        t = i / hz
        phase = t / seconds
        steer = 0.0 if phase < 1 / 3 else -0.6 if phase < 2 / 3 else 0.6
        frame = FRAME.pack(int(t * 1e9), 3,
                           *eyes[0], *fov, *eyes[1], *fov,
                           valid, 0.0, 0.0, steer, 1.0, *left, *left,  # left: trigger, squeeze, stick x, stick y
                           valid, 0.0, 0.0, 0.0, 0.0, *right, *right)
        frames.append(frame)
    header = HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000))
    with open(out, "wb") as f:
        f.write(header)
        for fr in frames:
            f.write(fr)
    print(f"{out}: {n} frames, {seconds} s at {hz} Hz")


if __name__ == "__main__":
    main()
