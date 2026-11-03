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
  - a collar where it goes up into the cap and a band round the knee;
  - its foot (`Foot`) cut level at the sill and standing on a 5 mm base plate, bolted at the corners, with two gussets in
    the acute angle under it. A slanted sleeve round the beam's square end, pushed into the shoulder, came first: "it
    didn't look like it was manufactured, it just looks like it has another collision" (the user);
  - behind it a web panel 18 mm behind the window's plane, from the pane's cut to the beam. It stands on the pane's side
    console, and in front of the old foot's cut it comes down to the sill;
  - the shoulder: a block from the floor to the plate (5 mm under the sill) under the foot, on the console's outer flat top,
    its top's edge light and its long sides panels set in;
  - the window's frame runs along its sill and top; the beam frames its rear edge, the windshield's frame its front.
- **The console** (the user: "the base of the pillar looks to collide with the control desk"):
  - in the first build the shoulder stood 45 mm in from the sill and the beam 36 mm. Near the sill the window's plane passes
    only 2 to 3 cm outside the right screen housing's outer step (x 0.26 there), so the shoulder cut through the housing
    and the boot sat on its rim;
  - the beam is now 40 mm deep with 24 mm of it behind the plane, so its room face stands 16 mm in;
  - the shoulder's room face stands 20 mm in (x 0.265 at its front), on the console's outer flat, beside the housing;
  - the beam ends on its base plate at the sill's level (above);
  - check: no shell face crosses the console above z 0.20. The only overlaps are the shoulder's walls going down through
    the console's outer top to the floor.
- **The cap's overlap:** cutting the panes' caps at y 0.024 left the shell's cap 3 mm of overlap instead of 15, and rays from
  eyes in front of the pillar left through the joint. The frame line now starts OVERLAP behind the cut.
- **Leaks:** probed with rays from 19 eyes over the cabin's front and rear, 76,000 rays, back faces culled. The one left is a
  single ray at the rear wall's top corner, from an eye at the console's height, unrelated to the pillar.
- **Result:**
  - shell 5,837 triangles (1,028 more);
  - each pane 24,508 raw and about 4,450 game-ready (from 27,023);
  - from the seat 4.5% of rays leave, all through the windows;
  - after decimating, the joints are unchanged from five of six eyes. The sixth gains two rays at the rear edge of the left
    pane's open window: an edge moved, not a crack;
  - package 34.2 MB, all checks passed;
  - not yet seen in the game.
- **Rounded edges were tried for the realism the user asked for next** ("mostly additive modeling, so it doesn't look real").
  A 3-segment, 4 mm bevel on every hard edge of the shell took it from 5,600 to 34,000 triangles and was hard to tell apart
  in lit renders. The additive look comes from flat bases with separate pieces stacked on them, not from sharp edges.

## 16. Toward the concept, reviewed in the live Blender (the user: "do your best to match the concept")

The user's critique of the pillar work ("all of these repairs ... are not considering the entire structure as a whole"),
and their direction: work through the Higgsfield bridge and plan the design as one structure that matches the concept.

- **The live Blender.** The Higgsfield bridge drives the user's own Blender session (`bl_execute` runs Python in it).
  `tools/cockpit/hull_live.py` builds `hull_assemble`'s geometry there in 1.5 s (no bake) into `LIVE_Hull`, beside the
  concept's two cameras. It renders overlays with the concept, EEVEE views lit by the build's own lamp list, and the
  pillar zone's blocked angles. The script stays the build of record. The bridge first failed to authenticate: a
  Blender from 2026-10-31 still held its port with a stale secret; it was closed with the user's leave.
- **The concept's camera, two ways.** By eye against the room (walls, portal, floor): (0, -0.52, 0.25), 13 mm. Fitted to
  the console's centre screen (Gauss-Newton on its four corners; Tripo made the console from the concept, so it is the
  one object both share): (0, -0.292, 0.191), 13 mm, level, 1.2 px rms. They disagree. The concept's console is about
  1.38 times larger against its room than ours, so the concept is not one consistent space:
  - through the room camera, the concept's windshield reaches the ceiling and ours stopped at the frame line;
  - through the console camera, our windshield's side frames sit on the concept's dark posts, and its top was already at
    the concept's;
  - the user chose the taller front ("do your best to match the concept"), which also gives the pilot more view.
