# SPDX-License-Identifier: MIT
"""Makes the packaged cockpit models game-ready: fewer triangles where the difference can't be seen, normals the
packager welds well, and the measurements that say so (research/47 "Game-ready models").

    blender -b --factory-startup --python tools/cockpit/game_ready.py -- <assets folder>
        [--tolerance 0.0005] [--backing-tolerance 0.002] [--smooth-deg 30] [--keep-normals]

Reads what tools/cockpit/package reads (detached/<part>.glb for the controls, else the newest repair) and writes
<assets>/game-ready/<file>.glb, which the packager then prefers, game-ready/manifest.json and game-ready/previews/.
Nothing it reads is changed.

A repaired model is two surfaces: the textured front and the inward backing shell (the material without an image),
meeting at junction vertices. Each is decimated by edge collapse on its own, the junction vertices locked (weight 0 in
the modifier's vertex group), so the two still meet exactly and the model stays watertight. The backing may then cross
the front by up to the tolerance: both are one-sided, so wherever the front faces the eye the backing faces away and is
culled. The renders check that. A surface keeps the fewest triangles whose distance from its source stays within its
tolerance (a fraction of the bounding box's diagonal; the 99th percentile both ways, the maximum within 4 times it).
Then the custom normals are cleared and set smooth by angle (`--keep-normals` keeps Tripo's, interpolated by the
collapse). The in-game material is unlit, so they change nothing on screen yet; the packager welds a vertex only where
position, normal and UV all agree, and Tripo's cost the chair 11% more welded vertices and the front cockpit none
(2026-11-02).

The manifest has, per model, source against result: triangles per surface; the packager's welded vertices per
primitive (65,535 a mesh, Build.cs splits the backing off above that); the deviation both ways; boundary and
non-manifold edges; junction vertices kept; the embedded images' bytes; the texel density; and eight orthographic
renders as the game draws them (flat texture, back faces culled) with the silhouettes' overlap and the colour
difference where both cover. For the controls, the moving part against its base at the motion's samples
(detached/manifest.json): intersecting face pairs, source against result.
"""
import hashlib
import json
import math
import os
import struct
import sys
import tempfile
import time

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from detach_controls import Newest, Sha  # noqa: E402  the packager's rule for which repair is current

# What tools/cockpit/package/Build.cs packages: (file, from detached/).
MODELS = [("joystick-base", True), ("joystick-grip", True), ("throtle-control-base", True), ("throtle-control-handle", True),
          ("chair", False), ("front-cockpit", False)]
# detach_controls.py's moving parts: (base, moving) per asset.
PAIRS = {"joystick": ("joystick-base", "joystick-grip"), "throtle-control": ("throtle-control-base", "throtle-control-handle")}
MAX_VERTICES = 65535  # Build.cs MaxIndexableVertices
SAMPLES = 20000
RENDER_SIZE = 384
VIEWS = {"front": (0, -1, 0), "back": (0, 1, 0), "left": (-1, 0, 0), "right": (1, 0, 0), "top": (0, 0, 1), "bottom": (0, 0, -1),
         "front-right": (1, -1, 0.6), "back-left": (-1, 1, 0.6)}


