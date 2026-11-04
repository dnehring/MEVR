# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) for testing the galaxy hologram
(research/51 section 8) on the fake headset: the head looks down at the hologram's table, the right controller points at one
of the map's objects and pulls its trigger once. The table and the objects are placed as plugin/GalaxyHologram and
core/vr/GalaxyHologram place them (the table ahead of and below the head, the level centred on the map's centre object and
turned so 'away' is the game camera's heading), from a `mevr galaxy dump` file of the level the map opens on.

    python tools/make-galaxy-pick-recording.py <dump.json> <label> out.mevrrec [--seconds S] [--pick-at T] [--pitch DEG]

<label>: the object's name as the hologram shows it (e.g. a cluster's). The trigger is down from --pick-at (default 3 s) for
0.3 s; the recording lasts --seconds (default 8). Replay it once (not in a loop) with -Inputs while the map is up:
    tools/replay.ps1 -Path out.mevrrec -Inputs
Prints where the object and the ray are, for checking against the host's `galaxy hologram:` lines."""
import json
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)

TABLE_DISTANCE = 0.70  # [Galaxy] tableDistanceM, tableBelowM, tableRadiusM defaults
TABLE_BELOW = 0.45
TABLE_RADIUS = 0.48


def orientation(yaw_deg, pitch_deg):
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


def short(full):
    return full.rsplit(".", 1)[-1]


def objects_on_table(dump):
    """The level's visible selectable objects in room space (head at the origin, facing -Z), with their labels."""
    props = dump["behavior"]["props"]
    kind = props["m_sSelectableObject"]
    labels = {}
    for table in ("m_pClusterTable", "m_pSystemTable", "m_pPlanetTable"):
        for row in dump["tables"][table]["data"]:
            name = row["cells"].get("Name", {})
            if "int" in name:
                labels[str(int(name["int"]))] = name.get("text", "")
    actors = []
    for a in dump["m_pDynamicObjects"]:
        if not isinstance(a, dict) or "location" not in a:
            continue
        tag = a.get("Tag", "")
        if len(tag) == len(kind) + 2 and tag.startswith(kind) and not a.get("bHidden", False):
            actors.append((labels.get(short(a["name"]), ""), a["location"]))
    centre = dump["m_pCenterObject"]["location"]
    yaw = dump["playerCamera"]["rotation"][1] * 2.0 * math.pi / 65536.0
    fwd = (math.cos(yaw), math.sin(yaw), 0.0)
    right = (-fwd[1], fwd[0], 0.0)  # Z x forward: UE3's right
    extent = 1.15 * max(math.hypot(*(p[k] - centre[k] for k in range(2))) for _, p in actors)
    scale = TABLE_RADIUS / extent
    table = (0.0, -TABLE_BELOW, -TABLE_DISTANCE)
    out = []
    for label, p in actors:
        d = [p[k] - centre[k] for k in range(3)]
        local = (sum(d[k] * right[k] for k in range(3)) * scale, d[2] * scale, -sum(d[k] * fwd[k] for k in range(3)) * scale)
        out.append((label, tuple(table[k] + local[k] for k in range(3))))
    return out


def main():
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and sys.argv[i - 1] not in ("--seconds", "--pick-at", "--pitch")]
    if len(args) < 3:
        print(__doc__)
        return 2
    dump_path, label, out = args[0], args[1], args[2]
    seconds = option("--seconds", 8.0)
    pick_at = option("--pick-at", 3.0)
    pitch = option("--pitch", -30.0)
    with open(dump_path, encoding="utf-8") as f:
        dump = json.load(f)
    placed = objects_on_table(dump)
    target = next((p for name, p in placed if name == label), None)
    if target is None:
        print(f"no object '{label}' on the table; there are: {', '.join(name for name, _ in placed)}")
        return 1
    hand = (0.20, -0.45, -0.20)
    d = [target[k] - hand[k] for k in range(3)]
    length = math.sqrt(sum(v * v for v in d))
    d = [v / length for v in d]
    aim = (*orientation(math.degrees(math.atan2(d[0], -d[2])), math.degrees(math.asin(d[1]))), *hand)
    head_q = orientation(0.0, pitch)
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)
    eyes = [(*head_q, side * ipd / 2, 0.0, 0.0) for side in (-1, 1)]
    valid = (1 << 4) | (1 << 5)
    left = (0.0, 0.0, 0.0, 1.0, -0.22, -0.75, -0.05)
    hz = 90.0
    frames = []
    for i in range(int(seconds * hz)):
        t = i / hz
        trigger = 1.0 if pick_at <= t < pick_at + 0.3 else 0.0
        frames.append(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                                 valid, 0.0, 0.0, 0.0, 0.0, *left, *left,
                                 valid, trigger, 0.0, 0.0, 0.0, *aim, *aim))
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for frame in frames:
            f.write(frame)
    print(f"{out}: {len(frames)} frames; '{label}' at ({target[0]:.3f}, {target[1]:.3f}, {target[2]:.3f}) m from the head, "
          f"{length:.2f} m from the right hand; the trigger down at {pick_at:g} s for 0.3 s")
    for name, p in placed:
        print(f"  {name:<20} ({p[0]:+.3f}, {p[1]:+.3f}, {p[2]:+.3f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
