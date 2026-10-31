# SPDX-License-Identifier: MIT
"""Checks detach_controls.py's output: re-imports the exported parts, compares them with the source, measures the throttle's
slot from its lit texture and the grip's collar against its pivot, and renders each control at its limits.

    blender -b --factory-startup --python tools/cockpit/check_controls.py -- <assets folder>
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree


def Load(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    return [o for o in new if o.type == "MESH"][0]


def Bvh(o, matrix=None):
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.transform(matrix if matrix is not None else o.matrix_world)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    return tree


def Reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def Render(objects, path, view, size_hint=None):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.color_type = "TEXTURE"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.show_backface_culling = False
    sc.render.resolution_x = sc.render.resolution_y = 640
    cam = bpy.data.cameras.new("preview")
    cam.type = "ORTHO"
    co = bpy.data.objects.new("preview", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    pts = [o.matrix_world @ Vector(b) for o in objects for b in o.bound_box]
    lo = Vector([min(p[a] for p in pts) for a in range(3)])
    hi = Vector([max(p[a] for p in pts) for a in range(3)])
    size = max(hi - lo)
    cam.ortho_scale = size * 1.2
    d = Vector(view).normalized()
    co.location = (lo + hi) / 2 + d * size * 3
    co.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(co)


def Lit(o):
    """World positions of the base's faces whose texture is orange and lit, and which face up."""
    image = next((n.image for n in o.active_material.node_tree.nodes if n.type == "TEX_IMAGE" and n.image), None)
    if image is None:
        return []
    w, h = image.size
    px = image.pixels[:]
    uv = o.data.uv_layers.active.data
    found = []
    for p in o.data.polygons:
        if p.normal.z < 0.5:
            continue
        u = sum(uv[i].uv.x for i in p.loop_indices) / p.loop_total
        v = sum(uv[i].uv.y for i in p.loop_indices) / p.loop_total
        x, y = int((u % 1) * (w - 1)), int((v % 1) * (h - 1))
        i = (y * w + x) * 4
        r, g, b = px[i], px[i + 1], px[i + 2]
        if r > 0.35 and r > g * 1.5 and g > b * 1.5:
            found.append(o.matrix_world @ p.center)
    return found


def Main(root):
    out = os.path.join(root, "detached")
    manifest = json.load(open(os.path.join(out, "manifest.json"), encoding="utf-8"))
    report = {}

    # Joystick: the grip's collar around the twist axis; a clash at every twist.
    Reset()
    info = manifest["joystick"]
    base = Load(os.path.join(out, "joystick-base.glb"))
    grip = Load(os.path.join(out, "joystick-grip.glb"))
    pivot = Vector(info["moving"]["pivot"])
    print("RESULT joystick origin in file: base", tuple(round(c, 4) for c in base.location), "grip", tuple(round(c, 4) for c in grip.location))
    print("RESULT joystick tris", len(base.data.loop_triangles) if base.data.loop_triangles else "?", "materials", [m.name for m in grip.data.materials])
    low = [grip.matrix_world @ v.co for v in grip.data.vertices if (grip.matrix_world @ v.co).z < pivot.z + 0.08]
    radii = [math.hypot(p.x - pivot.x, p.y - pivot.y) for p in low]
    centre = sum(low, Vector()) / len(low)
    report["gripCollar"] = {"offsetFromAxis": math.hypot(centre.x - pivot.x, centre.y - pivot.y), "maxRadius": max(radii), "minZ": min(p.z for p in low)}
    print("RESULT collar offset from axis", round(report["gripCollar"]["offsetFromAxis"], 4), "max radius", round(max(radii), 4), "lowest", round(report["gripCollar"]["minZ"], 4))
    base_tree = Bvh(base)
    for deg in (0, 22.5, 45, -22.5, -45):
        m = Matrix.Translation(pivot) @ Matrix.Rotation(math.radians(deg), 4, "Z") @ Matrix.Translation(-pivot) @ grip.matrix_world
        clash = len(base_tree.overlap(Bvh(grip, m)))
        print(f"RESULT joystick twist {deg}: {clash} face pairs intersect")
    home = grip.matrix_world.copy()
    grip.matrix_world = Matrix.Translation(pivot) @ Matrix.Rotation(math.radians(45), 4, "Z") @ Matrix.Translation(-pivot) @ home
    bpy.context.view_layer.update()
    Render([base, grip], os.path.join(out, "check-joystick-twist45.png"), (1, -1, 0.7))

    # Throttle: the slot from its lit strip, the handle's foot in it, a clash along the slide.
    Reset()
    info = manifest["throtle-control"]
    base = Load(os.path.join(out, "throtle-control-base.glb"))
    handle = Load(os.path.join(out, "throtle-control-handle.glb"))
    lit = Lit(base)
    if lit:
        lit_lo = [min(p[a] for p in lit) for a in range(3)]
        lit_hi = [max(p[a] for p in lit) for a in range(3)]
        print("RESULT lit faces", len(lit), "x", round(lit_lo[0], 3), round(lit_hi[0], 3), "y", round(lit_lo[1], 3), round(lit_hi[1], 3), "z", round(lit_lo[2], 3), round(lit_hi[2], 3))
    pivot = Vector(info["moving"]["pivot"])
    print("RESULT handle foot", tuple(round(c, 3) for c in pivot), "bounds x", round(min((handle.matrix_world @ Vector(b)).x for b in handle.bound_box), 3), round(max((handle.matrix_world @ Vector(b)).x for b in handle.bound_box), 3))
    base_tree = Bvh(base)
    home = handle.matrix_world.copy()
    motion = info["motion"]
    print("RESULT slide", motion)
    for dx in (motion["min"], motion["min"] / 2, 0.0, motion["neutral"], motion["max"] / 2 + motion["neutral"] / 2, motion["max"]):
        clash = len(base_tree.overlap(Bvh(handle, Matrix.Translation((dx, 0, 0)) @ home)))
        print(f"RESULT throttle slide {dx:+.1f}: {clash} face pairs intersect")
    Render([base], os.path.join(out, "check-throttle-base-top.png"), (0, 0, 1))
    for dx, name in ((motion["min"], "min"), (motion["neutral"], "neutral"), (motion["max"], "max")):
        handle.matrix_world = Matrix.Translation((dx, 0, 0)) @ home
        bpy.context.view_layer.update()
        Render([base, handle], os.path.join(out, f"check-throttle-slide-{name}.png"), (0.2, -1, 0.5))


if __name__ == "__main__":
    Main(sys.argv[sys.argv.index("--") + 1])