def Import(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if len(meshes) != 1 or meshes[0].parent is not None:
        raise SystemExit(f"{path}: expected one mesh without a parent")
    return meshes[0]


def Textured(material):
    return bool(material and material.node_tree and any(n.type == "TEX_IMAGE" and n.image for n in material.node_tree.nodes))


def Surface(mesh, kinds, kind):
    """The vertices (float64) and the triangles of one surface: kind 1 the textured front, 0 the backing."""
    mesh.calc_loop_triangles()
    n = len(mesh.loop_triangles)
    tri = np.empty(n * 3, np.int32)
    mesh.loop_triangles.foreach_get("vertices", tri)
    poly = np.empty(n, np.int32)
    mesh.loop_triangles.foreach_get("polygon_index", poly)
    mat = np.empty(len(mesh.polygons), np.int32)
    mesh.polygons.foreach_get("material_index", mat)
    co = np.empty(len(mesh.vertices) * 3, np.float32)
    mesh.vertices.foreach_get("co", co)
    keep = kinds[np.minimum(mat[poly], len(kinds) - 1)] == kind
    return co.reshape(-1, 3).astype(np.float64), tri.reshape(-1, 3)[keep]


def Classes(mesh, kinds):
    """Per vertex: 1 only front faces, 2 only backing faces, 3 both or on an open or non-manifold edge (locked)."""
    cls = np.zeros(len(mesh.vertices), np.int8)
    for p in mesh.polygons:
        bit = 1 if kinds[min(p.material_index, len(kinds) - 1)] else 2
        for v in p.vertices:
            cls[v] |= bit
    bm = bmesh.new()
    bm.from_mesh(mesh)
    for e in bm.edges:
        if not e.is_manifold:
            for v in e.verts:
                cls[v.index] = 3
    bm.free()
    return cls


def Edges(mesh):
    bm = bmesh.new()
    bm.from_mesh(mesh)
    result = {"boundary": sum(1 for e in bm.edges if e.is_boundary), "nonManifold": sum(1 for e in bm.edges if not e.is_manifold)}
    bm.free()
    return result


def Sample(co, tri, count, seed):
    a, b, c = co[tri[:, 0]], co[tri[:, 1]], co[tri[:, 2]]
    area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
    rng = np.random.default_rng(seed)
    pick = rng.choice(len(tri), size=count, p=area / area.sum())
    r1 = np.sqrt(rng.random(count))
    r2 = rng.random(count)
    return a[pick] * (1 - r1)[:, None] + b[pick] * (r1 * (1 - r2))[:, None] + c[pick] * (r1 * r2)[:, None]


def Tree(co, tri):
    return BVHTree.FromPolygons(co.tolist(), tri.tolist(), all_triangles=True)


def Deviation(source, co, tri):
    """Distances between a surface and its source, both ways, in model units."""
    points, tree = source
    there = np.array([hit[3] for hit in map(Tree(co, tri).find_nearest, points.tolist())])
    back = np.array([hit[3] for hit in map(tree.find_nearest, Sample(co, tri, len(points), 2).tolist())])
    both = np.concatenate([there, back])
    return {"p50": float(np.percentile(both, 50)), "p99": float(np.percentile(both, 99)), "max": float(both.max()),
            "sourceToResultMax": float(there.max()), "resultToSourceMax": float(back.max())}


def Decimate(o, kinds, kind, source, tolerance):
    """Edge collapse among this surface's own vertices (everything else has weight 0, which the modifier never collapses);
    the fewest triangles whose deviation passes, found by bisection on the share kept."""
    _, tri = Surface(o.data, kinds, kind)
    mine = len(tri)
    if mine == 0:
        return None
    total = len(o.data.polygons)  # all triangles: the sources are, and collapse triangulates
    free = np.flatnonzero(Classes(o.data, kinds) == (1 if kind else 2)).tolist()
    if not free:
        # An empty group leaves the mesh without weights, and the modifier then locks nothing at all.
        return {"triangles": [mine, mine], "deviation": None, "tolerance": tolerance, "tries": [], "skipped": "no vertex of its own"}
    group = o.vertex_groups.new(name="free")
    group.add(free, 1.0, "REPLACE")
    mod = o.modifiers.new("decimate", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.use_collapse_triangulate = True
    mod.vertex_group = group.name

    def Try(share):
        mod.ratio = (total - mine + share * mine) / total
        deps = bpy.context.evaluated_depsgraph_get()
        eo = o.evaluated_get(deps)
        me = eo.to_mesh()
        co, t = Surface(me, kinds, kind)
        eo.to_mesh_clear()
        return Deviation(source, co, t), len(t)

    tries = []
    best = (1.0, None, mine)
    lo, hi = 0.03, 1.0
    for _ in range(9):
        share = math.sqrt(lo * hi)
        dev, n = Try(share)
        ok = dev["p99"] <= tolerance and dev["max"] <= 4 * tolerance
        tries.append({"share": round(share, 4), "triangles": n, "p99": dev["p99"], "max": dev["max"], "passes": ok})
        if ok:
            hi, best = share, (share, dev, n)
        else:
            lo = share
    share, dev, n = best
    mod.ratio = (total - mine + share * mine) / total
    new = bpy.data.meshes.new_from_object(o.evaluated_get(bpy.context.evaluated_depsgraph_get())) if share < 1.0 else None
    o.modifiers.remove(mod)
    o.vertex_groups.remove(group)  # the groups live on the mesh: before the mesh is swapped
    if new is not None:
        old = o.data
        o.data = new
        bpy.data.meshes.remove(old)
        for g in list(o.vertex_groups):
            o.vertex_groups.remove(g)
    return {"triangles": [mine, n], "deviation": dev, "tolerance": tolerance, "tries": tries}


def Smooth(o, degrees):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.mesh.customdata_custom_splitnormals_clear()
    o.data.shade_smooth()
    o.data.set_sharp_from_angle(angle=math.radians(degrees))


def Texel(mesh, kinds):
    """The front's texture pixels per model unit: the square root of its pixel area over its surface area."""
    sizes = {}
    for i, m in enumerate(mesh.materials):
        if kinds[i]:
            image = next(n.image for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image)
            sizes[i] = image.size[0] * image.size[1]
    if not sizes or not mesh.uv_layers.active:
        return None
    mesh.calc_loop_triangles()
    n = len(mesh.loop_triangles)
    loops = np.empty(n * 3, np.int32)
    mesh.loop_triangles.foreach_get("loops", loops)
    verts = np.empty(n * 3, np.int32)
    mesh.loop_triangles.foreach_get("vertices", verts)
    poly = np.empty(n, np.int32)
    mesh.loop_triangles.foreach_get("polygon_index", poly)
    mat = np.empty(len(mesh.polygons), np.int32)
    mesh.polygons.foreach_get("material_index", mat)
    uv = np.empty(len(mesh.loops) * 2, np.float32)
    mesh.uv_layers.active.data.foreach_get("uv", uv)
    co = np.empty(len(mesh.vertices) * 3, np.float32)
    mesh.vertices.foreach_get("co", co)
    uv, co = uv.reshape(-1, 2).astype(np.float64)[loops].reshape(-1, 3, 2), co.reshape(-1, 3).astype(np.float64)[verts].reshape(-1, 3, 3)
    pixels = np.array([sizes.get(m, 0) for m in mat[poly]], np.float64)
    keep = pixels > 0
    e1, e2 = uv[:, 1] - uv[:, 0], uv[:, 2] - uv[:, 0]
    uv_area = 0.5 * np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0])
    area = 0.5 * np.linalg.norm(np.cross(co[:, 1] - co[:, 0], co[:, 2] - co[:, 0]), axis=1)
    return float(math.sqrt((uv_area * pixels)[keep].sum() / area[keep].sum()))


