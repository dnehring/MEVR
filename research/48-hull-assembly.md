# 48: The cockpit's hull from Tripo parts and a template (2026-11-02)

**Question.** The Mako cockpit's hull exists as one rough Tripo mesh, `concept/assets/hull reconstruction/hull full -bad.glb`:
46,635 triangles, 118 islands, 3,650 boundary and 4,145 non-manifold edges. Two better Tripo parts exist: `front computer
console.glb` and `side pane.glb`, made from the concept images in the same folder. **Can the hull be rebuilt clean, close to the
template's shape and looking like the concept (`Enclosed Armored Sci‑Fi Cockpit.png`; a windowed version from the user), with the
parts in it?**

**Built:** `tools/cockpit/hull_assemble.py` (headless Blender 5.2, MIT; the `tripo-game-ready` skill's "Placing parts against
a reference mesh"). It writes `assembled/` beside the inputs (gitignored like all of `concept/`):
- each piece as a `.glb` in the hull's coordinates, and `hull.glb`;
- `manifest.json`;
- `previews/seat.png` (the template, the parts and the hull from the seat);
- `previews/outside.png`;
- `previews/concept.png` (the concept image beside the hull and the template, from its viewpoint).

`--refit` prints the side pane's candidate places.

**The user's correction of the first version** (same day): it boxed the parts in with flat walls, a flat front with a frame
for a screen, and a ceiling raised 7% onto the panes' tops. The front is meant to be windows, as in the concept. Windows may be
flat solid (that's how the parts model them). And the template is close to the intended geometry, only messy. So the shell now
follows the template.

**The user's second correction:** the side panes' slanted light pillars with the red stripe are the **A-pillars**. They
carry the roof and frame the windows, and the second version treated them as features. They ended in free tips in front of a
plain wall, and a small window sat beside them, framed by nothing. Section 6 is the fix.

## 1. The template's shape

Sections in x, y and z, and ray fans from inside it, give the envelope (x across, y forward, z up; the constants at the top of
the script):
- **The walls.** The side walls' inner face is at x ±0.463 up to z 0.412, then a chamfer runs into the ceiling at (±0.39, 0.48).
  The rear is open at y -0.425.
- **The plan is a hexagon.** The side walls step in to x ±0.39 at y 0.06: that's the zone of the red pillars, which the side
  pane brings. Front chamfers run from (±0.39, 0.175) to (±0.245, 0.374), with window holes in their upper half.
- **The windshield leans back about 56 degrees.** At x = 0 it runs from (y 0.328, z 0.388) to (0.252, 0.503); the shell takes
  it from the ledge (y 0.374, z 0.30) to the ceiling (y 0.267, z 0.48). Its top corners lie on the front chamfers.
- **The ceiling** has a lowered centre (half width 0.09, bottom z 0.408, y -0.2 to 0.05 in the template) and longitudinal ribs
  either side. The roof's outside is at 0.516.
- **The template has no floor.**

## 2. Placing the parts

Every Tripo model is normalised to about one unit across: the template is 1.0 x 0.85 x 0.516, the console 1.0 x 0.76 x 0.41,
the side pane 1.0 x 0.39 x 0.97. So each part needs its own scale, and the side pane is the left wall turned a quarter turn
about z (its room side faces -y as made).

| Attempt | Result |
|---|---|
| Fit against the whole template: a distance field (1 cm grid), a coarse search over scale and position with the distance relative to the part's size, then trimmed ICP (scale and position, rotation fixed) | **Wrong.** Both panes sank into the template's wall at scale 0.30, coming through the outside in a render. A thin shell scores any small part well. |
| The same against the template's **interior** only (the faces hit by rays from 18 points inside it: 4,483 faces, 2.91 square units), with only the part's faces toward the room, and the scale held to 0.40 to 0.65 | **The left pane at 0.595** matches the inner wall's octagonal window, slanted pillar and ledge in an x-section: 47% of its room side within 1 cm. **The right pane** went to 0.40 with a *better* score (75% within 1 cm), and the render shows it wrong. |
| The right pane | **The left pane mirrored.** The template's bounds are symmetric in x (±0.4998), and the two panes' whole-template fits mirrored each other within 0.003. |
| The console against the whole interior, then by rule (0.6, back at the front) | Stopped at the scale bound. Its screen face then sat 0.13 nearer the seat than the template's (y 0.16 against 0.29). |
| The console against the interior faces **in front only** (y > -0.05, z < 0.33, \|x\| < 0.42), centred, from 175 starts | **0.752** (42% within 1 cm), centre (0, 0.275, 0.147): the top at 0.30, as the template's ledge. The best scales spread 0.75 to 0.79. Its deep back passes behind the wall under the windshield, unseen. |

`--refit` reproduces the pane's place: its five best candidates agree within 0.007 in scale and 0.008 in position. The panes'
top caps (z 0.577) end above the shell's ceiling chamfer, hidden.

## 3. The shell (904 triangles)

Modelled from the constants. Every edge is shared, so the shell is closed except at the open rear:
- **Floor:** the hexagon in three slabs.
- **Ceiling:** at 0.48, with the lowered channel (plates underneath) and 4 ribs a side, dark as in the concept.
- **Side walls:** behind the panes, with their chamfers into the ceiling, the step and the pillar-zone walls.
- **Front chamfers:** plates below the ledge. Above it, a wall up to the windshield's edge with an **angled window** whose top
  follows that edge.
- **Windshield:** flat glass leaning back, in a light frame.
- **Under the windshield:** a wall behind the console.
- **Rear:** a beam under the ceiling at the open back.

Windows are flat dark panes, 0.003 units proud of their wall, in light frames 0.016 to 0.018 units wide (the hull's units have
no metric scale yet). The shell is tiled with the side pane's
own atlas (a bolted plate; light, dark and the pane's window glass), so it uses the pane's material and image, and the packager
would share the texture (it hashes the image).

## 4. Result

| | Box version (first) | Template-shaped shell |
|---|---|---|
| Template's interior within 2 cm of the hull | 55% | **89%** |
| Within 6 cm | 85% | **99.9%** |
| The shell's inside within 2 cm / 5 cm of the template's (area) | not measured | 52% / 79%; median 1.9 cm, p90 8.5 cm |
| Rays from the seat (0, -0.22, 0.36) leaving | only by the rear: 23.9% | only by the rear: 19.0% |

The template lets 15.4% out through its missing floor, 0.6% through the front and 0.9% through the sides. The shell's far
areas are the floor (the template has none) and the walls behind the panes.

**Look,** from the concept's viewpoint: the layout matches.
- **Front:** the windshield in its frame over the console's ledge, the angled windows between it and the red-striped pillars.
- **Ceiling:** the dark ribs either side of the channel.
- **Floor:** plates.

The whole room is lighter than the concept art, because the parts' textures are. The shell's plates are a repeated plate from
the pane, flatter than the concept's panels.

## 6. The A-pillars carry the roof and frame the side windows

- **The panes moved 0.12 forward.** The interior fit had matched the wall's general surfaces, not its features.
  - Sections at x 0.43 put the template's slanted pillar edge 0.10 to 0.13 ahead of the pane's: (y 0.055, z 0.51) to
    (0.23, 0.21), against (-0.08, 0.55) to (0.17, 0.12).
  - A two-way fit on the pillar-and-window band alone (right wall, z 0.25 to 0.48) put it 0.08 to 0.14 ahead. That fit's
    landscape is flat: its top eight results are within 0.002 of each other.
  - The pane's whole-wall match drops from 31% to 22% within 1 cm. The pillar and the octagonal window are now where the
    template has them, and the panes' rear ends leave 0.12 of bare wall at the open back.
- **The pillar is read from the pane:** its light and red faces above the ledge, in the pane's front 0.35 (`PillarEdge`).
  - Its foot is at (0.258, 0.221, 0.326), its top at (0.392, 0.082, 0.463).
  - The roof meets it at y 0.067, just behind its front face: the side wall, its chamfer into the ceiling and the step to the
    pillar zone end there.
- **The front side window is one glass** from the pillar to the windshield's frame, from the ledge to the roof. It is two flat
  triangles, because its corners aren't in one plane. It shares its edges with the chamfer wall below, the windshield's side
  edge and the ceiling's front edge. The pillar's foot lies forward of the glass's rear corner, so the pillar overlaps the glass:
  it is the window's frame on that side, with a light sill below and a light beam above.
- **The roof rests on the pillars:** a light beam joins the two pillar tops under the ceiling.
- **Light portal posts** stand at the open back, where the panes left the wall bare: the concept's foreground pillars.

**Result:** the template's interior is within 2 cm of the hull for 89.6% and within 6 cm for 100%. Only the rear is open
(19.1% of the rays from the seat). The shell is 884 triangles.

From the concept's viewpoint, each red-striped A-pillar rises from the end of the console's ledge to the roof beam, with the
dark side window between it and the windshield, and the panes' octagonal windows behind it. The panes' pointed pillar feet
stop just above the windows' sills: that's the pane's own shape.

## 7. Open windows (the user's third step: "convert the window sections into actual windows")

- **The side panes' octagonal windows were already open.** The pane has a back skin behind the octagon, about 0.42 out,
  whose faces point outward. From inside it is culled, so the game draws the octagon as an opening. The shell's plated side
  wall right behind it (x 0.463) filled it, so it read as a panel.
  - A ray probe along +x from the room that skips faces seen from behind (`Visible`, as the game culls them) finds the
    opening: 0.035 square units, y -0.207 to 0.021, z 0.246 to 0.422. Its front edge slants with the A-pillar. The pane's
    own frame around it reaches x 0.416.
  - A probe that counted every face found nothing open: facing matters.
- **The shell's side walls (and their ceiling chamfers) are cut there** with an exact boolean, and a light lining carries the
  pane's white frame from 0.416 out through the wall (`Reveal`).
  - **Only those walls go through the boolean.** Run on the whole shell, which is overlapping closed slabs, the exact solver
    dropped about 200 triangles it found inside other slabs. Among them was the wall under the windshield, and 0.4% of the
    seat's rays leaked out under the windshield. With the cut limited to the walls: no leak, 1,044 triangles.
- **The windshield and the front side windows have no glass now.** Each opening is lined by light bars through the shell's
  thickness: all round the windshield; the sill, the roof's beam and the pillar's edge for the side windows.
- **The closure check counts what the game would show** (back faces culled) and sorts each escaping ray: through a window,
  through the open rear, or a leak with the box face it leaves by and where.
  - Result: 4.3% through the windows, 19.1% through the rear, no leak.
  - The template, checked the same way: 15.4% through the floor, 3.1% through the front, 2.5% through the ceiling, 1.2%
    through the sides, 23.1% through the rear.
- **Previews** have a sky colour and a ground below the floor, so the openings read as windows. In the game, the world would
  show through them, or whatever the Mako's own model puts in front of the eye: research/47.

The template's interior is now within 2 cm of the hull for 84% and within 6 cm for 98%, less than before because its window
areas are open now.

## 8. The roof on the panes' frame, and the shell modelled (the user's fourth step)

The user: the roof cuts into the window frames, so the continuous shape doesn't show; and the features look generic, not
like advanced modelling.

**Why the roof cut the frames.** Sections of the placed right pane, each face classed by its texture's colour (light, red,
dark), and rays fanned from the room (`hull_assemble.PillarEdge` and section probes, 2026-11-02):
- **The pane's light frame goes higher than the template's ceiling.** Its rear post chamfers in to a top rail, a light
  band from (0.399, 0.497) to (0.394, 0.509) along y -0.31 to 0.03, and the A-pillar's top meets that rail.
- **Above the rail, the pane has its own cap:** a dark soffit rising inward from (0.394, 0.509) to (0.287, 0.540), then a
  lip up to 0.553 at x 0.286. The cap runs from y -0.308 to about 0.030; its front end varies 2 to 8 mm across its width.
- **The shell's ceiling was the template's, z 0.48,** with its chamfer from (0.463, 0.412) to (0.39, 0.48). The chamfer
  came through the pane's wall face under the rail (x 0.416 at z 0.456), and the ceiling hid the rail and the cap.
- **The ribs crossed the side windows.** The ribs hung 0.028 below the ceiling out to x 0.32 and forward to y 0.165,
  across the side windows' tops, and a light beam crossed between the pillar tops.

**What changed** (`tools/cockpit/hull_assemble.py`, with the modelling pieces in `tools/cockpit/hull_shapes.py`):
- **A frame line at the panes' rail (ZH 0.497) runs all round.** The shell sweeps the panes' cap profile (rail, soffit,
  lip) from each pane's cap along the side window's top to the windshield's header and across, and back from the panes
  to the open rear. The light line goes portal arch, rail, A-pillar, window tops, unbroken. Every window's top edge is
  on it; the windshield, its lean kept, now reaches it at (±0.330, 0.257).
- **The roof rests on the caps' lip (ZR 0.553):** 7 cm above the template's ceiling, 2.5 cm above at the rail.
- **Ceiling:** the template's lowered centre and ribs, raised with the roof, as a two-tier spine (its lower tier
  recessed underneath, its bottom at 0.465) and four louvred slats a side. Each slat has a face sloping toward the
  middle and a light foot. Every beam's ends slope up into the ceiling at 45 degrees, at the portal and at the
  ceiling's front edge, so nothing hangs below the frame line.
- **Openings:** the windshield and the side windows are framed by one swept profile with mitred corners: a light face
  12 mm proud, chamfered, stepping to a dark gasket that lines the opening through the wall. The side windows' frames
  run along the sill, up behind the pillar and along the top, and end inside the windshield's frame.
- **Rear:** a light chamfered portal arch at the open rear follows the walls, the caps and the ceiling. Behind the
  panes, a side wall with a panel set in.
- **Floor:** chamfered plates (corners cut 2 cm, top edge bevelled 5 mm) over a dark base, with 8 mm seams 12 mm deep.
  The walls start on the base, below the seams.
- **Its own texture.** The game draws the cockpit unlit, so a bevel is invisible unless its colour differs.
  `hull_shapes.Bake` unwraps the faces seen from inside (the rest share one dark texel) and bakes four passes on the
  GPU: the classes' trims of the side pane's atlas (tinted per class), ambient occlusion within 0.05 with the parts
  occluding, object normals and positions. It combines them with a point light in the cockpit's middle. The result is
  `assembled/shell.png`, 4096 square, 71% of its texels used, about 24 s with OptiX. Choosing the device by scanning
  all of them crashed Blender in Intel's `ze_loader.dll`; listing OptiX devices alone works.

**Joints.**
- **The first build leaked the sky at two joints.** Between the pane's cap and the shell's rail: the shell's copy began
  at the cap's outermost vertex, 2 to 8 mm past where the soffit ends in its middle. And at the pane's rear end. A
  60,000-ray probe from five eyes, counting only rays that cross the side above the frame line, found 8 to 13 per eye.
  The 4,000 rays from the seat had missed them.
- **The fix overlaps instead of butting:** the shell's copies run 15 mm into the panes, 0.5 mm below their surface, so
  neither a crack nor a flickering coincident face is left. Then no ray.
- **A 12 mm crack along the front chamfer wall's foot** (the walls started on the plates' top, the seams go below it)
  let one seat ray in 4,000 out. The walls now start on the base.

