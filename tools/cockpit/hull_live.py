# SPDX-License-Identifier: MIT
"""hull_assemble's geometry inside a running Blender session (the Higgsfield bridge's `bl_execute`, research/48 section
16): the shell, the panes and the console, no bake, into the LIVE_Hull collection under RIG_Live (3 m per hull unit),
replacing the previous live build in about 1.5 s. For reviewing a change beside the concept before the full build;
hull_assemble.py stays the build of record.

    import sys, importlib; sys.path.insert(0, r"<repo>/tools/cockpit"); import hull_live; importlib.reload(hull_live)
    hull_live.Cameras(); result = hull_live.Build()
    result = hull_live.Lit("seat", views=[("ahead", hull_live.H.EYE, (0, 0.4, 0.33), 14)], fill=6.0)

Cameras() loads the lit concept as the image "concept_lit" and adds its two viewpoints: CAM_Concept (matched by eye to
the room) and CAM_ConsoleFit (fitted to the console's screens). Overlay() and Lit(camname=...) put a render beside the
concept; Shots() and Lit(views=...) render hull-unit views; Blocked() measures what the A-pillar zone hides at eye
level. Images go to $HULL_LIVE_OUT (default: hull-live in the temp folder). Everything made here is named LIVE_ and
removed by the next build; nothing else in the session is touched.
"""
import importlib
import math
import os
import sys
import tempfile

import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hull_assemble as H  # noqa: E402
import hull_shapes  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FOLDER = os.path.join(REPO, "concept", "assets", "hull reconstruction")
SCRATCH = os.environ.get("HULL_LIVE_OUT") or os.path.join(tempfile.gettempdir(), "hull-live")
os.makedirs(SCRATCH, exist_ok=True)
CONCEPT = "mako-interior-plain-front-2.png"
# The concept's viewpoints (hull units, 13 mm lens on 36 mm at its 1671 x 941): by eye against the room, and fitted to
# the console's screens (Gauss-Newton on the centre screen's corners, the same Tripo console as ours; research/48
# section 16). The two disagree: the concept is not one consistent space.
VIEWS = {"CAM_Concept": ((0.0, -0.52, 0.25), 0.0201), "CAM_ConsoleFit": ((0.0, -0.2924, 0.1911), 0.0016)}  # eye, pitch down (rad)
CLASS_COLOURS = {"PLATE": (0.13, 0.125, 0.13), "DARK": (0.06, 0.058, 0.062), "LIGHT": (0.42, 0.39, 0.4), "RED": (0.2, 0.004, 0.008),
                 "SEAM": (0.02, 0.02, 0.022), "LAMP": (1.0, 0.85, 0.6), "LAMP_ORANGE": (1.0, 0.5, 0.15), "LAMP_BLUE": (0.7, 0.85, 1.0), "METAL": (0.2, 0.19, 0.19)}


def Cameras():
    """The concept image (as "concept_lit") and its two viewpoints, in REF_Concept; returns their names."""
    image = bpy.data.images.get("concept_lit")
    if image is None:
        image = bpy.data.images.load(os.path.join(FOLDER, CONCEPT), check_existing=True)
        image.name = "concept_lit"
    ref = Collection("REF_Concept")
    for name, (eye, pitch) in VIEWS.items():
        o = bpy.data.objects.get(name)
        if o is None:
            cam = bpy.data.cameras.new(name)
            cam.lens, cam.sensor_width, cam.clip_start = 13.0, 36.0, 0.01
            bg = cam.background_images.new()
            bg.image, bg.alpha, bg.display_depth, bg.frame_method = image, 0.5, "FRONT", "STRETCH"
            cam.show_background_images = True
            o = bpy.data.objects.new(name, cam)
            ref.objects.link(o)
            o.location = Vector(eye) * 3
            o.rotation_euler = (math.pi / 2 - pitch, 0.0, 0.0)
    return list(VIEWS)


def Collection(name, parent=None):
    c = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    parent = parent or bpy.context.scene.collection
    if c.name not in parent.children:
        parent.children.link(c)
    return c