def Glb(path):
    """What the packager sees in a .glb: per primitive the welded vertices (exact position, normal and UV bits) and
    triangles; and each embedded image's bytes, hashed."""
    data = open(path, "rb").read()
    json_length = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + json_length])
    blob = data[20 + json_length + 8:]

    def Accessor(index):
        a = doc["accessors"][index]
        if "sparse" in a:
            raise SystemExit(f"{path}: sparse accessors aren't read")
        view = doc["bufferViews"][a["bufferView"]]
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        dtype = np.dtype({5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a["componentType"]])
        stride = view.get("byteStride") or dtype.itemsize * width
        return np.ndarray((a["count"], width), dtype, blob, view.get("byteOffset", 0) + a.get("byteOffset", 0), (stride, dtype.itemsize)).copy()

    def Image(index):
        view = doc["bufferViews"][doc["images"][index]["bufferView"]]
        start = view.get("byteOffset", 0)
        return hashlib.sha256(blob[start:start + view["byteLength"]]).hexdigest()

    primitives = []
    for node in doc["nodes"]:
        for p in doc["meshes"][node["mesh"]]["primitives"] if "mesh" in node else []:
            at = p["attributes"]
            rows = np.concatenate([Accessor(at["POSITION"]), Accessor(at["NORMAL"]), Accessor(at["TEXCOORD_0"])], axis=1)
            index = Accessor(p["indices"]).ravel() if "indices" in p else np.arange(len(rows))
            rows = np.ascontiguousarray(rows[index]) + np.float32(0)  # -0 to +0: .NET's float equality has them equal
            welded = len(np.unique(rows.view(np.dtype((np.void, rows.dtype.itemsize * rows.shape[1])))))
            material = doc["materials"][p["material"]] if "material" in p else {}
            base = material.get("pbrMetallicRoughness", {}).get("baseColorTexture")
            primitives.append({"material": material.get("name"), "triangles": len(index) // 3, "welded": welded,
                               "baseColor": Image(doc["textures"][base["index"]]["source"]) if base else None})
    return {"primitives": primitives, "welded": sum(p["welded"] for p in primitives)}