**Result:**
- **Rays from the seat** leave only through the windows (4.8%) and the open rear (22.6%). None leak.
- **Shell:** 2,113 triangles (1,044 before).
- **The template's interior** is within 2 cm of the hull for 69% and within 6 cm for 93% (84% and 98% before): the roof
  now stands above the template's ceiling on purpose.
- **From the concept's viewpoint:** a louvred ceiling converging forward around the spine, the red-striped A-pillars
  running into the light rail, framed windows and a plated floor.

## 9. Game-ready and in a package (same day, after the user approved section 8)

**No outside repair for the hull's parts.** The console still has 3,209 boundary and 3,509 non-manifold edges, the pane 526
and 872. But from inside they are closed:
- **Probe:** rays from 27 points in the cockpit (64,352 hits), taking the first face each meets without culling.
- **Result:** apart from the panes' windows, a part is seen from behind on 0.02 to 0.04% of them: console 23 rays, side
  panes 15 and 19, at their ends and the windows' edges. That is where the game would show what is behind it. The
  panes' windows themselves are 1.6% each, and meant.
- **Why skip the repair:** its method, an inward backing 0.0005 behind every sheet, would face into the room behind the
  panes' outward-facing back skin and close their windows. So the parts go straight to `game_ready.py`, which locks the
  vertices on open edges.

