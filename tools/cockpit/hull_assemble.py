# SPDX-License-Identifier: MIT
"""Builds the Mako cockpit's hull: a clean shell modelled to the template's shape, and the Tripo parts placed in it
(research/48).

    blender -b --factory-startup --python tools/cockpit/hull_assemble.py -- "<concept/assets/hull reconstruction>" [--refit]

--refit only prints candidate places for the side pane (Refit), to choose PANE_SCALE and PANE_CENTRE from.

The template (`hull full -bad.glb`: the whole cockpit as one Tripo mesh) is close to the intended shape but messy:
holes for windows, stray struts, lumps. It is never shipped. The shell follows its measured envelope (the constants
below, from rays cast inside it) and the panes' own frame: a floor of chamfered plates, the roof on the panes' caps
with a central spine and slats, a frame line at the panes' top rail that runs round the front (the A-pillars' tops,
the side windows' tops, the windshield's header) with the panes' cap profile above it, open windows in chamfered light
frames with mitred corners, a portal arch at the open rear. Its faces are classed (plate, dark, light, red, seam) with
UVs into the side pane's atlas, and hull_shapes.Bake gives it a texture of its own from them, with occlusion and a soft
light baked in (the game draws it unlit).

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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hull_shapes import QUAD, Bake, Builder, Chamfered, Clip, Corners, Newell, Plate, Slab, Sweep, Visible  # noqa: E402

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
YT = 0.267                 # the windshield's lean: the template's top edge is here at ZC (56 degrees from vertical)
# The panes' own frame, measured in sections of the placed right pane on 2026-11-02 (research/48 section 8). The
# template's ceiling (ZC) sits below the panes' top rail and hid it; the roof now rests on the panes' caps.
ZH = 0.497      # the frame line: the panes' top rail's lower edge, the A-pillars' tops, every window's top edge
ZR = 0.553      # the ceiling: the panes' caps' inner lip
XRAIL = 0.399   # the rail's room edge
XWALL = 0.413   # the panes' wall face under the rail
# The cap above the rail in a section at y -0.15: (inward from the rail's room edge, up from ZH), from the wall's top
# to the ceiling, and each edge's class. The shell carries it where the panes end: back to the open rear, and round
# the front along the windows' tops.
CAP = [(XRAIL - XWALL, 0.0), (0.0, 0.0), (0.005, 0.012), (0.025, 0.014), (0.096, 0.036), (0.112, 0.043), (0.113, ZR - ZH)]
CAP_CLASSES = ["DARK", "LIGHT", "LIGHT", "DARK", "DARK", "DARK"]
# The shell's copies run 0.5 mm below it (all but the edge on the ceiling) and 15 mm into the panes' ends, so the joints
# neither crack nor flicker: the panes' caps end 2 to 8 mm apart across their width, and game_ready.py moves their
# surfaces by up to 1.7 mm. The ceiling reaches as far over the caps.
CAP_SWEPT = [(s, u - 0.0005) for s, u in CAP[:-1]] + CAP[-1:]
OVERLAP = 0.015
# An open window's frame: (into the opening, toward the room) from the opening's edge on the wall's room face. A light
# face standing 12 mm proud, chamfered, stepping down to a dark gasket that lines the opening through the wall.
WINDOW = [(-0.004, -0.032), (0.024, -0.032), (0.024, -0.008), (0.018, -0.002), (0.018, 0.004), (0.012, 0.012),
          (0.0, 0.012), (-0.004, 0.008), (-0.004, -0.032)]
WINDOW_CLASSES = ["DARK", "SEAM", "SEAM", "LIGHT", "LIGHT", "LIGHT", "LIGHT", "DARK"]
# The ceiling's spine and slats, as profiles (side, down from ZR negative): the template's lowered centre and ribs,
# raised with the roof. The spine's upper tier, and its lower tier with a recess underneath; each slat a louvre whose
# face slopes toward the cockpit's middle, a light edge along its foot (x, depth for each).
SPINE_UPPER = Chamfered(-0.12, 0.12, -0.028, 0.004, 0.006)
SPINE = [(0.085, -0.028), (-0.085, -0.028), (-0.085, -0.076), (-0.073, -0.088), (-0.05, -0.088), (-0.045, -0.082),
         (0.045, -0.082), (0.05, -0.088), (0.073, -0.088), (0.085, -0.076), (0.085, -0.028)]
SPINE_CLASSES = ["DARK", "PLATE", "LIGHT", "DARK", "SEAM", "SEAM", "SEAM", "DARK", "LIGHT", "PLATE"]
SLATS = ((0.14, 0.030), (0.175, 0.026), (0.21, 0.022), (0.245, 0.018))
SLAT_CLASSES = ["DARK", "DARK", "LIGHT", "DARK", "DARK"]


def Louvre(depth):
    """A slat's profile (side toward the middle, up): a vertical outer face, a light foot, a face sloping up inward."""
    return [(0.016, 0.004), (-0.016, 0.004), (-0.016, -depth), (-0.006, -depth), (0.016, -0.004), (0.016, 0.004)]


