# SPDX-License-Identifier: MIT
"""A holster grab for the test loop's replay (research/19; the sequels: research/le2-vrik.md "Holsters and the reload on the
sequels"): the right hand goes from in front of the chest to a body slot, squeezes its grip there (the draw), comes back up
holding the grip (the gun held), goes back to the slot and lets go (the holster).

    python tools/make-holster-recording.py out.mevrrec [--slot hip|shoulder|hipleft|shoulderleft] [--eye M] [--grab S]
        [--hold S] [--away] [--length S]

--slot: the default layout's slot (core/ik/BodySlots DefaultLayout: hip = hipRight, the pistol; shoulder = shoulderRight, the
assault rifle; hipleft the shotgun; shoulderleft the sniper rifle). --eye: the standing eye height the slots scale with
(default 1.7526, the fake headset's head height). --grab: when the grip closes at the slot (default 3 s). --hold: how long the
gun is held up before the hand goes back (default 5 s). --away: let go in front of the chest instead of at the slot (the gun
goes back by itself after [Holsters] releaseGrace). The head stays level at the origin, as the other generators' does.
Replay with: tools\\replay.ps1 -Path out.mevrrec -Inputs."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)
VALID = (1 << 4) | (1 << 5)  # gripValid, aimValid
REFERENCE_EYE = 1.65         # core/ik/BodySlots kReferenceEyeHeight
# right, up, forward from the eyes at the reference eye height (DefaultLayout)
SLOTS = {
    "hip": (0.20, -0.62, -0.05),
    "shoulder": (0.18, -0.08, -0.20),
    "hipleft": (-0.20, -0.62, 0.00),
    "shoulderleft": (-0.18, -0.08, -0.20),
}


def option(name, default):
    return float(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3.0 - 2.0 * x)


def main():
    valued = ("--slot", "--eye", "--grab", "--hold", "--length")
    taken = {i + 1 for i, a in enumerate(sys.argv) if a in valued}
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and i not in taken]
    slot = sys.argv[sys.argv.index("--slot") + 1] if "--slot" in sys.argv else "hip"
    if len(args) != 1 or slot not in SLOTS:
        print(__doc__)
        return 2
    out = args[0]
    eye = option("--eye", 1.7526)
    grab = option("--grab", 3.0)
    hold = option("--hold", 5.0)
    away = "--away" in sys.argv
    scale = min(max(eye / REFERENCE_EYE, 0.7), 1.3)
    r, u, f = (v * scale for v in SLOTS[slot])
    at_slot = (r, u, -f)                 # OpenXR LOCAL: +X right, +Y up, -Z forward
    chest_right = (0.15, -0.35, -0.30)
    chest_left = (-0.15, -0.35, -0.30)
    reach = 0.6                          # seconds for the hand's way to the slot or back
    back = grab + 0.5 + reach + hold     # the hand starts back to the slot
    release = back + reach + 0.4 if not away else back
    length = option("--length", release + 3.0)
    hz = 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)
    q = (0.0, 0.0, 0.0, 1.0)
    eyes = [(*q, side * ipd / 2, 0.0, 0.0) for side in (-1, 1)]

    def right_hand(t):
        if t < grab - 0.4 - reach:
            w = 0.0
        elif t < grab - 0.4:
            w = smooth((t - (grab - 0.4 - reach)) / reach)
        elif t < grab + 0.5:
            w = 1.0
        elif t < grab + 0.5 + reach:
            w = 1.0 - smooth((t - grab - 0.5) / reach)
        elif away or t < back:
            w = 0.0
        elif t < back + reach:
            w = smooth((t - back) / reach)
        elif t < release + 0.5:
            w = 1.0
        else:
            w = 1.0 - smooth((t - release - 0.5) / reach)
        return tuple(a + (b - a) * w for a, b in zip(chest_right, at_slot))

    n = int(length * hz)
    left = (*q, *chest_left)
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for i in range(n):
            t = i / hz
            squeeze = 1.0 if grab <= t < release else 0.0
            hand = (*q, *right_hand(t))
            f.write(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                               VALID, 0.0, 0.0, 0.0, 0.0, *left, *left,
                               VALID, 0.0, squeeze, 0.0, 0.0, *hand, *hand))
    print(f"{out}: {n} frames at {hz:g} Hz ({length:g} s); the right hand at the {slot} slot "
          f"({at_slot[0]:+.2f}, {at_slot[1]:+.2f}, {at_slot[2]:+.2f}) m from the eyes (eye height {eye:g} m), its grip closed "
          f"{grab:g}-{release:g} s ({'let go in front of the chest' if away else 'let go at the slot'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
