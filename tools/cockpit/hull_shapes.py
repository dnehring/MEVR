# SPDX-License-Identifier: MIT
"""Modelling pieces for the cockpit hull's shell (hull_assemble.py, research/48).

Every face carries a surface class (its material index into CLASSES) and UVs into that class's trim of the side pane's
atlas (UV layer "trim"). The pieces: profiles swept along paths with mitred corners (window frames, the roof's edge,
beams), panels set into a surface, chamfered floor plates; Display() paints an idle screen. Bake() then gives the shell a texture of its own on a fresh
unwrap: the classes' colours, ambient occlusion and a soft light from a point inside the cockpit. The game draws the cockpit unlit
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

CLASSES = ("PLATE", "DARK", "LIGHT", "RED", "SEAM")
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


def Sweep(b, path, up, profile, classes, closed=False):
    """A profile swept along a polyline, mitred at its corners.

    `profile`: (side, up) points, where `up` is the given direction (one vector, or a list with one per path point)
    made square to the path, and `side` is up x the path's direction. Each profile edge's faces point to its right in
    those axes, so a counter-clockwise loop (last point = first) is a beam's outside and an open strip faces the side to
    its right. `classes`: one, or one per profile edge. `closed`: the path is a loop; else a loop profile's ends are
    capped. Returns the rings: the profile's points at each path point."""
    P = [Vector(p) for p in path]
    n = len(P)
    ups = [Vector(u) for u in up] if isinstance(up, list) else [Vector(up)] * n
    loop = len(profile) > 2 and tuple(profile[0]) == tuple(profile[-1])
    pts = profile[:-1] if loop else profile
    m = len(pts)
    edges = m if loop else m - 1
    if isinstance(classes, str):
        classes = [classes] * edges
    segments = list(range(n if closed else n - 1))
    frames = []
    for k in segments:
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
        rings.append([P[i] + ms * s + mu * u for s, u in pts])
    for k, (t, s, u) in zip(segments, frames):
        r0, r1 = rings[k], rings[(k + 1) % n]
        for j in range(edges):
            j1 = (j + 1) % m
            ds, du = pts[j1][0] - pts[j][0], pts[j1][1] - pts[j][1]
            b.Face([r0[j], r1[j], r1[j1], r0[j1]], classes[j], toward=s * du - u * ds, uvs=QUAD)
    if loop and not closed:
        b.Face(rings[0], classes[0], toward=-frames[0][0])
        b.Face(rings[-1], classes[0], toward=frames[-1][0])
    return rings


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