# Rectangles (u0, v0, u1, v1) of the side pane's 4096 x 4096 atlas, by surface class, and each class's tint in the bake.
PLATE = (2075 / 4096, 2952 / 4096, 2392 / 4096, 3460 / 4096)  # a plate with four bolts and a bevelled edge, 317 x 508 px
LIGHT = (0.71875, 0.15625, 0.7265625, 0.1875)
RED = (0.7109375, 0.296875, 0.71875, 0.3359375)
DARK = (0.0, 0.0078125, 0.0234375, 0.4296875)
TRIMS = {"PLATE": PLATE, "DARK": DARK, "LIGHT": LIGHT, "RED": RED, "SEAM": DARK}
TINTS = {"PLATE": 1.0, "DARK": 1.0, "LIGHT": 2.6, "RED": 1.0, "SEAM": 0.35}
SLAB = 0.02  # the shell's thickness
SEAM = 0.012  # how deep the floor's seams go: the base under the plates, where the walls start
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


def PillarEdge(pane):
    """The right pane's A-pillar (its light and red faces above the ledge, in its front 0.35): the front-most point of
    those faces just above the ledge and at the ceiling, the pane's rear end, and where its cap over the rail ends in
    front."""
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

    # where the cap over the rail ends in front: its soffit's vertices, inboard of the rail and above the pillar's top
    co = np.array([v.co[:] for v in me.vertices])
    cap = co[(co[:, 2] > ZH + 0.02) & (co[:, 0] < XRAIL - 0.01)]
    return Front(ZL + 0.03), Front(ZC - 0.01), float(lo[1]), float(cap[:, 1].max())


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
    bm = bmesh.new()
    a = [Vector((x0, p.y, p.z)) for p in polygon]
    c = [Vector((x1, p.y, p.z)) for p in polygon]
    centre = (sum(a, Vector()) + sum(c, Vector())) / (2 * len(a))
    for f in [a, c] + [[a[k], a[(k + 1) % len(a)], c[(k + 1) % len(a)], c[k]] for k in range(len(a))]:
        if Newell(f).dot(sum(f, Vector()) / len(f) - centre) < 0:
            f = f[::-1]
        bm.faces.new([bm.verts.new(p) for p in f])
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-7)
    return bm


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
        b.Face(quad, "LIGHT", toward=Vector((0, centre.y - mid.y, centre.z - mid.z)), uvs=QUAD)


def Inward(points, up):
    """A loop's points in the order whose sides (up x direction) point into it."""
    pts = [Vector(p) for p in points]
    side = Vector(up).cross(pts[1] - pts[0])
    return pts if side.dot(sum(pts, Vector()) / len(pts) - pts[0]) > 0 else pts[::-1]


def FrontAt(line, x):
    """The front-most y of a polyline (x, y, z points) where it crosses x."""
    ys = [p.y + (q.y - p.y) * (x - p.x) / (q.x - p.x) for p, q in zip(line, line[1:])
          if min(p.x, q.x) <= x <= max(p.x, q.x) and abs(q.x - p.x) > 1e-9]
    return max(ys)


def Area(polygon):
    return 0.5 * abs(sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(polygon, polygon[1:] + polygon[:1])))