**game_ready.py** now takes the hull's placed parts from `assembled/` (its `MODELS` rows name a source: detached, the
newest repair, or a path). The shell isn't decimated: it is built low and clean.

| Part | Triangles | Packager's welded vertices | Deviation (of the diagonal) | Worst view's silhouette overlap |
|---|---|---|---|---|
| hull-console | 27,316 → 13,306 | 36,933 → 22,419 | p99 0.00035, max 0.00162 | 0.9986 |
| hull-side-left | 27,023 → 4,339 | 35,374 → 8,014 | p99 0.00050, max 0.00085 | 0.9965 |
| hull-side-right | 27,023 → 4,429 | 35,374 → 8,146 | p99 0.00050, max 0.00079 | 0.9922 |

The previews' difference rows show thin edge lines and a few slivers.

**The joints after decimation.**
- **Method:** rays from six eyes (40,000 each), once with the raw parts and once with the game-ready ones (the same
  shell); a ray that escapes only with the game-ready parts is a crack the decimation opened.
- **Found:** 1 to 4 such rays per eye, at two corners: the pane's rear end (the outer wall ended there exactly, and
  stopped at the frame line) and the caps' inner lip (the ceiling stopped at it).
- **Fixed in the shell:** the outer wall now runs back to the open rear and up to the ceiling, behind the cap; the
  ceiling reaches 15 mm over the caps.
