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
import os

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


def Lines(manifest, metres, yaw_sign):
    eye = manifest["eye"]["hull"]
    out = ["mevr cockpit parts clear"]

    def Part(mesh, origin, yaw, metres_per_unit):
        right, forward, up = ((origin[i] - eye[i]) * metres for i in range(3))
        out.append(f"mevr cockpit part {PACKAGE}.{mesh} {right:.4f} {forward:.4f} {up:.4f} {yaw:.1f} {metres_per_unit:.6f}")

    for mesh in HULL_MESHES:
        Part(mesh, (0.0, 0.0, 0.0), 90.0, metres / UNITS_PER_MODEL_UNIT)
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


def Script(manifest, metres, yaw_sign, eye):
    """The cockpit's script: what the mod runs on entering the Mako (`mevr cockpit` lines without those words)."""
    parts = [line[len("mevr cockpit "):] for line in Lines(manifest, metres, yaw_sign) if line.startswith("mevr cockpit part ")]
    return (["; The Mako's cockpit (research/47): written by tools/cockpit/place.py --script; run by the mod on entering the Mako.",
             "parts sweep"] + parts +
            ["screenpart BIOG_MEVR_Cockpit.Hull_Console", "element 1", "post 0", "screen 768 343 float",
             f"ride {eye[0]:.3f} {eye[1]:.3f} {eye[2]:.3f}"])


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--assets", default=os.path.join(here, "..", "..", "concept", "assets"))
    parser.add_argument("--metres-per-hull-unit", type=float, default=None, help="default: the manifest's scale")
    parser.add_argument("--yaw-sign", type=float, default=1.0)
    parser.add_argument("--script", help="write the cockpit's script to this file instead of printing console lines")
    parser.add_argument("--eye", type=float, nargs=3, default=(0.0, 1.0, 0.8),
                        help="the cockpit's eye from the Mako's origin, metres: right forward up (default 0 1.0 0.8)")
    args = parser.parse_args()
    path = os.path.join(args.assets, "hull reconstruction", "assembled", "manifest.json")
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    metres = args.metres_per_hull_unit or manifest["scale"]["metresPerHullUnit"]
    if args.script:
        os.makedirs(os.path.dirname(os.path.abspath(args.script)), exist_ok=True)
        with open(args.script, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(Script(manifest, metres, args.yaw_sign, args.eye)) + "\n")
        print(f"{args.script}: the cockpit's script")
        return
    for line in Lines(manifest, metres, args.yaw_sign):
        print(line)


if __name__ == "__main__":
    main()
