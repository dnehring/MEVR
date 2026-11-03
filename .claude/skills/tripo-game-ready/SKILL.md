---
name: tripo-game-ready
description: Turns Tripo (AI-generated) .glb models into game-ready LE1 cockpit assets with headless Blender, from inspection through repair, splitting moving parts, decimation under measured tolerances and the UE3 package to the in-game check, every step judged by numbers and renders. Use when adding, fixing, reducing or packaging a Tripo or other AI-generated model, when a cockpit model looks wrong in the game, or before placing parts against a reference mesh (the hull).
---

# Tripo models to game-ready (LE1 cockpit)

The cockpit's models come from Tripo: `concept/assets/` (gitignored except `game-ready/` and the eleven source models the
packager reads, in git on Git LFS since 2026-11-03: the models' licence is settled, MIT, see game-ready's README and
`NOTICE.md`; the raw outputs, renders, working files and the package stay local). They reach the game through a chain of
stages. Each stage
reads the previous one's files and never writes them, writes its own folder with a `manifest.json`, and prints `RESULT`
lines. research/47 is the record ("Packaging our models", "Game-ready models").

| Stage | Tool | Writes | It's done when |
|---|---|---|---|
| Inspect | a throwaway bpy script in the scratchpad (below) | nothing | you know the meshes, materials, images, welded and boundary counts |
| Repair (watertight) | the outside repair pipeline; its report, recipe and ledgers are in `repairs/<date>/` | `repairs/<date>/<asset>/vNNN/Repaired.glb` (+ `.blend`), `CURRENT.json` | 0 boundary and non-manifold edges, winding consistent (`ASSET_REPAIR_REPORT.md`) |
| Moving parts | `tools/cockpit/detach_controls.py`, then `check_controls.py` | `detached/<asset>-<part>.glb`, `manifest.json` (pivot, axis, motion) | parts' triangles add up to the source's; no clash over the motion |
| Game-ready | `tools/cockpit/game_ready.py` | `game-ready/<file>.glb`, `manifest.json`, `previews/<file>.png` | every RESULT line within tolerance, locked vertices all kept, previews clean |
| Package | `tools/cockpit/package` (`build`) | `build/cockpit/BIOG_MEVR_Cockpit.pcc` + `.json` | `All checks passed` |
| In game | research/47 "Next run": `tools/console.ps1 "mevr cockpit ..."`, `tools/snapshot.ps1` | snapshots | the snapshot shows the model as the previews do |

"Newest" is one rule in all three tools (`detach_controls.Newest`, `Build.Newest`): the highest `vNNN` under
`repairs/<date>/<asset>/`, else the file beside `repairs/`. The packager prefers `game-ready/<file>.glb`, but only when
`game-ready/manifest.json` was made from the file that rule picks (its `sourceSha256`). Otherwise it stops: run
`game_ready.py` again, or build with `--raw` to package the repaired and detached models as they are.

## Running Blender