- **The front's top steps up to the ceiling** (`ShieldTop`):
  - the windshield's side edges carried on to ZR: top corners at (±0.355, 0.224, 0.553). They lie on the front side
    windows' plane too (its B and C are on the same edge line), so each front side window stays one plane;
  - the frame line (the panes' cap profile) now ends at each A-pillar's top;
  - a corner plate spans from there over the front side window: from the cap's end at the pillar's top (the rail's room
    edge and the caps' lip on the ceiling) to the windshield's top corner. A lamp sits on it, where the concept has its
    angled ceiling lamps;
  - the ceiling runs forward to the windshield's top edge;
  - the windshield's frame is dark with a light chamfer, the concept's slim posts; the side windows' frames stay light.
- **The ceiling, as the concept's.** The louvred slats are gone:
  - the spine's lower tier hangs 60 mm, not 88. At 88 it came within 19 cm of the pilot's eye and filled the top of the
    view; at 60 it clears it by 29 cm;
  - its recess floor is in bolted panels 0.15 long: each swept length maps the atlas's bolted plate once;
  - it ends square 3 cm short of the windshield's frame, over a drop panel whose rear face slopes 32 degrees toward the
    pilot, with a lit strip and two orange indicators: the concept's lit spine front;
  - from the seat only ceiling faces turned down or toward the pilot show, and the eye sits inside the spine's width, so
    its sides never do. The spine's three lamps a side moved from its side faces to its underside;
  - conduits: three 19 mm pipes (12-sided) either side at x 0.148 to 0.2, mid-grey (a new class, METAL), on dark straps
    every 0.15. They end in flanges at the rear wall and the portal and rise into the ceiling short of the windshield.
    The old single pipe ran through the portal's beam;
  - lamps: the corner plates' (1.2 W), and wedge housings with their sloped face to the middle, over the cabin
    (0.8 W, y -0.27 to -0.13) and the rear section (0.8 W).
- **Slimmer A-pillars** (the user: "They would obscure a lot in our model"). Measured from the eye with rays every
  quarter degree, back faces culled:
  - before: at eye level the pillar zone blocked 43.75 to 66.5 degrees right of ahead (22.75): beam, web and the pane's own
    frame;
  - the beam is 40 mm wide, not 65 (12 cm). Its knee and foot sit 25 mm further back, so the front side window grows by the
    difference; the top joint stays;
  - the pillar zone's wall (x 0.39, from the template's layout) stood outside the new glass and blocked the view through
    the front side window's lower rear part: it now stops at the sill. Cutting the step at the pillar's top too let
    rays through the panes' windows out sideways (the leak probe), and no line of sight through the front side window
    reaches it, so it stays;
  - after: blocked 47 to 66.5 degrees (19.5); the pane's own frame accounts for the last 10;
  - a quarter window behind the beam, in place of the web, opened only a 1 degree slit at eye level (its frame filled the
    rest) and was dropped. The web stays, as the concept's dark panels behind its pillars;
  - checked against the console with `BVHTree.overlap`: nothing in the pillar's region crosses it.
- **Smaller changes:**
  - the controls' pedestals are 0.22 of their base either side, not 0.32;
  - the red is darker (tint 0.55, from 1.0). The concept's stripe is about (115, 7, 13) beside (158, 138, 147) on the
    pillar's light face; the game showed ours at (219, 14, 15). Baked and drawn unlit it is now (115, 3, 4).
- **Result:**
  - shell 7,168 triangles (5,837);
  - from the seat 5.8% of rays leave, all through the windows (4.5%: the windows are bigger);
  - the leak probe (19 eyes) finds only the one ray at the rear wall's top corner from section 15. Its first run found
    two real leaks: the cap, now ending at the pillar's top, was an open strip there (rays from near the console went up
    through it: it has an end face now), and the step cut above;
  - after decimating, the joints are as in section 15;
  - package 36.3 MB, all checks passed;
  - not yet seen in the game.