def Bake(o, atlas, trims, tints, path, occluders, points, size=4096, light=(0.0, -0.15, 0.15), reach=0.6, distance=0.05):
    """Gives `o` a texture of its own, written to `path` (PNG), and returns what it did.

    A fresh unwrap ("bake") of the faces seen from `points` (the rest share one dark texel in a corner); the atlas
    through each class's trim UVs times its tint (`tints`, linear), times ambient occlusion within `distance` (the
    occluders count) and a light at the point `light`, by the angle to it and fading with distance (`reach`: where it
    has halved). Faces turned to the cockpit's middle so read apart from faces turned away, which in the unlit game is
    all a bevel has. Afterwards `o` has one material with that image, the unwrap as its
    only UVs, and every face's material index 0."""
    start = time.time()
    me = o.data
    seen = Seen(o, occluders, points)
    # the bake materials: the atlas through the trim UVs, tinted, into the target image
    target = bpy.data.images.new("bake-target", size, size, alpha=True, float_buffer=True)
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
        bsdf = nt.nodes.new("ShaderNodeBsdfDiffuse")
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        nt.links.new(uv.outputs["UV"], tex.inputs["Vector"])
        nt.links.new(tex.outputs["Color"], tint.inputs["Color"])
        nt.links.new(tint.outputs["Color"], bsdf.inputs["Color"])
        nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
        node = nt.nodes.new("ShaderNodeTexImage")
        node.image = target
        nt.nodes.active = node
        targets.append(node)
        me.materials.append(m)
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
    Pass("AO", 128)
    Pass("NORMAL", 1, normal_space="OBJECT")
    # each texel's position, as emission (0.5 + p / 2)
    for m, node in zip(me.materials, targets):
        nt = m.node_tree
        geometry = nt.nodes.new("ShaderNodeNewGeometry")
        scale = nt.nodes.new("ShaderNodeVectorMath")
        scale.operation = "MULTIPLY_ADD"
        scale.inputs[1].default_value = (0.5, 0.5, 0.5)
        scale.inputs[2].default_value = (0.5, 0.5, 0.5)
        emission = nt.nodes.new("ShaderNodeEmission")
        nt.links.new(geometry.outputs["Position"], scale.inputs[0])
        nt.links.new(scale.outputs["Vector"], emission.inputs["Color"])
        nt.links.new(emission.outputs["Emission"], next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL").inputs["Surface"])
        nt.nodes.active = node
    Pass("EMIT", 1)
    albedo, covered = passes["DIFFUSE"][..., :3], passes["DIFFUSE"][..., 3] > 0.5
    ao = passes["AO"][..., 0]
    normal = passes["NORMAL"][..., :3] * 2 - 1
    to = np.array(light, np.float32) - (passes["EMIT"][..., :3] - 0.5) * 2
    dist = np.linalg.norm(to, axis=-1) + 1e-6
    lit = np.clip((normal * to).sum(-1) / dist, 0, 1) / (1 + (dist / reach) ** 2)
    linear = albedo * ((0.30 + 0.70 * ao) * (0.40 + 0.75 * lit))[..., None]
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
    return {"size": size, "device": device, "facesSeen": len(seen), "faces": len(me.polygons),
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


def Display(width, height, aspect, bars=()):
    """An idle screen in the game's orange interface style, as linear RGB (rows bottom first) for a screen `aspect` times
    as wide as it is tall, whatever the image's own size: a dark amber field warming toward a thin glowing frame with cut
    corners just inside the edge, brackets at an inner frame's corners, and short lit bars on the edges named in `bars`
    ("top", "bottom"). Sizes are fractions of the screen's height."""
    y, x = np.mgrid[0:height, 0:width].astype(np.float32)
    px = ((x + 0.5) / width - 0.5) * aspect
    py = (y + 0.5) / height - 0.5
    ax, ay = np.abs(px), np.abs(py)

    def Box(a, b, c):
        """Signed distance to a rectangle of half sizes a and b with its corners cut by c (negative inside)."""
        return np.maximum(np.maximum(ax - a, ay - b), (ax + ay - (a + b - c)) / math.sqrt(2))

    def Line(d, width):
        return np.exp(-(d / width) ** 2)

    orange = np.array([1.0, 0.36, 0.06], np.float32)
    outer = Box(aspect / 2 - 0.055, 0.5 - 0.055, 0.13)
    inner = Box(aspect / 2 - 0.115, 0.5 - 0.115, 0.07)
    corners = (ax > aspect / 2 - 0.115 - 0.16) & (ay > 0.5 - 0.115 - 0.16)
    glow = np.where(outer < 0, np.exp(outer / 0.06), np.exp(-outer / 0.03))
    light = Line(outer, 0.005) + 0.10 * glow + 0.45 * Line(inner, 0.0035) * corners
    core = np.zeros_like(px)
    for edge in bars:
        centre = (0.5 - 0.055) * (1 if edge == "top" else -1)
        bar = np.maximum(ax - aspect * 0.13, np.abs(py - centre) - 0.012)
        light = light + np.where(bar < 0, 1.6, 1.6 * np.exp(-bar / 0.02))
        core = np.maximum(core, Line(np.maximum(bar, 0), 0.006))
    field = np.array([0.022, 0.0085, 0.0035], np.float32)  # sRGB about (0.16, 0.09, 0.05), the concept's screens' brown
    hot = np.array([1.0, 0.8, 0.5], np.float32)  # the bars' near-white cores
    return np.clip(field[None, None] * (1 + 1.5 * glow[..., None]) + orange[None, None] * light[..., None]
                   + hot[None, None] * core[..., None], 0, 1)
