# 39: The scope's real zoomed render (2026-10-27)

**Question.** The sniper scope's picture (research/21 sections 4 and 13) magnifies the OTHER eye's already-rendered image
4x: 22 degrees of picture show 5.5 degrees of the world, about 130 source pixels (the eye swapchain has about 24 per
degree) stretched over about 530 headset pixels, and whatever the eye image went through (FSR's RCAS, research/32) is
magnified too. The user approved a real zoomed render: the engine's own `SceneCapture2DComponent` rendering the scope's
field into a small render target, only while the picture is up, sampled by the host instead of the eye copy. It was off by
default until its frame cost was measured in game; **on by default since 2026-11-04** (section 12).

**Built** (mod `game/ScopeCapture`, host `plugin/ScopeCaptureSource` + `plugin/SightLayers`, shared `core/vr/ScopeCapture`,
HostServices **v20**, two `[MEVR]` seams in Halcyon). Nothing here was run in the game: every engine-side claim below
is **[verify]** until the measurement session (notes/next-vr-session.md step 44).

## 1. Sources and what could not be read

- **LExSDKv2's LE1 headers** (read only, for layouts and signatures; no LExSDKv2 in the code): `USceneCaptureComponent`
  (`CaptureInfo` 0x98, `ViewState` 0xA0, `UmbraCamera` 0xA8, `PostProcess` 0xB0, `FrameRate` 0xC0, `MaxUpdateDist` 0xC4,
  the bool word at 0xD0: `bEnabled`, `bEnablePostProcess`, `bEnableFog`, `bEnableParticles`,
  `bUseMainScenePostProcessSettings`, `bSkipUpdateIfTextureUsersOccluded`, `bSkipUpdateIfOwnerOccluded`,
  `bSkipRenderingDepthPrepass`), `USceneCapture2DComponent` (`ViewMatrix` 0xE0, `ProjMatrix` 0x120, `TextureTarget` 0x160,
  `FieldOfView` 0x168, `NearPlane`, `FarPlane`, `bUpdateMatrices`; `SetView`, `SetCaptureParameters`),
  `ASceneCaptureActor.SceneCapture` 0x284, `ASceneCapture2DActor`, `UTexture.Resource` 0xA0 (the FTextureResource*),
  `UTextureRenderTarget2D` (`SizeX`, `SizeY`, `Format`; static `Create(InSizeX, InSizeY, InFormat, InClearColor,
  bOnlyRenderOnce)`), LE1's `Actor.Spawn` (with LE1's extra `pActorType` and `bFindSafeLocation`), `Actor.Destroy`,
  `Actor.ForceUpdateComponents(bCollisionUpdate, bTransformOnly)`, `SceneCaptureComponent.SetEnabled`, EPixelFormat
  (`PF_A8R8G8B8` 2, `PF_FloatRGBA` 10). The code resolves all of it through `src/ue3`, with rows in `engine/ReflectCheck`.
- **Halcyon** (`src/halcyon/le1`): `pchud.cpp` `[REFLRATE]` and `d3d_capture.cpp` `[REFLCEN]` (LE1's planar floor
  reflections are SceneCaptureReflectComponents, throttled by `FrameRate`, re-rendering on "only ~60% of frames" at the
  stock 60 against 60 presents: so a capture's update is decided per frame by elapsed time), `render_hook.cpp`
  (`HookCalcSceneView` at 0x4C5300, the gameplay call site 0x4C7A4E, `[POSEEXACT]` pair arms), `d3d_capture.cpp`
  `[ME2PORT]` (the pass-0 snapshot at every scene-sized depth clear with the backbuffer dirty) and `[UBERFILL]`.
- **The perf session's classification** (main c2872e4, research/37 2026-10-27): `SceneRenderHook` (FSceneRenderer::Render,
  0x76EE20) reads the renderer's view-family render target (+0x18) and size, learns the eye target, and runs any render
  into another target with index -1, counted as `captures (scene renders into another target, no eye index): N a frame`.
