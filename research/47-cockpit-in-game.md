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


## Why a capture took an eye (2026-10-31, read in Ghidra; fix built, not run)

**Where a capture renders.** `0x76EE20`, which `plugin/RenderLevers` and Halcyon call the scene render, is really the render thread's *view family* command: hit proxies, or else (show flag 8) a loop over the scene's captures, then the eye's `FSceneRenderer::Render` (`0x76BC00`, or its Umbra form), then the renderer's delete. Each capture's probe builds its own family on the render target, its own renderer and view, and calls `0x76BC00` or its Umbra form (`0x76ECD0`) **directly**, then copies the target's surface to its texture (RHI `+0x210`). So:

- **Why two renders a frame and no capture:** a capture never passes `0x76EE20`, so `SceneRenderHook` never sees it and `IsCaptureRender` had nothing to look at. It isn't a third render beside the eye's: it is nested inside the eye's command, before the eye's own passes, **with the eye's `t_renderIndex`, the eye's armed recording and the eye's one-Draw flags**. research/39 section 3's "its own `FSceneRenderer::Render`, so RenderLevers' hook sees it" had the wrong function.
- **Why `no pass-1 record` under `sharePass = 5`:** the pass hooks are `0x76BC00`'s callees, so the capture's world pass marked the eye's replay tail, and the capture's finish took `t_oneDrawFinish`: it ended the recording and looked for a pass-1 record of **the capture's** view. Three misses later Halcyon's second Draw resumed. (Reflection captures walk the same loop, so the same applied to them: the `no pass-1 record` at a floor-reflecting level may be this too [verify].)

**Where the picture went.** The capture's view family has the render target (the renderer's `+0x18`), and `FinishRenderViewTarget` (`0x75B2E0`) would copy to it. But with a post-process chain, which both the probe and the scope capture ask for:

- The uber post effect's render (`0x279360`, its sibling `0x27A100`), when it is the group's final effect and the family resolves, binds **the game's global backbuffer** through `FSceneRenderTargets` (`0x773D30`, BeginRenderingBackBuffer: the scene targets' own surface reference, and a viewport of the scene buffer's size), never asking the view family.
- It draws a quad whose clip space is computed from the **family's** target size, so it fills whatever is bound; the UVs are the capture's rect in the scene buffer. On the backbuffer: **the capture's view, 512x288 pixels stretched over the whole eye**. The snapshot's blocks and vertical stretch show it (2026-10-31_18-03-15).
- It then sets the view's `+0x4B4`, and `FinishRenderViewTarget` returns at once when that's set (no dynamic resolution running): **nothing is written to the family's target, the render target keeps its clear colour.** The probe's resolve copies the purple surface to the purple texture.

A stock game never meets this: its captures are reflections with no post chain.

**Why the left eye.** Halcyon's pass 0 is the backbuffer copied at a scene-sized depth clear with the backbuffer dirty, the last such copy before Present winning. A capture in the second Draw's command dirties the backbuffer with its composite, and the eye's clear right after copies that as pass 0: left = the capture. (That the capture does run in the second Draw is inferred from the picture: the update test reads a last-render time only the render thread writes, so the game thread's Draw 2 can still find one due [verify].) Halcyon has no fault here and needs no change.

**Not explained: the right eye black under `sharePass = 0`** (`-right-world.png` black too), against the normal picture under `sharePass = 5`'s fallback. What differs between those two is the stereo mode (2 with its off-centre frustum, against 1) and nothing in the capture's path I could read. Treat it as open: the run below says whether it goes with the rest.

**The fix (host, `plugin/RenderLevers`, no Halcyon change):**

