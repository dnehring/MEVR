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
