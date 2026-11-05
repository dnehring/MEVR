# SPDX-License-Identifier: MIT
"""The console lines that put the whole cockpit around the player's head (research/47 "The cockpit in one frame").

Reads the hull's manifest (concept/assets/hull reconstruction/assembled/manifest.json: the eye point, the furniture's
places, the proposed scale) and prints one `mevr cockpit part` line per packaged mesh, the cockpit's eye on the head:

    python tools/cockpit/place.py [--assets concept/assets] [--metres-per-hull-unit 3.0] [--yaw-sign 1]
    tools/console.ps1 (python tools/cockpit/place.py)

With --script <file> it writes the cockpit's script instead: the parts, the live chase view on the console's screen and the
ride in the Mako, the lines the mod runs by itself on entering the Mako ([Cockpit] script in LE1MEVR.ini, default
<ASI>\\cockpit\\cockpit.txt; research/47 "Riding in the Mako"):

    python tools/cockpit/place.py --script "<game>\\Binaries\\Win64\\ASI\\cockpit\\cockpit.txt"

Frames: the hull is x right, y forward, z up (right-handed); the package is that times 100 with forward on -Y, so a hull mesh
needs yaw +90 to face the head's way. A furniture file is turned about hull z by its `turnDeg`; mirrored into the package that
is a yaw of -turnDeg (`--yaw-sign -1` if the game shows them turned the other way).
"""
import argparse
import json
import math
import os
import struct

PACKAGE = "BIOG_MEVR_Cockpit"
UNITS_PER_MODEL_UNIT = 100.0  # the packager's glTF import
HULL_MESHES = ["Hull_Shell", "Hull_Console", "Hull_Side_Left", "Hull_Side_Right", "Hull_Glass"]  # the glass from 66d3a38 on
FURNITURE = {  # manifest name: package mesh
    "chair": "Chair",
    "throtle-control-base": "Throttle_Base",
    "throtle-control-handle": "Throttle_Handle",
    "joystick-base": "Joystick_Base",
    "joystick-grip": "Joystick_Grip",
}


def Lines(manifest, metres, yaw_sign, seat_back=0.0):
    """`seat_back`: the pilot (the eye, the chair and its controls) that many metres further back in the hull: the hull's own meshes
    move forward around them (the user, 2026-11-03: the side displays were too close to read)."""
    eye = manifest["eye"]["hull"]
    out = ["mevr cockpit parts clear"]

    def Part(mesh, origin, yaw, metres_per_unit, ahead=0.0):
        right, forward, up = ((origin[i] - eye[i]) * metres for i in range(3))
        forward += ahead
        out.append(f"mevr cockpit part {PACKAGE}.{mesh} {right:.4f} {forward:.4f} {up:.4f} {yaw:.1f} {metres_per_unit:.6f}")

    for mesh in HULL_MESHES:
        Part(mesh, (0.0, 0.0, 0.0), 90.0, metres / UNITS_PER_MODEL_UNIT, seat_back)
    for name, mesh in FURNITURE.items():
        f = manifest["furniture"][name]
        origin = list(f["origin"])
        motion = f.get("motion", {})
        if motion.get("kind") == "slide":  # at its neutral place
            for i in range(3):
                origin[i] += motion["axis"][i] * motion["neutral"] * f["hullUnitsPerModelUnit"]
        Part(mesh, origin, 90.0 - yaw_sign * f["turnDeg"], f["hullUnitsPerModelUnit"] * metres / UNITS_PER_MODEL_UNIT)
    out.append("mevr cockpit parts")
    return out


def HudLine(manifest, metres, seat_back=0.0):
    """`hud ...`: the Mako's status on the left half of the manifest's left display, the radar on its right half (each a
    little in front of it), the rest of the HUD panel high in the windshield; metres from the cockpit's eye (right, forward, up)."""
    eye = manifest["eye"]["hull"]
    left = manifest["screens"]["left"]
    corners = left["corners"]
    centre = [sum(c[i] for c in corners) / 4.0 for i in range(3)]
    u, normal = left["u"], left["normal"]
    quarter = left["width"] / 4.0
    lift = 0.02 / metres  # 2 cm off the display, toward the pilot

    def At(along):
        p = [centre[i] + u[i] * along + normal[i] * lift for i in range(3)]
        right, forward, up = ((p[i] - eye[i]) * metres for i in range(3))
        return right, forward + seat_back, up

    width = left["width"] * metres * 0.42
    status, radar = At(-quarter), At(quarter)
    return ("hud status {:.3f} {:.3f} {:.3f} {:.3f} radar {:.3f} {:.3f} {:.3f} {:.3f} panel 0.000 1.600 0.300 1.000"
            .format(*status, width, *radar, width * 0.85))