- **UE3 engine behaviour** (UDK-era design, from general knowledge, no source in this repository): where captures render,
  how `FrameRate` throttles them, what `SetView` does. **Every such statement is [verify].**
- **Ghidra was not available this session**: the GhidraMCP server could not open the project (`Project 'LE1' ... is
  locked`; the lock dates from 2026-10-25 15:19 and two `ghidra-mcp` processes from that afternoon are still running).
  Nothing was decompiled. The two things Ghidra should settle next are marked **[Ghidra]**: where `RenderSceneCaptures`
  is called and what `FSceneCaptureProbe::UpdateRequired` compares, and the FTextureRenderTarget2DResource -> RHI ->
  ID3D11Texture2D offsets (replaced for now by a validated run-time search, section 4).

## 2. Creating the capture from the mod (question 1)

- **No component can be constructed through ProcessEvent** (script's `new` is an opcode, not a function), so the mod
  **spawns a `SceneCapture2DActor`** (`Actor.Spawn` on the player controller, owner the controller, at the sight's world
  place, `bNoCollisionFail`), whose `SceneCapture` is a `SceneCapture2DComponent` made from the class's template. If the
  class's defaults say `bStatic` or `bNoDelete` (Spawn refuses those) [verify which], the two bits are cleared on its
  default object for the one call and put back (journaled writes); the spawned actor copies the cleared values, so
  `Destroy` works on it. The log says which (`SceneCapture2DActor's defaults say ...`).
- **The render target:** `TextureRenderTarget2D.Create` is static: called on its class's default object (as script's
  `.static.` call does), `scopeCaptureSize` square (1024), `PF_FloatRGB` by default (9: `execCreate` refuses FloatRGBA, research/47; `scopeCaptureFormat = rgba8` for
  `PF_A8R8G8B8`), clear colour black, `bOnlyRenderOnce` false. The default objects are found once by class and the
  `RF_ClassDefaultObject` flag (a GObjects walk at creation only).
- **Never skipped by the engine's own tests:** `bSkipUpdateIfTextureUsersOccluded` = 0 (no material uses our texture,
  so it would always count as occluded), `bSkipUpdateIfOwnerOccluded` = 0 (the actor has nothing visible),
  `MaxUpdateDist` = 1e7 (the actor stays where it was spawned while the player walks; a huge distance never stops it,
  whether the engine reads 0 as "no limit" or "never") [verify the captures keep coming after walking 50 m].
