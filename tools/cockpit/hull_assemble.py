# SPDX-License-Identifier: MIT
"""Builds the Mako cockpit's hull: a clean shell modelled to the template's shape, and the Tripo parts placed in it
(research/48).

    blender -b --factory-startup --python tools/cockpit/hull_assemble.py -- "<concept/assets/hull reconstruction>" [--refit]

--refit only prints candidate places for the side pane (Refit), to choose PANE_SCALE and PANE_CENTRE from.

The template (`hull full -bad.glb`: the whole cockpit as one Tripo mesh) is close to the intended shape but messy:
holes for windows, stray struts, lumps. It is never shipped. The shell follows its measured envelope (the constants
below, from rays cast inside it) and the panes' own frame: a floor of chamfered plates, the roof on the panes' caps
with a central spine, conduits and lamps as the lit concept's, a frame line at the panes' top rail along the side walls
to the A-pillars' tops, where the front's top steps up to the ceiling (the windshield's top edge is the ceiling's front
edge, a corner plate over each front side window), open windows in chamfered frames with mitred corners, a portal arch
at the open rear. Its faces are classed (plate, dark, light, red, seam, metal) with
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
import subprocess
import sys
import tempfile

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform, intersect_ray_tri

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hull_shapes import (GLOW, QUAD, Bake, Builder, Chamfered, Clip, Corners, Cross, Display, Grille, Inset, Keypad, Local, Newell,  # noqa: E402
                         Offset, Pipe, Plate, Png, RETICLE, Relight, Rings, ScreenGlow, Slab, Sweep, Visible)

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
# Then grown toward the lit concept's (the user, 2026-11-02: "you can attempt to scale parts of the console asset"):
# around the same console the concept's room is smaller (research/48 section 17). Across, the console already meets the
# walls, and grown upward it would hide the windshield's sill from the seat, so it grows toward the seat and in height
# about a point on the windshield's wall (x, y, z factors; the point's height): its top stays on the sill's sightline,
# its foot sinks 5 cm under the floor plates and its wings reach 0.08 nearer the seat, clear of the controls. (1.3 deep
# wrapped the room most but stretched the angled side screens past the concept's console shape.)
CONSOLE_GROW, CONSOLE_PIVOT_Z = (1.03, 1.2, 1.3), 0.25
# And stood 0.1 nearer the seat before growing (the user, 2026-11-02, on the cabin's proportions; research/48 section 18):
# measured from the camera matched to the room, the concept's console stands about 0.2 further from the windshield and
# higher, free in the room. Its wings then pass beside the controls, and its top stays on the sill's sightline.
CONSOLE_SHIFT = -0.1
# The template's shell, measured by rays from inside it on 2026-11-02 (research/48): x across, y forward, z up.
W, ZW = 0.463, 0.412       # the side walls' inner face; the height where their chamfer into the ceiling starts
XC, ZC = 0.39, 0.48        # the chamfer's top edge; the ceiling
YR = -0.38                 # the portal arch: its front face flush with the panes' rear ends (-0.309), so the panes'
                           # rear posts and the arch read as one frame (the user, 2026-11-02, on the side walls; it
                           # stood at the template's open rear, -0.425)
YB = -0.62                 # the rear wall, closing the cabin 0.59 m behind the arch at 3 m per unit (the user, 2026-11-02:
                           # the contours carried on to a rear section, as Codex's assembly did)
DOOR = (0.14, 0.48, 0.05)  # the rear wall's door: half width, height, its top corners' cut (0.84 x 1.44 m)
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
# The rear door's frame, as WINDOW but heavier: a 30 mm light face standing 15 mm proud, lining the wall's thickness.
DOOR_FRAME = [(-0.006, -0.03), (0.03, -0.03), (0.03, 0.0), (0.024, 0.015), (0.0, 0.015), (-0.006, 0.009), (-0.006, -0.03)]
DOOR_FRAME_CLASSES = ["DARK", "SEAM", "LIGHT", "LIGHT", "LIGHT", "DARK"]
# The A-pillars (the user, 2026-11-02: the side pane's own pillar ended in the air, 15 cm above the console). Ours is
# the front side window's rear edge, in the window's plane (through the windshield's side edge and the window's top
# rear corner, WindowPlane): a beam from the cap down to a knee, then flatter to its foot on the sill, where a shoulder
# on the console's outer end carries it to the floor. KNEE: the front edge's knee (y, z); FOOT_Y: its foot on the sill.
# The knee and the foot sit 25 mm further back than the first pillar's (the user, 2026-11-02: the pillars "would
# obscure a lot"), the beam narrower, so the front side window grows by the difference.
KNEE, FOOT_Y = (0.0906, 0.4034), 0.2242
# The beam's profile: (side, toward the room) from its front edge on the window's plane, the side negative toward its
# rear: 40 mm wide (it was 65), chamfered, a 12 mm channel 3 mm deep along its face. The channel is red in two lengths
# above the knee, as the concept's stripes, and a plate below it. It is 37 mm deep, 24 of them behind the plane, so its
# room face stands 13 mm into the room: near the sill the plane passes 2 to 3 cm outside the console's right screen
# housing, and a beam standing 36 mm in sat on the housing's rim (the user, 2026-11-02: "the base of the pillar looks to
# collide with the control desk").
PILLAR = [(0.0, -0.024), (0.0, 0.006), (-0.006, 0.013), (-0.012, 0.013), (-0.014, 0.01), (-0.026, 0.01), (-0.028, 0.013),
          (-0.034, 0.013), (-0.04, 0.006), (-0.04, -0.024), (0.0, -0.024)]
PILLAR_CLASSES = ["DARK", "LIGHT", "LIGHT", "SEAM", "RED", "SEAM", "LIGHT", "LIGHT", "DARK", "DARK"]
CHANNEL = 4  # the channel's floor among the profile's edges
# The bands round the beam where it meets the cap, at the knee and on the shoulder: 6 mm proud of it all round.
COLLAR = Chamfered(-0.046, 0.006, -0.024, 0.019, 0.004)
COLLAR_CLASSES = ["DARK", "DARK", "LIGHT", "LIGHT", "LIGHT", "DARK", "DARK", "DARK"]
# The web behind the beam: its face this far behind the window's plane (the beam stands 34 mm proud of it).
WEB = -0.018
# The foot (the user, 2026-11-02: a slanted sleeve where the beam met the shoulder "looks like it has another
# collision"): the beam cut level and standing on a base plate this thick, bolted to the shoulder, two gussets this long
# under it.
FOOT_PLATE, GUSSET = 0.005, 0.04
# The shoulder under the foot, on the console's outer flat top: its rear and front y, and how far its room face stands in
# from the window's sill (in x), clear of the right screen housing's outer step (x 0.26 near the foot).
SHOULDER = (0.12, 0.275, 0.02)
# The panes lose their own pillar: everything in front of PANE_CUT above their side console's top (PANE_CONSOLE), and
# the pillar's foot below that, where it stands in front of the web, over the shoulder (CutPillar).
PANE_CUT, PANE_CONSOLE = 0.024, 0.346
# The old foot's rear end, behind the shoulder: from this y to the shoulder, inboard of this x (the side console's
# inner face, which stays).
FOOT_CUT = (0.07, 0.357)
# The ceiling, as the lit concept's (the user, 2026-11-02: "do your best to match the concept"): a spine down the middle,
# conduits either side of it, lamps in the corners. The spine's profiles (side, down from ZR negative): its upper tier,
# and its lower tier with a recess underneath. The spine and the conduits end SPINE_CLEAR behind the windshield's top
# edge, clear of its frame; the spine's front carries a drop panel (DROP: its length, how much deeper than the spine,
# the length of its sloped rear face), a lit face toward the pilot: from the seat only faces turned down or back show,
# and the eye sits inside the spine's width, so its sides never do.
# The lower tier hangs 60 mm (it hung 88: 19 cm above the pilot's eye, and it filled the top of the view), its recess
# floor in bolted panels SPINE_PANEL long (the concept's).
SPINE_UPPER = Chamfered(-0.12, 0.12, -0.028, 0.004, 0.006)
SPINE_DEPTH = 0.06
SPINE = [(0.085, -0.028), (-0.085, -0.028), (-0.085, -0.048), (-0.073, -0.06), (-0.05, -0.06), (-0.045, -0.054),
         (0.045, -0.054), (0.05, -0.06), (0.073, -0.06), (0.085, -0.048), (0.085, -0.028)]
SPINE_CLASSES = ["DARK", "DARK", "LIGHT", "DARK", "SEAM", "PLATE", "SEAM", "DARK", "LIGHT", "DARK"]
SPINE_PANEL = 0.15
SPINE_CLEAR = 0.03
DROP = (0.1, 0.014, 0.035)
# Three pipes either side of the spine (x of each, their radius), hung under the ceiling on straps STRAP apart.
CONDUITS, CONDUIT_R, STRAP = (0.148, 0.174, 0.2), 0.0095, 0.15
# The ceiling lamps' housings over the cabin: wedges under the ceiling between the conduits and the caps' lip, their
# sloped face (and lamp) turned to the room's middle: (inner x on the ceiling, outer x, drop at the outer face).
FIXTURE = (0.222, 0.283, 0.034)
# The windshield's frame: as WINDOW, but dark all round, the concept's slim posts between the windshield and the front
# side windows (with a light chamfer along the glass, the posts read from the seat as light struts to the ceiling's
# corners).
SHIELD_CLASSES = ["DARK", "SEAM", "SEAM", "DARK", "DARK", "DARK", "DARK", "DARK"]
# The floor as the lit concept's (the user, 2026-11-02): large flat plates in three runs, the middle one as wide as the
# console's middle and the side runs' rows half a row out of step, the seams 3 mm grooves (FLOOR_SEAM either side), the
# plates' corners cut by FLOOR_CORNER where they meet, their top edges bevelled FLOOR_BEVEL. (They were 0.15 by 0.2
# tiles with 8 mm seams, 20 mm corners and a 5 mm bevel: chunky beside the concept's.)
FLOOR_COLUMNS, FLOOR_ROW = (-0.16, 0.16), 0.166
FLOOR_SEAM, FLOOR_CORNER, FLOOR_BEVEL = 0.0015, 0.012, 0.002
FLOOR_STRIPS = (-0.275, -0.222)  # y of the pale strips along the panes' foot in front of the portal, as the concept's
# The portal arch, bolder, as the concept's frames down the side walls: (into the room, along the cabin) from its line,
# 65 mm proud and 70 deep, chamfered, a dark groove down its face.
PORTAL = [(0.0, 0.0), (0.052, 0.0), (0.065, 0.013), (0.065, 0.031), (0.061, 0.031), (0.061, 0.039), (0.065, 0.039),
          (0.065, 0.057), (0.052, 0.07), (0.0, 0.07), (0.0, 0.0)]
PORTAL_CLASSES = ["DARK", "LIGHT", "LIGHT", "SEAM", "SEAM", "SEAM", "LIGHT", "LIGHT", "DARK", "DARK"]
PORTAL_DEPTH = 0.07
# A light stringer along each wall-ceiling corner, as the concept's pale corner beams: on the caps' sloped face, from the
# portal to the A-pillar's top. (along the slope, out of it) from the slope's middle: 60 mm wide, 22 mm proud.
STRINGER = Chamfered(-0.03, 0.03, -0.004, 0.022, 0.005)
STRINGER_CLASSES = ["DARK", "DARK", "LIGHT", "LIGHT", "LIGHT", "DARK", "DARK", "DARK"]


# Rectangles (u0, v0, u1, v1) of the side pane's 4096 x 4096 atlas, by surface class, and each class's tint in the bake.
PLATE = (2075 / 4096, 2952 / 4096, 2392 / 4096, 3460 / 4096)  # a plate with four bolts and a bevelled edge, 317 x 508 px
# LIGHT and DARK: plain square patches of painted metal (320 and 224 px, no panel edge in them, the mean colours of the
# strips they replace), since the bake repeats them across each face (hull_shapes.TILED): the old 32-px strips, repeated,
# streaked a member every 3 cm (research/48 section 24).
LIGHT = (1344 / 4096, 1 - 800 / 4096, 1664 / 4096, 1 - 480 / 4096)
RED = (0.7109375, 0.296875, 0.71875, 0.3359375)
DARK = (3424 / 4096, 1 - 3424 / 4096, 3648 / 4096, 1 - 3200 / 4096)
TRIMS = {"PLATE": PLATE, "DARK": DARK, "LIGHT": LIGHT, "RED": RED, "SEAM": DARK, "LAMP": LIGHT, "LAMP_ORANGE": LIGHT, "LAMP_BLUE": LIGHT,
         "METAL": LIGHT, "DECAL": (0.0, 0.0, 1.0, 1.0)}  # DECAL: the decal atlas's own UVs (Decal)
# The lit concept's lamps (mako-interior-plain-front-2.png), each a glowing panel in the shell and an area light for
# the bake, by kind: its class and power (W, in hull units). The world adds a dim warm fill.
LAMP_POWER = {"ceiling": 1.2, "midCeiling": 0.8, "rearCeiling": 0.8, "wall": 0.35, "floor": 0.15, "door": 0.4, "foot": 0.05, "spine": 0.0}
FILL = ((0.05, 0.044, 0.04), 0.6)  # the world's colour and strength: what the room's dark corners keep
SCREEN_GLOW = 5.0  # the console's screens' emission while baking: their orange on the desk and the frames
AMBIENT = 0.32  # what every baked surface keeps without a lamp facing it (times its albedo): the concept's dark ceiling still reads
# The shell's baked texture pages (4096 each), the nearer faces on more texels (hull_shapes.Bake): on one page the shell had
# about 330 texels a metre where the headset resolves about 1,000 at arm's length (research/48 section 24).
SHELL_PAGES = 3
TINTS = {"PLATE": 1.3, "DARK": 1.5, "LIGHT": 2.2, "RED": 0.55, "SEAM": 0.6, "METAL": 1.1}  # METAL: the conduits, mid grey
SLAB = 0.02  # the shell's thickness
SEAM = 0.012  # how deep the floor's seams go: the base under the plates, where the walls start
# The scale and the furniture (proposed 2026-11-02, the user to confirm the scale): 3 m per hull unit makes the cabin 2.5 m
# wide between the panes, the ceiling 1.66 m high and the console's ledge 0.9 m. The chair, the throttle and the stick are
# the Mako driver's seat with a HOTAS (the user, 2026-11-02: the seat "where it would make sense if sitting in the mako
# vehicle along with the hotas"; Shepard sits in it in VR, so the hands meet the real models): the chair keeps Codex's
# place and size (concept/assets/cockpit/finish_20261031_v001), in the middle, its front clear of the console and the legs
# room under it; the controls on its armrests' front sections, where a forearm on the armrest brings the hand (they stood
# 0.58 m out, beyond the armrests, at the end of an outstretched arm), each dropped onto the armrest's top (Furnish). Offsets
# in metres from the chair's origin, each turned about z as Codex turned it (research/48 section 22).
METRES_PER_UNIT = 3.0
CHAIR_AT = (0.0, -0.19, 0.0)
FURNITURE = (  # name, game_ready.py's files, metres per model unit, offset from the chair's origin (metres), turn about z
    ("chair", ("chair",), 1.2206, (0.0, 0.0, 0.0), 90.0),
    ("throttle", ("throtle-control-base", "throtle-control-handle"), 0.29, (-0.363, 0.27, 0.645), -90.0),
    ("joystick", ("joystick-base", "joystick-grip"), 0.29, (0.375, 0.27, 0.645), -90.0),
)
ON_ARMREST = 0.002  # a control's base above the armrest's highest point under it
# The seated pilot's eye, from the chair's origin: a seated adult with the back against this chair's reclined backrest (the
# cushion's top 0.48 m, the eye 0.73 m above it; 0.17 m in front of the headrest, the back of a head resting on it). Codex's
# anchor (0, -0.13, 1.21) put the eye 7 to 11 cm from the headrest: the head inside the backrest, and in the game, looking
# over a shoulder, the near plane cut into it (the game side, 2026-11-02).
EYE_FROM_CHAIR = (0.0, -0.03, 1.21)
# the pilot's eye: the previews and the enclosure rays look from it
EYE = tuple(round(c + e / METRES_PER_UNIT, 4) for c, e in zip(CHAIR_AT, EYE_FROM_CHAIR))
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


def Placements(pbm, cbm):
    """The left pane's, the right pane's (its mirror) and the console's matrices, from their files' meshes: a quarter
    turn for the pane, each scaled and centred as fitted, stood on the floor; the console then grown (CONSOLE_GROW)."""
    turn = Matrix.Rotation(math.radians(90), 4, "Z")
    co = np.array([(turn @ v.co)[:] for v in pbm.verts])
    centre = Vector(((co.min(0) + co.max(0)) / 2).tolist())
    left = Matrix.Translation(Vector(PANE_CENTRE)) @ Matrix.Scale(PANE_SCALE, 4) @ Matrix.Translation(-centre) @ turn
    floor = min((left @ v.co).z for v in pbm.verts)
    left = Matrix.Translation((0, 0, -floor)) @ left
    right = Matrix.Diagonal((-1, 1, 1, 1)) @ left
    cco = np.array([v.co[:] for v in cbm.verts])
    ccentre = Vector(((cco.min(0) + cco.max(0)) / 2).tolist())
    console = Matrix.Translation(Vector(CONSOLE_CENTRE) + Vector((0, CONSOLE_SHIFT, 0))) @ Matrix.Scale(CONSOLE_SCALE, 4) @ Matrix.Translation(-ccentre)
    floor = min((console @ v.co).z for v in cbm.verts)
    pivot = Vector((0.0, YF, CONSOLE_PIVOT_Z))
    grow = Matrix.Translation(pivot) @ Matrix.Diagonal((*CONSOLE_GROW, 1.0)) @ Matrix.Translation(-pivot)
    return left, right, grow @ Matrix.Translation((0, 0, -floor)) @ console


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


def WindowPlane(pillar):
    """The right front side window's plane: through the windshield's side edge (B on the sill, C on the frame line) and
    the window's top rear corner D, where the cap turns from along the pane toward the windshield (PillarEdge's top).
    Returns B, C, D, the plane's normal toward the room and the plane's x at (y, z)."""
    yh = YF - (ZH - ZL) * (YF - YT) / (ZC - ZL)
    B = Vector((XF, YF, ZL))
    C = Vector((XP - (yh - YC) * (XP - XF) / (YF - YC), yh, ZH))
    D = Vector((XRAIL, pillar[1].y - 0.015, ZH))
    n = (C - B).cross(D - C).normalized()
    n = -n if n.x > 0 else n
    return B, C, D, n, lambda y, z: B.x - (n.y * (y - B.y) + n.z * (z - B.z)) / n.x


def ShieldTop():
    """The right top corner of the windshield, on the ceiling (the user, 2026-11-02: the front's top rises to the
    ceiling as the concept's, the frame line stepping up at the A-pillars): its side edge, on the front chamfer and
    leaning back as the template's, carried up to ZR. It lies on the front side window's plane too (WindowPlane's B and
    C are on the same edge)."""
    yt = YF - (ZR - ZL) * (YF - YT) / (ZC - ZL)
    return Vector((XP - (yt - YC) * (XP - XF) / (YF - YC), yt, ZR))


def CutPillar(pane, plane, mirrored=False):
    """Removes the pane's own A-pillar with the web under it and its foot (the shell's Pillar takes their place): every
    face in front of PANE_CUT above PANE_CONSOLE; below that from z 0.26 up, in front of the shoulder's rear end where it
    stands in front of the web, and behind it (from FOOT_CUT's y) inboard of the side console's inner face (`plane`:
    WindowPlane, for the right pane; `mirrored`: the left). The faces near the cut are split on its planes first, so what
    is left ends on them; the Tripo mesh is open, so an exact boolean is no use."""
    B, _, _, n, X = plane
    sign = -1 if mirrored else 1
    bm = bmesh.new()
    bm.from_mesh(pane.data)
    planes = [((0, PANE_CUT, 0), (0, 1, 0)), ((0, 0, PANE_CONSOLE), (0, 0, 1)), ((0, 0, 0.26), (0, 0, 1)), ((0, SHOULDER[0], 0), (0, 1, 0)),
              ((sign * (B.x + WEB * n.x + 0.01), B.y, B.z), (sign * n.x, n.y, n.z)), ((0, FOOT_CUT[0], 0), (0, 1, 0)),
              ((sign * FOOT_CUT[1], 0, 0), (1, 0, 0))]
    for co, no in planes:
        faces = [f for f in bm.faces if max(v.co.y for v in f.verts) > PANE_CUT - 0.005 and max(v.co.z for v in f.verts) > 0.25]
        geom = list({v for f in faces for v in f.verts}) + list({e for f in faces for e in f.edges}) + faces
        bmesh.ops.bisect_plane(bm, geom=geom, plane_co=co, plane_no=no, dist=1e-6)
    gone = []
    for f in bm.faces:
        c = f.calc_center_median()
        if c.y > PANE_CUT and c.z > PANE_CONSOLE:
            gone.append(f)
        elif c.y > SHOULDER[0] and 0.26 < c.z <= PANE_CONSOLE and sign * c.x < X(c.y, c.z) + WEB * n.x + 0.01:
            gone.append(f)
        elif c.y > FOOT_CUT[0] and 0.26 < c.z <= PANE_CONSOLE and sign * c.x < FOOT_CUT[1]:
            gone.append(f)
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    bm.to_mesh(pane.data)
    bm.free()
    Smooth(pane)
    return len(gone)


def Turned(plane, path):
    """Whether a path down the pillar must run bottom up for Sweep's side to point toward the window (PILLAR's
    convention: the profile's side is negative toward the beam's rear)."""
    _, C, D, n, _ = plane
    return n.cross(Vector(path[1]) - Vector(path[0])).dot(C - D) < 0


def Beam(b, plane, path, profile, classes, ends=(None, None)):
    """A profile swept down the pillar (`path` from its top), its side toward the window; `classes` and `ends` as
    Sweep's, per segment and end from the top. Returns the rings, top first."""
    if not Turned(plane, path):
        return Sweep(b, path, plane[3], profile, classes, ends=ends)
    per_segment = isinstance(classes, list) and isinstance(classes[0], list)
    return Sweep(b, path[::-1], plane[3], profile, classes[::-1] if per_segment else classes, ends=ends[::-1])[::-1]


