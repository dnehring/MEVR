<!-- SPDX-License-Identifier: MIT -->
# Re-rigging characters: hair chains and soft-tissue bones

`mevr-rig` adds bones to a character's skeletal meshes, from the player's own game files, and weights the meshes to them:
hair chains (so a hairstyle modelled as one shell bends in several places) and soft-tissue bones (breasts and glutes for the
jiggle). The mod then puts the rigged meshes on the character and drives the bones (`game/Hair`, `engine/HairPose`). First
character: ME3's Miranda (2026-11-19; research/trilogy-hair.md "The re-rig in the game", research/trilogy-jiggle.md).

Re-rigged meshes are BioWare's meshes with our bones: built on the player's machine from a plan of ours (the owner,
2026-11-19), never committed or published. The plan files (`plans/`) hold only our numbers and names.

## Running it

Needs .NET (the tool is `net10.0-windows`) and Legendary Explorer's nightly in `mod-manager/` (the csproj finds it; GPL-3.0,
referenced by path, never copied; `NOTICE.md`).

```powershell
dotnet build tools/rig/Rig.csproj -c Release -o tools/rig/bin/out
$rig = 'tools/rig/bin/out/mevr-rig.exe'
# What a mesh holds: bones, LODs, chunks (rigid and soft vertices, bone maps); --bones lists the skeleton, --rest each
# bone's rest place in the mesh's space, --dump writes a LOD's vertices with their weights (CSV) for placing regions.
& $rig inspect <package.pcc> <mesh path or its end> [--bones] [--rest] [--dump out.csv [--lod 0]] [--game LE2|LE3]
# Build the plan's package (%LOCALAPPDATA%\MEVR\rig\<game>\<name>.pcc); it prints every step and checks before and after.
& $rig rig tools/rig/plans/le3-miranda.jsonc
# What a written package holds (exports, imports, the mesh's properties).
& $rig contents $env:LOCALAPPDATA\MEVR\rig\LE3\MEVR_Miranda.pcc
# Development install: CookedPCConsole plus a PCConsoleTOC.bin entry; the game's TOC kept and put back by -Uninstall.
tools/rig/install.ps1 -Game LE3
tools/rig/install.ps1 -Game LE3 -Uninstall
```

`{LE3}` in a plan is the game's ME3 `BioGame` folder (`--game-root`, `MEVR_GAME_ROOT`, else Steam's default), `{MEVR}`
`%LOCALAPPDATA%\MEVR`. Read a mesh from the package the game loads it from: a level package's cooked copy (Miranda's body
is in `BioD_CitHub_Miranda.pcc`), and the patch DLC's copy where one exists (`DLC_METR_Patch01` overrides several).

## A plan

`steps`, in order, all into the plan's one package (`out`). Each step copies `mesh` from `source` as `name`, adds `bones`,
weights `regions`, or generates `hair` chains; `bonesOf` adds an earlier step's generated chain bones unweighted (the body a
hair follows must carry them: ME3 poses a follower from its parent, so `game/Hair` simulates the hair on the body). See
`plans/le3-miranda.jsonc`.

