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

The joystick's base also gives up one of its three modelled toggles (the one nearest the pilot) as the Mako's auto-turret
switch (the owner, 2026-11-18: "a basic switch in the mako to engage auto-turret", "the base of the joystick"):
`joystick-switch.glb` is its lever, split off the toggle's island by height over the toggle's plate (every face with a
vertex more than `cut` above the plate's top; the plate's normal measured from the model), its origin the lever's foot on
the plate, hinged so a turn about `axisOn` by `onDeg` tips it from its modelled pose (off) toward the plate's upper end (on). The painted lamp at the toggle's
upper end (found by its lit texels) keeps its paint: the faces its glow lies on take a material of their own, "TurretLamp",
on a cut-out of the texture, and `detached/joystick-base-TurretLamp-on.png` is that cut-out with the glow turned blue
(the owner, 2026-11-18: the colour changes, nothing is laid over it); the packager makes it the element's "on" instance,
which the game swaps in. manifest.json's `joystick.switch` has the pivot, the axis, the throw, the lever's tip at rest
(off) and the lamp.
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
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

# Which islands move: the centre of an island's bounds above `moveAboveZ` (the model's own units, Z up as imported).
ASSETS = {
    "joystick": {"moving": "grip", "moveAboveZ": 0.40, "ring": True, "twistDeg": 45.0, "switch": {
        # the toggle nearest the pilot (model +x faces the seat; the three toggles' islands lie along y, this one lowest),
        # the island whose bounds' centre is nearest `near`
        "near": (0.40, -0.178, 0.16),
        # the toggle's plate: its normal (measured on 2026-11-18 from the plate's slope, 40 degrees off level toward +x), the
        # lever every face with a vertex this far above the plate's top (the bezel round the slot stands 0.018 above it)
        "normal": (0.64, 0.0, 0.77), "cut": 0.022,
        # + about this tips the lever toward the plate's upper end (-x, +z): on. The lever's thin side faces along the
        # plate's short axis, so it flips that way. As modelled it leans 10 degrees toward the lower end (off); on is this
        # many degrees from there (14 past the plate's normal toward the upper end)
        "axisOn": (0.0, -1.0, 0.0), "onDeg": 24.0,
        }},
    # slide: how far the handle may go along X from where it was modelled, measured from the slot's ends on the base's
    # height map; the midpoint is the throttle's neutral. Pushing away from the seat is forward, pulling back reverse;
    # the switch panel (+X) is assumed to face the seat, so forward is -X.
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


SAMPLE = 0.0004  # the lamp's texels are sampled this far apart on the model (units: about 0.1 mm on the stick's base)


def Lit(colour):
    """Which texels (an array of RGB, the last axis) are a painted lamp itself: its orange rim, fading to red 0.6, or its
    yellow core, all with blue under 0.2, not the bluer, duller haze painted round it (measured across the switch's bar,
    2026-11-18: core 0.96 0.82 0.06, rim 0.91 0.51 0.07 fading to 0.65 0.31 0.13, haze 0.5-0.7 0.4-0.5 0.2-0.3)."""
    r, g, b = colour[..., 0], colour[..., 1], colour[..., 2]
    return (r > 0.6) & (b < 0.2) & (g <= r) & ((r >= 1.25 * g) | (r > 0.85))


def Switch(o, bm, cfg):
    """The auto-turret switch on the joystick's base (the module's docstring): the lever's face indices, its pivot, the faces the
    lamp's glow lies on, and what was measured, from the toggle's island."""
    near = Vector(cfg["near"])
    island = min(Islands(bm), key=lambda fs: ((sum(Bounds(fs), Vector()) / 2) - near).length)
    lo, hi = Bounds(island)
    if (((lo + hi) / 2) - near).length > 0.03 or not (300 < len(island) < 600):
        raise SystemExit(f"joystick switch: the island nearest {tuple(near)} ({len(island)} faces at {tuple(round(c, 3) for c in (lo + hi) / 2)}) isn't the toggle")
    n = Vector(cfg["normal"]).normalized()
    # The plate's top: the area-weighted commonest height among the faces facing along the normal.
    heights = {}
    for f in island:
        if f.normal.dot(n) > 0.9:
            k = round(f.calc_center_median().dot(n) / 0.0025)
            heights[k] = heights.get(k, 0.0) + f.calc_area()
    top = max(heights, key=heights.get) * 0.0025
    lever = [f for f in island if max(v.co.dot(n) for v in f.verts) > top + cfg["cut"]]
    if not (40 <= len(lever) <= 120):
        raise SystemExit(f"joystick switch: {len(lever)} lever faces (expected 40 to 120): the cut missed it")
    vs = {v for f in lever for v in f.verts}
    centre = sum((v.co for v in vs), Vector()) / len(vs)
    pivot = centre + n * (top - centre.dot(n))
    peak = max(v.co.dot(n) for v in vs)
    crest = [v.co for v in vs if v.co.dot(n) > peak - 0.004]
    tip = sum(crest, Vector()) / len(crest)
    # The lamp: orange texels on the plate's upper end, past the slot (toward -t, the way the lever tips on), on faces that
    # look out of the plate, sampled on each triangle.
    image = next(nd.image for nd in o.data.materials[0].node_tree.nodes if nd.type == "TEX_IMAGE" and nd.image)
    w, h = image.size
    px = np.empty(w * h * 4, np.float32)
    image.pixels.foreach_get(px)
    px = px.reshape(h, w, 4)
    uv = bm.loops.layers.uv.active
    down = Vector((0.0, 1.0, 0.0)).cross(n).normalized()  # along the plate toward its lower end (+x, -z)
    slot = min((v.co - pivot).dot(down) for v in vs)  # the lever's upper side
    points = []
    # The bar is painted across the toggle's island and the base's own surface round it (2026-11-18: its lower end lies on the
    # main island), so every face near the toggle counts, not only its island's.
    reach = 0.01
    lever_faces = set(lever)

    def Overlaps(f):  # the face's box meets the toggle's (a big triangle of the base can cross the bar with its middle far off)
        return all(min(v.co[i] for v in f.verts) <= hi[i] + reach and max(v.co[i] for v in f.verts) >= lo[i] - reach for i in range(3))

    near_faces = [f for f in bm.faces if Overlaps(f)]
    for f in near_faces:
        if f in lever_faces or f.material_index != 0 or f.normal.dot(n) < 0.5 or len(f.loops) != 3:
            continue
        corners = [(l.vert.co.copy(), l[uv].uv.copy()) for l in f.loops]
        # samples SAMPLE apart or closer along each edge: the bar is painted across two big triangles (2026-11-18: at 15 a
        # side the samples were 3 mm apart in the model and the lamp face fell short of the bar's ends)
        longest = max((corners[i][0] - corners[i - 1][0]).length for i in range(3))
        count = min(200, max(15, int(longest / SAMPLE) + 1))
        P = np.array([c[0][:] for c in corners])
        Q = np.array([c[1][:] for c in corners])
        grid = np.array([(i / count, j / count) for i in range(count + 1) for j in range(count + 1 - i)])
        weights = np.column_stack([grid, 1.0 - grid.sum(1)])
        at = weights @ P
        uvs = weights @ Q
        colours = px[(uvs[:, 1] * h).astype(int) % h, (uvs[:, 0] * w).astype(int) % w, :3]
        keep = ((at - np.array(pivot[:])) @ np.array(down[:]) < slot) & Lit(colours)
        points.extend(at[keep])
    if len(points) < 50:
        raise SystemExit(f"joystick switch: {len(points)} lamp texels found (expected a painted bar)")
    pts = np.array(points)
    mid = Vector(pts.mean(0))
    # The glow's faces: every textured face near the bar (within GLOW_REACH of its middle) whose texels there are lit at all
    # (orange over the grey: the bar, its rim and the haze round it, front and inward copies alike). They take a material of
    # their own on a cut-out of the texture (GlowMaterial), so the game can swap that element's look without touching the rest.
    glow = set()
    for f in bm.faces:
        if f in lever_faces or f.material_index != 0 or len(f.loops) != 3:
            continue
        if min((v.co - mid).length for v in f.verts) > GLOW_REACH and (f.calc_center_median() - mid).length > GLOW_REACH:
            continue
        corners = [(l.vert.co.copy(), l[uv].uv.copy()) for l in f.loops]
        count = 40
        P = np.array([c[0][:] for c in corners])
        Q = np.array([c[1][:] for c in corners])
        grid = np.array([(i / count, j / count) for i in range(count + 1) for j in range(count + 1 - i)])
        weights = np.column_stack([grid, 1.0 - grid.sum(1)])
        at = weights @ P
        uvs = weights @ Q
        colours = px[(uvs[:, 1] * h).astype(int) % h, (uvs[:, 0] * w).astype(int) % w, :3]
        close = np.linalg.norm(at - np.array(mid[:]), axis=1) < GLOW_REACH
        if (close & (Glowing(colours) > 0.5)).sum() > 3:
            glow.add(f.index)
    lamp = {"centre": list(mid), "texels": len(points), "glowFaces": len(glow),
            "from": [float(pts.min(0)[i]) for i in range(3)], "to": [float(pts.max(0)[i]) for i in range(3)]}
    info = {"island": len(island), "plateNormal": list(n), "plateTop": top, "leverFaces": len(lever), "pivot": list(pivot),
            "axisOn": list(Vector(cfg["axisOn"]).normalized()), "onDeg": cfg["onDeg"], "tipRest": list(tip),
            "leverHeight": peak - top, "lamp": lamp, "lampMaterial": GLOW_MATERIAL}
    return {f.index for f in lever}, pivot, glow, info


GLOW_REACH = 0.03  # the glow's faces lie this close to the bar's middle (model units: about 9 mm)
GLOW_MATERIAL = "TurretLamp"  # the packager names its instances Joystick_TurretLamp_MIC and, from the sidecar, _On_MIC


def Glowing(colour):
    """How much of the painted lamps' orange each texel holds (0..1; an array of RGB on the last axis): the bar, its rim and
    the haze round it, not the grey metal (whose red stands no higher than its blue)."""
    r, b = colour[..., 0], colour[..., 2]
    return np.clip((r - b - 0.08) / 0.2, 0.0, 1.0) * np.clip((r - 0.3) / 0.2, 0.0, 1.0)


def Recoloured(rgb):
    """The painted glow turned blue (the owner, 2026-11-18: "change the color for it to actually just be blue"): each texel's
    hue, orange to yellow, moved to a blue (rim 205 degrees, core 215), its saturation and brightness kept, as much as it
    glows (Glowing), so the rim, the core and the haze keep their shape. sRGB values in, sRGB values out."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    top = np.max(rgb, axis=-1)
    low = np.min(rgb, axis=-1)
    span = np.maximum(top - low, 1e-6)
    hue = np.where(top == r, ((g - b) / span) % 6.0, np.where(top == g, (b - r) / span + 2.0, (r - g) / span + 4.0)) / 6.0
    sat = np.where(top > 0, (top - low) / np.maximum(top, 1e-6), 0.0)
    new_hue = np.clip(0.57 + (hue - 0.06) * 0.3, 0.55, 0.62)
    h6 = new_hue * 6.0
    k = lambda m: (m + h6) % 6.0  # noqa: E731
    f = lambda m: top - top * sat * np.clip(np.minimum(k(m), 4.0 - k(m)), 0.0, 1.0)  # noqa: E731
    blue = np.stack([f(5.0), f(3.0), f(1.0)], axis=-1)
    weight = Glowing(rgb)[..., None]
    return rgb * (1.0 - weight) + blue * weight


def GlowMaterial(o, faces, folder, stem, name):
    """Gives `faces` (indices into o's mesh) a material of their own whose image is the cut-out of the model's texture they use,
    their UVs moved into it, and writes the same cut-out recoloured (Recoloured) beside the detached models as
    <stem>-<name>-on.png for the packager. The cut-out: the faces' UV box and 4 texels round it, at the texture's own resolution, its
    sides powers of two. Returns its size."""
    mesh = o.data
    image = next(nd.image for nd in mesh.materials[0].node_tree.nodes if nd.type == "TEX_IMAGE" and nd.image)
    w, h = image.size
    px = np.empty(w * h * 4, np.float32)
    image.pixels.foreach_get(px)
    px = px.reshape(h, w, 4)
    uv = mesh.uv_layers.active.data
    loops = [li for fi in faces for li in mesh.polygons[fi].loop_indices]
    us = np.array([uv[li].uv.x for li in loops])
    vs = np.array([uv[li].uv.y for li in loops])
    x0, x1 = int(np.floor(us.min() * w)) - 4, int(np.ceil(us.max() * w)) + 4
    y0, y1 = int(np.floor(vs.min() * h)) - 4, int(np.ceil(vs.max() * h)) + 4
    cw = 1 << max(2, (x1 - x0 - 1).bit_length())
    ch = 1 << max(2, (y1 - y0 - 1).bit_length())
    x0 = min(max(0, x0 - (cw - (x1 - x0)) // 2), w - cw)
    y0 = min(max(0, y0 - (ch - (y1 - y0)) // 2), h - ch)
    crop = px[y0:y0 + ch, x0:x0 + cw].copy()

    def Image(tag, pixels):
        img = bpy.data.images.new(tag, cw, ch, alpha=False)
        img.pixels.foreach_set(pixels.ravel())
        return img

    cut = Image(name, crop)
    cut.pack()
    on = crop.copy()
    on[..., :3] = Recoloured(crop[..., :3])
    on_image = Image(name + "-on", on)
    on_image.filepath_raw = os.path.abspath(os.path.join(folder, f"{stem}-{name}-on.png"))
    on_image.file_format = "PNG"
    on_image.save()
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    bsdf = next(nd for nd in material.node_tree.nodes if nd.type == "BSDF_PRINCIPLED")
    tex = material.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = cut
    material.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    mesh.materials.append(material)
    index = len(mesh.materials) - 1
    for fi in faces:
        poly = mesh.polygons[fi]
        poly.material_index = index
        for li in poly.loop_indices:
            uv[li].uv = ((uv[li].uv.x * w - x0) / cw, (uv[li].uv.y * h - y0) / ch)
    return cw, ch, x0, y0


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
            switch = None
            if "switch" in cfg:
                lever, switch_pivot, glow_faces, switch_info = Switch(o, bm, cfg["switch"])
                fixed -= lever
                switch = (lever, switch_pivot)
                info["switch"] = switch_info
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
                info["motion"] = {"kind": "slide", "axis": [1.0, 0.0, 0.0], "forward": [-1.0, 0.0, 0.0], "min": cfg["slide"][0], "max": cfg["slide"][1],
                                  "neutral": (cfg["slide"][0] + cfg["slide"][1]) / 2, "lift": lift, "clashes": clash}
            bm.free()
            if "switch" in cfg:
                size = GlowMaterial(o, sorted(glow_faces), out, f"{name}-base", GLOW_MATERIAL)
                info["switch"]["lamp"]["cutout"] = {"size": list(size[:2]), "at": list(size[2:]), "on": f"{name}-base-{GLOW_MATERIAL}-on.png"}
            source_tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
            b, bi = Part(o, f"{name}-base", fixed, pivot)
            m, mi = Part(o, f"{name}-{cfg['moving']}", moving, pivot, lift)
            extra = []
            if switch is not None:
                s, si = Part(o, f"{name}-switch", switch[0], switch[1])
                extra.append(s)
                info["switch"]["triangles"] = si["triangles"]
                if bi["triangles"] + mi["triangles"] + si["triangles"] != source_tris:
                    raise SystemExit(f"{name}: the parts' triangles (with the switch) don't add up to the source's")
            elif bi["triangles"] + mi["triangles"] != source_tris:
                raise SystemExit(f"{name}: the parts' triangles don't add up to the source's")
            o.hide_set(True)
            o.hide_render = True
            Export(b, os.path.join(out, f"{name}-base.glb"))
            Export(m, os.path.join(out, f"{name}-{cfg['moving']}.glb"))
            for x in extra:
                Export(x, os.path.join(out, f"{x.name}.glb"))
                if previews:
                    Preview([b, m, x], os.path.join(out, f"{name}-switch-together.png"), (1, -0.3, 0.9))
            info["base"], info["moving"], info["movingName"] = bi, mi, cfg["moving"]
            manifest[name] = info
            print(f"RESULT {name}: {info['source']}: base {bi['triangles']} tris, {cfg['moving']} {mi['triangles']} tris, pivot {tuple(round(p, 4) for p in pivot)}, motion {info['motion']}")
            if "switch" in info:
                sw = info["switch"]
                print(f"RESULT {name} switch: lever {sw['triangles']} tris, {sw['leverHeight']:.4f} tall, pivot {tuple(round(p, 4) for p in sw['pivot'])}, "
                      f"on {sw['onDeg']:g} degrees about {tuple(round(p, 3) for p in sw['axisOn'])}; the lamp's glow on {sw['lamp']['glowFaces']} faces "
                      f"round {tuple(round(p, 4) for p in sw['lamp']['centre'])}, a {sw['lamp']['cutout']['size'][0]} x {sw['lamp']['cutout']['size'][1]} cut-out "
                      f"and its blue copy {sw['lamp']['cutout']['on']}")
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
