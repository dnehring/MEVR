# SPDX-License-Identifier: MIT
"""Builds the Mako cockpit's hull: a clean shell modelled to the template's shape, and the Tripo parts placed in it
(research/48).

    blender -b --factory-startup --python tools/cockpit/hull_assemble.py -- "<concept/assets/hull reconstruction>" [--refit]

--refit only prints candidate places for the side pane (Refit), to choose PANE_SCALE and PANE_CENTRE from.

The template (`hull full -bad.glb`: the whole cockpit as one Tripo mesh) is close to the intended shape but messy:
holes for windows, stray struts, lumps. It is never shipped. The shell follows its measured envelope (the constants
below, from rays cast inside it): a hexagonal floor, side walls with a chamfer into the ceiling, a lowered channel and
longitudinal ribs on the ceiling, front chamfers each with an angled window, and a windshield leaning back between
them. Windows are flat dark panes in light frames, as the parts model them. Every edge is shared, so the shell is closed
except at the open rear. It is tiled with rectangles of the side pane's own atlas (a bolted plate, a light frame, a
dark strip, its window's glass), so it shares that texture.

The side pane (`side pane.glb`) is the left wall turned a quarter turn, scaled and placed as it fitted the template's
interior (the faces seen from inside it), stood on the floor; the right wall is its mirror. The console
(`front computer console.glb`) is centred and placed as it fitted the template's console region. Every Tripo model is
about one unit across, so each has its own scale.

Writes <folder>/assembled/: each piece as a .glb in the hull's coordinates (transforms applied), hull.glb with all of
them, manifest.json (placements, triangles, the shell's distance from the template, how much of the template's interior
is covered, which rays from the seat leave the hull) and previews/ (the seat's view of the template and of the hull,
the hull from outside, and the hull beside the concept image from its viewpoint).
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
from mathutils.geometry import intersect_ray_tri

TEMPLATE = "hull full -bad.glb"
PANE = "side pane.glb"
CONSOLE = "front computer console.glb"
# The side pane's place, fitted to the template's interior on 2026-11-02 (research/48): a quarter turn about z, this
# scale, its bounding box's centre here; then stood on the floor.
# Then 0.12 forward: the interior fit matched the wall's surfaces but put the pane's A-pillar and octagonal window
# 0.10 to 0.13 behind the template's (sections at x 0.43; a two-way fit on the pillar-and-window band alone: 0.08 to 0.14).
PANE_SCALE = 0.5951
PANE_CENTRE = (-0.3647, -0.1318 + 0.12, 0.2748)
# The console's, fitted to the template's interior faces in front (y > -0.05, z < 0.33), centred (research/48).
CONSOLE_SCALE = 0.7524
CONSOLE_CENTRE = (0.0, 0.2749, 0.1473)
# The template's shell, measured by rays from inside it on 2026-11-02 (research/48): x across, y forward, z up.
W, ZW = 0.463, 0.412       # the side walls' inner face; the height where their chamfer into the ceiling starts
XC, ZC = 0.39, 0.48        # the chamfer's top edge; the ceiling
YR = -0.425                # the open rear
YP, XP = 0.06, 0.39        # the side walls step in to the pillar zone here (the template's; the shell takes the
                           # panes' A-pillar tops instead, PillarEdge)
YC = 0.175                 # the front chamfers start here, at XP
XF, YF = 0.245, 0.374      # and end here, at the windshield
ZL = 0.30                  # the windshield's bottom edge: the console's ledge
YT = 0.267                 # the windshield's top edge, at the ceiling: it leans back like the template's (56 degrees)
CHANNEL = (0.09, 0.408, -0.20, 0.10)   # the ceiling's lowered centre: half width, bottom, from y, to y
RIBS = (0.14, 0.20, 0.26, 0.32)         # the longitudinal ribs' x (both sides), 0.028 wide, 0.028 deep
# Rectangles (u0, v0, u1, v1) of the side pane's 4096 x 4096 atlas.
PLATE = (2075 / 4096, 2952 / 4096, 2392 / 4096, 3460 / 4096)  # a plate with four bolts and a bevelled edge, 317 x 508 px
PLATE_ASPECT = 317 / 508
LIGHT = (0.71875, 0.15625, 0.7265625, 0.1875)
RED = (0.7109375, 0.296875, 0.71875, 0.3359375)
DARK = (0.0, 0.0078125, 0.0234375, 0.4296875)
GLASS = (2525 / 4096, 955 / 4096, 2855 / 4096, 1365 / 4096)  # inside the pane's own window
SLAB = 0.02  # the shell's thickness
EYE = (0.0, -0.22, 0.36)  # where the previews and the enclosure rays look from: the seat, in the hull's units
VIEW = ((0.0, -0.52, 0.25), 13.0)  # the concept image's viewpoint (eye, lens mm), matched by eye


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

    def Face(self, points, uvs, toward=None):
        """`toward` (before mirroring): the face is wound to point that way."""
        points = [Vector(p) for p in points]
        uvs = list(uvs)
        if toward is not None and Newell(points).dot(Vector(toward)) < 0:
            points.reverse()
            uvs.reverse()
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


def Newell(points):
    n = Vector()
    for p, q in zip(points, points[1:] + points[:1]):
        n += Vector(((p.y - q.y) * (p.z + q.z), (p.z - q.z) * (p.x + q.x), (p.x - q.x) * (p.y + q.y)))
    return n


def Project(points, trim):
    """UVs for a planar polygon: its own plane's axes, its bounds fitted to the trim."""
    u = (points[1] - points[0]).normalized()
    v = Newell(points).normalized().cross(u)
    c = [((p - points[0]).dot(u), (p - points[0]).dot(v)) for p in points]
    lo = [min(x[i] for x in c) for i in range(2)]
    hi = [max(x[i] for x in c) for i in range(2)]
    u0, v0, u1, v1 = trim
    return [(u0 + (u1 - u0) * (x - lo[0]) / (hi[0] - lo[0]), v0 + (v1 - v0) * (y - lo[1]) / (hi[1] - lo[1])) for x, y in c]


