<!-- SPDX-License-Identifier: MIT -->
# Hair for the trilogy: the options, planned

2026-11-19. **Options, not accepted work.** Nothing here is in [the trilogy plan](trilogy-plan.md)'s work list or
[the parity ledger](trilogy-parity-ledger.md) yet; section 9 says what to add when the owner picks. The facts are in
[research/trilogy-hair.md](../research/trilogy-hair.md); this file is written so each step can be done from it alone:
names, files, commands, what to check. **[verify]** marks what nobody has measured yet.

The owner's ask: Miranda's hair "looks like plastic since it acts like a uniform block"; can ME2 and ME3 approach the
realism of modern games?

## 1. What decides the plan (from the research)

1. **Miranda's hair cannot move.** `HMF_HIR_MIR_MDL` (LE2) and `HMF_HIR_PROMiranda_MDL` (LE3), 4,370 vertices, are
   weighted to Head, Neck, Neck1, Chest2 and the two collars only. So are every LE2 hair checked and all LE3 hairs but
   two.
2. **ME3 has three rigged hairs nothing drives:** Ashley (10 hair bones), Jack (8), and an unused older Miranda mesh
   `Hair_PROMiranda.HMF_HIR_PROMiranda_MDL_Old` (13). Only 20 cutscene animation sets name those bones.
3. **No asset uses the engine's cloth, soft body, `SkelControlTrail` or `SkelControlSpline`** (2 games, 3,184 base
   packages scanned; DLC folders not scanned).