- **Left:** the only rays that still escape with the game-ready parts alone pass window openings (a side window past
  the pillar, the edge of the pane's window). From the seat: windows 4.8%, open rear 22.6%, no leak.

**The package.**
- **Build.cs's `Models`** name a source per row too, and the hull is four meshes: `Hull_Shell`, `Hull_Console`,
  `Hull_Side_Left` and `Hull_Side_Right` (the two sides share one texture, `Hull_Side_Diff`). All four share the hull's
  origin.
- **Test build** `build/cockpit-hull/BIOG_MEVR_Cockpit.pcc` (37.9 MB): all checks passed.

| Mesh | Vertices | Triangles |
|---|---|---|
| Hull_Shell | 3,822 | 2,113 |
| Hull_Console | 22,676 | 13,306 |
| Hull_Side_Left | 8,215 | 4,339 |
| Hull_Side_Right | 8,339 | 4,429 |

- **Axes:** the shell's extents confirm the importer's axes. The hull's rear (y -0.425) is at Unreal y +42.5, so the
  hull's forward is the package's -Y, its right +X, up +Z.
- **Installed:** the in-game session installs and checks packages. In the same commit came its change of the textures'
  LOD group from APL_1024 (read as group 27, only the 4x4 mip resident) to Environment_1024 (research/47).
  - That only took effect in its 89b4bfe: written as the name `TEXTUREGROUP_Environment` with number 1025, as the game's
    packages store it (the skill's trap 10).
  - **In the game** (that session, fake headset, Eden Prime, ffa726d's build with that fix): every 4096 texture has all
    13 mips resident, and Hull_Shell shows its texture and the window frames from outside.
  - Nothing is placed around the player yet.

## 10. The console's screens (the user: the centre one shows the live third-person view; the others glow orange like the
Normandy's interface screens)

- **Found on the placed console** (`hull_assemble.Screens`): for each region (centre, left, right), the largest dark flat
  patch facing the seat, then every face in its plane within its bounds.
  - Centre: 213 faces, 0.350 x 0.123 hull units, vertical, facing the seat.
  - Left: 52 faces, 0.250 x 0.101. Right: 46 faces, 0.239 x 0.098. Both on the console's angled wings; the Tripo
    console isn't quite symmetric.
- **Own materials:** the centre on "Screen", the wings on "Display". Their UVs run 0 to 1 across each screen's rectangle,
  u to the pilot's right and v up. The glTF export turns v downward, as a render target's rows run, so a capture shows
  upright and unmirrored. `assembled/manifest.json` `screens` has each one's corners (bottom left, bottom right, top
  right, top left as the pilot sees them), normal, axes and size.
- **Idle look** (`hull_shapes.Display`, drawn for each screen's own shape on a 1024 x 512 image), judged against crops of
  the lit concept's screens:
  - a dark amber-brown field (sRGB about 0.16, 0.09, 0.05);
  - a thin orange frame with cut corners just inside the edge, with a narrow glow;
  - brackets at an inner frame's corners;
  - short lit bars with near-white cores on the edges: top and bottom for the centre, the bottom for the wings.
  The centre shows it until the mod puts the capture there.
- **Decimation:** `game_ready.py` now also locks every vertex between two materials, so the screens' outlines stay exact.
  Console: 27,316 → 13,676 triangles.
- **Package:** a texture per image now (`Hull_Console_Screen_Diff`, `Hull_Console_Display_Diff`, 1024 x 512), and the
  "Screen" material gets an instance of its own. `Hull_Console`'s slots: 0 `Hull_Console_MIC` (its atlas), 1
  `Hull_Console_Screen_MIC` (the live view: its `Texture` parameter takes the capture), 2 `Hull_Console_Display_MIC`.
  Test build: all checks passed, 36.2 MB.

## 11. The chair and the controls (the user: the asset needs the chair, the joystick and the throttle)

- **Scale, proposed (the user to confirm): 3 m per hull unit.** The cabin is then 2.5 m wide between the panes, the
  ceiling 1.66 m high, the frame line 1.49 m, the console's ledge 0.9 m. The in-game session's provisional 1.6 m cabin
  would have put the ceiling at 1.07 m, below a seated eye.
- **Arrangement:** Codex's (`concept/assets/cockpit/finish_20261031_v001`: `RuntimeBindings.json` and
  `Cockpit_Preview.glb`'s nodes), in metres from the chair's origin, each part turned about z as Codex turned it.
  - Chair: 1.22 m per model unit, turned +90 degrees.
  - Throttle and joystick: 0.29 m per model unit, turned -90 degrees, 0.58 m to either side, 0.25 m ahead, 0.54 m up.
  - Pilot's eye: 0.13 m behind and 1.21 m above the chair's origin.
- **Placement:** the chair's origin is at (0, -0.19, 0), its front clear of the console. That puts the eye at
  (0, -0.233, 0.403) hull units; the previews and the closure rays now look from there.
- **Pedestals:** the controls stand on chamfered columns in the shell, under each base's middle third.
- **For the mod:** `assembled/manifest.json` `furniture` gives each game-ready file's origin (a moving part's pivot) in
  hull coordinates, its turn, its scale (hull units per model unit) and a moving part's axis.
- **Closure from the new eye:** windows 4.6%, open rear 22.0%, no leak.

## 12. The rear section (the user: the contours continue to a rear section, as Codex's assembly did)

- **The cabin now runs on to a rear wall at y -0.62,** 0.195 hull units (0.59 m) behind the template's open rear. The
  portal arch stays at y -0.425 as a structural rib framing the rear section.
- **Carried back:**
  - the floor's plates;
  - the side walls, with a panel set in either side of the arch;
  - the rail and the panes' cap profile;
  - the ceiling with its plates;
  - a second run of the spine and the slats, from the rear wall to the arch. At the arch their ends slope up into the
    ceiling; at the rear wall they run on into it.
- **The rear wall** follows the room's section (the floor, the walls, the caps' profile, the ceiling) round a door
  0.28 x 0.48 hull units (0.84 x 1.44 m) with cut top corners. Panels are set in either side; a light frame (30 mm,
  standing 15 mm proud) lines the opening; two leaves with panels stand 15 mm back in it.
- **Closed:** from the eye, rays leave only through the windows (4.6%). The first build had a 4 mm slit between the
  leaves (0.05% of rays); now they meet.
- **Shell:** 3,055 triangles; the bake uses 72% of its texels.

## 13. Lit like the concept (the user: the interior should look very like `mako-interior-plain-front-2.png`)

- **Lamps, each a glowing panel in the shell and an area light for the bake:**
  - a fixture over the louvres on each side, and a smaller one over the rear section;
  - a strip on each pane's lower wall (its face measured with a ray);
  - an orange strip on the floor along each wall's foot;
  - small lamps on the spine's sides;
  - blue-white lights at the portal's feet;
  - a strip over the door.
  Plus a dim warm world fill, and the console's screens emitting their orange.
- **The light is baked in** (the game draws everything unlit). Into the shell's texture as direct and bounced light,
  times its occlusion, plus a small ambient (0.07 of the albedo) so the dark ceiling still reads, plus the lamps' glow.
  Into the console's and the panes' own textures too (`hull_shapes.Relight`): their images are the albedo, baked on
  their own UVs, which overlap on only 2-3% of the texels. The two panes share one re-baked texture; the lighting is
  symmetric.
- **The atlas leans blue** (mean sRGB 64, 64, 75), so albedo is multiplied by (1, 1, 0.86) before the warm light. Warm
  light on the blue-grey textures had turned them pink.
- **Bug found on the way: in Blender 5.2, `Mesh.materials.clear()` sets every face's material index to 0.** Since the
  first bake the per-class tints never applied: the classes only differed through their atlas rectangles. Bake now
  saves and restores the indices; the lamps' glow showed only after that.
- **Result:** with the furniture hidden, from the concept's viewpoint, the room is dark and warm: glowing ceiling
  panels, orange floor strips, the orange screens, light frames and red-striped pillars. The concept's finer detail
  (pipes, the spine's lamp clusters, the console's lit strips and keypads, glossy floor) isn't modelled yet.
  - Texture means (sRGB 0-255): shell 25, console 21, panes 24.
  - Each run's bakes take about 75 s on OptiX.
- **Package:** `build/cockpit-hull/BIOG_MEVR_Cockpit.pcc` (32.1 MB, exit 0).
- **In the game** (the in-game session, fake headset, Eden Prime, from the seat; grey level 0-255 of the left eye's
  world image, against the same regions of the lit concept):

  | Region | Lit concept | In game, 29a7c83 |
  |---|---|---|
  | Ceiling centre | 31 | 9 |
  | Console desk | 72 | 42 |
  | Light frames | 67 | 72 |
  | Lamp panel | 248 (p90) | 205, 194, 209 |

  The frames were on target. The dark surfaces were 1.7 to 3.4 times too dark, and the game's tone (Eden Prime's
  grade, presumably) turns the warm-white panels lavender (255, 239, 218 in the texture).