def Clear():
    """Removes the previous live build (only what this script made: the LIVE_Hull collection's objects)."""
    c = bpy.data.collections.get("LIVE_Hull")
    if not c:
        return 0
    n = 0
    for o in list(c.objects):
        data = o.data
        bpy.data.objects.remove(o)
        if data is not None and data.users == 0 and isinstance(data, bpy.types.Mesh):
            bpy.data.meshes.remove(data)
        n += 1
    made = ("LIVE_", "Screen", "Display", "tripo")  # what a live build makes; only orphans of those go
    for m in list(bpy.data.materials):
        if m.name.startswith(made) and m.users == 0:
            bpy.data.materials.remove(m)
    for i in list(bpy.data.images):
        if i.name.startswith(("Screen", "Display")) and i.users == 0:
            bpy.data.images.remove(i)
    for me in list(bpy.data.meshes):
        if me.users == 0 and me.name.startswith(("tripo", "side-", "console", "shell", "walls", "cut")):
            bpy.data.meshes.remove(me)
    return n


def Build(furniture=False, **overrides):
    """The live build; on a failure everything this run made is removed again before the error is raised. `overrides`:
    hull_assemble constants to try for this build only (Build(CONSOLE_GROW=(1, 1.2, 1.2)))."""
    removed = Clear()
    before = set(bpy.data.objects)
    mats = set(bpy.data.materials)
    try:
        out = Make(before, furniture, overrides)
    except Exception:
        for o in set(bpy.data.objects) - before:
            data = o.data
            bpy.data.objects.remove(o)
            if isinstance(data, bpy.types.Mesh) and data.users == 0:
                bpy.data.meshes.remove(data)
        for m in set(bpy.data.materials) - mats:
            if m.users == 0:
                bpy.data.materials.remove(m)
        raise
    out["removed"] = removed
    return out


def Make(before, furniture, overrides):
    global H
    importlib.reload(hull_shapes)
    H = importlib.reload(H)
    for k, v in overrides.items():
        assert hasattr(H, k), k
        setattr(H, k, v)
    pane_src, pbm = H.Import(os.path.join(FOLDER, H.PANE))
    console_src, cbm = H.Import(os.path.join(FOLDER, H.CONSOLE))
    left, right, console_m = H.Placements(pbm, cbm)
    pieces = {"side-left": H.Placed("side-left", pane_src, pbm, left), "side-right": H.Placed("side-right", pane_src, pbm, right, mirrored=True),
              "console": H.Placed("console", console_src, cbm, console_m)}
    pbm.free()
    cbm.free()
    screens = H.Screens(pieces["console"], SCRATCH)
    made, placed, pedestals = H.Furnish(os.path.dirname(FOLDER))
    if not furniture:
        for o in made:
            bpy.data.objects.remove(o)
        made = []
    opening, rim, area = H.PaneOpening(pieces["side-right"])
    wall = {"floor": [H.PaneFace(pieces["side-right"], y, 0.012) for y in H.FLOOR_STRIPS]}
    pillar = H.PillarEdge(pieces["side-right"])
    cut = {k: H.CutPillar(pieces[k], H.WindowPlane(pillar), mirrored=k == "side-left") for k in ("side-left", "side-right")}
    shell, windows, lights, dims = H.Shell(pillar, (opening, rim), pedestals, wall,
                                           {"desk": H.DeskShelf(pieces["console"]), "screen": screens["centre"],
                                            "bezel": H.Bezel(pieces["console"], screens["centre"]),
                                            "column": H.Column(pieces["console"], screens["centre"])})
    for cls in hull_shapes.CLASSES:  # flat colours near the baked look, so the classes read in the Workbench views
        m = bpy.data.materials.new("LIVE_" + cls)
        m.diffuse_color = CLASS_COLOURS[cls] + (1.0,)
        m.use_nodes = True
        bsdf = next(k for k in m.node_tree.nodes if k.type == "BSDF_PRINCIPLED")
        bsdf.inputs["Base Color"].default_value = CLASS_COLOURS[cls] + (1.0,)
        bsdf.inputs["Roughness"].default_value = 0.55
        if cls.startswith("LAMP"):
            bsdf.inputs["Emission Color"].default_value = hull_shapes.GLOW[cls] + (1.0,)
            bsdf.inputs["Emission Strength"].default_value = 6.0
        shell.data.materials.append(m)
    bpy.app.driver_namespace["LIVE_LAST"] = {"lights": lights, "screens": [m for m in pieces["console"].data.materials if m.name.startswith(("Screen", "Display"))]}
    pieces["shell"] = shell
    live = Collection("LIVE_Hull")
    rig = bpy.data.objects.get("RIG_Live")
    if rig is None:
        rig = bpy.data.objects.new("RIG_Live", None)
        rig.scale = (3, 3, 3)
    if rig.name not in live.objects:
        for c in rig.users_collection:
            c.objects.unlink(rig)
        live.objects.link(rig)
    keep = set(pieces.values()) | set(made)
    for o in set(bpy.data.objects) - before:
        if o == rig:
            continue
        if o not in keep:
            data = o.data
            bpy.data.objects.remove(o)
            if isinstance(data, bpy.types.Mesh) and data.users == 0:
                bpy.data.meshes.remove(data)
            continue
        for c in o.users_collection:
            c.objects.unlink(o)
        live.objects.link(o)
        o.parent = rig
        o.name = "LIVE_" + o.name.split(".")[0]
    tris = {o.name: sum(len(p.vertices) - 2 for p in o.data.polygons) for o in keep}
    return {"objects": sorted(o.name for o in keep), "triangles": tris, "paneCut": cut,
            "dims": {k: v for k, v in dims.items() if k != "paneWindow"}, "lights": len(lights)}


