# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py): a standing player holding a pistol
ahead of the chest who racks it with the left hand every `period` seconds (research/45 section 6, the vent's visible rack):
the left hand rests beside the gun, comes over the receiver with the grip open, squeezes, pulls back along the gun fast and
lets go, then rests again. For checking the rack on the fake headset without a person.

Usage: python tools/make-rack-recording.py out.mevrrec [seconds 30] [period 6] [eye height 0] [along 0.04] [up 0.05]
  eye height: the head's height in tracking space (0 on the fake headset: the null driver recentres with the head at the
              origin; 1.65 for a standing player's recording, as tools/make-ads-recording.py says)
  along, up:  where the left grip pose takes hold, meters from the right grip pose along the barrel (+ toward the muzzle)
              and above it. The receiver zone is measured from the hand BONES ([Vent] zone*), so if the snapshot's
              "mod"."vent" onGun says the hand stops outside it (inZone false), move these by the difference.
Then: tools/console.ps1 "mevr weapon pistol", "mevr vent heat 0.9 hold"; tools/replay.ps1 -Path out.mevrrec -Loop -Inputs;
the log's `vent: VENTED` and `vent: rack: the pistol's '...' went N units back` lines; snapshots during a stroke
(tools/snapshot.ps1 -Count 6 -Interval 0.05) show the part back; `mevr vent heat off` and tools/replay.ps1 -Stop after."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)

IDENTITY = (0.0, 0.0, 0.0, 1.0)  # looking along -Z (OpenXR: -Z forward, +Y up, +X right)


def lerp(a, b, f):
    return tuple(x + (y - x) * f for x, y in zip(a, b))


def smooth(f):
    f = min(max(f, 0.0), 1.0)
    return f * f * (3.0 - 2.0 * f)


def left_hand(phase, rest, take, back):
    """The left hand's grip position and squeeze at `phase` seconds into a stroke's period."""
    # 0.0-0.6 rest beside the gun, grip open; 0.6-1.1 over to the receiver; 1.1-1.6 there, open (the vent's mask goes up);
    # 1.6 squeeze; 1.75-1.83 the rack: back along the gun 0.12 m in 80 ms (1.5 m/s), let go at its end; 1.83-2.4 back to
    # rest, open.
    if phase < 0.6:
        return rest, 0.0
    if phase < 1.1:
        return lerp(rest, take, smooth((phase - 0.6) / 0.5)), 0.0
    if phase < 1.6:
        return take, 0.0
    if phase < 1.75:
        return take, 1.0
    if phase < 1.83:
        return lerp(take, back, (phase - 1.75) / 0.08), 1.0
    if phase < 2.4:
        return lerp(back, rest, smooth((phase - 1.83) / 0.57)), 0.0
    return rest, 0.0


def main():
    out = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    period = float(sys.argv[3]) if len(sys.argv) > 3 else 6.0  # longer than [Vent] ventCooldownS (4)
    eye_h = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
    along = float(sys.argv[5]) if len(sys.argv) > 5 else 0.04
    up = float(sys.argv[6]) if len(sys.argv) > 6 else 0.05
    hz = 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    # The pistol ahead of the chest, pointing forward: the right aim pose, its grip pose 6 cm under it.
    right_aim = (0.12, eye_h - 0.32, -0.42)
    right_grip = (right_aim[0], right_aim[1] - 0.06, right_aim[2])
    # The left hand: at rest low beside the body; taking hold over the receiver (forward is -Z); after the rack 0.12 m back.
    rest = (-0.22, eye_h - 0.55, -0.20)
    take = (right_grip[0], right_grip[1] + up, right_grip[2] - along)
    back = (take[0], take[1] + 0.01, take[2] + 0.12)
    frames = []
    n = int(seconds * hz)
    for i in range(n):
        t = i / hz
        eyes = [(*IDENTITY, side * ipd / 2, eye_h, 0.0) for side in (-1, 1)]
        grip, squeeze = left_hand(t % period, rest, take, back)
        left_grip = (*IDENTITY, *grip)
        left_aim = (*IDENTITY, grip[0], grip[1] + 0.05, grip[2] - 0.02)
        buttons = (1 << 4) | (1 << 5)  # gripValid, aimValid
        frame = FRAME.pack(int(t * 1e9), 3,
                           *eyes[0], *fov, *eyes[1], *fov,
                           buttons, 0.0, squeeze, 0.0, 0.0, *left_grip, *left_aim,
                           buttons, 0.0, 1.0, 0.0, 0.0, *IDENTITY, *right_grip, *IDENTITY, *right_aim)
        frames.append(frame)
    header = HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000))
    with open(out, "wb") as f:
        f.write(header)
        for fr in frames:
            f.write(fr)
    print(f"{out}: {n} frames, {seconds} s at {hz} Hz, a rack every {period} s (left hand takes hold {along} m ahead of and "
          f"{up} m over the right grip pose)")


if __name__ == "__main__":
    main()
