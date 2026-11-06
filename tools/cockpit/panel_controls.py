# SPDX-License-Identifier: MIT
"""The two controls set into the Mako cockpit (research/48 section 27): the repair button and the extraction lever, Tripo
models made ready for the hull. hull_assemble.py places them (its PANELS) and exports them; on its own this previews them
in their own units:

    blender -b --factory-startup --python tools/cockpit/panel_controls.py -- <concept/assets> <out folder>

The repair button (`repair-button.glb`): a base with a flip cover standing open, a ring of nine lamps round a press
button and a "HOLD TO REPAIR" plate with eight progress squares. Its cover was lettered "FEGAIP"; panel_textures.py
paints "REPAIR" there. The ring's segments and the plate's squares are painted on; each gets a lamp face of its own just
above the paint, on a material of its own ("light-health-<i>", "light-progress-<i>"), so the game can light each one:
the package gives them a dark instance and a glowing one per colour (tools/cockpit/package/Build.cs). The cap is
split off to be pressed.

The extraction lever (`extraction-lever.glb`): a box with a red label and a red pull handle on two arms, hinged on an
axle across the recess. The handle and its arms are split off to swing about the axle the way they're pulled: down, as
the bar's "PULL" arrows point (the user, 2026-11-05: swung out of the face, up and back, it took a push); how far is
fitted so it clears the box.

Both are left as Tripo made them otherwise: low-poly (4,000 to 4,500 triangles), and their open edges (230 each) are
on faces the cabin never shows (the base's underside, the box's back inside the pod).
"""
import math
import os
import subprocess
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

REPAIR, EXTRACTION = "repair-button.glb", "extraction-lever.glb"
# The repair button, in its own units (Z up as imported), measured on 2026-11-04 (research/48 section 27):
CAP_TOP = 0.236  # the cap's top face lies above this
CAP_REACH = 0.19  # the cap's rounded edge and side, down to the groove round it, within this of its centre
GROOVE = 0.16  # the groove's floor (0.1545) and below stay with the base
HINGE_Y = 0.3  # the cover's hinge (from y 0.34) tops out level with the cap: the cap's top is on this side of it
# the ring's lamps: each painted segment's ends (degrees clockwise from +y seen from above about the cap's centre: +y
# is the cover's side), from the segment by the plate (180) on clockwise, and the inner and outer radius. The nine
# aren't evenly spaced (29 to 34 degrees wide, 9 to 12 apart); measured on dense rays straight down, the paint's
# runs at three radii, the widest kept, and RING_MARGIN added at each end
SEGMENTS = ((163.25, 197.0), (206.0, 239.25), (248.5, 278.0), (288.0, 315.5), (327.25, 355.75), (2.75, 32.0), (43.5, 72.0),
            (81.75, 111.5), (121.0, 154.5))
RING = (0.211, 0.263)
RING_MARGIN = 0.75
# the plate's progress squares: centres along x (left to right as the plate reads; an arrow follows on the right), the
# row's middle (y, z), half a square across and along the plate, the plate's normal
SQUARES = (-0.1529, -0.1170, -0.0777, -0.0418, -0.0075, 0.0268, 0.0602, 0.0970)
SQUARE_ROW = (-0.3938, 0.1612)
SQUARE_HALF = (0.0135, 0.0145)
PLATE_NORMAL = (0.0, -0.291, 0.957)
PROUD = 0.003  # the lamp faces over the paint (0.4 mm at 0.14 m a unit)
PRESS = 0.02  # the cap's travel (2.8 mm at 0.14 m a unit)
# The extraction lever: the parts that reach out of the box's face (the handle, its arms) swing; the axle is the long
# thin island across the recess; the pull is tried out to PULL_TRY degrees in steps of 1. The arms' knuckles ride on
# the axle and its brackets (they cross them as modelled, at every angle): the clearance is the arms beyond KNUCKLE from
# the axle and the handle against everything fixed. Measured 2026-11-05: down (the pull) clear to 10 degrees, the bar
# 1.1 cm down and in at the cabin's scale; up and out of the face (the swing until then) clear to 31.
REACH_OUT = -0.13
PULL_TRY = 70
KNUCKLE = 0.08
LEVER_FRONT = (-0.1047, 0.354)  # the box's face (y) and its middle's height: what sits proud of a mount's face
LIT = {"health": (0.1, 0.9, 0.25), "progress": (1.0, 0.45, 0.05)}  # the lamps' preview colours, lit (the package sets the game's)


