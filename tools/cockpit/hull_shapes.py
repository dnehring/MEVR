# SPDX-License-Identifier: MIT
"""Modelling pieces for the cockpit hull's shell (hull_assemble.py, research/48).

Every face carries a surface class (its material index into CLASSES) and UVs into that class's trim of the side pane's
atlas (UV layer "trim"). The pieces: profiles swept along paths with mitred corners (window frames, the roof's edge,
beams), panels set into a surface, chamfered floor plates; Display() paints an idle screen. Bake() then gives the shell a texture of its own on a fresh
unwrap: the classes' colours, lit by the cockpit's lamps (direct and bounced), with ambient occlusion. The game draws the cockpit unlit
(EmissiveTexturedMaterial, research/47), so without baked shading a bevel would look as flat as the face beside it; the
Tripo parts' textures carry their lighting the same way.
"""
import math
import time

import bmesh
import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

CLASSES = ("PLATE", "DARK", "LIGHT", "RED", "SEAM", "LAMP", "LAMP_ORANGE", "LAMP_BLUE", "METAL")
# The lamps' classes glow in their colour (linear) instead of taking light: warm white panels, orange strips, blue-white
# floor lights, as the lit concept's.
GLOW = {"LAMP": (1.0, 0.8, 0.55), "LAMP_ORANGE": (1.0, 0.45, 0.12), "LAMP_BLUE": (0.62, 0.8, 1.0)}
# The side pane's atlas, which the shell's trims come from and the parts share, leans blue (mean sRGB 64, 64, 75); the
# lit concept's metal is a neutral to warm grey. Albedo is multiplied by this before the light.
BALANCE = (1.0, 0.98, 0.82)
# The chair's painted metal (its Tripo texture): chipped edges, rust specks, uneven paint. The shell and the re-baked
# parts get the same wear in the bake, so the room and the furniture read as one material (the user, 2026-11-02).
WEAR = {"edge": 0.004, "chip": (0.3, 0.29, 0.27), "rust": (0.12, 0.06, 0.03), "vary": 0.25}
QUAD = ((0, 0), (1, 0), (1, 1), (0, 1))


def Newell(points):
    n = Vector()
    for p, q in zip(points, points[1:] + points[:1]):
        n += Vector(((p.y - q.y) * (p.z + q.z), (p.z - q.z) * (p.x + q.x), (p.x - q.x) * (p.y + q.y)))
    return n


def Local(points):
    """UVs from 0 to 1 for a planar polygon: its own plane's axes, fitted to its bounds."""
    u = (points[1] - points[0]).normalized()
    v = Newell(points).normalized().cross(u)
    c = [((p - points[0]).dot(u), (p - points[0]).dot(v)) for p in points]
    lo = [min(x[i] for x in c) for i in range(2)]
    hi = [max(x[i] for x in c) for i in range(2)]
    return [((x - lo[0]) / max(hi[0] - lo[0], 1e-9), (y - lo[1]) / max(hi[1] - lo[1], 1e-9)) for x, y in c]


class Builder:
    """Faces with a class each and UVs into its trim (`trims`: class -> atlas rectangle). With `mirror` set, x is
    negated and the winding reversed, which keeps faces outward."""

    def __init__(self, trims):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("trim")
        self.trims = trims
        self.mirror = False

    def Face(self, points, cls, toward=None, uvs=None):
        """`uvs`: from 0 to 1 across the trim (by default the polygon's own plane); `toward` (before mirroring): the
        face is wound to point that way."""
        points = [Vector(p) for p in points]
        uvs = list(uvs or Local(points))
        if toward is not None and Newell(points).dot(Vector(toward)) < 0:
            points.reverse()
            uvs.reverse()
        if self.mirror:
            points = [Vector((-p.x, p.y, p.z)) for p in reversed(points)]
            uvs = list(reversed(uvs))
        f = self.bm.faces.new([self.bm.verts.new(p) for p in points])
        f.material_index = CLASSES.index(cls)
        u0, v0, u1, v1 = self.trims[cls]
        for loop, (a, c) in zip(f.loops, uvs):
            loop[self.uv].uv = (u0 + (u1 - u0) * a, v0 + (v1 - v0) * c)
        return f