def Keep(polygon, a, c, inside):
    """A 2D polygon cut by the line through a and c, keeping the side the point `inside` is on."""
    side = lambda p: (c[0] - a[0]) * (p[1] - a[1]) - (c[1] - a[1]) * (p[0] - a[0])
    sign = 1 if side(inside) > 0 else -1
    out = []
    for p, q in zip(polygon, polygon[1:] + polygon[:1]):
        if sign * side(p) >= 0:
            out.append(tuple(p))
        if side(p) * side(q) < 0:
            out.append(Cross(p, q, a, c))
    return out


def Shoulder(b, outline, top, bevel=0.006):
    """A block from the floor's base up to `top` over a convex outline ((x, y), counter-clockwise): its top's edge
    bevelled light; each side long enough a panel set in, the others dark."""
    up = Vector((0, 0, 1))
    rim = [Vector((x, y, top - bevel)) for x, y in outline]
    face = [p + up * bevel for p in Offset(rim, up, bevel)]
    b.Face(face, "PLATE", toward=up)
    centre = sum(rim, Vector()) / len(rim)
    for k in range(len(rim)):
        q = (k + 1) % len(rim)
        b.Face([rim[k], rim[q], face[q], face[k]], "LIGHT", toward=up, uvs=QUAD)
        side = [rim[k], rim[q], Vector((rim[q].x, rim[q].y, -SEAM)), Vector((rim[k].x, rim[k].y, -SEAM))]
        out = (rim[k] + rim[q]) / 2 - centre
        out.z = 0.0
        if (rim[q] - rim[k]).length > 0.05:
            Inset(b, side, out, 0.008, 0.005, 0.012, surface="DARK", ring="DARK", face="PLATE")
        else:
            b.Face(side, "DARK", toward=out, uvs=QUAD)