def Import(path):
    """The file's one mesh as an object of its own, transforms applied."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == "MESH"]
    if len(meshes) != 1:
        raise SystemExit(f"{path}: expected one mesh, found {len(meshes)}")
    o = meshes[0]
    o.matrix_world = o.matrix_world.copy()
    o.parent = None
    for x in new:
        if x is not o:
            bpy.data.objects.remove(x)
    o.data.transform(o.matrix_world)
    o.matrix_world = Matrix()
    return o


def Material(name, image=None, colour=(0.5, 0.5, 0.5)):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    if image is not None:
        tex = m.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = image
        m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    else:
        bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
        m.diffuse_color = (*colour, 1.0)  # what the previews (Workbench) show
    return m


def BaseImage(o):
    return next(n.image for m in o.data.materials if m and m.use_nodes for n in m.node_tree.nodes
                if n.type == "TEX_IMAGE" and n.image and n.outputs["Color"].links and "normal" not in n.image.name.lower()
                and "_rm" not in n.image.name.lower())


def OneMaterial(o, name, image):
    """The model's own material, rebuilt plain (the game reads only the base colour)."""
    o.data.materials.clear()
    o.data.materials.append(Material(name, image))


def Islands(bm):
    """Faces by position-welded island (detach_controls.Islands' rule), largest first."""
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    key = lambda v: (round(v.co.x, 5), round(v.co.y, 5), round(v.co.z, 5))
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
    return (Vector([min(v.co[a] for v in vs) for a in range(3)]), Vector([max(v.co[a] for v in vs) for a in range(3)]))


def Split(o, name, faces):
    """The faces (indices) as an object of their own, taken off `o`. Returns the new object."""
    keep = set(faces)
    part = o.copy()
    part.data = o.data.copy()
    part.name = part.data.name = name
    bpy.context.scene.collection.objects.link(part)
    for obj, mine in ((part, True), (o, False)):
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bm.faces.ensure_lookup_table()
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if (f.index in keep) != mine], context="FACES")
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
        bm.to_mesh(obj.data)
        bm.free()
    return part