def PolySlab(b, points, toward, depth, trim, sides=DARK):
    """A slab whose room face is a convex planar polygon facing `toward`, `depth` thick behind it."""
    pts = [Vector(p) for p in points]
    if Newell(pts).dot(Vector(toward)) < 0:
        pts.reverse()
    n = Newell(pts).normalized()
    b.Face(pts, Project(pts, trim))
    back = [p - n * depth for p in reversed(pts)]
    b.Face(back, Project(back, DARK))
    for p, q in zip(pts, pts[1:] + pts[:1]):
        b.Face([p, p - n * depth, q - n * depth, q], Fit(sides))


def Frame(b, points, toward, bar, out, trim=LIGHT, edges=None, depth=0.0):
    """Light bars along a convex polygon's edges (all, or those numbered in `edges`: k runs from point k to k + 1),
    `bar` wide inside it, standing `out` toward the room and reaching `depth` behind the polygon: an open window's
    frame lines the opening through the wall."""
    pts = [Vector(p) for p in points]
    room = Vector(toward).normalized()
    centre = sum(pts, Vector()) / len(pts)
    for k, (p, q) in enumerate(zip(pts, pts[1:] + pts[:1])):
        if edges is not None and k not in edges:
            continue
        e = (q - p).normalized()
        w = room.cross(e).normalized()
        if w.dot(centre - p) < 0:
            w = -w
        n = e.cross(w)
        if n.dot(room) > 0:
            Box(b, (0, 0, -depth), ((q - p).length, bar, out), trim, axes=(p, e, w, n))
        else:  # the same bar in a right-handed frame
            Box(b, (0, -bar, -depth), ((q - p).length, 0, out), trim, axes=(p, e, -w, e.cross(-w)))


def PillarEdge(pane):
    """The right pane's A-pillar (its light and red faces above the ledge, in its front 0.35): the front-most point of
    those faces just above the ledge and at the ceiling, and the pane's rear end."""
    me = pane.data
    image = next(n.image for m in me.materials for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image)
    w, h = image.size
    px = np.empty(w * h * 4, np.float32)
    image.pixels.foreach_get(px)
    px = px.reshape(h, w, 4)
    uv = me.uv_layers.active.data
    lo, hi = Bounds(pane)
    found = []
    for p in me.polygons:
        c = p.center
        if c.z < ZL or c.y < hi[1] - 0.35:
            continue
        u = sum(uv[i].uv.x for i in p.loop_indices) / p.loop_total
        v = sum(uv[i].uv.y for i in p.loop_indices) / p.loop_total
        r, g, bl = px[int((v % 1) * (h - 1)), int((u % 1) * (w - 1)), :3]
        if 0.2126 * r + 0.7152 * g + 0.0722 * bl > 0.42 or (r > 0.3 and r > 2 * g):
            found.append(c[:])
    found = np.array(found)

    def Front(z):
        near = found[np.abs(found[:, 2] - z) < 0.02]
        return Vector(near[np.argmax(near[:, 1])])

    return Front(ZL + 0.03), Front(ZC - 0.01), float(lo[1])