- **Not matched, the next lever (the user's call):** around the same console, the concept's room is smaller. Through
  the console-fitted camera its side walls close in at the console's ends and its A-pillars stand on them; ours stand
  well outside the frame. Matching it means a larger console in our room (about 1.38 times), or a narrower room.

## 17. The console grown (the user: "you can attempt to scale parts of the console asset")

- **What room there is.** The console can't simply be 1.35 to 1.38 times larger:
  - across, its wings already reach x ±0.376 where the walls beside them are at 0.39 (the side panes' consoles at 0.357
    overlap them already);
  - its back top (z 0.307) already sits at the windshield's sill. From the eye the sill line is 9.6 degrees down, and
    every centimetre the console rises hides more of the view ahead and down;
  - a uniform 1.35 about its foot at the windshield's wall puts its top at 0.415, above the pilot's eye (0.403);
  - so the concept's console-to-room ratio is out of reach for a seated pilot who sees out. Its camera sits at the
    screens' height, not a seated eye's;
  - toward the seat there is room: the wings ended at y -0.01, and the controls stand at -0.107, inboard of them.
- **Grown in parts** (`CONSOLE_GROW`, about a point on the windshield's wall at z 0.25):
  - 1.03 across, 1.2 toward the seat, 1.2 in height;
  - its top stays on the sill's sightline (9.2 degrees down from the eye, 9.8 before);
  - its foot sinks 5 cm under the floor plates;
  - its wings reach y -0.087, 8 cm nearer the seat;
  - the screens are 3% wider and 20% taller, and the screens, keypads and the bezel's lit bars follow, since they are
    found on the placed console.
- **The variants** (seat and room-camera renders in the live Blender; all clear of the controls and the chair):
  - 1.0 / 1.2 / 1.07;
  - 1.03 / 1.25 / 1.12;
  - 1.03 / 1.3 / 1.15: wrapped the room most in the room camera, but stretched the angled side screens past the concept's
    console shape (the camera refitted to the grown console's screens shows it);
  - 1.03 / 1.15 / 1.25;
  - 1.03 / 1.2 / 1.2: chosen.
- **Checked:**
  - the joystick, the throttle and the chair: no overlap (`BVHTree.overlap`);
  - the shell above z 0.2: only the windshield's sill frame against the console's back top (as before) and the bezel's
    lit bars, placed on it;
  - the pillar's region is clear;
  - from the seat, the console's top meets the windshield's sill frame cleanly. 184 vertices of its back edge lie a few
    millimetres past the glass line, under the frame.
- **The screens' faces** (`Screens`): the stretch tilted small folds in the side screens past the 0.98 normal test, and they
  showed as dark lines across the left screen. Every face in a screen's plane and rectangle now counts down to 0.85
  (32 degrees).
- **Not changed:** the controls keep their pedestals. The wings now pass them 6 to 9 cm outboard, but under a wing's
  inner edge is its screen, so a bracket to the wing would cross it.
- **The probes' last leak, found:** the rear wall's panel was inset on an outline that follows the caps' profile, which
  dips (not convex). The offset flipped a triangle at the wall's top corner, culled from the room, and 430 rays from a
  cabin eye left through it (exported and read back, as the probes see it). The panel now stops at the frame line and the
  wall above it is plain: 0 rays. The joints check after decimating had found one new ray through the same corner with
  the grown console.

## 18. Proportions, side walls and floor (the user: "focus on side wall, proportions, and floor. Only retopologize when you get to a state of confidence")

No decimation or packaging until the user approves the modelling.

- **Proportions.** Measured against the console, the concept's room is about 0.75 times ours every way: its pillar
  tops, ceiling and side windows sit lower, and its walls close in. What the seat allows:
  - **lower:** the eye at 0.403 leaves the ceiling (0.553) 45 cm above it. The concept's ratio would put the ceiling at
    about the eye;
  - **narrower** (tried live, 0.9 and 0.85 in plan, heights kept): the console already spans the room at the front, and
    its wings ran through the side walls;
  - **the console further back:** the camera matched to the room puts the concept's console about 0.2 nearer the rear
    than ours, and higher, standing free in the room. Moved 0.1 toward the seat (`CONSOLE_SHIFT`, before growing) and
    grown 1.3 in height:
    - it fills the room's width and height in the room camera, as the concept's;
    - from the seat it stands nearer (the centre screen 0.38 from the eye, 0.52 before);
    - its top still meets the windshield's sill (the sightline over it is 8.8 degrees down; the sill's 9.6);
    - its wings reach y -0.207, beside the controls (they are inboard of the wings): no overlap with the controls or the
      chair;
    - the price: its centre screen is 2.25 times as wide as tall (the concept's 2.8). Refitted to this screen, the concept's
      camera stands at (0, -0.468, 0.175).
- **Side walls.** The concept's walls carry bold light frames that follow the room's section, and pale beams along the
  wall-ceiling corners:
  - the portal arch is bolder: 65 mm proud and 70 deep, chamfered, with a dark groove down its face (`PORTAL`);
  - it moved forward to y -0.38, its front face against the panes' rear ends, so the panes' own light rear posts and the
    arch read as one frame. It stood at the template's open rear (-0.425);
  - a light stringer runs along each wall-ceiling corner on the caps' sloped face (`STRINGER`): from the arch to the
    A-pillar's top, where it slopes up into the cap at 45 degrees. Ended square, it hung under the corner plate. It meets
    two of the pane's own light posts at its cap. The side walls now go arch, stringer, A-pillar: one frame system;
  - the spine and the conduits start at the arch's new front face.
- **Floor.** The concept's floor is large flat plates with fine grooves:
  - now three runs: the middle 0.32 wide (the console's middle), the side runs to the walls, half a row out of step;
  - rows 0.166 long, 3 mm grooves, the corners cut 12 mm where plates meet, a 2 mm bevel;
  - they were 0.15 by 0.2 tiles with 8 mm seams, 20 mm corners and a 5 mm bevel;
  - each plate maps the atlas's bolted plate once, so the bolts sit at its corners.
- **The pillar zone's view** is unchanged by all of it: 19.5 degrees blocked at eye level.
- **The controls' stands** (`Stand`, the user: "do more modeling and then decimate on completion"). The plain chamfered
  posts became stands made as the console's legs are:
  - a base plate bolted to the floor at its corners;
  - a column, light at its chamfers and dark on its sides, with a groove down its front and back;
  - a mounting head under the control's base, which rests 3 mm into it (the only overlap with the controls).
  The concept has no free posts, but the controls are the user's.
- **Checked and left as they are:**
  - the side walls' near frames: the concept's bold frame behind each side window is ours too, the arch's leg against
    the panes' own light rear post;
  - the console's back now stands clear of the windshield's wall, below the sightline over its top;
  - the stringers meet each A-pillar's top in one node with the corner plate.
- **Result, decimated and packaged** (the modelling pass complete):
  - shell 7,340 triangles;
  - console 14,250 game-ready (27,316 raw), each pane about 4,450;
  - from the seat 5.8% of rays leave, all through the windows;
  - the leak probe (19 eyes): no ray escapes;
  - after decimating, the joints are as in section 15: two rays at the rear edge of the left pane's open window;
  - package 36.1 MB, all checks passed;
  - not yet seen in the game.

## 19. The screens as panels of their own (the user: the side displays "look like they may be a little distorted when I look at the edges of the display and look at the orange glow")

- **What it was:**
  - the side screens are slanted: their long edges tilt in their plane, as the concept's;
  - `Screens` fitted every screen to a level rectangle. On a side screen that rectangle took in the bezel's corners and,
    after the 0.85 normal test of section 17, the tilted faces round the screen's border;
  - the model's screen isn't one clean face either (ragged triangles round its border, folds across it);
  - so the orange idle image lay stretched over bezel corners and folds, with a jagged edge (close-ups of the baked hull
    from the seat, unlit as the game draws it).
- **Now each screen is a panel of its own:**
  - the seed patch (the largest dark flat patch) and the faces joined to it within 2 mm of its plane and 18 degrees of its
    normal;
  - the smallest rectangle round the region's outline, its long sides on the screen's straight edges, 1.5 mm larger each
    way (`SCREEN_TUCK`), so its edges go under the bezel. A principal axis came first: it followed the ragged border, tilted
    the left panel 3 degrees off the right's mirror, and put one corner over the bezel;
  - one quad 1.5 mm in front of the model's screen (`SCREEN_PROUD`; the model's folds rise at most 1.1 mm), its UVs 0 to 1;
  - the model's screen faces keep their own texture under it.
- **Result:**
  - the idle image's frame lines and glow follow each screen's edges, the two side screens mirror images (close-ups of
    the baked hull from the seat, unlit);
  - the centre screen, the live view's target, is level as before, its manifest corners the panel's;
  - the decimated console keeps each panel (2 triangles each);
  - the leak probe finds no escape, and the joints are as in section 18;
  - package 36.1 MB, all checks passed;
  - not yet seen in the game.

## 20. Each screen's panel in its trim (the user: "the actual display is not nearly centred within its own trim")

- **What it was:** section 19 fitted each panel round the model's screen faces. Those aren't what the eye takes for the
  screen: Tripo's screen runs on under the bezel at one end, folds back at the other, and is painted a darker band
  along some edges. Face-on (rays straight at each screen, the console's texture where they land), the committed panels
  left part of the opening as bare model screen:
  - left screen 5.3%, most of it at its inner end, while its outer end reached the trim;
  - right screen 6.3%, most at its inner end;
  - centre 8.8%, all round;
  - and the side openings aren't rectangles: their short sides lean and each has one cut corner (the outer top), so a
    rectangular frame in the image couldn't keep an even distance from the trim either.
- **Now (`Opening`, `Screens`):**
  - the opening is found as the eye takes it: the cells darker than the trim's paint (luminance 0.225 as stored: the
    screens about 0.18, the bands painted round them about 0.1, the trim 0.25 to 0.55), not raised more than 6 mm from
    the screen's plane, joined to the middle. Without the height limit the centre's opening ran out over the raised
    trim's dark top at its corners. A band of the screens' own shade alone missed the darker band at the side screens'
    ends;
  - the panel is the opening's outline (its convex hull, the raster's steps smoothed out within 1.5 mm) grown 1.5 mm
    under the trim. A rectangle round it lay over the trim's light bevel where the trim leans or cuts a corner;
  - each idle image's frames follow the outline at the design's insets, their corners cut as a rectangle's were, the
    brackets at the outline's corners (bends over 30 degrees) and the lit bar on the outline's bottom at the middle;
  - the side screens' images are stacked in the one `Display` texture (1024 x 1024, the left screen below), so the
    package keeps its materials; the live screen's UVs still span 0 to 1 over the rectangle round its panel (the
    manifest's `corners`; `panel` lists the panel's own corners).
- **Result** (face-on, the same check before and after):
  - bare model screen: left 0.6%, right 0.5%, centre 0.9% (a thin line under the centre's top lip);
  - the panel over light trim paint: a hairline along the edges, under 1 mm on average (the tuck over a lip that isn't
    raised there);
  - the panels' rectangles: left 0.293 x 0.116 (0.272 x 0.107), right 0.296 x 0.118 (0.262 x 0.110), centre 0.361 x
    0.161 (0.347 x 0.145); the centre's aspect 2.24.
  - rebuilt with the bake: the leak probe (19 eyes) finds no escape; decimated, the console 27,337 to 13,317 triangles,
    its panels whole (21 triangles, their vertices on open edges are locked); the joints as in section 18;
  - package 36.2 MB, all checks passed (the `Display` texture 1024 x 1024);
  - not yet seen in the game.

## 21. More of the concept's parts (the user: "Please continue on modeling")

- **The side walls.** The concept's lower side walls (a lamp in a framed housing under a ledge, a bolted panel, a skirting,
  white strips in the floor at their foot) stand beside its console's ends. Ours there are behind the console's wings:
  between the portal's front face (y -0.31) and the wings (from y -0.18 at desk height) only the pane's rear post and
  7 cm of its lower wall show (rays along +x). The lamp on the pane's lower wall (y -0.14) stood behind a wing. So:
  - the rear section's side walls (`SideWall`, y -0.62 to -0.38, each one plain panel before) carry the concept's wall:
    a skirting 8 mm proud with a light top chamfer, a bolted panel set in up to z 0.195, an amber lamp (0.15 x 0.022,
    the bake's "wall" light) in a dark housing on a framed plate, a ledge 30 mm proud with light faces at z 0.25, and a
    panel set in above with the vent;
  - two pale strips in the floor at the panes' foot in front of the portal, as the concept's (the orange strip along
    the wall ran under the wing); the lamp behind the wing is gone.
- **The columns beside the centre screen** carry the concept's lit 3 x 3 keypad near the top and a vent below it
  (`Column`: the flat face found by rays, the longest run of samples at one depth across, then up and down through its
  middle; the first try latched on a sliver before the column's bevel).
- **Vents under the spine's upper tier**, behind its lamps, as the concept's either side of its spine. From the seat the
  lower tier hides them; from the concept's lower viewpoint they show.
- **The windshield's frame is dark all round** (its one light chamfer is dark too). The light struts seen from the console
  camera, from the console's ends up to the ceiling's corners, are the A-pillars' lower beams (rays from that camera),
  which the concept has light as well; listed as the windshield's posts before, wrongly.
- **The corner plates' lamps** fit their plate now. The plate is a narrow triangle (sides 0.126, 0.171, 0.172), and the
  lamp placed by its middle hung 9 mm over the front side window's top edge. It is the largest rectangle along the
  plate's top edge 6 mm in from every edge: 0.099 x 0.022 (0.104 x 0.032). The concept's larger lamp doesn't fit there.
  Read through the console-fitted camera, the concept's lamps are at our corner plates.
- **The controls' stands** are dark with light chamfers, as the console's legs: with light faces they were the brightest
  thing in front of the console, where the concept has nothing.
- **Checked and left:** the floor already reads as the concept's (three runs, the side runs out of step, small cuts where
  the plates meet, bolts at their corners).
- **Result:**
  - shell 8,388 triangles (7,340);
  - from the seat 5.8% of rays leave, all through the windows; the leak probe (19 eyes) finds no escape;
  - decimated, the console 27,337 to 13,317; the joints as in section 18;
  - package 36.2 MB, all checks passed;
  - not yet seen in the game.
- **Not matched:** the concept's lamps and panels on the lower side walls beside its console. Ours there are behind the
  wings, and the rear section carries them instead.

## 22. The seat and the HOTAS for a seated Shepard (the user: the hull replaces the front cockpit; the seat "where it would make sense if sitting in the mako vehicle along with the hotas"; in VR Shepard sits in it, so the hands meet the real models)

- **`Front_Cockpit` is out of the package** (`Build.cs`, `game_ready.py`): the hull replaces it.
- **What the game showed** (d4d3373, research/47 "The hull of d4d3373 with its live screen"): over either shoulder the
  chair's backrest filled half the view, dark, with light triangles through it. The eye (Codex's anchor, 0.13 m behind the
  chair's origin) was 7 to 11 cm from the headrest: inside the game's near plane, which cut into the backrest and showed
  its inward backing. A head can't be there either.
- **Laid out for a seated adult in this chair** (the cushion 0.45 m high, the backrest reclined about 30 degrees; the chair
  keeps Codex's place and size, its front clear of the console):
  - **the eye** 0.10 m further forward (`EYE_FROM_CHAIR` (0, -0.03, 1.21)): 0.21 m in front of the headrest, hull
    (0, -0.2, 0.4033);
  - **the HOTAS on the armrests' front sections**, each base dropped onto the armrest's top (`Furnish`). Codex's stood 0.58
    m out, beyond the armrests: shoulder to grip 0.66 m, an outstretched arm. Now 0.50 m, the elbows bent just over the
    armrests (a stick figure solved to the grips, `scratchpad` review renders);
  - **the floor stands are gone**: nothing stands under the controls now;
  - **the legs** fit: the knees 0.51 m and the heels 0.63 m ahead of the eye, in the console's foot well (open to 1.0 m ahead
    of the eye under its middle).
- **`pilot` in the manifest**, for the game side to seat Shepard and place the grabs:
  - eye, hip point (H-point), knees, heels and both grip points, in hull units and from the eye in metres (right, up,
    forward: the mod's `leverOffset`/`stickOffset` convention);
  - the stick's grip (0.375, -0.415, 0.314) and the throttle's at neutral (-0.362, -0.446, 0.286), against the mod's defaults
    (±0.20, -0.55, 0.25);
  - the throttle model's travel either side of neutral, 0.055 m (the mod's `throttleTravelM` is 0.15: the hand would leave
    the handle), and the stick's twist limit, 45 degrees (the mod's full lock 42);
  - the eye's distance from the headrest.
- **The idle screens' fill about 2.5 times brighter.** Eden Prime's grade took the first fill, sRGB (41, 23, 12), to (25, 17,
  12), against the concept's (39, 26, 23).
- **The cockpit's outer size** at 3 m per hull unit (the four hull meshes' bounds): 2.96 m wide, 3.4 m long, 2.0 m high,
  for the game side to hold against the Mako's body.