def GlbPositions(path):
    """Every vertex position of a .glb's meshes (glTF axes: x, y up, z), without the nodes' transforms (detach_controls.py made
    each part's origin its pivot, and the packager places that origin)."""
    with open(path, "rb") as f:
        data = f.read()
    length = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + length])
    at = 20 + length
    binary = data[at + 8:at + 8 + struct.unpack_from("<I", data, at)[0]]
    points = []
    for node in doc.get("nodes", []):
        if "mesh" not in node:
            continue
        for prim in doc["meshes"][node["mesh"]]["primitives"]:
            accessor = doc["accessors"][prim["attributes"]["POSITION"]]
            view = doc["bufferViews"][accessor["bufferView"]]
            start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            stride = view.get("byteStride", 12)
            for i in range(accessor["count"]):
                points.append(struct.unpack_from("<3f", binary, start + i * stride))
    return points


def HandleInEye(manifest, assets, name, metres):
    """A furniture model's vertices in metres from the cockpit's eye (right, forward, up), as Lines places it (a slide at neutral)."""
    f = manifest["furniture"][name]
    turn = math.radians(f["turnDeg"])
    c, s = math.cos(turn), math.sin(turn)
    scale = f["hullUnitsPerModelUnit"]
    origin = list(f["origin"])
    motion = f.get("motion", {})
    if motion.get("kind") == "slide":
        for i in range(3):
            origin[i] += motion["axis"][i] * motion["neutral"] * scale
    eye = manifest["eye"]["hull"]
    out = []
    for x, y, z in GlbPositions(os.path.join(assets, "game-ready", name + ".glb")):
        bx, by, bz = x, -z, y  # glTF (y up) to the hull's z up
        hull = (origin[0] + (c * bx - s * by) * scale, origin[1] + (s * bx + c * by) * scale, origin[2] + bz * scale)
        out.append(tuple((hull[i] - eye[i]) * metres for i in range(3)))
    return out


def Mean(points):
    return tuple(sum(p[i] for p in points) / len(points) for i in range(3))


def Normalized(v):
    length = math.sqrt(sum(a * a for a in v))
    return tuple(a / length for a in v)


def LongAxis(points):
    """The points' principal axis (power iteration on their covariance)."""
    m = Mean(points)
    cov = [[sum((p[i] - m[i]) * (p[j] - m[j]) for p in points) for j in range(3)] for i in range(3)]
    v = (1.0, 0.3, 0.1)
    for _ in range(60):
        v = Normalized(tuple(sum(cov[i][j] * v[j] for j in range(3)) for i in range(3)))
    return v


def CentreLine(points):
    """An upright handle's centre line: through the middles of its height's second and third fifths (its grip, below the head
    that a stick bends forward into)."""
    lo = min(p[2] for p in points)
    hi = max(p[2] for p in points)

    def Band(a, b):
        return Mean([p for p in points if lo + a * (hi - lo) <= p[2] <= lo + b * (hi - lo)])

    low, high = Band(0.1, 0.3), Band(0.4, 0.6)
    return Mean([low, high]), Normalized(tuple(high[i] - low[i] for i in range(3)))


def OnLine(point, through, axis):
    t = sum((point[i] - through[i]) * axis[i] for i in range(3))
    return tuple(through[i] + axis[i] * t for i in range(3))


