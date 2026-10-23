# 32: UE3 VR performance: what can go (2026-10-23)

**Question (user, 2026-10-23).** Without playable VR performance the VR mode can't be used: try anything
within reason, radical changes included. What can Unreal Engine 3 give up per eye?

Builds on [22](22-performance-levers.md) (the measurements: at 90 fps the render thread is the limit, ~10.6 ms
of 11.1; per eye ~50% is `InitViews`: Umbra's resolve with BioWare's per-object callbacks ~21%, and ~25-30%
that 22 called the depth pre-pass of the visible set) and [30](30-metric-stereo.md) (both passes hand the
engine identical CPU view and projection matrices). Web research by a subagent, sources at the end and in
[sources.md](sources.md). **[inference]** marks our reasoning, **[verify]** what one test settles.

**Correction (2026-10-23, a Ghidra pass on the commander, "The commander, read" below):** the 25-30% is the
world's **opaque base pass** (`0x8BF8F0` → `0x8BEDB0` → `0x76C1D0`, which turns colour writes on and draws the
base-pass draw lists), not a depth pre-pass. The real depth pre-pass is inside the Umbra resolve: the commander
flushes new occluders into depth (`0x8B5FF0` → UE3's `RenderPrePass` body `0x76D820`, ~5% flat) so its queries
can test against them. `0x76D650` (11%) is the lights pass, `0x76D0A0` (9%) translucency. The per-eye picture is
then: base pass ~1.4 ms, the resolve ~1.2 ms (of which the occluder depth ~0.5 ms, the queries ~0.2 ms, per-object
bookkeeping ~0.5 ms), lights ~0.6 ms, translucency ~0.5 ms. The `depthPrepass` switch built on the wrong name
(it skipped `0x76C1D0`, i.e. the world's geometry) is removed; it never ran in game.

## What other VR injectors do about the second eye

- **Unreal Engine 4/5 single-pass stereo and Unity's single-pass instanced** cull once for both eyes with a
  frustum covering both, and the second view uses the first view's visibility. An Unreal forum thread shows the
  catch: canted displays need a conservative merged frustum or edge objects pop.
- **UEVR:** native stereo is fastest; "synchronized sequential" renders two full frames per tick (slower,
  TAA ghosts); AFR advances time between eyes (desync). No culling numbers published.
- **Alternate-eye rendering** (Luke Ross's R.E.A.L., REFramework's AFR toggle, MGS2-PCVR's default,
  Halcyon's AER): one eye per engine frame, the other reprojected: about half the engine's work. Costs: each eye
  updates at half rate; moving content and near objects ghost (rotation-only reprojection can't fix them).
  A higher refresh rate (120 Hz: 60 per eye) reduces it.
- **The Outlast VR mod** (also UE3) tried rendering UE3's view twice per frame, found draw and UI state changed
  between the eyes, and settled on AER (plus a 24-frame occlusion grace).
- **Draw replay:** NVIDIA 3D Vision Automatic duplicated each draw with a per-eye clip-space shift (second eye:
  GPU cost, almost no engine CPU). MGS2-PCVR's experimental "Two Eye" mode replays the frame's D3D11 draws for
  the other eye with a widened FOV; flickering quads remain.
- **Depth-image stereo** (vorpX's Z-buffer 3D, "YORO"): one render, the second eye synthesised from depth; keeps
  the frame rate, looks less deep, fails on very near objects and transparency. Halcyon's DIBR is this.

## Unreal Engine 3 and LE1 specifics

- UE3's own split-screen notes: the render thread's cost roughly doubles with two views, each doing its own
  visibility and draw submission (`stat initviews`, `stat splitscreen`).
- Later UE3 builds have per-renderer and screen-size switches for the depth pre-pass. **LE1 has none of their
  strings** (`DepthPrepassCullingThreshold`, `MinScreenRadiusForDepthPrepass`, `OneFrameThreadLag`,
  `MaxDrawDistanceScale`: absent from the exe), so the pre-pass can only be changed by a hook (research/22's
  render levers).
- A GPL-3.0 recompilation of the 2007 Xbox 360 Mass Effect documents its draw-cost settings: it skips the depth
  pre-pass (the material pass writes its own depth) and found real occlusion queries culled only 2-4%; no
  `[SystemSettings]` there. LE1 is a later UE3, with Umbra: our own A/B (research/22) found Umbra's occlusion
  culling removes ~45% of the draws in a combat scene, so that doesn't carry over.
- No CPU-side ini tweaks for LE are known in the community; the published ones are image quality.
- D3D11 constant buffers: each `UpdateSubresource` or `Map(DISCARD)` renames the buffer, a cost per bound stage
  and draw; D3D11.1's `*SetConstantBuffers1` with offsets allows one ring buffer instead (NVIDIA). LE1's RHI
  commits with `UpdateSubresource` (research/22): a deep RHI change, kept last.

## Umbra Occlusion Booster

- Public material (Aila's SurRender Umbra thesis) describes the scheme: hierarchical visibility with temporal
  coherence per camera, occluders rendered into depth and hardware occlusion queries against it. In LE1 the
  commander's callbacks receive visible objects and flush batches; whether LE1's visible-set pre-pass doubles as
  the occluder depth for the queries is **[verify]**: `depthPrepass = 0` shows it (if draws jump like the +81% of
  occlusion off, it does).
- Nothing public shares one camera's result with another; a split-screen setup uses a camera per view.
- "HOAX": not defined anywhere public; our A/B saw no effect.

## Driver and OS

- NVIDIA "Threaded optimization" mainly concerns OpenGL; no confirmed DX11 effect. Low Latency Mode likely
  doesn't apply under a VR compositor. **[unverified]**
- Hybrid Intel CPUs: Windows 11 reportedly schedules unfocused windows' heavy work onto E-cores; users of a
  flight sim report steadier frames with the game on P-cores (and one report of worse). In VR the game window is
  often unfocused. Our flat A/B (research/22) found no difference; `pCoresOnly` is there for the VR test.
- SteamVR: per-app "Throttling behavior: Fixed" locks to a divisor of the refresh (45 at 90 Hz, 60 at 120 Hz)
  with motion smoothing as a safety net; Advanced Frame Timing shows CPU "Late Start".

## The levers for LE1, ranked

| # | Lever | Saves (VR, per frame) | Cost / risk | State |
|---|---|---|---|---|
| 1 | **Second pass from the first pass's scene renderer:** keep pass 0's FSceneRenderer, give its views pass 1's matrices, re-run only the depth pre-pass and the passes; pass 1's own renderer skips `InitViews` | pass 1's visibility (~21% of a pass, ~1.1 ms); with its pre-pass dropped too ~2.5 ms | reverse engineering of `SceneRenderB`'s state; per-view state (LOD, translucency order, shadows) taken from pass 0, which is ~3 cm away | built as `sharePass`, untested (below) |
| 2 | ~~**No visible-set pre-pass in the second pass** (`depthPrepass = 1`)~~ | ~~1.4 ms~~ | the "pre-pass" was the base pass (the correction above): the switch would have removed the world from that eye | removed |
| 3 | **AER at 120 Hz** (Halcyon's existing mode) | ~half the render thread | ghosting on motion; our seams (HUD panel, sights) may assume Stereo 2 **[verify]** | exists |
| 4 | **D3D11 replay of pass 0 for pass 1** (`sharePass = 2`): pass 0's calls recorded at the context, re-issued for pass 1 with the view's bytes substituted | pass 1's base pass, lights and translucency above the driver: ~1-1.5 ms on top of lever 1 | what the substitution misses (the eye diff measures it); the recording's own cost (~0.3-0.5 ms, measured by the log) | built, untested ("Lever 4, built" below) |
| 5 | **P-cores only**, SteamVR fixed throttling as a net | steadier frames | low | `pCoresOnly` built |
| 6 | **No wait for Umbra's query results** (`umbraQueryWait = 0`) | 0 flat; in VR whatever the resolve spins for a query the GPU hasn't reached (the stamp is a float of 2 s precision: the wait, when it happens, is 0 to ~1 s) | an unready query counts as visible: a few more draws | built, untested; the log's `longest results read` says if it ever spins |
| 7 | **UE3's own per-primitive queries waited on** (`0x762550`, `bWait = TRUE`): the second eye waits for the first eye's query of the same frame **[verify]** | unknown in VR | `UMBRA TOGGLEOCCLUSION` (console) ignores them: a zero-code A/B; lever 1 removes the second eye's wait anyway | to measure |

### Lever 1 in detail [inference, to verify in the code]

`SceneRenderB` (0x76EE20) is, for the Umbra path: `GSceneRenderTargets.Allocate`; `InitViews` (0x768090: per-view
setup, Umbra resolve 0x8BFBB0, its finalisation, `0x77BE00`, the visible-set pre-pass 0x8BF8F0); `0x7666A0`;
`0x759240` (conditional); DPG 1 (`0x76D650`, `0x76D0A0`); `0x76CA70`; DPG 2; `0x76DD10` (finish); then the
renderer is deleted (`0x4ED700`). Both passes' views differ only in what Halcyon edits on the game thread
(ViewOrigin `+0x320`, PreViewTranslation `+0x250`, the translated matrices `+0x190`/`+0x1D0` and their inverses;
in life-size stereo mode 2 the GPU frustum in `+0x1D0`). So pass 1 = pass 0's renderer with those fields copied
in, the pre-pass re-run (it clears depth and draws from the views' matrices), and the passes after `InitViews`.
What has to be checked before building it: which renderer state the passes consume or mutate (they must be safe
to run twice), where the view's inverse matrices live, what `0x7666A0` and `0x759240` do, and that nothing keeps
pointers into pass 1's renderer.

**Built (`[Performance] sharePass = 1`, `plugin/RenderLevers`, off by default; untested in game).** What the code
found:
- `0x4ED700` is FSceneRenderer's deleting destructor (`0x764AF0`, then frees 0x110 bytes); hooked, it holds back
  one renderer's deletion while that renderer renders, so the frame's next scene render can use it.
- FSceneView is the first ~0x4E4 bytes of FViewInfo (copied by `0x763450`): containers at 0x44 (a TArray), 0x140
  (the view frustum's planes) and 0x330 (copied by `0x762C90`); plain matrices and vectors in 0x90-0x140, 0x190-0x330
  (translated matrices, PreViewTranslation 0x250, view-projection and inverses, ViewOrigin 0x320), 0x480-0x4B0 and
  0x4C0-0x4D0. Those four ranges are copied from the second render's views, so its own metric-stereo edits come
  along. FViewInfo's own members start at 0x4F4 (visibility and draw lists).
- FSceneRenderer: +0x00 scene, +0x08 the family copy (+0x18 its render target, +0x28 show flags), +0x5C the Umbra
  path, +0x60/+0x68 the views (stride 0x12F0), +0xD8 set before the passes.
- The tail it replays, as `SceneRenderB` calls it: GDynamicRHI `+0x628`; the scene frame counter `0x178779C`++;
  `+0xD8 = 1`; `0x8C1AE0(0)`; `Allocate(GSceneRenderTargets 0x167D0E0, target +0x18, target +0x20, 0)`; the pre-pass
  `0x77BE00(r, 1)` (InitViews calls it after the resolve: per light and view into scene render targets, so it's
  eye-dependent; `0x76CA70` calls it for DPG 2); the pre-pass `0x8BF8F0` (the commander's per-view flags,
  +0x34 + 8i, re-armed: the resolve sets them, the pre-pass clears them); `0x7666A0`; dirty = `0x1795D38`; `0x759240` unless `0x167D49C`; DPG 1 `0x76D650(r, 1, &dirty)`,
  `0x76D0A0(r, 1, t, &dirty, 1)`; `+0x5C = 0`; `0x76CA70(r, 2, &out, &dirty, 0)`; DPG 2; `+0x5C = 1`;
  `0x76DD10(r, t, dirty)`; GDynamicRHI `+0x620`. Then both renderers are deleted.
- Not re-run for the second eye: the Umbra resolve and its per-object callbacks, its finalisation, and
  InitViews' per-view setup before it (view state bookkeeping, done with identical CPU matrices by the first).
- Pairs only renders with the same scene, render target, show flags, view count, view states and view rectangles
  (cutscenes with per-eye view states don't pair); a kept renderer with no partner is deleted at the next Present;
  an exception in the replay turns it off for the session (and leaks the two renderers rather than free them).
- To verify in the headset: both eyes still differ (stereo; `tools/stereo-check.py`), no flicker or missing
  objects, the second render's time in `render levers:` drops by about a third, and nothing leaks.

Measuring any of the switches: `tools/perf-ab.ps1 -Key sharePass -Values 0,1` (or `depthPrepass -Values 2,1`,
`pCoresOnly -Values 0,1`) alternates the key in the deployed ini, captures each, and restores it.

### Lever 4, built: the second eye as a replay of the first eye's D3D11 calls (`sharePass = 2`)

What a replay has to patch, from Halcyon's code: it tried draw-level stereo itself, long before Stereo 2
(`d3d_capture.cpp`'s `[CAMCB96_ATLAS]`, marker-gated, off): each scene `DrawIndexed` twice into the two halves of the
target, with the **96-byte constant buffer at VS slot 1** rewritten per eye. Its shape (`LooksLikeSceneCameraCb96`):
rows 0-3 a projection-shaped 4x4 (the GPU's TranslatedViewProjection), row 4 (0,0,0,1), row 5 a world position with
w = 1 (PreViewTranslation: the eye offset is applied to row 5 only). So the vertex side of UE3's view constants is one
small buffer; the rest of a draw's constants are per primitive or per material. Pixel-side view constants (camera
position for specular, fog, screen-position scale) sit at per-shader registers and are **[verify]**: the eye diff
below lists them.

**Built (`plugin/DrawReplay`, driven by `plugin/RenderLevers`; off by default; untested in game):**
- With `sharePass = 2` the host hooks the immediate context's vtable (65 entries, above Halcyon's own draw hooks, at
  the first Present after the switch; they stay for the session, one thread-local read per call idle) and records the
  frame's first scene render as the game issues it: every state set, draw, clear, copy, resolve, query, map and buffer
  update, with the payload of each buffer update (and of each mapped buffer up to 64 KB, copied whole at its unmap).
  Typically ~20k calls and a few MB per eye, in a reserved arena; the objects it names are AddRef'd for the frame.
- The tail starts at the world DPG's light targets (`0x77BE00(renderer, 1)`, hooked as the mark: InitViews makes that
  call right after the resolve), so it holds the base pass, the lights, translucency, the foreground DPG and the finish,
  exactly what `sharePass = 1`'s engine replay runs. For the second render (a compatible one, as lever 1 pairs them) the
  host re-issues the tail through the vtable's next entries, so Halcyon's hooks see it as the second eye's rendering
  (its pass-0 capture keys on the scene-sized depth clear with the backbuffer dirty: the re-issued clear). The engine's
  per-render bookkeeping around it (RHI begin/end scene, the scene frame counter, the renderer's begun flag) is done as
  `sharePass = 1` does it; the engine's passes themselves don't run.
- **The substitution:** the second eye's view differs from the first's only in the FSceneView fields Halcyon edits on
  the game thread (research/30: ViewOrigin, PreViewTranslation, the translated matrices and inverses; in metric mode 2
  the GPU frustum). Before and after lever 1 copies the second render's fields into the kept renderer, the host snapshots
  the plain ranges; every 16-byte chunk that differs (a matrix row, and each column of any 4x4 that could start at a
  16-byte boundary, for shaders that take matrices transposed; positions by their first 12 bytes, the chunk's own 4th
  float kept) becomes a key → value pair. Each replayed constant-buffer payload is scanned chunk by chunk (a 64 Kbit
  filter on the first word, then a small open-addressing table): a chunk bitwise equal to a first-eye key becomes the
  second eye's value. Bitwise equality of 3-4 floats to a world position or a matrix row is a safe signature; chunks
  that are products of a view matrix with something per draw (a shadow's screen-to-shadow matrix, say) aren't caught:
  that's what the eye diff is for.
- **Not replayed:** query begin/end (the first eye's results stay; the second eye never reads any), texture uploads
  (one-time data), `Flush`. **Not replayable** (that frame runs as `sharePass = 1`, counted in the log): a command list
  executed in the tail (cutscenes render on a deferred context), a compute dispatch, stream-out, indirect draws, UAVs,
  class instances on a shader, a buffer update over 1 MB, a dynamic buffer over 64 KB discarded twice in the tail (its
  earlier contents would be gone). A failure inside a replay (an exception, a call that can't be re-issued) turns the
  draw replay off for the session; that eye is then rendered by the engine's passes.
- **The eye diff** (`tools/eye-diff.ps1`, `eyediff.request`): one paired frame recorded both ways, the first eye as
  above and the second eye's *engine* passes (lever 1's replay) through the same recorder, compared call by call:
  whether the two streams align (same calls, same arguments and objects: if not, a replay would issue the wrong calls),
  and for every buffer update how many 16-byte chunks differ, how many the substitution covers, and the rest listed by
  (bound shader, offset) with both eyes' values. Written to `%LOCALAPPDATA%\MEVR\perf\<stamp>-eyediff.txt`. Works at any
  `sharePass` (0 runs that one frame as 1). **It is the first thing to run in the headset:** it decides whether lever 4
  is right as built, and what to add if not.
- **To verify in the headset:** the eye diff's verdict ("aligned and fully covered"); then with `sharePass = 2`: both
  eyes still stereo (`tools/stereo-check.py`), no flicker, shadows and specular agreeing between the eyes (the classes
  the substitution could miss), the second render's time in `render levers:` well under the first's, and the log's
  `draw replay:` line (replays, substitutions per replay, the recording's size, fall-backs and why).
- The heap is not a lever: `RtlAllocateHeap` + `RtlFreeHeap` are ~2.7% of the flat render-thread samples (the
  "ntdll 15%" was mostly `NtWaitForSingleObject`, semaphores and FLS). A replacement allocator would save ~0.3 ms.

## The commander, read (2026-10-23, Ghidra, a subagent's pass; decompiled C in the scratchpad, names in symbols-le1.csv)

BioWare's `UmbraCommander::command` (`0x8C34C0`, vtable `0x11D62C0` slot 1) is the game's side of Umbra's resolve.
The two addresses the sampler blamed are `.pdata` chunk starts inside it, not call sites. By command code: 0 begins a
resolve (clears its flags; stamps `appSeconds` **as a float** at `0x1795D40`); 3 reports visible instances: per
instance a 0x50-byte `FPrimitiveSceneInfoCompact` is rebuilt from three objects (`0x8826F0`: 1.7% of the flat render
thread, all cache misses), the squared distance to the view origin computed, then `0x8BE0B0`: hidden-primitive and
draw-distance checks, UE3's own per-primitive occlusion for primitives with compact flag 0x1000 (`0x762550`: a history
TSet, `GetOcclusionQueryResult` with `bWait = TRUE`, a new query batched), UE3's InitViews per-primitive body
(`0x76A570`: relevance, static-mesh LOD and visibility bitmaps, dynamic lists, light interactions, decals, an
`ActorVisibilitySet` add that regrows from empty per view) and the proxy's `PreRenderView` once per scene frame; 5
flushes the new occluders into depth (`0x8B5FF0` → UE3's `RenderPrePass` body `0x76D820`: the real depth pre-pass,
rescanning the whole static depth lists at every flush) and issues Umbra's queries (one `DrawIndexedPrimitiveUP` per
query inside begin/end occlusion query, `0x8B6550`); 6 reads the results (`0x8B6330`); 1 ends it (a final flush).

Flat, the commander is 12.7% of the render thread: 42% of it the occluder depth pre-pass, 16% the queries and their
reads, 40% per-object bookkeeping. Nothing in it is quadratic, allocates per object or locks per object; the
candidates are small (the static-list rescan per flush 0.4%, the ActorVisibilitySet regrowth 0.2%, the compact rebuild
1.7% of which maybe half avoidable) and deep. The commander is not a lever beyond lever 1, which removes it from the
second eye entirely.

**Two waits for the GPU inside the resolve, both [verify] in VR** (flat they never triggered: the GPU is ahead):
- The results read (code 6): for a query Umbra marks as needed whose result isn't ready, it spins (`GetData`,
  `Sleep(0)`) while `appSeconds - float(stamp) < 0.01`. The stamp is a float at ~16.7 M (UE3's `appSeconds` offset),
  whose precision there is 2 s: the "10 ms budget" is really a random window, and when the spin happens it lasts
  anything from nothing to about a second. In VR the second eye's queries are issued a millisecond before they're read,
  with the GPU still on the first eye. `[Performance] umbraQueryWait = 0` moves the stamp 4 s back as soon as it's
  written, so an unready query counts as visible instead. The `render levers:` log now times the resolve's parts per
  render (instances, occluder flush + queries, results reads) and the longest single results read: the measurement.
- UE3's own per-primitive queries (`0x762550`, `bWait = TRUE`, the RHI spinning without sleeping for up to 0.5 s): the
  previous frame's query must have completed. With Stereo 2 and one view state for both eyes, the second eye's
  "previous frame" is the first eye of the same frame, whose draws the GPU may not have reached. `UMBRA
  TOGGLEOCCLUSION` (console, `GIgnoreAllOcclusionQueries`) is the zero-code A/B; lever 1 avoids it for the second eye.

## Sources

- UEVR: https://github.com/praydog/UEVR, https://docs.uevr.io/
- Unreal single-pass stereo culling: https://forums.unrealengine.com/t/pico-openxr-fix-sps-mono-culling-frustum-incorrectly-culling-edge-objects/2763119
- Unity single-pass instanced: https://docs.unity3d.com/ScriptReference/XR.XRSettings.StereoRenderingMode.SinglePassInstanced.html
- REFramework VR (AFR): https://github.com/praydog/REFramework/blob/master/src/mods/VR.hpp
- R.E.A.L. AER: https://mixed-news.com/en/real-vr-mod-dlss-ray-reconstruction/
- vorpX 3D modes: https://www.vorpx.com/forums/topic/what-the-difference-in-3d-reconstruction/
- NVIDIA 3D Vision best practices: https://developer.download.nvidia.com/whitepapers/2010/3D_Vision_Best_Practices_Guide.pdf
- MGS2-PCVR (MIT): https://github.com/Shiffo0/MGS2-PCVR
- Outlast VR mod (no licence; read only): https://github.com/Hammerthis/Outlast-Vr-Mod
- AER artefacts: https://communityforums.atmeta.com/discussions/dev-general/simple-alternate-eye-rendering-sdk-sample--why-is-alternate-eye-rendering-produc/769164,
  https://communityforums.atmeta.com/discussions/dev-pcvr/reducing-latency-by-re-using-eye-textures/140574
- YORO: https://www.uploadvr.com/you-only-render-once-vr-frame-rate-improving-technique/
- UE3 render thread and split screen: https://docs.unrealengine.com/udk/Three/RenderThreadProfilingHome.html,
  https://docs.unrealengine.com/udk/Three/OptimizingForSplitscreen.html
- Mass Effect Xbox 360 recompilation (GPL-3.0): https://github.com/nebadasSwifty/masseffect-nx/blob/main/docs/draw-count-options.md
- LE ini tweaks: https://wccftech.com/mass-effect-legendary-edition-new-ini-tweaks-shadow-resolution/
- Constant buffers: https://developer.nvidia.com/content/constant-buffers-without-constant-pain-0
- Umbra (public): Aila's thesis, https://users.aalto.fi/~ailat1/publications/MSc_thesis.pdf
- NVIDIA threaded optimization: https://community.khronos.org/t/threaded-optimization/68597
- Hybrid CPUs: https://elevenforum.com/t/forcing-performance-mode-for-background-app-on-intel-p-e-core-cpu.15763/page-2,
  https://forum.dcs.world/topic/321076-dcs-mt-improve-frame-times-consistently-with-process-lasso,
  https://forum.dcs.world/topic/357099-using-process-lasso-to-assign-e-cores
- SteamVR: https://steamcommunity.com/app/358720/discussions/0/385429254937377076,
  https://roadtovr.com/steamvr-motion-smoothing-asw-alex-vlachos/,
  https://forums.flightsimulator.com/t/steamvr-1-16-1-and-valve-index-observation/351754/11,
  https://developer.valvesoftware.com/wiki/SteamVR/Frame_Timing, https://blog.imseankim.com/?p=7949
- Not cited: two proprietary code copies posted without a licence (an Umbra SDK and a UE3 source tree) that the
  research agent read for facts; nothing from them is in this repo.