1. A hook on `0x742C80`, the thunk the capture loop calls per capture: for its duration the render index is -1, the passes run (`t_skipPasses` off), the one-Draw flags are cleared, and the recorder and the eye scissor are suspended (`draw_replay::SuspendForCapture`). All put back after. Rules in `core/vr/SceneCaptureScope` (unit-tested). This covers every probe type, so reflection captures stop touching the eye's recording too.
2. A hook on `0x773D30`: inside a capture it binds the capture's own target (`+0x18`'s surface, RHI `+0x520`) and a viewport of its size (RHI `+0x340`) instead of the backbuffer; everywhere else it is one thread-local compare. The uber quad then fills the target as the engine meant, `+0x4B4` keeps the finish out of it, and the probe's resolve carries it to the texture. The post chain stays on, so a capture keeps the game's look.
3. A capture is counted when its finish runs (the levers line's `captures ...`, the `ran as a capture` line), and the eye's "draws the recorder missed" no longer counts the capture's draws.

**What this doesn't touch:** the capture's scene-sized depth clear still takes a pass-0 copy when the backbuffer is dirty, as reflections' do; the eye's own clear then finds it clean and the copy stays the previous Draw's finished eye, which is the right image. Its 512x288 draws don't dirty it (the gate compares the width with the eye's).

**FloatRGB in the scope's search:** `plugin/ScopeCaptureSource` checks size, samples, layers and bindability, never format, so the search doesn't reject it. `ViewFormat` reads `R11G11B10_FLOAT`, `R16G16B16A16_FLOAT` and `R32G32B32A32_FLOAT`, the mappings UE3's D3D11 has for it; which one this build uses wasn't read (the pixel-format table is filled at run time): the `scope capture source:` line says, and an unknown format keeps the eye copy with a warning. R11G11B10 has no alpha (reads 1) and no negative values, which suits a linear colour picture.

**Check in the game** (host redeployed): `tools/console.ps1 "mevr cockpit screen 512 288 rgba8"`, then `tools/snapshot.ps1`, under `sharePass = 0` and `5`:

| Log, eyes, screen | Means |
|---|---|
| `render levers: a capture's final post-process pass bound its own target ... (512x288)`, `ran as a capture`, levers line `captures ...: about 0.3 a frame`; both eyes normal; the mesh shows the chase view | Fixed: the route works |
| Eyes normal, the mesh still purple, `bound its own target` logged | The bind works and the texture isn't reached: the resolve (RHI `+0x210`) or a material matter |
| Eyes normal, purple, `ran as a capture` but no `bound` line | The uber effect isn't the final here (no post chain?): `FinishRenderViewTarget` should then have copied: look at the view's `+0x4B4` |
| `a capture's final pass found no target of its own` | A pass hook didn't note the renderer (`gPassHooksInstalled`?) |
| Right eye still black at `sharePass = 0`, left normal | The open question above, on its own: compare `sfr2MetricEyeFrustum = 0` (mode 1): if that clears it, the cause is in mode 2's frustum with a capture |
| `one Draw: no pass-1 record` while a capture is up under `sharePass = 5` | The eye's own view missed: the suspension isn't covering something; the record's position against the log's view says whose it is |

A capture target as wide as the eye in `R8G8B8A8_UNORM` would pass Halcyon's backbuffer-dirty gate (it compares width and format, not identity) and take extra pass-0 copies; below the eye's width it can't.

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

## Second run (2026-10-31 19:40, fake headset, same level, `god` on, `sharePass = 5`, all of the above deployed)

- **Our package loads.** With `BIOG_MEVR_Cockpit.pcc` in `CookedPCConsole` and an entry in `PCConsoleTOC.bin`, `mevr cockpit load BIOG_MEVR_Cockpit.Front_Cockpit` returned the StaticMesh and `mevr cockpit mesh` showed it in both eyes with correct disparity. Install record and undo: `UNDO.txt` in `%LOCALAPPDATA%/MEVR/cockpit-install`.
- **Its own material draws black** (the MIC over `EmissiveTexturedMaterial` with our DXT1 texture). So did a game stand-in with its own material in the first run, so the cause isn't settled: the texture's mips, the MIC's parameter or the probe's handling. Open (narrowed in the next section).
- **The capture fix holds.** With a capture up, both eyes are the normal view, one Draw stays on (1 scene render a frame), the levers line counts captures at 1.00 a frame and logs `ran as a capture` and the post-process finals into their own target. The black right eye from the first run wasn't seen in this mode; `sharePass = 0` was not rerun.
- **A live picture reaches the mesh:** on `EngineBuildings.BuildingQuadMesh` through a MIC over `EmissiveTexturedMaterial`, the render target's image shows on the quad in the world, in both eyes, darker than the eye image.
- **The capture's view doesn't follow `SetView`:** `chase 10 8` and `chase 12 10` left the picture the same, also with a full re-attach (`ForceUpdateComponents` with `bTransformOnly` false) every tick. It shows a close view with Shepard's hand, not 6 m behind and 3 m up. Whether the picture updates at all wasn't separated from that. The scope capture sets its view the same way, so it had the same problem. The cause is in the next section (`bUpdateMatrices`).
- **Cost, roughly:** `#0` in the levers line (the render thread's scene render, with the capture nested in it) went from 2.97 ms to 4.7-5.4 ms at a light spot with a 512x288 capture every frame. One spot, no A/B; not a measurement to plan on.
- `FrameRate` 30 didn't reduce the capture rate: it ran once a frame.

## The capture's view, its rate and the black mesh (2026-10-31, read in Ghidra and from the packages; changes built, not run)

### Why the view didn't follow SetView: `bUpdateMatrices` (read, and the picture fits; [verify] by the run below)

- `SetView` (`0x4FFBF0`) writes the component's `ViewMatrix` and sets `bNeedsSceneUpdate`. That's all. The render thread never reads the component: the **probe**, built on the game thread by the component's virtual at vtable +0x338 (`0x4F2060`), holds **a copy** of `ViewMatrix` (+0x90) and `ProjMatrix` (+0xD0), and `0x4EE4B0` builds the capture's view from those. A new view reaches the render thread only as a new probe.
- A new probe comes from `SetParentToWorld` (`0x4FE250`, +0x2C8), which every update of the component runs (a re-attach; a transform update too by UE3's `ConditionalUpdateTransform`, from memory, not read here [verify]; `Actor.ForceUpdateComponents` at `0x2CDEB0`/`0x2D6130` only marks the components and runs the actor's update), or from the component's Tick (`0x500910`) when `bNeedsSceneUpdate` is set.
- **But `SetParentToWorld` first overwrites `ViewMatrix` from the actor's transform when `bUpdateMatrices` (+0x174 bit 0) is set, and that is the class's default** (`Default__SceneCapture2DComponent` in Engine.pcc: `bUpdateMatrices True`; the actor's template doesn't change it). So `SetView` then `ForceUpdateComponents` wrote our view and threw it away in the same call, and the new probe carried the actor's. The actor stands where it was spawned, at the controller's place with rotation zero: **inside Shepard, looking along +X. That's the close view with the hand**, the same for every `chase`. It updated all along.
- `SetCaptureParameters` (`0x505E10`) sets the bit for its one call to compute `ProjMatrix` (`0x501B70`) and puts it back, so the projection doesn't depend on the bit. Probe creation also calls `0x501B70`, gated by the bit: with it off the matrix stays what `SetCaptureParameters` made.
- **Nothing of ours touches it.** Halcyon's view hooks run in `ULocalPlayer::CalcSceneView` (`0x4C5300`), which no capture passes (`0x4EE4B0` builds its view itself); RenderLevers' capture scope changes index, passes, recorder and target, no matrix. No `[MEVR]` change, no host change.

**The fix (mod):** `bUpdateMatrices = false` on the component, once, before anything else, in `game/CockpitProbe` and `game/ScopeCapture`. Then `SetView` and a transform-only update, which is what `ScopeCapture` already sends. `mevr cockpit info` prints where `ViewMatrix` looks from against what was last asked.

### Why FrameRate didn't throttle: each SetView makes a new probe

`0x501E10` (UpdateRequired) compares the family's world time with the probe's last render time (+0x54) against `1 / FrameRate` (+0x70, from the constructor `0x4ECB00`), and **a new probe's last render time is 0**, so it renders at its next chance. A probe rebuilt each tick renders each frame whatever the rate. So research/39 section 3's clock reasoning doesn't decide anything for a capture that moves. (The test's other way out, the global `0x1782CD8`, is set only by `CalcSceneView`'s dynamic resolution change: not this.)

So **for a moving capture, the rate is how often the mod re-points it.** The probe's `Tick` now calls `SetView` at `rate` a second; `mevr cockpit aim 0` stops that, and the engine's own throttle then holds for a still view. A new probe also starts with a new view state (`0x4EE4B0` allocates one for a probe without one), so a capture moved every render has no occlusion history and no temporal state: a real cost the cockpit should measure. If it matters, the way out is writing the live probe's matrices instead of rebuilding it (the probe's place on the component isn't read yet, and the write would race the render thread: not built). The scope capture re-points each tick at 150, which is one render a frame, as research/39 wanted, on that same fresh state.

### The darker picture: not established; two candidates and the reading that splits them

- **The eye's post runs over the capture's post.** The chain tonemaps the capture; the result then enters the eye's scene as emissive and goes through the eye's tonemap again. Applying a filmic curve to an already tonemapped value darkens it. `mevr cockpit post 0` takes the capture's chain away.
- **Gamma on the 8-bit target.** `Default__Texture` has `SRGB True`, so an `rgba8` target's values are decoded on sampling. If this build's final pass writes linear values when it binds our target (the backbuffer it's written for is FP16 scRGB), they're decoded once too often. `screen ... float` avoids the question, `screen ... rgba8 linear` sets `bForceLinearGamma`.

A format or material change isn't needed before those readings; `float` is the default and the run used `rgba8`.

### The black mesh: narrowed, not solved

Measured in the snapshot of the second run (`2026-10-31_19-41-30-291`, `-left-world.png`): the mesh's inside is **a constant (32.5, 30.1, 26.0) of 255, spread 4.2, no structure at all** (stretched five times it's even grain), against a texture with white panels and red marks whose mean is (55.5, 54.9, 53.7). The same mesh under the render target's instance (`2026-10-31_19-42-01-381`) shows the picture across its UVs. So:

| | Finding | Evidence |
|---|---|---|
| Ruled out | The mesh: geometry, UVs, vertex factory | The render target's picture maps across it |
| Ruled out | The parent's shaders on this mesh | Same parent, same mesh, drawing |
| Ruled out | The probe losing the material | `status`'s `material None` is only **our override's** slot; `SetStaticMesh` leaves `Materials` empty and the element's own instance draws. `status` now says so, `info` reads it |
| Unlikely (UE3's behaviour, not read here) | A missing parent or parameter | Either should show the parent's `EngineResources.DefaultTexture`, or the default material, not a constant [verify: `info`] |
| Ruled out | Mip count | `0x2674D0`: `RequestedMips` is the mip count limited by `GMaxTextureMipCount` (14 here, `0x997857`) for a NeverStream texture; all 13 |
| Open | **The texture's GPU copy is empty, or the instance's value isn't the texture** | The constant looks like black plus the scene's post (bloom, grain); not proved. A flat lowest mip would look the same |

The stand-in of the first run is a different case until shown otherwise: a lit material with no light. `info` prints the light environment.

**One real defect found in the package, fixed:** every mip's bulk offset was relative to its export (293 for a texture whose data starts at 38,111,834); the game's are absolute in the uncompressed package. LEC serializes with the export's place in the file, 0 before the first save. `Build.cs` now saves, writes the textures' binaries again and saves (`Check.cs` fails otherwise; all mips inside their exports on the real models). **This fixes the cockpit only if the bulk data is read lazily**, which `DisallowLazyLoading` should exclude [verify: `info` prints `attached to the linker` or `read inline`], and a lazy read at 293 would have given noise rather than a constant. So it's a correction, not the expected cure. Also added, for parity with the game's instances: `m_Guid`, `ParentLightingGuid` (the parent's). Compared and the same as the game's: the instance has no binary (the game's non-static instances too; only static permutations have one), the import's shape, texture properties and mip sizes.

### New verbs

- `mevr cockpit info`: each element's material chain with its parameters, each texture's `Resource`, `SRGB`, `NeverStream`, `LODGroup`, size, format, `RequestedMips`/`ResidentMips`, the first and last mip's bulk flags, size, offset and whether it's attached; the engine's own `GetTextureParameterValue`; the light environment; the capture's `bUpdateMatrices`, `FrameRate`, where `ViewMatrix` looks from.
- `mevr cockpit texture <Package.Object>`: a new instance of ours over `EngineMaterials.EmissiveTexturedMaterial` (`Texture`, unless `material`/`param` were set) with that texture, in place of the render target. No name: back to the render target.
- `mevr cockpit aim 0|1`, `post 0|1`, `screen ... linear`. `chase` re-points at once, also under `aim 0`.
- `mevr vehicle`: map (the top of WorldInfo's path), game mode, the pawn and whether it's a `BioVehicleBase`, `FindClosestDrivableVehicle`'s answer, the nearest live vehicle and its distance. `mevr vehicle enter`: `BioPlayerController.BioEnterVehicle` on the first answer, else the nearest. `FindClosestDrivableVehicle` is a native of `SFXSaveGame`, not a static one: it's asked only of a live SFXSaveGame, never a default object, and only with a BioPawn [verify whether one exists then, and whether entering works outside the game's own trigger].

### Next run

The mod hot-reloads (`MEVR-LE1-mod`; no host change, no HostServices bump). **The package must be installed again with the game closed** (research/47 "What to do": the file, then the TOC, its size changed). The view and rate lines don't need it. All `[verify]`:

```powershell
tools/console.ps1 "mevr cockpit mesh EngineBuildings.BuildingQuadMesh" "mevr cockpit screen 512 288 float" "mevr cockpit info"
tools/snapshot.ps1 -Tag view-1
tools/console.ps1 "mevr cockpit chase 12 10" "mevr cockpit info"
tools/snapshot.ps1 -Tag view-2
tools/console.ps1 "mevr cockpit aim 0" "mevr cockpit rate 10"     # then the log's levers line, about 10 s on
tools/console.ps1 "mevr cockpit aim 1" "mevr cockpit rate 30"     # the levers line again
tools/console.ps1 "mevr cockpit post 0"
tools/snapshot.ps1 -Tag post-off
tools/console.ps1 "mevr cockpit post 1" "mevr cockpit screen 512 288 rgba8"
tools/snapshot.ps1 -Tag rgba8
tools/console.ps1 "mevr cockpit screen 512 288 rgba8 linear"
tools/snapshot.ps1 -Tag rgba8-linear
tools/console.ps1 "mevr cockpit clear"
tools/console.ps1 "mevr cockpit load BIOG_MEVR_Cockpit.Front_Cockpit" "mevr cockpit mesh" "mevr cockpit info"
tools/snapshot.ps1 -Tag ours-own
tools/console.ps1 "mevr cockpit texture BIOG_MEVR_Cockpit.Front_Cockpit_Diff" "mevr cockpit info"
tools/snapshot.ps1 -Tag ours-texture
tools/console.ps1 "mevr cockpit texture EngineResources.DefaultTexture"
tools/snapshot.ps1 -Tag game-texture
tools/console.ps1 "mevr cockpit clear" "mevr vehicle"
```

| Reading | Means |
|---|---|
| `info`: `ViewMatrix looks from` = `last asked`; view-1 shows the pawn from behind and above; view-2 differs | A is fixed |
| They agree but the picture stays the old view | The update isn't making a new probe: the transform-only route isn't what's assumed; `FlushActor(.., false)` in `PointCapture` |
| They differ, `bUpdateMatrices 0` | Something else writes `ViewMatrix`: look for it there, the bit is innocent |
| Levers `captures`: about 0.11 a frame at 90 Hz under `aim 0` + `rate 10`; about 0.33 under `aim 1` + `rate 30` | Rate understood. Still 1.00 under `aim 0`: the probe is rebuilt by something else, or the world-time test isn't what was read |
| `post-off` as bright as the eye, others darker | The double post: use no chain for a world screen |
| `float` as bright as the eye, `rgba8` darker, `rgba8 linear` as bright | Target gamma: float, or `bForceLinearGamma` |
| `ours-texture` shows the panels | The texture is good, **our package's instance is the fault**: `info`'s chain and parameter lines on `ours-own` against this |
| `ours-texture` constant, `game-texture` shows a pattern | **The texture is bad**: `info`'s `Resource`, `ResidentMips`, flags; lazy or inline |
| `game-texture` constant too | The test is wrong: an instance doesn't reach the mesh that way; `screen`'s path works, compare |
| `info`: `Resource NULL` | The resource never made: `UpdateResource` didn't run for this load path |
| `info`: `attached to the linker (lazy)` and own texture now right | The offsets were the cause after all |

## Third run (2026-10-31 20:27, fake headset, Eden Prime then `BIOA_UNC20`, `sharePass = 5`, host deployed from a57f03d)

- **The capture's view is fixed:** with `bUpdateMatrices` off, the panel shows the chase view and `chase 12 10` changes it.
- **The darker picture is the post chain run twice:** `post 0` is about as bright as the eye image. `rgba8 linear` is much darker than `rgba8`. A world screen should run without the capture's post chain.
- **Our mesh and its instance are fine; our texture was the fault:** `mevr cockpit texture EngineResources.DefaultTexture` shows that texture across the model, while `texture BIOG_MEVR_Cockpit.Front_Cockpit_Diff` stays one flat colour. `info` read `Resource set`, `LODGroup 27`, `RequestedMips 1`, `ResidentMips 1`, 13 mips, inline. The package set no usable `LODGroup`; the cause and the fix are in "Fourth run".
- **`open BIOA_UNC20` from the console loads a Mako level**, no modding tool. `mevr vehicle` found `BioVehicleWheeled` 15.5 m away. The level's squad selection screen comes up first (game mode 7). `mevr vehicle enter` made the Mako the pawn while that screen was up.
- **Accepting the squad made Shepard the pawn again in game mode 6 (cinematic) on a black screen**, and it stayed so for 40 s or more: the level's arrival sequence is waiting on something a direct load doesn't provide. Space then Escape left the game with no pawn. So a direct load doesn't reach drivable Mako gameplay yet, and none of the Mako features have run. Next to try: enter the vehicle while the squad screen is up and close the screen another way (its handler's `ConfirmParty`/`FinalizeParty`, the game mode manager), read how the level's sequence continues, or a save with the Mako.
- **Posted mouse messages drive the game's menus:** `WM_MOUSEMOVE` highlights a button, and `WM_LBUTTONDOWN`/`UP` clicked Accept Squad and the confirmation. One click 120 ms after the press did nothing on the second dialog; a move, 900 ms, then a 300 ms press worked. So the game pointer's route works, but it may need the hover to settle first [verify in the headset]. The cursor was also set with `SetCursorPos` for the first of them.

## Game-ready models (2026-11-02; packaged and checked, not yet loaded by the game)

`tools/cockpit/game_ready.py` (headless Blender 5.2, MIT) sits between the detached/repaired models and the packager. The procedure, its traps and the numbers to judge a run by are the `tripo-game-ready` skill (`.claude/skills`). It reads the six files the packager reads and writes `concept/assets/game-ready/<file>.glb`, `manifest.json` and `previews/` (source, result, difference). It takes 21 s.

- **What it does:** each surface (the textured front, the repair's inward backing) is decimated by edge collapse on its own, with the vertices where they meet locked. The front keeps the fewest triangles within 0.0005 of the model's diagonal (99th percentile both ways, maximum within 4 times that); the backing is held to 0.002. Normals are then smooth by angle (30 degrees). The models are normalised to about one unit each, so a tolerance can't be in metres.
- **Result:** 107,282 → 64,742 triangles and 203,146 → 95,233 vertices after the packager's weld. **`Front_Cockpit` is one mesh again** (25,377 welded, 26,322 after LEC's tangents), so there is no `Front_Cockpit_Backing`. Per model: joystick base 10,632 triangles, grip 8,606, throttle base 6,398, handle 5,434, chair 17,192, front cockpit 16,480. The package is 24.4 MB against 25.6 (the textures are most of it). Its `check`: all passed, including the winding (0.0% of faces agree with their vertex normals, as before).
- **Kept, measured:**
  - the base colour's bytes;
  - boundary and non-manifold edges (0, and the chair's 1);
  - every locked vertex;
  - texel density (1,602 to 1,968 per unit, within 3%);
  - the controls' clearance (0 intersecting face pairs at every motion sample, before and after);
  - eight culled, flat-textured orthographic renders per model: silhouette IoU 0.994 to 0.9999, 1 to 4% of pixels off by more than 16/255, all along edges.
- **The packager:** it prefers `game-ready/<file>.glb` when the manifest's `sourceSha256` is the file its own rule picks, and stops with a message otherwise (tested). `--raw` packages the repaired and detached models. A build to `--out build/cockpit-gameready/...` left the installed package alone.
- **Measured, not assumed:**
  - Tripo's normals cost little. Kept (`--keep-normals`), the chair welds to 29,941 against 27,028 and the front cockpit to 25,360 against 25,377: the vertex savings come from the triangles.
  - An empty vertex group on Blender's Decimate modifier locks nothing (no weights layer): the joystick, whose backing is all junction rims, lost 120 junction vertices until that pass was skipped.
- **Not established:** how it looks in the game. Install the game-ready package (research/47 "What to do", the game closed), load it with the console lines of "Next run", and compare a snapshot with `previews/`.

## Fourth run (2026-11-02 14:40, fake headset, Eden Prime): the texture's LOD group

- **`TEXTUREGROUP_APL_1024` changed nothing:** `info` still read `LODGroup 27`, `RequestedMips 1`. Neither did
  `TEXTUREGROUP_Environment_1024`, although Eden Prime's own `BIOA_PRO10_T.pro10_bunkertrims01_diff` in that group read
  `LODGroup 18`, `RequestedMips 12` of 12 in the same game.
- **The cause is how the name was stored.** LE1's `TextureGroup` enum has 27 values (0 to 26, LEC's LE1 database), so 27 is one
  past the last: the game's lookup of our value failed. The game's packages store `Environment_1024` as the name
  `TEXTUREGROUP_Environment` with instance number 1025 (and `_512` as 513, `VFX_1024` as `TEXTUREGROUP_VFX` 1025). Ours stored
  the whole string as one name with number 0, which matches no enum entry. The earlier "unset" build had the same fault: it named
  `TEXTUREGROUP_Vehicle`, which isn't in LE1's enum at all.
- **Fixed in `tools/cockpit/package/Build.cs`** (`Build.TextureGroup`, a `NameReference("TEXTUREGROUP_Environment", 1025)`). In the
  game: `LODGroup 18`, `RequestedMips 13`, `ResidentMips 13` for every 4096 texture. The front cockpit shows its panel lines and
  highlights instead of one dark colour. The hull's shell, loaded the same way, shows its texture and window frames.
- **Any enum value with a numeric suffix** written through LEC has to be split the same way, or the game reads the enum's end.

## The live view on the console's screen (2026-11-02 15:40, fake headset, Eden Prime)

`Hull_Console` (research/48, commit 9a9161f) has the centre screen as material slot 1 (`Hull_Console_Screen_MIC` over
`EmissiveTexturedMaterial`, UVs 0 to 1 with v = 0 at the top). The probe's new `mevr cockpit element 1` puts its instance on
that slot only (`element all`: every slot, as before), over the slot's own instance as parent. The lines:
`load BIOG_MEVR_Cockpit.Hull_Console`, `mesh`, `dist 1.2`, `rot 0 -90 0` (the screen faces the package's +Y: yaw -90 turns it
to the head), `element 1`, `post 0`, `screen 768 270 float` (the screen's 2.85:1), `chase 6 3`. **Result:** the chase view of
Shepard and the squad fills the centre screen, upright and not mirrored, with no flip in the material; the wing screens and the
console's atlas keep their own materials.

## The cockpit in one frame (2026-11-02 15:45, fake headset, Eden Prime)

The probe's `part <Package.Object> <right> <forward> <up> <yaw> <metres per package unit>` spawns an actor of its own per mesh,
placed in one frame: the head's place and flat heading at the first `part` (or `anchor`), offsets in metres, the yaw on top of
the frame's, the draw scale from metres per package unit times the view's units per metre. `parts` lists them, `parts clear`
removes them. `tools/cockpit/place.py` writes the lines from the hull's manifest (research/48): the cockpit's eye point
(hull 0, -0.2333, 0.4033) on the head, the four hull meshes at yaw 90 (the package's forward is -Y), the chair and the controls
at their manifest places with yaw 90 - `turnDeg`, the throttle's handle at its neutral slide, all at the proposed 3 m per hull
unit (not yet confirmed by the user).

**Result:** from the head, the cockpit stands as modelled: the windshield and the two angled side windows with Eden Prime
through them, the ceiling's ribs, the console with its orange-framed screens, the side panes. Moved 0.6 m ahead, the chair shows
its backrest's back (it faces forward). The controls alone, brought ahead and up into view: both switch panels face the pilot,
the throttle's lever stands at the base's far end at neutral, the joystick's thumb buttons face the pilot, as in the hull's
renders: `--yaw-sign 1` (yaw 90 - `turnDeg`) is right. The cockpit is fixed where it was anchored; following the Mako is the
next step.

**A bug on the way:** `parts clear` first asked `Still()`, which checks the single mesh's world (empty after `clear`), so it
removed nothing and each rerun stacked another cockpit (37 actors by the controls check). Parts now check the frame's world, and
`parts sweep` destroys every spawnable actor showing a `BIOG_MEVR_Cockpit` mesh: what a hot reload or a lost list leaves behind.

**`Actor.Destroy` returns false for every actor the probe spawns** (78 of 78 in the sweeps, the capture actor too), with
`bStatic 0`, `bNoDelete 0`, `Role 3` read on each: so not the engine's first two refusals (`UWorld::DestroyActor`, `0x3FACD0`:
static or no-delete, then not `ROLE_Authority`). It is the third branch, BioWare's: through a virtual on the actor
(vtable +0x6D0) to an object whose member is of an Engine class, it sets a flag and calls a method on another object instead,
then returns 0, which looks like a deferred destroy **[verify]**. The probe's fallback hides the actor, so nothing stays in view.
But a hidden actor still shows our mesh, so a later sweep counts it again (41 after 37).

## The hull of d4d3373 with its live screen (2026-11-02 23:25, fake headset, Eden Prime)

The hull built from d4d3373 (research/48 sections 17 to 21: a bigger console, each screen a panel cut to its bezel's opening, the
rear section's lamps and panels, keypads and vents) was installed as in "What to do" and placed with `place.py`'s lines, in the
same frame as the earlier runs (-122375, 235857, -263, heading -81). The probe's new `screenpart <Package.Object>` puts the screen on
a placed part instead of a mesh of its own: the part stays where the frame put it, the tick never moves it, and `parts clear` /
`sweep` take the screen and its capture with them. The lines after the parts: `screenpart BIOG_MEVR_Cockpit.Hull_Console`,
`element 1`, `post 0`, `screen 768 343 float` (the centre panel's 0.3605 x 0.161 hull units, 2.24:1), `chase 6 3`.

- **The live view fills the centre panel's opening**, upright, unmirrored, at the panel's own aspect.
- **The chase capture sees our cockpit from outside:** 6 m behind and 3 m above Shepard, the cockpit floating at the head is a dark
  box in the middle of the picture, the chair's legs below it. In the Mako the cockpit sits inside the vehicle's own hull, which should
  hide it unless the cockpit is wider than the Mako at 3 m per hull unit **[verify]**. If not, hide the parts from the capture
  (the components' `bOwnerNoSee` with the capture actor as their owner, if a scene capture's view takes its owner as the view actor
  **[verify]**).
- **Sides** (the cockpit turned 70 degrees each way around the head): the angled side windows, the side panels with the idle
  images' orange frames, the A-pillars' feet on the console's blocks with no sleeve, the controls in front of the side panels.
- **Over the shoulders** (150 degrees each way): the rear section's wall panels, vents, the amber lamp strips and the ceiling lamps
  as in Asset Modeling's `rear-baked` render. The chair's backrest fills half the view, a flat, untextured-looking dark surface
  (mean RGB 28, 31, 28 with post processing) with small light shards where something behind shows through: whether the eye sits too
  close to or inside the backrest is the asset side's to check.
- **Tone** (the same three boxes as before, post on / off / on): 32.3 / 29.2 / 34.6 / 38.5, then 56.2 / 45.4 / 68.5 / 68.0, then
  on again as the first. The boxes now fall on the new ceiling and console, so they don't compare with the earlier builds' numbers.
  The panels' idle images read darker than in the renders: the centre panel's opening mean RGB 25, 17, 12 (43, 28, 16 without
  post processing), the side panels 35-38, 29-30, 26-27.
- A rear view lasted to the end of Eden Prime's bomb timer (`4 Charges Remaining`, under 2 minutes after the load): views from this
  save need taking soon after `god`.

## The hull of 5969f57: the seat, the grips, the Mako's size (2026-11-02 23:46, fake headset, Eden Prime then `BIOA_UNC20`)

Build 5969f57 (research/48 section 22): the hull replaces `Front_Cockpit`, the eye 0.10 m further from the headrest (`eye.hull`
(0, -0.2, 0.4033), 0.213 m from it), the HOTAS on the armrests, the idle images 2.5 times brighter, and the manifest's `pilot` points.

- **Over the shoulders the backrest shows its fabric and piping** and nothing shows through it: the dark surface and the light
  shards of the run before were the near plane (`LocalPlayer.NearClipPlane`, 10 units: 0.10 m at the view's 100 units a metre) cutting
  into the backrest from inside, with the eye 7-11 cm from it.
- **The idle centre panel** reads mean RGB 37, 26, 19 with post processing (the concept's aim 39, 26, 23), 66, 43, 29 without.
- Tone boxes (post on / off): 32.9 / 27.5 / 34.5 / 42.3 and 57.3 / 46.9 / 68.5 / 73.1.
- **The Mako's size** (`mevr vehicle size`, new: the vehicle's `Pawn.Mesh`, its `MeshComponent.GetUnscaledBounds` times the actor's
  and the component's scales, and the component's world `Bounds`), `BioVehicleWheeled_0` in `BIOA_UNC20` (actor `DrawScale` 3, yaw
  210.3 degrees): the mesh's own box scaled is 15.7 x 8.3 x 4.0 m, the world box 8.11 x 6.59 x 1.66 m. Unrotated (yaw 30 off the
  world axes), the world box is a body 7.5 m long and 3.3 m wide, 1.66 m tall. The two disagree by more than the scale, so neither is
  the body's size for sure **[verify]**: the mesh's box looks loose (as the guns' are), and a skeletal component's world box may come from
  its physics bodies. Against the cockpit's 2.96 x 3.4 x 2.0 m at 3 m per hull unit, the width fits under either; the height is the
  open question. A picture of the cockpit inside the Mako settles it, once the cockpit follows the vehicle.

**The controls moved to the cockpit's grips** (`core/vr/MakoControls` defaults, `game/HandCalibration`'s template and new keys): `leverOffset`
-0.362 -0.446 0.286, `stickOffset` 0.375 -0.415 0.314 (the manifest's `pilot.fromEyeMetres`), `throttleTravelM` 0.055 (the
model's slot). A file holding all three old defaults exactly gets the new ones once; any other value is kept.

**The Mako's size doesn't matter** (the user, relayed 2026-11-02: the Mako is entered and left through full blackout
transitions, so the cockpit needn't fit inside its body; it just has to feel real). The two boxes above stay as a note.

**Shepard in the seat** (the user's decision, relayed 2026-11-02): in the Mako, Shepard's body sits in our chair with the VR head at the
eye, the pelvis at `hPoint`, the heels on the floor, and the hands on the stick and throttle models while held. Not started: it needs
drivable Mako gameplay to see what the game does with the driver's pawn there (hidden, based on the vehicle **[verify]**), then the body
shown in the cockpit's frame, a seated pose (the legs to the knee and heel points, the spine to the eye) and arm IK to the grips.

## The aiming screen and the cockpit out of the captures (2026-11-03 00:00, fake headset, Eden Prime, hull f0e8fa5)

f0e8fa5 (research/48 section 23) gives `Hull_Console` a fourth slot: 3, `Hull_Console_Aim_MIC` on the right screen (2.51:1),
its idle image a gun sight. The user wants the right screen to show the aiming picture while the centre shows the Mako in third
person.

- **`aimscreen [W H] [fov DEG]`** (probe): a second `SceneCapture2DActor` with its own FloatRGB target (768x306, 30 degrees, no post
  chain, the capture rate) on slot 3 of the screen part, through our instance over the slot's own. On foot its stand-in aim is the
  head's view; the Mako's aim point replaces it. **It works:** the right screen shows the live picture along the aim.
- **The cockpit out of the captures (`capturehide`):** a primitive's `bOwnerNoSee` / `bOnlyOwnerSee` compare the view's actor with
  the component's owner chain.
  - 1, the parts owned by the chase capture with `bOwnerNoSee`: the cockpit stayed in the chase picture.
  - 2 and 3, owned by the pawn or the controller with `bOnlyOwnerSee`: the cockpit vanished from the eyes too.
  - **Then the cause: `Actor.SetOwner` through ProcessEvent changed nothing.** Every part's `Owner` read the player controller (Spawn's
    owner) after every call, so 1 to 3 all ran with the controller as owner. The probe now writes the `Owner` field itself when the
    call leaves it (journalled `CockpitProbe.partOwner`, logged once).
  - **With the field written, 2 works:** owned by the pawn, `bOnlyOwnerSee`, the cockpit is in the eyes and in neither capture; the
    centre screen shows Shepard and the squad, the right screen the aim picture. So the eyes' view actor is the pawn (as Halcyon's head
    hide implies), and a capture's is neither the pawn nor the controller (none, or its own actor **[verify]**). 2 is the default.
    In the Mako the pawn is the vehicle: the owner has to follow the pawn.
- **A reticle in the aim picture** needs a mesh the captures see and the eyes don't: owned by the pawn with `bOwnerNoSee`. It would
  show in both captures, so it belongs at the aim point itself (traced, scaled with distance to keep its size in the aim picture), where
  it also marks the target in the chase picture. It needs a reticle mesh in the package.

## The aim reticle (2026-11-03 00:40, fake headset, Eden Prime, hull b486139)

b486139 adds `Aim_Reticle` (the right screen's idle sight as flat two-sided strips, 97 package units across, facing +X, research/48)
and a detail pass (three baked pages on `Hull_Shell`, labels, stripes, handles, cables).

- **`aimreticle [DEG] [range M] [pitch DEG] [extent N] [eyes]`** (probe): spawns the mesh, writes its `Owner` (the pawn), sets
  `bOwnerNoSee`, and each pointing of the aim capture places it on the aim line, square to it, `DEG` wide as the capture sees it
  (draw scale 2 d tan(DEG/2) / 97). `aimscreen ... slot N` puts the aim picture on another slot (the centre's, 1, to look at it).
- **It works:** on the centre screen the aim picture shows the reticle centred and orange, the eyes don't see it (`eyes` shows it to them).
- **`Actor.Trace` through ProcessEvent hit nothing:** level from the eye past a crate and buildings, and 25 degrees down at the floor, with
  and without a box extent: `ReturnValue` None, `HitLocation` (0, 0, 0). So a reticle meant for the aim point sat behind the crate (8 m)
  or the buildings (50 m, 200 m) and was hidden; at 5 m, in front of the crate, it showed. In Ghidra `AActor::execTrace` (`0x140506E50`)
  reads HitLocation, HitNormal, TraceEnd, TraceStart, bTraceActors, Extent, HitInfo, ExtraTraceFlags and one more int, then
  `0x140500CB0` (flags, a trace-actor swap) and the world's line check `0x140408E30`; why nothing comes back is open **[verify]**.
  `game/Sights` (shot convergence), `game/Mako` (the turret's aim point) and `game/Throwables` use the same call; none has been seen
  hitting in the game. On a miss the reticle sits at most 50 m out.

## Lit and glowing instances, and what glass may import (2026-11-03 00:45, fake headset, Eden Prime, hull 59ae138)

59ae138 (research/48 section 25): `Hull_Shell` has six elements (three pages, then their lamps' faces), and every element has an
optional `<instance>_Lit_MIC` over `EngineDebugMaterials.TexturePaint_2Tex_Color` (Phong; the baked image as emissive, a dark diffuse,
a grey specular) and the lamps and idle side screens a `_Glow_MIC` over `EngineBuildings.ProcBuildingSimpleMaterial` (the image x 3).

- **`loaded <Package.Object> ...`** (probe): whether objects are in memory, without loading. `BIOG_APL_MASTER_MATERIAL.APL__MASTER__Mat`,
  `BIOG_Humanoid_MASTER_MTR_R.Human.HMF_VSR_MASTER_MAT` and `BIOA_JUG00_T.JUG00_GLASS01` are all loaded at the title (`entrymenu`, as soon
  as a player exists), in `BIOA_PRO00` and in the Mako level `BIOA_UNC20` (loaded with `open` from Eden Prime).
- **`variant lit|glow|base`** (probe): each placed part's elements to the package's variant by name (our own screen instances left alone).
  `lit` swapped 16 elements, `glow` 5 (on top of lit). Same pose, post on:
  - **Lit renders** (no default checkerboard) **and looks like the unlit one**: the region means change by under 0.2, 2,825 pixels differ by
    more than 20 grey levels. No highlights: whatever the parts' light environment gathers inside the cabin barely shows under the dark diffuse.
  - **Glow brightens the lamps** (their brightest 1%: RGB 155, 122, 110 -> 227, 210, 213) with no visible bloom halo.
- **This build is darker than b486139 at the same views:** ahead the whole picture's mean grey 56.0 -> 35.1 (the top third, the ceiling,
  37.2 -> 24.2); over the left shoulder 50.3 -> 28.3. The idle centre screen's opening: RGB 14.8, 10.2, 7.8 (5969f57: 37, 26, 19).
  **Not the asset** (Asset Modeling: the packaged textures match b486139's to 0.1 of a level) **and not reproduced:** the next session
  (66d3a38, below) reads the old values at the same pose. Something in that session's state darkened the whole picture; what, is open.

## Window glass (2026-11-03 00:59, fake headset, Eden Prime, hull 66d3a38)

66d3a38 (research/48 section 26) adds `Hull_Glass` (six panes, placed like the hull pieces; `tools/cockpit/place.py` lists it now),
`Hull_Glass_MIC` over `HMF_VSR_MASTER_MAT` (unlit, translucent, opacity 0.22) and `Hull_Glass_Alt_MIC` over `JUG00_GLASS01` (lit,
frosted); the package now imports from `BIOC_Materials`, and it loaded. `variant alt` (probe) swaps to the alternative. Same pose:

| | whole | top third | centre screen (RGB) | through the windshield (RGB) |
|---|---|---|---|---|
| no screens | 51.9 | 37.2 | 37.4, 25.6, 19.2 | |
| screens and aim live | 55.2 | 37.2 | 101.3, 64.3, 48.7 | |
| no screens again | 51.9 | 37.2 | 37.4, 25.6, 19.1 | 97.9, 72.9, 65.1 (b486139 without glass: 101.8, 75.7, 67.1) |
| glass alt | 49.7 | 37.2 | | 84.2, 62.2, 54.5 |
| + lit (16 elements) | 49.7 | 37.2 | | |
| + glow x 4 (5) | 51.0 | 38.1 | | |

- **The glass is close to clear:** 4% darker through the windshield, no visible sheen from the seat. The alternative reads as glass but its
  hexagonal frost bends the world (13% darker).
- **Lit, retuned (mid-grey diffuse, stronger specular), still changes nothing**: the parts' light environment gathers almost no light in the
  closed cabin. Lights inside (the manifest's `lamps`) would be the fix, at their stereo cost (not tried).
- **The screens don't darken anything:** with the live screens the rest of the picture reads the same.

**83a375a** (01:04, same pose): the glass at opacity 0.32 with cubemap 1.0 reads through the windshield as RGB 97.2, 73.5, 66.7 (0.22:
97.9, 72.9, 65.1; none: 101.8, 75.7, 67.1), still close to clear from the seat. **The lamps' glow at x 10 blooms:** a soft halo round each
lamp, white at the centre (their brightest 1% 255, 255, 255 against 220, 203, 205 unlit; the lamps' area mean grey 57 -> 73).

**136f457** (01:09, same pose): the lamps glow by default (`Hull_Shell` elements 3-5 are the x 10 `_Glow_MIC`s); `variant base` swaps
those three back to the unlit `_lamp_MIC`s (the lamps' brightest 1% 255 -> 222, 205, 207), as its name rule says. **The glass with its
cubemap at 3 shows a sheen:** a pale haze over the windshield's middle, the view through it brighter, not darker (RGB 114.3, 92.3, 86.9
against 97.2, 73.5, 66.7 at cubemap 1), the crate's dark face there washed toward grey.

**f858dda** (01:13, same pose; the glass's minimum fresnel `VSR_Colour_02.r` 0.15 -> 0.03): the windshield's middle unchanged (114.2, 92.3,
86.9). **Most of that haze is the lamps' bloom, not the glass:** with the lamps unlit (`variant base`) the middle reads 102.6, 80.4, 75.5
(grey stddev 46.8 against 57.0), so the x 10 glow's bloom spreads over the windshield below the lamp bar and the glass at cubemap 3 adds
about +5 face-on. Turned 70 degrees, the side windows show no sheen in a still frame.

**8dcf26f** (01:18, same pose; the glow x 7, the fresnel floor back at 0.15): the halo stays, a little softer than at x 10 (the lamps'
area mean grey 66.1, unlit 51.7, x 10 73), and **the spill over the windshield is the same as at x 10** (114.2, 91.4, 86.3; unlit 102.9,
80.7, 75.8). The spill doesn't scale with the factor between 7 and 10: whatever is over the bloom threshold spreads alike.

## Reaching the Mako (2026-11-03 09:27, fake headset, Eden Prime then `BIOA_UNC20`)

The third run's stall (game mode 6 on black after the squad screen) is solved: **a direct `open BIOA_UNC20` reaches drivable Mako
gameplay** once the squad screen is accepted through its own handler. `tools/mako-start.ps1` does the whole thing.

**How the level arrives** (its Kismet read with Legendary Explorer's library, a local reader; prose only): once the exterior layer is in
(`BioSeqEvt_ForegroundStreamingDone`), `BIOA_UNC20_00_DSG` asks the plot whether the rover has landed. No: black screen, then
`BioSeqAct_ShowPartySelectionGUI`. That action finishes when no panel tagged `PartySelect` is left (its `UpdateOp`). Its output turns
cinematic mode on, plays the loading movie and sets the streaming state `Cinematic` (the `BIOA_UNC20_CIN` layer). When that layer is
in, the plot is marked landed and the remote event `UNC_RoverCutscene` fires. The CIN layer's sequence plays the drop and the landing
(two matinees, 8.3 s and 5.3 s), enters the vehicle tagged `UNC_DefaultRover` (`BioSeqAct_EnterVehicle`), teleports it to
`UNC_PlayerStart`, autosaves, turns cinematic mode off and fires `RE_CINEMATIC_COMPLETE`, which streams the CIN layer out again.

**What differed from the third run:** there, `mevr vehicle enter` had made the Mako the pawn while the squad screen was up, and the screen was
then accepted by posted mouse clicks. Here nothing entered the vehicle by hand, and the screen was accepted by the handler's own
confirmation, `BioSFHandler_PartySelection.FinalizeParty(bAPressed = true)` (what the confirmation box's A calls), which hides the panel. Which of
the two broke the third run isn't known: most likely the early enter, since the drop's own `BioSeqAct_EnterVehicle` then finds the player already
driving [verify]. The `mevr vehicle party` verb (new) calls `FinalizeParty` on the live handler through ProcessEvent, with no squad picked
(Shepard alone). It needs no window input.

**The run:** at 09:27:36 `party`; at 09:27:42 game mode 6 and `BIOA_UNC20_CIN` loaded and visible (`mevr vehicle levels`, new); at 09:27:51
the mod's `mako: driving BioVehicleWheeled ... (behavior found, 1 turret from the weapons' info, 2 turret controls in the AnimTree, the
vehicle's Tick hooked, its shot start hooked)`; at 09:27:53 game mode 1 with the Mako as the pawn, the CIN layer streamed out. The eye
shows the game's chase camera behind the Mako, the HUD's vehicle silhouette and our hand reticle.

**First facts from research/46 section 7:** `mako controls: simulation is BioSVehicleSimTank`, `TurnInPlaceThrottle 0.25`; `placed from the
head` (the lever and the stick in tracking space). The turret's two controls are `TurretYawController` and `TurretPitchController`. The fake headset's
controllers aren't tracked, so the game aims the turret and nothing was grabbed; the controls' checks need a replay with tracked
controllers or the headset.

**New probe verbs** (mod, hot-reloaded; their classes, fields and functions have ReflectCheck rows): `mevr vehicle levels` (the
persistent level's streaming levels: loaded, should load, visible, should show, load pending), `mevr vehicle party` (above), `mevr vehicle
event <EventName>` (fires the level's `SeqEvent_RemoteEvent`s of that name through `SequenceEvent.CheckActivate`, originator the WorldInfo,
instigator the pawn; for stepping a stalled script, not needed here).

## Riding in the Mako (2026-11-03 09:56, fake headset, `BIOA_UNC20`, hull 8dcf26f)

The user's decision (2026-11-03): the cockpit stays with the Mako (it pitches and rolls with it: "we need to feel like we're actually
driving the mako"), and the third-person view is the display inside it.

**How (`mevr cockpit ride`, mod, hot-reloaded):** a post-handler of `Engine.Camera.UpdateCamera` (registered at the mod's start, inert until
asked). The game fills `Camera.CameraCache.POV` there, and `PlayerController.GetPlayerViewPoint` reads it back for the view through
`Camera.GetCameraViewPoint`; Halcyon builds both eyes from that view, its head look turning about the camera's own axes and its head offset
moving along them. So while the Mako is the pawn (game mode 1), in that handler:
- the game's chase POV is kept, and the centre screen's capture is pointed at it (`PointCapture`), so the screen is the game's own
  third-person camera, the same frame;
- the parts are put in the vehicle's frame: the cockpit's eye at `eyeM` (right, forward, up from the vehicle's origin; default 0, 1.0,
  0.8 m), each part at its manifest offset from the eye along the vehicle's axes, turned by the vehicle's full rotation then its own yaw
  (`engine::BasisRotation`, new, unit-tested, roll included); location and rotation written, journalled, `ForceUpdateComponents`;
- the camera goes to the eye with the vehicle's rotation, minus the **seat**: the head's offset from the game camera (the host view's
  forward, right, up) when the ride began in this vehicle, or at `ride seat`. Wherever the head was when the player got in becomes the
  cockpit's eye; leaning moves it from there. The fake headset's head sits 0.34 m behind its tracking centre: without the seat the eye
  was inside the seat's back;
- the vehicle's own primitives (`Actor.AllComponents`, 14 on the Mako) get `bOwnerNoSee`: out of the eyes (their view actor is the
  pawn, the vehicle), still in the captures. The parts are owned by the vehicle with `bOnlyOwnerSee` (capturehide 2, rerun on entry): in the
  eyes, out of the screens. All put back on `ride off`, leaving the vehicle, a level change, the mod's unload.

**The run:** `tools/mako-start.ps1`, the parts from `tools/cockpit/place.py`, the screen on `Hull_Console` element 1, `ride`. The eye sees
the cockpit and the planet through the windshield, the console's screen the chase view of the Mako (its body is not in the eyes). Driven by
`tools/make-drive-recording.py` (new: a seated head, the left stick forward, then left, then right) replayed with `-Inputs`: forward speed
850-1,550 units/s, the Mako climbed a slope and rolled hard, the cockpit stayed fixed around the eye while the horizon tilted through the
windshield, and the screen showed the same moment from outside. The camera was moved every game frame (about 200 a second under two Draws),
10 parts placed each time.

**Open:**
- The chase camera turns with the controller's rotation (the right stick), not the Mako: while the game drives, the screen can show the Mako
  from the side. `[Mako] cameraFollow` only acts while our controls drive.
- Comfort of the full roll with real head motion [verify in the headset]; the vignette's `turnRate` is the lever.
- The HUD panel quad can sit over the console's screen; the Mako's HUD pieces want their own places in the cockpit.
- The aim screen and its reticle still use the on-foot stand-in (the head's view): next, the turret's aim.
- Shepard isn't in the seat yet; the lever's and the stick's quads (MakoControls) are placed from the head at entry, which is now the
  cockpit's eye, so they should meet the models' grips [verify with tracked controllers].
- The frame cost of the parts' per-frame updates and the two captures in stereo isn't measured.

## Why Trace never hit: ProcessEvent skips natives with an opcode (2026-11-03 10:37, read in Ghidra, measured)

LE1's `UObject::ProcessEvent` (`0x12DD20`) runs a function only if its `iNative` (the u16 at UFunction +0xDC) is 0, as stock UE3's
does: a native with an opcode of its own is meant for the script VM, which reaches it through `GNatives` by that opcode. Called
through ProcessEvent it returned at once, every out value left as it was. That was `Actor.Trace` (277: "never hits"), `FastTrace`
(548), `Destroy` (279: 458 calls in one session, all false, nothing destroyed), and very likely `SetLocation`, `SetRotation`,
`SetCollision`, `SetBase`, `SetOwner`, `SetPhysics` (the old notes "Actor.SetLocation through ProcessEvent refuses the player",
"Actor.SetOwner through ProcessEvent changed nothing").

**Fix (`ue3::runtime::Call`):** for our call only, the function's `iNative` reads 0 (journalled `ue3.Call.iNative`) and is put back
after; the VM's own calls go by opcode and never read the field. Measured (`mevr vehicle trace`, new: from 3 m above the Mako straight
down, from the Mako, the controller and the WorldInfo): `Trace` returns `Terrain_0` with its hit 1.41 m under the Mako's origin (from the
WorldInfo with actors, the Mako itself); `FastTrace` false down, true up into the sky; `Destroy` true with `bDeleteMe` set.

**What changes:** every mod call of such a native now runs. `game/Sights` (shot convergence), `game/Mako` (the turret's aim point),
`game/Throwables` (the throw), `game/Comfort` (`Actor.SetRotation` on the pawn), the probe's `SetCollision`/`SetBase`/`SetOwner` all act
for the first time **[verify each in the headset]**. The task "Find why Actor.Trace via ProcessEvent never hits" is answered by this.

## Why the Mako wouldn't let Shepard out: the focus pause (2026-11-03 10:45)

`BioWorldInfo.Tick` freezes the world (`WorldInfo.bPlayersOnly`: only the players tick) while `HasFocus()` is false and the game
mode can pause, and lets it go when focus is back (`m_bPausedByFocusLoss`). On this machine the game runs behind the desktop, so
from the moment the window lost focus NPCs stopped and `BioPlayerController.BioExitVehicle`'s native refused (its first test is
`bPlayersOnly`; `mevr vehicle why` shows the driver, `bPlayersOnly` and `PlaceExitingDriver`'s answer). `MassEffectGuiManager`'s
screens put back whatever they found, so the squad screen kept it. **`[Game] pauseUnfocused = 0`** (`game/FocusPause`, new; 1 =
the game's own behaviour, the default) undoes that one pause right after the tick that made it (a `BioWorldInfo.Tick`
post-handler, only when `m_bPausedByFocusLoss` is set; any menu's or wheel's `PauseGame` is left alone). With it the exit works:
`mevr vehicle exit` (B's `TryExitVehicle`) took Shepard out (game mode 0), `mevr vehicle enter` back in.

**The pad doesn't reach the game without focus either:** the same replayed left stick that drove at 09:57 (the window had focus then)
gave throttle 0 after it lost focus, with the cockpit off and without the native fix alike; the controllers had the input. The cockpit's
own controls write the vehicle's throttle and steering themselves after `SetInputs`, so they drive unfocused (below). Where the engine
drops pad input without focus is open **[verify]**; a player with the game in front never meets it.

## The cockpit by itself, and its controls driving (2026-11-03 10:20-10:54)

- **No console:** `[Cockpit] enabled = 1` (new section) runs `ASI\cockpit\cockpit.txt` once on entering the Mako (`tools/cockpit/place.py
  --script` writes it: the parts, the screen on `Hull_Console`, `ride`); leaving it parks the cockpit (hidden, captures off), the next
  entry shows it again and seats anew; a level change forgets it; `enabled = 0` parks it. Checked: hot reload, a fresh level load
  (`mako-start`), out with B's exit, in with `BioEnterVehicle`; on foot the eyes see the real Mako, back in the cockpit.
- **The seat waits for the host's view** (the first frames after a load have none; seated from zero the eye was inside the backrest).
- **The chase screen trails the Mako:** on entry the controller's yaw is set to the Mako's, and while riding `game/MakoControls`
  turns it with the Mako whoever drives (`[Cockpit] followCamera`; the view anchor isn't told: the ride sets the view itself).
- **The cockpit's controls drove the Mako for the first time**, on synthetic tracked hands (`tools/make-drive-recording.py --controls
  --eye-height 0`: grab the lever and the stick where MakoControls placed them, push, twist 35 degrees each way): "the mod drives;
  throttle +1.00 steer +0.82 (lever held, stick held)", forward speed about 1,950 units/s, the screen behind the Mako through both
  turns; at the replay's end "stopped (a controller stopped tracking)", the game's controls back.
- `Destroy` works now (above): the sweep destroys leftovers.

## The aiming screen and head look in the cockpit (2026-11-03 10:57-11:05, fake headset, `BIOA_UNC20`; host redeployed)

- **The right screen follows the turret:** while riding, the aim capture looks from about the turret (150 units up the vehicle's
  axis) toward the turret's traced aim point (`mako::CurrentTurretAim`, new), else straight ahead of the vehicle; its reticle sits on
  the traced point (the first trace hit 105 m ahead on the terrain). The cockpit script brings both up (`aimscreen 768 306`, `aimreticle 6`).
  The reticle is in the world, so the centre screen's chase view shows it too.
- **Halcyon parks head look in game mode 1** (its Mako default: the stick aims, the crosshair matches the cannon), so in the cockpit
  the head turned only the head-locked quads. Two `[MEVR]` seams (host): `xr_session.cpp`'s game-mode-1 branch is skipped while
  `MevrBridge::CockpitLook()` (the mod's new pad flag `gamepad::kFlagCockpitLook`, fresh: set while `cockpit_probe::Riding()`), so the
  on-foot head look applies; and `vr_menu.cpp`'s `RotateMoveStickByHeadLook` (moveFollowsHead) stands down then (the Mako steers by its
  own controls, not by where you look). Measured: host view `headLook true`, yaw 35 and pitch -25 degrees for a head turned that way;
  the eye sees the seat's armrest, the joystick model and the right screen with the turret's view and its reticle.
- **Loading a save inside the Mako** (the drop's own autosave, through the launcher's Resume) put the player in the cockpit by itself,
  seated, both screens up.

## The cockpit's frame cost (2026-11-03 11:12, fake headset, `BIOA_UNC20` at the landing, the Mako parked, a still replayed head)

`tools/perf-probe.ps1 -Seconds 15`, the cockpit riding (parts, the chase and the aim captures at 30 a second) against parked
(`[Cockpit] enabled = 0`: the game's chase view in the eyes), alternated twice:

| | frame interval median (p90) | GPU median (p90) | render thread busy median |
|---|---|---|---|
| riding | 7.30 (14.13), 7.45 (15.34) ms | 5.76 (14.75) ms | 3.88 ms |
| parked | 7.99 (16.06), 8.19 (16.41) ms | 7.67 (16.14) ms | 4.83 ms |

**The cockpit costs nothing net: the frame is cheaper with it.** The cabin hides most of the planet from the eyes, which saves more than
the two captures cost. The GPU sets the pace either way, with the median well inside the 11.1 ms budget (90 Hz). The p90 is over the
budget in both, so it isn't the cockpit's. This level is light; item 10 of docs/mako-cockpit-goal.md wants a heavy Mako spot (a busier Mako
level, enemies, the cannon), still to be measured.

## Shots go where the reticle is (2026-11-03 11:15, fake headset, `BIOA_UNC20`)

A replayed right controller by the knee, pointing ahead and 8 degrees down; `mevr vehicle fire` (new: the vehicle game mode's own
`FireCannon`, RB's binding, released 0.3 s later through `StopFiringCannon`). `mako: shot 2: from the gun's start toward Terrain_0
74.3 m out; 0.66 degrees off the controller's ray`, the same for shot 3: the cannon fires from its own start at the traced aim point,
the point the aiming screen's reticle marks (`mako::CurrentTurretAim`'s target); the 0.66 degrees are the gun's offset from the hand.
Every aim trace hit (500 of 500 in 5 s; none before the native fix).

## The Mako's HUD in the cockpit (2026-11-03 11:28, fake headset, `BIOA_UNC20`; host redeployed)

The Mako's HUD has three pieces: the vehicle's status silhouette at the bottom left (in the weapon panel's band: stage x 64..210, the
bottom 139..35), the radar at the bottom right (x 1089..1214, the bottom 163..37; `DefaultRadarCrop` x 1080..1222, 172..28) and the
crosshair in the centre. `vr::hud_pieces::Pieces` grew a cockpit part (the mod's `cockpit_probe::FillHud`: anchors for the status, the
radar and the rest of the panel in tracking space, from the cockpit's eye and the vehicle's axes; the struct's size changed, so host and
mod deploy together). While it's set, in game mode 1 the host (`HalcyonOverlays`) puts the weapon panel's band on the status anchor,
the radar's band on a quad of its own, masks both out of the HUD panel and places the panel's remainder at the cockpit's anchor (high in
the windshield, 1.6 m out, 0.3 m up, 1 m wide). `place.py --script` writes the `hud` line from the manifest's left display: the status on
its left half, the radar on its right half, 2 cm off it toward the seat.

Measured: looking ahead the centre screen is clear (the crosshair's small remainder sits up in the windshield); turned down-left, the left
display shows the Mako's silhouette and the radar. The quads are compositor layers without depth: the throttle model in front of the
display is drawn under the radar.

## Shepard in the seat (2026-11-03 11:43-11:50, fake headset, `BIOA_UNC20`; mod only)

What the game does with the driver (`mevr vehicle why`, which since then moves the driver with `PlaceExitingDriver` only when asked,
`why place`): in the Mako Shepard's pawn stays the vehicle's `Driver`, hidden (`bHidden` 1), based on the vehicle, `PHYS_None`, at the
vehicle's origin, owned by it, its body mesh not hidden. Nothing of it reaches the eyes.

The seat ([Cockpit] `seated`, default 1; the script's `seat hip ... knees ... heels ... grips ...` line, written by `place.py --script`
from the manifest's `pilot.fromEyeMetres`, which reads right, up, forward; the mod takes right, forward, up):
- `game/cockpit_probe` (`SeatTick` from `RideTick`): while riding, the driver is shown (`Actor.SetHidden` false), each of its primitives
  `bOnlyOwnerSee` (owned by the vehicle, the eyes' view actor: in the eyes, out of both captures), the head's five meshes
  (`BioPawn.m_oHeadMesh`, hair, head gear, visor, face plate) hidden (`PrimitiveComponent.SetHidden`; the eye is inside the head), and lit by
  the vehicle's light environment (`PrimitiveComponent.SetLightEnvironment`): with its own, on, it drew black inside the hull. In each
  camera update, after the parts, its Location and Rotation are written (the vehicle's rotation; the hips' midpoint as animated, in the
  actor's axes, onto the H-point) and flushed. Leaving the Mako, a cutscene or the setting off undoes it as found; still the driver, it goes
  back hidden at the vehicle's origin.
- `game/arm_ik` (`DriveSeated`, when not on foot and the cockpit has a seat): a bone-pose request for the driver's mesh. The legs are the
  arm solver's two-bone IK (`ik::FindLegChain`: LE1's `LeftHip > LeftKnee > LeftAnkle`, `RightHip > RightKnee > RightAnkle`), the ankle 8 cm
  above the layout's heel, the knee bending toward the layout's knee (`ik::ArmGoal::pole`, new, unit-tested). The hands follow the
  controllers in the view's frame (`hand_targets::Current(..., inView)`); a held control moves the wrist with the controller's grip onto
  that control's grip (the lever's travelling with it). Fingers as on foot. The torso keeps the animation's; Camera and God are kept.

Measured: from in front of the seat looking back (`make-drive-recording.py --head-at 0 0.25 0.6 --look 180 -35`) Shepard sits in the chair,
thighs along the seat, knees bent, no head, the hands at the stick and the lever; from the side the right arm reaches down to the stick.
With the synthetic hands grabbing at 1 s the status goes from `left hand on its controller, right hand on its controller` to `on its
control`, every wrist 0.0 units from its target (the stick's grip at component (24, 40, 127), the hips at (3, 1, 99)). Exit
(`mevr vehicle exit`): `out of the seat (no longer driving)`, Shepard on foot as before; entering again shows the seat again. Seen from the
eye, looking down, the shoulders fill the lower view (the hips under the eye at the manifest's 0.69 m: Shepard's own hip-to-eye is
about the same).

Only while posed (2026-11-03 12:03, `BIOA_UNC52`): with no replay running the fake headset has no tracked controllers, arm IK stays off,
nothing poses the driver, and the animation stood Shepard up with the hips' fallback (the actor's origin) on the H-point: the torso filled
the eye. The driver now stays hidden until arm IK's seated pose reports in (`NoteSeatHips`), and hides again 0.5 s after it stops (log
`seat: Shepard in view, posed seated by arm IK` / `hidden again`).

## In and out by itself: a cutscene, a death, a level load (2026-11-03 11:58-12:03, fake headset)

- **The game's drop cutscene** (`tools/mako-start.ps1`, BIOA_UNC20, then BIOA_UNC52): through game modes 8, 7 and 6 (the squad screen, the
  drop's cinematic) nothing of the cockpit is built; at mode 1 with the Mako as the pawn the script runs (the parts, the screens, the HUD,
  the seat) within the same tick as the mod's `mako: driving` line. A new level first forgets the old world's parts (`a new level: forgot
  what it spawned`).
- **The pawn's death** (the console's `suicide` while riding): the Mako's pawn is gone, the cockpit parks itself (`out of the Mako: the cockpit
  is parked`), Shepard is put back (`out of the seat (no longer driving)`), the HUD goes back to the panel and the eyes get the game's own
  camera with the Mako's body visible again. The cheat leaves the controller without a pawn rather than running ME1's game-over; a death
  by enemy fire and the game-over screen's reload are [verify] (the reload of a save made in the Mako was checked before: "The cockpit by
  itself").

## Terrain and the cabin (2026-11-03 12:00, fake headset, `BIOA_UNC20`)

A 16 s drive with the cockpit's own controls (`make-drive-recording.py --controls`: full throttle, twisted one way, then the other) and a
snapshot every 2.5 s: up and down the landing's slopes, turning beside a cliff, the Mako pitched and rolled. No terrain or level geometry
in the cabin in any of them: the windshield, the side panes and the screens show the outside only through the glass. The cabin is bigger
than the Mako (research/48), so a nose into a wall or a steep bank can still bring the ground through the floor or the nose [verify in the
headset].

## The cockpit's cost at a heavy Mako spot (2026-11-03 12:04-12:06, fake headset, `BIOA_UNC52`)

`BIOA_UNC52` is the heaviest uncharted world by its packages (225 MB against `BIOA_UNC20`'s 89 MB): rocky terrain, flying objects with
trails. At the drop's landing, the Mako parked, a still replayed head with tracked hands (so the seated Shepard is drawn too),
`tools/perf-probe.ps1 -Seconds 15`, riding against parked (`[Cockpit] enabled = 0`), alternated twice:

| | frame interval median (p90) | GPU median (p90) | render thread pace |
|---|---|---|---|
| riding | 7.24 (8.67), 7.23 (9.90) ms | 4.33 (7.22), 4.79 (8.15) ms | 4.80, 5.41 ms |
| parked | 7.24 (8.64), 7.21 (7.54) ms | 4.41 (7.40), 4.34 (4.98) ms | 4.90, 3.85 ms |

Within the 11.1 ms budget (90 Hz) at the p90 too, riding or not; the render thread sets the pace. The cockpit's cost here is within
the runs' spread at the median (about 0.2 ms of GPU) and up to 3 ms at the p90 in one of the two pairs (the two captures' frames). The
user's PS VR2 renders more pixels than the fake headset's 2160-high eyes: the headset run of item 9 says the rest.
