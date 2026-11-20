<!-- SPDX-License-Identifier: MIT -->
# Hair in ME2 and ME3: why it moves as one block, and what could change it

2026-11-19. Investigation only: nothing built. Asked by the owner: Miranda's hair "looks like plastic since it acts like a
uniform block"; can the sequels' hair approach modern games, and do existing mods already do it?

## 1. What the game data says (measured)

Read from the installed packages with LegendaryExplorerCore (a throwaway probe in the session scratchpad, not committed:
`ObjectBinary.From<SkeletalMesh>` for the skeleton, each LOD0 chunk's `BoneMap` for the bones the vertices are actually
weighted to, the materials' properties).

### Miranda's hair has no hair bones

| Mesh | Game | Bones in skeleton | Bones the vertices use | LOD0 verts |
|---|---|---|---|---|
| `BIOG_HMF_HIR_PRO.Hair_PROMiranda.HMF_HIR_MIR_MDL` (used, in `BioH_Vixen_00.pcc`) | LE2 | 44 (the face skeleton) | Head, Neck, Neck1, Chest2, LeftCollar, RightCollar | 4,370 |
| `Hair_PROMiranda.HMF_HIR_PROMiranda_MDL` (used, `BioD_CitHub_Miranda.pcc`) | LE3 | 44 | the same six | 4,370 |
| `Hair_PROMiranda.HMF_HIR_PROMiranda_MDL_Old` (**unused**, in LE3's `BIOG_HMF_HIR_PRO.pcc`) | LE3 | 123 | Head + 13 hair bones: HairBackTop/Bottom, HairBackLeftTop/Bottom, HairLeftTop/Bottom, HairRightTop/Bottom, HairLeftBangTop/Bottom, HairRightBangTop/Mid/Bottom | 3,881 |

So the shipped hair is rigid on the head and neck: there is nothing in it that could swing. BioWare once rigged an older
Miranda hair with 13 hair bones (two-bone chains: back, sides, bangs) and shipped it unused.

### ME3 rigs a few hairstyles, and animates them only by hand in a few cutscenes

| Mesh (LE3 `BIOG_HMF_HIR_PRO.pcc`) | Bones | Hair bones the vertices use |
|---|---|---|
| `Hair_PROAshley.HMF_HIR_PROAshley_MDL` | 120 | 10: BackHairTop/Bottom, LeftHairBangs(+Bottom), Left/RightShoulderHair* |
| `Hair_PROJack.HMF_HIR_PROJack_MDL` | 118 | 8: HairPonyTop/Bottom, HairRightStrandA/B_Top/Bottom, HairLeftStrand_Top/Bottom |
| `Hair_PROJessica`, the CC hairs, every LE2 hair checked | 9-110 | none (Head, Neck, Neck1, Chest2, collars) |

The only things that move those bones are keyframed cinematic tracks: 20 `BioAnimSetData` sets name them, all in cutscenes
(`BioD_Nor_131RomAshley*` the romance scenes, `BioD_CitHub_000ProCit` Mars escape, `BioD_OmgJck_310JackScene`,
`BioD_OmgJck_650ShuttleExit`, `BioD_Cat002_700PostKLFight`, `BioD_Cat003_480CarCutscene`). Whether those tracks reach the hair at
all, and how a hair component is posed at run time (following another component by bone name through
`ParentAnimComponent`, UE3's usual way for attached parts, or composing its own pose), is not measured **[verify: the
plan's step P1]**; no gameplay animation set names the hair bones.

### The engine has the pieces, no asset uses them

`Engine` in both games (LExSDKv2's LE2/LE3 headers) has `SkelControlTrail`, `SkelControlSpline`, physics assets with
`PhysicsWeight` blending, and PhysX 2-era cloth and soft-body fields (`bEnableClothSimulation`, `ClothBones`,
`bEnableSoftBodySimulation`, `SoftBodyBones`); no `SkelControlSpring` or anything like AnimDynamics. A scan of every
non-localised package in the base games (LE2 1,390, LE3 1,794; DLC folders not scanned) found **no** SkelControlTrail or
SkelControlSpline instance, **no** skeletal mesh with cloth or soft-body bones or cloth enabled, beyond the classes' own
defaults in `Engine.pcc`. Whether the cloth code is still alive in the LE executables is unknown **[verify]**; no shipped
asset exercises it.

### The material: two highlights, both at their defaults

`HMF_HIR_PROMiranda_MAT_1a` is a MaterialInstanceConstant that sets one colour (`HED_Hair_Colour_Vector`) and one texture
(`HAIR_Diff`) over `BIOG_Humanoid_MASTER_MTR_R.Human.HMN_HED_PRO_MASTER_HAIR_MAT`: `BLEND_Translucent`, `TwoSided`,
`TwoSidedSeparatePass`, `bUseLitTranslucencyDepthPass`, `bUseLitTranslucencyPostRenderDepthPass`,
`bCastLitTranslucencyShadowAsMasked`, BioWare's `bHairPass`. The cooked packages keep only the master's parameter nodes:

| Game | The master's parameters (defaults) |
|---|---|
| LE2 | `Highlight1Specularity` 50, `Highlight2SpecExp_Scalar` 250, `HED_Hair_Colour_Vector`, `HAIR_Diff` |
| LE3 | `Highlight1SpecExp_Scalar` 50, `Highlight2SpecExp_Scalar` 750, `Hightlight1Intensity` 1, `Hightlight2Intensity` 1, `Highlight1Color`, `Highlight2Color`, `HED_Hair_Colour_Vector`, `HAIR_Diff`; a Normal input and `OpacityMaskClipValue` 0.333 |

So the material has two highlight lobes, which Miranda's instance leaves at the defaults. **Correction (same day):** this
note's first version said "one isotropic highlight, no anisotropy"; that was a guess from the instance alone. Whether the
lobes run along the strand (Kajiya-Kay) or are plain Phong can only be read from the compiled shaders (the plan's step
P4) **[verify]**. A first highlight at exponent 50 is broad and bright, a likely part of the plastic look, and it is a
parameter.

## 2. Existing mods (searched 2026-11-19)

All replace meshes and textures; none found adds motion or new shading.

- Miranda hairstyles: [mod 1414](https://www.nexusmods.com/masseffectlegendaryedition/mods/1414) (14 styles, 2 colours,
  needs the Unofficial LE2 Patch), [mod 991](https://www.nexusmods.com/masseffectlegendaryedition/mods/991) (one style
  throughout the game), [mod 680](https://www.nexusmods.com/masseffectlegendaryedition/mods/680) (the prologue only).
- [Mod 1441](https://www.nexusmods.com/masseffectlegendaryedition/mods/1441): LE2/LE3 default hair back to the 2010 look.
- FemShep hair ports (the Sims 4 ports among them): static meshes; the community guides say a replacement must match the
  replaced mesh's bone count ([LegendaryExplorer wiki](https://github.com/ME3Tweaks/LegendaryExplorer/wiki/Mass-Effect-3-Tutorial:--Adding-Hair-to-the-Character-Creator),
  [girlplaysgame hair tutorial](https://girlplaysgame.com/mass-effect/mass-effect-how-to-mod-to-your-hearts-content/mass-effect-modding-tutorial-hair-modding/)).
- No hair physics mod turned up in Nexus or tutorial searches. The ME3Tweaks Discord is where to ask if one is in
  progress **[verify]**.

## 3. Options, cheapest first

### A. Motion: bone-chain hair simulated by the mod (recommended first)

What modern games mostly ship for in-engine hair is cards on simulated bone chains. Here:

1. **Targets with no content work:** ME3's Ashley (10 bones) and Jack (8), already weighted. ME3's unused
   `HMF_HIR_PROMiranda_MDL_Old` (13 bones) is a ready prototype for Miranda; the mod could set it on her hair component at run
   time where its package is loaded (`SetSkeletalMesh`; it sits in `BIOG_HMF_HIR_PRO`, cooked into her levels) **[verify]**,
   at the cost of the older, coarser mesh.
2. **Everyone else needs a re-rigged mesh:** chains of 3-6 bones (back, sides, bangs; 8-20 chains for a quality look)
   added to the hair mesh in Blender and weighted, then imported with Legendary Explorer. Whether LEX's importer accepts a
   hair mesh with more bones than the one it replaces is the first thing to check **[verify]** (the old ME3Explorer refused;
   if the hair takes its pose by bone name, extra bones stay at their reference pose unless driven: the plan's P1). A mesh mod is game
   content of ours, distributed as a package mod, like the cockpit.
3. **The simulation lives in the mod** (the engine's own `SkelControlTrail` would need an AnimTree on each hair component;
   physics-asset bodies on attached hair have a one-frame lag, [UE3 physical animation docs](https://docs.unrealengine.com/udk/Three/PhysicalAnimation.html)):
   - shared core `core/ik/HairChain` (unit-tested): verlet or XPBD chains, gravity, inertia from the head's world motion,
     damping, angle limits, collision with capsules from the head, neck, collar and shoulder bones (`core/ik/HandContact`'s
     `PartsFor` already builds capsules from bones by name), optional wind;
   - per-game adapter: the pawn's hair component (`BioPawn` + 0x86C on LE2, + 0xB48 on LE3: Halcyon's engine probes),
     written after its pose in `engine/BonePose`'s post-pose stage (as the vent's weapon part is), journalled;
   - authored tracks win: where a cutscene keys the hair bones (the 20 sets above), blend to the animation;
   - LOD: only characters near the player and in view; a handful of chains costs microseconds.
   - In VR the hands could brush hair aside through the same contact capsules (an interaction design: owner's call first).
4. Ceiling: convincing sway, bounce and swing as in 2013-2018 card-hair games; not strand-level.

### B. Shading: tune the two highlights, then perhaps replace the hair pass's pixel shader

The cheap step is the material's own parameters (exponents on both games, intensities and colours on ME3), set at run
time on the hair component's own material instance. Beyond that: new UE3 materials need compiled shaders, and
modders can't compile LE materials today **[verify]**, so the route is the mod: identify the hair material's pixel shaders
at creation (the `[MEVR]` `MevrBridge::NotePixelShaderCreated` seam and `core/d3d11/ShaderReflect` already read every
pixel shader's constants) and bind our own for those draws with two shifted anisotropic lobes along the card's tangent
(the GPU skin vertex factory carries TangentX/Z), keeping the game's light inputs. Works on every hair at once, no content.
Risk: matching the game's lighting constants per permutation, and the one-Draw replay's second eye (the replay records
the bound shaders; ours must be in the recording).

### C. Strand hair (TressFX-class)

Real strands (AMD TressFX, MIT) drawn in our own pass against the game's depth: grooms authored per character, lighting
approximated from the pawn's light environment, shadows and translucency sorting of our own, inside the stereo and replay
pipeline, on a frame already near budget in VR (research/22). Months of work with a high risk of looking pasted on. Only
after A and B, if ever.

## 4. Next steps if the owner wants it

**The plan of the options, step by step, is [docs/trilogy-hair-plan.md](../docs/trilogy-hair-plan.md).** In short:

1. A + ME3's Ashley and Jack: the core and the ME3 adapter, checked on the fake headset in a conversation with Ashley.
2. Check LEX's import of a hair mesh with added bones (one LE2 Miranda chain set), then Miranda on ME2.
3. B as a separate prototype: one hair pixel shader swapped, same-pose snapshots before and after.

## The first runs in ME3 (2026-11-19, fake headset)

Three runs (`tools/le2-halcyon-run`, `-Game LE3 -RendererPreview`, branch `hair-miranda`). What they showed:

1. **ME3's Miranda is an `SFXStuntActor`, not a pawn** (`BioD_CitHub_Miranda`, `BioD_End001_436CRMiranda`; archetype
   `char_henchmen.Archetypes.NonParty.Miranda.Miranda_Conversation`): `BodyMesh`, `HeadMesh`, `HairMesh` (`HairSMC`) of its
   own, not on the world's pawn list. `game/Hair` scans for stunt actors once a second. Her Citadel level holds the actor at
   (-8220, 18941, -184011) facing -X, a use module and her own `SFXSeqAct_StartConversation`.
2. **The stand-in loads**: `DynamicLoadObject` found `HMF_HIR_PROMiranda_MDL_Old` and `SetSkeletalMesh` put it on her hair
   (123 bones). Its 13 hair bones are **not two-bone chains**: the `...Top` bones hang from `Neck`/`Neck1`, the
   `...Bottom` ones from `Chest2`/`LeftCollar`/`RightCollar` (13 one-bone chains).
3. **A stunt actor's hair is posed only while something animates it**: in London outside her conversation (T pose, unlit)
   no `UpdateSkelPose` call came for her hair; the Citadel docking officer in her arrival conversation got 6,152.
4. **The rigged hair breaks on a body without hair bones, with our stage off too**: on the seated docking officer the
   bangs sat on her head and the rest floated above and behind it (snapshots 2026-11-19_14-07-19 and 14-08-00). The hair
   follows the body (`ParentAnimComponent`); bones the body lacks get nothing from it and stay at the mesh's reference pose
   in the component's space (standing height), so a seated or moving character leaves them behind.
5. **At `UpdateSkelPose`'s return the follower's `SpaceBases` aren't the drawn pose**: every bone of the hair read at the
   component's origin, and what the stage wrote there never showed: the copy from the body comes later and replaces it.

What it means for A1 (docs/trilogy-hair-plan.md 4.3):
- The stage has to run **after the engine's copy from the parent** (find where LE3 fills a follower's SpaceBases from
  `ParentAnimComponent`/`ParentBoneMap`: Ghidra, callers of 0x796EB0 and the component's transform update).
- The hair bones' animated targets must be **built by us**: each hair bone's reference-pose local transform (RefSkeleton)
  composed on its posed parent bone (Neck, Neck1, Chest2, collars), because the engine gives them nothing. With that, the
  stand-in also works on bodies that lack the bones (every body).
- Fixtures: a stunt actor in a conversation. `Save_0031` moved to `BioP_CitHub` by a scratch script on `tools/le3-save.py`'s
  codec (base level, location, the hub's `levels`) loads into the Citadel's docking conversation (the saved place is
  ignored); its officer is a ready test body. Never clone the player's pawn (`Actor.Spawn` with it as the template crashed
  ME3 in the copy's first pose update, exe+0x9C44D4).

**Correction, same day: Miranda's own ME3 body has the hair bones.** `BIOG_HMF_ARM_MIR_R.MIRa.HMF_ARM_MIRa_MDL` (in
`BioD_CitHub_Miranda.pcc`) has 123 bones with the same 13 hair bones as `HMF_HIR_PROMiranda_MDL_Old`: same names, same
parents, the same reference positions and rotations to three decimals (the probe's `@refpose` listing, compared line by line;
the only difference a signed zero). BioWare rigged her body and the old hair together. So on Miranda the engine's copy from
the body gives every hair bone its place: point 4 above is the docking officer's body (no hair bones), not hers, and our own
posing of the hair bones is needed only for the stand-in on other bodies. What remains for Miranda is point 5: the stage must
run after the copy from the parent.

## Miranda's hair moves (2026-11-19, third run)

- **The follower rule, read in ME3's code**: `UpdateSkelPose` (0x796EB0) composes nothing for a component with a
  `ParentAnimComponent` (+0x308): it copies flags and returns. The drawing takes each bone's matrix from the parent's
  `SpaceBases` where the parent has that bone (`ParentBoneMap`, +0x310), else the component's own (identity unless posed:
  the docking officer's floating hair). So **the stage simulates on the body** when the body the hair follows carries the
  hair bones (`game/Hair` `SimulatedOn`; Miranda's MIRa body has all 13) and writes them into the body's pose after the
  body's `UpdateSkelPose`; the body's skin doesn't use them.
- **Gravity 0 by default**: the authored hair pose already hangs as gravity has it; gravity on top dragged every one-bone
  strand to its bend limit (2-16 cm sag). Motion now comes from the head and body moving, the hands and wind.
- **Collisions push a point out on its rest point's side** (`Collide`, `keepRest`): two strands that had crossed the neck
  capsule's axis stayed 7 and 16 cm off on the wrong side; unit test `TestCollision`'s crossed tip.
- Result (trace, flick of 2 m/s): all 13 strands at their styled place at rest, swing 1-7 cm, settled within 1 cm in
  0.06-0.36 s, worst stretch 1.3 mm. Visible in the snapshot (the hair behind her shoulder swings out and back). The settle is
  quick (stiffness 0.06, damping 0.10 per 1/60 s): the feel is for the headset and the owner.
- **The fixture**: `Save_0031` moved to `BioP_CitHub` (scratch script on `tools/le3-save.py`'s codec) loads into the docking
  taxi conversation; `mevr hair call bioconversationcontroller QueueReply nReply=5` ("No thanks", a position in the wheel);
  `mevr hair stream biod_cithub_miranda` (then `stream mirandat2 off`): her level's "level is live" event starts her
  conversation at her apartment, Shepard on his mark facing her. `mevr hair fire <op>` and `bring` are the other tools.

## Footage: how it looks walking (2026-11-19, fourth run)

`tools/footage.ps1` (tools/motion/README.md "Footage"): Miranda outside her apartment walking the generated `walk-sway` clip
in place, side-on (`mevr anim clip miranda walk-sway 1 0 0 90`), slowed 100x, 73 snapshots = 26 frames per second of motion
over two loops; then 37 more with `mevr hair off` for an A/B at the same pose and crop (the GIFs stay local: game images).

- **It reads as one sheet, not strands.** The simulation moves the back panel of the hair: it trails out behind the neck and
  swings back with the step, visibly more than the game's own (off: the hair is rigid with the head). It never parts or
  separates: the mesh is a shell of cards over 13 bones in short chains, so each bone carries a whole region, and there is
  nothing between them to move on its own. More bones along the same chains (the re-rig route, docs/trilogy-hair-plan.md
  section 3) would let the panel bend in more places; strand-level motion needs another hair model altogether (cards with
  their own bones per clump, or a strand renderer), which the owner's papers may speak to.
- **The swing is small and settles fast** (stiffness 0.06, damping 0.10): at walking pace a few centimetres at the ends. Softer
  settings would show more, at the risk of the panel looking loose; that's for the headset.
- **A walk out of the camera's view**: with a walk and turn 90 she walked along the corridor out of the fixed first-person
  view; footage walks in place. Each range end of a walk is a 180-degree turn, which resets the hair chains (136 resets in
  one run): a walk for hair should turn smoothly.

## The re-rig in the game (2026-11-19, fifth and sixth runs)

The plan's A3 and P3 (re-rigged meshes; does the game take a mesh with added bones), done without Blender or Legendary
Explorer's importer: `tools/rig` (README there) edits the meshes' binaries through Legendary Explorer's library.

- **The game takes a mesh with added bones.** ME3 loaded `MEVR_Miranda.pcc` (installed in `CookedPCConsole` with a TOC
  entry), and Miranda wore `HMF_ARM_MIRa_MEVR` (155 bones: 123 + 28 hair-chain + 4 soft-tissue) and
  `HMF_HIR_PROMiranda_MEVR` (151 bones), drawn exactly as before at rest, textures and all: the materials are imports by the
  original's path, resolved against the loaded originals (`RequireImportsAlreadyLoaded`).
- **LE meshes keep no CPU copies of their vertices**: the GPU skin buffer is the only copy (rigid vertices too, one bone at
  255). Miranda's body: 4 LODs, one chunk each (64 bones in LOD 0's map, room for 11 under UE3's 75); her `_Old` hair: one
  LOD, 3,881 vertices, a 14-bone map.
- **Hair chains generated from the hair mesh**: 7 chains of 4 bones (back, back-sides, sides, front locks; the face the
  open front), from the hairline at 170 cm down to each sector's tip (142 cm behind, 149 at the sides); every vertex
  re-weighted. At rest the chains sit on their targets (0.0-0.2 cm).
- **Chains hung from the head turned the hair on the shoulders with it** (her head turned in the conversation's idle: the
  side lock went behind the ear and uncovered a stray card): the game's own rigs hang the lower hair bones from the neck and
  collars. `core/ik/HairChain` `followBody` (0.7): bone i of n targets the pose the upper chest carries by followBody * i /
  (n - 1), the relation taken from the **bind pose** (`engine/Skeleton` reads each bone's rest transform: `FMeshBone`
  orientation at +0x10, position at +0x20); recorded from the live pose instead it changed nothing (the head was already
  turned). With it the hair matched the original's shape at rest. Unit test `TestFollowBody`.
- **Through the generated walk** (side-on, 26 frames per second of motion slowed 100 times): the back of the hair bends
  along its length and swings out behind the shoulders and back, where the 13 one-bone strands moved one panel. It is still
  a shell of cards: it bends, it doesn't part into locks.
- **Her look** (B1, `game/HairLook`): the master material (`HMN_HED_PRO_MASTER_HAIR_MAT_USER`) has two highlight lobes,
  `Highlight1SpecExp_Scalar` 50 / `Highlight2SpecExp_Scalar` 750, `Highlight1Color`/`Highlight2Color`,
  `Hightlight1Intensity`/`Hightlight2Intensity` (the game's spelling); hers: a dark broad lobe (0.108, 0.070, 0.053) and the
  default reddish tight one (0.57, 0.25, 0.14). Three same-pose sheets (the apartment's wide shot, cool window light):
  changes under 2x intensity barely show; a highlight colour multiplies the light's, so under this blue light a neutral
  blond reads cyan and a warm one (1.3, 0.85, 0.35) golden. Picked: broad lobe (0.40, 0.27, 0.11) x2 at exponent 90, tight
  lobe (1.3, 0.85, 0.35) x2.8 at 200 (`kMirandaLook`); `[Hair] look` overrides it.

## The same on ME2 (2026-11-19, seventh run)

`tools/rig/plans/le2-miranda.jsonc`, the ME3 plan with ME2's meshes, no code change in the tool: her ME2 hair
(`HMF_HIR_MIR_MDL` from `BioH_Vixen_00.pcc`, 44 bones, no hair bones at all, 4,370 vertices) got 7 generated chains and
was re-weighted whole; her body (`HMF_MIR_LGTa_MDL`, 110 bones: the same mesh and rest pose as ME3's conversation outfit,
6,370 vertices) the chain bones and the four soft-tissue bones with ME3's numbers. Two table entries in
`engine/GameAdapter.hpp` (LE2). In the game (Freedom's Progress, the runner's Save 4): Miranda, a squad pawn here, wore
both, the hair simulated on her body (ME2's follower rule as ME3's), drawn intact in her combat stance, the chains
0.4-0.9 cm off their targets through the generated walk, the soft tissue up to 1.3 cm in ordinary squad motion. A test's
teleport of her (`mevr hair bring`) or a clip's snap from her aiming pose swung the tips up to 39 cm for a moment, settled
at once. Not yet on ME2: her look (ME2's hair master material, behind the same effects wrapper, shows no highlight
parameters there: the look needs the wrapper's own parent, research/trilogy-hair.md section 1's ME2 row).
- **ME2's look, reached but not picked** (2026-11-19, eighth run): ME2's hair chain is the instance, the effects wrapper
  (`HMN_HED_PRO_MASTER_HAIR_MAT_USER`, effects parameters only) and its `m_pBaseMaterial`,
  `BIOG_Humanoid_MASTER_MTR_R.Human.HMN_HED_PRO_MASTER_HAIR_MAT`, whose parameters are the hair's: `Highlight1Specularity`
  50 and `Highlight2SpecExp_Scalar` 250 (the two exponents only: no highlight colour or intensity, so no blond tint on ME2
  without B2, the shader route) and `HED_Hair_Colour_Vector` (hers 0.016, 0.011, 0.008). `game/HairLook` now follows a
  wrapper to its base and takes every material's parameters in the chain. At Freedom's Progress at night, backlit, four
  exponent pairs (50/250, 90/200, 150/300, 300/200) changed her hair by about 3 of 255 on average (noise): her hair is a
  silhouette there. ME2's look stays the game's until a lit scene or the headset can judge it.

## The hair through the body (2026-11-19, the owner's headset verdict)

The owner, after the first-person show in Miranda's apartment: the jiggle is "pretty reasonable", max a little better; "the core
problem is actually the hair, because the hair is clipping through the body, so it doesn't lay flat against the anatomy", and
it "re-triggers" when the animation restarts.

**The re-trigger.** Three causes, all fixed (tools/motion/README.md "Playing a clip in the game"): a loop's last frame was
never its first (the wrap jumped), a new clip snapped in at once, and the clip's `turn` was applied at once (a 180 is a turn
past the simulation's 60-degree reset). The loop is closed, clips blend in over 0.4 s and turns are eased.

**The clipping, measured.** The body's surface fitted from her meshes (tools/rig/README.md "Body spheres"; `mevr-rig
colliders`), then `mevr-hair-bench --spheres` over the walk, the jump and the CMU dance. The capsules the hair collided with
were guesses that didn't fit her: her neck is 5 cm from its bones and the 13 cm spine capsule reached up past it (the nape's hair
sat inside it, so it was never pushed), the tops of her shoulders 9-13 cm from their bones against 7 cm collars (strands passed
through). How much deeper than its own pose the simulation took the hair, mean / peak / steps past 5 mm:

| Underwear body (NKDc) | capsules only | with the fitted spheres |
| --- | --- | --- |
| walk | 5.1 / 43.6 mm / 40% | 1.7 / 11.1 mm / 4% (jitter 0.69 mm, settled 0.22 s; before 0.78, 0.26) |
| jump | 6.0 / 34.4 mm / 39% | 2.3 / 17.9 mm / 11% |
| dance | 2.9 / 33.4 mm / 19% | 1.3 / 17.7 mm / 9% |

Tried and dropped on the way: laying the animated targets on the surface (the bones run 3-5 cm deep at rest by design: pushed
out, the strands swung 15-30 cm and never settled), pushing out by the full radius (the same fight), mirroring pushes by the skin's
normal (no better). Her suit (MIRa) keeps the capsules: its high collar's inside held the nape's hair against the spheres (the
walk 8.7 mm deeper against 7.1 without, never settled); a suit with a collar needs its own rule. [verify] in the headset: the
underwear body walking, jumping and dancing side-on and from behind.

In the headset after the fixes (2026-11-19, late): the front strand at the collarbone fixed by the hair's 3 cm thickness against the surface (`surfaceRadiusM`). With `followBody` 1 for the session the hair around her neck moved "almost like a large blob" in one direction (its lower half carried by the chest, its upper by the head); back at the shipped 0.7 the owner: "that looks better". `followBody` stays 0.7.
