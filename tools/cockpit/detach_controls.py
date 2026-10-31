# SPDX-License-Identifier: MIT
"""Splits the Mako cockpit's joystick and throttle models into a fixed base and a moving part, each with its pivot.

    blender -b --factory-startup --python tools/cockpit/detach_controls.py -- <assets folder> [--previews]

Re-runnable: each asset's newest repair (repairs/<date>/<name>/vNNN/Repaired.glb, else Backed_Source.glb, else the
original beside repairs/) is read and nothing there is changed; the results replace <assets>/detached/. The models hold
their parts as separate surfaces, joined only where two vertices share a position (UV seams), so the parts are found by
position-welded islands and no face is cut. A part's UVs, normals and materials are the source's; mesh vertices only
shift so that the object's origin is the pivot. manifest.json holds each pivot and axis in the model's own units, the
source's hash and the checks.

The models weren't made to move, so the motion is fitted: the grip's twist axis is the one nearest the collar ring's
centre at which the grip clears the base over the whole twist, and the handle is lifted the least that clears the ridge
across the slot over the slot's travel. A fit that finds no clear motion is reported, not hidden.
"""
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

# Which islands move: the centre of an island's bounds above `moveAboveZ` (the model's own units, Z up as imported).
ASSETS = {
    "joystick": {"moving": "grip", "moveAboveZ": 0.40, "ring": True, "twistDeg": 45.0},
    # slide: how far the handle may go along X from where it was modelled, measured from the slot's ends on the base's
    # height map (research/47); the midpoint is the throttle's neutral.
    "throtle-control": {"moving": "handle", "moveAboveZ": 0.30, "ring": False, "slide": (-0.06, 0.32)},
}


def Newest(root, name):
    found = []
    for dirpath, _, files in os.walk(os.path.join(root, "repairs")):
        parts = dirpath.replace("\\", "/").split("/")
        if name not in parts[-2:]:
            continue
        for f in files:
            if f in ("Repaired.glb", "Backed_Source.glb"):
                m = re.fullmatch(r"v(\d+)", parts[-1])
                found.append((int(m.group(1)) if m else 1, os.path.join(dirpath, f)))
    if found:
        return max(found)[1]
    return os.path.join(root, name + ".glb")


def Sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def Import(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if len(meshes) != 1:
        raise SystemExit(f"{path}: expected one mesh, found {len(meshes)}")
    o = meshes[0]
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return o


def Islands(bm):
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def key(v):
        return (round(v.co.x, 5), round(v.co.y, 5), round(v.co.z, 5))

    for f in bm.faces:
        ks = [key(v) for v in f.verts]
        for k in ks[1:]:
            parent[find(k)] = find(ks[0])
    groups = {}
    for f in bm.faces:
        groups.setdefault(find(key(f.verts[0])), []).append(f)
    return sorted(groups.values(), key=len, reverse=True)


def Bounds(faces):
    vs = {v for f in faces for v in f.verts}
    lo = Vector([min(v.co[a] for v in vs) for a in range(3)])
    hi = Vector([max(v.co[a] for v in vs) for a in range(3)])
    return lo, hi


def Tree(mesh, keep_indices, matrix=None):
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.faces.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index not in keep_indices], context="FACES")
    if matrix is not None:
        bm.transform(matrix)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    return tree


def FitTwist(mesh, fixed, moving, centre, z, limit):
    base = Tree(mesh, fixed)
    angles = [limit, limit / 2, -limit / 2, -limit]

    def clashes(x, y):
        at = Vector((x, y, z))
        return sum(len(base.overlap(Tree(mesh, moving, Matrix.Translation(at) @ Matrix.Rotation(a * 0.017453293, 4, "Z") @ Matrix.Translation(-at))))
                   for a in angles)

    best = None
    for ix in range(-10, 11):
        for iy in range(-10, 11):
            x, y = centre.x + ix * 0.005, centre.y + iy * 0.005
            score = (clashes(x, y), (ix * ix + iy * iy))
            if best is None or score < best[0]:
                best = (score, x, y)
    return Vector((best[1], best[2], z)), best[0][0]


def FitSlide(mesh, fixed, moving, slide):
    base = Tree(mesh, fixed)
    steps = [slide[0] + (slide[1] - slide[0]) * i / 12 for i in range(13)]
    best = None
    for k in range(0, 21):
        lift = k * 0.005
        n = sum(len(base.overlap(Tree(mesh, moving, Matrix.Translation((dx, 0, lift))))) for dx in steps)
        if best is None or n < best[0]:
            best = (n, lift)
        if n == 0:
            break
    return best[1], best[0]


def Part(source, name, keep_indices, pivot, lift=0.0):
    o = source.copy()
    o.data = source.data.copy()
    o.name = o.data.name = name
    bpy.context.scene.collection.objects.link(o)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index not in keep_indices], context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bmesh.ops.translate(bm, verts=bm.verts, vec=-pivot)
    tris = sum(len(f.verts) - 2 for f in bm.faces)
    bm.to_mesh(o.data)
    bm.free()
    o.location = pivot + Vector((0.0, 0.0, lift))
    return o, {"triangles": tris, "pivot": list(pivot), "lift": lift}