def Frame(o):
    pts = [o.matrix_world @ Vector(b) for b in o.bound_box]
    lo = Vector([min(p[a] for p in pts) for a in range(3)])
    hi = Vector([max(p[a] for p in pts) for a in range(3)])
    return (lo + hi) / 2, max(hi - lo)


def Render(frame, folder, tag):
    """The scene from each view, orthographic, as the game draws it: the texture flat (the material is unlit), back
    faces culled (it isn't two-sided). RGBA arrays, rows bottom first."""
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.color_type = "TEXTURE"
    sc.display.shading.light = "FLAT"
    sc.display.shading.show_backface_culling = True
    sc.render.film_transparent = True
    sc.view_settings.view_transform = "Standard"
    sc.render.resolution_x = sc.render.resolution_y = RENDER_SIZE
    sc.render.resolution_percentage = 100
    centre, size = frame
    cam = bpy.data.cameras.new("view")
    cam.type = "ORTHO"
    cam.ortho_scale = size * 1.15
    cam.clip_end = size * 10
    co = bpy.data.objects.new("view", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    out = {}
    for name, d in VIEWS.items():
        d = Vector(d).normalized()
        co.location = centre + d * size * 3
        co.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(folder, f"{tag}-{name}.png")
        bpy.ops.render.render(write_still=True)
        image = bpy.data.images.load(sc.render.filepath)
        px = np.empty(RENDER_SIZE * RENDER_SIZE * 4, np.float32)
        image.pixels.foreach_get(px)
        bpy.data.images.remove(image)
        out[name] = px.reshape(RENDER_SIZE, RENDER_SIZE, 4)
    bpy.data.objects.remove(co)
    bpy.data.cameras.remove(cam)
    return out


def Compare(before, after):
    rows = {}
    for view in before:
        a, b = before[view], after[view]
        ma, mb = a[..., 3] > 0.5, b[..., 3] > 0.5
        both, either = ma & mb, ma | mb
        diff = np.abs(a[..., :3] - b[..., :3]).max(-1)[both] * 255
        rows[view] = {"silhouetteIoU": float(both.sum() / max(either.sum(), 1)), "colourDiffMean": float(diff.mean()),
                      "colourDiffP99": float(np.percentile(diff, 99)), "pixelsOver16": float((diff > 16).mean())}
    return rows


def Sheet(before, after, path):
    """The views side by side on grey: the source, the result, and their difference (the colour's x 4 in red where
    both cover, yellow where only one does)."""
    def Row(shots):
        return np.concatenate([s[..., :3] * s[..., 3:] + 0.5 * (1 - s[..., 3:]) for s in shots.values()], axis=1)

    def Difference(a, b):
        ma, mb = a[..., 3] > 0.5, b[..., 3] > 0.5
        out = np.zeros(a.shape[:2] + (3,), np.float32)
        out[..., 0] = np.where(ma & mb, np.minimum(np.abs(a[..., :3] - b[..., :3]).max(-1) * 4, 1), 0)
        out[ma != mb] = (1, 1, 0)
        return out
    diff = np.concatenate([Difference(before[v], after[v]) for v in before], axis=1)
    rgb = np.concatenate([diff, Row(after), Row(before)], axis=0)  # bottom row first
    h, w = rgb.shape[:2]
    image = bpy.data.images.new("sheet", w, h, alpha=True)
    image.pixels.foreach_set(np.concatenate([rgb, np.ones((h, w, 1), np.float32)], axis=2).astype(np.float32).ravel())
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    bpy.data.images.remove(image)


def Export(o, path):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    # The node keeps the object's place: a detached part's origin is its pivot.
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_format="GLB", export_image_format="AUTO", export_apply=False)


