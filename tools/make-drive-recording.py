# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) that drives the Mako: a seated
player looking ahead, both controllers resting at the seat, the left stick pushed forward, then steering left, then right, in a
loop of `seconds`. For the cockpit riding in the Mako on the fake headset (research/47 "Riding in the Mako"):
`tools/mako-start.ps1`, then `tools/replay.ps1 -Path out.mevrrec -Loop -Inputs` (the sticks reach the game as pad 0 until a
Mako control is grabbed).

    python tools/make-drive-recording.py out.mevrrec [seconds] [hz] [--press-b | --controls] [--eye-height M] [--look YAW PITCH]

--press-b: the sticks rest and the right controller's B is held from 1 s to 1.4 s instead (the game's TryExitVehicle).
--controls: the cockpit's own controls instead of the stick (research/46; needs no pad input, so it drives with the game window
unfocused too): both hands go to the lever and the stick (their places for a head entering at this recording's seat), squeeze at
1 s, the lever is pushed full forward by 2.5 s, the stick twisted 35 degrees one way from 4 s, the other from 8 s, back at 12 s.
Play it once (no -Loop): a loop lets go of both at its end. --eye-height: the seated head's height in tracking space (default
1.2; the controls are placed from the head when the Mako is entered, so use the head's height then: 0 on the fake headset).
--look: the head turned YAW degrees (+ right) and PITCH (+ up), e.g. to watch the controls' models (the hands don't turn).
--head-at R U F: the head moved R metres right, U up and F forward of the seat (the hands stay), e.g. `--head-at 0 0.25 0.55
--look 180 -35` to look back at the seated Shepard (research/47 "Shepard in the seat")."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)

# game/MakoControls' defaults ([Mako] leverOffset, stickOffset: right, up, forward from the head at entry) for a head looking
# down -Z: the controls' grab points from the head, in tracking space's axes.
LEVER = (-0.362, -0.446, -0.286)
STICK = (0.375, -0.415, -0.314)
THROTTLE_TRAVEL = 0.06  # a little past [Mako] throttleTravelM (0.055): full
TWIST_DEG = 35.0


def twist(degrees):
    """A rotation about the grip's -Z (OpenXR), xyzw: the stick's twist axis for a grip held this way."""
    half = math.radians(degrees) / 2.0
    return (0.0, 0.0, -math.sin(half), math.cos(half))


def head_orientation(yaw_deg, pitch_deg):
    """xyzw: turned yaw (+ right) about +Y, then pitched (+ up) about the turned +X (OpenXR: -Z ahead)."""
    y, p = math.radians(-yaw_deg) / 2.0, math.radians(pitch_deg) / 2.0
    qy = (0.0, math.sin(y), 0.0, math.cos(y))
    qp = (math.sin(p), 0.0, 0.0, math.cos(p))
    return (qy[3] * qp[0] + qy[0] * qp[3] + qy[1] * qp[2] - qy[2] * qp[1],
            qy[3] * qp[1] - qy[0] * qp[2] + qy[1] * qp[3] + qy[2] * qp[0],
            qy[3] * qp[2] + qy[0] * qp[1] - qy[1] * qp[0] + qy[2] * qp[3],
            qy[3] * qp[3] - qy[0] * qp[0] - qy[1] * qp[1] - qy[2] * qp[2])


def main():
    press_b = "--press-b" in sys.argv
    controls = "--controls" in sys.argv
    eye_h = 1.20
    if "--eye-height" in sys.argv:
        eye_h = float(sys.argv[sys.argv.index("--eye-height") + 1])
    look = (0.0, 0.0)
    if "--look" in sys.argv:
        at = sys.argv.index("--look")
        look = (float(sys.argv[at + 1]), float(sys.argv[at + 2]))
    head_at = (0.0, 0.0, 0.0)
    if "--head-at" in sys.argv:
        at = sys.argv.index("--head-at")
        head_at = tuple(float(v) for v in sys.argv[at + 1:at + 4])
    taken = ({i + 1 for i, a in enumerate(sys.argv) if a == "--eye-height"} | {i + k for i, a in enumerate(sys.argv) if a == "--look" for k in (1, 2)} |
             {i + k for i, a in enumerate(sys.argv) if a == "--head-at" for k in (1, 2, 3)})
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and i not in taken]
    out = args[0]
    seconds = float(args[1]) if len(args) > 1 else (16.0 if controls else 12.0)
    hz = float(args[2]) if len(args) > 2 else 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    q = (0.0, 0.0, 0.0, 1.0)  # the hands: pointing ahead
    head = head_orientation(*look)
    right_axis = (math.cos(math.radians(look[0])), 0.0, math.sin(math.radians(look[0])))  # the head's right, laid flat
    eyes = [(*head, head_at[0] + side * ipd / 2 * right_axis[0], eye_h + head_at[1], -head_at[2] + side * ipd / 2 * right_axis[2])
            for side in (-1, 1)]
    valid = (1 << 4) | (1 << 5)  # gripValid, aimValid
    frames = []
    n = int(seconds * hz)
    for i in range(n):
        t = i / hz
        right_buttons = valid
        left_sq = right_sq = 0.0
        steer, push = 0.0, 0.0
        if controls:
            forward = THROTTLE_TRAVEL * min(max((t - 1.5) / 1.0, 0.0), 1.0)
            left = (*q, LEVER[0], eye_h + LEVER[1], LEVER[2] - forward)
            angle = TWIST_DEG if 4.0 <= t < 8.0 else -TWIST_DEG if 8.0 <= t < 12.0 else 0.0
            right = (*twist(angle), STICK[0], eye_h + STICK[1], STICK[2])
            left_sq = right_sq = 1.0 if t >= 1.0 else 0.0
        else:
            # Hands on the knees, pointing ahead (OpenXR: -Z forward), away from the lever and the stick's grab points.
            left = (*q, -0.20, eye_h - 0.45, -0.35)
            right = (*q, 0.20, eye_h - 0.45, -0.35)
            phase = t / seconds
            steer = 0.0 if phase < 1 / 3 else -0.6 if phase < 2 / 3 else 0.6
            push = 1.0
            if press_b:
                steer, push = 0.0, 0.0
                if 1.0 <= t < 1.4:
                    right_buttons |= 1 << 2  # secondary: B
        frame = FRAME.pack(int(t * 1e9), 3,
                           *eyes[0], *fov, *eyes[1], *fov,
                           valid, 0.0, left_sq, steer, push, *left, *left,  # left: trigger, squeeze, stick x, stick y
                           right_buttons, 0.0, right_sq, 0.0, 0.0, *right, *right)
        frames.append(frame)
    header = HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000))
    with open(out, "wb") as f:
        f.write(header)
        for fr in frames:
            f.write(fr)
    print(f"{out}: {n} frames, {seconds} s at {hz} Hz")


if __name__ == "__main__":
    main()