def Chamfered(s0, s1, u0, u1, c):
    """A rectangle (side s0 to s1, up u0 to u1) with its corners cut by c: a closed counter-clockwise profile."""
    pts = [(s1 - c, u0), (s1, u0 + c), (s1, u1 - c), (s1 - c, u1), (s0 + c, u1), (s0, u1 - c), (s0, u0 + c), (s0 + c, u0)]
    return pts + pts[:1]


def Rings(path, up, points, closed=False, ends=(None, None)):
    """Where Sweep puts (side, up) points along a polyline: each segment's frame (direction, side, up) and, at each
    path point, the points' places, mitred where two segments meet. `ends`: for an open path, a plane (point, normal) or
    None for each end; an end's points move along their segment onto it (a member cut to stand flush on a plate)."""
    P = [Vector(p) for p in path]
    n = len(P)
    ups = [Vector(u) for u in up] if isinstance(up, list) else [Vector(up)] * n
    frames = []
    for k in range(n if closed else n - 1):
        t = (P[(k + 1) % n] - P[k]).normalized()
        u = ups[k] + ups[(k + 1) % n]
        u = (u - t * u.dot(t)).normalized()
        frames.append((t, u.cross(t).normalized(), u))
    rings = []
    for i in range(n):
        before = frames[i - 1] if closed or i > 0 else None
        after = frames[i] if closed or i < n - 1 else None
        if before and after:  # the mitre: a point that is `side` from both segments' offset lines
            ms = (before[1] + after[1]) / (1 + before[1].dot(after[1]))
            mu = (before[2] + after[2]) / (1 + before[2].dot(after[2]))
        else:
            ms, mu = (before or after)[1], (before or after)[2]
        ring = [P[i] + ms * s + mu * u for s, u in points]
        plane = None if closed else ends[0] if i == 0 else ends[1] if i == n - 1 else None
        if plane:
            c, m = Vector(plane[0]), Vector(plane[1])
            t = (before or after)[0]
            ring = [q + t * ((c - q).dot(m) / t.dot(m)) for q in ring]
        rings.append(ring)
    return frames, rings


def Sweep(b, path, up, profile, classes, closed=False, ends=(None, None)):
    """A profile swept along a polyline, mitred at its corners.

    `profile`: (side, up) points, where `up` is the given direction (one vector, or a list with one per path point)
    made square to the path, and `side` is up x the path's direction. Each profile edge's faces point to its right in
    those axes, so a counter-clockwise loop (last point = first) is a beam's outside and an open strip faces the side to
    its right. `classes`: one, or one per profile edge, or a list of either with one per path segment. `closed`: the
    path is a loop; else a loop profile's ends are capped, square to the path or on `ends`' planes (Rings). Returns the
    rings: the profile's points at each path point."""
    loop = len(profile) > 2 and tuple(profile[0]) == tuple(profile[-1])
    pts = profile[:-1] if loop else profile
    m = len(pts)
    edges = m if loop else m - 1
    frames, rings = Rings(path, up, pts, closed, ends)
    per_segment = isinstance(classes, list) and isinstance(classes[0], list)

    def Classes(k):
        c = classes[k] if per_segment else classes
        return [c] * edges if isinstance(c, str) else c

    for k, (t, s, u) in enumerate(frames):
        r0, r1 = rings[k], rings[(k + 1) % len(rings)]
        for j in range(edges):
            j1 = (j + 1) % m
            ds, du = pts[j1][0] - pts[j][0], pts[j1][1] - pts[j][1]
            b.Face([r0[j], r1[j], r1[j1], r0[j1]], Classes(k)[j], toward=s * du - u * ds, uvs=QUAD)
    if loop and not closed:
        b.Face(rings[0], Classes(0)[0], toward=-frames[0][0])
        b.Face(rings[-1], Classes(len(frames) - 1)[0], toward=frames[-1][0])
    return rings


