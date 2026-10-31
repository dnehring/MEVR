# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py): a standing player holding a long gun
at the eye for N seconds (ADS: the right aim pose ahead of the right eye along -Z, the left hand on the foregrip), with a slow
head sway so Umbra and the blur see motion. For the scope-capture A/B on the fake headset (research/39, research/35 "Measuring
without a person"). Usage: python tools/make-ads-recording.py out.mevrrec [seconds] [hz]; then `tools/console.ps1 "mevr weapon sniper"`,
`tools/replay.ps1 -Path out.mevrrec -Loop -Inputs`, `tools/measure-levers.ps1 -Section Sights -Key scopeCapture -Values 0,1`."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)


def quat_yaw_pitch(yaw, pitch):
    # xyzw; yaw about +Y, pitch about +X (OpenXR: -Z forward, +Y up)
    cy, sy = math.cos(yaw / 2), math.sin(yaw / 2)
    cp, sp = math.cos(pitch / 2), math.sin(pitch / 2)
    # q = qyaw * qpitch
    return (cy * sp, sy * cp, -sy * sp, cy * cp)


def main():
    out = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    hz = float(sys.argv[3]) if len(sys.argv) > 3 else 90.0
    eye_h = 1.65
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    frames = []
    n = int(seconds * hz)
    for i in range(n):
        t = i / hz
        yaw = math.radians(3.0) * math.sin(2 * math.pi * t / 8.0)
        pitch = math.radians(1.0) * math.sin(2 * math.pi * t / 5.0)
        q = quat_yaw_pitch(yaw, pitch)
        # forward and right in the world for this head
        fx, fz = -math.sin(yaw), -math.cos(yaw)
        rx, rz = math.cos(yaw), -math.sin(yaw)
        hx, hy, hz_ = 0.0, eye_h, 0.0
        eyes = []
        for side in (-1, 1):
            ex, ey, ez = hx + side * ipd / 2 * rx, hy, hz_ + side * ipd / 2 * rz
            eyes.append((q[0], q[1], q[2], q[3], ex, ey, ez))
        # right aim pose: 0.30 m ahead of the RIGHT eye, 1.5 cm below, pointing forward (the sniper's eyepiece near the eye)
        ax = hx + ipd / 2 * rx + 0.30 * fx
        az = hz_ + ipd / 2 * rz + 0.30 * fz
        ay = hy - 0.015
        right_aim = (q[0], q[1], q[2], q[3], ax, ay, az)
        right_grip = (q[0], q[1], q[2], q[3], ax, ay - 0.06, az)
        # left hand on the foregrip 0.25 m further along the barrel
        lx, lz = ax + 0.25 * fx, az + 0.25 * fz
        left_aim = (q[0], q[1], q[2], q[3], lx, ay - 0.02, lz)
        left_grip = (q[0], q[1], q[2], q[3], lx, ay - 0.07, lz)
        buttons = (1 << 4) | (1 << 5)  # gripValid, aimValid
        left_sq = 1.0 if t > 2.0 else 0.0  # the support grip after 2 s
        right_sq = 1.0  # the right grip holds the gun
        frame = FRAME.pack(int(t * 1e9), 3,
                           *eyes[0], *fov, *eyes[1], *fov,
                           buttons, 0.0, left_sq, 0.0, 0.0, *left_grip, *left_aim,
                           buttons, 0.0, right_sq, 0.0, 0.0, *right_grip, *right_aim)
        frames.append(frame)
    header = HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000))
    with open(out, "wb") as f:
        f.write(header)
        for fr in frames:
            f.write(fr)
    print(f"{out}: {n} frames, {seconds} s at {hz} Hz")


if __name__ == "__main__":
    main()