def Overlay(camname, tag, hide=("chair", "joystick", "throtle"), only=None):
    """Renders `camname` (Workbench, textures, flat) at the concept's size and writes <tag>.png: our render above, the
    50/50 mix with the concept below. `only`: the collection whose objects are shown (the others hidden)."""
    sc = bpy.context.scene
    keep = (sc.camera, sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.render.filepath)
    sc.camera = bpy.data.objects[camname]
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.color_type = "TEXTURE"
    sc.display.shading.light = "FLAT"
    sc.display.shading.show_backface_culling = True
    ref = bpy.data.images["concept_lit"]
    W, Hh = ref.size
    sc.render.resolution_x, sc.render.resolution_y = W, Hh
    sc.render.resolution_percentage = 100
    shown = bpy.data.collections.get(only) if only else None
    hidden = [o for o in sc.objects if o.type == "MESH" and not o.hide_render and
              (any(h in o.name for h in hide) or (shown is not None and o.name not in shown.objects))]
    for o in hidden:
        o.hide_render = True
    path = os.path.join(SCRATCH, tag + "-render.png")
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    for o in hidden:
        o.hide_render = False
    sc.camera, sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.render.filepath = keep
    r = bpy.data.images.load(path)
    a = np.empty(W * Hh * 4, np.float32)
    r.pixels.foreach_get(a)
    bpy.data.images.remove(r)
    b = np.empty(W * Hh * 4, np.float32)
    ref.pixels.foreach_get(b)
    a, b = a.reshape(Hh, W, 4), b.reshape(Hh, W, 4)
    mix = a * 0.5 + b * 0.5
    mix[..., 3] = 1
    sheet = np.concatenate([mix, a], 0)
    o = bpy.data.images.new("ov", W, 2 * Hh)
    o.pixels.foreach_set(sheet.ravel())
    o.filepath_raw = os.path.join(SCRATCH, tag + ".png")
    o.file_format = "PNG"
    o.save()
    bpy.data.images.remove(o)
    return os.path.join(SCRATCH, tag + ".png")