def Grille(b, centre, u, v, n, half_u, half_v, slats, proud=0.006):
    """A louvred vent standing on a surface facing n: a dark housing `proud` deep, its well dark, and `slats` bars
    across it (along u) with dark gaps between them."""
    c, u, v, n = Vector(centre), Vector(u), Vector(v), Vector(n)
    rim = min(half_u, half_v) * 0.18
    outer = [c + u * a * half_u + v * bb * half_v + n * proud for a, bb in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    Inset(b, outer, n, rim, proud * 0.7, surface="DARK", ring="DARK", face="SEAM")
    for p, q in zip(outer, outer[1:] + outer[:1]):  # the housing's sides, down to the surface
        b.Face([p, q, q - n * proud, p - n * proud], "DARK", toward=(p + q) / 2 - c - n * proud, uvs=QUAD)
    step = 2 * (half_v - rim) / slats
    for k in range(slats):
        at = c + v * (-half_v + rim + step * (k + 0.5)) + n * (proud * 0.5)
        Sweep(b, [at - u * (half_u - rim), at + u * (half_u - rim)], n,
              Chamfered(-step * 0.33, step * 0.33, -proud * 0.22, proud * 0.22, min(step * 0.1, 0.0008)), "DARK")


def Pipe(b, path, radius, up, cls="DARK", sides=8):
    """A pipe along a polyline: a polygon of `sides` swept with mitred bends, its ends capped."""
    ring = [(radius * math.cos(2 * math.pi * k / sides), radius * math.sin(2 * math.pi * k / sides)) for k in range(sides)]
    Sweep(b, path, up, ring + ring[:1], cls)


def Keypad(b, centre, u, v, n, cols, rows, key, gap, cls="LAMP_ORANGE"):
    """A keypad on a surface facing n: a dark plate 3 mm proud with `cols` x `rows` glowing keys on it."""
    c, u, v, n = Vector(centre), Vector(u), Vector(v), Vector(n)
    hu, hv = (cols * key + (cols + 1) * gap) / 2, (rows * key + (rows + 1) * gap) / 2
    Slab(b, [c + u * a * hu + v * bb * hv + n * 0.003 for a, bb in ((-1, -1), (1, -1), (1, 1), (-1, 1))], n, 0.004, "DARK", "DARK")
    for i in range(cols):
        for j in range(rows):
            k = c + u * (-hu + gap + key / 2 + i * (key + gap)) + v * (-hv + gap + key / 2 + j * (key + gap)) + n * 0.0035
            b.Face([k + u * a * key / 2 + v * bb * key / 2 for a, bb in ((-1, -1), (1, -1), (1, 1), (-1, 1))], cls, toward=n)


def Offset(points, toward, d):
    """A convex planar polygon moved in by d in its plane, mitred."""
    P = [Vector(p) for p in points]
    n = Vector(toward).normalized()
    centre = sum(P, Vector()) / len(P)
    ins = []
    for k in range(len(P)):
        w = n.cross(P[(k + 1) % len(P)] - P[k]).normalized()
        ins.append(w if w.dot(centre - P[k]) > 0 else -w)
    return [P[i] + (ins[i - 1] + ins[i]) / (1 + ins[i - 1].dot(ins[i])) * d for i in range(len(P))]


def Inset(b, points, toward, border, depth, margin=0.0, surface="PLATE", ring="DARK", face="PLATE"):
    """A panel set into a surface: the convex polygon `points` (facing `toward`) keeps a flat `margin` of `surface`,
    then steps in by `border` and down by `depth` (negative: up) to the panel's face; the step is chamfered."""
    n = Vector(toward).normalized()
    outer = [Vector(p) for p in points]
    edge = Offset(outer, n, margin) if margin else outer
    inner = [p - n * depth for p in Offset(edge, n, border)]
    for a, c in ((outer, edge), (edge, inner)) if margin else ((outer, inner),):
        cls = surface if c is edge else ring
        for k in range(len(a)):
            q = (k + 1) % len(a)
            b.Face([a[k], a[q], c[q], c[k]], cls, toward=n, uvs=QUAD)
    b.Face(inner, face, toward=n)


def Slab(b, room, toward, depth, room_cls="PLATE", sides="DARK", inset=None):
    """A slab: its room face the convex polygon `room`, facing `toward`, and `depth` behind it. `inset`: (border,
    depth, margin) sets the room face in as a panel."""
    pts = [Vector(p) for p in room]
    n = Vector(toward).normalized()
    if inset:
        Inset(b, pts, n, inset[0], inset[1], inset[2], surface=room_cls, ring=sides, face=room_cls)
    else:
        b.Face(pts, room_cls, toward=n)
    back = [p - n * depth for p in pts]
    b.Face(back, "DARK", toward=-n)
    centre = sum(pts, Vector()) / len(pts)
    for p, q, p2, q2 in zip(pts, pts[1:] + pts[:1], back, back[1:] + back[:1]):
        b.Face([p, q, q2, p2], sides, toward=(p + q) / 2 - centre, uvs=QUAD)


def Plate(b, outline, z, bevel, drop, top="PLATE", edge="DARK"):
    """A floor plate: the convex outline ((x, y) points) with its top at z, the top's edge bevelled by `bevel`, and its
    sides going down to z - drop."""
    up = Vector((0, 0, 1))
    rim = [Vector((x, y, z - bevel)) for x, y in outline]
    face = [p + up * bevel for p in Offset(rim, up, bevel)]
    b.Face(face, top, toward=up)
    centre = sum(rim, Vector()) / len(rim)
    for k in range(len(rim)):
        q = (k + 1) % len(rim)
        b.Face([rim[k], rim[q], face[q], face[k]], edge, toward=up, uvs=QUAD)
        low = [rim[k] - up * (drop - bevel), rim[q] - up * (drop - bevel)]
        b.Face([rim[k], rim[q], low[1], low[0]], edge, toward=(rim[k] + rim[q]) / 2 - centre, uvs=QUAD)


def Clip(polygon, convex):
    """The 2D polygon clipped to a convex one (both counter-clockwise), Sutherland-Hodgman."""
    out = list(polygon)
    for a, c in zip(convex, convex[1:] + convex[:1]):
        def Inside(p):
            return (c[0] - a[0]) * (p[1] - a[1]) - (c[1] - a[1]) * (p[0] - a[0]) >= -1e-12
        src, out = out, []
        for p, q in zip(src, src[1:] + src[:1]):
            if Inside(q):
                if not Inside(p):
                    out.append(Cross(p, q, a, c))
                out.append(q)
            elif Inside(p):
                out.append(Cross(p, q, a, c))
        if not out:
            break
    return out


def Cross(p, q, a, c):
    """Where the segment p-q crosses the line through a and c."""
    d1 = (q[0] - p[0], q[1] - p[1])
    d2 = (c[0] - a[0], c[1] - a[1])
    den = d1[0] * d2[1] - d1[1] * d2[0]
    t = ((a[0] - p[0]) * d2[1] - (a[1] - p[1]) * d2[0]) / den
    return (p[0] + d1[0] * t, p[1] + d1[1] * t)


def Corners(polygon, c):
    """A convex 2D polygon with each corner cut by c along both its edges (where the edges are long enough)."""
    out = []
    n = len(polygon)
    for i in range(n):
        p, a, q = Vector(polygon[i - 1]), Vector(polygon[i]), Vector(polygon[(i + 1) % n])
        k = min(c, (p - a).length / 3, (q - a).length / 3)
        out += [tuple(a + (p - a).normalized() * k), tuple(a + (q - a).normalized() * k)]
    return out


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


def Seen(o, occluders, points, rays=2000):
    """Indices of o's faces that some ray from `points` sees first (from their front), with the occluders in the way."""
    verts, polys = [], []
    for obj in [o] + list(occluders):
        base = len(verts)
        verts += [(obj.matrix_world @ v.co)[:] for v in obj.data.vertices]
        polys += [[base + i for i in p.vertices] for p in obj.data.polygons]
    tree = BVHTree.FromPolygons(verts, polys)
    count = len(o.data.polygons)
    i = np.arange(rays) + 0.5
    phi, th = np.arccos(1 - 2 * i / rays), np.pi * (1 + 5 ** 0.5) * i
    dirs = np.stack([np.cos(th) * np.sin(phi), np.sin(th) * np.sin(phi), np.cos(phi)], 1).tolist()
    seen = set()
    for p in points:
        for d in dirs:
            hit = Visible(tree, p, Vector(d))
            if hit is not None and hit[2] < count:
                seen.add(hit[2])
    return seen


def Cycles(samples):
    """Cycles on the GPU (OptiX, else CUDA) when there is one. Only that kind's devices are listed: the general scan
    crashed in Intel's ze_loader.dll on the dev machine."""
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = samples
    sc.cycles.device = "CPU"
    prefs = bpy.context.preferences.addons["cycles"].preferences
    for kind in ("OPTIX", "CUDA"):
        try:
            prefs.compute_device_type = kind
            devices = [d for d in prefs.get_devices_for_type(kind) if d.type == kind]
        except Exception:
            continue
        if devices:
            for d in prefs.devices:
                d.use = d.type == kind
            sc.cycles.device = "GPU"
            return kind
    return "CPU"


def Bake(o, atlas, trims, tints, path, occluders, points, size=4096, distance=0.05, samples=512, exposure=1.0, ambient=0.0):
    """Gives `o` a texture of its own, written to `path` (PNG), and returns what it did.

    A fresh unwrap ("bake") of the faces seen from `points` (the rest share one dark texel in a corner); the atlas
    through each class's trim UVs times its tint (`tints`, linear) as albedo, lit by the scene's lights (direct and
    bounced, the occluders casting shadows and bouncing too), times `exposure`, darkened in corners by ambient
    occlusion within `distance`, plus `ambient` times the albedo (in the occlusion) so what no lamp faces still shows;
    the lamps' classes glow in their colour (GLOW). Faces turned to the lamps then read
    apart from faces turned away, which in the unlit game is all a bevel has. Afterwards `o` has one material with that
    image, the unwrap as its only UVs, and every face's material index 0."""
    start = time.time()
    me = o.data
    seen = Seen(o, occluders, points)
    # the bake materials: the atlas through the trim UVs, tinted, into the target image
    target = bpy.data.images.new("bake-target", size, size, alpha=True, float_buffer=True)
    classes = np.empty(len(me.polygons), np.int32)
    me.polygons.foreach_get("material_index", classes)  # Mesh.materials.clear() sets every face's index to 0
    me.materials.clear()
    targets = []
    for cls in CLASSES:
        m = bpy.data.materials.new("bake-" + cls)
        m.use_nodes = True
        nt = m.node_tree
        nt.nodes.clear()
        uv = nt.nodes.new("ShaderNodeUVMap")
        uv.uv_map = "trim"
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = atlas
        tint = nt.nodes.new("ShaderNodeHueSaturation")
        tint.inputs["Value"].default_value = tints.get(cls, 1.0)
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        if cls in GLOW:
            bsdf = nt.nodes.new("ShaderNodeEmission")
            bsdf.inputs["Color"].default_value = GLOW[cls] + (1.0,)
            nt.links.new(bsdf.outputs["Emission"], out.inputs["Surface"])
        else:
            bsdf = nt.nodes.new("ShaderNodeBsdfDiffuse")
            balance = Worn(nt, tex.outputs["Color"])
            nt.links.new(uv.outputs["UV"], tex.inputs["Vector"])
            nt.links.new(balance, tint.inputs["Color"])
            nt.links.new(tint.outputs["Color"], bsdf.inputs["Color"])
            nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
        node = nt.nodes.new("ShaderNodeTexImage")
        node.image = target
        nt.nodes.active = node
        targets.append(node)
        me.materials.append(m)
    me.polygons.foreach_set("material_index", classes)
    # the unwrap: seen faces packed into all but a corner, the rest on one point in it
    layer = me.uv_layers.new(name="bake")
    me.uv_layers.active = layer
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.mode_set(mode="EDIT")
    bm = bmesh.from_edit_mesh(me)
    for f in bm.faces:
        f.select = f.index in seen
    bmesh.update_edit_mesh(me)
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004)
    bpy.ops.object.mode_set(mode="OBJECT")
    data = me.uv_layers["bake"].data
    for p in me.polygons:
        for i in p.loop_indices:
            data[i].uv = data[i].uv * 0.99 if p.index in seen else (0.996, 0.996)
    device = Cycles(1)
    sc = bpy.context.scene
    sc.render.bake.margin = 16
    passes = {}

    def Pass(kind, samples, **kw):
        target.pixels.foreach_set(np.zeros(size * size * 4, np.float32))
        sc.cycles.samples = samples
        bpy.ops.object.bake(type=kind, uv_layer="bake", margin=16, **kw)
        px = np.empty(size * size * 4, np.float32)
        target.pixels.foreach_get(px)
        passes[kind] = px.reshape(size, size, 4)

    Pass("DIFFUSE", 4, pass_filter={"COLOR"})
    if sc.world is None:
        sc.world = bpy.data.worlds.new("bake")
    sc.world.light_settings.distance = distance
    covered = passes["DIFFUSE"][..., 3] > 0.5
    albedo = passes.pop("DIFFUSE")[..., :3]
    Pass("AO", 128)
    ao = passes.pop("AO")[..., 0]
    Pass("EMIT", 1)
    glow = passes.pop("EMIT")[..., :3]
    Pass("DIFFUSE", samples, pass_filter={"DIRECT", "INDIRECT", "COLOR"})
    lit = passes.pop("DIFFUSE")[..., :3]
    linear = (lit * exposure * (0.6 + 0.4 * ao)[..., None] + albedo * (ambient * ao)[..., None] + glow).astype(np.float32)
    linear[~covered] = 0.008
    final = Png(linear, "shell-baked", path)
    bpy.data.images.remove(target)
    # the shell's one material: the baked image on the unwrap
    me.materials.clear()
    m = bpy.data.materials.new("shell")
    m.use_nodes = True
    nt = m.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = final
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    nt.nodes.active = tex
    me.materials.append(m)
    for p in me.polygons:
        p.material_index = 0
    me.uv_layers.remove(me.uv_layers["trim"])
    return {"size": size, "device": device, "samples": samples, "exposure": exposure, "facesSeen": len(seen), "faces": len(me.polygons),
            "texelsCovered": round(float(covered.mean()), 3), "seconds": round(time.time() - start, 1)}