- **Lifted:** ambient 0.07 → 0.18 (what the dark surfaces keep), the screens' glow on the console 3 → 5, the ceiling
  lamps 1.1 → 1.4 W, and the lamps' glow warmer (1, 0.8, 0.55) against the game's cooling. Texture means (sRGB):
  shell 25 → 29, console 21 → 27, panes 24 → 31. Build: all checks passed, 33.0 MB.
- **Measured again** (the same head pose, the frame anchor identical; 49cd60c):

  | Region | Before | After the lift | Target |
  |---|---|---|---|
  | Ceiling | 8.1 | 10.3 | 31 |
  | Console desk | 41.8 | 50.7 | 72 |
  | Pane frame | 60.9 | 65.7 | 67 |

  The lamp's top 10% went from (205, 194, 209) to (204, 188, 190): no longer lavender, but not warm. The game's tone
  caps the panel's red near 205.
- **Why the ceiling barely moved:** that region is mostly the spine's recessed underside and the dark ceiling plates,
  whose albedo (the dark trim at 0.9, the recess at 0.35 of it) was too low for any fill to lift.
- **Next round:**
  - albedo of the dark surfaces up: tints PLATE 1.3, DARK 1.5, SEAM 0.6;
  - ambient 0.32;
  - the lamps' glow (1, 0.79, 0.28).
  The lamp colour is set against the game's shift at Eden Prime (red x 0.80, blue x 0.98) and is tuned to that
  level's grade: elsewhere it may read warmer. Texture means (sRGB): shell 37, console 33, panes 37. Build: all
  checks passed, 33.8 MB.
