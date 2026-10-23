# SPDX-License-Identifier: MIT
"""Reads a pose recording (.mevrrec, written by the game's test loop: src/core/vr/PoseRecording.hpp).

    python tools/mevrrec.py <file.mevrrec>             summary (duration, rate, ranges)
    python tools/mevrrec.py <file.mevrrec> --csv out   every frame as CSV ('-' for stdout)
    python tools/mevrrec.py --latest                   the newest in %LOCALAPPDATA%\\MEVR\\recordings

Poses are in Halcyon's recentered LOCAL app space (meters, +Y up, -Z forward); quaternions xyzw.
"""
import argparse
import csv
import math
import os
import struct
import sys

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)
BUTTONS = ["stickClick", "primary", "secondary", "menu", "gripValid", "aimValid"]


def read(path):
    data = open(path, "rb").read()
    magic, version, header_bytes, frame_bytes, _, created = HEADER.unpack_from(data, 0)
    if magic != MAGIC:
        raise SystemExit(f"{path}: not a .mevrrec recording")
    if version != 1 or frame_bytes < FRAME.size:
        raise SystemExit(f"{path}: unsupported version {version} / frame size {frame_bytes}")
    frames = []
    count = (len(data) - header_bytes) // frame_bytes
    for i in range(count):
        v = FRAME.unpack_from(data, header_bytes + i * frame_bytes)
        f = {"timeS": v[0] * 1e-9, "headValid": bool(v[1] & 1), "controllersActive": bool(v[1] & 2)}
        k = 2
        for eye in ("left", "right"):
            f[eye + "Eye"] = v[k:k + 7]
            f[eye + "Fov"] = v[k + 7:k + 11]
            k += 11
        for hand in ("left", "right"):
            buttons = v[k]
            for b, name in enumerate(BUTTONS):
                f[f"{hand}.{name}"] = bool(buttons & (1 << b))
            f[hand + ".trigger"], f[hand + ".squeeze"], f[hand + ".stickX"], f[hand + ".stickY"] = v[k + 1:k + 5]
            f[hand + ".grip"] = v[k + 5:k + 12]
            f[hand + ".aim"] = v[k + 12:k + 19]
            k += 19
        frames.append(f)
    return created, frames


def head_center(f):
    l, r = f["leftEye"], f["rightEye"]
    return tuple((l[4 + i] + r[4 + i]) / 2 for i in range(3))


def yaw_pitch(q):
    x, y, z, w = q
    # forward = q * (0, 0, -1)
    fx = -(2 * (x * z + w * y))
    fy = -(2 * (y * z - w * x))
    fz = -(1 - 2 * (x * x + y * y))
    return math.degrees(math.atan2(-fx, -fz)), math.degrees(math.asin(max(-1.0, min(1.0, fy))))


def summary(path, created, frames):
    if not frames:
        print(f"{path}: no frames")
        return
    duration = frames[-1]["timeS"] - frames[0]["timeS"]
    print(f"{path}")
    print(f"  {len(frames)} frames, {duration:.2f} s, {len(frames) / duration if duration > 0 else 0:.1f} Hz, "
          f"recorded {created} (unix ms)")
    heads = [f for f in frames if f["headValid"]]
    print(f"  head valid in {len(heads)} frames, controllers active in "
          f"{sum(f['controllersActive'] for f in frames)}")
    if heads:
        centers = [head_center(f) for f in heads]
        for axis, name in enumerate("xyz"):
            values = [c[axis] for c in centers]
            print(f"  head {name}: {min(values):+.3f} .. {max(values):+.3f} m")
        yaws = [yaw_pitch(f["leftEye"][:4]) for f in heads]
        print(f"  head yaw {min(y for y, _ in yaws):+.1f} .. {max(y for y, _ in yaws):+.1f} deg, "
              f"pitch {min(p for _, p in yaws):+.1f} .. {max(p for _, p in yaws):+.1f} deg")
    for hand in ("left", "right"):
        tracked = [f for f in frames if f[hand + ".gripValid"]]
        pulls = sum(1 for a, b in zip(frames, frames[1:]) if a[hand + ".trigger"] < 0.5 <= b[hand + ".trigger"])
        print(f"  {hand}: grip tracked in {len(tracked)} frames, {pulls} trigger pulls, "
              f"max squeeze {max(f[hand + '.squeeze'] for f in frames):.2f}")


def write_csv(frames, out):
    columns = ["timeS", "headValid", "controllersActive"]
    for eye in ("left", "right"):
        columns += [f"{eye}Eye.{c}" for c in ("qx", "qy", "qz", "qw", "x", "y", "z")]
        columns += [f"{eye}Fov.{c}" for c in ("left", "right", "up", "down")]
    for hand in ("left", "right"):
        columns += [f"{hand}.{b}" for b in BUTTONS]
        columns += [f"{hand}.{a}" for a in ("trigger", "squeeze", "stickX", "stickY")]
        for pose in ("grip", "aim"):
            columns += [f"{hand}.{pose}.{c}" for c in ("qx", "qy", "qz", "qw", "x", "y", "z")]
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(columns)
    for f in frames:
        row = [f"{f['timeS']:.6f}", int(f["headValid"]), int(f["controllersActive"])]
        for eye in ("left", "right"):
            row += [f"{v:.6f}" for v in f[eye + "Eye"]] + [f"{v:.6f}" for v in f[eye + "Fov"]]
        for hand in ("left", "right"):
            row += [int(f[f"{hand}.{b}"]) for b in BUTTONS]
            row += [f"{f[f'{hand}.{a}']:.4f}" for a in ("trigger", "squeeze", "stickX", "stickY")]
            row += [f"{v:.6f}" for v in f[hand + ".grip"]] + [f"{v:.6f}" for v in f[hand + ".aim"]]
        writer.writerow(row)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file", nargs="?")
    parser.add_argument("--latest", action="store_true")
    parser.add_argument("--csv", metavar="OUT")
    args = parser.parse_args()
    path = args.file
    if args.latest or not path:
        folder = os.path.join(os.environ.get("LOCALAPPDATA", ""), "MEVR", "recordings")
        files = [os.path.join(folder, n) for n in os.listdir(folder) if n.endswith(".mevrrec")] if os.path.isdir(folder) else []
        if not files:
            raise SystemExit(f"no recordings in {folder}")
        path = max(files, key=os.path.getmtime)
    created, frames = read(path)
    if args.csv:
        if args.csv == "-":
            write_csv(frames, sys.stdout)
        else:
            with open(args.csv, "w", newline="") as out:
                write_csv(frames, out)
            print(f"{len(frames)} frames -> {args.csv}")
    else:
        summary(path, created, frames)


if __name__ == "__main__":
    main()
