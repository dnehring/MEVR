# SPDX-License-Identifier: MIT
"""Renders OBJ bodies (mevr-rig export) in headless Blender for tools/rig/preview-sizes.ps1: per body, the torso from the
front, the side and three quarters behind, flat grey, orthographic, the same framing for all.

  blender -b --factory-startup -P tools/rig/preview-render.py -- <out folder> <name>=<file.obj> ...

Writes <out folder>/<name>-<view>.png. The bodies are the player's game data: the PNGs stay local.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
out = argv[0]
bodies = [a.split('=', 1) for a in argv[1:]]
os.makedirs(out, exist_ok=True)

scene = bpy.context.scene
for o in list(scene.objects):
    bpy.data.objects.remove(o, do_unlink=True)
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x = 520
scene.render.resolution_y = 720
scene.render.film_transparent = False
shading = scene.display.shading
shading.light = 'STUDIO'
shading.color_type = 'SINGLE'
shading.single_color = (0.72, 0.70, 0.68)
shading.show_cavity = True
shading.cavity_type = 'WORLD'
shading.show_shadows = False
scene.world = scene.world or bpy.data.worlds.new('w')
scene.display_settings.display_device = 'sRGB'
try:
    scene.view_settings.view_transform = 'Standard'
except TypeError:
    pass

cam_data = bpy.data.cameras.new('cam')
cam_data.type = 'ORTHO'
cam_data.ortho_scale = 100.0          # cm: the torso, hips to shoulders
cam = bpy.data.objects.new('cam', cam_data)
scene.collection.objects.link(cam)
scene.camera = cam

CENTRE_Z = 118.0
DIST = 400.0
# The body faces +x (UE: x forward, y right, z up); the views' directions from the body to the camera.
VIEWS = {
    'front': 0.0,
    'side': 90.0,
    'back34': 225.0,
}


def place(yaw_deg):
    a = math.radians(yaw_deg)
    cam.location = (DIST * math.cos(a), DIST * math.sin(a), CENTRE_Z)
    d = Vector((0.0, 0.0, CENTRE_Z)) - cam.location  # look at the torso's middle
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


objects = []
for name, path in bodies:
    before = set(bpy.data.objects)
    bpy.ops.wm.obj_import(filepath=path, forward_axis='Y', up_axis='Z')
    new = [o for o in bpy.data.objects if o not in before]
    for o in new:
        o.name = name
        if hasattr(o.data, 'shade_smooth'):
            o.data.shade_smooth()
    objects.append((name, new))

for name, objs in objects:
    for other_name, others in objects:
        for o in others:
            o.hide_render = other_name != name
    for view, yaw in VIEWS.items():
        place(yaw)
        scene.render.filepath = os.path.join(out, f'{name}-{view}.png')
        bpy.ops.render.render(write_still=True)
print('rendered', len(objects) * len(VIEWS))