def Foot(b, plane, foot, down):
    """Where the beam (cut level at the sill, its front edge's foot at `foot`, running `down`) stands on the shoulder: a
    base plate round its footprint, its top on the sill's level, bolted at the corners; two gussets in the acute angle
    under the beam, near its room face and its back."""
    n = plane[3]
    Z = Vector((0, 0, 1))
    w = n.cross(down).normalized()
    w = w if w.z > 0 else -w  # in the window's plane, square to the beam, toward its front edge
    h = Vector((down.x, down.y, 0)).normalized()  # the beam's way across the floor
    c = Z.cross(h)
    width = -min(s for s, _ in PILLAR)
    u0, u1 = min(u for _, u in PILLAR), max(u for _, u in PILLAR)
    Cut = lambda p: p + down * ((ZL - p.z) / down.z)  # along the beam onto the sill's level
    footprint = [Cut(foot + w * s + n * u) for s in (0.0, -width) for u in (u0, u1)]
    room = 1 if c.dot(n) > 0 else -1  # which way along c the room is
    for u, out in ((u1 - 0.004, room), (u0 + 0.004, -room)):
        p0 = Cut(foot - w * width + n * u)
        p1 = p0 - down * GUSSET
        p2 = Vector((p1.x, p1.y, ZL))
        Slab(b, [p0, p1, p2], c * out, 0.005, "LIGHT", "DARK")
        footprint.append(p2)
    along, across = [p.dot(h) for p in footprint], [p.dot(c) for p in footprint]
    a0, a1, c0, c1 = min(along) - 0.008, max(along) + 0.008, min(across) - 0.006, max(across) + 0.006
    corners = [h * a + c * k for a, k in ((a0, c0), (a1, c0), (a1, c1), (a0, c1))]
    centre = sum(corners, Vector()) / 4
    corners.sort(key=lambda p: math.atan2(p.y - centre.y, p.x - centre.x))
    Plate(b, [(p.x, p.y) for p in corners], ZL, 0.0015, FOOT_PLATE + 0.003, top="DARK", edge="LIGHT")
    b.Face([p + Z * (ZL - FOOT_PLATE - 0.003) for p in corners], "DARK", toward=-Z)  # its underside, where it overhangs
    for p in corners:
        q = p + (centre - p).normalized() * 0.008 + Z * ZL
        Pipe(b, [q, q + Z * 0.0025], 0.003, Vector((1, 0, 0)), "LIGHT")


def Pillar(b, plane, top):
    """The right A-pillar (b's mirror makes the left): the beam along its front edge from the cap through the knee to
    the sill, a collar where it meets the cap, a band round the knee, and its foot cut level on a base plate (Foot); the
    web behind it, parallel to the window's plane from the pane's cut to the beam, standing on the pane's side console
    and on the shoulder; the shoulder, from the floor to the sill under the foot, on the console's outer end;
    the window's frame along its sill and top (the beam frames its rear edge, the windshield's frame its front).
    Returns the window's opening, a convex polygon in its plane: the windshield's corner on the sill, the foot, the
    knee, the pillar's top, the windshield's top corner on the ceiling (`top`, ShieldTop): its top edge rises from
    the pillar's top along the corner plate over it (Shell)."""
    B, _, D, n, X = plane
    C = top
    At = lambda y, z: Vector((X(y, z), y, z))
    knee, foot = At(*KNEE), At(FOOT_Y, ZL)
    up, down = (D - knee).normalized(), (foot - knee).normalized()
    # the beam, from 16 mm up inside the cap (the collar's end, 4 mm further, caps it) down to the sill, cut level there
    # to stand on its base plate; its channel red down to the knee in two lengths, a plate below
    gap = (D + knee) / 2
    path = [D + up * 0.016, D, gap + up * 0.008, gap - up * 0.008, knee, foot + down * 0.03]
    channel = {c: PILLAR_CLASSES[:CHANNEL] + [c] + PILLAR_CLASSES[CHANNEL + 1:] for c in ("RED", "SEAM", "PLATE")}
    Beam(b, plane, path, PILLAR, [channel[c] for c in ("RED", "RED", "SEAM", "RED", "PLATE")], ends=(None, ((0, 0, ZL), (0, 0, 1))))
    Beam(b, plane, [D + up * 0.02, D - up * 0.03], COLLAR, COLLAR_CLASSES)
    Beam(b, plane, [knee + up * 0.012, knee, knee + down * 0.012], [(s, u - 0.003) for s, u in COLLAR], COLLAR_CLASSES)
    Foot(b, plane, foot, down)
    # the web, WEB behind the window's plane and 12 mm in under the beam, split into convex panels by the knee and where
    # the old foot was cut (FOOT_CUT): behind that it stands on the pane's side console, in front of it it comes down to
    # the sill behind the side console's inner face and onto the shoulder. (A quarter window there instead opened a 1
    # degree slit at eye level: its frame filled the rest, and the pane's own frame behind it blocks 11 degrees.)
    turned = Turned(plane, path)
    _, rings = Rings(path[::-1] if turned else path, n, [(min(s for s, _ in PILLAR) + 0.012, 0.0)])
    line = [r[0].yz for r in (rings[::-1] if turned else rings)]
    head, bend, low = line[1], line[4], line[5]
    rear, y0 = FOOT_CUT[0], PANE_CUT - 0.004
    for rect, (a, c) in (([(y0, bend[1]), (1.0, bend[1]), (1.0, ZH + 0.004), (y0, ZH + 0.004)], (head, bend)),
                         ([(y0, PANE_CONSOLE), (rear, PANE_CONSOLE), (rear, bend[1]), (y0, bend[1])], (bend, low)),
                         ([(rear, ZL - FOOT_PLATE), (1.0, ZL - FOOT_PLATE), (1.0, bend[1]), (rear, bend[1])], (bend, low))):
        panel = Keep(rect, a, c, (y0, ZL))
        if len(panel) >= 3 and Area(panel) > 2e-4:
            Slab(b, [At(y, z) + n * WEB for y, z in panel], n, SLAB, inset=(0.006, 0.004, 0.006) if Area(panel) > 0.0015 else None)
    # the shoulder: its room face SHOULDER[2] in from the sill, out 4 mm into the side wall and the front chamfer
    y0, y1, inward = SHOULDER
    chamfer = Vector((XP + 0.004 - (y1 - YC) * (XP - XF) / (YF - YC), y1, ZL))
    Shoulder(b, [(X(y0, ZL) - inward, y0), (XP + 0.004, y0), (XP + 0.004, YC), chamfer.xy, (X(y1, ZL) - inward, y1)], ZL - FOOT_PLATE)
    # in front of it, the sill outside the glass: from the window's plane out to the front chamfer
    b.Face([At(y1, ZL), chamfer, B], "PLATE", toward=Vector((0, 0, 1)))
    # an end plate over the pane's post and rail where CutPillar cut them, facing forward (the Tripo mesh is open there,
    # so an eye in front of the pillar looked into it), from inside the post's room face out to the web, under the cap
    y = PANE_CUT + 0.001
    out = lambda z: X(y, z) + WEB * n.x + 0.005
    b.Face([(0.35, y, PANE_CONSOLE), (out(PANE_CONSOLE), y, PANE_CONSOLE), (out(ZH + 0.006), y, ZH + 0.006), (0.35, y, ZH + 0.006)], "DARK",
           toward=Vector((0, 1, 0)))
    # the window's frame along the sill and the top, each run 12 mm on into the beam
    window = [B, foot, knee, D, C]
    on = lambda p, q: p + (p - q).normalized() * 0.012
    runs = [[B, on(foot, B)], [on(D, C), C]]
    for run in runs if Inward(window, n)[1] == foot else [r[::-1] for r in runs]:
        Sweep(b, run, n, WINDOW, WINDOW_CLASSES)
    return window


# The console's screens, by where each one's middle lies in x: the centre one shows the live third-person view (the
# user's choice, 2026-11-02), the right one the gun's aiming picture (the user, 2026-11-02: "the right display could also
# show the current aiming picture"), each a material of its own for the mod's capture; the left an idle screen in the
# game's orange interface style. Each image is drawn for its screen's shape; the aiming screen's is an idle sight.
SCREENS = (("centre", (-0.1, 0.1), "Screen"), ("left", (-0.5, -0.15), "Display"), ("right", (0.15, 0.5), "Aim"))
SCREEN_LOOK = {"Screen": {"bars": ("top", "bottom")}, "Display": {"bars": ("bottom",)}, "Aim": {"bars": (), "reticle": True}}
SCREEN_PIXELS = (1024, 512)  # each idle screen's image; the design is drawn for each screen's own shape
SCREEN_PROUD, SCREEN_TUCK = 0.0015, 0.0015  # each screen's panel: in front of the model's screen, under the bezel
# The openings, found face-on (Opening): the raster's step (hull units); the trim's paint, at or above this luminance
# as stored (the screens about 0.18, the bands painted round them about 0.1, the trim 0.25 to 0.55); and how far in
# front of the screen's plane a dark face still counts as the screen's (dark paint runs over the raised trim's top at
# the centre screen's corners, out to the console's dark surroundings).
SCREEN_STEP, SCREEN_TRIM, SCREEN_RAISED = 0.0005, 0.225, 0.006


def Opening(tree, texel, centre, n, h, w, name):
    """A screen's opening as the pilot takes it: rays straight at it (along -n, back faces culled) read the console's own
    texture where they land; the opening is the cells darker than the trim's paint and not raised from the screen's
    plane, joined to the middle. Returns its convex outline in (h, w) from `centre`, counter-clockwise, the same with
    the raster's steps smoothed out (for the image's frames), and its number of cells."""
    samples = {}

    def Open(i, j):
        if (i, j) not in samples:
            hit = Visible(tree, centre + (h * i + w * j) * SCREEN_STEP + n * 0.05, -n)
            samples[(i, j)] = hit is not None and texel(hit) < SCREEN_TRIM and (hit[0] - centre).dot(n) < SCREEN_RAISED
        return samples[(i, j)]
    start = next(((i, j) for r in range(40) for i in range(-r, r + 1) for j in range(-r, r + 1) if max(abs(i), abs(j)) == r and Open(i, j)), None)
    if start is None:
        raise RuntimeError(f"the {name} screen: no screen texel near its middle")
    cells, stack = set(), [start]
    while stack:
        i, j = stack.pop()
        if (i, j) in cells or not Open(i, j):
            continue
        if abs(i) * SCREEN_STEP > 0.25 or abs(j) * SCREEN_STEP > 0.12:
            raise RuntimeError(f"the {name} screen's opening runs out past its trim")
        cells.add((i, j))
        stack += [(i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)]
    rim = [(i, j) for i, j in cells if not all(k in cells for k in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)))]
    outline = Hull2([((i + di) * SCREEN_STEP, (j + dj) * SCREEN_STEP) for i, j in rim for di in (-0.5, 0.5) for dj in (-0.5, 0.5)])
    smooth = list(outline)
    while len(smooth) > 3:  # drop the corners the raster's steps make: those within 3 steps of the line past them
        far = [abs((cx - ax) * (by - ay) - (cy - ay) * (bx - ax)) / max(math.hypot(cx - ax, cy - ay), 1e-12)
               for (ax, ay), (bx, by), (cx, cy) in zip(smooth[-1:] + smooth[:-1], smooth, smooth[1:] + smooth[:1])]
        k = min(range(len(smooth)), key=far.__getitem__)
        if far[k] >= 3 * SCREEN_STEP:
            break
        del smooth[k]
    return outline, smooth, len(cells)


