# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) of a player standing still with
tracked hands held up in view who pulls a trigger at given times: ME2's interrupts on the triggers (docs/le2-continuation-
plan.md M5.2, research/le2-cinema.md). In ME2's conversation mode the left trigger is the pad's LT (Paragon) and the right
its RT (Renegade) (plugin/PadMerge's ConversationTriggers); a pull inside the prompt's window takes the interrupt, one
outside it doesn't. Replay it with its inputs on the fake headset, started when the conversation's node begins:

    python tools/make-interrupt-recording.py out.mevrrec [--seconds S] [--hz HZ] [--pull SIDE:AT[:HOLD]] ...
    tools/replay.ps1 -Path out.mevrrec -Inputs

--pull: SIDE is left (Paragon) or right (Renegade), AT the second it is pulled all the way, HOLD how long (default 0.25 s);
repeatable. --a EVERY: the right controller's A pressed for 0.15 s every EVERY seconds from 1 s on (to step through a
conversation's lines and its wheel's highlighted reply). --seconds: the recording's length (default 12). The head looks
straight ahead; the hands are held 0.25 m ahead of the eyes, 0.08 m under them (make-walk-recording.py's --hands view), the
sticks centred, no grips."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("--"):
        print(__doc__)
        return 2
    out = args[0]
    seconds = 12.0
    hz = 90.0
    pulls = []  # (hand 0 left / 1 right, at, hold)
    a_every = 0.0
    i = 1
    while i < len(args):
        if args[i] == "--a":
            a_every = float(args[i + 1])
            i += 2
        elif args[i] == "--seconds":
            seconds = float(args[i + 1])
            i += 2
        elif args[i] == "--hz":
            hz = float(args[i + 1])
            i += 2
        elif args[i] == "--pull":
            parts = args[i + 1].split(":")
            if parts[0] not in ("left", "right") or len(parts) < 2:
                print("--pull left|right:AT[:HOLD]")
                return 2
            pulls.append((0 if parts[0] == "left" else 1, float(parts[1]), float(parts[2]) if len(parts) > 2 else 0.25))
            i += 2
        else:
            print(f"unknown option {args[i]}")
            return 2
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    q = (0.0, 0.0, 0.0, 1.0)              # straight ahead (OpenXR: -Z)
    eyes = [(*q, side * ipd / 2, 0.0, 0.0) for side in (-1, 1)]
    hands = [(*q, -0.09, -0.08, -0.25), (*q, 0.09, -0.08, -0.25)]
    valid = (1 << 4) | (1 << 5)  # gripValid, aimValid
    n = int(seconds * hz)
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for k in range(n):
            t = k / hz
            trigger = [0.0, 0.0]
            for hand, at, hold in pulls:
                if at <= t < at + hold:
                    trigger[hand] = 1.0
            a = 2 if a_every > 0 and t >= 1.0 and (t - 1.0) % a_every < 0.15 else 0  # the button bit 1: primary (A)
            f.write(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                               valid, trigger[0], 0.0, 0.0, 0.0, *hands[0], *hands[0],
                               valid | a, trigger[1], 0.0, 0.0, 0.0, *hands[1], *hands[1]))
    text = ", ".join(f"{'left (LT, Paragon)' if h == 0 else 'right (RT, Renegade)'} at {a:g} s for {d:g} s" for h, a, d in pulls)
    print(f"{out}: {n} frames at {hz:g} Hz ({seconds:g} s); trigger pulls: {text or 'none'}"
          f"{f'; A every {a_every:g} s' if a_every > 0 else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
