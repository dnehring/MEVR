# 47: A real mesh with a live screen in the game world, at run time (2026-10-31)

**Question.** The Mako cockpit is planned as a real mesh in the world with live screens showing a third-person view. Before
anyone packages custom art: **can the mod, at run time, with nothing in the game's install changed, put a static mesh in the
world in front of the player, move it every tick with the player or the Mako, and show a live SceneCapture2D render target as
a texture on it, drawn with correct depth in both stereo eyes?**

**Built:** a probe, not a feature: `game/CockpitProbe` (mod side, hot-reloaded, MIT), driven by `mevr cockpit ...` console lines
(`tools/console.ps1`, `game/ConsoleRequest`), inert until asked (one early return a tick, no handle resolved). No host change,
no HostServices bump (it hot-reloads into v22), no new raw address. **Nothing here has been run in the game: everything about
the engine's behaviour is [verify] until the run below.** The layouts and signatures below come from LExSDKv2's LE1 headers
(read only; the code resolves them through `src/ue3`, with rows in `engine/ReflectCheck`). What the engine does with them is
from general UE3/UDK knowledge; Ghidra wasn't used for this.

## 1. What it does and why

### The actor

`Engine.Actor.Spawn` on the player controller, with `game/ScopeCapture`'s trick: `bStatic`/`bNoDelete` cleared on the class's
default object for the one call and put back at once (research/39 section 2). `mevr cockpit classes` prints each candidate's
defaults; the spawn's answer says what each one did.

| Class | Expected defaults (UDK memory) **[verify: `classes`]** | Role |
|---|---|---|
| `Engine.InterpActor` | `bNoDelete`, movable, `PHYS_Interpolating`, has `DynamicSMActor.StaticMeshComponent` and `LightEnvironment` | **First.** Made to be moved by the game, lit dynamically |
| `Engine.KActorSpawnable` | spawnable as it is, `PHYS_RigidBody` | Second. The physics are turned off after the spawn |
| `Engine.StaticMeshActor` | `bStatic`, `bNoDelete`, own `StaticMeshComponent` field, no light environment | Last. Static lighting assumptions may leave it black |
| `Engine.KActor` | `bNoDelete`, rigid body | Only with `spawnclass` |
| `Engine.DynamicSMActor` | abstract in UDK: Spawn should refuse it | Only with `spawnclass` |

