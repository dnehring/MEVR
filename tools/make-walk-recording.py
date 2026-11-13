# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) of a player standing still in the
room and walking Shepard with the left stick: forward, strafing right, backing up, strafing left, then standing, each for
`segment` seconds, in a loop. The head looks ahead (or turned --yaw degrees in the room, as after turning in place) and
can look down (--pitch) so the eye snapshots show the legs; both hands are held in front of the chest (--hands view: up in
front of the eyes, inside the view, so every eye snapshot shows Shepard's drawn hands where the controllers are: the audit's
check of the body's place, research/69 "Seeing the body in the audit"). For the legs
keeping the run (research/29 "The legs keep the run"): replay it with its inputs on the fake headset,

    python tools/make-walk-recording.py out.mevrrec [segment] [hz] [--yaw DEG] [--pitch DEG] [--eye-height M] [--stick S] [--grip]
        [--plan forward:14,stand:600] [--hands chest|view]
    tools/replay.ps1 -Path out.mevrrec -Loop -Inputs

segment: seconds per stick direction (default 3). --yaw: the head (and the hands) turned DEG degrees right in the room.
--pitch: the head pitched DEG degrees (+ up; -60 shows the legs). --eye-height: the head's height in tracking space
(default 0: the fake headset's floor is at the head). --stick: the stick's deflection (default 0.9, a run: above the
game's 0.70). --grip: the right grip held all the way (a gun drawn with `mevr weapon pistol` stays in the hand). --plan: the
segments by name and seconds instead of the loop of five (forward, strafe right, back, strafe left, stand): `stand:600` is a
player standing with tracked hands (an audit run's hands, tools/conversation-audit.ps1); `forward:14,stand:600` one who walks
with a scripted walk and keeps the stick pushed through its cut (the council hearing, research/69 "The body that stayed at the
mark"). --hands: chest (default, 0.35 m under the eyes and 0.30 m ahead: below a 45-degree view) or view (0.20 m under
the eyes and 0.40 m ahead, 0.15 m to each side: 27 degrees down, 21 to the side)."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)

# The left stick per segment (x right, y forward).
SEGMENTS = [("forward", 0.0, 1.0), ("strafe right", 1.0, 0.0), ("back", 0.0, -1.0), ("strafe left", -1.0, 0.0),
            ("stand", 0.0, 0.0)]


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
    yaw = option("--yaw", 0.0)
    pitch = option("--pitch", 0.0)
    eye_h = option("--eye-height", 0.0)
    stick = option("--stick", 0.9)
    grip = 1.0 if "--grip" in sys.argv else 0.0
    hands = sys.argv[sys.argv.index("--hands") + 1] if "--hands" in sys.argv else "chest"
    if hands not in ("chest", "view"):
        print("--hands chest|view")
        return 2
    taken = {i + 1 for i, a in enumerate(sys.argv) if a in ("--yaw", "--pitch", "--eye-height", "--stick", "--plan", "--hands")}
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and i not in taken]
    if not args:
        print(__doc__)
        return 2
    out = args[0]
    segment = float(args[1]) if len(args) > 1 else 3.0
    hz = float(args[2]) if len(args) > 2 else 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    head = head_orientation(yaw, pitch)
    hand_q = head_orientation(yaw, 0.0)  # pointing where the body faces
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    right = (c, 0.0, s)       # the head's right, laid flat (tracking: +X right, -Z ahead)
    ahead = (s, 0.0, -c)

    def at(r, u, f):
        return (r * right[0] + f * ahead[0], eye_h + u, r * right[2] + f * ahead[2])

    eyes = [(*head, side * ipd / 2 * right[0], eye_h, side * ipd / 2 * right[2]) for side in (-1, 1)]
    down, ahead_m = (0.20, 0.40) if hands == "view" else (0.35, 0.30)
    left_hand = (*hand_q, *at(-0.15, -down, ahead_m))
    right_hand = (*hand_q, *at(0.15, -down, ahead_m))
    valid = (1 << 4) | (1 << 5)  # gripValid, aimValid
    by_name = {name: (name, sx, sy) for name, sx, sy in SEGMENTS}
    if "--plan" in sys.argv:
        plan_text = sys.argv[sys.argv.index("--plan") + 1]
        steps = [(by_name[name.strip()], float(seconds)) for name, seconds in (part.split(":") for part in plan_text.split(","))]
    else:
        steps = [(seg, segment) for seg in SEGMENTS]
    frames = []  # the segment of each frame
    for seg, seconds in steps:
        frames += [seg] * int(seconds * hz)
    n = len(frames)
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for i in range(n):
            t = i / hz
            _, sx, sy = frames[i]
            f.write(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                               valid, 0.0, 0.0, sx * stick, sy * stick, *left_hand, *left_hand,
                               valid, 0.0, grip, 0.0, 0.0, *right_hand, *right_hand))
    starts = [sum(seconds for _, seconds in steps[:k]) for k in range(len(steps))]
    plan = ", ".join(f"{seg[0]} {start:g}-{start + seconds:g} s" for (seg, seconds), start in zip(steps, starts))
    print(f"{out}: {n} frames at {hz:g} Hz: {plan}; head yaw {yaw:g}, pitch {pitch:g}, stick {stick:g}, hands {hands}"
          f"{', right grip held' if grip else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
