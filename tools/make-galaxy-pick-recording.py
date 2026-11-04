# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) for testing the galaxy hologram
(research/51 section 8) on the fake headset: the head looks down at the hologram's table, the right controller points at one
of the map's objects and pulls its trigger once. The table and the objects are placed as plugin/GalaxyHologram and
core/vr/GalaxyHologram place them (the table ahead of and below the head, the level centred on the map's centre object and
turned so 'away' is the game camera's heading), from a `mevr galaxy dump` file of the level the map opens on.

    python tools/make-galaxy-pick-recording.py <dump.json> <label> out.mevrrec [--seconds S] [--pick-at T] [--pitch DEG]

--world AHEAD BELOW RADIUS TILT: the CIC table on foot instead (the host's logged table centre, metres; its radius and tilt).

    python tools/make-galaxy-pick-recording.py --snapshot <snapshot.json> <label> out.mevrrec [...]

--snapshot: aim at an object where the host placed it (the snapshot's "galaxyHologram", app space, taken under a replay with
the head at the origin), at any level of the map; the table needn't be computed. With `--panel-uv U V` (and any <label>, e.g.
"yes") it aims at that point of the game's UI panel instead (the snapshot's "hud" quad; U, V from 0..1 of its -hud.png:
the game's own travel box over the table); `--layer NAME` another quad (e.g. quad0: Halcyon's flat screen).
<label>: the object's name as the hologram shows it (e.g. a cluster's). The trigger is down from --pick-at (default 3 s) for
--hold (0.3 s); the recording lasts --seconds (default 8). Replay it once (not in a loop) with -Inputs while the map is up:
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


def objects_in_world(dump, ahead, below, radius, tilt_deg):
    """The CIC table (game/GalaxyMap PlaceInWorld, on foot): the clusters from GalaxyMap_Cluster's X/Y (630 units a step round
    (0.5, 0.5)), the visible ones as the open map showed them, 'away' the world's -Y, the far edge raised tilt_deg; the table's
    centre `ahead` and `below` the head (as the host logs it: "galaxy hologram: up: the table at (x, y, z)")."""
    hidden = {}
    for a in dump["m_pDynamicObjects"]:
        if isinstance(a, dict) and a.get("Tag", "").startswith("Cluster") and len(a.get("Tag", "")) == 9:
            hidden[short(a["name"])] = a.get("bHidden", False)
    placed = []
    for row in dump["tables"]["m_pClusterTable"]["data"]:
        c = row["cells"]
        ref = str(int(c["Name"]["int"]))
        if hidden.get(ref, True):
            continue
        placed.append((c["Name"].get("text", ""), ((c["X"]["float"] - 0.5) * 630.0, (c["Y"]["float"] - 0.5) * 630.0)))
    extent = 1.15 * max(math.hypot(x, y) for _, (x, y) in placed)
    scale = radius / extent
    t = math.radians(tilt_deg)
    out = []
    for name, (x, y) in placed:
        lx, ly, lz = x * scale, 0.0, y * scale  # right = game +X; away (-Z) = game -Y, so local z = +y
        ry, rz = ly * math.cos(t) - lz * math.sin(t), ly * math.sin(t) + lz * math.cos(t)
        out.append((name, (lx, -below + ry, -ahead + rz)))
    return out


def main():
    skip = set()
    for flag, n in (("--seconds", 1), ("--pick-at", 1), ("--pitch", 1), ("--world", 4), ("--panel-uv", 2), ("--layer", 1), ("--hold", 1), ("--press", 1)):
        if flag in sys.argv:
            k = sys.argv.index(flag)
            skip.update(range(k + 1, k + 1 + n))
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and i not in skip]
    if len(args) < 3:
        print(__doc__)
        return 2
    dump_path, label, out = args[0], args[1], args[2]
    seconds = option("--seconds", 8.0)
    pick_at = option("--pick-at", 3.0)
    pitch = option("--pitch", -30.0)
    hold = option("--hold", 0.3)
    # --press primary|secondary|stick: that right-hand button instead of the trigger (A, B, the stick's click)
    press_bit = {"stick": 1 << 0, "primary": 1 << 1, "secondary": 1 << 2}.get(
        sys.argv[sys.argv.index("--press") + 1] if "--press" in sys.argv else "", 0)
    with open(dump_path, encoding="utf-8-sig") as f:
        dump = json.load(f)
    if "--snapshot" in sys.argv and "--panel-uv" in sys.argv:
        i = sys.argv.index("--panel-uv")
        u, v = float(sys.argv[i + 1]), float(sys.argv[i + 2])
        name = sys.argv[sys.argv.index("--layer") + 1] if "--layer" in sys.argv else "hud"
        hud = next((layer for layer in dump.get("layers", []) if layer.get("name") == name and layer.get("type") == "quad"), None)
        if hud is None:
            print(f"{dump_path}: no '{name}' quad in this snapshot")
            return 1
        x, y, z, w = hud["pose"]["orientation"]
        centre = hud["pose"]["position"]
        width, height = hud["size"]

        def rotate(vec):
            # q * v * q^-1 for a unit quaternion (x, y, z, w)
            tx = 2.0 * (y * vec[2] - z * vec[1])
            ty = 2.0 * (z * vec[0] - x * vec[2])
            tz = 2.0 * (x * vec[1] - y * vec[0])
            return (vec[0] + w * tx + (y * tz - z * ty), vec[1] + w * ty + (z * tx - x * tz), vec[2] + w * tz + (x * ty - y * tx))

        right, up = rotate((1.0, 0.0, 0.0)), rotate((0.0, 1.0, 0.0))
        point = tuple(centre[k] + right[k] * (u - 0.5) * width + up[k] * (0.5 - v) * height for k in range(3))
        placed = [(label, point)]
    elif "--snapshot" in sys.argv:
        holo = dump.get("galaxyHologram") or {}
        if not holo.get("up"):
            print(f"{dump_path}: the hologram wasn't up in this snapshot")
            return 1
        placed = [(o["label"], tuple(o["at"])) for o in holo.get("objects", [])]
    elif "--world" in sys.argv:
        i = sys.argv.index("--world")
        ahead, below, radius, tilt = (float(v) for v in sys.argv[i + 1:i + 5])
        placed = objects_in_world(dump, ahead, below, radius, tilt)
    else:
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
        down = pick_at <= t < pick_at + hold
        trigger = 1.0 if down and press_bit == 0 else 0.0
        buttons = valid | (press_bit if down else 0)
        frames.append(FRAME.pack(int(t * 1e9), 3, *eyes[0], *fov, *eyes[1], *fov,
                                 1 << 4, 0.0, 0.0, 0.0, 0.0, *left, *left,  # the left hand: its grip only, it points at nothing
                                 buttons, trigger, 0.0, 0.0, 0.0, *aim, *aim))
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for frame in frames:
            f.write(frame)
    print(f"{out}: {len(frames)} frames; '{label}' at ({target[0]:.3f}, {target[1]:.3f}, {target[2]:.3f}) m from the head, "
          f"{length:.2f} m from the right hand; the trigger down at {pick_at:g} s for {hold:g} s")
    for name, p in placed:
        print(f"  {name:<20} ({p[0]:+.3f}, {p[1]:+.3f}, {p[2]:+.3f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