- **Result:**
  - shell 7,944 triangles (8,388 with the stands);
  - the leak probe (19 eyes) finds no escape; decimated, the joints as in section 18;
  - package 29.2 MB (34.5 with `Front_Cockpit`), all checks passed;
  - in the game (research/47 "The hull of 5969f57"): over either shoulder the backrest shows its fabric and nothing through
    it (the near plane is 10 units, 0.10 m at the view's 100 units a metre); the idle centre screen reads (37, 26, 19) with
    post processing; the mod's `[Mako] leverOffset`/`stickOffset` are now `pilot`'s grips and `throttleTravelM` 0.055.

## 23. The aiming picture on the right screen (the user: "We do need a HUD to indicate aiming in game. I imagine the right display could also show the current aiming picture, since the center will show the mako in 3d person mode")

- **The right screen is a material of its own, `Aim`**, as the centre's `Screen`: slot 3 of `Hull_Console`
  (`Hull_Console_Aim_MIC` over `EmissiveTexturedMaterial`, `Hull_Console_Aim_Diff`), its UVs 0 to 1 over the rectangle round
  its panel. The slots: 0 the console, 1 `Screen` (the live third-person view), 2 `Display` (the left screen, idle), 3 `Aim`.
  The left screen's image is no longer stacked with the right's.
- **Its idle image is a gun sight** in the orange style (`Display(reticle=True)`: a ring, a cross open at its middle, a dot,
  range ticks), its frames following the opening as the others'. The live picture (the turret's view) and the reticle
  over it are the game side's: a capture replaces the image, so the image's sight shows only while there is none.