- **That round read darker, not lighter** (4287b1c): ceiling 10.0, desk 30.3 (from 50.7), frame 62.6, lamp mean
  70.7, its top 10% (125, 112, 79).
  - **Not the game's exposure:** the view outside through the windshield stayed at 78 in all three snapshots.
  - **The game's display darkens saturated warm colours.** The same lamp texel showed as (205, 194, 209) when it was
    (255, 239, 218), and (125, 112, 79) when it was (255, 230, 144). The yellow light also tinted every lit surface,
    so the desk and ceiling went olive and darker on screen, though their textures were lighter.
  - Where in the game's pipeline this happens (the level's grade, the tone curve, the mod's colour path) isn't known
    yet. Judge colour in the game, not in Blender.
- **Fourth round,** to separate the two changes: the second round's lamp colour (1, 0.8, 0.55), with the third
  round's albedo and ambient kept. Build: all checks passed, 33.8 MB.
- **Measured (2f58016):** ceiling 15.8, desk 57.4, frame 67.4, lamp as round two. The yellow was the whole cause of
  round three's drop, and the lighter surfaces and fill now show.
- **Where the colour goes** (the in-game session, `show postprocess` off and on at the same frame):

  | Region | Post on | Post off | On/off |
  |---|---|---|---|
  | Ceiling | 15.8 | 26.6 | 0.59 |
  | Desk | 57.3 | 83.8 | 0.68 |
  | Frame | 67.4 | 105.8 | 0.64 |
  | Lamp | 122.4 | 165.8 | 0.74 |

  With the post chain off, the lamp's top 10% is (255, 229, 194): the texel as baked. The material is unlit, so the
  texture reaches the screen unchanged, and all the darkening and the cool shift are **the level's grade** (its
  post-process volume: tint, desaturation, highlights and shadows) and the tone curve. That takes about 35% off
  mid-greys, lifts blue against red, and crushes saturated warm colours most. The mod's colour path is neutral on the
  fake headset (`[MEVR-COLOUR] profile auto (= neutral)`).