def Visible(tree, origin, d, far=3.0):
    """The first face a ray sees from its front, as the game draws them (back faces culled); None if it sees none."""
    p = Vector(origin)
    for _ in range(64):
        hit = tree.ray_cast(p, d, far)
        if hit[0] is None:
            return None
        if hit[1].dot(d) < 0:
            return hit
        p = hit[0] + d * 1e-5
    return None


def Hull2(points):
    """Convex hull of 2D points, counter-clockwise (monotone chain)."""
    pts = sorted(set(points))

    def Half(seq):
        out = []
        for p in seq:
            while len(out) >= 2 and (out[-1][0] - out[-2][0]) * (p[1] - out[-2][1]) - (out[-1][1] - out[-2][1]) * (p[0] - out[-2][0]) <= 0:
                out.pop()
            out.append(p)
        return out
    lower, upper = Half(pts), Half(reversed(pts))
    return lower[:-1] + upper[:-1]


def PaneOpening(pane):
    """The right pane's window as the game draws it: where a ray from the room along +x sees no face from its front (the
    pane's back skin behind the octagon is culled from inside). The largest such region away from the pane's edges, as
    a convex polygon on the side wall's face (x = W), and how far out the pane's own frame around it reaches."""
    bm = bmesh.new()
    bm.from_mesh(pane.data)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    lo, hi = Bounds(pane)
    step = 0.004
    ys, zs = np.arange(lo[1], hi[1], step), np.arange(0.0, hi[2], step)
    d = Vector((1, 0, 0))
    seen = np.full((len(zs), len(ys)), np.nan)  # x of the first face seen; nan where open
    for i, z in enumerate(zs):
        for j, y in enumerate(ys):
            hit = Visible(tree, (0.2, y, z), d)
            if hit is not None:
                seen[i, j] = hit[0].x
    open_ = np.isnan(seen)
    label = np.zeros(open_.shape, int)
    best = (0, None)
    for i0, j0 in zip(*np.nonzero(open_)):
        if label[i0, j0]:
            continue
        label[i0, j0] = n = label.max() + 1
        stack, cells, edge = [(i0, j0)], [], False
        while stack:
            i, j = stack.pop()
            cells.append((i, j))
            edge |= i in (0, len(zs) - 1) or j in (0, len(ys) - 1)
            for a, b in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
                if 0 <= a < len(zs) and 0 <= b < len(ys) and open_[a, b] and not label[a, b]:
                    label[a, b] = n
                    stack.append((a, b))
        if not edge and len(cells) > best[0]:
            best = (len(cells), cells)
    cells = best[1]
    corners = [(ys[j] + dy, zs[i] + dz) for i, j in cells for dy in (-step / 2, step / 2) for dz in (-step / 2, step / 2)]
    rim = [seen[a, b] for i, j in cells for a, b in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1))
           if 0 <= a < len(zs) and 0 <= b < len(ys) and not open_[a, b]]
    return [Vector((W, y, z)) for y, z in Hull2(corners)], float(np.percentile(rim, 10)), len(cells) * step * step


def Prism(polygon, x0, x1):
    """A closed prism over a polygon in (y, z), from x0 to x1, faces outward."""
    b = Builder()
    a = [Vector((x0, p.y, p.z)) for p in polygon]
    c = [Vector((x1, p.y, p.z)) for p in polygon]
    centre = (sum(a, Vector()) + sum(c, Vector())) / (2 * len(a))
    faces = [a, c] + [[a[k], a[(k + 1) % len(a)], c[(k + 1) % len(a)], c[k]] for k in range(len(a))]
    for f in faces:
        b.Face(f, [(0, 0)] * len(f), toward=sum(f, Vector()) / len(f) - centre)
    bmesh.ops.remove_doubles(b.bm, verts=b.bm.verts, dist=1e-7)
    return b.bm