def Model(root, name, detached, options, scratch):
    src = os.path.join(root, "detached", name + ".glb") if detached else Newest(root, name)
    out = os.path.join(root, "game-ready", name + ".glb")
    o = Import(src)
    frame = Frame(o)
    before = Render(frame, scratch, f"{name}-source")
    kinds = np.array([1 if Textured(m) else 0 for m in o.data.materials] or [1], np.int8)
    co, _ = Surface(o.data, kinds, 1)
    diagonal = float(np.linalg.norm(co.max(0) - co.min(0)))
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=diagonal * 1e-6)  # the files are triangle soup
    bm.to_mesh(o.data)
    bm.free()
    info = {"source": os.path.relpath(src, root).replace("\\", "/"), "sourceSha256": Sha(src), "diagonal": diagonal,
            "edges": {"source": Edges(o.data)}, "texel": {"source": Texel(o.data, kinds)}}
    cls = Classes(o.data, kinds)
    locked = {tuple(o.data.vertices[i].co) for i in np.flatnonzero(cls == 3)}
    sources = {}
    for kind in (1, 0):
        sco, stri = Surface(o.data, kinds, kind)
        if len(stri):
            sources[kind] = (Sample(sco, stri, SAMPLES, 1), Tree(sco, stri))
    info["front"] = Decimate(o, kinds, 1, sources.get(1), options["tolerance"] * diagonal)
    info["backing"] = Decimate(o, kinds, 0, sources.get(0), options["backing-tolerance"] * diagonal) if 0 in sources else None
    after_positions = {tuple(v.co) for v in o.data.vertices}
    info["lockedKept"] = [sum(1 for p in locked if p in after_positions), len(locked)]
    if info["lockedKept"][0] != len(locked):
        raise SystemExit(f"{name}: {len(locked) - info['lockedKept'][0]} locked vertices moved; the surfaces no longer meet")
    for kind, key in ((1, "front"), (0, "backing")):
        if info[key] is not None:
            info[key]["final"] = Deviation(sources[kind], *Surface(o.data, kinds, kind))
    info["edges"]["result"] = Edges(o.data)
    info["texel"]["result"] = Texel(o.data, kinds)
    if options["keep-normals"]:
        info["normals"] = "the source's custom normals, interpolated by the collapse"
    else:
        Smooth(o, options["smooth-deg"])
        info["normals"] = f"smooth, sharp above {options['smooth-deg']:g} degrees"
    Export(o, out)
    info["output"] = os.path.relpath(out, root).replace("\\", "/")
    glb_source, glb_result = Glb(src), Glb(out)
    info["packager"] = {"source": glb_source, "result": glb_result, "limit": MAX_VERTICES}
    # The packager takes only the base colour; the exporter rebuilds a packed metallic-roughness image.
    info["baseColorUnchanged"] = [p["baseColor"] for p in glb_source["primitives"]] == [p["baseColor"] for p in glb_result["primitives"]]
    after = Render(frame, scratch, f"{name}-result")
    info["renders"] = Compare(before, after)
    Sheet(before, after, os.path.join(root, "game-ready", "previews", name + ".png"))
    return info