def Shots(tag, views, size=(800, 600), only="LIVE_Hull"):
    """Renders hull-unit views (name, eye, target, lens) of the live build into one sheet <tag>.png, two per row."""
    sc = bpy.context.scene
    keep = (sc.camera, sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.render.filepath)
    cam = bpy.data.cameras.new("LIVE_view")
    cam.clip_start = 0.01
    co = bpy.data.objects.new("LIVE_view", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.color_type = "TEXTURE"
    sc.display.shading.light = "FLAT"
    sc.display.shading.show_backface_culling = True
    sc.render.resolution_x, sc.render.resolution_y = size
    sc.render.resolution_percentage = 100
    shown = bpy.data.collections.get(only)
    hidden = [o for o in sc.objects if o.type == "MESH" and not o.hide_render and o.name not in shown.objects]
    for o in hidden:
        o.hide_render = True
    tiles = []
    for name, eye, target, lens in views:
        e, t = Vector(eye) * 3, Vector(target) * 3
        cam.lens = lens
        co.location = e
        co.rotation_euler = (t - e).to_track_quat("-Z", "Y").to_euler()
        path = os.path.join(SCRATCH, f"{tag}-{name}.png")
        sc.render.filepath = path
        bpy.ops.render.render(write_still=True)
        r = bpy.data.images.load(path)
        a = np.empty(size[0] * size[1] * 4, np.float32)
        r.pixels.foreach_get(a)
        bpy.data.images.remove(r)
        tiles.append(a.reshape(size[1], size[0], 4))
    for o in hidden:
        o.hide_render = False
    bpy.data.objects.remove(co)
    bpy.data.cameras.remove(cam)
    sc.camera, sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.render.filepath = keep
    if len(tiles) % 2:
        tiles.append(np.zeros_like(tiles[0]))
    rows = [np.concatenate(tiles[i:i + 2], 1) for i in range(0, len(tiles), 2)]
    sheet = np.concatenate(rows[::-1], 0)
    o = bpy.data.images.new("sh", sheet.shape[1], sheet.shape[0])
    o.pixels.foreach_set(sheet.ravel())
    o.filepath_raw = os.path.join(SCRATCH, tag + ".png")
    o.file_format = "PNG"
    o.save()
    bpy.data.images.remove(o)
    return os.path.join(SCRATCH, tag + ".png")



def Lit(tag, views=None, camname=None, size=(800, 600), samples=24, glow=5.0, fill=1.5, power=1.0, furniture=False):
    """An EEVEE preview of the live build lit as the bake lights it: the build's lamps as area lights (scaled with the
    rig: x3 in size, x9 in power), the screens glowing, a dim warm world. `views` as Shots, or `camname` at the
    concept's size with the 50/50 mix (Overlay's layout)."""
    sc = bpy.context.scene
    rig = bpy.data.objects["RIG_Live"]
    live = bpy.data.collections["LIVE_Hull"]
    made = []
    LAST = bpy.app.driver_namespace["LIVE_LAST"]
    for centre, n, u, (su, sv), colour, pw in LAST["lights"]:
        light = bpy.data.lights.new("LIVE_lamp", "AREA")
        light.shape = "RECTANGLE"
        light.size, light.size_y = su, sv
        light.energy = pw * 9 * power
        light.color = colour
        o = bpy.data.objects.new("LIVE_lamp", light)
        z = -n.normalized()
        x = (u - z * u.dot(z)).normalized()
        o.matrix_world = Matrix((x, z.cross(x), z)).transposed().to_4x4()
        o.location = centre
        live.objects.link(o)
        o.parent = rig
        made.append(o)
    hull_shapes.ScreenGlow(LAST["screens"], glow)
    world = bpy.data.worlds.get("LIVE_World") or bpy.data.worlds.new("LIVE_World")
    world.use_nodes = True
    bg = next(k for k in world.node_tree.nodes if k.type == "BACKGROUND")
    bg.inputs["Color"].default_value = H.FILL[0] + (1.0,)
    bg.inputs["Strength"].default_value = fill
    keep = (sc.camera, sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.render.filepath, sc.world,
            sc.view_settings.view_transform, sc.view_settings.look)
    sc.world = world
    sc.render.engine = "BLENDER_EEVEE"
    sc.eevee.taa_render_samples = samples
    sc.view_settings.view_transform = "AgX"
    hidden = [o for o in sc.objects if o.type == "MESH" and not o.hide_render and
              (o.name not in live.objects or (not furniture and any(h in o.name for h in ("chair", "joystick", "throtle"))))]
    for o in hidden:
        o.hide_render = True
    out = []
    try:
        if camname:
            ref = bpy.data.images["concept_lit"]
            W, Hh = ref.size
            sc.camera = bpy.data.objects[camname]
            sc.render.resolution_x, sc.render.resolution_y = W, Hh
            path = os.path.join(SCRATCH, tag + "-render.png")
            sc.render.filepath = path
            bpy.ops.render.render(write_still=True)
            r = bpy.data.images.load(path)
            a = np.empty(W * Hh * 4, np.float32)
            r.pixels.foreach_get(a)
            bpy.data.images.remove(r)
            b = np.empty(W * Hh * 4, np.float32)
            ref.pixels.foreach_get(b)
            a, b = a.reshape(Hh, W, 4), b.reshape(Hh, W, 4)
            sheet = np.concatenate([b, a], 0)
            o = bpy.data.images.new("ov", W, 2 * Hh)
            o.pixels.foreach_set(sheet.ravel())
            o.filepath_raw = os.path.join(SCRATCH, tag + ".png")
            o.file_format = "PNG"
            o.save()
            bpy.data.images.remove(o)
            out.append(os.path.join(SCRATCH, tag + ".png"))
        if views:
            cam = bpy.data.cameras.new("LIVE_view")
            cam.clip_start = 0.01
            co = bpy.data.objects.new("LIVE_view", cam)
            sc.collection.objects.link(co)
            sc.camera = co
            sc.render.resolution_x, sc.render.resolution_y = size
            tiles = []
            for name, eye, target, lens in views:
                e, t = Vector(eye) * 3, Vector(target) * 3
                cam.lens = lens
                co.location = e
                co.rotation_euler = (t - e).to_track_quat("-Z", "Y").to_euler()
                path = os.path.join(SCRATCH, f"{tag}-{name}.png")
                sc.render.filepath = path
                bpy.ops.render.render(write_still=True)
                r = bpy.data.images.load(path)
                a = np.empty(size[0] * size[1] * 4, np.float32)
                r.pixels.foreach_get(a)
                bpy.data.images.remove(r)
                tiles.append(a.reshape(size[1], size[0], 4))
            bpy.data.objects.remove(co)
            bpy.data.cameras.remove(cam)
            if len(tiles) % 2:
                tiles.append(np.zeros_like(tiles[0]))
            rows = [np.concatenate(tiles[i:i + 2], 1) for i in range(0, len(tiles), 2)]
            sheet = np.concatenate(rows[::-1], 0)
            o = bpy.data.images.new("sh", sheet.shape[1], sheet.shape[0])
            o.pixels.foreach_set(sheet.ravel())
            o.filepath_raw = os.path.join(SCRATCH, tag + ".png")
            o.file_format = "PNG"
            o.save()
            bpy.data.images.remove(o)
            out.append(os.path.join(SCRATCH, tag + ".png"))
    finally:
        for o in hidden:
            o.hide_render = False
        for o in made:
            light = o.data
            bpy.data.objects.remove(o)
            bpy.data.lights.remove(light)
        hull_shapes.ScreenGlow(LAST["screens"], 0.0)
        (sc.camera, sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.render.filepath, sc.world,
         sc.view_settings.view_transform, sc.view_settings.look) = keep
    return out


def Blocked(elevations=(-10, 0, 10, 20), step=0.25):
    """From the pilot's eye, to the right: at each elevation (degrees), the azimuth bands (degrees from straight ahead)
    where a ray meets the live build's faces (back faces culled, as the game draws) between the front side window and
    the pane's window: the A-pillar zone's blocked width."""
    import bmesh
    from mathutils.bvhtree import BVHTree
    live = bpy.data.collections["LIVE_Hull"]
    bm = bmesh.new()
    for o in live.objects:
        if o.type != "MESH" or not o.name.startswith(("LIVE_shell", "LIVE_side-right", "LIVE_console")):
            continue
        part = bmesh.new()
        part.from_mesh(o.data)
        me = bpy.data.meshes.new("LIVE_tmp")
        part.to_mesh(me)
        part.free()
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    eye = Vector(H.EYE)
    out = {}
    for el in elevations:
        e = math.radians(el)
        row = []
        for az in np.arange(20.0, 85.0, step):
            a = math.radians(az)
            d = Vector((math.sin(a) * math.cos(e), math.cos(a) * math.cos(e), math.sin(e)))
            row.append(hull_shapes.Visible(tree, eye, d) is not None)
        # the blocked run that contains the pillar: the longest blocked run with open sky on both sides
        runs, start = [], None
        for i, hit in enumerate(row + [False]):
            if hit and start is None:
                start = i
            if not hit and start is not None:
                runs.append((start, i))
                start = None
        inner = [(s, t) for s, t in runs if s > 0 and t < len(row)]
        if inner:
            s, t = max(inner, key=lambda r: r[1] - r[0])
            out[el] = [round(20 + s * step, 2), round(20 + t * step, 2), round((t - s) * step, 2)]
        else:
            out[el] = None
    return out
