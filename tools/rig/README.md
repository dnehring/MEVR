<!-- SPDX-License-Identifier: MIT -->
# Re-rigging characters: hair chains and soft-tissue bones

`mevr-rig` adds bones to a character's skeletal meshes, from the player's own game files, and weights the meshes to them:
hair chains (so a hairstyle modelled as one shell bends in several places) and soft-tissue bones (breasts and glutes for the
jiggle). The mod then puts the rigged meshes on the character and drives the bones (`game/Hair`, `engine/HairPose`). First
character: ME3's Miranda (2026-11-19; research/trilogy-hair.md "The re-rig in the game", research/trilogy-jiggle.md).

Re-rigged meshes are BioWare's meshes with our bones: built on the player's machine from a plan of ours (the owner,
2026-11-19), never committed or published. The plan files (`plans/`) hold only our numbers and names. Players get them
through MEVR Setup, which runs this tool's `bodies` verb on their PC ("The players' build" below).

## Running it

Needs .NET (the tool is `net10.0-windows`) and Legendary Explorer's nightly in `mod-manager/` (the csproj finds it; GPL-3.0;
a development build references it by path and copies nothing of it; `NOTICE.md`).

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
python tools/rig/females-plan.py LE1 --write   # tools/rig/plans/le1-females.jsonc (its steps and its "clubs" list)
tools/rig/build-bodies.ps1 -Game LE1           # `mevr-rig bodies`: MEVR_Bodies_LE1.pcc + .json + colliders\ (+ MEVR_Miranda.pcc
                                               # on ME2 and ME3), its log beside it (Idle priority)
