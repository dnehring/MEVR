# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) that works our main menu (research/28)
with the right controller, by the document's own coordinates (1440 x 900 dp, as mevr-menu-preview's PNGs show them): both
stick clicks held to open it, the right aim ray on a point, a trigger pull, the right stick to scroll. For checking the
menu's rows in the game on the fake headset (research/60: the Performance row clicked through).

    python tools/make-menu-recording.py out.mevrrec "open; point 150 452; click; scroll 4; point 1281 236; click; hold 4"
    tools/replay.ps1 -Path out.mevrrec -Inputs          # -Inputs: the recorded buttons and sticks reach the menu
    tools/snapshot.ps1 -Count 4 -Interval 3 -Delay 2    # the menu layer is <stamp>-menu.png; the JSON's "mainMenu" has
                                                         # the pointer's hit (x, y in dp) to check the aim

Steps, separated by ';' (seconds in brackets are the defaults):
  open                 both stick clicks held 0.6 s (the menu opens after 0.4 s), then 0.6 s to settle
  point X Y [1.2]      the right aim ray moved onto document point (X, Y) and held there
  click                the right trigger pulled for 0.15 s, then 0.35 s held still
  scroll S [down]      the right stick held down (or up) for S seconds; the ray stays where it points
  hold S               nothing for S seconds
The head stays at the tracking origin looking along -Z, so the menu opens at the place plugin/MainMenu's Place gives it
(`--distance` ahead, 0.10 m below the eyes, `--width` wide: [Menu] distanceM and widthM, 1.3 and 1.45 by default). The right
hand is held at (0.20, -0.35, -0.30) m. Prints each step's start time, for snapshots at the right moments."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)
DOC_W, DOC_H = 1440.0, 900.0
HZ = 90.0
HAND = (0.20, -0.35, -0.30)


def option(name, default):
    return float(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def aim_quat(target):
    """xyzw turning -Z (the aim pose's forward) onto the direction from the hand to target."""
    d = [target[i] - HAND[i] for i in range(3)]
    n = math.sqrt(sum(c * c for c in d))
    d = [c / n for c in d]
    f = (0.0, 0.0, -1.0)
    axis = (f[1] * d[2] - f[2] * d[1], f[2] * d[0] - f[0] * d[2], f[0] * d[1] - f[1] * d[0])
    s = math.sqrt(sum(c * c for c in axis))
    angle = math.atan2(s, sum(f[i] * d[i] for i in range(3)))
    if s < 1e-9:
        return (0.0, 0.0, 0.0, 1.0)
    k = math.sin(angle / 2.0) / s
    return (axis[0] * k, axis[1] * k, axis[2] * k, math.cos(angle / 2.0))


def main():
    distance = option("--distance", 1.3)
    width = option("--width", 1.45)
    taken = {i + 1 for i, a in enumerate(sys.argv) if a in ("--distance", "--width")}
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and i not in taken]
    if len(args) < 2:
        print(__doc__)
        return 2
    out, script = args[0], args[1]
    height = width * DOC_H / DOC_W
    centre = (0.0, -0.10, -distance)

    def doc_point(x, y):
        return (centre[0] + (x / DOC_W - 0.5) * width, centre[1] + (0.5 - y / DOC_H) * height, centre[2])

    # A timeline of segments: (seconds, sticks clicked, trigger, stickY, aim target or None to keep).
    segments = [(0.5, False, 0.0, 0.0, None)]
    t = 0.5
    for raw in script.split(";"):
        words = raw.split()
        if not words:
            continue
        verb = words[0]
        print(f"{t:6.2f} s  {raw.strip()}")
        if verb == "open":
            segments += [(0.6, True, 0.0, 0.0, None), (0.6, False, 0.0, 0.0, None)]
            t += 1.2
        elif verb == "point":
            secs = float(words[3]) if len(words) > 3 else 1.2
            segments.append((secs, False, 0.0, 0.0, doc_point(float(words[1]), float(words[2]))))
            t += secs
        elif verb == "click":
            segments += [(0.15, False, 1.0, 0.0, None), (0.35, False, 0.0, 0.0, None)]
            t += 0.5
        elif verb == "scroll":
            secs = float(words[1])
            up = len(words) > 2 and words[2] == "up"
            segments.append((secs, False, 0.0, 1.0 if up else -1.0, None))
            t += secs
        elif verb == "hold":
            segments.append((float(words[1]), False, 0.0, 0.0, None))
            t += float(words[1])
        else:
            raise SystemExit(f"unknown step '{raw.strip()}'")
    print(f"{t:6.2f} s  end")

    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)
    head = (0.0, 0.0, 0.0, 1.0)
    eyes = [(*head, side * ipd / 2, 0.0, 0.0) for side in (-1, 1)]
    valid = (1 << 4) | (1 << 5)
    left = (0.0, 0.0, 0.0, 1.0, -0.22, -0.75, -0.05)
    aim = (0.0, 0.0, 0.0, 1.0)
    frames = []
    clock = 0.0
    for secs, sticks, trigger, stick_y, target in segments:
        if target is not None:
            aim = aim_quat(target)
        for _ in range(max(1, int(round(secs * HZ)))):
            buttons = valid | (1 if sticks else 0)
            right = (*aim, *HAND)
            frames.append(FRAME.pack(int(clock * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                                     buttons, 0.0, 0.0, 0.0, 0.0, *left, *left,
                                     buttons, trigger, 0.0, 0.0, stick_y, *right, *right))
            clock += 1.0 / HZ
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for frame in frames:
            f.write(frame)
    print(f"{out}: {len(frames)} frames, {clock:.1f} s; menu centre {centre}, {width:.2f} x {height:.3f} m")
    return 0


if __name__ == "__main__":
    sys.exit(main())