def Clashes(base_path, moving_path, pivot, motion):
    """Intersecting face pairs between a moving part and its base at the motion's samples (as check_controls.py)."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    parts = []
    for path in (base_path, moving_path):
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=path)
        parts.append(next(o for o in bpy.data.objects if o not in before and o.type == "MESH"))

    def Bvh(o, matrix):
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bm.transform(matrix)
        tree = BVHTree.FromBMesh(bm)
        bm.free()
        return tree

    base, moving = parts
    tree = Bvh(base, base.matrix_world)
    home = moving.matrix_world.copy()
    if motion["kind"] == "twist":
        samples = {f"{a:+g} deg": Matrix.Translation(pivot) @ Matrix.Rotation(math.radians(a), 4, "Z") @ Matrix.Translation(-pivot) @ home
                   for a in (-motion["limitDeg"], -motion["limitDeg"] / 2, 0.0, motion["limitDeg"] / 2, motion["limitDeg"])}
    else:
        samples = {f"{dx:+.3f}": Matrix.Translation((dx, 0, 0)) @ home
                   for dx in (motion["min"], motion["neutral"], 0.0, motion["max"])}
    return {k: len(tree.overlap(Bvh(moving, m))) for k, m in samples.items()}


def Run(root, options):
    started = time.time()
    os.makedirs(os.path.join(root, "game-ready", "previews"), exist_ok=True)
    manifest = {"options": options, "models": {}, "controls": {}}
    with tempfile.TemporaryDirectory() as scratch:
        for name, detached in MODELS:
            info = Model(root, name, detached, options, scratch)
            manifest["models"][name] = info
            f, b, p, r = info["front"], info["backing"], info["packager"], info["renders"].values()
            dev = f["final"]
            print(f"RESULT {name}: front {f['triangles'][0]} -> {f['triangles'][1]} tris"
                  + (f", backing {b['triangles'][0]} -> {b['triangles'][1]}" if b else "")
                  + f"; welded {p['source']['welded']} -> {p['result']['welded']} (limit {MAX_VERTICES})"
                  + f"; front p99 {dev['p99'] / info['diagonal']:.5f}, max {dev['max'] / info['diagonal']:.5f} of the diagonal"
                  + f"; locked kept {info['lockedKept'][0]}/{info['lockedKept'][1]}; edges {info['edges']['source']} -> {info['edges']['result']}"
                  + f"; base colour unchanged {info['baseColorUnchanged']}; worst view: IoU {min(x['silhouetteIoU'] for x in r):.4f}"
                  + f", colour diff mean {max(x['colourDiffMean'] for x in r):.2f}, over 16/255 {max(x['pixelsOver16'] for x in r):.4f}")
    detached = json.load(open(os.path.join(root, "detached", "manifest.json"), encoding="utf-8"))
    for asset, (base, moving) in PAIRS.items():
        pivot = Vector(detached[asset]["moving"]["pivot"])
        motion = detached[asset]["motion"]
        row = {"source": Clashes(os.path.join(root, "detached", base + ".glb"), os.path.join(root, "detached", moving + ".glb"), pivot, motion),
               "result": Clashes(os.path.join(root, "game-ready", base + ".glb"), os.path.join(root, "game-ready", moving + ".glb"), pivot, motion)}
        manifest["controls"][asset] = row
        print(f"RESULT {asset} clashes (face pairs) source {row['source']} result {row['result']}")
    manifest["seconds"] = round(time.time() - started, 1)
    with open(os.path.join(root, "game-ready", "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:]
    options = {"tolerance": 0.0005, "backing-tolerance": 0.002, "smooth-deg": 30.0, "keep-normals": False}
    root = None
    i = 0
    while i < len(args):
        if args[i] == "--keep-normals":
            options["keep-normals"] = True
        elif args[i].startswith("--") and args[i][2:] in options:
            options[args[i][2:]] = float(args[i + 1])
            i += 1
        elif root is None:
            root = args[i]
        else:
            raise SystemExit(f"unknown argument {args[i]}")
        i += 1
    if root is None:
        raise SystemExit(__doc__)
    Run(os.path.abspath(root), options)