python tools/rig/bodies-report.py LE1 --list   # per body: mounds placed, LOD 0 vertices per region, checks, spheres
```

The census and the plan script are the dev machine's: the plans they write (committed) carry everything the build needs,
so a player's build reads only their game files.

Sizes and times (2026-11-19): ME1 38 bodies 7.5 MB, ME2 80 of 101 14.4 MB, ME3 116 of 143 22.1 MB, under a minute each
(research/trilogy-hair.md "Automatic placement and every female body").

## Sizes (the clubs' dancers)

A dancer's breasts and glutes (game/Hair, docs/trilogy-hair-plan.md section 12; baked into the clubs' copies of the bodies,
"Club bodies" below: the game can't make this growth itself) grow by an amount `a` per region,
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
romance body's (ASA_ARM_LIAd) grown by a = 0.10 (the owner: up to 10% larger than hers), clamped 1.0..1.8; the `bodies` verb
runs it after each build (ME3's package holds the reference; for ME1 and ME2 it builds that one body from ME3's files when
ME3's package isn't given with `--ref`). Glutes a 0.10..0.16, a middle dancer 0.12.

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
- **The fit**: on the back half (the angle from straight back under 70 degrees, fading out at 110), from 13 cm above the
  glutes' widest point to 38 cm under it (fading in over the top 12 cm and out over the bottom 16: the hem and the front are
  the game's), each vertex moves along the line from one axis (the reference's middle front to back at the glutes) onto the
  **draped shape** at that angle and height: the reference's convex hull (the shape a cloth stretched round the hips and both
  thighs takes) scaled by the hips' width ratio, plus 0.5 cm of ease at the glutes growing to 2.5 cm 25 cm under them, then
  bridged under the glutes' widest point (per angle the upper envelope of that radius below the peak: the cloth hangs from the
  glutes toward the thighs, never into the fold under them) and smoothed along the height (a Gaussian, 3 cm). At most 6 cm,
  never a vertex more than 8 cm from that shape (an arm, a hand). Every LOD; each moved vertex's tangent frame turned with its
  surface's normal, so the game's shading follows. Baked before the bones are placed, so the glute bone and region sit on the
  fitted shape.
- **Why the drape** (the owner, 2026-11-20, of the first fit on ME3 CTHc and ME2 CTHd: "the glutes look pointed"): the first fit
  followed the hull slice by slice from each slice's own middle, with the ease growing downward, so the fabric went in under the
  glutes and out again (ME3 CTHc's back centre: -12.9 cm at z 95, -10.6 at 87, -10.9 at 82) and each slice's middle jumped at
  the crotch; the glutes' peak read as a point. Drape: -12.7 at 95, then -12.6, -12.5, -12.3, -12.1 down to the hem's -11.2.
- The build log says per step `fit: fitted under the glutes, ...` or `fit: not fitted, <why>`. Fitted on 2026-11-20: CTHc,
  CTHd and CTHg (and their `_old` versions) in all three games, and ME2's ASA_ARM_CTHa (Liara's dress in the cabin): 20 rigs.
  Every woman wearing one of those dresses gets the fitted shape, not only the dancers.

## Club bodies (the dancers' shape, baked)

The game skins a bone from a rotation, a translation and one uniform scale, so the growth above (more outward than sideways, a
sag) can't be made at run time. `Clubs.cs` bakes it into a copy of each body the clubs' women wear, beside the rig in the same
package:

    python tools\rig\club-bodies.py LE3 --list [--plan]  # which rigs: in a club level's packages, all of their jiggle kept
    mevr-rig clubs <bodies.pcc> --names <rig>,<rig>... [--middle 0.7] [--glute 0.12]

The list is in the females plan (`"clubs"`, written by females-plan.py from the census with club-bodies.py's rule; `--plan`
works it out from the plan's steps instead of a built index: the same names), and the `bodies` verb bakes the copies of the
ones it built.

- **Which bodies**: every rigged body the census finds in a club level's packages (game/Hair's `clubLevels`: ME1 Chora's Den
  `bioa_sta60_05`, ME2 Afterlife `biod_omghub_2*`, the VIP room and the bars, ME3 Purgatory `biod_cithub_wardsflux*`,
  `bioa_cithub_afterlife`, Omega's Afterlife `biod_omg004_9*`) that keeps all of its jiggle (no armour type, no `bodyJiggle`
  share under 1: Cerberus armour, a light or medium armour set stay out). `club-bodies.py` holds a copy of both lists.
- **The copy**: `<rig minus _MEVR>_CLUB_MEVR` (e.g. HMF_ARM_NKDb_MDL_CLUB_MEVR): the same bones, weights, materials and
  sockets; every LOD's vertices grown by the formula above at a middle dancer's amounts (breasts a = 0.7 (breastMax - 1),
  glutes a = 0.12), the shading turned with the surface, a skirt's fit kept; its body spheres in `colliders\<copy>.json`.
- **The index**: the rig's entries get `"club": "<copy>"` and `"clubBreastA": <a baked>` beside `breastMax`. The game wears
  the copy on a club NPC and adds only a small uniform per-dancer variation on top (each Breast*/Glute* bone scaled about a
  pivot 5 units above it).
- The `bodies` verb runs it after `sizes` (it needs breastMax). Pictures of what the game will draw:

      tools\rig\preview-clubs.ps1        # %LOCALAPPDATA%\MEVR\rig\previews\sizes3-<GAME>.png, sizes3-all.png

  per game an underwear body and a dress: LIAd, the game's mesh, the club copy exported from the package, and the copy with
  the strongest variation (`mevr-rig export ... --uniform-breast 1.06 --uniform-glute 1.03 [--pivot-up 5]`).

## The players' build (MEVR Setup)

Players never run Python, the census or a plan by hand: MEVR Setup runs one command per game after installing the mod
(`src/install/Bodies`, research/33 section 18):

    mevr-rig bodies --game LE1|LE2|LE3 [--game-root <Mass Effect Legendary Edition>] [--out <folder>] [--plans <folder>]
                    [--ref <ME3's MEVR_Bodies_LE3.pcc>]

`plans/players.jsonc` says what each game gets: its `bodies` plan (`<game>-females.jsonc`), then its `also` plans (Miranda's
hair, `le2-miranda.jsonc` and `le3-miranda.jsonc`, the packages `engine/GameAdapter.hpp` `kHairStandIns` names; Liara's plan is
a development one and isn't shipped). In order (`Bodies.cs`): the bodies plan (`Rigging.Build`), the breasts' limits
(`Sizes.Compute` against `sizesReference`, ME3's LIAd: for ME1 and ME2 that one step of ME3's plan is built alone into
`<out>\reference` and deleted after, unless `--ref` names ME3's package), the clubs' copies of the plan's `"clubs"` rigs that were
built (`Clubs.Make`), then the `also` plans. A body that fails is left out of its package (the plan's `continueOnError`); a
stage or an `also` plan that fails is left out and logged; the run fails (exit 3) only when the bodies package isn't built.
Into `--out` (default `%LOCALAPPDATA%\MEVR\rig\<GAME>`): the packages, their indexes, `colliders\`, and `bodies.json`
(`mevr-rig-bodies` version 1: each package's file and size, the bodies built, the failures, the seconds), which Setup reads.
`progress <percent> <text>` lines on stdout drive Setup's bar; everything else is the log. Setup runs it at below-normal
priority into `rig\<GAME>.build`, then moves the packages into the game's `BioGame\CookedPCConsole` (with a `PCConsoleTOC.bin`
entry each, on the install record) and the rest to `rig\<GAME>`. `tools/rig/build-bodies.ps1` runs the same verb for
development (Idle priority, `--ref` the ME3 package already built there).

**The published tool** (`Rig.csproj -p:MevrPlayers=true`, what `tools/package-release.ps1` puts in `payload\rig` with
`players.jsonc` and the plans it names): self-contained for Windows x64 (players have no .NET), the .NET runtime trimmed
(`TrimMode partial`: no assembly of ours or Legendary Explorer's is touched), Legendary Explorer's library and the four
libraries it loads (`LexRuntime`), their licence texts in `licenses\`, no development path inside. 98 files, 38 MB, 22.7 MB
zipped (2026-11-20). **Oodle** (the games' package compression, RAD's library) is never shipped: `Lex.Init` loads the
game's own `oo2core_8_win64.dll` from `<game root>\Game\ME<n>\Binaries\Win64`. Which libraries it needs: run any verb with
`MEVR_RIG_LOADED=1` (it lists every loaded assembly and module on stderr); all three games' `bodies` load Legendary
Explorer's library, Serilog, Newtonsoft.Json, CommunityToolkit.Diagnostics and .HighPerformance and RecyclableMemoryStream,
never its image, glTF, D3D or tangent libraries. A missing one fails the build (Setup installs without the bodies).

    dotnet publish tools/rig/Rig.csproj -c Release -p:MevrPlayers=true -o <folder>   # then copy plans\ beside it

Checked 2026-11-20 on this machine's game files into scratch folders: the packages, indexes and body spheres of all three
games byte for byte the development pipeline's (the old rig, sizes, clubs sequence), from the development build and from the
published one; ME1 7 s, ME2 10 s, ME3 16 s at below-normal priority on the development PC (24 threads),
12 s, 23 s and 40 s at Idle with a game running beside it. And end to end through MEVR Setup's `--install` on a scratch
game root holding copies of the packages ME1's plan and ME3's reference read: the installed package byte for byte, the
root byte for byte after the uninstall.

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
