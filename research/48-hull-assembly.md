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

## 5. Not done

- **The parts are raw Tripo output:** the console has 3,209 boundary and 3,509 non-manifold edges, the pane 526 and 872, and
  27,000 triangles each. Before packaging they need the repair stage, then `game_ready.py`.
- **Not packaged and not in the game.**
- **The tone:** darker plates would need a texture of the shell's own.
- **Not placed:** the seat, and how the hull relates to `front-cockpit` (research/47).