4. **The hair material already has two highlights** (found while writing this plan; it corrects the research note's
   first version). The master `BIOG_Humanoid_MASTER_MTR_R.Human.HMN_HED_PRO_MASTER_HAIR_MAT` exposes:

   | Game | Parameters (defaults) |
   |---|---|
   | LE2 | `Highlight1Specularity` 50, `Highlight2SpecExp_Scalar` 250, `HED_Hair_Colour_Vector`, `HAIR_Diff` |
   | LE3 | `Highlight1SpecExp_Scalar` 50, `Highlight2SpecExp_Scalar` 750, `Hightlight1Intensity` 1, `Hightlight2Intensity` 1 (BioWare's spelling), `Highlight1Color`, `Highlight2Color`, `HED_Hair_Colour_Vector`, `HAIR_Diff`; a Normal input |

   The cooked packages keep only the parameter nodes, so whether the lobes run along the strand (anisotropic) or are
   plain Phong can only be read from the compiled shader **[verify, step P4]**. Miranda's instance sets only the colour
   and the texture, so she takes the defaults. An exponent of 50 is a broad, bright highlight: a likely part of the
   plastic look, and it can be changed without touching a shader.
5. **Existing mods** swap meshes and textures. None adds motion or shading.

## 2. The options at a glance

Effort is a rough guess in working sessions of this project (one session: a lane's day), not a commitment.

| Id | Option | What the player gets | Content work | Main risk | Effort | Needs the owner |
|---|---|---|---|---|---|---|
| P | Discovery (section 3) | nothing; answers four questions the rest stands on | none | none | 1 | no |
| A1 | Simulated hair on bones that exist | Ashley's and Jack's hair sways, swings, settles, collides (ME3) | none | how the hair component is posed (P1) | 2-3 | yes: default on? |
| A2 | Miranda on ME3's old rigged mesh | Miranda's hair moves in ME3; an older, coarser mesh (3,881 vertices) | none | the mesh may look worse than today's; loaded only where its package is | 1 after A1 | yes: acceptable look? |
| A3 | Re-rigged meshes | Miranda in ME2 and ME3 (then others) with 8-20 chains on today's mesh | Blender rig per hairstyle | import with added bones (P3); **how it's distributed** (question 2) | 3-5 for Miranda, 1-2 per further hairstyle | yes: distribution |
| A4 | Hands touch hair | a VR hand brushes hair aside | none | an interaction design | 1 after A1 | yes: design first |
| B1 | Tune the highlights | less plastic at once, every character with that material | none | none; subjective | 1 | yes: the values |
| B2 | Replace the hair pixel shader | strand-direction highlights, softer edges, if P4 shows the game's lobes are plain | none | reproducing the game's lighting per shader permutation; the one-Draw replay on LE1 | 4-8, may fail | yes: after a prototype |
| C | Strand hair (TressFX class) | real strands | a groom per character | lighting, sorting, shadows, VR frame budget; may look pasted on | months | not recommended |

Recommended order: **P, B1, A1, A2, A3 (Miranda on ME2 first), then B2 as a prototype**. C only if A and B land and
still fall short. B1 comes early because it is a day's work and may remove half the complaint.

## 3. P: discovery (no player-facing change)

Each step ends with a paragraph in research/trilogy-hair.md and the hypothesis struck or confirmed.

### P1. How is a hair component posed at run time?

The whole of option A depends on it. UE3 offers two ways: the component follows another's pose by bone name
(`SkeletalMeshComponent.ParentAnimComponent` + `ParentBoneMap`; its own `SpaceBases` are not what is drawn), or it runs
its own animation and composes its own `SpaceBases`.

1. Add a console command `mevr hair people` in a new `src/game/Hair.cpp` (mod side, hot-reloaded; all three games): for
   every BioPawn within 10 m, print the hair component (`BioPawn.m_oHairMesh` by reflection: add the row to
   `engine/ReflectCheck`; the offsets in Halcyon's probes, 0x86C on LE2 and 0xB48 on LE3, are only a cross-check), its
   `SkeletalMesh` path, `engine::skeleton::PoseParent()` (the `ParentAnimComponent`) with that component's mesh path,
   `AnimTreeTemplate`, `Animations`, the size of `SpaceBases`, the bone count, and every bone whose name contains
   `Hair` (case-insensitive) with its index and parent.
2. Add `mevr hair trace <seconds> [pawn n]`: each tick, the component-space place of every `Hair` bone and of `Head`,
   from the hair component's `SpaceBases` and from its pose parent's, to
   `%LOCALAPPDATA%\MEVR\hair-trace-<stamp>.csv`.
3. Run on ME3 with Ashley present (a Normandy save after Mars, or her hospital scene `BioD_CitHub_Hospital_Ash`;
   which of the runner's saves has her is **[verify]**: `tools/le2-halcyon-run/README.md`). Take `people`, then a 20 s
   trace while she walks and while a cutscene that keys the bones plays (`BioD_Nor_131RomAshley` or the Mars escape).
4. Decide:
   - **The hair follows a parent and its own `SpaceBases` are empty or unused** (expected): the cutscene tracks
     cannot reach the bones either, unless the trace shows them moving. Then option A writes by route R1 (section 4.3).
   - **The hair composes its own pose:** route R2.
5. Also log where `USkeletalMeshComponent::UpdateSkelPose` (already hooked on all three games, `engine/BonePose`,
   `addresses::kUpdateSkelPose`) is called for the hair component, and whether after the body's in the same tick
   (print the tick counter and call order once per pawn).

Check: the research note states the route, with the trace file's numbers (hair bone travel against the head, in units).

### P2. Does ME3's old Miranda mesh load, and how does it look?

1. In `mevr hair people` output on a ME3 Miranda scene (`BioD_CitHub_Miranda`), confirm the used mesh's path.
2. `mevr instances SkeletalMesh PROMiranda` (the existing command): is `HMF_HIR_PROMiranda_MDL_Old` in memory there?
   The static scan found it only in `BIOG_HMF_HIR_PRO.pcc`; a cooked level may not carry it. If it isn't loaded, the
   engine's `DynamicLoadObject` on `BIOG_HMF_HIR_PRO.Hair_PROMiranda.HMF_HIR_PROMiranda_MDL_Old` is the try
   **[verify]**.
3. Add `mevr hair mesh <pawn n> <object path>`: `SkeletalMeshComponent.SetSkeletalMesh` through
   `ue3::runtime::Call` on the game thread. Snapshot before and after at the same pose (`tools/snapshot.ps1`).

Check: two snapshots side by side; a yes or no on whether A2 is worth doing.

### P3. Does Legendary Explorer import a hair mesh with added bones?

1. Export `HMF_HIR_MIR_MDL` from a copy of `BIOG_HMF_HIR_PRO.pcc` (LE2) to PSK or glTF with the Legendary Explorer
   nightly in `mod-manager/` (Meshplorer). Work in `scratch/hair/` (game data: never committed).
2. In Blender add one test chain of three bones under `Head` (`HairBack_1..3`), weight a strip of vertices to it,
   export.
3. Import over the copy's mesh. Record: does the importer accept 47 bones over 44; does the game load the package
   (put the copy in a throwaway DLC mod folder or over a backup of the original on the dev machine only); does the
   hair still sit on the head with the new bones at rest.
4. If the importer refuses: the fallback is a NEW mesh export under a new name in a package of ours (as the cockpit's
   `tools/cockpit/package/Build.cs` builds meshes from scratch), set at run time by `SetSkeletalMesh` (A3's route
   anyway, section 5.3).

Check: the three answers in the research note, with the importer's message if it refused.

### P4. What do the hair's compiled shaders do?

1. With LegendaryExplorerCore (the scan tool's way of loading it, `tools/conversation-scan`), read the hair master
   material's compiled shaders: LE keeps them in the game's shader cache (`RefShaderCache-PC-D3D-SM5.upk` and the
   per-package caches); LEC has a shader-cache reader that lists a material's shaders by its `m_Guid` **[verify the
   API's name]**. Legendary Explorer's Package Editor shows the same per material ("compiled shaders") as a check.
2. Write each pixel shader's bytecode to `scratch/hair/shaders/` (game data: local only) and disassemble with the
   Windows SDK's `fxc /dumpbin` or `D3DDisassemble`.
3. Record per shader: which pass it is (base pass with the light environment, a per-light pass, depth only, velocity,
   shadow), its size and a hash of the bytecode (the hash is what B2 would ship), its constant buffer layout
   (`core/d3d11/ShaderReflect` reads RDEF), and whether the two highlights use the surface tangent (a dot of the
   half vector with a tangent, shifted: Kajiya-Kay) or the normal (Phong/Blinn).

Check: a table in the research note; the line "the game's highlights are anisotropic: yes/no" decides whether B2 has
anything to add beyond B1.

## 4. Option A: simulated bone-chain hair

One shared core, one engine stage, one game module, each game's differences in `engine/GameAdapter.hpp`. No game-specific
copy (the ledger's rule).

### 4.1 The core: `src/core/ik/HairChain.{hpp,cpp}` (game-independent, unit-tested)

```
namespace mevr::ik::hair {
struct Settings { float gravity = 9.8f;      // m/s2, along world down
                  float stiffness = 0.35f;   // 0..1: pull back to the animated pose per 1/60 s
                  float damping = 0.12f;     // velocity lost per 1/60 s
                  float inertia = 1.0f;      // how much of the head's motion the chain feels (0: rides the head)
                  float maxBendDeg = 55.f;   // per joint, from the rest direction
                  float radiusM = 0.012f;    // a joint's collision sphere
                  float windMS = 0.f; };
struct Chain   { std::vector<int> bones;     // root first; bones[0]'s parent is the anchor (Head or a hair root)
                 std::vector<Vec3> rest;     // each bone's rest offset from its parent, parent space
                 std::vector<Vec3> pos, prev; };  // world space, simulated (bones[1..])
struct Input   { Transform anchorWorld;      // the chain's parent bone this tick, world, metres
                 std::span<const hand_contact::Capsule> colliders;  // world
                 float dt; bool reset; };
void Step(Chain&, Settings const&, Input const&);       // verlet + constraints, fixed 1/120 s substeps
void Pose(Chain const&, Transform const& anchorWorld, std::span<Transform> outParentSpace);  // rotations only
bool Discover(std::span<const std::string> names, std::span<const int> parents,
              std::span<const int> usedBones, std::vector<Chain>& out);  // chains from a skeleton, by name
}
```

Rules:
1. **Position-based dynamics** (verlet): integrate each joint with gravity and wind, then iterate 3 times: length
   (each joint exactly its rest distance from its parent: hair doesn't stretch), bend limit (`maxBendDeg` from the
   animated direction), the pull toward the animated pose (`stiffness`), collisions (push each joint's sphere out of
   every capsule, as `core/ik/HandContact` does for the palm).
2. **Fixed substeps** of 1/120 s with an accumulator, at most 4 per tick, so 30, 72, 90 and 120 Hz give the same
   motion; `dt` of 0 (paused) does nothing.
3. **Reset** (joints put on the animated pose, no velocity) when `Input::reset` is set, when the anchor moved more
   than 0.5 m or turned more than 60 degrees in one tick (a teleport, a camera cut's re-placement), when a value is
   not finite.
4. **Output is rotations only** (`Pose`): each bone turned so it points at its simulated child; the bone lengths
   stay the mesh's. The last bone of a chain keeps its parent's turn.
5. **`Discover`** makes chains from names: every bone whose name contains `hair` (any case) and that some vertex
   uses or that has such a descendant; a chain starts at a hair bone whose parent is not a hair bone and follows
   single children (`HairPonyTop` to `HairPonyBottom`; Ashley's `BackHairTop` to `BackHairBottom`). A hair bone with no
   hair child is a one-bone chain (a swinging flap).

Tests, `tests/HairChainTests.cpp` (ctest `mevr-tests`; LE3's Ashley, Jack and old-Miranda skeletons as name and parent
tables typed into the test, no game data):
- at rest with gravity off the chain stays within 1e-4 m for 10 s;
- gravity alone: the droop is bounded by `maxBendDeg` and settles (speed under 1 mm/s) within 2 s;
- a 90-degree head turn in 0.2 s: the tip lags (peak offset over 2 cm), then is back within 1 cm of rest in 1.5 s;
- a capsule moved through the chain: no joint ends inside it;
- the same input at 30, 72 and 120 Hz: tips within 1 cm of each other at every half second;
- a 2 m anchor jump resets; a NaN input resets; `Discover` returns the chains of Ashley's, Jack's and old Miranda's
  skeletons (the expected counts come from the bones' parents, which P1's `mevr hair people` prints: Jack's are four
  two-bone chains by their names; don't guess the others).

### 4.2 The game module: `src/game/Hair.{hpp,cpp}` (mod side, every game)

Each tick (the mod's tick, after `arm_ik` and `hand_contact`):
1. `ue3::runtime::Up()` and a VR or flat view: any; this isn't a VR-only feature.
2. Candidates: visible BioPawns within `rangeM` (6) of the view, nearest first, at most `maxPeople` (6); the player's
   own hair is skipped while first person hides the head.
3. Per candidate, cached by hair component address and mesh address: the chains (`Discover` over
   `engine::skeleton::ReadRefSkeleton`), none found: remembered as "rigid", never asked again until the mesh changes.
4. Colliders: the pawn's capsules from `ik::hand_contact::PartsFor` (skull, neck, collars, spine, upper arms),
   already built by `game/HandContact` for people in its range: share the cache, don't build twice.
5. `hair::Step` per chain with the anchor's world transform from the bone the chain hangs on.
6. Publish the result to the engine stage (4.3): per component, the chain bones' parent-space rotations.
7. **Authored animation wins:** if P1's trace shows cutscene tracks do move the bones, the stage blends to the
   animated value whenever a bone's animated rotation differs from its reference pose by more than 1 degree
   (weight 1 over 0.2 s, back over 0.5 s).

Settings, `[Hair]` in the shared settings file (`vr::settings_file`, a key for the three games; defaults appended by
the module): `on = 1` (the owner's yes first, question 1), `rangeM`, `maxPeople`, `gravity`, `stiffness`, `damping`,
`inertia`, `maxBendDeg`, `wind`. The menu: one row, Display or Comfort, "Hair movement" On/Off, only after the yes.

Logs `hair:` (once per pawn and mesh: `hair: Ashley's hair (Hair_PROAshley.HMF_HIR_PROAshley_MDL): N chains, 10
bones` or `...: no hair bones, rigid`; the heartbeat: people simulated, chains, resets, the stage's written and
rejected counts). Snapshot `"mod"."hair"`. Console `mevr hair [on|off|auto | people | trace <s> [n] | mesh <n>
<path> | shake <deg> <hz> [s]]` (`shake`: the simulation fed a synthetic head swing on top of the real one, so the
response can be measured on a pawn that stands still).

### 4.3 The engine stage: `engine/BonePose` (mod side)

A new stage beside the weapon-part stage (`PublishWeaponPart`): `PublishHair(std::span<HairPose const>)`, each
`{component, mesh, boneCount, bones[], keys[], rotations[]}`, checked on every call as the weapon part is (component
by address, bone count, the bones' names by key, the game thread, the age under 0.25 s), inside SEH, journalled
(`write_journal::Wrote("BonePose.hair", ...)`), at most 8 components.

- **R2 (the hair composes its own pose):** in `AfterUpdateSkelPose` for a published component, rebuild the chain
  bones' `SpaceBases` entries parents first: parent's SpaceBase x (rest offset, our rotation).
- **R1 (the hair follows a parent):** the renderer takes mapped bones from the parent's `SpaceBases` through
  `ParentBoneMap` and never reads the hair's own. Two ways, the first to try first:
  1. Stop following for that component: `SetParentAnimComponent(None)` once (ProcessEvent; put back at unload and
     when the feature goes off), then in its `UpdateSkelPose` post-stage write EVERY bone: the bones the parent has,
     copied from the parent's `SpaceBases` by name (the map `ParentBoneMap` held, kept by us), the hair bones from
     the simulation. Needs the hair's update to run after the parent's in the tick (P1 step 5); if it runs before,
     call `ForceSkelUpdate` on it from the parent's post-stage, as the vent's gun does.
  2. If the component then no longer updates or draws correctly: read with Ghidra how the LE builds fill an unmapped
     bone when following (`tools/ghidra/decompile.ps1` from `UpdateParentBoneMap`'s callers) and write there.
- Never `Camera` or `God` (they aren't hair bones; the name filter excludes them, and the stage refuses them by key).
- Bounds: hair that swings out of the component's box can be culled at the frame's edge; if seen, pad as
  `engine/BodyBounds` does for the body.

`engine/ReflectCheck` rows: `BioPawn.m_oHairMesh`, `SkeletalMeshComponent.SetParentAnimComponent`,
`SetSkeletalMesh`, `ParentBoneMap`, per game.

### 4.4 A1: ME3's Ashley and Jack

1. P1 done.
2. Core and tests (4.1), build `build-trilogy` and `build-mono`.
3. Module and stage (4.2, 4.3), `[Hair] on = 0` until checked.
4. Run on the fake headset, ME3, Ashley present: `mevr hair on`, `mevr hair people` (her chains listed), `mevr hair shake 30
   1.5 6` with `mevr hair trace 8`.
5. `python tools/hair-trace.py <csv>` (new, small): per chain the tip's peak offset from rest, the settle time, the
   frames any joint was inside a collider (must be 0), resets.
6. Snapshots at rest, at the swing's peak, after settling; a 10 s recording through a cutscene that keys the bones
   (no fight between the animation and the simulation: the trace's blend weight goes to 1 there).
7. `tools/perf-probe.ps1` with 6 people simulated against off: the game thread's tick time (expected under 0.05 ms).
8. Headset checklist line (the look at conversation distance; in VR the head is 0.5-1 m away, closer than any flat
   camera ever was).

Done when: the numbers in step 5 are in the research note, ME1's compiled code is unchanged with the feature off, and
the owner has answered question 1.

### 4.5 A2: Miranda on ME3's old rigged mesh

Only if P2 said the mesh is acceptable and loadable.
1. A per-game table in `engine/GameAdapter.hpp`: `{used mesh path, rigged mesh path}`; LE3 has one entry (Miranda's).
2. `game/Hair`: a pawn whose hair mesh is a table's "used" path gets the rigged one by `SetSkeletalMesh`, once, put
   back when the feature goes off. A hair mesh that isn't the table's (another mod's Miranda hair) is left alone.
3. Check as 4.4; snapshot pair against today's mesh at the same pose.

### 4.6 A3: re-rigged meshes (Miranda on ME2 first)

**The rig** (in Blender, per hairstyle, kept in `concept/hair/<name>/` as our own data: bones and weights, not the
mesh; see distribution below):
1. Chains for Miranda's long loose hair: back x3, each side x2, the two front locks x1 each, the fringe x2: 9 chains
   of 4 bones, 36 bones (44 + 36 = 80, under the 110 the body uses). Named `Hair<Where><n>_<i>`
   (`HairBack1_1..4`), so `Discover` finds them.
2. Roots parented to `Head`; chain joints placed along the hair cards from the scalp outward, evenly by length.
3. Weights: scalp vertices stay on `Head`; along each lock blend from `Head` to the chain's bones by distance from
   the scalp, at most 4 influences per vertex, smooth across neighbouring chains. The vertices now on Neck, Chest2 and
   the collars (the hair resting on the shoulders) go to the chains' lower bones: the simulation's collision with the
   shoulder capsules replaces the static weighting.
4. Test in Blender by posing the chains to their bend limit: no folds, no gaps at the scalp.

**Getting it into the game** (5.3 has the open question): the mesh goes in under a NEW object name in a package of
ours and is set at run time through A2's table; no vanilla package is edited, and another mod's hair replacement
keeps working (its mesh isn't the table's "used" path, so we stand down).

**Then:** ME3's Miranda (same mesh, 4,370 vertices: the same rig), then the hairstyles the owner names; ME1's hairs
(`check first`: the same probe on LE1's `BIOG_HMF_HIR_PRO`).

### 4.7 A4: hands touch hair (design first)

Technically one line: add the player's palm spheres and forearm capsules to `Input::colliders`. It is an interaction,
so it goes to the owner as options before any build: off; on for the squad only; on for everyone; with or without a
faint haptic tick. Nobody is pushed (as hand contact's rule).

## 5. Option B: shading

### 5.1 B1: tune the highlights the material already has

1. `mevr hair material <pawn n>`: print the hair component's material chain (the instance, its parent, each
   scalar and vector parameter's current value by `GetScalarParameterValue`/`GetVectorParameterValue`).
2. `mevr hair param <pawn n> <name> <value>`: give the component its own instance
   (`MeshComponent.CreateAndSetMaterialInstanceConstant(0)`, so the shared package object isn't changed) and set the
   parameter (`SetScalarParameterValue`); all on the game thread through `ue3::runtime::Call`; rows in ReflectCheck.
3. On Miranda (ME2: `tools/le2-halcyon-run` with a Normandy save; ME3: `BioD_CitHub_Miranda`), a fixed pose
   (`tools/console.ps1 pause`), snapshots over a small grid: highlight 1's exponent 50 (now), 100, 200; highlight 2's
   250/750 (now), 400, 1200; ME3 only: intensities 1 (now), 0.6, 0.35. A contact sheet of the crops.
4. The owner picks from the sheet (question 3). The picked values become `[Hair] highlight1Exp`, `highlight2Exp`,
   `highlight1Intensity`, `highlight2Intensity` (empty: the game's), applied by `game/Hair` to every hair component
   whose material's parent is the master hair material, each on its own instance, put back when off.

Limits: ME2's material has no intensity parameters, only the two exponents (a higher exponent is a tighter, less
plastic highlight, but its strength stays). If that isn't enough on ME2, B2 is the only further lever there.

### 5.2 B2: our own hair pixel shader (a prototype with a gate)

Only if P4 shows something worth changing (the lobes aren't along the strand, or the edges are hard).

1. **Find the shaders at run time.** `MevrBridge::NotePixelShaderCreated` (LE1's CreatePixelShader seam; LE3's
   `d3d_capture.cpp` hooks CreatePixelShader too; LE2's needs the hook added **[verify]**) hashes each new shader's
   bytecode and compares with P4's hashes for the hair master's lit passes (a table per game in the host; a hash is
   not game data).
2. **Swap at creation, not per draw:** for a match, create our shader from our bytecode and hand the game ours.
   Every later bind is ours with no per-draw hook, and LE1's one-Draw replay records and replays whatever was bound,
   so the second eye needs nothing more. `[Hair] shader = 0` returns the game's own (a restart applies it).
3. **Ours must take exactly the game's inputs:** the same input signature and constant buffer layout as the shader
   it replaces (P4's table), per pass. Two routes, in this order:
   - **Patch route:** disassemble the game's shader in the process, replace only the highlight term, reassemble
     (3Dmigoto's DXBC assembler is GPL-3.0, compatible; it would go in `external/` with its notices). The game's own
     lighting, fog and shadowing stay byte for byte. Nothing derived from game code is shipped: the patch is a recipe
     applied on the player's machine.
   - **Rewrite route:** our HLSL per pass, compiled offline, reading the game's constants by their RDEF offsets.
     More control, but every lighting path must be reproduced; one missed permutation is a visibly wrong hair in
     some light.
4. **What ours adds:** Kajiya-Kay with two lobes shifted along the tangent by the texture's value (the sheen that
   moves along the strands as the head turns), the primary lobe white, the secondary tinted by the hair colour, and a
   softer alpha edge (the material is translucent with a depth pass; dithered alpha at the card edges is the
   candidate **[verify what the pass order allows]**).
5. **The gate:** one pass (the base pass under the light environment) on Miranda on ME3, same-pose snapshots: the
   game's, B1's, ours. If ours isn't clearly better than B1's, B2 stops there and the note says so.

Risks: the per-pass count (each light type has its own shader); the tangent may not be there in the interpolators the
game's shader receives (then the patch route can't have it, and the vertex shader would need swapping too: stop);
LE1's replay and the second eye's screen-space matrices (the hair pass reads none **[verify]**).

### 5.3 Better hair assets (noted, not planned)

Higher-density cards, a flow map and alpha with strand detail are content work the modding community already does;
our part is not to block it: A stands down on meshes it doesn't know, B1 and B2 key on the master material, which
replacement hairs usually keep.

## 6. Option C: strand hair, and why not now

What it would take, so the "no" is informed:
1. A groom per character (10-30 thousand guide strands) authored from the card mesh: content nobody has.
2. Simulation and rendering of our own (AMD TressFX 4.1, MIT, is the reference): a compute pass per character, drawn
   into the game's scene colour and depth before translucency, in both eyes, inside LE1's one-Draw replay.
3. Lighting taken from the pawn's light environment (one direction and colour, an ambient term) because the game's
   forward lights aren't available to a pass of ours; shadows and the head's occlusion of our own.
4. A frame budget: the heavy cells already use the whole frame at 90 Hz (research/22), and strands at 0.5 m in VR
   want over 1 px width at 2,800 px per eye.

Gate to reopen: A and B shipped, the owner still wants more, and a frame-time reserve of 1.5 ms on the 4090 at the
heavy cells.

## 7. Distribution of re-rigged meshes (A3), the open question

The project's rule is no game art in the repository or the release (the rail's art is copied from the player's own
game at run time). A re-rigged Miranda mesh is BioWare's mesh with our bones.

| Way | How | For | Against |
|---|---|---|---|
| 1. Built on the player's machine (recommended) | Setup (or the mod at first start) reads the player's own mesh, adds our bones and weights from a **recipe** (chain curves and a weighting rule, our data), writes a package of ours next to the game's | no game art leaves the player's machine; one click | needs a package writer in the players' build: LegendaryExplorerCore (GPL-3.0, .NET) run by Setup, or our own minimal writer for one mesh export |
| 2. A separate mesh mod | the re-rigged mesh as an ordinary mod on Nexus, as every hair mod there is | the community's norm; no tool work | game art published under our name; the owner's policy call; a second download |
| 3. Only hairs that are already rigged | A1 and A2 only | nothing to distribute | Miranda on ME2 stays rigid |

The recipe in way 1 must not store per-vertex data taken from the mesh; weights are computed from each vertex's
distance along and across the chain curves. The dev machine can use way 2's files locally for iteration either way.

## 8. The owner's answers (2026-11-19)

1. **Hair movement is on by default** (`[Hair] on = 1`), every game.
2. **Re-rigged meshes are built on the player's machine** (section 7, way 1): no game art published.
3. **The highlight:** "likely blondish": a blond-tinted highlight on Miranda's dark hair. ME3 has `Highlight1Color` and
   `Highlight2Color`; ME2's material has no highlight colour, only the two exponents (so on ME2 a tint needs B2, or a
   texture tint). The values still come from the contact sheet (B1 step 3), now with blond tints in the grid.
4. **ME3's older Miranda mesh is acceptable** (A2): it goes on by default (`[Hair] standIns = 1`).
5. **Hands move hair, and our own VRIK body too:** the player's drawn hands, forearms and upper arms (the arms arm IK poses
   from the controllers) are colliders for everyone's hair (`[Hair] hands = 1`). How the player's own Shepard's hair
   should behave is open (in first person the head is hidden).
6. **Scope: Miranda only for now.** ME3 through A2; ME2 through A3 (the re-rig, built on the player's machine), then
   B1's highlight on both.

### The order this gives (Miranda)

1. A1's core, module and stage on ME3 with the old mesh (A2) in one landing; P1 and P2 are its first in-game run.
2. B1 on Miranda, ME2 and ME3: the contact sheet with blond tints (ME3) and exponents (both); the owner picks.
3. A3 for ME2's Miranda: P3 first (the importer, the game loading a package of ours), then the recipe and its builder
   (section 7 way 1).

## 9. When an option is accepted

- Add a feature entry to [trilogy-plan.md](trilogy-plan.md) (the next free T id, "Hair: movement and shading", State
  per game, Lives in `core/ik/HairChain`, `game/Hair`, `engine/BonePose`'s hair stage) and a row to the parity ledger
  (`tools/parity-ledger.py --write`); update both in every landing that moves it.
- `CHANGELOG.md` when a player can see it; `notes/headset-checklist.md` for the look at conversation distance;
  the gotchas bundle if P1's pose route bites.
- New files carry `SPDX-License-Identifier: MIT`; 3Dmigoto's assembler, if B2's patch route is taken, is
  `GPL-3.0-only` in `external/` and listed in `NOTICE.md`.
- Lanes: at most two at once; A's core and tests need no game run; every game run through
  `tools/le2-halcyon-run/run-lock.ps1`; saves backed up before runs.

## 10. Later scope: breast and glute jiggle (the owner, 2026-11-19)

The owner noted the bodies want better secondary motion too. Measured: Miranda's ME2 body
(`BIOG_HMF_MIR_LGT_R.LGTa.HMF_MIR_LGTa_MDL`) is the standard 110-bone humanoid, its vertices on Chest, Chest1, Chest2,
Pelvis, the hips and their twist bones; there is no breast or glute bone (the ME3 bodies dumped for this note have the same
skeleton). So:
- **The simulation is the same core:** `core/ik/HairChain` with a one-bone chain is a damped spring bone (gravity off or
  low, high stiffness, small bend limit, no collisions); `engine/HairPose`'s stage writes body meshes the same way.
- **The bones aren't there:** each body mesh needs a few added bones (a breast bone per side under Chest2, a glute bone
  per side under Pelvis) and weights blended from the existing ones, exactly A3's problem, so it rides on A3's route
  (section 7 way 1: built on the player's machine from a recipe, never published) and on P3's answer.
- **The bodies are many** (armours and outfits per character), unlike one hairstyle: the recipe must be generic (bone
  places from the mesh's own landmarks, weights from distance), and every outfit checked.
- Not started; it comes after Miranda's hair, and the look and the defaults go to the owner first.

## 11. Progress, 2026-11-19 (branch hair-miranda)

- **ME3 Miranda's hair moves** (research/trilogy-hair.md "Miranda's hair moves"): the old rigged hair on her own body
  (identical rig), simulated on the body the hair follows (ME3's follower rule), gravity 0 over the styled pose.
- **The bench** `mevr-hair-bench` (tools/hair-bench): the hair or the jiggle over one of our generated clips on a body's
  skeleton, no game, parameter sweeps in seconds. What it found, fixed in `core/ik/HairChain`: damping now slows the hair
  relative to the animation (a steady walk had trailed strands 7 cm); the animation is sampled at each fixed step's time and
  drawn at the frame's (120 and 240 fps agree to 0.00 cm; jitter 2.4 -> 0.96 mm per step). What is left: 30-60 fps differ
  from 120 by 2-9 cm (the animation is seen once per frame; better input interpolation could trim it); today's defaults
  (stiffness 0.06, damping 0.10) swing a walk 0.7 cm mean, 3.9 cm peak and settle in 0.26 s, a little stiff; softer values
  are the owner's choice in the headset.
- **Generated animation** (tools/motion, the owner's ARDY pipeline): walk-sway, jump-place, jump-forward, run, turn-walk on
  ME3 Miranda's skeleton, walk-sway-me2 and jump-place-me2 on ME2's; played in the game by `mevr anim clip` (engine/ClipPose,
  before the hair stage), on conversation characters and pawns; `mevr anim at <s>` holds a moment. Checked on ME3 (Citadel)
  and ME2 (Freedom's Progress).
- **Jiggle** (section 10, research/trilogy-jiggle.md): `core/ik/Jiggle` built and unit-tested, benched on stand-in bones
  (breasts on Chest2, glutes on Pelvis): a walk 0.7 cm, the jump 2.2 cm (4.5 at intensity 1), settled within 0.15 s. In the
  game it still needs the added bones and weights (the A3 route, built on the player's machine); its look, defaults and
  whether it is on go to the owner first.
- **The re-rig works in the game** (A3 and P3 answered; research/trilogy-hair.md "The re-rig in the game";
  `tools/rig/README.md`): no Blender or importer needed: `tools/rig` adds bones and weights by editing the meshes through
  Legendary Explorer's library, from the player's own game files, into our package (materials as imports). ME3 Miranda wears
  `MEVR_Miranda.HMF_HIR_PROMiranda_MEVR` (7 generated chains of 4 bones; the lower joints rest with the shoulders,
  `followBody`, from the bind pose) and `HMF_ARM_MIRa_MEVR` (the same chain bones plus breast and glute bones); she draws
  as before at rest, the back of the hair bends along its length through a walk, and the soft tissue moves on the jump
  (+-1 cm breasts, +-0.3 cm glutes at intensity 0.5).
- **B1 done for Miranda** (`game/HairLook`): her highlight a warm blond (both lobes tighter, warmer, stronger), from three
  same-pose sheets; `[Hair] look` overrides, `mevr hair material | param`.
- **Next**: the owner in the headset (followBody, the jiggle's intensity, the highlight; notes/headset-checklist.md); ME2's
  Miranda through the same plan format (her hair has no hair bones at all: the hair step alone; her body the bones);
  the players' install of a rig (MEVR Setup building the plan's package on the player's machine, docs section 7); other
  characters by a plan each.

## 12. Jiggle on every female character, ME1-3 (the owner, 2026-11-19)

After judging Miranda (suit and underwear) and Liara (her romance-scene body) in the headset: jiggle good, full intensity
preferred (`jiggleIntensity` 1); Liara needs no changes; "we would want to make this change for all female characters in
me1-3", reduced in armour ("full gear"), full in a bodysuit, casual clothes or underwear. Decided: armour keeps a quarter,
told by the body mesh's name (`game/Hair` `OutfitJiggle`, `[Hair] jiggleArmour`, `armourBodies`, `fullBodies`; built,
892c8249). Hairless characters (asari) simulate on the body (3402bc7c, 55954bd3).

What's left, in order:

1. **Automatic placement in the rig tool.** **Built** (42079a7e: `"place": "auto"`, `mevr-rig place`; within 1 cm per axis of MIRa, NKDc, LIAd; research/trilogy-hair.md "Automatic placement and every female body"). Today the breast and glute bones and their weight regions are placed by hand per
   mesh from its surface (`inspect --dump`, then the front peaks per side in z 120-145 and the back peaks in z 85-105, as in
   `plans/le3-miranda.jsonc` and `plans/le3-liara.jsonc`). A `"bones": "auto"` step (or a `place` verb) finds them: per side,
   the front's furthest vertex in the chest band (y 2..12 cm off the middle) is the breast peak, the bone 7.5 cm behind it
   on the parent `Chest2`, the region centred 3.5 cm behind the peak, radius (6.5, 7.0, 8.0); the back's furthest vertex in the
   hip band is the glute peak, bone 7.5 cm inside it on `Pelvis`, region (7.0, 7.5, 10.0); the bands scaled by the skeleton's
   `Chest2` and `Pelvis` heights (a krogan or a child stands otherwise). Check each against the hand placements of Miranda
   (MIRa, NKDc) and Liara (LIAd): within 1 cm.
2. **The female bodies of each game.** **Built** (7a7f31a8: `mevr-rig census`, `plans/le1|le2|le3-females.jsonc`; ME1 38 of 38 rigged, ME2 80 of 101, ME3 116 of 143; the rest Shepard's armour pieces without a mound). A `find`-driven list per game (`mevr-rig find <CookedPCConsole> *.pcc <regex>`):
   ME3 `HMF_ARM_*` and `ASA_ARM_*` bodies (squad, NPC, player female armour sets), ME2 `HMF_*`/`ASA_*` (character outfits like
   `HMF_MIR_LGTa`, the female armour sets), ME1 likewise; male and non-humanoid skeletons left out (no `Chest2`/`Pelvis`, or
   `HMM_`). One generated plan per game (`plans/<game>-females.jsonc`), one package per game (`MEVR_Bodies_<GAME>.pcc`).
3. **Stand-ins by name, not a table.** `engine/GameAdapter.hpp` `kHairStandIns` lists Miranda's meshes by hand; for every
   body, `game/Hair` looks for `MEVR_Bodies_<GAME>.<mesh>_MEVR` (loaded through the TOC like Miranda's) and swaps it in;
   the hand table stays for hair. Bodies in another body's package (a level's own copy) match by mesh name. **Built**
   (lane B, research/trilogy-hair.md "Rigged bodies by path, and ME1"): by the mesh's path through the package's index
   `MEVR_Bodies_<GAME>.json`, the hand table first; `mevr hair bodies`.
4. **ME1 and ME2.** The hair module and the jiggle stage run on LE2 (Miranda at Freedom's Progress); LE1 needs its
   `HairPose` stage wired the same way (check `engine/BonePose`'s hook for it). **Done** (lane B): it already ran there; the
   head's field (`m_oHeadMesh`) and a reflect row were what a body swap lacked.
Not Shepard (the owner, 2026-11-19): the player's pawn is never simulated (a first-person VR experience; third person isn't
the priority), so the sequels' modular armour pieces (breasts on the torso piece, glutes on the legs) need no work.
Per character (the owner, 2026-11-19): EDI none (her robot body: no rigged body, no swap), Samara half, Kasumi a quarter,
Jack full, Ashley in armour, Liara's combat outfits and Cerberus female armour a quarter (`[Hair] bodyJiggle = hmf_arm_ashc:1,
asa_arm_liad:1, _edi:0, asa_spr_:0.5, asa_arm_sam:0.5, _ksm:0.25, hmf_jck_:1, hmf_arm_ash:0.25, asa_arm_lia:0.25,
hmf_arm_crb:0.25`, the first match wins, before the armour rules). ME3's outfits are told apart by the packages that load them
(lane A's census, `%LOCALAPPDATA%\MEVR\rig\census\LE3.csv`): `BioH_<name>_NN` is a squad appearance, so armour (Ashley's ASHa,
ASHb, ASHf; Liara's LIAa, LIAe, LIAf, LIAh); Ashley's ASHc loads only in the hospital and her romance scene, Liara's LIAd only
in hers (full). Left: the package's load
cost at the first lookup per level [verify], and step 5.

5. **Checks per game:** the generated rigs' `check:` lines, a bench run of the jiggle per body
   (`mevr-hair-bench --jiggle`), then the owner in the headset on a sample: one armour, one bodysuit, one casual per game.
