# SPDX-License-Identifier: MIT
"""Builds the Mako cockpit's hull: Tripo parts placed against a template, and panels modelled where they leave it open
(research/48).

    blender -b --factory-startup --python tools/cockpit/hull_assemble.py -- "<concept/assets/hull reconstruction>" [--refit]

--refit only prints candidate places for the side pane (Refit), to choose PANE_SCALE and PANE_CENTRE from.

The template (`hull full -bad.glb`: a whole cockpit as one rough Tripo mesh) places the parts and is never shipped.
The side pane (`side pane.glb`) is the left wall turned a quarter turn, scaled and placed as it fitted the template's
interior (the faces seen from inside it), stood on the floor; the right wall is its mirror. The console
(`front computer console.glb`) is centred on the floor with its back on the template's front. Every Tripo model is
about one unit across, so each has its own scale.

The fillers are modelled from the placed parts' bounds: the floor, the ceiling (ribs and a lowered centre), the front
wall (a frame for a screen), the front corners between the console and the walls (chamfered pillars with a red stripe)
and the side skins behind the walls. They are tiled with rectangles of the side pane's own atlas (a bolted plate, a
light frame, a red stripe, a plain dark strip), so they share its texture.

Writes <folder>/assembled/: each piece as a .glb in the hull's coordinates (transforms applied), hull.glb with all of
them, manifest.json (placements, triangles, how much of the template's interior is covered, which rays from the seat
leave the hull) and previews/ (the seat's view of the template and of the hull, the hull from outside).
"""
import json
import math
import os
import sys
import tempfile

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

TEMPLATE = "hull full -bad.glb"
PANE = "side pane.glb"
CONSOLE = "front computer console.glb"
# The side pane's place, fitted to the template's interior on 2026-11-02 (research/48): a quarter turn about z, this
# scale, its bounding box's centre here; then stood on the floor.
PANE_SCALE = 0.5951
PANE_CENTRE = (-0.3647, -0.1318, 0.2748)
CONSOLE_SCALE = 0.6
# Rectangles (u0, v0, u1, v1) of the side pane's 4096 x 4096 atlas.
PLATE = (2075 / 4096, 2952 / 4096, 2392 / 4096, 3460 / 4096)  # a plate with four bolts and a bevelled edge, 317 x 508 px
PLATE_ASPECT = 317 / 508
LIGHT = (0.71875, 0.15625, 0.7265625, 0.1875)
RED = (0.7109375, 0.296875, 0.71875, 0.3359375)
DARK = (0.0, 0.0078125, 0.0234375, 0.4296875)
SLAB = 0.02  # the fillers' thickness
EYE = (0.0, -0.22, 0.36)  # where the previews and the enclosure rays look from: the seat, in the hull's units


def Import(path):
    """The file's one mesh, its world transform applied, as (object, bmesh)."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    o = next(x for x in bpy.data.objects if x not in before and x.type == "MESH")
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.transform(o.matrix_world)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bm.verts.index_update()
    bm.faces.index_update()
    return o, bm


def Placed(name, source, bm, matrix, mirrored=False):
    """A new object holding `bm` moved by `matrix` (a mirror reverses the faces back outward), normals smooth by angle."""
    bm = bm.copy()
    bm.transform(matrix)
    if mirrored:
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for m in source.data.materials:
        me.materials.append(m)
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    Smooth(o)
    return o


def Smooth(o):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    if o.data.has_custom_normals:
        bpy.ops.mesh.customdata_custom_splitnormals_clear()  # a turned or mirrored part's stored normals no longer fit it
    o.data.shade_smooth()
    o.data.set_sharp_from_angle(angle=math.radians(30))


def Bounds(o, select=None):
    co = np.array([v.co[:] for v in o.data.vertices])
    if select is not None:
        co = co[select(co)]
    return co.min(0), co.max(0)


class Builder:
    """Quads with their own UVs; with `mirror` set, x is negated and the winding reversed, which keeps faces outward."""

    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.mirror = False

    def Face(self, points, uvs):
        points = [Vector(p) for p in points]
        if self.mirror:
            points = [Vector((-p.x, p.y, p.z)) for p in reversed(points)]
            uvs = list(reversed(uvs))
        f = self.bm.faces.new([self.bm.verts.new(p) for p in points])
        for loop, uv in zip(f.loops, uvs):
            loop[self.uv].uv = uv
        return f


def Fit(rect, turn=False):
    u0, v0, u1, v1 = rect
    uv = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
    return uv[1:] + uv[:1] if turn else uv


def Grid(b, o, a, c, na, nc, trim):
    """The rectangle (corner o, sides a and c, facing a x c) as na x nc quads, each with the whole trim."""
    for i in range(na):
        for j in range(nc):
            p = o + a * (i / na) + c * (j / nc)
            da, dc = a / na, c / nc
            b.Face([p, p + da, p + da + dc, p + dc], Fit(trim))


def Plates(b, o, a, c, size, na=None, nc=None):
    """The rectangle as bolted plates about `size` on their long side, the long side along the rectangle's longer one."""
    la, lc = a.length, c.length
    turn = la > lc
    long_, short = (la, lc) if turn else (lc, la)
    n_long = max(1, round(long_ / size))
    n_short = max(1, round(short / (size * PLATE_ASPECT)))
    na = na or (n_long if turn else n_short)
    nc = nc or (n_short if turn else n_long)
    for i in range(na):
        for j in range(nc):
            p = o + a * (i / na) + c * (j / nc)
            da, dc = a / na, c / nc
            b.Face([p, p + da, p + da + dc, p + dc], Fit(PLATE, turn))
    return na, nc