def Tris(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


def Tree(o, matrix=None):
    bm = bmesh.new()
    bm.from_mesh(o.data)
    if matrix is not None:
        bm.transform(matrix)
    t = BVHTree.FromBMesh(bm)
    bm.free()
    return t


def Repair(assets, folder):
    """The repair button in its own units: (base, cap, info). The base carries the lamps on materials of their own after
    its texture's: the ring's nine (`light-health-<i>`, clockwise from the plate's side seen from above) and the plate's
    eight squares (`light-progress-<i>`, left to right as the plate reads)."""
    o = Import(os.path.join(assets, REPAIR))
    o.name = "repair-base"
    source = BaseImage(o)
    raw = os.path.join(folder, "repair-button-source.png")
    fixed = os.path.join(folder, "repair-button.png")
    source.save(filepath=raw)
    subprocess.run(["python", "-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "panel_textures.py"), raw, fixed], check=True)
    os.remove(raw)
    image = bpy.data.images.load(fixed)
    image.scale(2048, 2048)  # the package takes it at 1024 (Build.GroupMaxTexture): the glbs needn't carry 4096 twice
    image.pack()
    tris = Tris(o)
    tree = Tree(o)
    OneMaterial(o, "repair", image)

    # the cap: its top's middle is the press's centre; its faces are within CAP_REACH of it, above the groove's floor,
    # facing up or out (the ring's inner wall across the groove faces in)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    top = [f for f in bm.faces if f.normal.z > 0.95 and CAP_TOP < f.calc_center_median().z < CAP_TOP + 0.01
           and f.calc_center_median().y < HINGE_Y]
    lo, hi = Bounds(top)
    centre = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, hi.z))
    cap = []
    for f in bm.faces:
        c = f.calc_center_median()
        radial = Vector((c.x - centre.x, c.y - centre.y, 0.0))
        if radial.length < CAP_REACH and c.z > GROOVE and (f.normal.z > 0.5 or f.normal.dot(radial.normalized()) > 0.2):
            cap.append(f.index)
    bm.free()
    button = Split(o, "repair-cap", cap)
    if Tris(o) + Tris(button) != tris:
        raise SystemExit("repair button: the parts' triangles don't add up to the model's")

    # the lamps: faces on the painted ones, PROUD above the surface under them (rays along the surface's normal), each
    # on a material of its own after the model's
    count, (r0, r1) = len(SEGMENTS), RING
    names = [f"light-health-{i}" for i in range(count)] + [f"light-progress-{i}" for i in range(len(SQUARES))]
    for name in names:
        o.data.materials.append(Material(name, colour=LIT[name.split("-")[1]]))
    bm = bmesh.new()
    bm.from_mesh(o.data)

    def Surface(p, n):
        hit = tree.ray_cast(p + n * 0.05, -n)
        if hit[0] is None:
            raise SystemExit(f"repair button: no surface under a lamp at {tuple(round(c, 3) for c in p)}")
        return hit[0] + n * PROUD

    def Quad(corners, name, facing):
        f = bm.faces.new([bm.verts.new(c) for c in corners])
        f.material_index = 1 + names.index(name)
        f.normal_update()
        if f.normal.dot(facing) < 0:
            f.normal_flip()

    up = Vector((0, 0, 1))
    for i, (a0, a1) in enumerate(SEGMENTS):
        a0, a1 = a0 - RING_MARGIN, a1 + RING_MARGIN
        ring = []
        for s in range(7):
            a = math.radians(a0 + (a1 - a0) * s / 6)
            d = Vector((math.sin(a), math.cos(a), 0.0))  # clockwise from +y seen from above
            ring.append((Surface(centre + d * r0, up), Surface(centre + d * r1, up)))
        for (p, q), (p2, q2) in zip(ring, ring[1:]):
            Quad([p, q, q2, p2], names[i], up)
    n = Vector(PLATE_NORMAL).normalized()
    along = n.cross(Vector((1, 0, 0))).normalized()
    along = along if along.y > 0 else -along  # up the plate, away from its front edge
    hu, hv = SQUARE_HALF
    for i, x in enumerate(SQUARES):
        c = Vector((x, *SQUARE_ROW))
        Quad([Surface(c + Vector((a * hu, 0, 0)) + along * b * hv, n) for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))], names[count + i], n)
    bm.to_mesh(o.data)
    bm.free()
    info = {"pivot": list(centre), "pressAxis": [0.0, 0.0, -1.0], "travel": PRESS,
            "elements": ["repair"] + names, "health": names[:count], "progress": names[count:],
            "triangles": {"base": Tris(o), "cap": Tris(button), "source": tris}}
    return o, button, info