def Shell(pillar, opening):
    """The template's shell, clean: from the measured constants above and the panes' own frame, symmetric about x = 0.

    The frame line (ZH, the panes' top rail) runs round the cockpit: the panes' rails, then the shell's own rail with the
    panes' cap profile above it, along both front side windows' tops and the windshield's header, and back from the
    panes to the open rear, where a light portal arch stands. The roof rests on the caps (ZR), so nothing of it comes
    down across a window or the panes' frames: the spine and the slats end where the cap profile meets the ceiling.
    Each A-pillar frames its side window's rear; light frames with mitred corners line the rest of every opening. The
    side walls behind the panes are cut where the panes' windows are (`opening`: the right one and how far out its frame
    reaches) and lined in light. Only those walls go through the boolean: the shell's pieces overlap, and the exact
    solver drops faces it finds inside another piece."""
    b, walls = Builder(TRIMS), Builder(TRIMS)
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    low, high, pane_rear, cap_front = pillar
    YP_ = high.y - 0.015  # the A-pillar's top, just behind its front face: the side window's top rear corner
    lean = (YF - YT) / (ZC - ZL)
    yh = YF - (ZH - ZL) * lean                    # the windshield's top edge, on the frame line
    xh = XP - (yh - YC) * (XP - XF) / (YF - YC)   # its top corners lie on the front chamfers
    # floor: chamfered plates over a dark base, the seams between them 8 mm wide and SEAM deep; the walls start on the base
    rear = [(-W, YR), (W, YR), (W, YP_), (-W, YP_)]
    front = [(-XP, YP_), (XP, YP_), (XP, YC), (XF, YF), (-XF, YF), (-XP, YC)]
    for piece in (rear, front):
        Slab(b, [(x, y, -SEAM) for x, y in piece], Z, SLAB, "SEAM", "DARK")
    columns = (-W, -0.27, -0.12, 0.12, 0.27, W)
    rows = np.arange(YR, YF + 0.2, 0.2)
    for x0, x1 in zip(columns, columns[1:]):
        for y0, y1 in zip(rows, rows[1:]):
            cell = [(x0 + 0.004, y0 + 0.004), (x1 - 0.004, y0 + 0.004), (x1 - 0.004, y1 - 0.004), (x0 + 0.004, y1 - 0.004)]
            for piece in (rear, front):
                part = Clip(cell, piece)
                if len(part) >= 3 and Area(part) > 0.002:
                    Plate(b, Corners(part, 0.02), 0.0, 0.005, SEAM)
    # the wall under the windshield, behind the console, facing back
    Slab(b, [(-XF, YF, -SEAM), (XF, YF, -SEAM), (XF, YF, ZL), (-XF, YF, ZL)], -Y, SLAB, "DARK")
    # the windshield: an opening leaning back between the front chamfers up to the frame line, framed all round
    shield = [Vector((-XF, YF, ZL)), Vector((XF, YF, ZL)), Vector((xh, yh, ZH)), Vector((-xh, yh, ZH))]
    room = Vector((0, -(ZC - ZL), -(YF - YT))).normalized()
    Sweep(b, Inward(shield, room), room, WINDOW, WINDOW_CLASSES, closed=True)
    windows = [shield]
    # the frame line round the front: the rail and the panes' cap profile, from one pane's cap along the side window's
    # top, the windshield's header and back to the other's; the ceiling's front edge is where it ends
    line = [Vector((XRAIL, cap_front - OVERLAP, ZH)), Vector((XRAIL, YP_, ZH)), Vector((xh, yh, ZH)),
            Vector((-xh, yh, ZH)), Vector((-XRAIL, YP_, ZH)), Vector((-XRAIL, cap_front - OVERLAP, ZH))]
    edge = [r[-1] for r in Sweep(b, line, Z, CAP_SWEPT, CAP_CLASSES)]
    # the ceiling: plates from the rear to the caps' front end, then up to that front edge; its back
    xc = edge[0].x
    cols = (-xc - OVERLAP, -0.143, 0.0, 0.143, xc + OVERLAP)  # over the caps' top, as the joints with the panes
    rows = np.linspace(YR, edge[0].y, 4)
    for x0, x1 in zip(cols, cols[1:]):
        for y0, y1 in zip(rows, rows[1:]):
            b.Face([(x0, y0, ZR), (x1, y0, ZR), (x1, y1, ZR), (x0, y1, ZR)], "PLATE", toward=-Z)
    b.Face(edge + [Vector((x, edge[0].y, ZR)) for x in cols[1:-1]], "PLATE", toward=-Z)
    b.Face([Vector((xc, YR, ZR + SLAB))] + [p + Z * SLAB for p in edge] + [Vector((-xc, YR, ZR + SLAB))], "DARK", toward=Z)
    # the spine, two tiers, the lower recessed underneath, and the louvred slats either side: from the portal to the
    # ceiling's front edge, each end sloping up into the ceiling at 45 degrees
    def Beam(x, reach, profile, classes, depth):
        ya, yb = YR + 0.055 + 1.414 * depth, FrontAt(edge, x + reach) - 1.414 * depth
        Sweep(b, [Vector((x, ya - depth, ZR + depth)), Vector((x, ya, ZR)), Vector((x, yb, ZR)), Vector((x, yb + depth, ZR + depth))],
              Z, profile, classes)

    Beam(0.0, 0.12, SPINE_UPPER, "DARK", 0.028)
    Beam(0.0, 0.085, SPINE, SPINE_CLASSES, 0.088)
    for x, depth in SLATS:
        for mirror in (False, True):
            b.mirror = mirror
            Beam(x, 0.016, Louvre(depth), SLAT_CLASSES, depth)
    b.mirror = False
    # the portal arch at the open rear: a light chamfered beam up the walls, along the caps and across under the roof
    portal = [Vector((-XWALL, YR, -SEAM)), Vector((-XWALL, YR, ZH)), Vector((-0.30, YR, ZR)), Vector((0.30, YR, ZR)),
              Vector((XWALL, YR, ZH)), Vector((XWALL, YR, -SEAM))]
    Sweep(b, portal, Y, Chamfered(0.0, 0.05, 0.0, 0.055, 0.01), "LIGHT")
    for mirror in (False, True):
        b.mirror = walls.mirror = mirror
        # behind the panes: the side wall with a panel set in, the rail and cap carried back to the portal
        Slab(b, [(XWALL, YR, -SEAM), (XWALL, pane_rear, -SEAM), (XWALL, pane_rear, ZH), (XWALL, YR, ZH)], -X, SLAB,
             inset=(0.008, 0.006, 0.02))
        Sweep(b, [Vector((XRAIL, YR, ZH)), Vector((XRAIL, pane_rear + OVERLAP, ZH))], Z, CAP_SWEPT, CAP_CLASSES)
        # the side wall behind the pane (cut below for its window), on back behind the rear wall and up behind the cap,
        # which close the corners round the pane's rear end
        Slab(walls, [(W, YR, -SEAM), (W, YP_, -SEAM), (W, YP_, ZR), (W, YR, ZR)], -X, SLAB)
        # behind the A-pillar: the step in to the pillar zone, and the pillar zone's wall up to the window's rear edge
        Slab(b, [(XP, YP_, -SEAM), (W, YP_, -SEAM), (W, YP_, ZH), (XP, YP_, ZH)], -Y, SLAB, "DARK")
        Slab(b, [(XP, YP_, -SEAM), (XP, YC, -SEAM), (XP, YC, ZL), (XP, YP_, ZH)], -X, SLAB, "DARK")
        # the front chamfer below the ledge
        p0, p1 = Vector((XP, YC, 0)), Vector((XF, YF, 0))
        Slab(b, [p0 - Z * SEAM, p1 - Z * SEAM, p1 + Z * ZL, p0 + Z * ZL], Vector((p0.y - p1.y, p1.x - p0.x, 0)), SLAB, "DARK")
        # the front side window, from the A-pillar to the windshield's frame and from the ledge to the frame line. Its
        # frame runs along the sill, up behind the pillar and along the top; the windshield's frame is its front edge
        corners = [p0 + Z * ZL, p1 + Z * ZL, Vector((xh, yh, ZH)), Vector((XRAIL, YP_, ZH))]
        n = Newell(corners).normalized()
        room = n if n.x < 0 else -n
        path = Inward(corners, room)
        cut = next(k for k in range(4) if {tuple(path[k]), tuple(path[k - 1])} == {tuple(corners[1]), tuple(corners[2])})
        Sweep(b, path[cut:] + path[:cut], room, WINDOW, WINDOW_CLASSES)
        windows.append([Vector((-c.x, c.y, c.z)) for c in corners] if mirror else corners)
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
    o = bpy.data.objects.new("shell", me)
    bpy.context.scene.collection.objects.link(o)
    o.data.shade_flat()
    return o, windows, {"windshieldTop": [round(xh, 4), round(yh, 4)], "pillarTopY": YP_, "capFront": round(cap_front, 4),
                        "paneRear": round(pane_rear, 4), "pillarFoot": [round(c, 4) for c in low], "pillarTop": [round(c, 4) for c in high]}


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
    shell, windows, dims = Shell(PillarEdge(pieces["side-right"]), (right, rim))
    # the shell's own texture: its classes' trims with occlusion (the parts' too) and a soft light baked in
    template.hide_render = True
    atlas = next(n.image for m in pieces["side-right"].data.materials for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image)
    inside = [(x, y, z) for x in (-0.22, 0.0, 0.22) for y in (-0.36, -0.2, -0.04, 0.1) for z in (0.12, 0.3, 0.42)]
    bake = Bake(shell, atlas, TRIMS, TINTS, os.path.join(out, "shell.png"), [pieces[k] for k in ("side-left", "side-right", "console")], inside)
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
    box = ((-W - 0.03, YR, -SLAB), (W + 0.03, 0.6, ZR + SLAB + 0.01))
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
        "shellConstants": {"W": W, "ZW": ZW, "XC": XC, "ZC": ZC, "YR": YR, "YP": YP, "XP": XP, "YC": YC, "XF": XF, "YF": YF, "ZL": ZL, "YT": YT,
                           "ZH": ZH, "ZR": ZR, "XRAIL": XRAIL, "XWALL": XWALL, **dims},
        "bake": bake,
        "paneFitToTemplateInterior": fit, "shellFromTemplateInterior": shell_fit,
        "triangles": {k: sum(len(p.vertices) - 2 for p in o.data.polygons) for k, o in pieces.items()},
        "templateInteriorCovered": coverage, "raysLeavingFromSeat": {"eye": EYE, **escapes},
    }
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    print("RESULT", json.dumps({k: manifest[k] for k in ("paneFitToTemplateInterior", "shellFromTemplateInterior", "triangles", "templateInteriorCovered", "raysLeavingFromSeat", "bake")}))


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