- **What the targets mean depends on a decision, the user's:**
  - Graded with the level, as now: tune each region to its target divided by the grade. At Eden Prime that is about
    ceiling 53, desk 106, frame 105 with post off; the frame is there. That holds for Eden Prime only; every level
    grades differently.
  - Kept out of the level's grade (the mod drawing the cockpit apart, the in-game session's side): the textures
    themselves should then match the concept, about ceiling 31, desk 72, frame 67 with post off. The frame would come
    down, not up.
  Tuning waits on that.

## 14. Richer: the chair's material language and the concept's smaller parts

The user, after round four (2026-11-02):
- the level's grading is to be considered for every level (the cockpit stays in it);
- the seats' materials look different from the interior and are much richer in detail;
- the interior still looks very plain.

- **The atlases were no source for detail.** The Tripo atlases are fragmented islands with their own shading baked in,
  so tiling crops of them would bring island edges with them. The chair's atlas shows what "richer" means: warm grey
  metal with chipped edges and rust specks, white armour with worn edges and stencil decals, orange and blue
  indicator lights. The pane's and the console's atlases are cleaner and lean blue.
- **Wear in the bake** (`hull_shapes.Worn`, on the shell's classes and on the console's and panes' own images before
  the light), from vector maths, map range and textures only:
  - a slightly warm balance (1, 0.98, 0.82);
  - paint value varied at low frequency (±12%);
  - convex edges chipped toward bare metal (0.3, 0.29, 0.27), where a 4 mm bevel's normal turns from the true one,
    broken up by noise. The first noise scale (60) chipped half of every edge, in dashes like stitching; now it is 30,
    thresholded to sparse patches;
  - small rust specks;
  - grime from the occlusion, as before.