- **bones**: `name`, `parent`, `at` (rest place in the mesh's space: +X forward, +Y the character's right, +Z up, game
  units), and the rest axes: `yaw` (the mesh's axes turned about up: soft-tissue bones, X out of the body, glutes 180),
  `toward` (X aims at a point), or neither (the parent's axes). Soft-tissue bones are found by name at run time:
  `Breast*`, `Glute*`.
- **regions**: a bone takes a share of each vertex inside an ellipsoid (`center`, `radius` per axis), the full `weight` in
  the inner `inner` fraction, falling smoothly to none at the edge, taken only from the `from` bones (the chest's for a
  breast, so the arms keep their weights; the pelvis's for a glute, so the legs keep theirs).
- **hair**: chains at `sectors` (name, azimuth round the `scalp` bone: 0 front, 90 the right, 180 back), each from `rootZ`
  (the hairline) down to its sector's lowest vertex with `joints` joints evenly in height at the hair's centre line (the
  centroid of the sector's vertices within `sectorWidthDeg` and `bandCm`), bones `<prefix><sector>_<n>` (the name must
  hold "hair": `core/ik/HairChain` finds chains by it). Every vertex is re-weighted: the two chains round the head its
  azimuth lies between (a gap wider than 100 degrees, the face, isn't blended across), the smooth chain blend along (each
  bone's weight a hat on its segment's middle), the scalp above the hairline within `blendAboveCm`.

## Placing regions

Dump the body (`inspect --dump`) and read the surface from the vertices: for breasts, the front's furthest point per
height and side (the apex's height and its distance from the middle); for glutes, the back's. A bone sits about at the
tissue's middle (half way from the body's core to the apex), the region's centre a little outward, its radii the mound's
half extents. Miranda's numbers and how they were read are in the plan's comments.

## Automatic placement

`"place": "auto"` in a step (or `mevr-rig place <package> <mesh> [--plan p --step id]`, which also prints the distance from
that step's hand-placed bones and regions) places the four soft-tissue bones and their regions from the mesh's own surface
(`Placement.cs`): LOD 0's triangles as a height field seen from the front for the chest and from the back for the hips (the
furthest x per 0.5 cm cell of y and z), averaged with its mirror image; per mound the apex (the furthest cell 2..12 cm off the
middle, in Chest2's height -15..+5.5 cm or Pelvis's -17..+5), its height from the field smoothed (0.75 cm chest, 1.5 cm hips),
its side the middle of the run across the apex's height within 1.5 cm of it (inner end no nearer the middle than 4 cm chest,
3.5 cm hips); the bone 7.5 cm (breast) or 7.0 cm (glute) inside the apex on Chest2 or Pelvis, the region's centre 3.5 or 4.25 cm
inside, the hand plans' radii, weights and `from` bones. Every distance scales with Chest2's or Pelvis's height over Miranda's
(140.4, 101.2). `tools/rig/check-place.ps1` checks it against the three hand-placed rigs (ME3 Miranda's suit and underwear
body, Liara): each centre within 1 cm on each axis (research/trilogy-hair.md "Automatic placement").

## Every female body

`tools/rig/census.ps1 -Game LE1|LE2|LE3` lists every `HMF_*`/`ASA_*` skeletal mesh in every package of a game and its DLC
(`mevr-rig census`, Idle priority) into `%LOCALAPPDATA%\MEVR\rig\census\<GAME>.csv` (local: the player's game data), with LOD 0's
vertex count and a hash of its vertices (a cooked copy in another package hashes the same), the bone names' hash and whether the
skeleton has Chest2 and Pelvis. `python tools/rig/females-plan.py <GAME> [--compare ...] [--list] [--write]` sorts it (bodies
are every family but hair, heads, headgear, visors, breathers and hats) and writes `plans/<game>-females.jsonc`: one step per
distinct body mesh (its object path in the game's memory; one mesh kept under two groups of its package is one step),
`"place": "auto"`, `"continueOnError": true`, into `{MEVR}/rig/<GAME>/MEVR_Bodies_<GAME>.pcc`, each mesh named `<mesh>_MEVR`
(`<mesh>_<vertices>_MEVR` where two meshes share a name: ME2 Jack's two `HMF_JCK_LGTa_MDL`). `standsFor` lists the paths the rig
stands in for; the build writes them to the package's index, `MEVR_Bodies_<GAME>.json` (`mevr-rig-index` v1: `meshes`, each
`path` and `name`), which the game reads to swap by path. A part of an outfit without a mound (ME2/ME3 Shepard's shoulders and
sleeves) is skipped; a torso gets breasts, legs glutes.

```powershell
tools/rig/census.ps1 -Game LE1                 # %LOCALAPPDATA%\MEVR\rig\census\LE1.csv
python tools/rig/females-plan.py LE1 --write   # tools/rig/plans/le1-females.jsonc
tools/rig/build-bodies.ps1 -Game LE1           # MEVR_Bodies_LE1.pcc + .json + colliders\, its log beside it (Idle priority)
python tools/rig/bodies-report.py LE1 --list   # per body: mounds placed, LOD 0 vertices per region, checks, spheres
```

Sizes and times (2026-11-19): ME1 38 bodies 7.5 MB, ME2 80 of 101 14.4 MB, ME3 116 of 143 22.1 MB, under a minute each
(research/trilogy-hair.md "Automatic placement and every female body").

## Sizes (the clubs' dancers)

The game grows a dancer's breasts and glutes (game/Hair, docs/trilogy-hair-plan.md section 12) by an amount `a` per region,
mostly outward from the body and a little down (`Sizes.cs`; the owner, 2026-11-20: grown the same every way they looked rounder
and higher). In the mesh's bind frame (x forward, y side, z up; "out" is +x for a breast, -x for a glute) about the bone's bind
place P, a vertex's share on the bone moves to P + G (v - P):

    out' = out (1 + a kOut),  y' = y (1 + a kSide),  z' = z (1 + a kUp) - a kDrop out'

| region | kOut | kSide | kUp | kDrop |
| --- | --- | --- | --- | --- |
| breasts | 1.0 | 0.45 | 0.15 | 0.18 |
| glutes | 1.0 | 0.35 | 0.30 | 0.15 |

(kDrop 0.25 drew a crease under the breasts at the upper end.) `mevr-rig sizes <bodies.pcc> --ref <package> <mesh> --write`
writes each rig's `breastMax` (= 1 + the largest breast `a`) into the package's index: its breast peak reaches ME3 Liara's
romance body's (ASA_ARM_LIAd) grown by a = 0.10 (the owner: up to 10% larger than hers), clamped 1.0..1.8; build-bodies.ps1
runs it after each build (LE3's package first: it holds the reference). Glutes a 0.10..0.16, a middle dancer 0.12.

    tools\rig\preview-sizes.ps1          # before/after sheets: %LOCALAPPDATA%\MEVR\rig\previews\sizes2-<GAME>.png, sizes2-all.png

renders a bare body and a dress per game (as the game made it, a middle dancer, the upper end, beside LIAd) with
`mevr-rig export <package> <mesh> <out.obj> [--breast 1+a] [--glute 1+a] [--breast-shape k,k,k,k] [--glute-shape ...]
[--fit <ref package> <ref mesh>]` and headless Blender. The pictures are the player's game data: local only.

## Dress fit (a skirt fitted under the glutes)

A dress hangs straight down from the glutes' widest point, so their shape (and the dancers' boost) never shows (the owner,
2026-11-20). A step with `"fit": "auto"` (every step of the generated plans; `"skirt"` forces it, `"none"` or the plan
script's `FIT_EXCLUDE` leaves it) is checked against the plan's `fitReference` (the game's bare HMF_ARM_NKDa: every female body
shares the skeleton) by `Fit.cs`:

- **A skirt** when its back comes in less than 2.5 cm over the 12 cm under the glutes' widest point (the reference's height),
  is closed in the middle 24 cm under it, and has no second surface inside (legs modelled under a coat: left alone).
- **The fit**: on the back half (the angle from straight back under 70 degrees, fading out at 110), from 10 cm above the
  glutes' widest point to 38 cm under it (fading in over the top 7 cm and out over the bottom 16: the hem and the front are the
  game's), each vertex moves along the line from the slice's middle onto the reference's convex hull at that height (the shape
  a cloth stretched round the hips and both thighs takes), scaled by the hips' width ratio, plus 0.5 cm of ease at the glutes
  growing to 2.5 cm 25 cm under them; at most 6 cm, never a vertex more than 8 cm from that shape (an arm, a hand). Every LOD;
  each moved vertex's tangent frame turned with its surface's normal, so the game's shading follows. Baked before the bones
  are placed, so the glute bone and region sit on the fitted shape.
- The build log says per step `fit: fitted under the glutes, ...` or `fit: not fitted, <why>`. Fitted on 2026-11-20: CTHc,
  CTHd and CTHg (and their `_old` versions) in all three games, and ME2's ASA_ARM_CTHa (Liara's dress in the cabin): 20 rigs.
  Every woman wearing one of those dresses gets the fitted shape, not only the dancers.

## Body spheres (the hair's collision with the body)

Every body the `rig` verb builds (a step without hair, unless its plan says `"colliders": false`) also gets its surface for the
hair simulation: `<out folder>\colliders\<mesh>.json` (`mevr-body-spheres`, version 1), next to the package. `Colliders.cs`
fits it from the mesh itself: for each LOD 0 vertex whose heaviest bone is the neck, Chest1/Chest2, a collar, a shoulder (and its
twists) or a breast, a sphere tangent to the skin there (its normal from the triangles, turned away from the bone's axis), its
radius 0.85 of the vertex's distance from that axis (2.5-7 cm), shrunk until no vertex of the body is inside it; spheres on an
inner surface (a high collar's inside, its normal toward the neck) left out; then the largest kept, a candidate only further
than 0.6 of the smaller radius from those. Each records its bone, centre and radius in the bind pose (game units) and the skin's
normal. `mevr-rig colliders <package> <mesh> [--out file]` fits one already built.

The game (`game/Hair` `SpheresFor`, `core/ik/BodySpheres`) reads `%LOCALAPPDATA%\MEVR\rig\<GAME>\colliders\<mesh>.json` for
a body it simulates hair on, binds the spheres to that skeleton and places them on its bones every pose; the hair chain
(`ik::hair::Input::surface`) keeps its points out of them by the capsules' rule: out on the rest point's side, never further than
the point's own animated place sits from each sphere. With spheres, the body's spine, neck and collar capsules stand down (the
skull and upper arms stay). Why that rule: the rigged hair's bones run inside its volume, 3-5 cm under Miranda's skin at rest,
and the authored mesh doesn't clip there; what clips is a strand the simulation moves deeper than its pose (research/trilogy-
hair.md "The hair through the body").

Measured with `mevr-hair-bench --spheres <mesh.json> [--surface 0|1]` (2026-11-19, Miranda's underwear body): how much deeper
than its pose the hair sinks, mean / peak / steps past 5 mm, the walk 5.1 / 43.6 mm / 40% without them, 1.7 / 11.1 / 4% with,
as calm (jitter 0.7 mm, settled 0.22 s). Her suit (`HMF_ARM_MIRa_MDL`) has `"colliders": false`: its high collar's inside held
the nape's hair (the walk's hair never settled, worse than without).

## What the tool edits (LE's skeletal meshes)

- Per LOD one or more chunks; a chunk's vertices are a range of the GPU skin buffer, rigid (one bone, stored at weight 255)
  first, then soft (up to four). Bone bytes index the chunk's `BoneMap` (at most 75 bones a chunk: UE3's GPU skin limit).
- **LE keeps no CPU copies** of the vertices (`RigidVertices`/`SoftVertices` are empty): the tool edits the GPU buffer only
  and refuses a mesh with CPU copies. A rigid vertex that needs a second bone is moved into the soft range (the chunk's
  range permuted, the index buffer remapped); a LOD with shadow indices, edges or raw points is refused (not needed so far).
- New bones go at the skeleton's end (parents before children), into `NameIndexMap`, every LOD's `RequiredBones` (sorted,
  parents included) and `ActiveBoneIndices`. Checks before and after: weights sum to 255, every bone byte inside its map,
  rigid vertices one bone, the chunks cover the buffer.
- **The package**: the mesh alone at the package's root under its new name (the mod loads `<package>.<name>`), its
  sockets copied as children, the forced-export flag cleared, its materials **imports** by the original's full path. A
  cooked LE package resolves imports against objects already in memory, and the mod only swaps the rigged mesh onto a
  component wearing the original (or after loading `needs`, the package the materials come from), so they are loaded then.
  Copying them instead brought 29 master materials and 400 expressions whose paths the game already had.

## In the game

`engine/GameAdapter.hpp` `kHairStandIns`: for a mesh the game puts on, the rigged mesh worn instead (entries tried in
order: ours, then the game's own fallback), `needs` loaded first, and the hair's `look`. `game/Hair` swaps the body first
and re-attaches its head and hair (`SetParentAnimComponent`), then the hair; puts everything back when off. The hair stage
simulates every chain and drives the soft-tissue bones (`[Hair]` in the settings file: `followBody`, `jiggle`,
`jiggleIntensity`, `look`; console `mevr hair people | follow | jiggle | material | param`).
