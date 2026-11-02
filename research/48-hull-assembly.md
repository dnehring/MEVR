# 48: The cockpit's hull from Tripo parts and a template (2026-11-02)

**Question.** The Mako cockpit's hull exists as one rough Tripo mesh, `concept/assets/hull reconstruction/hull full -bad.glb`:
46,635 triangles, 118 islands, 3,650 boundary and 4,145 non-manifold edges. Two better Tripo parts exist: `front computer
console.glb` and `side pane.glb`, made from the concept images in the same folder. **Can the template place the parts, and can
what they leave open be modelled, so that the result is one closed cockpit?**

**Built:** `tools/cockpit/hull_assemble.py` (headless Blender 5.2, MIT; the `tripo-game-ready` skill's "Placing parts against
a reference mesh"). It writes `assembled/` beside the inputs (gitignored like all of `concept/`):
- each piece as a `.glb` in the hull's coordinates, and `hull.glb`;
- `manifest.json`;
- `previews/seat.png` (the template, the parts and the hull from the seat) and `previews/outside.png`.

`--refit` prints the side pane's candidate places.

## 1. Placing the parts

Every Tripo model is normalised to about one unit across: the template is 1.0 x 0.85 x 0.516, the console 1.0 x 0.76 x 0.41,
the side pane 1.0 x 0.39 x 0.97. So each part needs its own scale, and the side pane is the left wall turned a quarter turn
about z (its room side faces -y as made).

| Attempt | Result |
|---|---|
| Fit against the whole template: a distance field (1 cm grid), a coarse search over scale and position with the distance relative to the part's size, then trimmed ICP (scale and position, rotation fixed) | **Wrong.** Both panes sank into the template's wall at scale 0.30, coming through the outside in a render. A thin shell scores any small part well. |
| The same against the template's **interior** only (the faces hit by rays from 18 points inside it: 4,483 faces, 2.91 square units), with only the part's faces toward the room, and the scale held to 0.40 to 0.65 | **The left pane at 0.595** matches the inner wall's octagonal window, slanted pillar and ledge in an x-section: 47% of its room side within 1 cm. **The right pane** went to 0.40 with a *better* score (75% within 1 cm), and the render shows it wrong. |
| The right pane | **The left pane mirrored.** The template's bounds are symmetric in x (±0.4998), and the two panes' whole-template fits mirrored each other within 0.003. |
| The console's fit | It stopped at its lower scale bound (0.6) and came out through the front by 0.055. The template's console is wider and lower than the model, so no single scale matches both. **Placed by rule instead:** scale 0.6, centred, on the floor, its back on the template's front (y 0.425). Its wing tips reach \|x\| 0.30. |

`--refit` reproduces the pinned place: its five best candidates agree within 0.007 in scale and 0.008 in position. At the pane's
place, the panes reach z 0.577, above the template's top (0.516). The new ceiling therefore goes on the panes (0.554): the cockpit
is 7% taller than the template.

## 2. What the parts leave open

Of the template's interior, the parts alone come within 2 cm of 31% and within 6 cm of 66%. What's open, in square units:
- the ceiling: 0.76;
- the front above the console, and the front corners: 0.56;
- the side walls around the panes: 0.27 and 0.33;
- the rear edge: 0.09.

The template has **no floor**: 15% of the rays from the seat leave it downward.

## 3. The fillers (1,034 triangles)

Modelled from the placed parts' measured bounds, all slabs 0.02 thick:
- the **floor**;
- the **ceiling**, with 7 transverse ribs and a lowered centre (0.035 deep, 0.18 wide);
- the **front wall**, with a light frame (0.48 x 0.23) around a dark inset where a screen can go: research/47's live screen;
- the **front corners**: prisms from the console's side to the wall, chamfered toward the seat, a light pillar face and a red stripe;
- the **side skins** behind the panes, which show above the panes' low front ends.

The room faces are tiled with a bolted plate cut from the side pane's own atlas (u 0.5066-0.5840, v 0.7207-0.8447, 317 x 508 px).
Light frame, red stripe and plain dark strips come from the same atlas. So the fillers use the pane's material and image: no
texture of their own, and the packager would share the pane's texture with them (it hashes the image).

## 4. Result

- **Coverage:** the hull comes within 2 cm of 55% of the template's interior and within 6 cm of 85%. The rest is where the design
  departs from the template on purpose: the higher ceiling, and the closed front where the template has a windshield.
- **Closed:** of 4,000 rays from the seat (0, -0.22, 0.36), only those through the open rear leave (23.9%). The template lets
  15.4% out through the floor, 0.6% through the front and 0.9% through the sides.
- **Look:** in the previews, the pieces read as one cockpit with the concept's layout. The fillers are plain repeated plates,
  flat beside the parts' detail.

## 5. Not done

- **The parts are raw Tripo output:** the console has 3,209 boundary and 3,509 non-manifold edges, the pane 526 and 872, and
  27,000 triangles each. Before packaging they need the repair stage, then `game_ready.py`.
- **Not packaged and not in the game.**
- **Design choices that are the user's:**
  - a closed front against the template's windshield;
  - the ceiling height;
  - the console's scale;
  - the open rear;
  - how the hull relates to the seat and to `front-cockpit` (research/47).
