# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) of a player standing still with tracked
hands and pressing the right controller's A: the vault's double A (research/le2-traversal.md), or a single A to compare. Replay it
with its inputs on the fake headset after placing Shepard at an obstacle (mevr traversal place ...):

    python tools/make-vault-recording.py out.mevrrec [--start S] [--taps N] [--every S] [--gap S] [--tap S] [--single] [--length S]
        [--hands chest|view] [--grip] [--support [--support-release [--regrab S]]] [--trigger] [--yaw DEG] [--pitch DEG]
    tools/replay.ps1 -Path out.mevrrec -Inputs

--start: the first double tap's first press, seconds from the replay's start (default 3). --taps: double taps (default 1), each
--every seconds after the one before (default 6: a vault lasts about a second, and the player stands again). --gap: between the
first press's release and the second press (default 0.15 s; the mod's double tap counts a second press within [Traversal]
doubleTapS, 0.45 s, of the first). --tap: how long each press is held (0.08 s). --single: one press instead of two (the native
single A: nothing of ours). --length: the recording's length (default: 3 s after the last tap). --hands: as tools/make-walk-recording.py
(chest default, view: up in front of the eyes). --grip: the right grip held all the way (on ME2 today it is the pad's RB, the
power wheel after 0.3 s without the holsters' gripHolds). --support: the left hand on the held gun (5 cm ahead of the right hand, 3 cm
in and 2 cm below it: a pistol cupped, inside the support grip's zone) and its grip squeezed from 0.5 s on (the two-handed grip;
with --grip: the owner's "vault while holding our weapons"). --support-release: the left grip lets go as each double tap's
second press goes down (the gun in the right hand alone for the mantle) and squeezes again --regrab seconds later (default
1.8: the move and the camera's settle), the hand kept where it is. --trigger: the right index resting on the trigger at 0.3 (a held gun's
finger; never a shot). --yaw / --pitch: the head turned or pitched (degrees)."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)
PRIMARY = 1 << 1             # A (right) / X (left)
VALID = (1 << 4) | (1 << 5)  # gripValid, aimValid


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
    valued = ("--start", "--taps", "--every", "--gap", "--tap", "--length", "--hands", "--yaw", "--pitch", "--regrab")
    taken = {i + 1 for i, a in enumerate(sys.argv) if a in valued}
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and i not in taken]
    if not args:
        print(__doc__)
        return 2
    out = args[0]
    start = option("--start", 3.0)
    taps = int(option("--taps", 1.0))
    every = option("--every", 6.0)
    gap = option("--gap", 0.15)
    hold = option("--tap", 0.08)
    single = "--single" in sys.argv
    yaw = option("--yaw", 0.0)
    pitch = option("--pitch", 0.0)
    hands = sys.argv[sys.argv.index("--hands") + 1] if "--hands" in sys.argv else "chest"
    grip = 1.0 if "--grip" in sys.argv else 0.0
    support = "--support" in sys.argv
    support_release = support and "--support-release" in sys.argv
    regrab = option("--regrab", 1.8)
    trigger = 0.3 if "--trigger" in sys.argv else 0.0
    if hands not in ("chest", "view") or taps < 1:
        print(__doc__)
        return 2
    presses = []  # (down, up) seconds
    for k in range(taps):
        t0 = start + k * every
        presses.append((t0, t0 + hold))
        if not single:
            presses.append((t0 + hold + gap, t0 + 2 * hold + gap))
    length = option("--length", presses[-1][1] + 3.0)
    hz = 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)
    head = head_orientation(yaw, pitch)
    hand_q = head_orientation(yaw, 0.0)
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    right = (c, 0.0, s)
    ahead = (s, 0.0, -c)

    def at(r, u, f):
        return (r * right[0] + f * ahead[0], u, r * right[2] + f * ahead[2])

    eyes = [(*head, side * ipd / 2 * right[0], 0.0, side * ipd / 2 * right[2]) for side in (-1, 1)]
    side_m, down, ahead_m = (0.09, 0.08, 0.25) if hands == "view" else (0.15, 0.35, 0.30)
    left_hand = (*hand_q, *at(-side_m, -down, ahead_m))
    right_hand = (*hand_q, *at(side_m, -down, ahead_m))
    if support:
        left_hand = (*hand_q, *at(side_m - 0.03, -down - 0.02, ahead_m + 0.05))
    n = int(length * hz)
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for i in range(n):
            t = i / hz
            a = any(d <= t < u for d, u in presses)
            left_grip = 1.0 if support and t >= 0.5 else 0.0
            if support_release and any(d <= t < d + regrab for d, _ in presses[1::2] if not single):
                left_grip = 0.0  # let go for the mantle, the gun in the right hand alone
            f.write(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                               VALID, 0.0, left_grip, 0.0, 0.0, *left_hand, *left_hand,
                               VALID | (PRIMARY if a else 0), trigger, grip, 0.0, 0.0, *right_hand, *right_hand))
    taps_text = ", ".join(f"{d:.2f}-{u:.2f}" for d, u in presses)
    print(f"{out}: {n} frames at {hz:g} Hz ({length:g} s); A down {taps_text} s ({'single' if single else 'double'} taps); hands {hands}"
          f"{', right grip held' if grip else ''}{', left hand on the gun, its grip held' if support else ''}{f', let go for {regrab:g} s at each vault' if support_release else ''}{', index on the trigger' if trigger else ''}; head yaw {yaw:g}, pitch {pitch:g}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