Headless, from the shell, always: no GUI, no Blender MCP for batch work (the Higgsfield bridge's `bl_*` tools and
mcp-for-blender drive a live GUI session and are for looking, not for the pipeline).

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' -b --factory-startup --python tools/cockpit/game_ready.py -- concept/assets
dotnet build tools/cockpit/package/CockpitPackage.csproj -c Release
dotnet tools/cockpit/package/bin/Release/net10.0-windows/cockpit-package.dll build --assets concept/assets
```

Filter the output with `Select-String 'RESULT|Traceback|Error'`: Blender prints a line per render. A test build that
must not replace the installed package goes elsewhere with `--out build/cockpit-<tag>/BIOG_MEVR_Cockpit.pcc`. The
package in the game changes only with the game closed (research/47 "What to do").

New scripts go in `tools/cockpit/` (SPDX `MIT` first line, a docstring with the command line, `RESULT` lines, a
manifest, never writing what they read). Throwaway inspection goes in the scratchpad.

## What a Tripo model is (measured 2026-10-31 to 11-02)

- **Triangle soup:** 3 vertices per triangle, unindexed. Weld by position first (`bmesh.ops.remove_doubles`,
  1e-6 of the diagonal); UVs and normals live on the corners and survive it.
- **Normalised to about one unit:** the joystick base is as wide as the chair is tall. Each model's metric scale is
  unknown and the actor's scale sets it in the game, so tolerances are a fraction of each model's diagonal, never metres.
- **One textured material:** a 4096² base-colour JPEG (the front cockpit adds a 2048² normal map and a
  metallic-roughness PNG). The texture has lighting baked in.
- **One-sided sheets with holes.** The repair closed them with an inward backing shell 0.0005 units behind the front
  (material `Authored inward backing`, no image; dark grey in the game), or with thin rims where sheets meet (the
  joystick's backing is only rims). Front and backing share junction vertices.
- **Many more welded vertices than positions:** the packager's weld (exact position, normal and UV) gave 2.6 to 4.7
  times the position-welded count on the repaired models.

## The game's side

- `EngineMaterials.EmissiveTexturedMaterial`: **unlit and one-sided.** Normals, normal maps and the texture's baked
  lighting change nothing on screen yet, and back faces are culled. Normal-map baking and delighting wait for a lit
  parent (research/47 "A lit parent, later").
- **16-bit indices, no base vertex:** at most 65,535 vertices a mesh after the packager's weld and LEC's tangent splits
  (+0.3 to 4%). Above that, `Build.cs` splits the backing off as `<name>_Backing` (the front cockpit before game-ready).
- **Import:** glTF (x, y, z) becomes (x, z, y) x 100. A mesh's origin is its glb's origin and the node's translation is
  ignored, so a moving part's vertices must be relative to its pivot. `detach_controls` makes them so; keep node
  transforms on export (`export_apply=False`, modifiers applied to the mesh first).
- **Colour on screen is the texture through the level's grade.** The material is unlit, so with the post-process
  chain off (`show postprocess`) the texels reach the screen as baked. With it on, each level's grade and tone curve
  apply. At Eden Prime that took about 35% off mid-greys, lifted blue and halved saturated warm colours
  (research/48 section 13). Judge colour in the game, measured at a fixed head pose; Blender's previews show the
  texture only.
- **Textures:** the base colour only, DXT1, `--max-texture` (4096), in `TEXTUREGROUP_Environment_1024` (`Build.TextureGroup`:
  all 13 mips resident in game, 2026-11-02). The models carry 1,600 to 1,970 texels per model unit.
- **Collision:** none (the probe's actor has it off). **LODs:** none; the cockpit is always within arm's reach.

## Decimation (game_ready.py)

Each surface is decimated on its own by edge collapse, with everything else locked. The front keeps the fewest
triangles whose deviation from its source stays within `--tolerance` (0.0005 of the diagonal: 99th percentile both
ways, maximum at most 4 times that). The backing uses `--backing-tolerance` (0.002). At a 25 px/degree headset and arm's
length that is under about 2 pixels. Result on 2026-11-02, 21 s for all six:

| Model | Front tris | Backing tris | Welded vertices | Worst view IoU |
|---|---|---|---|---|
| joystick-base | 10,582 → 9,746 | 886 (rims, untouched) | 17,364 → 15,147 | 0.9999 |
| joystick-grip | 10,530 → 7,896 | 710 (rims, untouched) | 15,118 → 10,139 | 0.9988 |
| throtle-control-base | 5,373 → 3,465 | 5,941 → 2,933 | 26,962 → 9,945 | 0.9978 |
| throtle-control-handle | 4,466 → 3,442 | 4,854 → 1,992 | 21,695 → 7,597 | 0.9943 |
| chair | 10,972 → 9,698 | 12,188 → 7,494 | 45,051 → 27,028 | 0.9979 |
| front-cockpit | 19,269 → 10,545 | 21,511 → 5,935 | 76,956 → 25,377 (one mesh now) | 0.9978 |

Overall 107,282 → 64,742 triangles and 203,146 → 95,233 welded vertices, with the base-colour bytes, watertightness
and the controls' clearance unchanged.

**Judge a run** by its RESULT lines, then `previews/<file>.png`. Its rows are the source, the result and their
difference (the colour difference x 4 in red where both cover, yellow where only one does). Thin red lines along
texture edges and silhouettes are sub-pixel shifts and fine. A red patch means the texture swam, and yellow beyond the
thinnest stalks means the silhouette moved: lower the tolerance for that model. Report the numbers; the user decides by
same-pose measurements, not by a look.

## Traps (each one bit, or nearly)

1. **Decimating front and backing together** collapses the 0.0005-unit rims first and fuses the shells. Lock the
   junction vertices and decimate each surface alone.
2. **An empty vertex group locks nothing.** With no vertex in any group, the mesh has no weights and the Decimate
   modifier collapses freely: the joystick lost 120 junction vertices. Skip a surface with no free vertices, and
   check the locked positions afterwards (`game_ready.py` refuses to write otherwise).
3. **Vertex groups live on the mesh in Blender 5.x:** remove the group before swapping in the decimated mesh
   (`DeformGroup 'free' not in object`).
4. **A modifier ratio is a share of the whole mesh.** With the backing locked, ratio 0.5 took the throttle handle's
   front from 4,466 to 256 triangles. Ratio = (total - mine + share x mine) / total.
5. **The glTF exporter rebuilds a packed metallic-roughness image** (a warning about "more than one shader node tex
   image"). The base colour's bytes come through unchanged; compare the base colour only, since it's all the packager
   reads.
6. **A backing crossing the front is invisible.** Both are one-sided, and where the front faces the eye the backing
   faces away. Verify with culled renders, not by worrying.
7. **Count welded vertices as .NET does:** -0.0 equals +0.0 there. Byte-exact counts come out dozens too high (chair
   45,107 against the packager's 45,051).
8. **Keeping Tripo's normals costs little:** the chair 11% more welded vertices, the front cockpit none. The vertex
   savings come from the triangles.
9. **Never cut faces when splitting parts:** the models hold parts as separate surfaces joined only at shared positions,
   so split by position-welded islands (`detach_controls.Islands`).
11. **`Mesh.materials.clear()` sets every face's material index to 0** (Blender 5.2). Read them with
    `polygons.foreach_get("material_index", ...)` first and set them back after the new slots are in; else per-face
    classes silently collapse to the first material (the hull's tints and lamps did, research/48 section 13).
10. **A numbered name goes in as a name and a number** (found by the in-game session, 89b4bfe). UE3 stores a trailing
    `_N` as instance number N + 1, so the game's packages hold `TEXTUREGROUP_Environment_1024` as the name
    `TEXTUREGROUP_Environment` with number 1025. Written through LEC as one name with number 0, the enum lookup failed:
    the texture read LODGroup 27, past the end of LE1's 27 groups (0 to 26), and kept only its 4x4 mip, so the whole
    model showed one colour. Give any enum value or name ending in `_<digits>` as `new NameReference(stem, N + 1)`, and
    check it in game (`mevr cockpit info`: LODGroup, RequestedMips, ResidentMips). The first constant,
    `TEXTUREGROUP_Vehicle`, isn't one of LE1's groups at all.

## Inspecting a new model

In the scratchpad, import, weld by position, and print as JSON per object:
- vertices (raw and welded), faces;
- triangles and area per material;
- boundary and non-manifold edges, and edges between materials (junctions);
- bounds, node transform, UV layers;
- custom normals present;
- each image's size and where it is linked.

Then add the model:
1. Repair it if it has boundary edges.
2. If it has moving parts, add it to `detach_controls.ASSETS`.
3. Add its file to `game_ready.MODELS` and to `Build.Models`, and run the chain.

## Placing parts against a reference mesh (the hull)

`tools/cockpit/hull_assemble.py` (research/48) builds the hull from `concept/assets/hull reconstruction/`. The full
mesh (`hull full -bad.glb`) is never shipped, but it is **close to the intended shape, only messy** (the user, 2026-11-02),
so the new shell follows it. The parts (console, side pane) are placed in it, and `assembled/` gets each piece,
`hull.glb`, `manifest.json` and `previews/` (with `concept.png`, the concept image beside the hull from its viewpoint).

- **Rebuild the template's envelope; don't box the parts in.** Take x, y and z sections, plus ray fans from inside.
  Pin the walls, chamfers, windshield slope and ceiling as constants with the date they were measured.
- **Windows are openings in light frames** (the user, 2026-11-02): never a flat wall where the concept has glass. A part
  may model its window as a flat solid; the shell's own windows are open, framed by a swept profile (light face, dark
  gasket lining the opening) with mitred corners.
- **Build the shell around a part's structure; don't set the part in front of it.** The frame line passes the pillar tops
  (below), and the top of the side panes' red-striped pillar is read from the part (`PillarEdge`: its light and red faces).
- **Replace a part's structural feature that doesn't land; don't adapt it.** The panes' own A-pillars ended in a wedge
  15 cm above the console ("it looks like just a hanging feature", the user, 2026-11-02). The shell's own pillar replaces
  each (`Pillar`, research/48 section 15): a profile with a red channel, swept from the cap through a knee to a shoulder
  that stands on the floor, collars at the joints, a web panel behind it, and the front side window's rear edge on its
  front edge. Every member ends on something, joined the way a fabricator would: a collar into the cap, a level cut on a
  bolted base plate with gussets, a block on the floor. A sleeve pushed into a block reads as a collision ("it didn't
  look like it was manufactured", the user); `Sweep`'s `ends` cut a member on a plane.
- **Check a new member against the parts it lands near**, not only by eye: `BVHTree.overlap` between the shell and the part
  (which shell faces cross it, at what height), and a height map of the part's top from rays straight down (where it is
  level, where a rim steps). The first shoulder looked fine in renders yet cut through the console's screen housing ("the
  base of the pillar looks to collide with the control desk", the user, 2026-11-02); the map showed a level ledge
  beside the housing to stand it on.
- **Cut a Tripo part by splitting and deleting:** split its faces on the cut's planes (`bmesh.ops.bisect_plane` on the faces
  near the cut), then delete the faces on the far side (`CutPillar`). The mesh is open, so an exact boolean keeps what it
  should remove and adds the cutter's walls, and the cut leaves open polylines that `triangle_fill` can't close: cover a cut
  end the room can see with a plate in the shell. A cut also moves the part's ends: re-derive every joint overlap from the
  cut (the cap's overlap fell from 15 mm to 3 and leaked).
- **Let the parts' frame set the roof, not the template's ceiling.** The template's ceiling (z 0.48) sat 2 cm below the
  panes' top rail (0.497 to 0.509) and 7 cm below their caps' inner lip (0.553), so it hid the top of every window frame:
  "the roof cuts into the window frames" (the user, 2026-11-02). Measure a part's frame in sections with each face's class
  from its texture colour (light, red, dark), then carry its profile on where the part ends: the panes' cap profile runs
  round the front along the side windows' tops and the windshield's header and back to the open rear, so one light line
  goes portal, rail, A-pillar, window tops. Nothing of the roof may hang below that line (the old ribs crossed the side
  windows); the spine and slats end where the cap profile meets the ceiling, sloping up into it.
- **Model with profiles, not boxes** ("everything looks very generic", the user, 2026-11-02). `hull_shapes.py`: sweep a
  profile along a path with mitred corners (frames, rails, the roof's edge, beams, the portal arch); set panels in with a
  chamfered step; chamfer floor plates over a dark base. Each face carries a class (plate, dark, light, red, seam).
- **Bake the shell a texture of its own** (`hull_shapes.Bake`): the game draws the cockpit unlit, so a bevel shows only
  if its colour differs. A fresh unwrap of the faces seen from inside, the classes' atlas trims, ambient occlusion within
  0.05 (the parts occlude too) and a point light in the cockpit's middle, so faces turned to the room read apart from
  faces turned away. 4096 square on OptiX: 20 seconds. Pick the device with `get_devices_for_type("OPTIX")`: the general
  device scan crashed in Intel's `ze_loader.dll`.
- **Overlap a joint with a part, just under its surface:** 15 mm into the part, 0.5 mm below it. The part's ends aren't
  straight (the panes' caps end 2 to 8 mm apart across their width), so butting leaves cracks that show the sky, and a
  coincident surface flickers. Start walls on the floor's base, below the plates' seams, not on the plates' top.
- **Hunt leaks with rays from several eyes where no window can be** (above the frame line, along the panes' length).
  4,000 rays from the seat missed a 4 mm crack that a 60,000-ray probe from five eyes found.
- **Check what the game draws, with back faces culled.** A Tripo part's window can be an opening backed by a skin that faces
  outward, invisible from inside. Probe and measure closure with rays that skip faces seen from behind (`Visible`), and
  sort escaping rays into windows, the open rear and leaks.
- **Use an exact boolean only on a simple closed piece** (the side wall slab), never on the whole shell: on overlapping
  slabs the solver drops faces it finds inside another slab (a wall vanished, and rays leaked under the windshield).
- **Place a structural part by its features, not its overall surface match.** The panes' interior fit matched the wall's
  general surfaces yet left their pillar and window 0.12 behind the template's. Sections at the feature showed it.
- **Judge a version against the concept from its viewpoint** (`previews/concept.png`) and against the template from the seat.
  The first version, a box with a raised ceiling and a flat front, was rejected on sight.
- **Fit the concept's camera to a part the concept and the hull share** (the console: Tripo made it from the concept), by
  its screens' corners. An AI concept is not one consistent space: a camera matched by eye to the room (walls, portal)
  and one fitted to the console disagreed by 0.23 in depth and 0.06 in height, and a feature read through each gave
  opposite answers (the windshield's top above the ceiling through one, at the frame line through the other). Read the
  front, near the console, through the fitted camera; say which reading a change rests on; judge the seat's view last.
- **Review in the user's live Blender** (the Higgsfield bridge, the user's choice on 2026-11-02): `tools/cockpit/hull_live.py`
  runs `hull_assemble`'s geometry inside the session in 1.5 s (no bake) beside the concept cameras, renders overlays with
  the concept and EEVEE views lit by the build's own lamp list. The script stays the build of record.
- **From the seat only ceiling faces turned down or toward the pilot show**, and a spine wider than the eye's offset never
  shows its sides: lamps go on undersides and on a face sloped toward the seat (the spine's drop panel). A ceiling member
  hanging within 20 cm of the eye fills the top of the view (the spine hung to 19 cm above it: cut to 29).
- **Measure what a pillar blocks** at eye level: rays from the eye in azimuth steps, back faces culled, the blocked bands
  before and after. When a window plane moves, walls built for the old one can stand outside the new glass and block
  the view through it (one did); cut only what a line of sight through the glass reaches, then probe for leaks
  (research/48 section 16).
- **Grow a part where the room allows, about the point that must stay put** (`CONSOLE_GROW`, research/48 section 17):
  list what bounds each axis first (walls across, the windshield's sightline up, the controls toward the seat). The
  console grew 1.03 / 1.2 / 1.2 about a point on the windshield's wall, so its top stayed on the sill's sightline. Find
  features on the placed part, never from constants, so they follow it. A non-uniform scale tilts a Tripo surface's
  small folds: the screens' 0.98 normal test left dark lines across a stretched screen. When a concept's room is
  "smaller around" a part than a seated eye allows, move the part instead: the console stood 0.1 nearer the seat
  (`CONSOLE_SHIFT`, section 18) and filled the room camera's view as the concept's, at no cost to the windshield.
- **Name a difference from the concept only after rays say what it is**, and **put a part where something sees it.** A
  light strut taken for the windshield's posts was the A-pillar's lower beam (rays from the concept's camera), which the
  concept has light too; a wall lamp stood behind the console's wing from every viewpoint (research/48 section 21).
  Before modelling a feature, cast rays along the wall or from the views that matter to find what is open there.
- **Lay out a seat and its controls from a seated body, not from inherited anchors** (research/48 section 22). Measure the
  chair (cushion height, the backrest's front at hip and eye height, the armrests) with rays, then check: the eye's gap to
  the headrest against the game's near plane (7 to 11 cm let it cut into the backrest in the game); shoulder to grip
  under an arm's 0.69 m with the elbow bent (0.66 m was an outstretched arm); the knees and heels clear of the console.
  Publish the eye, hip point and grip points for the game side, which seats Shepard and places the grabs from them, and
  a moving part's travel, which its grab's travel has to match.
- **Fit a screen's panel to the opening the eye sees, not to the model's screen faces** (research/48 section 20). Tripo's
  screen runs on under its bezel and folds unevenly, so a rectangle round its faces sat off-centre in the trim (19 and
  37 mm of bare screen either side, in hull units; the user saw it). Rays straight at the screen read the model's own
  texture: the opening is the dark cells not raised from the screen's plane, joined to the middle (dark paint runs over
  the raised trim at corners). The panel is that outline grown 1.5 mm under the trim, and the image's frame follows the
  outline. Check face-on, per cell: the panel over light trim paint, and dark model screen still showing.

- **Fit against the template's interior, never the whole mesh.** The interior is the faces hit by rays from points
  inside it. Use only the part's faces toward the room, and hold the scale to a plausible range. Against the whole
  template, the side panes sank into the wall at scale 0.30: a thin shell scores any small part well.
- **A better score can be the wrong place.** The right pane's own fit beat the left's (75% against 47% within 1 cm)
  at the wrong scale. Choose by an x-section or seat render against the template, and mirror symmetric parts.
- **Fit a part against its own region of the template** when the whole interior pulls it elsewhere. The console only
  fitted (0.752, 42% within 1 cm) against the interior faces in front, kept centred, from many starts.
- **Pin the chosen place as constants** with their provenance. `--refit` prints the candidates again: the best five
  agreed within 0.007 in scale.
- **Wind faces toward a stated direction** and reverse the winding on mirrored sides. Within a piece, share edges; make
  neighbours meet on exact lines (the windshield's corners on the chamfer walls).
- **Take the shell's surfaces from a part's own atlas:** find plain and bolted rectangles (a 32 px block scan for low
  variance, then a look). They are the bake's source, tinted per class, so the shell's tone stays the part's.
- **Measure closure with rays from the seat**, by the face of the box each escaping ray leaves through: only the rear
  should be open.

**The hull's parts skip the outside repair.** They are raw Tripo output (console 3,209 boundary edges, pane 526), yet
seen from inside they are closed: from 27 points in the cockpit, under 0.05% of rays meet a part from behind. The
repair's inward backing would face into the room behind the panes' outward-facing back skin and close their windows.
`game_ready.py` takes them from `assembled/` (a `MODELS` row's source can be a path), locking the open edges. The shell
goes to the packager as built. **Check the joints again after decimating:** cast the same rays with the raw and the
game-ready parts. A ray that escapes only with the game-ready ones is a crack. Two corners opened by under 1 mm and
needed the shell's walls to overlap further (research/48 section 9). The package carries the hull as `Hull_Shell`,
`Hull_Console`, `Hull_Side_Left` and `Hull_Side_Right`, on the hull's origin; the hull's forward is the package's -Y.

## Checked and not used (2026-11-02)

- **Higgsfield bridge's Blender skills:** scene building for shots. Its modelling module's geometry checklist and
  decimate notes agree with the above.
- **arjun988/blender-skills** (MIT): checklists without code. Bake numbers: cage 0.01 to 0.05 m, margin 16 px.
- **God-601/blender-mcp-master:** beta, unread scripts, and an interview gate that blocks batch runs.
- **ahujasid mcp-for-blender:** a live GUI bridge running arbitrary Python over an unauthenticated socket.
- **Tripo's own add-on and MCP:** generation only.
- **Tripo's API** (`/v3/mesh/decimate` retopology with quads and texture bake, `/v3/mesh/segment`): paid (10 to
  40 credits) and untested. Worth one comparison against `game_ready.py` on a model, by the same RESULT measures, before
  relying on it.

None replaces headless scripts that measure their own results.