def Png(linear, name, path):
    """A linear RGB array (rows bottom first, as Blender's pixels) saved as an sRGB PNG, and the image."""
    h, w = linear.shape[:2]
    srgb = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(np.clip(linear, 0.0031308, None), 1 / 2.4) - 0.055)
    image = bpy.data.images.new(name, w, h, alpha=False)
    image.pixels.foreach_set(np.concatenate([np.clip(srgb, 0, 1), np.ones((h, w, 1))], -1).astype(np.float32).ravel())
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    return image


def Display(width, height, aspect, bars=(), outline=None, reticle=False):
    """An idle screen in the game's orange interface style, as linear RGB (rows bottom first) for a screen `aspect` times
    as wide as it is tall, whatever the image's own size: a dark amber field warming toward a thin glowing frame with cut
    corners just inside the edge, brackets at an inner frame's corners, and short lit bars on the edges named in `bars`
    ("top", "bottom"). Sizes are fractions of the screen's height, from the image's middle. `outline`: the opening the
    image shows through (a convex polygon, counter-clockwise), which the frames then follow; the image's edge if None.
    `reticle`: an idle gun sight in the middle (a ring, a cross open at its centre, a dot, range ticks either side)."""
    y, x = np.mgrid[0:height, 0:width].astype(np.float32)
    px = ((x + 0.5) / width - 0.5) * aspect
    py = (y + 0.5) / height - 0.5
    ax, ay = np.abs(px), np.abs(py)
    outline = outline or [(-aspect / 2, -0.5), (aspect / 2, -0.5), (aspect / 2, 0.5), (-aspect / 2, 0.5)]
    sides = []
    for (x0, y0), (x1, y1) in zip(outline, outline[1:] + outline[:1]):
        length = math.hypot(x1 - x0, y1 - y0)
        sides.append(((x0, y0), ((y1 - y0) / length, (x0 - x1) / length)))

    def Edge(qx, qy):
        """Signed distance to the outline (negative inside; exact inside a convex polygon)."""
        return np.max([(qx - x0) * nx + (qy - y0) * ny for (x0, y0), (nx, ny) in sides], axis=0)

    def Frame(inset, cut):
        """Signed distance to the outline moved `inset` inward, its corners (where it turns more than 60 degrees) cut by
        `cut` along each side, as a rectangle's would be."""
        d = Edge(px, py)
        for (_, (n0x, n0y)), ((cx, cy), (n1x, n1y)) in zip(sides, sides[1:] + sides[:1]):  # (cx, cy): the corner
            if n0x * n1x + n0y * n1y > 0.5:
                continue
            mx, my = n0x + n1x, n0y + n1y
            m = math.hypot(mx, my)
            d = np.maximum(d, ((px - cx) * mx + (py - cy) * my) / m + (cut + 2 * inset) / math.sqrt(2) - inset)
        return d + inset

    def Line(d, width):
        return np.exp(-(d / width) ** 2)

    orange = np.array([1.0, 0.36, 0.06], np.float32)
    outer = Frame(0.055, 0.13)
    inner = Frame(0.115, 0.07)
    # the brackets: the inner frame near the outline's corners (where it bends more than 30 degrees), about 0.16 along
    # each side from the inner frame's corner
    corners = np.zeros_like(px, dtype=bool)
    for (_, (n0x, n0y)), ((cx, cy), (n1x, n1y)) in zip(sides, sides[1:] + sides[:1]):
        if n0x * n1x + n0y * n1y < math.cos(math.radians(30)):
            corners |= (px - cx) ** 2 + (py - cy) ** 2 < 0.3 ** 2
    glow = np.where(outer < 0, np.exp(outer / 0.06), np.exp(-outer / 0.03))
    light = Line(outer, 0.005) + 0.10 * glow + 0.45 * Line(inner, 0.0035) * corners
    core = np.zeros_like(px)
    up = np.linspace(-0.6, 0.6, 4801)
    inside = up[Edge(np.zeros_like(up), up) <= 0]  # the outline's span up the middle, where the bars sit
    for edge in bars:
        centre = inside.max() - 0.055 if edge == "top" else inside.min() + 0.055
        bar = np.maximum(ax - aspect * 0.13, np.abs(py - centre) - 0.012)
        light = light + np.where(bar < 0, 1.6, 1.6 * np.exp(-bar / 0.02))
        core = np.maximum(core, Line(np.maximum(bar, 0), 0.006))
    if reticle:
        r = np.hypot(px, py)
        light = light + 0.9 * Line(r - 0.12, 0.004) + 0.9 * Line(r, 0.009)
        for along, across in ((ax, ay), (ay, ax)):  # the cross's four arms, 0.05 to 0.22 from the middle
            light = light + 0.9 * Line(np.maximum(np.maximum(0.05 - along, along - 0.22), across - 0.002), 0.002)
        for tick in (0.3, 0.38, 0.46):  # range ticks along the horizontal, shorter outward
            light = light + 0.6 * Line(np.maximum(np.abs(ax - tick) - 0.002, ay - (0.05 - 0.6 * (tick - 0.3))), 0.002)
    # sRGB about (0.25, 0.16, 0.1): the concept's screens' brown (39, 26, 23) once the game's grade has darkened it (Eden
    # Prime's took the first fill, (41, 23, 12), to (25, 17, 12): the game side, 2026-11-02)
    field = np.array([0.052, 0.022, 0.011], np.float32)
    hot = np.array([1.0, 0.8, 0.5], np.float32)  # the bars' near-white cores
    return np.clip(field[None, None] * (1 + 1.5 * glow[..., None]) + orange[None, None] * light[..., None]
                   + hot[None, None] * core[..., None], 0, 1)