- **The concept's smaller parts** (`hull_assemble.Details`):
  - a pipe on brackets along each side of the ceiling, by the caps' lip;
  - louvred vents on the rear section's side walls, beside the door and on the spine's sides (`Grille`);
  - a 4 x 3 keypad of orange keys on each desk shelf (`Keypad`; the shelf's top found on the console: `DeskShelf`);
  - lit orange bars on the centre screen's bezel above and below it, on the bezel's face 9 mm in front of the screen
    (`Bezel`, a ray).
- **Shell:** 4,809 triangles. Build: all checks passed, 34.6 MB.
- **Not done:** the chair and the controls still carry Tripo's own lighting, not the cockpit's lamps. Further
  detail: decals and markings, panel lines on the large panels, the spine's lamp clusters.
- **In the game** (acc4665, the in-game session; the same boxes and head pose):

  | Region | Eden Prime, post on | Eden Prime, post off | Normandy, post on | Normandy, post off |
  |---|---|---|---|---|
  | Ceiling | 15.4 | 26.0 | 18.7 | 26.0 |
  | Desk | 54.5 | 80.2 | 54.0 | 72.3 |
  | Frame | 68.0 | 106.8 | 78.9 | 116.4 |
  | Lamp mean | 123.4 | 167.3 | 138.6 | 192.2 |

  - The wear left the numbers almost where they were (the desk 3 darker).
  - The two grades (Eden Prime warm, the Normandy cold and blue) take 25-40% off each, the Normandy's a little less,
    and both shift the lamps cool alike. A Mako level couldn't be reached: by console it stops in its black arrival
    cinematic.
  - Even with post off the same surfaces differ between levels (desk 80 against 72). Perhaps height fog, which reaches
    unlit materials in the base pass [verify].
- **Seen from above the seat:** the worn bake reads on the console, but the chair (white and red, black fabric, high
  contrast) is still the richest thing in view against the room's low-contrast grey.

## 15. Our own A-pillars (the user: the pillar "looks like it's just a hanging feature and not a true structural element")

- **What was wrong** (renders from the seat, across the cabin and under the foot; rays down from the foot):
  - the A-pillar was the side pane's own: Tripo's diagonal from the pane's top rail to a wedge;
  - the wedge stopped 0.05 (15 cm) above the console and 0.05 in front of the pane's side console, with the front side
    window's sky under and around it;
  - only its top joined anything (the pane's own rail, then the cap);
  - the front side window behind it was a twisted quad, from the chamfer at the sill to the side wall at the top: its top
    rear corner lay 0.056 off the plane of its other three corners.
- **The panes lose their pillar** (`CutPillar`):
  - everything in front of y 0.024 above the side console's top (0.346);
  - below that, the wedge's foot: in front of the shoulder's rear end (y 0.12) where it stands in front of the web, and
    from y 0.07 inboard of the side console's inner face (x 0.357);
  - 4,035 faces of each pane's 27,023 after the split;
  - the faces near the cut are split on its seven planes (`bmesh.ops.bisect_plane`), then deleted on the far side;
  - an exact boolean was tried first: on the open Tripo mesh it kept the pillar and added the cutter's walls;
  - the cut leaves open polylines, not loops (the pane is an open shell), so `triangle_fill` closes nothing. A dark end
    plate in the shell covers the post's and rail's cut ends, facing forward.
- **The front side window is one plane** (`WindowPlane`): through the windshield's side edge and the window's top rear corner
  (where the cap turns from along the pane to the windshield), normal (-0.921, -0.334, 0.200). Its sill runs from the
  windshield's corner back and in, 0.043 inside the chamfer wall at the foot, the old pillar's line.
- **The pillar** (`Pillar`) is that window's rear edge, in its plane:
  - from the top corner (0.399, 0.067, 0.497) down to a knee at (0.367, 0.11, 0.42), then flatter to its foot on the sill
    at (0.285, 0.265, 0.30): 35 degrees from vertical above the knee, 56 below;
  - a 65 mm (20 cm) wide chamfered profile with a 20 mm channel along its face: red in two lengths above the knee, as the
    concept's stripes, a plate below;
  - a collar where it goes up into the cap, a band round the knee, a boot where it stands on the shoulder;
  - behind it a web panel 18 mm behind the window's plane, from the pane's cut to the beam. It stands on the pane's side
    console, and in front of the old foot's cut it comes down to the sill;
  - the shoulder: a block from the floor to the sill (0.30) under the foot, on the console's outer flat top, its top's edge
    light and its long sides panels set in;
  - the window's frame runs along its sill and top; the beam frames its rear edge, the windshield's frame its front.
- **The console** (the user: "the base of the pillar looks to collide with the control desk"):
  - in the first build the shoulder stood 45 mm in from the sill and the beam 36 mm. Near the sill the window's plane passes
    only 2 to 3 cm outside the right screen housing's outer step (x 0.26 there), so the shoulder cut through the housing
    and the boot sat on its rim;
  - the beam is now 40 mm deep with 24 mm of it behind the plane, so its room face stands 16 mm in;
  - the shoulder's room face stands 20 mm in (x 0.265 at its front), on the console's outer flat, beside the housing;
  - the beam ends square at the sill inside the shoulder, its boot above;
  - check: no shell face crosses the console above z 0.20. The only overlaps are the shoulder's walls going down through
    the console's outer top to the floor.
- **The cap's overlap:** cutting the panes' caps at y 0.024 left the shell's cap 3 mm of overlap instead of 15, and rays from
  eyes in front of the pillar left through the joint. The frame line now starts OVERLAP behind the cut.
- **Leaks:** probed with rays from 19 eyes over the cabin's front and rear, 76,000 rays, back faces culled. The one left is a
  single ray at the rear wall's top corner, from an eye at the console's height, unrelated to the pillar.
- **Result:**
  - shell 5,597 triangles (788 more);
  - each pane 24,500 raw and 4,421 game-ready (from 27,023);
  - from the seat 4.5% of rays leave, all through the windows;
  - after decimating, the joints are unchanged from five of six eyes. The sixth gains two rays at the rear edge of the left
    pane's open window: an edge moved, not a crack;
  - package 34.2 MB, all checks passed;
  - not yet seen in the game.
- **Rounded edges were tried for the realism the user asked for next** ("mostly additive modeling, so it doesn't look real").
  A 3-segment, 4 mm bevel on every hard edge of the shell took it from 5,600 to 34,000 triangles and was hard to tell apart
  in lit renders. The additive look comes from flat bases with separate pieces stacked on them, not from sharp edges.

## 5. Not done

- **Not seen from the seat in the game:** the meshes load and draw textured (section 9), but nothing is placed around the
  player until the scale and the eye point are decided.
- **Not decided** (the user's call):
  - the metric scale (the in-game session assumes a 1.6 m cabin for now, about 190 Unreal units per hull unit,
    adjustable at run time);
  - the pilot's eye point and the seat;
  - which console screen shows the live third-person view: it needs a material slot of its own, which the console
    doesn't have;
  - the controls' place relative to the hull.
- **The tone:** the shell has a texture of its own now (section 8), tinted from the pane's atlas so it matches the parts;
  the concept's darker grade would mean darkening the parts too.
- **The roof's height** follows the panes, 7 cm above the template's ceiling. Keeping the template's height would mean
  scaling the panes down or cutting their caps.
- **Not placed:** the seat, and how the hull relates to `front-cockpit` (research/47).
