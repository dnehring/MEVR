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