def Cut(o, prisms):
    """Subtracts closed prisms from the object (an exact boolean), applied."""
    bm = bmesh.new()
    for p in prisms:
        me = bpy.data.meshes.new("cut")
        p.to_mesh(me)
        p.free()
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)
    me = bpy.data.meshes.new("cutter")
    bm.to_mesh(me)
    bm.free()
    cutter = bpy.data.objects.new("cutter", me)
    bpy.context.scene.collection.objects.link(cutter)
    mod = o.modifiers.new("cut", "BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.solver = "EXACT"
    mod.object = cutter
    new = bpy.data.meshes.new_from_object(o.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    o.modifiers.remove(mod)
    old = o.data
    o.data = new
    bpy.data.meshes.remove(old)
    bpy.data.objects.remove(cutter)
    bpy.data.meshes.remove(me)


def Reveal(b, polygon, x0, x1):
    """The light lining of a window through the wall: the polygon's edges carried from x0 out to x1, facing its axis."""
    centre = sum(polygon, Vector()) / len(polygon)
    for p, q in zip(polygon, polygon[1:] + polygon[:1]):
        quad = [Vector((x0, p.y, p.z)), Vector((x0, q.y, q.z)), Vector((x1, q.y, q.z)), Vector((x1, p.y, p.z))]
        mid = (p + q) / 2
        b.Face(quad, Fit(LIGHT), toward=Vector((0, centre.y - mid.y, centre.z - mid.z)))


def Shell(material_source, pillar, opening):
    """The template's shell, clean: from the measured constants above and the panes' A-pillars, symmetric about x = 0.
    Each A-pillar carries the roof (the side's ceiling chamfer ends at its top) and frames the front side window, which
    runs from it to the windshield's frame between the ledge and the roof. The side walls behind the panes are cut
    where the panes' windows are (`opening`: the right one and how far out its frame reaches) and lined in light.
    Only those walls go through the boolean: the shell's slabs overlap, and the exact solver drops faces it finds
    inside another slab."""
    b = Builder()
    walls = Builder()
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    XT = XP - (YT - YC) * (XP - XF) / (YF - YC)  # the windshield's top corners lie on the front chamfers
    low, high, pane_rear = pillar
    # The roof meets the pillar's top here, just behind its front face: the side's ceiling chamfer, the side wall and the
    # step to the pillar zone end at it, and the side window's rear edge runs from it down to the chamfer's start at
    # the ledge, behind the pillar, which overlaps it (its foot is forward of that start: `low`).
    YP_ = high.y - 0.015
    pt = Vector((XC, YP_, ZC))
    # floor, facing up: the rear part, the pillar zone, under the front chamfers
    Slab(b, Vector((-W, YR, 0)), X * 2 * W, Y * (YP_ - YR), SLAB)
    Slab(b, Vector((-XP, YP_, 0)), X * 2 * XP, Y * (YC - YP_), SLAB)
    PolySlab(b, [(-XP, YC, 0), (XP, YC, 0), (XF, YF, 0), (-XF, YF, 0)], Z, SLAB, DARK)
    # ceiling, facing down: the rear part, then forward of the pillars' tops along the side windows' tops
    Slab(b, Vector((-XC, YR, ZC)), Y * (YP_ - YR), X * 2 * XC, SLAB)
    PolySlab(b, [(-XC, YP_, ZC), (XC, YP_, ZC), (XT, YT, ZC), (-XT, YT, ZC)], -Z, SLAB, DARK)
    # the wall under the windshield, behind the console, facing back
    Slab(b, Vector((-XF, YF, 0)), X * 2 * XF, Z * ZL, SLAB)
    # the windshield: an opening leaning back between the chamfers, lined by a light frame through the shell
    shield = [(-XF, YF, ZL), (XF, YF, ZL), (XT, YT, ZC), (-XT, YT, ZC)]
    room = Vector((0, -(ZC - ZL), -(YF - YT))).normalized()
    Frame(b, [Vector(p) for p in shield], room, 0.018, 0.012, depth=SLAB + 0.005)
    windows = [[Vector(p) for p in shield]]
    # the ceiling's lowered centre (plates underneath) and the ribs either side of it, dark as the concept's
    half, bottom, y0, y1 = CHANNEL
    Slab(b, Vector((-half, y0, bottom)), Y * (y1 - y0), X * 2 * half, ZC - bottom, size=0.16, sides=DARK)
    for x in RIBS:
        for s in (1, -1):
            Box(b, (s * x - 0.014, YR + 0.05, ZC - 0.028), (s * x + 0.014, YC - 0.01, ZC), DARK)
    # the roof's frame: a beam under the ceiling at the open back, and one between the A-pillars' tops
    Box(b, (-XC, YR, ZC - 0.045), (XC, YR + 0.04, ZC), LIGHT)
    Box(b, (-XC, YP_ - 0.02, ZC - 0.04), (XC, YP_ + 0.02, ZC), LIGHT)
    for mirror in (False, True):
        b.mirror = walls.mirror = mirror
        Slab(walls, Vector((W, YR, 0)), Z * ZW, Y * (YP_ - YR), SLAB)                         # side wall, facing in
        Slab(walls, Vector((W, YR, ZW)), Vector((XC - W, 0, ZC - ZW)), Y * (YP_ - YR), SLAB)  # its chamfer into the ceiling
        PolySlab(b, [(XP, YP_, 0), (W, YP_, 0), (W, YP_, ZW), (XC, YP_, ZC)], -Y, SLAB, DARK)  # the step, behind the pillar
        PolySlab(b, [(XP, YP_, 0), (XP, YC, 0), (XP, YC, ZL), (XP, YP_, ZC)], -X, SLAB, DARK)  # the pillar zone, behind it
        # the rear portal where the panes (moved forward) leave the wall bare: a light post and its band on the chamfer
        post = min(0.07, pane_rear - YR)
        Box(b, (W - 0.05, YR, 0), (W, YR + post, ZW), LIGHT)
        e = Vector((XC - W, 0, ZC - ZW))
        Box(b, (0, YR, 0), (e.length, YR + post, 0.05), LIGHT, axes=(Vector((W, 0, ZW)), e.normalized(), Y, e.normalized().cross(Y)))
        # the front chamfer below the ledge
        p0, p1 = Vector((XP, YC, 0)), Vector((XF, YF, 0))
        Slab(b, p0, Z * ZL, p1 - p0, SLAB)
        # the front side window: an opening from the A-pillar to the windshield's frame, the ledge to the roof, lined
        # by a light frame: the sill, the roof's beam, the pillar's edge (the windshield's frame lines its front)
        corners = [p0 + Z * ZL, p1 + Z * ZL, Vector((XT, YT, ZC)), pt]
        room = -Newell(corners).normalized() if Newell(corners).x > 0 else Newell(corners).normalized()
        Frame(b, corners, room, 0.018, 0.012, edges=(0, 2, 3), depth=SLAB + 0.005)
        windows.append([Vector((-c.x, c.y, c.z)) if mirror else c for c in corners])
    b.mirror = walls.mirror = False
    # the side walls, cut for the panes' windows, and the light lining that carries each pane's frame out through them
    right, rim = opening
    left = [Vector((-p.x, p.y, p.z)) for p in right]
    bmesh.ops.remove_doubles(walls.bm, verts=walls.bm.verts, dist=1e-6)
    me = bpy.data.meshes.new("walls")
    walls.bm.to_mesh(me)
    walls.bm.free()
    wall = bpy.data.objects.new("walls", me)
    bpy.context.scene.collection.objects.link(wall)
    Cut(wall, [Prism(right, rim - 0.01, W + SLAB + 0.03), Prism(left, -(W + SLAB + 0.03), -(rim - 0.01))])
    b.bm.from_mesh(wall.data)
    bpy.data.meshes.remove(wall.data)
    for mirror in (False, True):
        b.mirror = mirror
        Reveal(b, right, rim, W + SLAB + 0.01)
    b.mirror = False
    windows += [right, left]
    bmesh.ops.remove_doubles(b.bm, verts=b.bm.verts, dist=1e-6)
    me = bpy.data.meshes.new("shell")
    b.bm.to_mesh(me)
    b.bm.free()
    for m in material_source.data.materials:
        me.materials.append(m)
    o = bpy.data.objects.new("shell", me)
    bpy.context.scene.collection.objects.link(o)
    o.data.shade_flat()
    return o, windows, {"XT": XT, "pillarTopY": YP_, "pillarFoot": [round(c, 4) for c in low], "pillarTop": [round(c, 4) for c in high]}


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


def Escapes(tree, eye, lo, hi, windows=()):
    """The share of rays from the eye that see nothing (back faces culled, as the game draws), by where they leave:
    through a window (polygons), out of the box (lo, hi) by its rear (the cockpit's open back), or by another face of
    the box (a leak)."""
    dirs = Fibonacci(4000)
    names = (("left", "right"), ("rear", "front"), ("floor", "ceiling"))
    tris = [(w[0], w[k], w[k + 1]) for w in windows for k in range(1, len(w) - 1)]
    out, where = {}, []
    for d in dirs.tolist():
        if Visible(tree, eye, Vector(d)) is not None:
            continue
        if any(intersect_ray_tri(a, b, c, Vector(d), Vector(eye), True) for a, b, c in tris):
            key = "windows"
        else:
            exits = [((hi[a] if d[a] > 0 else lo[a]) - eye[a]) / d[a] if abs(d[a]) > 1e-9 else math.inf for a in range(3)]
            a = int(np.argmin(exits))
            face = names[a][d[a] > 0]
            key = "rear" if face == "rear" else "leak " + face
            if face != "rear":
                where.append([round(eye[i] + d[i] * exits[a], 3) for i in range(3)])
        out[key] = out.get(key, 0) + 1
    result = {k: round(v / len(dirs), 4) for k, v in sorted(out.items())}
    if where:
        result["leaksLeaveAt"] = where[:12]
    return result


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


def Outside():
    """For the previews only: a sky colour and a ground below the floor, so the open windows read as windows."""
    sc = bpy.context.scene
    if sc.world is None:
        sc.world = bpy.data.worlds.new("sky")
    sc.world.color = (0.52, 0.64, 0.78)
    me = bpy.data.meshes.new("ground")
    s = 30.0
    me.from_pydata([(-s, -s, -0.08), (s, -s, -0.08), (s, s, -0.08), (-s, s, -0.08)], [], [(0, 1, 2, 3)])
    material = bpy.data.materials.new("ground")
    material.diffuse_color = (0.42, 0.36, 0.28, 1.0)
    me.materials.append(material)
    sc.collection.objects.link(bpy.data.objects.new("ground", me))


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
    cco = np.array([v.co[:] for v in cbm.verts])
    ccentre = Vector(((cco.min(0) + cco.max(0)) / 2).tolist())
    console_m = Matrix.Translation(Vector(CONSOLE_CENTRE)) @ Matrix.Scale(CONSOLE_SCALE, 4) @ Matrix.Translation(-ccentre)
    floor = min((console_m @ v.co).z for v in cbm.verts)
    console_m = Matrix.Translation((0, 0, -floor)) @ console_m

    pieces = {"side-left": Placed("side-left", pane_src, pbm, left), "side-right": Placed("side-right", pane_src, pbm, right, mirrored=True),
              "console": Placed("console", console_src, cbm, console_m)}
    # the panes' windows are open (their back skin is culled from inside): where, to cut the shell's walls behind them
    right, rim, area = PaneOpening(pieces["side-right"])
    shell, windows, dims = Shell(pieces["side-right"], PillarEdge(pieces["side-right"]), (right, rim))
    dims["paneWindow"] = {"area": round(area, 4), "frameReachesX": round(rim, 4), "corners": len(right),
                          "y": [round(min(p.y for p in right), 3), round(max(p.y for p in right), 3)],
                          "z": [round(min(p.z for p in right), 3), round(max(p.z for p in right), 3)]}
    pieces["shell"] = shell

    # how the pane fits the template's interior at its pinned place, and how much of that interior the hull covers
    ttree = BVHTree.FromBMesh(tbm)
    inner = Interior(tbm, ttree)
    itree = BVHTree.FromPolygons([v.co[:] for v in tbm.verts], [[v.index for v in f.verts] for f in inner])
    # how far the shell's inside is from the template's: its faces seen from inside it, weighted by area
    sbm = bmesh.new()
    sbm.from_mesh(shell.data)
    sbm.faces.index_update()
    seen = Interior(sbm, BVHTree.FromBMesh(sbm))
    dist = np.array([itree.find_nearest(f.calc_center_median())[3] for f in seen])
    area = np.array([f.calc_area() for f in seen])
    order = np.argsort(dist)
    cum = np.cumsum(area[order]) / area.sum()
    shell_fit = {"faces": len(seen), "areaWithin2cm": round(float(area[dist <= 0.02].sum() / area.sum()), 3),
                 "areaWithin5cm": round(float(area[dist <= 0.05].sum() / area.sum()), 3),
                 **{f"p{q}": round(float(dist[order][np.searchsorted(cum, q / 100)]), 4) for q in (50, 90)}}
    sbm.free()
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
    box = ((-W - 0.03, YR, -SLAB), (W + 0.03, 0.6, ZC + SLAB))
    escapes = {"template": Escapes(ttree, EYE, tlo, thi), "parts": Escapes(parts_tree, EYE, *box),
               "hull": Escapes(hull_tree, EYE, *box, windows=windows)}

    for name, o in pieces.items():
        Export([o], os.path.join(out, name + ".glb"))
    Export(list(pieces.values()), os.path.join(out, "hull.glb"))
    every = [template] + list(pieces.values())
    Outside()
    with tempfile.TemporaryDirectory() as scratch:
        before = Render([template], scratch, "template", every)
        parts_only = Render([pieces[k] for k in ("side-left", "side-right", "console")], scratch, "parts", every)
        after = Render(list(pieces.values()), scratch, "hull", every)
        concept = next((f for f in sorted(os.listdir(folder)) if f.startswith("Enclosed") and f.endswith(".png")), None)
        if concept:
            Concept(os.path.join(folder, concept), [template], list(pieces.values()), every, scratch, os.path.join(out, "previews", "concept.png"))
    Sheet([before[:4], parts_only[:4], after[:4]], os.path.join(out, "previews", "seat.png"))
    Sheet([before[4:] + after[4:]], os.path.join(out, "previews", "outside.png"))

    manifest = {
        "template": TEMPLATE, "templateBounds": [tlo.tolist(), thi.tolist()],
        "placements": {"side-left": {"file": PANE, "matrix": [list(r) for r in left]},
                       "side-right": {"file": PANE, "matrix": [list(r) for r in right], "mirrored": True},
                       "console": {"file": CONSOLE, "matrix": [list(r) for r in console_m]}},
        "shellConstants": {"W": W, "ZW": ZW, "XC": XC, "ZC": ZC, "YR": YR, "YP": YP, "XP": XP, "YC": YC, "XF": XF, "YF": YF, "ZL": ZL, "YT": YT, **dims},
        "paneFitToTemplateInterior": fit, "shellFromTemplateInterior": shell_fit,
        "triangles": {k: sum(len(p.vertices) - 2 for p in o.data.polygons) for k, o in pieces.items()},
        "templateInteriorCovered": coverage, "raysLeavingFromSeat": {"eye": EYE, **escapes},
    }
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    print("RESULT", json.dumps({k: manifest[k] for k in ("paneFitToTemplateInterior", "shellFromTemplateInterior", "triangles", "templateInteriorCovered", "raysLeavingFromSeat")}))


def Concept(image_path, template_objects, hull_objects, every, scratch, path):
    """The concept image beside the hull and the template, from the image's viewpoint (VIEW), lit flat."""
    w, h = 835, 470
    sc = bpy.context.scene
    sc.render.resolution_x, sc.render.resolution_y = w, h
    cam = bpy.data.cameras.new("concept")
    cam.lens = VIEW[1]
    cam.clip_start = 0.002
    co = bpy.data.objects.new("concept", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    eye = Vector(VIEW[0])
    co.location = eye
    co.rotation_euler = (Vector((0, 1, -0.02))).to_track_quat("-Z", "Y").to_euler()
    shots = []
    for tag, objects in (("hull", hull_objects), ("template", template_objects)):
        for o in every:
            o.hide_render = o not in objects
        sc.render.filepath = os.path.join(scratch, f"concept-{tag}.png")
        bpy.ops.render.render(write_still=True)
        image = bpy.data.images.load(sc.render.filepath)
        px = np.empty(w * h * 4, np.float32)
        image.pixels.foreach_get(px)
        bpy.data.images.remove(image)
        shots.append(px.reshape(h, w, 4))
    image = bpy.data.images.load(image_path)
    iw, ih = image.size
    px = np.empty(iw * ih * 4, np.float32)
    image.pixels.foreach_get(px)
    bpy.data.images.remove(image)
    px = px.reshape(ih, iw, 4)
    ref = px[(np.arange(h) * ih / h).astype(int)][:, (np.arange(w) * iw / w).astype(int)]
    Sheet([[ref, shots[0]], [shots[1], np.zeros_like(ref)]], path)
    bpy.data.objects.remove(co)
    bpy.data.cameras.remove(cam)
    sc.render.resolution_x, sc.render.resolution_y = 640, 480


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