def Slab(b, o, a, c, depth, size=0.2, sides=DARK, room=None):
    """A slab whose room face is the rectangle (o, a, c), facing a x c, `depth` behind it. Its sides and back follow the
    room face's divisions, so no edge ends on another's middle."""
    n = a.cross(c).normalized()
    back = -n * depth
    if room is None:
        na, nc = Plates(b, o, a, c, size)
    else:
        na, nc = 1, 1
        b.Face([o, o + a, o + a + c, o + c], Fit(room))
    Grid(b, o + back + c, a, -c, na, nc, DARK)  # the back, facing away
    for p, d, k in ((o, a, na), (o + a, c, nc), (o + a + c, -a, na), (o + c, -c, nc)):
        for i in range(k):
            q0, q1 = p + d * (i / k), p + d * ((i + 1) / k)
            b.Face([q0, q0 + back, q1 + back, q1], Fit(sides))


CUBE = ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5))


def Box(b, lo, hi, trim, axes=None):
    """A box from lo to hi (in `axes`' frame: x, y, z unit vectors and an origin), each face the whole trim."""
    ox, ax, ay, az = axes or (Vector(), Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
    corners = [ox + ax * (hi[0] if i & 1 else lo[0]) + ay * (hi[1] if i & 2 else lo[1]) + az * (hi[2] if i & 4 else lo[2]) for i in range(8)]
    for f in CUBE:
        b.Face([corners[i] for i in f], Fit(trim))


def Prism(b, footprint, z0, z1, trims, rows):
    """An upright prism over a counter-clockwise footprint; side k gets trims[k] (PLATE tiles in `rows` rows), caps dark."""
    up = Vector((0, 0, z1 - z0))
    for k in range(len(footprint)):
        p, q = Vector((*footprint[k], z0)), Vector((*footprint[(k + 1) % len(footprint)], z0))
        if trims[k] is PLATE:
            Plates(b, p, q - p, up, 1.0, na=1, nc=rows)
        else:
            Grid(b, p, q - p, up, 1, rows, trims[k])
    for z, order in ((z1, footprint), (z0, list(reversed(footprint)))):
        pts = [Vector((x, y, z)) for x, y in order]
        lo = [min(p[i] for p in pts) for i in range(2)]
        hi = [max(p[i] for p in pts) for i in range(2)]
        u0, v0, u1, v1 = DARK
        uvs = [(u0 + (u1 - u0) * (p.x - lo[0]) / (hi[0] - lo[0]), v0 + (v1 - v0) * (p.y - lo[1]) / (hi[1] - lo[1])) for p in pts]
        f = b.bm.faces.new([b.bm.verts.new(Vector((-p.x, p.y, p.z)) if b.mirror else p) for p in (reversed(pts) if b.mirror else pts)])
        for loop, uv in zip(f.loops, reversed(uvs) if b.mirror else uvs):
            loop[b.uv].uv = uv


def Fillers(pane, console):
    """The panels the parts leave open, measured from them (the right pane: x > 0)."""
    lo, hi = Bounds(pane)
    xo, yr, yf = hi[0], lo[1], hi[1]
    _, rear_top = Bounds(pane, lambda c: c[:, 1] < yr + 0.03)
    zc = rear_top[2]  # the ceiling meets the panes' tops at their rear ends
    clo, chi = Bounds(console)
    xc, yw = chi[0], chi[1]
    dims = {"wallX": xo, "rearY": yr, "paneFrontY": yf, "ceilingZ": zc, "consoleHalfWidth": xc, "frontY": yw}
    b = Builder()
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    W, L = 2 * xo, yw - yr
    Slab(b, Vector((-xo, yr, 0)), X * W, Y * L, SLAB)                       # floor, facing up
    Slab(b, Vector((-xo, yr, zc)), Y * L, X * W, SLAB)                      # ceiling, facing down
    Slab(b, Vector((-xo, yw, 0)), X * W, Z * zc, SLAB)                      # front wall, facing back
    # the ceiling's lowered centre and its ribs
    drop, half = 0.035, 0.09
    Slab(b, Vector((-half, yr + 0.03, zc - drop)), Y * (L - 0.06), X * (2 * half), drop, size=0.18, sides=LIGHT)
    for y in np.linspace(yr + 0.06, yw - 0.06, 7):
        for s in (1, -1):
            x0, x1 = sorted((s * half, s * (xc - 0.0)))
            Box(b, (x0, y - 0.011, zc - 0.025), (x1, y + 0.011, zc), LIGHT)
    # the front wall's frame for a screen
    fx, fz0, fz1, bar, out = 0.24, 0.27, 0.50, 0.018, 0.012
    for lo_, hi_ in (((-fx, yw - out, fz0), (fx, yw, fz0 + bar)), ((-fx, yw - out, fz1 - bar), (fx, yw, fz1)),
                     ((-fx, yw - out, fz0 + bar), (-fx + bar, yw, fz1 - bar)), ((fx - bar, yw - out, fz0 + bar), (fx, yw, fz1 - bar))):
        Box(b, lo_, hi_, LIGHT)
    b.Face([(-fx + bar, yw - 0.002, fz0 + bar), (fx - bar, yw - 0.002, fz0 + bar), (fx - bar, yw - 0.002, fz1 - bar), (-fx + bar, yw - 0.002, fz1 - bar)], Fit(DARK))
    for mirror in (False, True):
        b.mirror = mirror
        # the side skin behind the wall, facing in
        Slab(b, Vector((xo, yr, 0)), Z * zc, Y * L, SLAB)
        # the front corner: from the console's side to the wall, chamfered toward the seat
        ch = 0.06
        foot = [(xc, yf + ch), (xc + ch, yf), (xo, yf), (xo, yw), (xc, yw)]
        Prism(b, foot, 0.0, zc, [LIGHT, PLATE, DARK, DARK, PLATE], rows=2)
        # a red stripe on the chamfer, standing 3 mm off it
        p, q = Vector((xc, yf + ch, 0)), Vector((xc + ch, yf, 0))
        along = (q - p).normalized()
        outward = along.cross(Z)
        mid = (p + q) / 2
        Box(b, (0.0, -0.0125, 0.30), (0.003, 0.0125, 0.46), RED, axes=(mid, outward, along, Z))  # outward x along = up
    b.mirror = False
    bmesh.ops.remove_doubles(b.bm, verts=b.bm.verts, dist=1e-6)
    me = bpy.data.meshes.new("fillers")
    b.bm.to_mesh(me)
    b.bm.free()
    for m in pane.data.materials:
        me.materials.append(m)
    o = bpy.data.objects.new("fillers", me)
    bpy.context.scene.collection.objects.link(o)
    o.data.shade_flat()
    return o, dims


def Fibonacci(n):
    i = np.arange(n) + 0.5
    phi = np.arccos(1 - 2 * i / n)
    th = np.pi * (1 + 5 ** 0.5) * i
    return np.stack([np.cos(th) * np.sin(phi), np.sin(th) * np.sin(phi), np.cos(phi)], 1)


def Tree(objects):
    bm = bmesh.new()
    for o in objects:
        part = bmesh.new()
        part.from_mesh(o.data)
        part.transform(o.matrix_world)
        me = bpy.data.meshes.new("t")
        part.to_mesh(me)
        part.free()
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    return tree


def Escapes(tree, eye, lo, hi):
    """The share of rays from the eye that hit nothing, by the face of the box (lo, hi) they leave it through: the rear
    is the cockpit's open back, any other face a hole."""
    dirs = Fibonacci(4000)
    names = (("left", "right"), ("rear", "front"), ("floor", "ceiling"))
    out = {}
    for d in dirs.tolist():
        if tree.ray_cast(Vector(eye), Vector(d))[0] is not None:
            continue
        exits = [((hi[a] if d[a] > 0 else lo[a]) - eye[a]) / d[a] if abs(d[a]) > 1e-9 else math.inf for a in range(3)]
        a = int(np.argmin(exits))
        face = names[a][d[a] > 0]
        out[face] = out.get(face, 0) + 1
    return {k: round(v / len(dirs), 4) for k, v in sorted(out.items())}


def Interior(template_bm, template_tree):
    """The template's faces seen from inside it."""
    seen = set()
    dirs = Fibonacci(3000).tolist()
    for v in [(x, y, z) for x in (-0.25, 0.0, 0.25) for y in (-0.3, -0.1, 0.1) for z in (0.12, 0.3)]:
        for d in dirs:
            hit = template_tree.ray_cast(Vector(v), Vector(d))
            if hit[0] is not None:
                seen.add(hit[2])
    template_bm.faces.ensure_lookup_table()
    return [template_bm.faces[i] for i in sorted(seen)]


def Refit(itree, tlo, thi, bm, turn, facing, scales):
    """Candidate places for a part: its faces toward the room against the template's interior, first by a coarse search
    over scale and position on a distance field of that interior, then by trimmed ICP (scale and position). Returns
    them best first. A small part scores well inside any thin wall, so the scale range must be plausible, and the
    choice among candidates is made by looking (research/48)."""
    work = bm.copy()
    bmesh.ops.triangulate(work, faces=work.faces)
    work.normal_update()
    work.verts.index_update()
    co = np.array([(turn @ v.co)[:] for v in work.verts])
    tri = np.array([[v.index for v in f.verts] for f in work.faces])
    nrm = np.array([(turn.to_3x3() @ f.normal)[:] for f in work.faces])
    work.free()
    a, b, c = co[tri[:, 0]], co[tri[:, 1]], co[tri[:, 2]]
    area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
    rng = np.random.default_rng(1)
    pick = rng.choice(len(tri), size=6000, p=area / area.sum())
    r1, r2 = np.sqrt(rng.random(6000)), rng.random(6000)
    pts = a[pick] * (1 - r1)[:, None] + b[pick] * (r1 * (1 - r2))[:, None] + c[pick] * (r1 * r2)[:, None]
    centre = (co.min(0) + co.max(0)) / 2
    front = pts[nrm[pick] @ np.array(facing, float) > 0.3] - centre
    half = (co.max(0) - co.min(0)) / 2
    step = 0.01
    axes = [np.arange(tlo[i] - 0.05, thi[i] + 0.05 + 1e-9, step) for i in range(3)]
    field = np.array([h[3] for h in map(itree.find_nearest, np.stack(np.meshgrid(*axes, indexing="ij"), -1).reshape(-1, 3).tolist())])
    field = field.reshape([len(x) for x in axes])
    origin = np.array([x[0] for x in axes])

    def Field(p):
        idx = np.rint((p - origin) / step).astype(int)
        inside = np.all((idx >= 0) & (idx < np.array(field.shape)), axis=-1)
        idx = np.clip(idx, 0, np.array(field.shape) - 1)
        return np.where(inside, field[idx[..., 0], idx[..., 1], idx[..., 2]], 1.0)

    coarse = front[np.random.default_rng(3).choice(len(front), 300, replace=False)]
    seeds = []
    for s in np.geomspace(scales[0], scales[1], 16):
        grid = [np.arange(tlo[i] + half[i] * s - 0.04, thi[i] - half[i] * s + 0.04 + 1e-9, 0.02) for i in range(3)]
        if any(len(g) == 0 for g in grid):
            continue
        T = np.stack(np.meshgrid(*grid, indexing="ij"), -1).reshape(-1, 3)
        score = np.minimum(Field(coarse[None] * s + T[:, None]) / s, 0.1).mean(1)  # relative to the part's size
        seeds += [(float(score[i]), float(s), T[i]) for i in np.argsort(score)[:3]]
    found = []
    for _, s, t in sorted(seeds, key=lambda x: x[0])[:10]:
        for _ in range(50):
            hits = [itree.find_nearest(p) for p in (front * s + t).tolist()]
            q = np.array([h[0][:] for h in hits])
            d = np.array([h[3] for h in hits])
            keep = d <= np.percentile(d, 70)
            pa, pb = front[keep], q[keep]
            ma, mb = pa.mean(0), pb.mean(0)
            s2 = min(max(float(((pa - ma) * (pb - mb)).sum() / ((pa - ma) ** 2).sum()), scales[0]), scales[1])
            t2 = mb - s2 * ma
            moved = abs(s2 - s) + np.linalg.norm(t2 - t)
            s, t = s2, t2
            if moved < 1e-5:
                break
        d = np.array([h[3] for h in map(itree.find_nearest, (front * s + t).tolist())])
        found.append({"scale": round(s, 4), "centre": [round(x, 4) for x in t], "p50": round(float(np.percentile(d, 50)), 4),
                      "within1cm": round(float((d < 0.01).mean()), 3), "relative70": round(float(np.percentile(d, 70) / s), 4)})
    return sorted(found, key=lambda r: r["relative70"])


def Render(objects, folder, tag, scene_objects):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.color_type = "TEXTURE"
    sc.display.shading.light = "FLAT"
    sc.display.shading.show_backface_culling = True
    sc.view_settings.view_transform = "Standard"
    sc.render.resolution_x, sc.render.resolution_y = 640, 480
    for o in scene_objects:
        o.hide_render = o not in objects
    cam = bpy.data.cameras.new("view")
    cam.lens = 12
    cam.clip_start = 0.005
    co = bpy.data.objects.new("view", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    eye = Vector(EYE)
    shots = []
    views = [("ahead", eye, (0, 0.4, 0.25)), ("left", eye, (-0.5, -0.05, 0.3)), ("right", eye, (0.5, -0.05, 0.3)), ("up", eye, (0, 0.05, 0.7)),
             ("outside", Vector((1.1, -1.3, 0.95)), (0, 0, 0.25)), ("rear", Vector((-0.9, -1.4, 0.7)), (0, 0, 0.25))]
    for name, at, target in views:
        cam.lens = 12 if at is eye else 35
        co.location = at
        co.rotation_euler = (Vector(target) - at).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(folder, f"{tag}-{name}.png")
        bpy.ops.render.render(write_still=True)
        image = bpy.data.images.load(sc.render.filepath)
        px = np.empty(640 * 480 * 4, np.float32)
        image.pixels.foreach_get(px)
        bpy.data.images.remove(image)
        shots.append(px.reshape(480, 640, 4))
    bpy.data.objects.remove(co)
    bpy.data.cameras.remove(cam)
    return shots


def Sheet(rows, path):
    """Rows of shots, the first on top."""
    grid = np.concatenate([np.concatenate(r, axis=1) for r in reversed(rows)], axis=0)
    h, w = grid.shape[:2]
    image = bpy.data.images.new("sheet", w, h, alpha=True)
    image.pixels.foreach_set(grid.astype(np.float32).ravel())
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    bpy.data.images.remove(image)


def Export(objects, path):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_format="GLB", export_image_format="AUTO", export_apply=True)


def Run(folder):
    out = os.path.join(folder, "assembled")
    os.makedirs(os.path.join(out, "previews"), exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    template, tbm = Import(os.path.join(folder, TEMPLATE))
    pane_src, pbm = Import(os.path.join(folder, PANE))
    console_src, cbm = Import(os.path.join(folder, CONSOLE))
    for o in (template, pane_src, console_src):
        o.hide_render = o is not template

    turn = Matrix.Rotation(math.radians(90), 4, "Z")
    co = np.array([(turn @ v.co)[:] for v in pbm.verts])
    centre = Vector(((co.min(0) + co.max(0)) / 2).tolist())
    left = Matrix.Translation(Vector(PANE_CENTRE)) @ Matrix.Scale(PANE_SCALE, 4) @ Matrix.Translation(-centre) @ turn
    floor = min((left @ v.co).z for v in pbm.verts)
    left = Matrix.Translation((0, 0, -floor)) @ left
    right = Matrix.Diagonal((-1, 1, 1, 1)) @ left
    tlo = np.array([min(v.co[i] for v in tbm.verts) for i in range(3)])
    thi = np.array([max(v.co[i] for v in tbm.verts) for i in range(3)])
    cco = np.array([v.co[:] for v in cbm.verts]) * CONSOLE_SCALE
    clo, chi = cco.min(0), cco.max(0)
    console_m = Matrix.Translation((-(clo[0] + chi[0]) / 2, thi[1] - chi[1], -clo[2])) @ Matrix.Scale(CONSOLE_SCALE, 4)

    pieces = {"side-left": Placed("side-left", pane_src, pbm, left), "side-right": Placed("side-right", pane_src, pbm, right, mirrored=True),
              "console": Placed("console", console_src, cbm, console_m)}
    fillers, dims = Fillers(pieces["side-right"], pieces["console"])
    pieces["fillers"] = fillers

    # how the pane fits the template's interior at its pinned place, and how much of that interior the hull covers
    ttree = BVHTree.FromBMesh(tbm)
    inner = Interior(tbm, ttree)
    itree = BVHTree.FromPolygons([v.co[:] for v in tbm.verts], [[v.index for v in f.verts] for f in inner])
    pane_world = np.array([v.co[:] for v in pieces["side-left"].data.vertices])
    pane_normals = np.array([v.normal[:] for v in pieces["side-left"].data.vertices])
    facing = pane_world[pane_normals[:, 0] > 0.3]
    d = np.array([itree.find_nearest(p)[3] for p in facing.tolist()])
    fit = {"roomFacingVertices": len(facing), "p50": float(np.percentile(d, 50)), "p70": float(np.percentile(d, 70)), "within1cm": float((d < 0.01).mean())}
    parts_tree = Tree([pieces[k] for k in ("side-left", "side-right", "console")])
    hull_tree = Tree(list(pieces.values()))
    coverage = {}
    for label, tree in (("parts", parts_tree), ("hull", hull_tree)):
        area = {0.02: 0.0, 0.06: 0.0}
        total = 0.0
        for f in inner:
            a = f.calc_area()
            total += a
            dist = tree.find_nearest(f.calc_center_median())[3]
            for k in area:
                area[k] += a if dist <= k else 0.0
        coverage[label] = {f"within{k:g}": round(v / total, 3) for k, v in area.items()}
    box = ((-dims["wallX"], dims["rearY"], 0.0), (dims["wallX"], dims["frontY"] + SLAB, dims["ceilingZ"] + SLAB))
    escapes = {"template": Escapes(ttree, EYE, tlo, thi), "parts": Escapes(parts_tree, EYE, *box), "hull": Escapes(hull_tree, EYE, *box)}

    for name, o in pieces.items():
        Export([o], os.path.join(out, name + ".glb"))
    Export(list(pieces.values()), os.path.join(out, "hull.glb"))
    every = [template] + list(pieces.values())
    with tempfile.TemporaryDirectory() as scratch:
        before = Render([template], scratch, "template", every)
        parts_only = Render([pieces[k] for k in ("side-left", "side-right", "console")], scratch, "parts", every)
        after = Render(list(pieces.values()), scratch, "hull", every)
    Sheet([before[:4], parts_only[:4], after[:4]], os.path.join(out, "previews", "seat.png"))
    Sheet([before[4:] + after[4:]], os.path.join(out, "previews", "outside.png"))

    manifest = {
        "template": TEMPLATE, "templateBounds": [tlo.tolist(), thi.tolist()],
        "placements": {"side-left": {"file": PANE, "matrix": [list(r) for r in left]},
                       "side-right": {"file": PANE, "matrix": [list(r) for r in right], "mirrored": True},
                       "console": {"file": CONSOLE, "matrix": [list(r) for r in console_m]}},
        "paneFitToTemplateInterior": fit, "fillerDimensions": dims,
        "triangles": {k: sum(len(p.vertices) - 2 for p in o.data.polygons) for k, o in pieces.items()},
        "templateInteriorCovered": coverage, "raysLeavingFromSeat": {"eye": EYE, **escapes},
    }
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    print("RESULT", json.dumps({k: manifest[k] for k in ("paneFitToTemplateInterior", "fillerDimensions", "triangles", "templateInteriorCovered", "raysLeavingFromSeat")}))


def Candidates(folder):
    """--refit: the left pane's candidate places (the console's place is a rule: centred, on the floor, at the front)."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    _, tbm = Import(os.path.join(folder, TEMPLATE))
    _, pbm = Import(os.path.join(folder, PANE))
    inner = Interior(tbm, BVHTree.FromBMesh(tbm))
    itree = BVHTree.FromPolygons([v.co[:] for v in tbm.verts], [[v.index for v in f.verts] for f in inner])
    tlo = np.array([min(v.co[i] for v in tbm.verts) for i in range(3)])
    thi = np.array([max(v.co[i] for v in tbm.verts) for i in range(3)])
    for r in Refit(itree, tlo, thi, pbm, Matrix.Rotation(math.radians(90), 4, "Z"), (1, 0, 0), (0.4, 0.65))[:5]:
        print("CANDIDATE side-left", json.dumps(r))


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:]
    if "--refit" in args:
        Candidates(os.path.abspath(next(a for a in args if not a.startswith("--"))))
    else:
        Run(os.path.abspath(args[0]))