def Screens(console, folder):
    """The console's screens as panels of their own: "Screen" for the live view, "Display" for the idle ones, each with
    an image of its idle state whose frames follow the screen's opening. A screen is the largest dark flat patch facing
    the seat in its region; its opening is what its trim leaves of it face-on (Opening); its panel is the rectangle round
    the opening along the screen's own long edge, just in front of the model's screen and its edges under the trim, u to
    the pilot's right and v up (the glTF export turns v downward, as a render target's rows run). Each screen's UVs span
    its material's image over the rectangle round its panel (a material shared by several screens would stack their
    images, each on its own rows). Returns each screen's rectangle in hull coordinates."""
    me = console.data
    atlas = next(n.image for m in me.materials for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image)
    tw, th = atlas.size
    px = np.empty(tw * th * 4, np.float32)
    atlas.pixels.foreach_get(px)
    px = px.reshape(th, tw, 4)
    lum = 0.2126 * px[..., 0] + 0.7152 * px[..., 1] + 0.0722 * px[..., 2]
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv.active
    tris = bm.calc_loop_triangles()
    tree = BVHTree.FromPolygons([v.co.copy() for v in bm.verts], [[l.vert.index for l in t] for t in tris])
    eye = Vector(EYE)
    facing = {f for f in bm.faces if f.normal.dot((eye - f.calc_center_median()).normalized()) > 0.3}

    def Lum(f):
        u = sum(l[uv].uv.x for l in f.loops) / len(f.loops)
        v = sum(l[uv].uv.y for l in f.loops) / len(f.loops)
        return float(lum[int((v % 1) * (th - 1)), int((u % 1) * (tw - 1))])

    def Texel(hit):
        t = tris[hit[2]]
        q = barycentric_transform(hit[0], *(l.vert.co for l in t), *(l[uv].uv.to_3d() for l in t))
        return float(lum[int((q.y % 1) * (th - 1)), int((q.x % 1) * (tw - 1))])

    patches, seen = [], set()
    for f in facing:
        if f in seen:
            continue
        stack, patch = [f], []
        seen.add(f)
        while stack:
            g = stack.pop()
            patch.append(g)
            for e in g.edges:
                for k in e.link_faces:
                    if k in facing and k not in seen and k.normal.dot(f.normal) > 0.996:
                        seen.add(k)
                        stack.append(k)
        area = sum(g.calc_area() for g in patch)
        centre = sum((g.calc_center_median() * g.calc_area() for g in patch), Vector()) / area
        patches.append((area, centre, patch, sum(Lum(g) * g.calc_area() for g in patch) / area))
    Z = Vector((0, 0, 1))
    fits = []
    for name, (x0, x1), material in SCREENS:
        area, centre, seed, _ = max((p for p in patches if x0 < p[1].x < x1 and p[3] < 0.22), key=lambda p: p[0])
        n = sum((g.normal * g.calc_area() for g in seed), Vector()).normalized()
        h = Z.cross(n).normalized()
        w = n.cross(h)
        # the opening as the eye takes it: the screen's faces themselves left the panel off-centre in its trim (the
        # model's screen runs on under the bezel and folds, unevenly round it; the user, 2026-11-02)
        hull, smooth, cells = Opening(tree, Texel, centre, n, h, w, name)
        # the smallest rectangle round it (its long sides on the screen's straight edges; the side screens are slanted)
        best = None
        for (ax, ay), (bx, by) in zip(hull, hull[1:] + hull[:1]):
            d = math.hypot(bx - ax, by - ay)
            if d < 1e-6:
                continue
            ex, ey = (bx - ax) / d, (by - ay) / d
            along = [px * ex + py * ey for px, py in hull]
            across = [-px * ey + py * ex for px, py in hull]
            box = (max(along) - min(along)) * (max(across) - min(across))
            if best is None or box < best[0]:
                best = (box, ex, ey, max(along) - min(along) >= max(across) - min(across))
        _, ex, ey, long_along = best
        u = h * ex + w * ey if long_along else h * -ey + w * ex
        u = u if u.x > 0 else -u
        v = n.cross(u)
        # the panel: the opening's outline SCREEN_TUCK larger, so its edges go under the trim (a rectangle round it lay
        # over the trim's light bevel where the trim leans or cuts a corner)
        origin = centre + n * SCREEN_PROUD
        outline = [(h * s + w * t).dot(u) for s, t in smooth], [(h * s + w * t).dot(v) for s, t in smooth]
        outline = list(zip(*outline))
        panel = Offset([origin + u * a + v * b for a, b in outline], n, -SCREEN_TUCK)
        a, b = [(p - origin).dot(u) for p in panel], [(p - origin).dot(v) for p in panel]
        fits.append((name, material, origin, n, u, v, (min(a), max(a), min(b), max(b)), outline, panel, cells))
    found = {}
    for material in SCREEN_LOOK:  # the material slots in this order: Screen 1, Display 2, Aim 3 (the mod's `element`)
        group = [f for f in fits if f[1] == material]
        # found by a tag, not by name: in a session that already has a material of that name (hull_live.py), Blender
        # adds a suffix to the new one's
        mine = [k for k, m in enumerate(me.materials) if m.get("screen") == material]
        if not mine:
            tiles = []
            for name, _, _, _, _, _, (u0, u1, v0, v1), outline, _, _ in group:
                middle, height = ((u0 + u1) / 2, (v0 + v1) / 2), v1 - v0
                tiles.append(Display(*SCREEN_PIXELS, (u1 - u0) / height, **SCREEN_LOOK[material],
                                     outline=[((a - middle[0]) / height, (b - middle[1]) / height) for a, b in outline]))
            image = Png(np.concatenate(tiles, 0), material, os.path.join(folder, f"screen-{material.lower()}.png"))
            m = bpy.data.materials.new(material)
            m.use_nodes = True
            bsdf = next(k for k in m.node_tree.nodes if k.type == "BSDF_PRINCIPLED")
            tex = m.node_tree.nodes.new("ShaderNodeTexImage")
            tex.image = image
            m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
            m["screen"] = material
            me.materials.append(m)
            mine = [len(me.materials) - 1]
        for slot, (name, _, origin, n, u, v, (u0, u1, v0, v1), _, panel, cells) in enumerate(group):
            # the screen's own flat panel SCREEN_PROUD in front of the model's screen: the model's screen is not one clean
            # face (ragged triangles round its border, folds across it that the grown console tilts), and textured in
            # place its edges showed jagged and its glow distorted (the user, 2026-11-02). Its UVs span the rectangle
            # round it, in the slot-th of the material's stacked images.
            face = bm.faces.new([bm.verts.new(p) for p in panel])
            face.material_index = mine[0]
            for loop in face.loops:
                p = loop.vert.co - origin
                loop[uv].uv = ((p.dot(u) - u0) / (u1 - u0), (slot + (p.dot(v) - v0) / (v1 - v0)) / len(group))
            corner = [origin + u * a + v * b for a, b in ((u0, v0), (u1, v0), (u1, v1), (u0, v1))]
            found[name] = {"material": material, "openingCells": cells, "width": round(u1 - u0, 4), "height": round(v1 - v0, 4),
                           "corners": [[round(c, 4) for c in p] for p in corner], "normal": [round(c, 4) for c in n],
                           "u": [round(c, 4) for c in u], "v": [round(c, 4) for c in v], "panel": [[round(c, 4) for c in p] for p in panel],
                           "imageRows": [slot / len(group), (slot + 1) / len(group)]}
    bm.to_mesh(me)
    bm.free()
    return found