- **The screen is 2.51 times as wide as tall** (manifest `screens.right`: 0.2955 x 0.1176): about 768 x 306 for a capture.
- **The Mako's size** (the user): we only enter and leave the Mako through full blackout transitions, so the cockpit
  needn't fit inside its body; it has to feel real in the game. The game side's measurement stays a note.
- **Result:** rebuilt with the bake, no leaks, decimated (the panels whole: 8, 6 and 7 triangles), joints as in section 18,
  package 29.2 MB, all checks passed. The pilot's points are unchanged. In the game (research/47 "The aiming screen and
  the cockpit out of the captures"): `aimscreen 768 306` fills the right screen with a second capture.

## 24. Toward the game's own surfaces (the user, 2026-11-03: texture resolution, shading, detail, glass and lights; not a remodel of the console)

- **What the game shows** (the game side's snapshots at the headset's resolution, beside Eden Prime's own scenery): the
  layout reads as a cockpit; the surfaces don't. They are matte and soft up close, with no highlights, and the frame
  members show smeared streaks.
- **Texture resolution, measured** (texels per metre of surface, from the built meshes' UVs): the shell about 250 over all
  its faces (about 330 over the ones seen), the console 630, the side panes 990, the chair 2,000, the stick 6,760, the
  throttle 9,260. The headset resolves about 1,000 a metre at arm's length (about 18 pixels a degree) and 500 at 2 m. The
  shell is most of what one sees: 59 m² of it are seen, nearly all 1 to 2 m from the eye.
  - **The controls' textures are capped at 2048** (`Build.cs` `GroupMaxTexture`): 3,400 and 4,600 texels a metre left,
    16 MB freed.
  - **The shell bakes onto three 4096 pages** (`SHELL_PAGES`, `Bake`'s `pages`): the seen faces, cut by distance from the
    eye so that each page carries the same area weighted by 1/distance², each unwrapped on its own; one bake pass fills
    every page (each face's material bakes into its page's image).
  - **The trims repeat across each face at the atlas's own density** (`TILED`, `TRIM_DENSITY` 3,000 texels a hull unit,
    about 1,000 a metre), mirrored at each repeat so no seam shows. Fitted once to each face, a 32-texel strip of the atlas
    lay smeared across a metre of member. A bolted plate still maps its plate once (its bolts at the corners).
- **Detail** (`Decor`): a decal atlas (`tools/cockpit/decals.py`: drawn with PIL by the system Python, which Blender's
  lacks; Rajdhani Bold, SIL OFL; generic markings, none of the game's insignia) baked into the shell like the trims:
  - labels: VENT A2 and VENT B2 under the rear-section vents, panel numbers R-01, L-01, P-03 and P-04, CAUTION HIGH
    VOLTAGE, REAR HATCH, LIFE SUPPORT and COMMS RELAY under the rear wall's vents. Each side's text is placed on its own,
    never mirrored (it would read backwards);
  - hazard stripes on the portal posts' feet, in front of the hatch and across the portal's threshold;
  - grab handles up the portal posts, clamped cable bundles up the rear corners, and a switch bank with two orange
    indicators beside each rear-section vent.
- **Daylight through the windows** in the bake (`WindowLights`): a soft area light across each opening, just inside and
  facing the room (`WINDOW_LIGHT`, a cool white). The world outside lit the cabin through nothing before.
- **`Aim_Reticle`** (the game side's ask: a capture replaces the aiming screen's image, so the reticle has to be in the
  world): the idle sight (`RETICLE`, shared with the screen's image) as flat strips a fortieth of its width, one model
  unit across (97 package units), centred on the origin in the package's Y-Z plane, both sides, 252 triangles, an orange
  texel on `EmissiveTexturedMaterial`.
- **The trims' patches:** repeated, the old 32-texel strips streaked a member every 3 cm, so LIGHT and DARK take plain
  square patches of the atlas now (320 and 224 texels, no panel edge in them, the mean colours of the strips: found by
  a search over the atlas for windows of that colour with no edges).
- **Result:**
  - the shell's pages: 1,340 texels a metre on its nearest 4.4 m², 860 on the next 17.5 m², 600 on the farthest 35.5 m²
    (about 330 on one page before); shell 9,020 triangles (7,944) with the detail;
  - the bake takes 169 s (three pages, 15 lamps and the windows' daylight);
  - the leak probe (19 eyes) finds no escape; decimated, the joints as in section 18;
  - package 28.0 MB (29.2), all checks passed; about 71 MB of textures resident (75);
  - not yet seen in the game.
- **Shading, glass and the lamps' glow** wait for which of the game's own materials can carry them (section 25).

## 25. The game's own materials for shading, glass and glow (a survey of LE1's packages, 2026-11-03)

- **LE1 can't run a new material**: there is no shader compiler in the game, and a material draws only if a shader map
  for its key (its resource, static switches and normal-map compression classes) is in a loaded cache; else the default
  material. An instance can only change a parent's scalar, colour and texture values; a static permutation (313 of the
  game's 1,043 instances over its main environment material carry one) we can't make. So every look comes from an
  existing parent. Sources: the game's packages read with LegendaryExplorerCore (a scratch survey tool: shader maps by
  `ShaderCacheManipulator`/`RefShaderCacheReader`, base pass pixel shaders disassembled, 1,473 level packages scanned for
  static-mesh use), `BIOEngine.ini`, ME3Tweaks on porting shaders.
- **Every base material in the global packages has its shaders in an always-loaded cache** (Engine.pcc's, 711 shader maps:
  all 664 base materials of Engine, Startup_INT, SFXGame, BIOC_Materials, SFXWorldResources and SFXVehicleResources).
  What a parent needs is that its package is in memory before ours, which `RequireImportsAlreadyLoaded` demands:
  Startup_INT is (the ini's startup packages); BIOC_Materials is in LegendaryExplorer's list of packages safe to import
  from and is imported so by 278 level packages, but what loads it wasn't found [verify in the game, at the main menu
  and in levels]; SFXWorldResources and SFXVehicleResources aren't loaded in BIOA_STA00.
- **Candidates:**

  | For | Parent | Package | Notes |
  |---|---|---|---|
  | lit, with our baked light kept | `BIOG_APL_MASTER_MATERIAL.APL__MASTER__Mat` | BIOC_Materials | Phong; Diffuse, Normal (BC5, xy), Specular, SpecularPower, Emissive, EmissiveColour, CubeMap; masked (clip at 0.333); 1,390 static meshes in 253 levels, 941 with a light environment |
  | the same | `EngineDebugMaterials.TexturePaint_2Tex_Color` | Startup_INT | Phong; Diffuse, Normal (xyz), Spec, Emissive, SpecularMultValue, SpecPower, EmissiveMultiply; used in no level |
  | glass | `BIOG_Humanoid_MASTER_MTR_R.Human.HMF_VSR_MASTER_MAT` (visors), `BIOA_JUG00_T.JUG00_GLASS01` | BIOC_Materials | translucent; the visor's opacity and a fresnel; JUG00's lit, two-sided, on 74 static meshes |
  | glow above 1 | `EngineBuildings.ProcBuildingSimpleMaterial` | Startup_INT | unlit, two-sided: DiffuseTexture x LightTexture x LightmapBrightness x WallSpecColor |

- **In the package now, optional** (nothing points at them; the package's listing has each mesh slot's `slotVariants`):
  every element's `<instance>_Lit_MIC` over TexturePaint_2Tex_Color (our image as its emissive, so the baked light
  stays; a dark 4x4 diffuse and a grey 4x4 specular, SpecPower 32, so the light environment adds highlights on the
  geometry's own normals); the shell's lamps and the idle screens' `_Glow_MIC` over ProcBuildingSimpleMaterial at
  LightmapBrightness 3 (over 1, so the game's bloom takes them). Only Startup_INT imports: no new load risk. The shell's
  lamps bake onto materials of their own now (`lamp`, `lamp2`, `lamp3` on the pages' images), so they swap alone.
- **Not yet:** the glass, and the main environment material, until BIOC_Materials is shown in memory before our package
  loads (a missing import fails the whole package). The glass needs pane meshes too.
- **To try in the game** (the game side): each part's elements swapped to their `_Lit_MIC` (do highlights show, and how
  does the light environment light an enclosed cabin); the lamps' and idle screens' `_Glow_MIC` (do they bloom).

## 26. Glass, and what the game showed of the lit and glowing instances (2026-11-03)

- **In the game** (59ae138; research/47 "Lit and glowing instances"):
  - **residency:** APL__MASTER__Mat, HMF_VSR_MASTER_MAT and JUG00_GLASS01 are in memory at the title (once a player
    exists), in Eden Prime and in BIOA_UNC20;
  - **lit:** renders (no checkerboard) and looks the same as unlit, within 0.2 of a level, no highlights. The parts are
    InterpActors with their DynamicLightEnvironment on; under the dark diffuse what light reaches them doesn't show;
  - **glow** at 3: the lamps' brightest 1% from (155, 122, 110) to (227, 210, 213), no halo yet;
  - **darker overall** than b486139 at the same views (ahead 56.0 to 35.1, the idle centre screen (37, 26, 19) to
    (15, 10, 8)). The packaged textures are the same as b486139's to a tenth of a level in every mean, including the
    screen's image, and the base instances unchanged: not the asset [verify on the game side: the two packages back to back
    in one session, and the post-process or exposure state].
- **Changed:** the lit instances' diffuse mid grey (0.3) and specular stronger (0.6, x1.6, power 24), so the game shows
  whether light reaches the parts at all; glow 4 (the game side's suggestion for a halo).
- **Glass** (`Glass`, `Hull_Glass`): each window's outline as one pane facing the room (the windshield, the front side
  windows, the side panes' windows: the outlines the frames are built round, so a pane fills its opening), packaged on its
  own (not in `hull.glb`, so the previews and the probes still look through). Its instance `Hull_Glass_MIC` is over the
  helmet visors' `HMF_VSR_MASTER_MAT` (unlit, translucent, one-sided): `VSR_Opacity` 0.22 (half that face on, all of it at
  grazing angles), `VSR_CubeMap_Intensity` 0.4 (its default 10 is a helmet's shine), a faint grey `VSR_Colour_01`, the
  fresnel at least 0.15; `Hull_Glass_Alt_MIC` over the level glass `JUG00_GLASS01` (lit, translucent, two-sided, fixed
  frost) to try instead. The package imports from BIOC_Materials now (`Check.cs` counts it among the always-loaded
  packages, on the game side's finding; not yet checked at the main menu reached from inside a game).
- **The lamps** are in the manifest (`lamps`: centre, facing, size, colour, power), for real lights in the game to match the
  baked ones: the light environment alone brings the level's light, not the cabin's.
- **In the game** (66d3a38; research/47 "Window glass"): the package loads with the BIOC_Materials imports; the darkness
  doesn't come back at the same pose (the ceiling 37.2, the idle screen (37, 26, 19)), so it was that session's state;
  the visor glass at 0.22 reads nearly clear (the view 4% darker, no sheen face on) and the level glass's hexagonal frost
  bends the world (13% darker), too much for a windshield; lit at the retuned values is still identical to unlit (the
  light environment gathers almost nothing in the closed cabin); glow at 4 brightens the lamps, still no halo.
- **So:** the glass at opacity 0.32 and its cube at 1.0 (the game side's guess); the lamps' glow at 10 and the idle
  screens' at 2. In the game (83a375a): at 10 the ceiling lamps bloom, a soft halo round each, white in the middle (their
  brightest 1% from (220, 203, 205) to white, the lamps' mean grey 57 to 73): they read as lit lamps. The glass at 0.32 and
  1.0 is still nearly clear (the view (97, 74, 67) against (102, 76, 67) without it), no sheen in a still view.
- **Now:** the shell's lamps glow by default (their elements on `_Glow_MIC` at 10; the unlit and lit instances are their
  variants), and the glass's cube at 3, its opacity kept at 0.32 so the view stays clear. The lit instances stay in the package unused: lit shading needs lights inside the cabin (`lamps`), a
  question of their stereo cost that the game side takes up once the Mako drives.

## 5. Not done

(Brought up to date 2026-11-02, after section 21.)

- **What the game side relies on** (keep it, or change both sides together):
  - the package `BIOG_MEVR_Cockpit` and its mesh names: `Hull_Shell`, `Hull_Console`, `Hull_Side_Left`, `Hull_Side_Right`,
    `Chair`, `Joystick_Base`, `Joystick_Grip`, `Throttle_Base`, `Throttle_Handle` (`Front_Cockpit` is gone, section 22);
  - the frame: hull units times 100, forward on -Y, so a hull mesh takes yaw 90 (`tools/cockpit/place.py`);
  - `Hull_Console`'s material slots: 1 the live third-person screen (`Screen`, UVs 0 to 1, v = 0 at the top), 2 the left
    idle screen (`Display`), 3 the right, aiming screen (`Aim`, UVs 0 to 1, section 23). The probe's `element 1` puts the
    capture on the centre;
  - the manifest's `eye.hull`, `scale.metresPerHullUnit` and each furniture file's `origin`, `turnDeg`, `motion` and
    `hullUnitsPerModelUnit` (place.py); and `pilot` (section 22): where Shepard sits and where the hands grab;
  - the unlit `EmissiveTexturedMaterial`: everything seen is the baked texture, so a moved chair or lamp means baking
    again (the chair's shadow is on the floor).
- **In the game** (the game side): d4d3373 seen there (research/47 "The hull of d4d3373 with its live screen"): the live
  screen at `screen 768 343` (the screen is 2.24:1) fits its opening. Since then: the seat, the HOTAS and the brighter idle
  screens (section 22), to install again (research/47 "What to do", the game closed).
- **Still to do in the game** (the game side): Shepard seated in the chair in the Mako (the user, 2026-11-02: in VR we are
  the real model, the hands on the real controls) from `pilot`; the grabs at `pilot`'s grip points with the throttle's
  travel; follow the Mako with the cockpit (it stays where it was anchored); reach drivable Mako gameplay at all (no save
  or level load has yet); the Mako's own mesh and the chase capture against the cockpit [verify]; what the cockpit costs
  a frame in stereo.
- **The scale:** 3 m per hull unit proposed (the cabin 2.5 m wide inside, the ceiling 1.66 m, the eye 1.21 m over the
  floor). It has to feel real in the game; the cockpit needn't fit inside the Mako's body, which is only entered and left
  through full blackout transitions (the user, 2026-11-02). Judged in the headset.
- **The aiming picture** on the right screen (section 23): the turret's view and a reticle over it, the game side's.
- **Texture memory:** six 4096 DXT1 textures, all mips resident (`NeverStream`), about 64 MB. Not measured against the
  frame yet.
- **The tone:** the concept is darker and warmer; in the game the level's grade changes ours too (section 13). Tune it
  against in-game snapshots, not renders.
- **Modelling left against the concept:** its lamps and panels on the lower side walls beside the console (ours there are
  behind the wings, section 21); its larger corner lamps (the corner plates can't hold them); the centre screen's
  aspect (2.24 against 2.8, from growing the console for a seated eye).
- **The roof's height** follows the panes, 7 cm above the template's ceiling. Keeping the template's height would mean
  scaling the panes down or cutting their caps.