def Extraction(assets):
    """The extraction lever in its own units: (base, handle, info). The handle swings about the axle (model +x) the way
    it's pulled, its bar down (-z, the arrows); `pullDeg` is how far before it would meet the box."""
    o = Import(os.path.join(assets, EXTRACTION))
    o.name = "extraction-base"
    OneMaterial(o, "extraction", BaseImage(o))
    tris = Tris(o)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    moving, axle = [], None
    for faces in Islands(bm):
        lo, hi = Bounds(faces)
        size = hi - lo
        if lo.y < REACH_OUT:
            moving += [f.index for f in faces]
        elif size.x > 0.6 and size.y < 0.07 and size.z < 0.07:
            axle = (lo + hi) / 2
    bm.free()
    if axle is None or not moving:
        raise SystemExit("extraction lever: no axle or no handle found")
    pivot = Vector((0.0, axle.y, axle.z))
    handle = Split(o, "extraction-handle", moving)
    if Tris(o) + Tris(handle) != tris:
        raise SystemExit("extraction lever: the parts' triangles don't add up to the model's")
    # the swing that takes the handle out of the face, and how far it clears everything fixed
    base_tree = Tree(o)
    bm = bmesh.new()
    bm.from_mesh(handle.data)
    for f in list(bm.faces):
        c = f.calc_center_median()
        if math.hypot(c.y - pivot.y, c.z - pivot.z) < KNUCKLE:
            bm.faces.remove(f)
    swinging = bm.copy()
    bm.free()

    def Swung(deg):
        return Matrix.Translation(pivot) @ Matrix.Rotation(math.radians(deg), 4, "X") @ Matrix.Translation(-pivot)

    def Clash(deg):
        b = swinging.copy()
        b.transform(Swung(deg))
        t = BVHTree.FromBMesh(b)
        b.free()
        return len(base_tree.overlap(t))

    # the grip: the middle of the handle's bar (the U's lowest 0.06, inside its bends)
    vs = [v.co for v in handle.data.vertices]
    floor = min(v.z for v in vs)
    bar = [v for v in vs if v.z < floor + 0.06 and abs(v.x) < 0.25]
    grip = sum(bar, Vector()) / len(bar)
    sign = 1 if (Swung(10) @ grip).z < grip.z else -1  # the sign that moves the bar down (its arrows: pulled)
    baseline = Clash(0)
    pull = 0
    for deg in range(1, PULL_TRY + 1):
        if Clash(sign * deg):
            break
        pull = deg
    swinging.free()
    if baseline or not pull:
        raise SystemExit(f"extraction lever: the handle clashes at rest ({baseline}) or can't be pulled ({pull} degrees)")
    info = {"pivot": list(pivot), "hingeAxis": [float(sign), 0.0, 0.0], "pullDeg": pull, "grip": list(grip),
            "front": [0.0, LEVER_FRONT[0], LEVER_FRONT[1]],
            "elements": ["extraction"], "triangles": {"base": Tris(o), "handle": Tris(handle), "source": tris}}
    return o, handle, info


def Place(o, matrix, pivot=None):
    """Into the hull: a fixed part's vertices by `matrix`; a moving part's relative to its pivot (the matrix's rotation
    and scale), the object at the pivot's place (a node translation the packager ignores)."""
    if pivot is None:
        o.data.transform(matrix)
        o.matrix_world = Matrix()
        return None
    at = matrix @ Vector(pivot)
    o.data.transform(Matrix.Translation(-Vector(pivot)))
    o.data.transform(matrix.to_3x3().to_4x4())
    o.matrix_world = Matrix.Translation(at)
    return at


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:]
    assets, out = args[0], args[1]
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    base, cap, rinfo = Repair(assets, out)
    lever, handle, linfo = Extraction(assets)
    print("RESULT repair", rinfo)
    print("RESULT extraction", linfo)
    for o in (lever, handle):
        o.location.x += 1.2
    handle.location.x += 0.0
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.color_type = "TEXTURE"
    sc.display.shading.light = "FLAT"
    sc.display.shading.show_backface_culling = True
    sc.render.resolution_x, sc.render.resolution_y = 1400, 700
    sc.world = bpy.data.worlds.new("w")
    sc.world.color = (0.2, 0.2, 0.2)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    for i, (d, target, scale) in enumerate((((0.3, -1, 1.1), (0.6, 0, 0.3), 2.4), ((0, -1, 0.15), (0.6, 0, 0.3), 2.4),
                                            ((0, 0, 1), (0.6, 0, 0.3), 2.4), ((0, -0.3, 1), (0, -0.1, 0.2), 0.9))):
        d, target = Vector(d).normalized(), Vector(target)
        cam.data.ortho_scale = scale
        cam.location = target + d * 4
        cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(out, f"panels-{i}.png")
        bpy.ops.render.render(write_still=True)
    print("RESULT previews", out)