After the spawn it is made harmless: `SetPhysics(PHYS_None)`, `Actor.SetCollision(false, false, true)`,
`PrimitiveComponent.SetActorCollision(false, false)`, `SetBlockRigidBody(false)`, `SetTraceBlocking(false, false)`, so it
doesn't stop the player, catch shots or the aim traces (`Actor.Trace`), or join physics. `LightEnvironmentComponent.SetEnabled(true)`
when it has one (UDK's `DynamicSMActor` template starts disabled, which draws unlit-black [verify]).

### The mesh

`StaticMeshComponent.SetStaticMesh(mesh, bForce = true)` through ProcessEvent, then the component's `StaticMesh` field is read
back; a mesh that didn't take clears the spawn and says so. `DynamicSMActor.SetStaticMesh` is avoided: its optional translation
and scale would arrive as zeros through a ProcessEvent block.

The default mesh is chosen by a score over all loaded `StaticMesh` objects: a name suggesting a panel, screen, plane or box, a
size a screen could be, flat. **LE1's `StaticMesh` has no script property for its bounds** (the SDK shows `UnknownData` where UDK
has them), so `list` takes extents from a live `StaticMeshComponent` that shows the mesh (`PrimitiveComponent.Bounds`, with that
component's scale); a mesh nobody placed in this level shows `(not placed here)`. After the spawn,
`MeshComponent.GetUnscaledBounds` gives the true half-sizes and the flat axis, which sets the default turn so the flat side
faces the head (`rot` overrides it; a one-sided panel may need `rot 0 180 0`).

### Moving it

Each tick (late in the mod's tick, the view anchor current): `distance` metres ahead of the head through `game/ViewAnchor`
(`frame.headWorld`, `OrientationToWorld(frame, head).forward`, `frame.unitsPerMeter`), facing the head, by `Actor.SetLocation` and
`Actor.SetRotation`, then `ForceUpdateComponents(false, true)` so the transform reaches the render thread before the next Draw
(research/39 section 5). Without a head-tracked view it uses the player camera, so the probe also runs on the desktop.
`follow 0` leaves it still in the world.

`attach`: `Actor.SetHardAttach(true)` then `Actor.SetBase(pawn)` on the controller's pawn (the Mako when driving); the tick stops
moving it and the engine carries it. This is the shape of the real cockpit. The answer reads `Actor.Base` back.

### The screen

- **Render target:** `TextureRenderTarget2D.Create` on its default object, 512x288 by default, FloatRGBA (`rgba8` to compare; a
  float target carries no gamma question), **clear colour purple (0.5, 0, 0.5)**: a purple mesh means the material took the
  texture and no capture landed yet.
- **Capture:** a `SceneCapture2DActor` spawned and set up like the scope's (research/39 section 2: no update skips, `MaxUpdateDist`
  huge, the player's post-process chain), `FrameRate` 30 (`rate N`), field 70 degrees, each tick `SetView` from 6 m behind and 3 m
  above the pawn (`chase B U`), looking at it, with `ForceUpdateComponents`.
- **Material instance:** script's `new` is an opcode, but **`MeshComponent.CreateAndSetMaterialInstanceConstant` is script that
  does it** (UDK's body: returns a MIC already on the element if it lives in the map or the transient package, else `new`s one
  with the element's material as parent and sets it). So nothing is needed from outside. A level's own MIC would be handed back
  and changed by us, so the element first gets the plain `Material` at the bottom of the chain, and our new MIC is then
  re-parented with `SetParent`.
- **Parent choice:** `material <name>` if given; else the mesh's own material if it has a texture parameter (that shader is known
  to draw on a static mesh); else the best-scored loaded one. Texture parameters come by two routes: every MIC's
  `TextureParameterValues` up its `Parent` chain, and the base `Material.Expressions`' `MaterialExpressionTextureSampleParameter`
  (a cooked material may have lost its expressions [verify]; the instances still name theirs). The score favours a parameter wired
  straight into `Material.EmissiveColor`, then `MLM_Unlit`, `BLEND_Opaque`, `bUsedWithStaticLighting`,
  `bUsedWithLightEnvironments`, names like screen or holo. `mevr cockpit materials [filter]` prints them all with their parameter
  names.
- **Parameter:** `param <name>` if given (matched against the loaded material's parameters: there's no way to make a new name
  here, and a name no loaded material has isn't a parameter of one); else the emissive one, then names with emis, glow, screen,
  video, diff, albedo, tex, color. `MaterialInstanceConstant.SetTextureParameterValue(name, target)`, read back through
  `MaterialInterface.GetTextureParameterValue`.

Every element of the mesh gets the instance, and it survives a change of mesh (`mesh <other>`).

### Life

`clear` disables the capture and destroys both actors (`Destroy`; if the engine refuses, `SetHidden`); the render target and the
instance have nothing to destroy, the garbage takes them once nothing refers to them. A new level forgets everything unread
(`engine::World()` changed: the old world's objects are gone); a part gone in the same world destroys what's left, as
ScopeCapture does. The mod's shutdown (hot reload) destroys; at process exit nothing is called. Settings (`dist`, `scale`, `rot`,
`material`, `param`...) survive `clear` and a level, not a reload. **Spawned objects stay in memory only; nothing is written to a
save or the install [verify: saving with it up doesn't save it; `clear` before saving until checked].**

## 2. Running it

The game needs the host now deployed (v22) and the rebuilt `LE1MEVR_mod.dll`: rebuild `MEVR-LE1-mod` while the game runs. Stand
somewhere with something behind the mesh (a wall a few metres off shows depth best). Every line is one `tools/console.ps1`:

```powershell
tools/console.ps1 "mevr cockpit classes"            # 1. each class's defaults: read before spawning
tools/console.ps1 "mevr cockpit list"               # 2. loaded meshes with extents
tools/console.ps1 "mevr cockpit materials"          # 3. materials with texture parameters: the crucial list
tools/console.ps1 "mevr cockpit mesh"               # 4. spawn + default mesh; the answer says what each class did
tools/console.ps1 "mevr cockpit status"
tools/snapshot.ps1 -Tag cockpit-mesh                # 5. A: the mesh, its own material
tools/console.ps1 "mevr cockpit dist 0.8"           # 6. nearer than the far hand, farther than the near one: raise the gun into it
tools/snapshot.ps1 -Tag cockpit-depth               #    B: occlusion
tools/console.ps1 "mevr cockpit dist 1.5" "mevr cockpit screen"     # 7. render target, capture, material instance
tools/snapshot.ps1 -Count 3 -Interval 1 -Tag cockpit-screen         #    C: the picture, changing between shots
tools/console.ps1 "mevr cockpit param <name>"       # 8. if the mesh looks unchanged: try each name `materials` listed
tools/console.ps1 "mevr cockpit material <name>"    #    and other parents
tools/perf-probe.ps1 -Seconds 20 -Tag cockpit-on    # 9. cost, against `clear` (and the log's `render levers:` line's captures)
tools/console.ps1 "mevr cockpit clear"
tools/perf-probe.ps1 -Seconds 20 -Tag cockpit-off
```

In the Mako (`mevr cockpit attach` is the real cockpit's shape; a save near a Mako section):

```powershell
tools/console.ps1 "mevr cockpit mesh" "mevr cockpit dist 3" "mevr cockpit screen" "mevr cockpit attach"
tools/snapshot.ps1 -Count 3 -Interval 2 -Tag cockpit-mako      # drive between shots: the mesh stays put relative to the hull
tools/console.ps1 "mevr cockpit detach" "mevr cockpit clear"
```

**What to look for in the snapshots** (`-left.png`, `-right.png`, `-left-world.png`; the JSON's `"mod"."cockpitProbe"`):

| | What | Means |
|---|---|---|
| A | The mesh in both eyes, offset by the disparity of something 1.5 m away (against the eye poses in the JSON; `tools/stereo-check.py`'s way of measuring) | Spawning and moving work, and the world pass drew it: it's a world object in both eyes |
| A | In one eye, or popping out frame to frame | Visibility: Umbra or the second-eye path (research/35, research/37) doesn't treat a moving spawned primitive as visible. Check `sharePass`, `umbraEyeMatrix`, `minScreenRadiusPx` before concluding |
| A | Black mesh | Lit but no light reaches it: the light environment, or a lit material on a dark spot. Not fatal: an emissive screen doesn't care |
| A | Grey or checker default material | The cooked material has no shader for this use: another mesh or material |
| B | The arms and gun cut across the mesh where nearer, the mesh hides what's behind it, in both eyes | **Correct depth: the whole point** |
| C | Purple | The material took the texture; no capture reached it: the levers line's `captures`, `bEnabled`, the rate |
| C | The third-person scene on the mesh, moving between shots | **The route works.** Note the orientation (UVs can be anything on a found mesh; a cube's faces are the clean case) |
| C | No change from A | The parameter isn't used by the parent: `param`, `material`, or the read-back line said "not found" |
| C | Black rectangle in the picture | The capture sees its own screen: the mesh is in its field and D3D11 refuses to read a target being written. Expected from this camera position **[verify]**; move it with `chase`. The real cockpit needs the capture not to see the cockpit (section 4) |
| All | Following the head (`follow 1`), the mesh trails a fast head turn by a frame or two | The latency of a world object placed on the game thread: judged in the headset, and irrelevant for the attached case (attached, it's fixed to the hull and the head moves freely) |

Logs: `cockpit probe:`, the heartbeat's `cockpit probe:` line, `render levers:` (`captures ...: N a frame`: about 1 render every third
frame at the default rate; RenderLevers keeps a capture out of the eye bookkeeping, research/39 section 3).

## 3. What each outcome means for the cockpit

- **Everything works (mesh, depth, picture):** the real-mesh route. A cockpit is then a matter of bringing our own mesh and
  material (section 5); the screens are this exact mechanism. Depth, lighting, shadows and the arms all come for free, because the
  engine draws it in the eye pass.
- **Mesh and depth work; no loaded material takes the texture usefully:** the mesh route still stands: the screen needs a parent
  material of our own (a package, section 5). Alternative without a package: the host draws the screen only, as a depth-less quad
  from the render target's D3D texture (`plugin/ScopeCaptureSource` finds it already, research/39 section 4), placed with the
  mesh's pose; it would draw over the arms unless the near-range depth is matched.
- **The picture works, but only on a lit material:** usable with a light, or as a proof while a package brings an unlit one.
- **The mesh fails in one eye or pops (Umbra or the replay paths):** look for what those paths need (a dynamic primitive's handling,
  `perEyeOcclusion`, `sharePass` off) before abandoning; if it's inherent, **the host draws the cockpit**: its own mesh on a
  layer, without the world's depth, so hands in front of it need a mask (the design this probe hopes to avoid).
- **Spawning or moving fails outright:** check the classes' report; `Actor.SetLocation` refusing a class is a class matter, and
  hard-attaching doesn't need SetLocation at all. If no spawned actor can draw a mesh: host-drawn.
- **Cost too high:** `rate`, the target's size, `mevr cockpit screen 256 144`; the capture's Umbra pass is its fixed cost
  (research/39 section 6). A cockpit with three screens could share one capture (a target viewed through three instances).

## 4. Questions the run can't answer yet

- **The capture sees the cockpit.** There's no per-capture hidden list in the component's fields. What may serve: a capture placed
  outside the hull (it is: a chase camera behind the Mako looks at the hull's outside, and the inside of a one-sided cockpit mesh is
  culled from outside: a useful accident [verify]), `bOwnerNoSee`-style flags don't apply because a capture's view has no view actor
  [verify].
- **The cockpit inside the Mako's hull:** the player camera in first person sits inside the hull model, whose hide rules are
  Halcyon's. The cockpit must draw in front of the hull's inside, or the hull hidden: research/44's turf.
- **Motion:** the hull moves on the physics tick, then the attachment; whether the hard attach lags the camera by a tick shows as
  a shake relative to the eye. If it does, moving it in a `BioVehicleWheeled.Tick` post-handler (research/44's seam) is the next try.
- **LOD and streaming:** a mesh from another level's package goes away with it; our own package solves that.

## 5. What this says about packages [verify]

(The first run's answers and the loader's rules are in "Packaging our models" and "Loading a package" at the end.)

If the run works, the run itself shows the install needs nothing for the **mechanism**. For the real art the requirements
are then:

- **The mesh and its material must be loaded objects.** Today's probe can only show what the level has loaded; `list` differs per
  level. A custom cockpit needs a package loaded by the mod at run time (`Object.DynamicLoadObject` or similar through
  ProcessEvent, not tried here) or merged into a package the game loads (MassFPS-style DLC merge, research/11; ME3Tweaks' mod
  formats). That is a later test: **can a package outside the game's own be loaded at run time, without editing the install?**
- **A cooked material needs its shaders in a shader cache** (UE3 doesn't compile at run time): a new material means a shader
  map from a tool that can produce one. A parent the game already has (the unlit material `materials`
  finds here) avoids new shaders altogether: our package would then hold only a mesh, textures and a `MaterialInstanceConstant` over
  the game's material. **The name this run finds in `materials` is the best thing this experiment can hand the art path.**
- **Static-mesh usage flags** (`bUsedWithStaticLighting`, `bUsedWithLightEnvironments`, printed by `materials` for each candidate): the
  cooked shaders exist only for the uses the flags cover; our mesh drawn with a parent that lacks them shows the default material.
- **Package availability by level:** a parent from one level's package isn't in another's: an always-loaded package's material
  (Engine or SFXGame content, BIOC_Materials) is what the cockpit should rely on. Run `materials` on several levels and take the
  intersection.

## 6. [verify] list

1. Which classes spawn and what their defaults are (`classes`, the answer of `mesh`).
2. `SetLocation` and `SetRotation` each tick on the spawned actor (the first placement's log line; `last error`).
3. The mesh draws in both eyes with correct depth under the deployed stereo (`sfr2Metric`, `sharePass`).
4. The light environment lights it.
5. `materials` finds candidates; cooked `Expressions` present or not; a parameter wired into emissive exists.
6. `CreateAndSetMaterialInstanceConstant` makes a MIC through ProcessEvent; `SetParent` keeps it drawing; a static-permutation
   parent still draws.
7. The render target reaches the screen, with the capture at about one render per three frames (`captures`), at what cost.
8. `attach`: the hard attach holds on foot and on the Mako, and the hull doesn't shake against it.
9. `clear`, a level change, hot reload: nothing left behind, no crash after the GC; a save with it up.

## First run (2026-10-31, fake headset, Eden Prime, the save's level; `god` on, an idle Shepard dies there)

- **A spawned mesh works.** `InterpActor` spawned (its `bNoDelete` default cleared for the call), any loaded StaticMesh on it, placed from the head every tick: in both eyes with the right disparity, in front of the world, under two Draws and under `sharePass = 5`'s replay (one Draw kept running with it up). The stand-in rendered black with the light environment on: its lighting is still to look at.
- **`SetLocation`/`SetRotation` return false** on it: `execSetLocation` goes through `UWorld::FarMoveActor`, whose collision test refuses a spot the actor encroaches on (read in Ghidra; the same refusal CLAUDE.md notes for the player). The probe writes `Actor.Location` and `Rotation` and flushes the components instead.
- **`TextureRenderTarget2D.Create` takes only formats 2 (A8R8G8B8), 3 (G8) and 9 (FloatRGB)** (`execCreate`'s format check, Ghidra). FloatRGBA (10), which research/39's scope capture asked for, returns None, so the scope capture could not have created its target: fixed to 9, not yet run.
- **Material:** `EngineMaterials.EmissiveTexturedMaterial` (unlit, opaque, `Texture` wired into emissive) is loaded in the level and took the render target (a MIC over it; its purple clear colour showed on the mesh). `EngineMaterials.ScreenMaterial` (`ScreenTex`) is the same kind.
- **The capture breaks the eyes: the open problem.** With a capture enabled, the left eye image shows the capture's view (full size, the chase camera's) and the right eye is black under `sharePass = 0`, or the normal view under `sharePass = 5` (Halcyon's second Draw back on: `no pass-1 record`). The screen stays purple: nothing lands in the render target. The levers line counts 2 scene renders a frame and no capture. So Halcyon or the levers take the capture's render for an eye. research/39 section 6's "no seam needed" doesn't hold; the scope capture would do the same.

## Packaging our models (2026-10-31; tool run, not yet loaded by the game)

`tools/cockpit/package` (C#, `net10.0-windows` because LegendaryExplorerCore's texture code needs `System.Drawing.Common`; GPL-3.0 since it calls LegendaryExplorerCore; references Legendary Explorer's nightly 6.5.0.6686 by path, `-p:LexDir=` or `--lex-dir` to override; nothing of it, and no model, texture or package, goes in git):

```powershell
dotnet build tools/cockpit/package/CockpitPackage.csproj -c Release
dotnet tools/cockpit/package/bin/Release/net10.0-windows/cockpit-package.dll build --assets "<checkout>\concept\assets"
```

It writes `build/cockpit/BIOG_MEVR_Cockpit.pcc` and `.pcc.json` (every object's full name, triangles, vertices, material slots, texture) and then runs its own `check`. `--out`, `--max-texture N` (default 4096), `check <pcc> --assets <folder>`, `inspect <pcc> [--filter t] [--props] [--bin]`, `survey <CookedPCConsole>` (what the game's own objects hold, which is what the decisions below were compared with). It takes about 15 s.

**The run:** about 25.6 million bytes (24.4 MB, compressed as the game's are; a few hundred bytes differ between runs, the GUIDs being new), 17 exports, 6 imports, "All checks passed".

| Object (`BIOG_MEVR_Cockpit.`...) | Triangles | glb vertices | Welded | In the mesh | Slots |
|---|---|---|---|---|---|
| `Joystick_Base` | 11,468 | 34,404 | 17,364 | 24,520 | `Joystick_MIC`, `Backing_MIC` |
| `Joystick_Grip` | 11,240 | 33,720 | 15,118 | 22,307 | `Joystick_MIC`, `Backing_MIC` |
| `Throttle_Base` | 11,314 | 33,942 | 26,962 | 29,397 | `Throttle_MIC`, `Backing_MIC` |
| `Throttle_Handle` | 9,320 | 27,960 | 21,695 | 23,824 | `Throttle_MIC`, `Backing_MIC` |
| `Chair` | 23,160 | 69,480 | 45,051 | 45,103 | `Chair_MIC`, `Backing_MIC` |
| `Front_Cockpit` | 19,269 | 57,807 | 12,717 | 12,807 | `Front_Cockpit_MIC` |
| `Front_Cockpit_Backing` | 21,511 | 64,533 | 64,239 | 64,242 | `Backing_MIC` |

plus `Joystick_Diff`, `Throttle_Diff`, `Chair_Diff`, `Front_Cockpit_Diff` (4096x4096 DXT1, 13 mips, 10.7 MB each, stored in the package) and `Backing_Diff` (4x4, the backing's dark grey).

What the importer and the game forced, against what was asked:

- **The object names can't start `MEVR_Cockpit.` in a file called `BIOG_MEVR_Cockpit.pcc`.** The first part of a loaded path is the package the engine looks for as a file (next section), so the objects sit at the top of the package and are `BIOG_MEVR_Cockpit.Joystick_Grip`. `--out MEVR_Cockpit.pcc` would give `MEVR_Cockpit.Joystick_Grip`; no code depends on the `BIOG_` prefix that I found.
- **`Front_Cockpit` is two meshes.** A static mesh's index buffer is 16-bit and its elements have no base vertex; the file's two primitives weld to 76,956 vertices. So a model over 65,535 is split by material: `<name>` and `<name>_Backing`, the same origin and transform. Place both. LEC casts indices to 16 bits without looking, so the unwelded chair (69,480) would have been silently corrupt.
- **Welding is ours:** the glbs are mostly unindexed triangle soup. Only exact duplicates of position, normal and UV merge; corners keep their order. LEC's MikkTSpace then adds vertices where tangents split ("in the mesh" above).
- **Scale and axes:** LEC multiplies by 100 and swaps axes: glTF (x, y, z) becomes (x, z, y) x 100, so 1 glTF unit is 100 Unreal units. A mesh's origin is its glb's origin: the importer ignores the node's translation, and the detached parts' vertices are already relative to their pivot, so **each moving part's origin is its pivot**. `detached/manifest.json`'s pivots and axes in the same (x, z, y) x 100. The actor's scale does the rest.
- **What LEC leaves to fix, compared with `survey` (the game's meshes in 40 of its `_ART` packages):** the field after the LODs is the LOD count on every one (LEC writes 0; we write it). 1 UV at stride 12 occurs in the game. The position buffer's unknown field is 0 or 1 in the game, left as LEC's 1. The winding is the game's: the share of faces whose geometric normal agrees with its vertex normals is 0.0-2.1% on ours and median 0.0%, max 1.0% on the game's (the sign in Unreal's coordinates).
- **Textures:** DXT1 with no alpha, through LEC's `Texture2D.Replace` as package-stored, `NeverStream` (the game's own package-stored textures are the same shape), group `TEXTUREGROUP_Vehicle` because BIOEngine.ini gives it `MaxLODSize=4096` where `TEXTUREGROUP_World` has 1024. The last mips read 4x4, 4x4, 4x4: so do the game's. `check` compares the mean colour after compression (channels in the right order).
- **Parent:** the instances' parent is an import of `EngineMaterials.EmissiveTexturedMaterial`, parameter `Texture`, its expression GUID copied from the game. **There is no `EngineMaterials.pcc`**: that material is a forced export inside `Startup_INT.pcc` (`Unlit`, not TwoSided), always loaded, so the import resolves by name; the tool reads Startup_INT to confirm parameter and path. The repaired models' inward backing shell is `Backing_MIC` (or its own mesh): one-sided surfaces with the shell behind them, which is what the repair was for.
- **Package flags** as the game's: `AllowDownload, Cooked, DisallowLazyLoading, RequireImportsAlreadyLoaded`, plus `Compressed` from LEC's save. Every object `Public, LoadForClient, LoadForServer, LoadForEdit, Standalone`.
- **Collision:** the elements' collision flags are off, no body setup, and LEC's `UseSimpleBoxCollision` left in (the probe's actor has collision off anyway).
- **What `check` can't say:** that the game accepts it. Everything above is against LEC's reading and the game's other packages.

**A lit parent, later (candidates, not solved):** the instance only needs a parent whose shaders are cooked for what we draw. `EngineBuildings.BuildingQuadMaterial` (Startup_INT; `DiffuseTexture`, `WindowMask`, `SpecularTexture`) and `EngineBuildings.ProcBuildingRoofMaster` (`BaseTexture`, `VariationTexture`, `MaskTexture`) are always loaded and lit; `BIOG_APL_MASTER_MATERIAL.APL__MASTER__Mat` (in BIOC_Materials.pcc; `Diffuse`, `Emissive`, `Specular`, `OpacityMask`, `SpecularPower`) has the right parameters. To find out: whether a parent is loaded in every level (`Startup_INT`'s always is; `BIOC_Materials` is listed as a `Package=` in BIOEngine.ini's `[Engine.PackagesToAlwaysCook]`, not read further), whether its cooked shader maps cover the static mesh's vertex factory with a light environment and no lightmap (`bUsedWithSkeletalMesh`-style flags are no guide: a level's meshes use these with lightmaps), and what its other parameters do at their defaults. The glb's normal map (`front-cockpit` has one) needs a BC5 texture and a parent that takes it.

## Loading a package (2026-10-31; from the engine, not yet tried in the game)

`mevr cockpit load <Package.Object> [class]` (class default `Engine.StaticMesh`) runs `Core.Object.DynamicLoadObject` (static native, ProcessEvent on the player controller, game thread, `MayFail` set, ReflectCheck rows) and says what came back. `mevr cockpit mesh` with no name then shows what `load` returned, and `mesh <Package.Object>` goes to the loader by itself if nothing loaded has that exact path. **Run them in one console call:** nothing holds a loaded object until a component does, and the next garbage collection may take it [verify].

Read in Ghidra (RVAs of 2.0.0.48602, prose only):

- **`execDynamicLoadObject` (`0x11FC40`)** calls `StaticLoadObject` (`0x170790`): it splits the path, looks for the object in memory, then calls the package loader on the path's **outermost** name with no file name given (`0x162460`, `GetPackageLinker`'s role), and creates the object from that package's linker. Under seek-free loading it also tries `<Top>.SeekFreeShaderCache` (quietly, by the load flags) first: none in ours is fine, the game's shipping packages don't all have one [verify].
- **`FindPackageFile` (`0xB33450`, slot +0x10 of the object created in appInit)** is not the path-scanning cache of older UE3. It makes up to four candidate names (the name and language variants), adds each of `[Core.System] Extensions` (`upk`, `u`, `sfm`, `pcc`) when the name has no extension, and asks the file manager (slot +0xD8).
- **That file manager slot (`0x9F31F0` -> `0x9EB7C0`) looks only in the mounted tables of contents.** It walks the list at the file manager's +0x30, and answers with the table's prefix plus the entry's relative path. There is no directory scan and no check on disk: **a package whose name no mounted TOC lists does not exist for the loader, wherever it sits.** `[Core.System] Paths` doesn't matter here.
- **Mounting (`0x9F2AA0`, parser `0x9F2C40`, format check `0xCD7DB0`)** has one caller, the file manager's start (`0x9EE020`), which mounts `<BioGame>\PCConsoleTOC.bin` with the prefix `..\..\`, once. **So a TOC change needs a game restart, and a DLC folder is no simpler:** nothing mounts a second table in this build. (`0x80C250`, which enumerates `DLC_*`, doesn't touch the TOC.) `BioGame\DLC` doesn't exist in this install either.
- **Opening** (`0xE7B20` -> `0xEA2E0`) is plain `CreateFile` with the size from `GetFileSize`, so the TOC's size field isn't what bounds that reader. Write the right one regardless: the async reader wasn't read.

The table is by the file's bare name: bucket = hash(name) mod buckets, the hash LEC's `TOCCreator` has. Checked on the installed file: **all 9,112 entries sit in the bucket that hash gives** (`BioGame\CookedPCConsole\Startup_INT.pcc` with its true size, flags 0).

### What to do (the game closed; a game update or a file verify can undo it)

```powershell
$game = "C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition\Game\ME1\BioGame"
$tool = "tools\cockpit\package\bin\Release\net10.0-windows\cockpit-package.dll"
Copy-Item "$game\PCConsoleTOC.bin" "<backups>\PCConsoleTOC.bin"          # 1. keep the original
Copy-Item "build\cockpit\BIOG_MEVR_Cockpit.pcc" "$game\CookedPCConsole\" # 2. the package beside the game's
dotnet $tool toc --toc "<backups>\PCConsoleTOC.bin" --out "scratch\PCConsoleTOC.new.bin" --add "build\cockpit\BIOG_MEVR_Cockpit.pcc"
Copy-Item "scratch\PCConsoleTOC.new.bin" "$game\PCConsoleTOC.bin"        # 3. the TOC with one entry more
```

The `toc` verb never writes over its input; it refuses a table its hash doesn't describe. Rebuilding the package changes its size: run steps 2 and 3 again. ME3Tweaks Mod Manager's AutoTOC rebuilds the table from the folder, and would keep the file listed while it's there.

**Undo:** `Remove-Item "$game\CookedPCConsole\BIOG_MEVR_Cockpit.pcc"`, and `Copy-Item "<backups>\PCConsoleTOC.bin" "$game\PCConsoleTOC.bin"` (or `toc --remove BIOG_MEVR_Cockpit.pcc`: on copies, adding then removing gave back the original byte for byte, SHA-256 `21B24FD0...`). The mod needs no undo: `load` is inert until asked. A save made with the mesh up: `clear` first (section 6's item 9 is still open).

**In the game (a rebuilt `LE1MEVR_mod.dll` hot-reloads into the host deployed now; no HostServices change):**

```powershell
tools/console.ps1 "mevr cockpit load BIOG_MEVR_Cockpit.Front_Cockpit" "mevr cockpit mesh" "mevr cockpit dist 2"
tools/snapshot.ps1 -Tag cockpit-ours
tools/console.ps1 "mevr cockpit mesh BIOG_MEVR_Cockpit.Joystick_Grip"
```

| What comes back | Means |
|---|---|
| `DynamicLoadObject returned None`, and the game's log says the package wasn't found | The TOC step: the entry's name, or the game wasn't restarted |
| The package is found, an import fails | `RequireImportsAlreadyLoaded` and the import's path; the object list of `check` |
| A crash in the load | `crash-breadcrumbs.txt`; the mesh binary's fields against `survey`; try a `Backing_Diff`-only load (`load BIOG_MEVR_Cockpit.Backing_MIC Engine.MaterialInstanceConstant`) to split mesh from texture |
| The mesh in LEC's checker-grey default | The instance didn't resolve its parent |
| Black | The parent's parameter didn't take the texture: `materials`, the instance's `TextureParameterValues` |
| The right colours, but inside out or the backing showing | One-sided faces and the winding; compare with the mean above |

### Outside the game folder? [verify]

Nothing in the loader cares where the file is, only what a mounted table says. So two routes exist that weren't tried: **(a)** the TOC entry's relative path can walk out of the folder (`--as ..\..\..\..\..\somewhere\BIOG_MEVR_Cockpit.pcc`, relative to `Binaries\Win64`), which still edits `PCConsoleTOC.bin`; **(b)** the mod could call the mount function itself (`0x9F2AA0`: the file manager, a folder holding a `PCConsoleTOC.bin` of our own, a prefix, a priority) to add a second table from `ASI\`, with **nothing changed in the install at all**. (b) is the shape for players: a new raw address (a row in `engine/Addresses.hpp`) and a native call at a safe moment, so it isn't in this test; the list insert takes the file manager's lock, so it looks safe after start, and whether anything caches earlier answers isn't known.

### What only the game can tell

That a package LEC made loads at all under this engine; that the linker takes our summary (no `NoExportsData`, `Protected` or `ServerSideOnly`, flags the game's have and ours don't); whether `Standalone` keeps the objects over a level change or they must be loaded again; DXT1 at 4096 in `TEXTUREGROUP_Vehicle` showing its top mip; the cost of 4 x 10.7 MB of never-streamed texture; and how a 65-thousand-vertex backing draws in stereo.