- **Colour:** the capture gets **the player's own post-process chain** (`LocalPlayer.PlayerPostProcess`, else
  `Engine.DefaultPostProcess`) with `bEnablePostProcess` and `bUseMainScenePostProcessSettings` on (`scopeCapturePost =
  0`: none), fog and particles on. With a float target the post chain's output is linear like the game's FP16 backbuffer
  (research/27), read through an `R16G16B16A16_FLOAT` view and written to the scope's sRGB swapchain exactly as the eye
  copy's linear samples were. An 8-bit target holds gamma-encoded colour (UE3's copy applies the display gamma, 2.2
  [verify]): read through an sRGB view if the texture is typeless, else decoded in the shader (`decode.x = 2.2`). **The
  capture does not get Halcyon's eye blit** (research/27's colour correction, the HDR tonemap): with the colour profile
  neutral (the PS VR2's) that is the same picture; with another profile the scope's colours differ from the eye around
  it by that correction [verify with a snapshot].
- **Life:** created on the first tick an eye is behind a scope (the host's `sightView` left/right bits), disabled
  (`SetEnabled(false)`: no probe, nothing rendered) `scopeCaptureHold` (0.5 s) after the eye leaves, enabled again when
  it returns; a new size or format makes a new render target (the old one goes with the garbage). A new level: the old
  world's objects are forgotten unread (`engine::World()` changed, or any of the three no longer alive and of its class).
  `scopeCapture = 0` destroys it; the mod's shutdown (hot reload, on the game thread) disables and destroys it; at
  process exit nothing is called.

## 3. When the engine renders it, and how often (question 3)

- **Where (read in Ghidra, 2026-10-31, research/47 "Why a capture took an eye"):** the loop over a scene's captures sits at
  the top of 0x76EE20, the view family's render command, before that family's own `FSceneRenderer::Render` (0x76BC00).
  A capture calls 0x76BC00 (or its Umbra form) itself, so **the c2872e4 classification below never saw one**: RenderLevers'
  hook on 0x76EE20 isn't on a capture's path. A hook on the loop's per-capture thunk (0x742C80) now gives it index -1 and
  takes it out of the eye's recording, one-Draw finish and scissor, and counts it when its finish runs.
- **How often (corrected 2026-10-31, read in Ghidra, research/47):** a capture compares the family's **world time** with
  its probe's last render time against `1 / FrameRate` (0x501E10), but **each `SetView` plus component update builds a
  new probe, whose last render time is 0: it renders at its next chance whatever `FrameRate` says.** This capture
  re-points every tick, so `scopeCaptureRate` doesn't throttle it: one render per tick's new probe, and under two Draws the
  second Draw finds the same probe already rendered at that world time only if the time test holds [verify: the levers
  line's `captures` while scoped, 1 expected, under two Draws and under `sharePass = 5`]. To lower the rate, re-point less
  often. Each new probe also starts with a new view state, so no occlusion history.

## 4. The texture on the render thread (question 4)

- The mod publishes the render target's `UTexture.Resource` (the FTextureRenderTarget2DResource*, read through
  reflection on the game thread), its size, format, a generation (a new render target) and the pose. The host, on the
  render thread, follows the engine's RHI references to the ID3D11Texture2D. **Without Ghidra's offsets** it searches:
  each 8-byte slot of the resource's first 0x100 bytes, the pointer itself and each slot of its pointee's first 0x80
  bytes (`vr::scope_capture::FindTexture`, unit-tested on a fake object graph). Every read is SEH-guarded; a candidate is
  asked anything only if its vtable lies in d3d11.dll's image (or the debug layer's) and the object itself doesn't; then
  `QueryInterface(ID3D11Texture2D)`, and it must be exactly the render target's size, one sample, one layer, bindable as a
  shader resource. The first match in address order wins; the path found (`resource +0xA -> +0xB`) is logged and tried
  first for the next render target. The host keeps its own reference (the engine may release the texture while the
  scope still shows it: the picture never reads freed memory) and makes the view (section 2's formats). **Fails closed:**
  not found in 240 frames, an unknown format, or a failed view: the log says so once per render target and the scope
  keeps the eye copy, exactly as before. [Ghidra: replace the search with the two offsets and a byte-pattern-free
  check once read; record them here.]
- **Recognising the capture's render (for the perf session):** its view family's render target is the FRenderTarget
  inside that resource (FTextureRenderTargetResource derives from FTexture and FRenderTarget), its size the capture's
  (1024 square by default), one view. `scope_capture_source::ActiveResource()` (the resource while the picture was last
  up) and `ActiveTexture()` (the D3D texture found) expose it to the host.

## 5. Per tick, latency and sync (questions 2 and 5)

- **Pose:** each tick the sight's centre and line (tracking space, `game/sights`) go through the view anchor's
  tracking frame into the world: `SetView(location, rotation)` with the rotator of the world sight line (roll 0: the
  picture's up is the world's, like an optical scope's image; the reticle rolls with the gun as before), then
  `Actor.ForceUpdateComponents(false, true)` so the new view leaves for the render thread now, not at the actor's next
  tick (an actor the game never ticks might never send it); after `SetEnabled` or `SetCaptureParameters` (which re-attach
  the component, deferred) the same call with `bTransformOnly` false sends the re-attach too [verify: the picture follows
  the gun without a lag of a second tick, and appears the frame after enabling]. The pose the host samples with is the
  tracking pose of that same rotator's axes. **Corrected 2026-10-31 (research/47):** `SetView` alone never worked: the
  component's `bUpdateMatrices` defaults to true, and each component update then overwrites `ViewMatrix` from the actor,
  which stands where it was spawned. `ConfigureComponent` now clears it. The render thread reads a copy of the view in
  the probe, so an update making a new probe is what sends it.
- **Field:** the capture's field is `2 atan((1 + margin) tan(picture / 2) / zoom)`: at the virtual eyepiece's 22 degrees
  and 4x, 5.50 degrees of the world, 6.95 with the 25% margin (an eye off the sight line moves the reticle and what is
  sampled); 18.4 degrees for a 1.5x assault-rifle scope. The physical lens (`scopeFieldDeg = 0`) uses the lens seen from
  5 cm. `SetCaptureParameters` only when the field changes by 0.05 degrees or the near plane by 5 units.
- **Near plane:** the capture sits at the sight's centre (the eyepiece), so the gun's own model (the scope tube, the
  barrel) lies ahead of it: the near plane is `scopeCaptureNear` (20 units) past the muzzle along the line (about 80 units
  for the sniper rifle), nothing closer shows in the picture (a real scope can't focus that close either). Far plane
  `scopeCaptureFar = 0`: the engine's own (an infinite projection when not beyond the near plane [verify: a black picture
  means 0 was taken literally; set e.g. 1000000]).
- **Which pose the texture was rendered with.** The mod points the capture at tick N; the next Draw (tick N+1, before the
  mod's tick N+1) renders it; that frame's Present usually runs after the mod's tick N+1 has published again. Sampling
  with the latest pose would shift the world by one tick of gun motion (at 30 degrees a second, 0.33 degrees of world, 1.3
  degrees in a 4x picture). So: a `[MEVR]` seam where each Draw's main pass latches its pair arm (`render_hook.cpp`,
  next to `RecordMetricLatch`) notes the capture publish current at that moment (`NoteDrawArm`); the submit passes the
  presented pair's arm (`OverlayFrame::pairArm` = Halcyon's own `g_sfrPairArmThisFrame`, `xr_session.cpp`); the host
  samples with that publish's pose (`SequenceForArm`, `BySequence`; the last 32). A Draw whose publish wasn't capturing
  yet (just enabled) shows no capture picture (the eye copy's fallback blinks for a frame instead of showing an empty
  texture). Without an arm (no Stereo 2 latch): the publish before the latest, after 3 ticks enabled. Snapshot
  `framesFromArm` against `framesUsed` says which path ran.
- **What still lags:** the capture's content is one game frame older than the eyes' head pose, like the gun model in the
  eye image (research/21 section 7's latency note); the world in the picture is placed with the pose it was rendered from,
  so it stays put under head motion. **The reticle stays collimated:** it is still drawn on the lens about the current
  eye's line along the current sight (Q), exactly as before; only the image under it is the capture.
- **The picture's sampling** is the existing shader unchanged in its geometry: per lens point, the magnified direction
  from the eye, its point `focusM` ahead, projected into the source with that source's pose and FOV. For the capture the
  source is its own pose, a square frustum (`tanHalf`) and the whole texture, so the "plain centred texture" falls out
  of it (the sight line lands on the centre, unit-tested) and an eye off the line, the shadow, the rim and the fade work
  as before. Parallax: the capture sits on the sight line 30-60 cm ahead of the eye, under 1% at 40 m.

## 6. Halcyon's hooks and the capture's view (the perf session's point 5)

- **No seam needed for the view:** Halcyon's FOV override, head look, metric per-pass eye offset and frustum
  (`PostProcessPerspectiveFsv`, `ApplyMetricEyeOffset`, `ApplyMetricFrustumSEH`) all run inside its `CalcSceneView` hook
  (`ULocalPlayer::CalcSceneView`, 0x4C5300, game thread). A capture's view is built by its probe on the render thread
  (0x4EE4B0) without it (read in Ghidra, 2026-10-31). The view's problem was the engine's own `bUpdateMatrices` (section 5).
- **But this section's first conclusion, that a capture needs no seam at all, was wrong** (first run, research/47): with a
  post-process chain the capture's final pass bound the global backbuffer (0x773D30) and skipped the view family's target,
  so the picture went to the eye and the render target stayed at its clear colour. This scope capture's `scopeCapturePost =
  1` would have done the same; `plugin/RenderLevers` now binds the capture's own target there. No Halcyon change.
- **`[ME2PORT]` pass-0 snapshots:** captures render into the shared scene render targets (`Allocate` only grows them), so
  the capture's depth clear is scene-sized and takes a pass-0 snapshot while the backbuffer is dirty. That copy is the
  previous Draw's finished eye, the right image, and the eye's own clear then finds it clean. The floor reflections live
  with the same. **This held only once the capture stopped writing the backbuffer itself**: before that, the eye's clear
  copied the capture's picture as pass 0 (research/47) [verify: no stale eye with the scope up].
- **`[UBERFILL]`** keys on the uber composite shader with a per-frame dedupe; a post-processed capture draws that shader
  first in the frame. It is only active while `SetStereoUiActive` (SBS stereo), not in Stereo 2 [verify].
- **Umbra:** the capture has its own `UmbraCamera` and `ViewState` (component fields 0xA8, 0xA0), so it runs its own
  visibility and Umbra resolve on the render thread (the heavy cell's eye resolve costs 1.3-1.5 ms). A 7-degree frustum
  should walk far fewer cells and portals than a 108-degree one, but nothing found here lets a capture skip Umbra short
  of the global console switches. Measure it: RenderLevers counts the capture's render but doesn't time it (it runs
  outside the eye bookkeeping), so the off/on A/B gives its whole cost and `tools/sample-render-thread.ps1` its Umbra
  share.

## 7. Cost, expected (to be measured)

- GPU: 1024 x 1024 = 1.05 Mpixel against 8.1 Mpixel per eye at 2824x2880: about 13% of one eye's pixel work, plus the
  post chain at that size. CPU (render thread): a scene render's fixed costs (InitViews, Umbra's resolve, lights) plus
  the draws of what a 7-degree frustum holds (a fraction of an eye's 4,100 at the heavy cell, but each far object at a
  higher LOD: the zoom quarters the LOD distance). Guess: +1-3 ms of render thread at the heavy cell while scoped,
  nothing when not. **Unknown until measured.**
- Resolution: 1024 over 6.95 degrees is 147 pixels per degree of the world; the picture shows 22 degrees in about 530
  headset pixels (24 per degree), i.e. the 5.5 degrees of world need about 530 x 1.25 = 660 capture pixels. 1024 is
  1.5x oversampled; `scopeCaptureSize = 640` is the first cost lever if the GPU is short.
- Offline look: `mevr-sight-preview` now writes `scope-capture-virtual-45cm.png`, `...-off-2.5cm.png`,
  `...-30cm-1.5x.png` and each capture target: the same geometry as `scope-virtual-45cm.png`, sharp instead of blocky.

## 8. Settings (`[Sights]` in `ASI\LE1MEVR.ini`, live)

`scopeCapture` (1 since 2026-11-04; an old file's 0 moved to 1 once, marker `scopeCapturePost`), `scopeCaptureSize` (1024,
256-2048), `scopeCaptureRate` (150), `scopeCapturePost` (1; written since 2026-11-04), written to older files; file-only:
`scopeCaptureFormat` (float | rgba8), `scopeCaptureMargin` (0.25), `scopeCaptureNear` (20 units
past the muzzle), `scopeCaptureFar` (0), `scopeCaptureHold` (0.5 s).

## 9. Logs and snapshots

- Mod: `scope capture: settings: ...`; `created <actor> with <component> ... disabled until an eye is behind the scope`;
  `render target ... (WxH, format F, resource 0x...)`; `post process on, the player's chain ...`; `enabled (the eye is
  behind the 4x scope): field 6.95 degrees, near plane N units` and `disabled ...` (the first five; then the heartbeat's
  `scope capture: ...; enabled N times ...`); `off: <reason>` (a failure, with what was missing); `destroyed ...`,
  `forgot the capture (a new level)`. Snapshot `"mod"."scopeCapture"`.
- Host: `scope capture source: the render target's D3D11 texture: resource 0x... +0xA -> +0xB = 0x..., WxH format F ...`
  (or `no ... texture found ...`), `first scope picture from the capture (... pose from the presented Draw's arm)`;
  `[MEVR-SIGHT] first scope picture: ... from the scope capture`; the scope's per-look line ends `N of them from the scope
  capture`. Snapshot `"sights"."pictureSource"` (`capture`/`eye copy`) and `"sights"."capture"` (`state`, `path`,
  `framesUsed`, `framesFromArm`, `framesNotReady`).
- RenderLevers (perf session): `render levers: a scene render into target ... ran as a capture` (the first five) and the
  levers line's `captures ...: N a frame`.

## 10. [verify] list

1. Spawn works (the defaults' flags), the component is a SceneCapture2DComponent, Create returns a target.
2. The capture renders once a frame while scoped and never when not (levers line), under two Draws and under sharePass 5.
3. The texture search finds it (the log's path), the view's format, and the colours match the eye around the picture.
4. The picture is right side up, unmirrored, centred on the reticle, the world in it steady when the head moves and the
   gun is still (pose from the arm), and when the gun moves (the ForceUpdateComponents path).
5. The near plane keeps the gun out; the far plane 0 isn't taken literally; fog and post look like the eye.
6. Nothing changes in the eye images with the scope up (no stale pass 0, no change in bloom).
7. Its cost (step 44).

## 11. First run in the game (2026-11-04, the fake headset, research/21 section 16's session)

- **1. Spawn and target: yes.** `scope capture: render target TextureRenderTarget2D Transient.TextureRenderTarget2D_2
  (1024x1024, format 9: FloatRGB, ...)`, `SceneCapture2DActor's defaults say bNoDelete: cleared for the spawn and put back`,
  `enabled (the eye is behind the 3.5041964x scope): field 7.14 degrees, near plane 93 units`, `capturing ... 624 ticks`.
- **3. The texture search: no.** `scope capture source: no 1024x1024 D3D11 texture found behind the render target's resource ...
  in 240 frames`, twice (two render targets). Yet a probe from the mod found it at **`+0xA0 -> +0x8`: 1024x1024, format 10
  (R16G16B16A16_FLOAT, so FloatRGB is RGBA16F here), bind 0x28 (render target and shader resource)**, inside the host's
  0x100 -> 0x80 window, both minutes after creation and within about a second of it. Why the host's `FindTexture` misses it:
  settled in section 12 (the pointer at `+0xA0` isn't 8-aligned). The scope kept the eye copy, so **its cost and its sharpness are still unmeasured** (item 7).
- That probe crashed the game (research/21 section 16: `QueryInterface` on d3d11 objects that aren't COM). The host's own
  search asks the same question of every d3d11-vtabled object in its smaller window; before widening it, compare a candidate's
  vtable with a known ID3D11Texture2D's (a texture the host creates on the same device) and call nothing else.

## 12. Found and measured (2026-11-04, later; the fake headset, BIOA_UNC20)

- **The texture search: yes.** The pointer at `+0xA0` isn't 8-aligned, and `sc::FindTexture`'s walk skips unaligned pointers
  (the galaxy session's finding with its own capture targets, research/51 section 8, 6c81bf0). `Resolve` now calls `NoteDevice`
  (a 4x4 texture of our own on the game's device gives the class's vtable; it lives in d3d11.dll, so the module check was never
  the problem) and then `FindTargetTexture`, whose fallback walks the same window for objects with exactly that vtable and asks
  them nothing but `GetDesc` and `AddRef`. Found in 2 frames: `+0xA0 -> +0x8`, 1024x1024, format 10, bind 0x28. Every scoped frame
  after that took the capture (`framesNotReady` 0, the pose from the presented Draw's arm).
- **Setup.** Sniper rifle at 3.5x, Shepard 10 m from the parked Mako (the hull's panel lines and its painted "CAUTION" text in the
  scope), `ads-sniper-30s-h0.mevrrec` replayed in a loop with its inputs (research/21 section 16: head height 0, hand drop 0.15).
- **Cost.** `tools/measure-levers.ps1 -Section Sights -Key scopeCapture -Values 0,1 -Rounds 3 -Seconds 10` (medians / p90, ms;
  the fake headset paces at 31 fps, so busy times are the measure):

  | | off (3 rounds) | on (3 rounds) | difference |
  |---|---|---|---|
  | render thread's work | 4.25-4.33 / 4.95-5.03 | 4.78-4.79 / 5.56-5.61 | +0.5 / +0.6 |
  | draws a frame | 929-931 | 1020-1035 | +95 |
  | GPU span, whole frame | 10.99-11.06 / 11.78-11.79 | 10.68-10.71 / 11.50-11.51 | -0.3 |
  | GPU span, copies and quads | 0.60 | 0.51 | -0.09 |
  | game thread's work | 2.87-2.91 | 2.80-2.84 | none |

  The levers line agrees: the eye's scene render takes 2.8-3.0 ms alone and 3.3-3.5 ms with the capture's render inside it
  (1.0 captures a frame). No GPU cost is visible. With the capture on, the scope no longer copies the other eye's image, which
  saves 0.09 ms of copies; the rest of the -0.3 ms is within the replay's spread. This is a light scene (930 draws); a dense one
  will cost more [verify at Eden Prime's heaviest cell].
- **Sharpness.** Snapshots at the same replay phases, measured on the scope layer's own 768 px picture:
  - **Eye copy:** it keeps 90% of its detail through a 3-4x downscale and back. It carries about 175 px of detail across the disc,
    the other eye's pixels magnified 3.5x, and looks blocky.
  - **Capture:** it keeps 90% only through 1.5x. Its 1024 px span 7.14 degrees, more than the layer holds. Panel scratches and
    lines are crisp.
  - **Edges and brightness:** the 99th percentile of the gradient is 48-93 for the eye copy and 129-169 for the capture. Mean
    brightness is alike.
  - **In the eye:** the scope's disc is about 380 px across. So the capture is now limited by the display, while the eye copy was
    about 2.2x below it.
- **A speckle from the post process.** The capture's dark areas carry a black speckle that the eye images don't have. With
  `scopeCapturePost = 0` the speckle is gone and the edges are stronger (99th percentile 190-198). But the game's colour grade goes
  too: the hull is whiter and more neutral than the bluish eye image around it. So the player's post-process chain makes the
  speckle at the capture's own pixel scale. Which effect is [verify]: the speckle follows crevices and dark edges, so the chain's
  ambient occlusion is the likely one. `scopeCapturePost` applies when the actor is created, so turn `scopeCapture` off and on.
- **The verify list (section 10).** 1, 3 and 7 are done. In 4, the picture was centred on the reticle with the world steady in it
  (snapshots); the rest of 4, 2, 5 and 6 weren't checked in this run.
- **Decision (the user, 2026-11-04): on by default, the speckle kept for now** and noted as a known issue (README, `core/vr/
  ScopeCapture.hpp`, notes/next-vr-session.md step 44). An old file still holding `scopeCapture = 0` is moved to 1 once (marker
  `scopeCapturePost`, written with 1); any other value is the player's.