def Export(o, path):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    # The object's own place stays in the node: the parts reassemble where they were.
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_format="GLB", export_image_format="AUTO", export_apply=False)


def Preview(objects, path, view):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.color_type = "TEXTURE"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.show_backface_culling = False
    sc.render.resolution_x = sc.render.resolution_y = 720
    cam = bpy.data.cameras.new("preview")
    cam.type = "ORTHO"
    co = bpy.data.objects.new("preview", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    pts = [o.matrix_world @ Vector(b) for o in objects for b in o.bound_box]
    lo = Vector([min(p[a] for p in pts) for a in range(3)])
    hi = Vector([max(p[a] for p in pts) for a in range(3)])
    size = max(hi - lo)
    cam.ortho_scale = size * 1.25
    d = Vector(view).normalized()
    co.location = (lo + hi) / 2 + d * size * 3
    co.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    others = [o for o in sc.objects if o.type == "MESH" and o not in objects]
    for o in others:
        o.hide_render = True
    for o in objects:
        o.hide_render = False
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(co)


def Run(root, previews):
    out = os.path.join(root, "detached")
    os.makedirs(out, exist_ok=True)
    manifest = {}
    for name, cfg in ASSETS.items():
        src = Newest(root, name)
        with tempfile.TemporaryDirectory() as tmp:
            copy = os.path.join(tmp, "source.glb")
            shutil.copyfile(src, copy)  # another job may rewrite the source while this reads it
            digest = Sha(copy)
            o = Import(copy)
            bm = bmesh.new()
            bm.from_mesh(o.data)
            bm.faces.ensure_lookup_table()
            moving, fixed = set(), set()
            ring = None
            rows = []
            for fs in Islands(bm):
                lo, hi = Bounds(fs)
                moves = ((lo + hi) / 2).z > cfg["moveAboveZ"]
                (moving if moves else fixed).update(f.index for f in fs)
                rows.append({"faces": len(fs), "lo": list(lo), "hi": list(hi), "moves": moves})
                # The collar's ring: the flat round island under the grip.
                if (cfg["ring"] and not moves and 0.02 < hi.z - lo.z < 0.08 and abs((hi.x - lo.x) - (hi.y - lo.y)) < 0.02
                        and 0.1 < hi.x - lo.x < 0.3):
                    ring = (lo, hi)
            mv = [bm.faces[i] for i in moving]
            mlo, _ = Bounds(mv)
            info = {"source": os.path.relpath(src, root).replace("\\", "/"), "sourceSha256": digest, "faces": len(bm.faces), "islands": rows}
            if cfg["ring"]:
                if ring is None:
                    raise SystemExit(f"{name}: no collar ring found; the twist axis can't be measured")
                c = (ring[0] + ring[1]) / 2
                lift = 0.0
                pivot, clash = FitTwist(o.data, fixed, moving, c, ring[1].z, cfg["twistDeg"])
                info["motion"] = {"kind": "twist", "axis": [0.0, 0.0, 1.0], "limitDeg": cfg["twistDeg"], "ringCentre": list(c), "clashes": clash}
            else:
                # The slide: the handle's stem meets the base at its lowest vertices.
                vs = {v for f in mv for v in f.verts}
                foot = [v.co for v in vs if v.co.z <= mlo.z + 0.02]
                c = sum(foot, Vector()) / len(foot)
                pivot = Vector((c.x, c.y, mlo.z))
                lift, clash = FitSlide(o.data, fixed, moving, cfg["slide"])
                info["motion"] = {"kind": "slide", "axis": [1.0, 0.0, 0.0], "min": cfg["slide"][0], "max": cfg["slide"][1],
                                  "neutral": (cfg["slide"][0] + cfg["slide"][1]) / 2, "lift": lift, "clashes": clash}
            bm.free()
            source_tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
            b, bi = Part(o, f"{name}-base", fixed, pivot)
            m, mi = Part(o, f"{name}-{cfg['moving']}", moving, pivot, lift)
            if bi["triangles"] + mi["triangles"] != source_tris:
                raise SystemExit(f"{name}: the parts' triangles don't add up to the source's")
            o.hide_set(True)
            o.hide_render = True
            Export(b, os.path.join(out, f"{name}-base.glb"))
            Export(m, os.path.join(out, f"{name}-{cfg['moving']}.glb"))
            info["base"], info["moving"], info["movingName"] = bi, mi, cfg["moving"]
            manifest[name] = info
            print(f"RESULT {name}: {info['source']}: base {bi['triangles']} tris, {cfg['moving']} {mi['triangles']} tris, pivot {tuple(round(p, 4) for p in pivot)}, motion {info['motion']}")
            if previews:
                view = (1, -1, 0.7)
                Preview([b, m], os.path.join(out, f"{name}-together.png"), view)
                Preview([b], os.path.join(out, f"{name}-base.png"), view)
                Preview([m], os.path.join(out, f"{name}-{cfg['moving']}.png"), view)
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:]
    Run(args[0], "--previews" in args)