def Relight(o, path, size=4096, samples=256, exposure=1.0, ambient=0.0, keep=("Screen", "Display", "Aim")):
    """`o`'s textured material with the scene's light baked in, on its own UVs: its image is the albedo (a Tripo
    texture already carries a soft light of its own), lit as Bake lights the shell (with the same `ambient`), written to `path`. The material
    then uses the new image; materials named in `keep` (the screens) are left as they are. Every object sharing that
    material (the mirrored pane) shows it too. Returns what it did."""
    start = time.time()
    me = o.data
    main = next(m for m in me.materials if m.name not in keep and any(n.type == "TEX_IMAGE" and n.image for n in m.node_tree.nodes))
    source = next(n for n in main.node_tree.nodes if n.type == "TEX_IMAGE" and n.image)
    target = bpy.data.images.new(o.name + "-target", size, size, alpha=True, float_buffer=True)
    spare = bpy.data.images.new("spare", 8, 8, alpha=True, float_buffer=True)
    added = []
    for m in me.materials:
        nt = m.node_tree
        node = nt.nodes.new("ShaderNodeTexImage")
        node.image = target if m is main else spare
        added.append((nt, node))
        if m is main:
            bsdf = nt.nodes.new("ShaderNodeBsdfDiffuse")
            worn = Worn(nt, source.outputs["Color"])
            nt.links.new(worn, bsdf.inputs["Color"])
            added += [(nt, n) for n in nt.nodes if n.get("worn")]
            nt.links.new(bsdf.outputs["BSDF"], next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL").inputs["Surface"])
            added.append((nt, bsdf))
        nt.nodes.active = node
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    device = Cycles(samples)
    passes = []
    for filters in ({"COLOR"}, {"DIRECT", "INDIRECT", "COLOR"}):
        target.pixels.foreach_set(np.zeros(size * size * 4, np.float32))
        bpy.context.scene.cycles.samples = 4 if len(filters) == 1 else samples
        bpy.ops.object.bake(type="DIFFUSE", pass_filter=filters, margin=8)
        px = np.empty(size * size * 4, np.float32)
        target.pixels.foreach_get(px)
        passes.append(px.reshape(size, size, 4))
    covered = passes[1][..., 3] > 0.5
    linear = passes[1][..., :3] * exposure + passes[0][..., :3] * ambient
    linear[~covered] = 0.008
    lit = Png(linear, o.name + "-lit", path)
    for nt, node in added:
        nt.nodes.remove(node)
    nt = main.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    nt.links.new(bsdf.outputs["BSDF"], next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL").inputs["Surface"])
    source.image = lit
    bpy.data.images.remove(target)
    bpy.data.images.remove(spare)
    return {"size": size, "device": device, "samples": samples, "exposure": exposure,
            "texelsCovered": round(float(covered.mean()), 3), "seconds": round(time.time() - start, 1)}


def Worn(nt, colour):
    """`colour` (a socket) as the chair's painted metal reads: times BALANCE; its value varied at low frequency
    (WEAR "vary"); its convex edges chipped toward bare metal (where a bevel's normal, `edge` wide, turns from the
    true one, broken up by noise); small rust specks. Built from vector maths, map range and textures only. The new
    nodes are tagged "worn". Returns the output socket."""
    made = []

    def Node(kind, **inputs):
        node = nt.nodes.new(kind)
        node["worn"] = True
        made.append(node)
        for k, v in inputs.items():
            node.inputs[k].default_value = v
        return node

    def Vec(op, a, b=None, scale=None):
        node = Node("ShaderNodeVectorMath")
        node.operation = op
        nt.links.new(a, node.inputs[0])
        if b is not None:
            if isinstance(b, tuple):
                node.inputs[1].default_value = b
            else:
                nt.links.new(b, node.inputs[1])
        if scale is not None:
            nt.links.new(scale, node.inputs["Scale"])
        return node.outputs["Vector"] if op != "DOT_PRODUCT" else node.outputs["Value"]

    def Range(value, lo, hi, to_lo=0.0, to_hi=1.0):
        node = Node("ShaderNodeMapRange", **{"From Min": lo, "From Max": hi, "To Min": to_lo, "To Max": to_hi})
        node.clamp = True
        nt.links.new(value, node.inputs["Value"])
        return node.outputs["Result"]

    def Mix(a, b, factor):  # a + (b - a) * factor
        return Vec("ADD", a, Vec("SCALE", Vec("SUBTRACT", b, a), scale=factor))

    def Times(a, b):
        node = Node("ShaderNodeMath")
        node.operation = "MULTIPLY"
        nt.links.new(a, node.inputs[0])
        nt.links.new(b, node.inputs[1])
        return node.outputs["Value"]

    where = Node("ShaderNodeTexCoord").outputs["Object"]

    def Noise(scale, detail):
        node = Node("ShaderNodeTexNoise", Scale=scale, Detail=detail)
        nt.links.new(where, node.inputs["Vector"])
        return node.outputs["Fac"]

    base = Vec("MULTIPLY", colour, BALANCE)
    base = Vec("SCALE", base, scale=Range(Noise(6.0, 3.0), 0.3, 0.7, 1 - WEAR["vary"] / 2, 1 + WEAR["vary"] / 2))
    bevel = Node("ShaderNodeBevel", Radius=WEAR["edge"])
    edge = Range(Vec("DOT_PRODUCT", bevel.outputs["Normal"], Node("ShaderNodeNewGeometry").outputs["Normal"]), 0.995, 0.93)
    chips = Times(edge, Range(Noise(30.0, 10.0), 0.56, 0.64))
    chip = Node("ShaderNodeRGB")
    chip.outputs["Color"].default_value = WEAR["chip"] + (1.0,)
    worn = Mix(base, chip.outputs["Color"], chips)
    voronoi = Node("ShaderNodeTexVoronoi", Scale=140.0)
    nt.links.new(where, voronoi.inputs["Vector"])
    specks = Times(Range(voronoi.outputs["Distance"], 0.06, 0.0), Range(Noise(9.0, 4.0), 0.55, 0.65))
    rust = Node("ShaderNodeRGB")
    rust.outputs["Color"].default_value = WEAR["rust"] + (1.0,)
    return Mix(worn, rust.outputs["Color"], specks)


def ScreenGlow(materials, strength):
    """The screens' materials (Principled, an image as base colour) emitting that image at `strength` (0: off), so a
    bake lights what is round them."""
    for m in materials:
        bsdf = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        image = next(n for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image)
        m.node_tree.links.new(image.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = strength
