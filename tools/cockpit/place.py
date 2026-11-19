# SPDX-License-Identifier: MIT
"""The console lines that put the whole cockpit around the player's head (research/47 "The cockpit in one frame").

Reads the hull's manifest (concept/assets/hull reconstruction/assembled/manifest.json: the eye point, the furniture's
places, the proposed scale) and prints one `mevr cockpit part` line per packaged mesh, the cockpit's eye on the head:

    python tools/cockpit/place.py [--assets concept/assets] [--metres-per-hull-unit 3.0] [--yaw-sign 1]
    tools/console.ps1 (python tools/cockpit/place.py)

The auto-turret switch on the stick's base (detach_controls.py's `joystick.switch`) gets its part and a `switch` line; which of
Joystick_Base's elements is its lamp comes from the packager's report (--package-report).

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
HULL_MESHES = ["Hull_Shell", "Hull_Console", "Hull_Side_Left", "Hull_Side_Right", "Hull_Glass",  # the glass from 66d3a38 on
               "Repair_Base", "Extraction_Base"]  # the controls' fixed parts (the manifest's "panels")
FURNITURE = {  # manifest name: package mesh
    "chair": "Chair",
    "throtle-control-base": "Throttle_Base",
    "throtle-control-handle": "Throttle_Handle",
    "joystick-base": "Joystick_Base",
    "joystick-grip": "Joystick_Grip",
    "repair-cap": "Repair_Cap",
    "extraction-handle": "Extraction_Handle",
}
ON_HULL = {"repair-cap", "extraction-handle"}  # moving parts set into the hull: they move with it (--seat-back)
EXTRACTION_FORWARD_M = -0.045  # pull the housing and its pivot clear of the V1 ceiling pod


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
        mount = EXTRACTION_FORWARD_M if mesh == "Extraction_Base" else 0.0
        Part(mesh, (0.0, 0.0, 0.0), 90.0, metres / UNITS_PER_MODEL_UNIT, seat_back + mount)
    for name, mesh in FURNITURE.items():
        f = manifest["furniture"][name]
        origin = list(f["origin"])
        motion = f.get("motion", {})
        if motion.get("kind") == "slide":  # at its neutral place
            for i in range(3):
                origin[i] += motion["axis"][i] * motion["neutral"] * f["hullUnitsPerModelUnit"]
        ahead = seat_back if name in ON_HULL else 0.0
        if name == "extraction-handle":
            ahead += EXTRACTION_FORWARD_M
        Part(mesh, origin, 90.0 - yaw_sign * f["turnDeg"], f["hullUnitsPerModelUnit"] * metres / UNITS_PER_MODEL_UNIT, ahead)
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


def PanelLine(manifest, metres, seat_back=0.0):
    """`panel ...`: the repair button's cap and press, the extraction handle's hinge, axis and grip, and which of Repair_Base's
    elements are lamps (the manifest's panels, research/48 section 27): points from the eye in metres, right, forward, up, the
    controls moving with the hull (--seat-back). None without panels in the manifest."""
    panels = manifest.get("panels")
    if not panels:
        return None
    eye = manifest["eye"]["hull"]

    def At(p, forward=0.0):
        return f"{(p[0] - eye[0]) * metres:.4f} {(p[1] - eye[1]) * metres + seat_back + forward:.4f} {(p[2] - eye[2]) * metres:.4f}"

    def Unit(v):
        return f"{v[0]:.5f} {v[1]:.5f} {v[2]:.5f}"

    repair, extraction = panels["repair"], panels["extraction"]
    health, progress = repair["health"]["elements"], repair["progress"]["elements"]
    return (f"panel button {At(repair['pressPoint'])} in {Unit(repair['pressAxis'])} travel {repair['travelMetres']:.4f} "
            f"lever {At(extraction['hingePoint'], EXTRACTION_FORWARD_M)} axis {Unit(extraction['hingeAxis'])} "
            f"grip {At(extraction['gripPoint'], EXTRACTION_FORWARD_M)} "
            f"pull {extraction['pullDeg']:g} lamps {health[0]} {len(health)} {progress[0]} {len(progress)}")


def SwitchPlace(manifest, assets):
    """The auto-turret switch on the stick's base (detach_controls.py's `joystick.switch`): its lever's pivot, the hinge axis
    for on, the lever's tip at rest, in hull coordinates, as the base's furniture entry places the base (its file's origin is
    the stick's twist pivot). None without the switch in the detached manifest."""
    path = os.path.join(assets, "detached", "manifest.json")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        joystick = json.load(f).get("joystick", {})
    switch = joystick.get("switch")
    if not switch or "joystick-base" not in manifest["furniture"]:
        return None
    base = manifest["furniture"]["joystick-base"]
    turn = math.radians(base["turnDeg"])
    c, s = math.cos(turn), math.sin(turn)
    scale = base["hullUnitsPerModelUnit"]
    twist = joystick["base"]["pivot"]

    def Turn(v):
        return (c * v[0] - s * v[1], s * v[0] + c * v[1], v[2])

    def At(p):
        d = Turn([p[i] - twist[i] for i in range(3)])
        return [base["origin"][i] + d[i] * scale for i in range(3)]

    return {"pivot": At(switch["pivot"]), "tip": At(switch["tipRest"]), "axis": list(Turn(switch["axisOn"])),
            "onDeg": switch["onDeg"], "turnDeg": base["turnDeg"], "scale": scale}


def SwitchLamp(report_path):
    """Which of Joystick_Base's elements is the switch's lamp (the packager's report: the element on "TurretLamp"), or None."""
    if not report_path or not os.path.exists(report_path):
        return None
    with open(report_path, encoding="utf-8") as f:
        report = json.load(f)
    for mesh in report.get("meshes", []) if isinstance(report, dict) else report:
        if mesh.get("name", "").endswith(".Joystick_Base") and "TurretLamp" in mesh.get("elementMaterials", []):
            return mesh["elementMaterials"].index("TurretLamp")
    return None


def SwitchLines(manifest, assets, metres, yaw_sign, report_path):
    """The switch's part (`part ...Joystick_Switch`) and its layout (`switch at R F U axis R F U tip R F U on DEG lamp N`), points
    from the eye in metres (right, forward, up), the lamp -1 when the package's report doesn't name it. Empty without a switch."""
    place = SwitchPlace(manifest, assets)
    if place is None:
        return []
    eye = manifest["eye"]["hull"]

    def At(p):
        return " ".join(f"{(p[i] - eye[i]) * metres:.4f}" for i in range(3))

    lamp = SwitchLamp(report_path)
    a = place["axis"]
    part = (f"part {PACKAGE}.Joystick_Switch {At(place['pivot'])} {90.0 - yaw_sign * place['turnDeg']:.1f} "
            f"{place['scale'] * metres / UNITS_PER_MODEL_UNIT:.6f}")
    layout = (f"switch at {At(place['pivot'])} axis {a[0]:.5f} {a[1]:.5f} {a[2]:.5f} tip {At(place['tip'])} on {place['onDeg']:g} "
              f"lamp {lamp if lamp is not None else -1}")
    return [part, layout]


def Script(manifest, metres, yaw_sign, eye, seat_back=0.0, handles=None, switch=()):
    """The cockpit's script: what the mod runs on entering the Mako (`mevr cockpit` lines without those words)."""
    parts = [line[len("mevr cockpit "):] for line in Lines(manifest, metres, yaw_sign, seat_back) if line.startswith("mevr cockpit part ")]
    parts += [line for line in switch if line.startswith("part ")]
    return (["; The Mako's cockpit (research/47): written by tools/cockpit/place.py --script; run by the mod on entering the Mako.",
             "parts sweep"] + parts +
            ["screenpart BIOG_MEVR_Cockpit.Hull_Console", "element 1", "post 0", "screen 768 343 float",
             "aimscreen 768 306", "aimreticle 6", HudLine(manifest, metres, seat_back), SeatLine(manifest, handles)] +
            ([PanelLine(manifest, metres, seat_back)] if PanelLine(manifest, metres, seat_back) else []) +
            [line for line in switch if line.startswith("switch ")] +
            [f"ride {eye[0]:.3f} {eye[1]:.3f} {eye[2]:.3f}"])


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
    # 0 since 2026-11-04: the hull build seats the pilot 0.15 m further back itself (hull_assemble.CHAIR_AT), the place this
    # default gave them in the game from 2026-11-03
    parser.add_argument("--seat-back", type=float, default=0.0,
                        help="the pilot this many metres further back in the hull than the manifest's eye (default 0)")
    parser.add_argument("--package-report", default=os.path.join(here, "..", "..", "build", "cockpit", "BIOG_MEVR_Cockpit.pcc.json"),
                        help="the packager's report (<package>.json), for which of Joystick_Base's elements is the switch's lamp")
    args = parser.parse_args()
    path = os.path.join(args.assets, "hull reconstruction", "assembled", "manifest.json")
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    metres = args.metres_per_hull_unit or manifest["scale"]["metresPerHullUnit"]
    if args.script:
        os.makedirs(os.path.dirname(os.path.abspath(args.script)), exist_ok=True)
        with open(args.script, "w", encoding="utf-8", newline="\n") as f:
            handles = Handles(manifest, args.assets, metres)
            switch = SwitchLines(manifest, args.assets, metres, args.yaw_sign, args.package_report)
            f.write("\n".join(Script(manifest, metres, args.yaw_sign, args.eye, args.seat_back, handles, switch)) + "\n")
        print(f"{args.script}: the cockpit's script")
        return
    for line in Lines(manifest, metres, args.yaw_sign, args.seat_back):
        print(line)
    for line in SwitchLines(manifest, args.assets, metres, args.yaw_sign, args.package_report):
        print("mevr cockpit " + line)


if __name__ == "__main__":
    main()