def Glass(windows, folder):
    """The windows' panes, for the game's glass (packaged as Hull_Glass over the game's visor material; research/48
    section 26): each window's outline as one flat pane facing the room (the material is one-sided, seen from inside),
    planar UVs, and a 4x4 grey image (written to `folder`) only because the packager wants one."""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    room = Vector((0.0, -0.1, 0.25))
    for w in windows:
        pts = [Vector(p) for p in w]
        if Newell(pts).dot(room - sum(pts, Vector()) / len(pts)) < 0:
            pts.reverse()
        f = bm.faces.new([bm.verts.new(p) for p in pts])
        for loop, t in zip(f.loops, Local(pts)):
            loop[uv].uv = t
    me = bpy.data.meshes.new("glass")
    bm.to_mesh(me)
    bm.free()
    m = bpy.data.materials.new("Glass")
    m.use_nodes = True
    tex = m.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = Png(np.full((4, 4, 3), 0.2, np.float32), "glass", os.path.join(folder, "glass.png"))
    m.node_tree.links.new(tex.outputs["Color"], next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED").inputs["Base Color"])
    me.materials.append(m)
    o = bpy.data.objects.new("glass", me)
    bpy.context.scene.collection.objects.link(o)
    return o


RETICLE_STRIP = 0.025  # the reticle mesh's strips, a fortieth of its one unit across (the game side's ask)


def AimReticle(folder):
    """The gun sight as a mesh for the game's aiming capture (packaged as Aim_Reticle; the game side places it at the traced
    aim point, scaled with distance, hidden from the eyes): the aiming screen's idle sight (RETICLE) as flat strips, one
    model unit across (100 package units), centred on the origin in the y-z plane, facing +x and -x (the package's +X),
    in the screens' orange (one flat texel, its image written to `folder`). A capture replaces the screen's own image, so the
    reticle has to be in the world for the capture to see it."""
    s = 0.5 / (RETICLE["ticks"][-1][0] + RETICLE_STRIP)  # the outermost tick at the edge
    w = RETICLE_STRIP / 2
    shapes = []
    ring, k = RETICLE["ring"] * s, 48
    for i in range(k):
        a, c = 2 * math.pi * i / k, 2 * math.pi * (i + 1) / k
        shapes.append([((ring - w) * math.cos(a), (ring - w) * math.sin(a)), ((ring + w) * math.cos(a), (ring + w) * math.sin(a)),
                       ((ring + w) * math.cos(c), (ring + w) * math.sin(c)), ((ring - w) * math.cos(c), (ring - w) * math.sin(c))])
    dot = max(RETICLE["dot"] * s, w * 1.2)
    shapes.append([(dot * math.cos(2 * math.pi * i / 12), dot * math.sin(2 * math.pi * i / 12)) for i in range(12)])
    a0, a1 = (c * s for c in RETICLE["cross"])
    for sy, sz in ((1, 0), (-1, 0), (0, 1), (0, -1)):  # the cross's arms
        if sy:
            shapes.append([(sy * a0, -w), (sy * a1, -w), (sy * a1, w), (sy * a0, w)])
        else:
            shapes.append([(-w, sz * a0), (w, sz * a0), (w, sz * a1), (-w, sz * a1)])
    for tick, half in RETICLE["ticks"]:
        for sy in (1, -1):
            h = max(half * s, w * 1.5)
            shapes.append([(sy * tick * s - w * 0.8, -h), (sy * tick * s + w * 0.8, -h), (sy * tick * s + w * 0.8, h), (sy * tick * s - w * 0.8, h)])
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    for shape in shapes:
        for facing in (1, -1):  # both sides: the camera may see it from behind
            pts = [Vector((0.0, y, z)) for y, z in shape]
            pts = pts if Newell(pts).x * facing > 0 else pts[::-1]
            f = bm.faces.new([bm.verts.new(p) for p in pts])
            for loop in f.loops:
                loop[uv].uv = (0.5, 0.5)
    me = bpy.data.meshes.new("aim-reticle")
    bm.to_mesh(me)
    bm.free()
    m = bpy.data.materials.new("Aim_Reticle")
    m.use_nodes = True
    tex = m.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = Png(np.ones((8, 8, 3), np.float32) * np.array([1.0, 0.36, 0.06], np.float32), "aim-reticle", os.path.join(folder, "aim-reticle.png"))
    m.node_tree.links.new(tex.outputs["Color"], next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED").inputs["Base Color"])
    me.materials.append(m)
    o = bpy.data.objects.new("aim-reticle", me)
    bpy.context.scene.collection.objects.link(o)
    return o


def Furnish(assets):
    """The chair and the controls from game_ready.py's files, placed (FURNITURE), each control's base dropped onto the
    chair's armrest (its highest point under the base, ON_ARMREST above it). Returns the objects; each file's place for
    the mod (its origin, the moving parts' pivot, in hull coordinates, the turn, the scale, a moving part's axis); and the
    seated pilot (Pilot)."""
    detached = json.load(open(os.path.join(assets, "detached", "manifest.json"), encoding="utf-8"))
    objects, placed, parts = [], {}, {}
    chair = None
    for name, files, metres, offset, turn in FURNITURE:
        scale = metres / METRES_PER_UNIT
        rotation = Matrix.Rotation(math.radians(turn), 4, "Z")
        at = Matrix.Translation(Vector(CHAIR_AT) + Vector(offset) / METRES_PER_UNIT) @ rotation @ Matrix.Scale(scale, 4)
        mine = []
        for file in files:
            before = set(bpy.data.objects)
            bpy.ops.import_scene.gltf(filepath=os.path.join(assets, "game-ready", file + ".glb"))
            o = next(x for x in bpy.data.objects if x not in before and x.type == "MESH")
            o.matrix_world = at @ o.matrix_world
            o.name = file
            mine.append(o)
        if name == "chair":
            chair = np.array([(mine[0].matrix_world @ v.co)[:] for v in mine[0].data.vertices])
        else:  # on the armrest: the base's footprint, the chair's highest point under it
            base = np.array([(mine[0].matrix_world @ v.co)[:] for v in mine[0].data.vertices])
            lo, hi = base.min(0), base.max(0)
            under = chair[(chair[:, 0] >= lo[0]) & (chair[:, 0] <= hi[0]) & (chair[:, 1] >= lo[1]) & (chair[:, 1] <= hi[1])]
            lift = Matrix.Translation((0.0, 0.0, float(under[:, 2].max()) + ON_ARMREST / METRES_PER_UNIT - float(lo[2])))
            for o in mine:
                o.matrix_world = lift @ o.matrix_world
        parts[name] = mine
        for i, (file, o) in enumerate(zip(files, mine)):
            objects.append(o)
            entry = {"origin": [round(c, 5) for c in o.matrix_world.translation], "turnDeg": turn,
                     "hullUnitsPerModelUnit": round(scale, 5)}
            asset = next((k for k in detached if file.startswith(k)), None)
            if asset and i == 1:
                axis = rotation.to_3x3() @ Vector(detached[asset]["motion"]["axis"])
                entry["motion"] = {**{k: v for k, v in detached[asset]["motion"].items() if k not in ("axis", "ringCentre", "clashes")},
                                   "axis": [round(c, 5) for c in axis], "unitsAreModel": True}
            placed[file] = entry
    return objects, placed, Pilot(parts, placed)


def Pilot(parts, placed):
    """The seated pilot the furniture is laid out for, for the game side (Shepard seated, the VR head at the eye, the hands
    on the controls): in hull units, and from the eye in metres along the seat's right, up and forward (the mod's
    `leverOffset`/`stickOffset` convention). The hip point (H-point) 0.13 m in front of the backrest at 0.12 m above the
    cushion's top, 0.07 m above it; knees and heels of a seated adult (thigh 0.45 m, shin 0.45 m); each hand's grip
    point: the stick's grip at 40% of its height, the throttle's handle at neutral; how far the eye is from the headrest
    and the throttle's travel either side of neutral."""
    M = METRES_PER_UNIT
    chair = parts["chair"][0]
    bm = bmesh.new()
    bm.from_mesh(chair.data)
    bm.transform(chair.matrix_world)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    eye = Vector(EYE)
    seat = tree.ray_cast(Vector((0.0, eye.y + 0.15 / M, 1.0)), Vector((0, 0, -1)))[0]
    back = lambda z: tree.ray_cast(Vector((0.0, 0.5, z)), Vector((0, -1, 0)))[0].y
    hip = Vector((0.0, back(seat.z + 0.12 / M) + 0.13 / M, seat.z + 0.07 / M))
    knee_y, knee_z = hip.y + 0.43 / M, hip.z - 0.02 / M
    points = {"eye": eye, "hPoint": hip}
    for side, x in (("Left", -0.11 / M), ("Right", 0.11 / M)):
        points["knee" + side] = Vector((x, knee_y, knee_z))
        points["heel" + side] = Vector((x, knee_y + 0.12 / M, 0.0))
    stick = parts["joystick"][1]
    grip = np.array([(stick.matrix_world @ v.co)[:] for v in stick.data.vertices])
    lo, hi = grip.min(0), grip.max(0)
    points["stickGrip"] = Vector(((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2] + 0.4 * (hi[2] - lo[2])))
    handle = parts["throttle"][1]
    motion = placed["throtle-control-handle"]["motion"]
    h = np.array([(handle.matrix_world @ v.co)[:] for v in handle.data.vertices])
    lo, hi = h.min(0), h.max(0)
    unit = placed["throtle-control-handle"]["hullUnitsPerModelUnit"]
    axis = Vector(motion["axis"])
    points["throttleGrip"] = Vector(((lo + hi) / 2).tolist()) + axis * motion["neutral"] * unit
    out = {"hull": {k: [round(c, 4) for c in p] for k, p in points.items()},
           "fromEyeMetres": {k: [round((p - eye).x * M, 3), round((p - eye).z * M, 3), round((p - eye).y * M, 3)]
                             for k, p in points.items() if k != "eye"},
           "convention": "fromEyeMetres: right, up, forward from the eye, as the mod's [Mako] leverOffset/stickOffset",
           "eyeToHeadrestMetres": round((eye.y - back(eye.z)) * M, 3),
           "seatTopMetres": round(seat.z * M, 3),
           "throttleTravelMetres": {"forward": round((motion["neutral"] - motion["min"]) * unit * M, 3),
                                    "back": round((motion["max"] - motion["neutral"]) * unit * M, 3),
                                    "forwardIsHull": [round(-c, 4) for c in axis]},
           "stickTwistLimitDeg": placed["joystick-grip"]["motion"]["limitDeg"]}
    return out


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


def RearWall(b):
    """The rear wall at YB: the room's section (the floor, the walls, the caps' profile, the ceiling) round a door's
    opening, with panels set in either side; the opening framed in light (a chamfered face standing proud, lining the
    wall's thickness); two leaves set back in it, each with a panel."""
    Y = Vector((0, 1, 0))
    hw, dh, dc = DOOR
    side = [Vector((XRAIL - s, YB, ZH + u)) for s, u in CAP]  # from the wall's top up to the ceiling
    for mirror in (False, True):
        b.mirror = mirror
        # beside the door up to the frame line, a panel set in; above it, plain, up the caps' profile to the ceiling. (An
        # inset needs a convex outline: offset round the profile's dip it flipped a triangle, and rays from the cabin
        # left through it, the probes' one leak since research/48 section 15.)
        Slab(b, [(hw, YB, -SEAM), (XWALL, YB, -SEAM), (XWALL, YB, ZH), (hw, YB, ZH)], Y, SLAB, inset=(0.008, 0.006, 0.03))
        Slab(b, [Vector((hw, YB, ZH))] + side + [Vector((hw, YB, ZR))], Y, SLAB)
        b.Face([(hw - dc, YB, dh), (hw, YB, dh), (hw, YB, dh - dc)], "PLATE", toward=Y)  # the opening's cut corner
        leaf = [(0.0, YB - 0.015, -SEAM), (hw, YB - 0.015, -SEAM), (hw, YB - 0.015, dh - dc), (hw - dc, YB - 0.015, dh), (0.0, YB - 0.015, dh)]
        Slab(b, leaf, Y, 0.02, inset=(0.01, 0.005, 0.03))
    b.mirror = False
    Slab(b, [(-hw, YB, dh), (hw, YB, dh), (hw, YB, ZR), (-hw, YB, ZR)], Y, SLAB, "DARK")
    opening = [Vector(p) for p in ((-hw, YB, 0), (hw, YB, 0), (hw, YB, dh - dc), (hw - dc, YB, dh), (-hw + dc, YB, dh), (-hw, YB, dh - dc))]
    Sweep(b, Inward(opening, Y), Y, DOOR_FRAME, DOOR_FRAME_CLASSES, closed=True)


# The rear section's side walls in the lit concept's wall language (its lower side walls; ours, beside the console, are
# behind its wings): heights of the skirting's top, the lower panel's top, the ledge's foot and top; the lamp's half
# length, half height and centre height, in a framed housing under the ledge.
SIDE_WALL = (0.03, 0.195, 0.25, 0.268)
SIDE_LAMP = (0.075, 0.011, 0.222)


def SideWall(b, y0, y1):
    """A rear-section side wall at XWALL from y0 to y1, facing -x (mirrored by the builder): the wall, a skirting along
    the floor, a bolted panel set in, the lamp's framed housing (its lamp: Lamps), a ledge, a panel set in above with a
    grille. Each piece stands proud of the wall, so the wall stays whole behind them."""
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    skirt, low, ledge0, ledge1 = SIDE_WALL
    half, tall, height = SIDE_LAMP
    mid, m = (y0 + y1) / 2, 0.012
    Slab(b, [(XWALL, y0, -SEAM), (XWALL, y1, -SEAM), (XWALL, y1, ZH), (XWALL, y0, ZH)], -X, SLAB, "DARK")
    along = [Vector((XWALL, y0, 0)), Vector((XWALL, y1, 0))]  # swept members: side -x (into the room), up z
    Sweep(b, along, Z, Chamfered(0.0, 0.008, -SEAM, skirt, 0.003), ["DARK", "DARK", "LIGHT", "LIGHT", "DARK", "DARK", "DARK", "DARK"])
    Slab(b, [(XWALL - 0.012, y0 + m, skirt + 0.006), (XWALL - 0.012, y1 - m, skirt + 0.006), (XWALL - 0.012, y1 - m, low),
             (XWALL - 0.012, y0 + m, low)], -X, 0.016, inset=(0.01, 0.006, 0.01))
    frame = [(XWALL - 0.004, mid + a * (half + 0.016), height + bb * (tall + 0.014)) for a, bb in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    Slab(b, frame, -X, 0.008, "PLATE", "LIGHT")
    Sweep(b, along, Z, Chamfered(0.0, 0.03, ledge0, ledge1, 0.005), ["DARK", "LIGHT", "LIGHT", "LIGHT", "DARK", "DARK", "DARK", "DARK"])
    face = XWALL - 0.010
    Slab(b, [(face, y0 + m, ledge1 + 0.012), (face, y1 - m, ledge1 + 0.012), (face, y1 - m, ZH - 0.012), (face, y0 + m, ZH - 0.012)],
         -X, 0.014, inset=(0.008, 0.005, 0.012))
    Grille(b, (face + 0.005, mid + 0.04, 0.4), Y, Z, -X, 0.04, 0.03, 6)


def DecalAtlas(folder):
    """The decal atlas (tools/cockpit/decals.py, run with the system Python, which has PIL) drawn into <folder>/decals:
    each decal's rectangle and aspect, and the image's path."""
    out = os.path.join(folder, "decals")
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "decals.py")
    subprocess.run(["python", "-I", script, out], check=True)
    with open(os.path.join(out, "decals.json"), encoding="utf-8") as f:
        return json.load(f)["decals"], os.path.join(out, "decals.png")


def Decal(b, decals, name, centre, facing, right, width, proud=0.0012):
    """A decal from the atlas (`decals`: DecalAtlas's rectangles): a quad `width` across along `right` (the way its text
    reads), as tall as the decal's aspect makes it, `proud` in front of the surface at `centre` facing `facing`. Never
    mirrored by the builder: mirrored text reads backwards, so each side's decal is placed on its own."""
    d = decals[name]
    u0, v0, u1, v1 = d["uv"]
    n, r = Vector(facing).normalized(), Vector(right).normalized()
    up = n.cross(r)
    c, half_w, half_h = Vector(centre) + n * proud, r * width / 2, up * width / d["aspect"] / 2
    mirror, b.mirror = b.mirror, False
    b.Face([c - half_w - half_h, c + half_w - half_h, c + half_w + half_h, c - half_w + half_h], "DECAL", toward=n,
           uvs=[(u0, v0), (u1, v0), (u1, v1), (u0, v1)])
    b.mirror = mirror


def Decor(b, decals):
    """What a working cabin carries (the user, 2026-11-03: the details a game of this kind has; research/48 section 24):
    labels, panel numbers and hazard stripes from the decal atlas (`decals`), grab handles on the portal's posts, cable
    bundles in the rear corners, a switch bank on each rear-section side wall beside its vent."""
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    hw, dh, dc = DOOR
    mid = (YB + YR) / 2
    post = XWALL - max(s for s, _ in PORTAL)  # the portal posts' room faces
    if decals:
        for s, vent, number, post_number in ((1, "vent-a", "r-01", "p-03"), (-1, "vent-b", "l-01", "p-04")):
            facing, right = -X * s, -Y * s  # into the room; reading left to right as one faces the wall
            Decal(b, decals, vent, (s * (XWALL - 0.005), mid + 0.04, 0.355), facing, right, 0.07)
            Decal(b, decals, number, (s * (XWALL - 0.006), YR - 0.07, 0.15), facing, right, 0.05)
            Decal(b, decals, "hazard-post", (s * post, YR + 0.035, 0.05), facing, right, 0.042)
            Decal(b, decals, post_number, (s * post, YR + 0.035, 0.24), facing, right, 0.04)
        Decal(b, decals, "caution", (XWALL - 0.006, mid, 0.11), -X, -Y, 0.13)
        Decal(b, decals, "hatch", (0.0, YB - 0.015, dh - dc - 0.025), Y, -X, 0.16)
        Decal(b, decals, "hazard", (0.0, YB + 0.05, 0.0), Z, X, 0.26)
        Decal(b, decals, "hazard-long", (0.0, YR + PORTAL_DEPTH + 0.02, 0.0), Z, X, 0.6)
        Decal(b, decals, "life", (0.25, YB, 0.31), Y, -X, 0.09)
        Decal(b, decals, "comms", (-0.25, YB, 0.31), Y, -X, 0.09)
    for mirror in (False, True):
        b.mirror = mirror
        # a grab handle up each portal post's room face, over its groove
        x, y = post + 0.002, YR + 0.035
        Pipe(b, [Vector((x, y, 0.29)), Vector((x - 0.014, y, 0.3)), Vector((x - 0.014, y, 0.4)), Vector((x, y, 0.41))], 0.0045, Y, "METAL", sides=8)
        # cables up the rear corner, in front of the rear wall and clear of the side wall's ledge, clamped
        for cx, cy in ((XWALL - 0.058, YB + 0.0125), (XWALL - 0.05, YB + 0.0175), (XWALL - 0.042, YB + 0.0125)):
            Pipe(b, [Vector((cx, cy, 0.03)), Vector((cx, cy, ZH - 0.012))], 0.0035, Y, "DARK", sides=8)
            for z in (0.1, 0.22, 0.34, 0.46):
                Pipe(b, [Vector((cx, cy, z)), Vector((cx, cy, z + 0.006))], 0.0055, Y, "LIGHT", sides=8)
        # a switch bank on the rear-section side wall's upper panel, two orange indicators over it
        Keypad(b, (XWALL - 0.005, mid - 0.06, 0.4), -Y, Z, -X, 4, 2, 0.008, 0.004, cls="LIGHT")
        Keypad(b, (XWALL - 0.005, mid - 0.06, 0.428), -Y, Z, -X, 2, 1, 0.005, 0.004)
    b.mirror = False


WINDOW_LIGHT = ((0.95, 0.96, 1.0), 4.0)  # daylight through each window in the bake: colour, power a square hull unit of opening


def WindowLights(windows):
    """Soft daylight through each window opening, for the bake: an area light across it, just inside, facing into the
    room (the world outside lit the cabin through nothing before; research/48 section 24)."""
    room = Vector((0.0, -0.1, 0.25))
    out = []
    for w in windows:
        pts = [Vector(p) for p in w]
        c = sum(pts, Vector()) / len(pts)
        n = Newell(pts).normalized()
        n = n if n.dot(room - c) > 0 else -n
        u = (pts[1] - pts[0]).normalized()
        v = n.cross(u)
        a, b = [(p - c).dot(u) for p in pts], [(p - c).dot(v) for p in pts]
        su, sv = max(a) - min(a), max(b) - min(b)
        at = c + u * (max(a) + min(a)) / 2 + v * (max(b) + min(b)) / 2 + n * 0.01
        out.append((at, n, u, (su * 0.9, sv * 0.9), WINDOW_LIGHT[0], WINDOW_LIGHT[1] * su * sv))
    return out


def Lamp(b, lights, kind, cls, centre, u, v, n, half_u, half_v, proud=0.003):
    """A lamp on a surface facing `n`: a dark housing `proud` deep and a glowing face just in front of it, and the area
    light (centre, facing, axes, size, colour, power) that stands for it in the bake."""
    c, u, v, n = Vector(centre), Vector(u), Vector(v), Vector(n)
    rim = 0.004
    housing = [c + u * a * (half_u + rim) + v * bb * (half_v + rim) + n * proud for a, bb in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    Slab(b, housing, n, proud + 0.002, "DARK", "DARK")
    b.Face([c + u * a * half_u + v * bb * half_v + n * (proud + 0.0008) for a, bb in ((-1, -1), (1, -1), (1, 1), (-1, 1))], cls, toward=n)
    if LAMP_POWER[kind]:
        sign = -1 if b.mirror else 1
        flip = lambda w: Vector((sign * w.x, w.y, w.z))
        lights.append((flip(c + n * (proud + 0.002)), flip(n), flip(u), (2 * half_u, 2 * half_v), GLOW[cls], LAMP_POWER[kind]))


def Wedge(b, y0, y1):
    """A ceiling lamp's housing (FIXTURE) from y0 to y1: a wedge under the ceiling, its sloped face turned to the room's
    middle. Returns the sloped face's centre, its axis up the slope and its normal."""
    x0, x1, drop = FIXTURE
    Y = Vector((0, 1, 0))
    section = [Vector((x0, 0, ZR)), Vector((x1, 0, ZR)), Vector((x1, 0, ZR - drop))]
    a, c = [p + Y * y0 for p in section], [p + Y * y1 for p in section]
    slope = (section[0] - section[2]).normalized()
    n = Y.cross(slope)
    n = n if n.z < 0 else -n
    b.Face([a[2], c[2], c[0], a[0]], "DARK", toward=n, uvs=QUAD)
    b.Face([a[1], c[1], c[2], a[2]], "DARK", toward=Vector((1, 0, 0)), uvs=QUAD)
    b.Face(a, "DARK", toward=-Y)
    b.Face(c, "DARK", toward=Y)
    return (section[0] + section[2]) / 2 + Y * (y0 + y1) / 2, slope, n


def Drop(b, front):
    """The drop panel under the spine's front end (DROP), across its lower tier: from inside the spine down past its
    underside, its rear face sloping toward the pilot. Returns that face's part below the spine: its centre, the axis
    up it and its normal."""
    length, deeper, face = DROP
    y0, top, low = front - length, ZR - SPINE_DEPTH + 0.008, ZR - SPINE_DEPTH - deeper
    section = [(y0, top), (y0 + face, low), (front, low), (front, top)]
    Sweep(b, [Vector((-0.085, 0, 0)), Vector((0.085, 0, 0))], Vector((0, 0, 1)), section + section[:1], ["DARK", "PLATE", "DARK", "DARK"])
    mid = (ZR - SPINE_DEPTH + low) / 2
    up = Vector((0, -face, top - low)).normalized()
    return Vector((0, y0 + face * (top - mid) / (top - low), mid)), up, Vector((0, -(top - low), -face)).normalized()


def Lamps(b, wall, corner, drop):
    """The lit concept's lamps, mirrored in x: a lamp on each corner plate over the front side windows (`corner`: the
    right plate's corners, Shell), lamps in wedge housings over the cabin and the rear section, an orange strip in each
    rear-section side wall's housing (SideWall), pale strips in the floor along the panes' foot, a strip and two
    indicators on the drop panel at the spine's front (`drop`: its face, Drop) and lamps along the spine's underside
    behind it, blue-white lights at the portal's feet, a strip over the door. `wall`: "floor", the panes' face (x) at
    the floor at each of FLOOR_STRIPS. Returns the area lights."""
    lights = []
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    hw, dh, _ = DOOR
    D, E, top = corner
    n = (E - D).cross(top - D).normalized()
    n = n if n.z < 0 else -n
    along = (top - E).normalized()
    for mirror in (False, True):
        b.mirror = mirror
        # the corner plate's lamp: along its top edge (on the ceiling), as long as the triangle holds it 6 mm in from
        # every edge (one placed by the plate's middle hung 9 mm over the front side window's top edge)
        perp = n.cross(along)
        perp = perp if perp.dot(D - E) > 0 else -perp
        span, dx, dy = (top - E).length, (D - E).dot(along), (D - E).dot(perp)
        rim, margin, half_v = 0.004, 0.006, 0.011
        w0, w1 = margin, margin + 2 * (half_v + rim)
        a0, a1 = dx / dy * w1 + margin, span - (span - dx) / dy * w1 - margin
        Lamp(b, lights, "ceiling", "LAMP", E + along * (a0 + a1) / 2 + perp * (w0 + w1) / 2, along, perp, n, (a1 - a0) / 2 - rim, half_v)
        for kind, y0, y1 in (("midCeiling", -0.27, -0.13), ("rearCeiling", -0.58, -0.47)):
            c, slope, m = Wedge(b, y0, y1)
            Lamp(b, lights, kind, "LAMP", c, Y, slope, m, (y1 - y0) / 2 - 0.012, 0.02, proud=0.001)
        # the rear section's side wall (SideWall's framed housing); short pale strips in the floor along the panes' foot
        # in front of the portal, where the console's wings leave it open (a lamp on the pane's lower wall stood behind
        # the wing)
        Lamp(b, lights, "wall", "LAMP_ORANGE", (XWALL - 0.004, (YB + YR) / 2, SIDE_LAMP[2]), Y, Z, -X, SIDE_LAMP[0], SIDE_LAMP[1], proud=0.008)
        for y, x in zip(FLOOR_STRIPS, wall["floor"]):
            Lamp(b, lights, "floor", "LAMP", (x - 0.014, y, 0.0), Y, X, Z, 0.021, 0.0035, proud=0.0015)
        # lamps along the spine's underside behind the drop panel, on its lower flats
        rear = drop[0].y - 0.03
        for y in (rear - 0.1, rear - 0.06, rear - 0.02):
            Lamp(b, lights, "spine", "LAMP", (0.0615, y, ZR - SPINE_DEPTH), Y, X, -Z, 0.012, 0.0075, proud=0.0015)
        # the portal's feet
        Lamp(b, lights, "foot", "LAMP_BLUE", (XWALL - 0.03, YR + PORTAL_DEPTH, 0.028), X, Z, Y, 0.014, 0.012, proud=0.001)
    b.mirror = False
    # the drop panel's face toward the pilot: a strip, an indicator either side
    mid, up, m = drop
    Lamp(b, lights, "spine", "LAMP", mid, X, up, m, 0.036, 0.0045, proud=0.001)
    for x in (-0.056, 0.056):
        Lamp(b, lights, "spine", "LAMP_ORANGE", mid + X * x, X, up, m, 0.006, 0.0045, proud=0.001)
    Lamp(b, lights, "door", "LAMP", (0.0, YB, dh + 0.045), X, Z, Y, 0.08, 0.008)
    return lights


def Conduits(b, y0, y1, rise):
    """The pipes either side of the spine (CONDUITS) from y0 to y1 under the ceiling, with straps across them STRAP
    apart: a bar under the pipes hung from the ceiling at both ends. `rise`: the run's front end bends up into the
    ceiling; else the pipes end in flanges against a beam there."""
    Z = Vector((0, 0, 1))
    r, z = CONDUIT_R, ZR - 0.003 - CONDUIT_R
    x0, x1 = CONDUITS[0] - r - 0.008, CONDUITS[-1] + r + 0.008
    for x in CONDUITS:
        path = [Vector((x, y0, z)), Vector((x, y1, z))]
        if rise:
            path = [path[0], Vector((x, y1 - 0.03, z)), Vector((x, y1, ZR + 0.02))]
        Pipe(b, path, r, Z, "METAL", sides=12)
        for y, end in ((y0, True), (y1, not rise)):
            if end:
                Pipe(b, [Vector((x, y, z)), Vector((x, y + (0.004 if y == y0 else -0.004), z))], r + 0.004, Z, "LIGHT", sides=12)
    for y in np.arange(y0 + STRAP / 2, y1 - (0.06 if rise else 0.03), STRAP):
        bar = Chamfered(-0.006, 0.006, z - r - 0.006 - ZR, z - r - ZR, 0.0015)
        Sweep(b, [Vector((x0, y, ZR)), Vector((x1, y, ZR))], Z, bar, "DARK")
        for x in (x0 + 0.003, x1 - 0.003):
            Sweep(b, [Vector((x, y, z - r - 0.003)), Vector((x, y, ZR))], Vector((0, 1, 0)), Chamfered(-0.003, 0.003, -0.006, 0.006, 0.001), "DARK")


def Details(b, edge, desk, screen, bezel, column, spine_end):
    """The lit concept's smaller parts: the conduits either side of the spine (Conduits), behind the portal from the rear
    wall and in front of it up into the ceiling short of the windshield; louvred vents beside the door, on the spine's
    sides and under its upper tier behind its lamps; a keypad on each desk shelf (`desk`: the right shelf's top,
    DeskShelf); a lit keypad over a vent on each column beside the centre screen (`column`: the right one's face,
    Column); lit bars on the centre screen's bezel above and below it (`screen`: its corners; `bezel`: how far the
    bezel's face stands in front of the screen, Bezel). (The rear section's side walls carry their own vents, SideWall.)"""
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    hw, dh, _ = DOOR
    at, facing, half_x, half_z = column
    u = Z.cross(facing).normalized()
    v = facing.cross(u)
    key, gap = 0.009, 0.0035
    pad = (3 * key + 4 * gap) / 2
    for mirror in (False, True):
        b.mirror = mirror
        Conduits(b, YR + PORTAL_DEPTH, FrontAt(edge, CONDUITS[-1]) - SPINE_CLEAR, True)
        Conduits(b, YB, YR, False)
        Grille(b, (0.25, YB, 0.36), X, Z, Y, 0.045, 0.035, 7)
        Grille(b, (0.085, spine_end - 0.3, ZR - 0.038), Y, Z, X, 0.03, 0.008, 3, proud=0.003)
        Grille(b, (0.0995, spine_end - 0.2, ZR - 0.028), X, Y, -Z, 0.011, 0.035, 7, proud=0.004)
        x0, y0, x1, y1, z = desk
        Keypad(b, ((x0 + x1) / 2 - 0.01, y0 + 0.035, z), X, Y, Z, 4, 3, 0.011, 0.004)
        # the column beside the centre screen: a 3 x 3 keypad near its top, a vent below it
        top = at + v * (half_z - pad - 0.008)
        Keypad(b, top, u, v, facing, 3, 3, key, gap)
        vent = (half_z - pad - 0.008) - pad - 0.012
        Grille(b, at + v * ((vent - half_z + 0.006) / 2), u, v, facing, half_x - 0.006, (vent + half_z - 0.006) / 2, 5, proud=0.004)
    b.mirror = False
    bl, br, tr, tl = [Vector(c) for c in screen["corners"]]
    n = Vector(screen["normal"])
    for z, edge_ in ((tl.z + 0.0037, tl), (bl.z - 0.0037, bl)):
        centre = Vector((0.0, edge_.y, z)) + n * (bezel + 0.0012)
        Slab(b, [centre + X * a * 0.05 + Z * bb * 0.0022 for a, bb in ((-1, -1), (1, -1), (1, 1), (-1, 1))], n, 0.002, "LAMP_ORANGE", "DARK")


def Bezel(console, screen):
    """How far the centre screen's bezel stands in front of the screen just above its top edge (a ray toward it)."""
    bl, br, tr, tl = [Vector(c) for c in screen["corners"]]
    n = Vector(screen["normal"])
    at = (tl + tr) / 2 + Vector((0, 0, 0.004))
    bm = bmesh.new()
    bm.from_mesh(console.data)
    hit = Visible(BVHTree.FromBMesh(bm), at + n * 0.2, -n)
    bm.free()
    return float((hit[0] - at).dot(n))


def Column(console, screen):
    """The flat face of the column right of the centre screen, where the lit concept has a keypad over a vent: rays
    along the screen's normal, in 2 mm steps outward from its right edge at its middle height, the longest run landing
    on faces turned the same way within 3 mm of each other's depth; then up and down through that run's middle. Returns
    the face's centre, normal and half sizes (across, up)."""
    bl, br, tr, tl = [Vector(c) for c in screen["corners"]]
    n = Vector(screen["normal"])
    bm = bmesh.new()
    bm.from_mesh(console.data)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    step = 0.002

    def Depth(x, z):
        hit = Visible(tree, Vector((x, br.y, z)) + n * 0.3, -n)
        return None if hit is None or hit[1].dot(n) < 0.95 else hit[0].dot(n)

    def Longest(samples):
        """The longest run of consecutive (position, depth) samples on one face: its first and last positions."""
        runs, run = [], []
        for at, d in samples:
            if d is not None and run and abs(d - run[0][1]) < 0.003:
                run.append((at, d))
                continue
            runs.append(run)
            run = [(at, d)] if d is not None else []
        runs.append(run)
        best = max(runs, key=len)
        return best[0][0], best[-1][0]
    z_mid = (br.z + tr.z) / 2
    x0, x1 = Longest([(br.x + step * k, Depth(br.x + step * k, z_mid)) for k in range(1, 60)])
    xc = (x0 + x1) / 2
    z0, z1 = Longest([(z_mid + step * k, Depth(xc, z_mid + step * k)) for k in range(-60, 60)])
    hit = Visible(tree, Vector((xc, br.y, (z0 + z1) / 2)) + n * 0.3, -n)
    return hit[0], hit[1].normalized(), (x1 - x0) / 2, (z1 - z0) / 2


def DeskShelf(console):
    """The right desk shelf's flat top in front of the centre screen: (x0, y0, x1, y1, z), its largest upward patch with
    0.05 < x < 0.2 and 0.08 < z < 0.2."""
    bm = bmesh.new()
    bm.from_mesh(console.data)
    up = [f for f in bm.faces if f.normal.z > 0.97 and 0.05 < f.calc_center_median().x < 0.2 and 0.08 < f.calc_center_median().z < 0.2]
    z = max(set(round(f.calc_center_median().z, 3) for f in up), key=lambda h: sum(f.calc_area() for f in up if round(f.calc_center_median().z, 3) == h))
    top = [f for f in up if abs(f.calc_center_median().z - z) < 0.003]
    co = np.array([v.co[:] for f in top for v in f.verts])
    bm.free()
    return float(co[:, 0].min()), float(co[:, 1].min()), float(co[:, 0].max()), float(co[:, 1].max()), float(co[:, 2].max())


def LightRig(lights):
    """The lamps' area lights and the world's fill, for a bake; returns the objects to remove after."""
    sc = bpy.context.scene
    if sc.world is None:
        sc.world = bpy.data.worlds.new("fill")
    sc.world.use_nodes = True
    background = next(n for n in sc.world.node_tree.nodes if n.type == "BACKGROUND")
    background.inputs["Color"].default_value = FILL[0] + (1.0,)
    background.inputs["Strength"].default_value = FILL[1]
    made = []
    for centre, n, u, (su, sv), colour, power in lights:
        light = bpy.data.lights.new("lamp", "AREA")
        light.shape = "RECTANGLE"
        light.size, light.size_y = su, sv
        light.energy = power
        light.color = colour
        o = bpy.data.objects.new("lamp", light)
        z = -n.normalized()  # an area light shines along its -z
        x = (u - z * u.dot(z)).normalized()
        o.matrix_world = Matrix((x, z.cross(x), z)).transposed().to_4x4()
        o.location = centre
        sc.collection.objects.link(o)
        made.append(o)
    return made


def PaneFace(pane, y, z):
    """The x where a ray from the room along +x first meets the right pane's face at (y, z)."""
    bm = bmesh.new()
    bm.from_mesh(pane.data)
    hit = Visible(BVHTree.FromBMesh(bm), (0.0, y, z), Vector((1, 0, 0)))
    bm.free()
    return hit[0].x


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


def Shell(pillar, opening, pane_face=None, console=None, decals=None):
    """The template's shell, clean: from the measured constants above and the panes' own frame, symmetric about x = 0.

    The frame line (ZH, the panes' top rail) runs along the side walls: the panes' rails, then the shell's own rail with
    the panes' cap profile above it, forward to the A-pillars' tops and back from the panes to the open rear, where a
    light portal arch stands. At each A-pillar's top the front's top edge steps up to the ceiling (ZR, the user's choice
    on 2026-11-02, as the lit concept's): the front side window's top edge rises to the windshield's top corner under a
    corner plate that spans to the cap's end and the ceiling, and the windshield's top edge is the ceiling's front edge.
    The roof rests on the caps, so nothing of it comes down across a window or the panes' frames: the spine and the
    conduits end short of the windshield's frame. Each A-pillar frames its side window's rear; frames with mitred
    corners line the rest of every opening, the windshield's dark (the concept's slim posts). The
    side walls behind the panes are cut where the panes' windows are (`opening`: the right one and how far out its frame
    reaches) and lined in light. Only those walls go through the boolean: the shell's pieces overlap, and the exact
    solver drops faces it finds inside another piece. With `pane_face` (the panes' face, PaneFace) the lamps go in too (Lamps); their area lights come back
    with the shell."""
    b, walls = Builder(TRIMS), Builder(TRIMS)
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    low, high, pane_rear, cap_front = pillar
    YP_ = high.y - 0.015  # the A-pillar's top, just behind its front face: the side window's top rear corner
    plane = WindowPlane(pillar)
    top = ShieldTop()  # the windshield's top corner: the front's top edge is on the ceiling
    # floor: chamfered plates over a dark base, the seams between them 8 mm wide and SEAM deep; the walls start on the base
    rear = [(-W, YB), (W, YB), (W, YP_), (-W, YP_)]
    front = [(-XP, YP_), (XP, YP_), (XP, YC), (XF, YF), (-XF, YF), (-XP, YC)]
    for piece in (rear, front):
        Slab(b, [(x, y, -SEAM) for x, y in piece], Z, SLAB, "SEAM", "DARK")
    columns = (-W,) + FLOOR_COLUMNS + (W,)
    for k, (x0, x1) in enumerate(zip(columns, columns[1:])):
        shift = 0.0 if k == 1 else FLOOR_ROW / 2  # the side runs half a row out of step with the middle one
        rows = np.arange(YB - shift, YF + FLOOR_ROW, FLOOR_ROW)
        for y0, y1 in zip(rows, rows[1:]):
            g = FLOOR_SEAM
            cell = [(x0 + g, y0 + g), (x1 - g, y0 + g), (x1 - g, y1 - g), (x0 + g, y1 - g)]
            for piece in (rear, front):
                part = Clip(cell, piece)
                if len(part) >= 3 and Area(part) > 0.002:
                    Plate(b, Corners(part, FLOOR_CORNER), 0.0, FLOOR_BEVEL, SEAM)
    # the wall under the windshield, behind the console, facing back
    Slab(b, [(-XF, YF, -SEAM), (XF, YF, -SEAM), (XF, YF, ZL), (-XF, YF, ZL)], -Y, SLAB, "DARK")
    # the windshield: an opening leaning back between the front chamfers up to the ceiling, framed all round, its frame
    # dark (the concept's slim posts between it and the front side windows)
    shield = [Vector((-XF, YF, ZL)), Vector((XF, YF, ZL)), top, Vector((-top.x, top.y, ZR))]
    room = Vector((0, -(ZC - ZL), -(YF - YT))).normalized()
    Sweep(b, Inward(shield, room), room, WINDOW, SHIELD_CLASSES, closed=True)
    windows = [shield]
    # the frame line along the side walls: the rail and the panes' cap profile, from the panes' caps (OVERLAP into them:
    # they end at their pillar's cut, CutPillar) to the A-pillar's top; there the window's top edge turns up to the
    # windshield's top corner on the ceiling, and a corner plate spans from it to the cap's end and the ceiling
    start = min(cap_front, PANE_CUT) - OVERLAP
    lip = XRAIL - CAP[-1][0]  # the caps' edge on the ceiling
    D = Vector((XRAIL - CAP_SWEPT[1][0], YP_, ZH + CAP_SWEPT[1][1]))
    E = Vector((lip, YP_, ZR))
    corner = (D, E, top)
    down = (E - D).cross(top - D).normalized()
    down = down if down.z < 0 else -down
    for mirror in (False, True):
        b.mirror = mirror
        Sweep(b, [Vector((XRAIL, start, ZH)), Vector((XRAIL, YP_, ZH))], Z, CAP_SWEPT, CAP_CLASSES)
        # the cap's end, closed up to the ceiling's level (it is an open strip: from in front, rays went up through it)
        b.Face([Vector((XRAIL - s_, YP_, ZH + u)) for s_, u in CAP_SWEPT] + [Vector((XWALL, YP_, ZR))], "DARK", toward=Y)
        Slab(b, [D, E, top], down, SLAB, "PLATE", "DARK")
    b.mirror = False
    # the ceiling: plates from the rear to the pillars' tops between the caps (over their top, as the joints with the
    # panes), then out along the corner plates' top edges to the windshield's top; its back. `edge`: its outline in front
    edge = [Vector((lip, start, ZR)), E, top, Vector((-top.x, top.y, ZR)), Vector((-lip, YP_, ZR)), Vector((-lip, start, ZR))]
    cols = (-lip - OVERLAP, -0.143, 0.0, 0.143, lip + OVERLAP)
    rows = np.linspace(YB, YP_, 5)
    for x0, x1 in zip(cols, cols[1:]):
        for y0, y1 in zip(rows, rows[1:]):
            b.Face([(x0, y0, ZR), (x1, y0, ZR), (x1, y1, ZR), (x0, y1, ZR)], "PLATE", toward=-Z)
    ahead = [Vector((cols[0], YP_, ZR)), Vector((cols[-1], YP_, ZR)), top, Vector((-top.x, top.y, ZR))]
    b.Face(ahead, "PLATE", toward=-Z)
    b.Face([(cols[0], YB, ZR + SLAB), (cols[-1], YB, ZR + SLAB), (cols[-1], YP_, ZR + SLAB), (cols[0], YP_, ZR + SLAB)], "DARK", toward=Z)
    b.Face([p + Z * SLAB for p in ahead], "DARK", toward=Z)
    # the spine, two tiers, the lower recessed underneath: from the portal to SPINE_CLEAR short of the windshield's top,
    # ending square there over the drop panel, and behind the portal from the rear wall to it. Each end at the portal
    # slopes up into the ceiling at 45 degrees; at the rear wall they run on into it.
    def Beam(x, profile, classes, depth, rear, front, into_wall=False, panel=None):
        ya = rear + 1.414 * depth
        start = [Vector((x, rear - 0.01, ZR))] if into_wall else [Vector((x, ya - depth, ZR + depth)), Vector((x, ya, ZR))]
        end = [Vector((x, front - 1.414 * depth, ZR)), Vector((x, front - 0.414 * depth, ZR + depth))] if into_wall else [Vector((x, front, ZR))]
        if panel:  # the straight run in lengths of `panel`, each face mapped once (the plate's bolts at its corners)
            a, c = start[-1].y, end[0].y
            k = max(1, round((c - a) / panel))
            start += [Vector((x, a + (c - a) * i / k, ZR)) for i in range(1, k)]
        Sweep(b, start + end, Z, profile, classes)

    spine_end = FrontAt(edge, 0.085) - SPINE_CLEAR
    for rear, front, wall in ((YR + PORTAL_DEPTH, spine_end, False), (YB, YR, True)):
        Beam(0.0, SPINE_UPPER, "DARK", 0.028, rear, front, wall)
        Beam(0.0, SPINE, SPINE_CLASSES, SPINE_DEPTH, rear, front, wall, panel=SPINE_PANEL)
    drop = Drop(b, spine_end)
    # the portal arch at the template's rear: a light chamfered beam up the walls, along the caps and across under the roof
    portal = [Vector((-XWALL, YR, -SEAM)), Vector((-XWALL, YR, ZH)), Vector((-0.30, YR, ZR)), Vector((0.30, YR, ZR)),
              Vector((XWALL, YR, ZH)), Vector((XWALL, YR, -SEAM))]
    Sweep(b, portal, Y, PORTAL, PORTAL_CLASSES)
    # the stringers along the wall-ceiling corners: on the caps' sloped face, from 1 cm inside the portal to the A-pillar's
    # top, where each slopes up into the cap at 45 degrees (it runs under the corner plate, so ended square it hung there)
    a, c = Vector((XRAIL - CAP[3][0], 0, ZH + CAP[3][1])), Vector((XRAIL - CAP[4][0], 0, ZH + CAP[4][1]))
    mid, slope = (a + c) / 2, (c - a).normalized()
    out = Vector((slope.z, 0, -slope.x))
    out = out if out.z < 0 else -out  # out of the slope, into the room
    for mirror in (False, True):
        b.mirror = mirror
        Sweep(b, [mid + Y * (YR + PORTAL_DEPTH - 0.01), mid + Y * (YP_ + 0.03)], out, STRINGER, STRINGER_CLASSES,
              ends=(None, (mid + Y * YP_, (Y + out).normalized())))
    b.mirror = False
    for mirror in (False, True):
        b.mirror = walls.mirror = mirror
        # behind the panes: the side wall (the rear section's in the concept's wall language, SideWall; a panel set in
        # behind the portal), the rail and cap carried back to the rear wall
        SideWall(b, YB, YR)
        Slab(b, [(XWALL, YR, -SEAM), (XWALL, pane_rear, -SEAM), (XWALL, pane_rear, ZH), (XWALL, YR, ZH)], -X, SLAB, inset=(0.008, 0.006, 0.02))
        Sweep(b, [Vector((XRAIL, YB, ZH)), Vector((XRAIL, pane_rear + OVERLAP, ZH))], Z, CAP_SWEPT, CAP_CLASSES)
        # the side wall behind the pane (cut below for its window), on back behind the rear wall and up behind the cap,
        # which close the corners round the pane's rear end
        Slab(walls, [(W, YB, -SEAM), (W, YP_, -SEAM), (W, YP_, ZR), (W, YB, ZR)], -X, SLAB)
        # behind the A-pillar: the step in to the pillar zone, and the pillar zone's wall up to the sill (higher, outside
        # the new pillar's glass, it stood in the view out through the front side window)
        Slab(b, [(XP, YP_, -SEAM), (W, YP_, -SEAM), (W, YP_, ZH), (XP, YP_, ZH)], -Y, SLAB, "DARK")
        Slab(b, [(XP, YP_, -SEAM), (XP, YC, -SEAM), (XP, YC, ZL), (XP, YP_, ZL)], -X, SLAB, "DARK")
        # the front chamfer below the ledge
        p0, p1 = Vector((XP, YC, 0)), Vector((XF, YF, 0))
        Slab(b, [p0 - Z * SEAM, p1 - Z * SEAM, p1 + Z * ZL, p0 + Z * ZL], Vector((p0.y - p1.y, p1.x - p0.x, 0)), SLAB, "DARK")
        # the A-pillar and the front side window in front of it, from the pillar to the windshield's frame and from the
        # ledge to the frame line (Pillar)
        window = Pillar(b, plane, top)
        windows.append([Vector((-c.x, c.y, c.z)) for c in window] if mirror else window)
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
    RearWall(b)
    lights = Lamps(b, pane_face, corner, drop) if pane_face else []
    if console:
        Details(b, edge, console["desk"], console["screen"], console["bezel"], console["column"], spine_end)
    Decor(b, decals)
    bmesh.ops.remove_doubles(b.bm, verts=b.bm.verts, dist=1e-6)
    me = bpy.data.meshes.new("shell")
    b.bm.to_mesh(me)
    b.bm.free()
    o = bpy.data.objects.new("shell", me)
    bpy.context.scene.collection.objects.link(o)
    o.data.shade_flat()
    return o, windows, lights, {"windshieldTop": [round(top.x, 4), round(top.y, 4), ZR], "pillarTopY": YP_, "capFront": round(cap_front, 4),
                        "paneRear": round(pane_rear, 4),
                        "pillar": {k: [round(c, 4) for c in p] for k, p in zip(("foot", "knee", "top"), window[1:4])}}


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


def Escapes(tree, eye, lo, hi, windows=(), open_rear=True):
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
            key = "rear" if face == "rear" and open_rear else "leak " + face
            if key != "rear":
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

    left, right, console_m = Placements(pbm, cbm)
    tlo = np.array([min(v.co[i] for v in tbm.verts) for i in range(3)])
    thi = np.array([max(v.co[i] for v in tbm.verts) for i in range(3)])

    pieces = {"side-left": Placed("side-left", pane_src, pbm, left), "side-right": Placed("side-right", pane_src, pbm, right, mirrored=True),
              "console": Placed("console", console_src, cbm, console_m)}
    screens = Screens(pieces["console"], out)
    furniture, placed, pilot = Furnish(os.path.dirname(folder))
    # the panes' windows are open (their back skin is culled from inside): where, to cut the shell's walls behind them
    right, rim, area = PaneOpening(pieces["side-right"])
    wall = {"floor": [PaneFace(pieces["side-right"], y, 0.012) for y in FLOOR_STRIPS]}
    # the panes' own A-pillars go; the shell's take their place
    pillar = PillarEdge(pieces["side-right"])
    pane_cut = {k: CutPillar(pieces[k], WindowPlane(pillar), mirrored=k == "side-left") for k in ("side-left", "side-right")}
    decals, decal_image = DecalAtlas(out)
    shell, windows, lights, dims = Shell(pillar, (right, rim), wall,
                                         {"desk": DeskShelf(pieces["console"]), "screen": screens["centre"],
                                          "bezel": Bezel(pieces["console"], screens["centre"]),
                                          "column": Column(pieces["console"], screens["centre"])}, decals)
    # the light baked in: the shell's own texture (its classes' trims), then the console's and the panes' textures, lit
    # by the lamps and the screens' glow (the parts and the furniture cast shadows and bounce light)
    template.hide_render = True
    atlas = next(n.image for m in pieces["side-right"].data.materials for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image)
    inside = [(x, y, z) for x in (-0.22, 0.0, 0.22) for y in (-0.55, -0.36, -0.2, -0.04, 0.1) for z in (0.12, 0.3, 0.42)]
    rig = LightRig(lights + WindowLights(windows))
    screens_glow = [m for m in pieces["console"].data.materials if m.get("screen")]
    ScreenGlow(screens_glow, SCREEN_GLOW)
    bake = Bake(shell, atlas, TRIMS, TINTS, os.path.join(out, "shell.png"), [pieces[k] for k in ("side-left", "side-right", "console")] + furniture,
                inside, ambient=AMBIENT, pages=SHELL_PAGES, eye=EYE, decals=bpy.data.images.load(decal_image))
    bake["relit"] = {k: Relight(pieces[k], os.path.join(out, k + "-lit.png"), ambient=AMBIENT) for k in ("console", "side-right")}
    bake["lamps"] = {"power": LAMP_POWER, "fill": FILL, "screenGlow": SCREEN_GLOW, "ambient": AMBIENT, "lights": len(lights)}
    ScreenGlow(screens_glow, 0.0)
    for o in rig:
        bpy.data.objects.remove(o)
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
    box = ((-W - 0.03, YB - 0.03, -SLAB), (W + 0.03, 0.6, ZR + SLAB + 0.01))
    escapes = {"template": Escapes(ttree, EYE, tlo, thi), "parts": Escapes(parts_tree, EYE, *box),
               "hull": Escapes(hull_tree, EYE, *box, windows=windows, open_rear=False)}

    for name, o in pieces.items():
        Export([o], os.path.join(out, name + ".glb"))
    Export(list(pieces.values()) + furniture, os.path.join(out, "hull.glb"))
    reticle = AimReticle(out)
    Export([reticle], os.path.join(out, "aim-reticle.glb"))
    bpy.data.objects.remove(reticle)
    glass = Glass(windows, out)  # not in hull.glb: the previews and the probes look through open windows
    Export([glass], os.path.join(out, "glass.glb"))
    bpy.data.objects.remove(glass)
    every = [template] + list(pieces.values()) + furniture
    Outside()
    with tempfile.TemporaryDirectory() as scratch:
        before = Render([template], scratch, "template", every)
        parts_only = Render([pieces[k] for k in ("side-left", "side-right", "console")], scratch, "parts", every)
        after = Render(list(pieces.values()) + furniture, scratch, "hull", every)
        concept = next((f for f in sorted(os.listdir(folder)) if f.startswith("Enclosed") and f.endswith(".png")), None)
        if concept:
            Concept(os.path.join(folder, concept), [template], list(pieces.values()) + furniture, every, scratch, os.path.join(out, "previews", "concept.png"))
    Sheet([before[:4], parts_only[:4], after[:4]], os.path.join(out, "previews", "seat.png"))
    Sheet([before[4:] + after[4:]], os.path.join(out, "previews", "outside.png"))

    manifest = {
        "template": TEMPLATE, "templateBounds": [tlo.tolist(), thi.tolist()],
        "placements": {"side-left": {"file": PANE, "matrix": [list(r) for r in left]},
                       "side-right": {"file": PANE, "matrix": [list(r) for r in right], "mirrored": True},
                       "console": {"file": CONSOLE, "matrix": [list(r) for r in console_m]}},
        "shellConstants": {"W": W, "ZW": ZW, "XC": XC, "ZC": ZC, "YR": YR, "YP": YP, "XP": XP, "YC": YC, "XF": XF, "YF": YF, "ZL": ZL, "YT": YT,
                           "ZH": ZH, "ZR": ZR, "XRAIL": XRAIL, "XWALL": XWALL, **dims},
        "paneCutFaces": pane_cut,
        "scale": {"metresPerHullUnit": METRES_PER_UNIT, "status": "proposed 2026-11-02, the user to confirm"},
        "eye": {"hull": list(EYE), "fromChairMetres": list(EYE_FROM_CHAIR)},
        "pilot": pilot,
        "lamps": {"note": "the bake's lamps, for real lights in the game to match: centre and facing in hull units, size across "
                          "and along (hull units), linear colour, power (W in the bake's hull-unit scene)",
                  "list": [{"centre": [round(c, 4) for c in centre], "facing": [round(c, 4) for c in n], "size": [round(su, 4), round(sv, 4)],
                            "colour": [round(c, 3) for c in colour], "power": power} for centre, n, u, (su, sv), colour, power in lights]},
        "furniture": {"note": "each file's origin (a moving part's pivot) in hull coordinates, turned about z, scaled by "
                              "hullUnitsPerModelUnit; motion values in model units", **placed},
        "bake": bake,
        "screens": {"convention": "u to the pilot's right, v up; the glTF's v runs down (its texture's rows), so a render "
                                  "target shows upright and unmirrored", "corners": "bottom left, bottom right, top right, "
                                  "top left as the pilot sees them: the rectangle the UVs span (the panel, its opening's "
                                  "outline, may cut its corners)", "imageRows": "the share of its material's image the "
                                  "screen's UVs take, bottom first", **screens},
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