def Handles(manifest, assets, metres):
    """Each handle's grip point and axis from the models (right, forward, up from the eye): the lever's bar along its length, the
    stick's centre line; each grip the manifest's moved onto that line (research/47 "The hands on the handles"). None without the
    models (Git LFS not fetched)."""
    p = manifest["pilot"]["fromEyeMetres"]
    try:
        lever = HandleInEye(manifest, assets, "throtle-control-handle", metres)
        stick = HandleInEye(manifest, assets, "joystick-grip", metres)
    except (OSError, ValueError, KeyError, struct.error):
        return None
    out = {}
    for key, points, line in (("lever", lever, lambda pts: (Mean(pts), LongAxis(pts))), ("stick", stick, CentreLine)):
        through, axis = line(points)
        right, up, forward = p["throttleGrip" if key == "lever" else "stickGrip"]
        out[key] = (OnLine((right, forward, up), through, axis), axis)
    return out


def SeatLine(manifest, handles=None):
    """`seat ...`: the pilot's hips, knees, heels and the two grips (throttle left, stick right) from the manifest's pilot, which
    gives them right, up, forward from the eye; the mod takes right, forward, up (research/47 "Shepard in the seat"). With the
    handles measured from the models, the grips sit on their axes and `axes` follows."""
    p = manifest["pilot"]["fromEyeMetres"]

    def RFU(name):
        right, up, forward = p[name]
        return f"{right:.3f} {forward:.3f} {up:.3f}"

    def Three(v):
        return f"{v[0]:.3f} {v[1]:.3f} {v[2]:.3f}"

    line = f"seat hip {RFU('hPoint')} knees {RFU('kneeLeft')} {RFU('kneeRight')} heels {RFU('heelLeft')} {RFU('heelRight')} "
    if handles is None:
        return line + f"grips {RFU('throttleGrip')} {RFU('stickGrip')}"
    return (line + f"grips {Three(handles['lever'][0])} {Three(handles['stick'][0])} "
            f"axes {Three(handles['lever'][1])} {Three(handles['stick'][1])}")


def Script(manifest, metres, yaw_sign, eye, seat_back=0.0, handles=None):
    """The cockpit's script: what the mod runs on entering the Mako (`mevr cockpit` lines without those words)."""
    parts = [line[len("mevr cockpit "):] for line in Lines(manifest, metres, yaw_sign, seat_back) if line.startswith("mevr cockpit part ")]
    return (["; The Mako's cockpit (research/47): written by tools/cockpit/place.py --script; run by the mod on entering the Mako.",
             "parts sweep"] + parts +
            ["screenpart BIOG_MEVR_Cockpit.Hull_Console", "element 1", "post 0", "screen 768 343 float",
             "aimscreen 768 306", "aimreticle 6", HudLine(manifest, metres, seat_back), SeatLine(manifest, handles),
             f"ride {eye[0]:.3f} {eye[1]:.3f} {eye[2]:.3f}"])


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--assets", default=os.path.join(here, "..", "..", "concept", "assets"))
    parser.add_argument("--metres-per-hull-unit", type=float, default=None, help="default: the manifest's scale")
    parser.add_argument("--yaw-sign", type=float, default=1.0)
    parser.add_argument("--script", help="write the cockpit's script to this file instead of printing console lines")
    # 0.25 m up (not 0.8): the Mako's own turret pivot is 0.75 m over the origin, so the cockpit sits under it and its barrels
    # show at the top of the windshield (research/47 "The cannon over the cabin", 2026-11-04).
    parser.add_argument("--eye", type=float, nargs=3, default=(0.0, 1.0, 0.25),
                        help="the cockpit's eye from the Mako's origin, metres: right forward up (default 0 1.0 0.25)")
    parser.add_argument("--seat-back", type=float, default=0.15,
                        help="the pilot this many metres further back in the hull than the manifest's eye (default 0.15)")
    args = parser.parse_args()
    path = os.path.join(args.assets, "hull reconstruction", "assembled", "manifest.json")
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    metres = args.metres_per_hull_unit or manifest["scale"]["metresPerHullUnit"]
    if args.script:
        os.makedirs(os.path.dirname(os.path.abspath(args.script)), exist_ok=True)
        with open(args.script, "w", encoding="utf-8", newline="\n") as f:
            handles = Handles(manifest, args.assets, metres)
            f.write("\n".join(Script(manifest, metres, args.yaw_sign, args.eye, args.seat_back, handles)) + "\n")
        print(f"{args.script}: the cockpit's script")
        return
    for line in Lines(manifest, metres, args.yaw_sign, args.seat_back):
        print(line)


if __name__ == "__main__":
    main()
