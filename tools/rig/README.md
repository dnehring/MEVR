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
