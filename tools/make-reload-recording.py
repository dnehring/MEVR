# SPDX-License-Identifier: MIT
"""A reload by hand for the test loop's replay (the sequels: research/le2-vrik.md "The reload by hand", game/Reload): the right
hand holds the gun ahead of the chest (its grip open: a gun nobody let go stays out; --grip-held squeezes it from 0.5 s on,
[Holsters] gripHolds' carry) and fires once;
then the left hand goes from its rest to the clip pouch on the left hip ([Reload] pouch), squeezes its grip there (a clip
taken), carries it beside the gun, pushes it into the receiver (over the right hand: [Vent]'s zone, entered from outside),
lets go and goes back to rest. The model is tools/make-rack-recording.py.
    python tools/make-reload-recording.py out.mevrrec [--eye M] [--along M] [--up M] [--no-shot] [--grip-held] [--length S]
--eye: the standing eye height the pouch scales with (default 1.7526, the fake headset's head height, as
make-holster-recording.py). --along, --up: where the left grip pose ends in the receiver, meters from the right grip pose along
the barrel (+ toward the muzzle) and above it (defaults 0.04 and 0.05, the rack's; the zone is measured from the hand bones:
if the snapshot's "mod"."reload" onGun says the hand stops outside it, move these by the difference). --no-shot: no trigger
pull (the magazine is full: a squeeze at the pouch is refused). The head stays level at the origin.
Replay with: tools\\replay.ps1 -Path out.mevrrec -Inputs; the log's `reload:` lines; `mevr reload status` for the ammo."""
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)
VALID = (1 << 4) | (1 << 5)  # gripValid, aimValid
REFERENCE_EYE = 1.65         # core/ik/BodySlots kReferenceEyeHeight
POUCH = (-0.20, -0.60, 0.02)  # right, up, forward at the reference eye height ([Reload] pouch's default)
IDENTITY = (0.0, 0.0, 0.0, 1.0)  # looking along -Z (OpenXR: -Z forward, +Y up, +X right)


def option(name, default):
    return float(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3.0 - 2.0 * x)


def lerp(a, b, f):
    return tuple(x + (y - x) * f for x, y in zip(a, b))


def main():
    valued = ("--eye", "--along", "--up", "--length")
    taken = {i + 1 for i, a in enumerate(sys.argv) if a in valued}
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and i not in taken]
    if len(args) != 1:
        print(__doc__)
        return 2
    out = args[0]
    eye = option("--eye", 1.7526)
    along = option("--along", 0.04)
    up = option("--up", 0.05)
    shot = "--no-shot" not in sys.argv
    grip_held = "--grip-held" in sys.argv
    # Held from 0.5 s, not from the first frame: a squeeze that begins before the mod has published that the grip holds the
    # gun keeps the meaning it began with (RB, ME2's power-wheel button: the shot never fired).
    grip_from = 0.5
    length = option("--length", 10.0)
    scale = min(max(eye / REFERENCE_EYE, 0.7), 1.3)
    pouch = (POUCH[0] * scale, POUCH[1] * scale, -POUCH[2] * scale)
    # The gun ahead of the chest, pointing forward: the right aim pose, its grip pose 6 cm under it (make-rack-recording).
    right_aim = (0.12, -0.32, -0.42)
    right_grip = (right_aim[0], right_aim[1] - 0.06, right_aim[2])
    rest = (-0.22, -0.55, -0.20)
    take = (right_grip[0], right_grip[1] + up, right_grip[2] - along)
    beside = (take[0] - 0.22, take[1], take[2])  # left of the gun, outside the receiver
    # The left hand's way: (start, end, from, to) and its grip.
    path = [
        (3.0, 3.6, rest, pouch),
        (4.0, 4.6, pouch, beside),
        (4.6, 4.9, beside, take),
        (5.4, 6.0, take, rest),
    ]
    squeeze_on, squeeze_off = 3.8, 5.2

    def left_hand(t):
        position = rest
        for start, end, a, b in path:
            if t >= end:
                position = b
            elif t >= start:
                return lerp(a, b, smooth((t - start) / (end - start)))
        return position

    hz = 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)
    eyes = [(*IDENTITY, side * ipd / 2, 0.0, 0.0) for side in (-1, 1)]
    n = int(length * hz)
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for i in range(n):
            t = i / hz
            grip = left_hand(t)
            left_squeeze = 1.0 if squeeze_on <= t < squeeze_off else 0.0
            trigger = 1.0 if shot and 1.5 <= t < 1.65 else 0.0
            right_squeeze = 1.0 if grip_held and t >= grip_from else 0.0
            left_grip = (*IDENTITY, *grip)
            left_aim = (*IDENTITY, grip[0], grip[1] + 0.05, grip[2] - 0.02)
            f.write(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                               VALID, 0.0, left_squeeze, 0.0, 0.0, *left_grip, *left_aim,
                               VALID, trigger, right_squeeze, 0.0, 0.0, *IDENTITY, *right_grip, *IDENTITY, *right_aim))
    print(f"{out}: {n} frames at {hz:g} Hz ({length:g} s); {'one shot at 1.5 s, ' if shot else ''}the left hand at the pouch "
          f"({pouch[0]:+.2f}, {pouch[1]:+.2f}, {pouch[2]:+.2f}) m from the eyes (eye height {eye:g} m), its grip closed "
          f"{squeeze_on:g}-{squeeze_off:g} s, into the receiver {along:g} m ahead of and {up:g} m over the right grip pose at 4.9 s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
