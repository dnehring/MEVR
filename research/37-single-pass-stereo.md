# 37. Single-pass stereo: the engine renders once, the second eye comes from its draw calls

The feasibility spike the player asked for on 2026-10-25 ("Yes please. Ensure that you keep a research log for this
specifically."). Everything here is measured on the fake headset (SteamVR's null driver, `tools/fake-headset.ps1`) at
Eden Prime's heaviest cell (-31000, 145000, 900; 4,500 draws) unless said otherwise; the loop and the earlier attempts
are in [research/35](35-performance-attempt-log.md).

## Why

At the heavy spot the render thread is 10.7-11 ms warm (research/35 attempts 43-47): two full scene renders a frame
through UE3's D3D11 path, Umbra's occlusion per eye, the lights per eye, and nothing below the engine (DXVK) or inside
it (the settings, the size cull, sharing Umbra) moves it by more than a millisecond. Every closed-engine VR project that
beat this did not render twice faster: it rendered once. Alternate-eye and depth-synthesis give up geometric stereo for
one eye; the one family that keeps true stereo is the oldest: NVIDIA's 3D Vision driver, Tridef, vorpX's G3D made the
second eye from the first render's draw calls. We hold more than a driver did: the engine's draw hook, the per-view
constants, each eye's matrices, and a recorder that already captures the first eye's D3D11 stream (`plugin/DrawReplay`,
research/32 lever 4, never run in VR).

## The two designs on the table

1. **Replay (lever 4's design, completed):** the engine renders the first eye; the second eye is the first eye's
   recorded D3D11 calls re-issued with the view's constant bytes replaced by the second eye's. Shaders untouched,
   post-processing and view-dependent shading right per eye (the constants are the eye's). Cost: one engine render plus
   the API re-issue (the d3d11 + driver share of a render, ~1.5 ms at the heavy spot). Expected frame at the heavy spot:
   ~5.5 (render) + ~1.5 (replay) + ~1.2 (Halcyon) = ~8 ms, from 11.
2. **Instanced duplication (the 3D Vision way):** every draw issued twice by instancing into a second viewport, every
   vertex shader patched at its end to shift clip-space x for the second instance. Cheaper still (no re-issue), but it
   needs runtime DXBC patching of thousands of shader variants, breaks on geometry/stream-out shaders, and gets screen-
   space passes wrong at the seam. Kept as the fallback if 1 fails on something fundamental.

## What stopped design 1 before (attempts 10-11, 20)

`sharePass = 1` (the second eye on the first eye's renderer, no second visibility) froze the game: the game thread blocked
pushing into Umbra's command ring (0xD4A5B0 waits for space) while the render thread waited for commands. The ring is
drained inside Umbra's resolve; a frame with two FViewportClient::Draw calls and one resolve pushes twice and drains once.
The flat game has one Draw and one resolve a frame and never deadlocks, so the question for design 1 is whether the
second eye's view can be made without a second Draw (or with a Draw that pushes nothing), and the replay issued after the
first render on the render thread.

## Log

(entries below, newest last)

### 2026-10-25 00:24, experiment 0: the eye diff on the current build (fake headset, heavy spot)

`tools/eye-diff.ps1 -Tag heavy`: the frame recorded both ways, the game alive afterwards. First eye: 21,009 calls
(1.7 MB), 826 constant-buffer updates, 17 mapped buffers copied, **881 maps too big to copy**, the tail from entry 4,552
(16,457 calls). View fields 660 bytes a view, 20 differing 16-byte chunks between the eyes (the eye translation in the
matrices and the origin; the same values to four digits, as 6.4 UU in 31,000 should be). **Tail replayable: no**, for two
mechanical reasons: 8 calls at vtable slot 68 (`CSSetShader`, which the recorder marks unsupported though a state set is
trivial to replay) and the uncopied maps (dynamic vertex data: Umbra's ~380 occluder-query draws an eye and the decals'
per-frame vertices, by the counts; the replay needs the data or must skip those draws). The diff's alignment stage gave up
after 55 pairs (51 mismatches from the first `OMSetRenderTargets`: the two eyes' render-target pointers differ), so the
alignment report is not usable as is; the substitution does not depend on it.

### 2026-10-25 00:27, experiment 1: the second scene render not run at all (sharePass = 4)

The deadlock test: `sharePass = 4` makes the frame's second scene render return without rendering (no renderer kept,
nothing replayed). **The game froze within seconds of its first frames, before the title's Enter was ever taken**: the
mod's log stopped (265 s old when checked), the process alive and the game thread stuck. So the freeze of attempts 10-11
was never the kept renderer: two FViewportClient::Draw calls with one Umbra resolve a frame deadlock the game thread in
Umbra's command stream. Design 1 therefore needs either one Draw a frame (and the second eye's view fields synthesised
in the host from the first eye's and the eye offset), or a second Draw whose Umbra pushes are dropped on the game thread
(Halcyon marks that Draw with `t_fvcReplay`), so the engine still builds the second FSceneView for the substitution.

### The design that follows (00:35): the second render keeps its resolve, the replay takes its passes (sharePass = 3)

Umbra's command streams are 512-byte buffers (`ImpCamera m_commandQueue` and friends, 0x200 in the constructor), and
their writer (`0xD4A5B0`) waits for the consumer whenever more is pending than fits. The consumer is the resolve, on the
render thread, one frame behind the game thread. So the game thread's pushes during a Draw are consumed by the resolve
of the previous Draw: one Draw and one resolve a frame balance (the flat game), two and two balance (Stereo 2 today),
two and one deadlock (attempts 10-11, experiment 1). The second render must therefore run its own resolve.

What that costs: the second resolve is ~1.5 ms a frame at the heavy spot (the levers line: 1.1-1.6 ms a render). What
it leaves for the replay: everything after the resolve in the second render, the opaque base pass (the prepass function
`0x8BF8F0`, called from InitViews after the resolve, not inside it), the lights, the foreground, the finish: ~4 ms of
engine work a frame, replaced by the re-issue of the first eye's recorded tail with the second eye's view bytes (~1.5
ms of API calls). Expected at the heavy spot: ~5.5 (first render) + ~1.5 (second resolve) + ~1.5 (replay) + ~1.2
(Halcyon) = ~9.7 ms from 11; the remaining 1.5 ms is the second resolve, which a later step can remove by dropping the
second Draw and synthesising its view (then the stream stays balanced with one Draw and one resolve).

Mechanics, all in the host: mode 3 of `sharePass`. Render 0: recorded (stream 0), its view fields kept. Render 1: the
engine's own InitViews runs (CalcSceneView was the game thread's, the resolve drains the stream), the pass functions
after the resolve are hooked to return at once for this render (prepass `0x8BF8F0`, the DPG pass `0x76D650`, the DPG
lights `0x76D0A0`, the foreground `0x76CA70`, the finish `0x76DD10`, before-lights `0x759240`), and after the engine's
render returns, the replay of stream 0's tail with render 1's view fields substituted. Halcyon's pass-0 capture keys on
the pre-pass's depth clear, which the skipped engine passes no longer issue but the replay's tail does, so the capture
order (eye 0's image complete before the replay overwrites the targets) holds by construction. Prerequisites in the
recorder: replay `CSSetShader` (vtable 68) as a plain state set, and copy the large mapped buffers (881 a frame).

### 2026-10-25 00:40, experiment 2: mode 3 runs without a hang; the replay not yet reached

`sharePass = 3` built and deployed: the second render runs the engine's InitViews (CalcSceneView was the game thread's;
the resolve drains Umbra's stream), the seven pass functions after the resolve return at once for it, and the replay
is attempted after. **No hang** over a minute of play at the heavy spot with the head recording looping: the design's
premise holds. The replay itself did not run yet ("no first-eye view fields of the same shape"): the first eye's view
fields were read after `FSceneRenderer::Render` returned, but SceneRenderB frees the renderer at its end, so the read
saw freed memory. Fixed by capturing them in the renderer's deleter (after the render, before the free) and by running
the replay inside the second render's finish function (the renderer alive, its view fields final after Halcyon's
per-pass matrices were applied inside the render).

What the frame costs with the second render reduced to its resolve and nothing else (the second eye's image stale):
8.9-9.3 ms median fpsVR-style, render thread busy 8.6-9.0, draws 3,160-3,370 a frame (one render's worth plus the UI),
GPU span 6.3-7.3. So the floor of this design before the replay's own cost is ~9 ms at the heavy spot, against 11 with
both renders; the second resolve is 1.4 ms of it.

### 2026-10-25 00:46, experiment 3: the replay runs every frame, and the two eyes come out identical

With the first eye's view fields read at its deleter and the replay issued from the second render's finish: the replay
ran every frame (1,095 replays in 10 s, ~20,000 calls each), no hang, the frame 8.7-9.3 ms median, GPU span 6-7 ms.
But the right eye equalled the left (mean pixel difference 0.22 against 14.6 for a normal frame) and the levers line
said **0 substitutions**. Two causes:
1. The substitution keys are the differing 16-byte chunks of the captured view bytes, and the capture ranges began at
   +0x8C (a per-pass word kept there for the old copy path), so every chunk sat astride two matrix rows and matched no
   constant buffer row. A second range list aligned to the view's 16-byte boundaries (+0x90...) now feeds the capture.
2. The recorded tail ends with the first eye's own copy into the left half of the game's side-by-side backbuffer, so
   the replay's result, whatever it was, never reached the right half. The replay now shifts any viewport, scissor or
   copy that addresses the left part of a target at least twice as wide as it is by its own width (the bound render
   target's width is looked up per view and cached); scene targets one eye wide are untouched.
The GPU span staying 1-1.5 ms above a single render says the replayed draws reached the GPU but mostly died on the
depth test against the first eye's depth (the same matrices, the same geometry); with the keys matching, that changes.

### 2026-10-25 01:06, experiment 4: the replay issued no draws because the recorder never saw them

With the aligned keys and the rect shift: the replay ran (1,289 a frame-second window), **0 draws re-issued, 0
substitutions, 8-20 rects shifted**, the eyes still identical. A one-time histogram of the recorded tail showed state
calls only (SetShader 241, SetConstantBuffers 126, IASetVertexBuffers 85, Map/Unmap 48...) and not one draw. The
recorder attaches at the first Present and patches the immediate context's vtable; its log line said DrawIndexed's slot
then held d3d11's own entry. Halcyon installs its draw hooks into the same vtable later (from the swapchain's creation:
`d3d_capture.cpp`, `g_drawIndexedSlot`), writing over the recorder's entries with its own and keeping d3d11's as its
original, so from then on every draw went engine -> Halcyon -> d3d11 and the recorder saw the other 59 slots only. The
recorder now takes its slots back at every BeginRecording (the overwriter becomes its next entry, so Halcyon still
sees every call, replayed ones included).

### 2026-10-25 01:15, experiment 5: the recorder takes its slots back; the tail now has draws, the eyes still equal

With the re-hook: 19 slots had been overwritten by Halcyon and were taken back. In gameplay at the heavy spot the
first render's stream is 23,000 entries, 4 MB; its tail (from the light-targets mark) 18,800 entries with 751 draws
(728 DrawIndexed, 10 Draw, 13 instanced), 1,246 constant-buffer updates, 718 map/unmap pairs, 6 depth clears, 155
occlusion-query begin/end pairs; **515 draws come before the mark** (Umbra's resolve issues them: the flush batches of
newly visible primitives through the primitives' own draw interfaces, `0x76D820`, plus the occluder queries). The
replay re-issued 157-751 draws a frame with only 30 substitutions, 20 rects shifted; the eyes stayed identical.

Two things follow. The recorded draws (515 + 751 = 1,266) are still well short of the ~2,250 a render issues by the
probe's count, so part of each frame's draws still bypass the recorder (Halcyon re-installs its draw hooks during the
frame; the recorder re-hooks only at the start of its recording). And 30 substitutions for 1,246 constant updates
says the eye-dependent constants are mostly not reached: on the null driver both eyes share the projection and the
orientation, so the only per-eye input to a draw is the pre-view translation (a float3), and either it is uploaded
in a form the keys miss or it sits in buffers the recorder does not copy (the maps over 64 KB). The eye diff, which
was built to answer exactly this, compared the first eye's tail with the second eye's whole stream and never aligned;
it now compares both tails.

### 2026-10-25 01:30, experiment 6: why the eyes were equal, and who took the recorder's draw slots

Two findings from the eye diff of 01:20 (`2026-10-25_01-20-29-heavy2-eyediff.txt`) and a logged re-hook.

**The eye is not in the per-view matrix.** The 14 substitution keys (the view chunks that differ between the eyes)
were the view origin, the world view-projection's translation row and their transposed columns, all equal to four
digits (6.3 UU in 31,000). None was a row of the translated view-projection at +0x1D0, the matrix the vertex shaders
read: it was identical for the two eyes. That is UE3's pre-view translation at work: the engine folds each view's
`PreViewTranslation` (= minus the view origin) into every primitive's LocalToWorld on the CPU, so the GPU's matrix is
rotation x projection with no eye position in it, and on the null driver (one orientation, one projection) the two
eyes' GPU matrices are the same bytes. The eye lives in the per-draw constants: the aligned pairs of the diff showed
light positions in the pixel shader's 1 KB buffer differing by 6-7 UU in world Y (translated-world positions), and
every draw's LocalToWorld carries the eye the same way. A chunk-for-chunk substitution of the view's bytes cannot
reach that, which is why 30-39 substitutions a replay moved nothing (mean pixel difference 0.22). The cure needs no
per-draw knowledge: the replayed geometry is in the first eye's translated space, so the second eye's clip
transform is `T(pvt2 - pvt1) x TVP2` (the first eye's translation undone, the second eye's view applied: derived
from `V = T(pvt) x TV`, confirmed by the levers' sign check), and its inverse `InvTVP2 x T(pvt1 - pvt2)`; the
substitution now carries those as whole 64-byte matrix keys (rows and transposed), the first eye's matrix wherever
it is uploaded becoming the second eye's. Lighting constants in translated space stay the first eye's (6 cm off:
invisible); shadow and screen-space passes that composite the inverse with other matrices on the CPU would be
wrong by the eye offset (the 3D Vision class of artefact).

**The draws vanished mid-frame because an overlay unhooks them at every Present.** The re-hook log, now naming
the slots and their new occupants: at every Present (16-31 ms apart) the same 19 of the recorder's vtable entries
are put back to d3d11.dll's own: every draw (12, 13, 20, 21, 38, 39, 40), UpdateSubresource (48), the clears (50,
51, 52, 53), the copies (46, 47, 49), resolve (57), GenerateMips (54) and the dispatches (41, 42). Nothing hooks
them in their place: a private copy of the context's vtable (the EA overlay's, research/32) is refreshed from
d3d11's table for exactly the GPU-work entries. Halcyon's capture survives it because Halcyon hooks seven of those
functions inline as well as in the vtable (`InstallContextDrawHooks`: PatchPointerSlot, then MinHook on the d3d11
body). The recorder's take-back at each BeginRecording covered the first render until the Present, which does not
explain the frames with draws missing from the tail only; whatever the timing, the fix is the same: those 19
entries are now hooked inline on d3d11's functions through the host's hook context (above Halcyon's inline hooks,
whose jump MinHook relocates into the trampoline), and the vtable keeps the other 44. The first render's draws are
now counted both ways (Halcyon's hook count against the recorder's) in the levers line.

Also: +0x210 of the view is not the inverse of +0x1D0 (the product check at the first replay); the inverse's
place is now found by search over the captured view bytes and logged. A compute `Dispatch` (slot 41) in the tail
(~6% of frames) is now recorded and re-issued like any call.

### 2026-10-25 01:58, experiment 7: the "overlay" was d3d11's own per-context vtable; Halcyon's hooks as the ear

The inline hooks on the 19 functions saw nothing (1,975 draws a render by Halcyon's count, 0 recorded), and the
address log said why: the context's vtable names **different d3d11 functions at different times**. Inside Present
(where the recorder attaches) slot 12 holds `d3d11+...3b70`; while the scene renders it holds `...35a0`, the
function Halcyon hooked at the swapchain's creation; the same for Draw (`...3610` / `...3540`), the instanced
draws, DrawAuto and UpdateSubresource (`...18f0` / `...1a00`). d3d11 keeps a vtable per context and rewrites its
draw, clear, copy and dispatch entries between variants of each method as the context's state changes (around
Present, and apparently on other state changes mid-frame, which is where the earlier tails lost their draws).
So nothing restored the recorder's entries: the runtime swapped them for the variant it wanted, and a hook on one
variant's body never sees calls made through the other.

Two measures, both in: (1) a `[MEVR]` seam at the start of Halcyon's seven immediate-context hooks (DrawIndexed,
Draw, the two instanced draws, DrawAuto, UpdateSubresource, ClearDepthStencilView: `MevrBridge::Record*` in
`mevr_bridge.h`, forwarding to `draw_replay::Note*`), which record the call unless the recorder's own hook already
did (a thread-local mark around its next-entry call), since Halcyon's hooks demonstrably see every draw; (2) for
the twelve entries Halcyon doesn't hook (ClearRenderTargetView, the copies, resolve, GenerateMips, the indirect
draws, the dispatches, the UAV clears), one inline detour per variant (templates on a variant index, up to three
per slot, each with its own trampoline), installed on whatever function the vtable names at the start of each
recording and at the tail mark. The replay now calls through the live vtable (the recorder's own hooks pass a call
through while nothing is recorded), never a stale "next" pointer of one variant.

With (1) alone (01:58): **1,641 draws issued, 1,641 recorded** every render; the replay re-issued 1,005 draws a
frame with 505 substitutions, 63 of them the 64-byte translated view-projection (so the per-view matrix is uploaded
about once per shader change, not per draw). The eyes were still equal (mean difference 0.24, shift 0 px) and the
frame was back at 11.2 ms (render thread 9.8 busy): the recording copies 5.5 MB of constants a frame and the replay
re-issues 1,005 draws with their uploads, so the replay is no longer the ~1.5 ms API re-issue the design assumed
but close to the engine's own cost for the passes (a point for the summary). The equal eyes have a candidate cause
in the twelve unhooked entries: the scene's colour clear and the final copy or resolve into the backbuffer are among
them, so the replay drew the shifted geometry into the scene targets and the backbuffer kept the first eye's
image. Also found: the "right half" shift of experiment 3 was wrong (the eyes render in turn into the whole
backbuffer; Halcyon's pass-0 snapshot takes the first) and is off; the inverse translated view-projection is at
+0x210 after all (the first check ran on the title screen's view; the search now retries).

### 2026-10-25 02:06, experiment 8: a real second eye from the replay, at the cost of the engine's own pass

With the variant hooks (the twelve entries hooked per d3d11 variant: the render-time variants were found and hooked
at the first recording, `slot 50 variant 1 at d3d11+...3b70` and so on) and the right-half shift off, **the replay
produces a second eye**: the snapshot's eyes differ as a stereo pair does (mean difference 13.9, against 14.6 for
the engine's own pair and 0.2 before), `tools/stereo-check.py` puts the far scenery at a median +1 px (metric), and
a frozen-head A/B against the engine's own Stereo 2 at the same pose (`2026-10-25_02-08-23` mode 3,
`2026-10-25_02-08-31` sharePass 0, eight seconds apart) gives: the two left eyes (both the engine's) differ by 5.9
(foliage and idle animation in the eight seconds), the replayed right eye and the engine's right eye by 7.8, the
replayed right eye and the engine's left eye by 13.5. So the replay's right eye is the engine's right eye to within
about two units of the temporal noise, and nothing like the left. The contact sheet (left, replayed right, their
difference; the two A/B differences; the engine's right) shows the parallax pattern on every edge, the near gun
and arm with the largest shift, and the structure at the right edge of the view that only the right eye sees, in
both the replayed and the engine's right eye. The eye diff's tail scan found the translated view-projection in
96 of the tail's 2,437 constant updates (as uploaded, never transposed; the inverse never uploaded by itself), so
the matrix key does the whole job of moving the eye.

**The cost is the engine's cost.** At the same spot under the same head loop (`perf-probe`, 10 s each):

| frame (fpsVR-style, median) | first render | second render |
|---|---|---|
| sharePass 0, both engine renders: 11.4 ms | 4.7 ms | 4.5 ms (resolve + passes) |
| sharePass 3, replay: 11.25 ms | 6.0 ms (engine render + recording) | 3.2 ms (resolve 1.4 + replay 1.8) |

The replay takes 1.8 ms where the engine's passes took 3.1 (the per-draw CPU the engine spends above the API: mesh
elements, draw policies, constant shadowing), but the recording adds 1.3 ms to the first render (25,000 entries,
5.6 MB of constant data copied, each chunk hashed for the substitution at replay), and the second resolve stays at
1.4. Net: 0.15 ms. The design's premise, that an API re-issue is a small fraction of a pass, is only half true for
this engine: UE3's D3D11 pass is about 60% API and driver, 40% its own per-draw work. What is left to gain: the
recording and substitution (1.3 + part of 1.8 ms) can be cut by maybe two thirds with a non-zeroing buffer,
in-place substitution and a fixed-offset fast path (next experiment); the second resolve (1.4 ms) only goes with
Halcyon doing one FViewportClient::Draw a frame and the host synthesising the second view (the ring then
balances as in the flat game), which is the larger prize and the larger change.

Full-resolution crops of the frozen-head A/B (the walkway, the rocks and tree, the far scenery), the replayed
right eye against the engine's right eye: walkway 8.5 where the two left eyes (both the engine's, eight seconds
apart) differ by 5.5 and the stereo pair by 22; rocks 7.4 against 4.9 and 9.8; far scenery 12.9 against 12.5 and
14.4 (the clouds move). The best horizontal alignment of the replayed right eye onto the engine's is 0 px in every
crop. With dynamic shadows off (the VR settings) nothing in the crops shows a lighting difference: the residual
above the temporal noise (2-3 units) is in the foliage and the walkway's grid edges, sub-pixel.

### 2026-10-25 02:17, experiment 9: the replay path made as cheap as it gets here

Three cuts, measured one after another at the same spot under the same head loop: the recording's buffer no longer
zeroes what it appends (std::vector::resize had value-initialised the 5-7 MB a frame); the substitution works in
place on the recorded stream instead of on a copy; and it looks only at constant updates up to 512 bytes, because
the eye diff's tail scan placed every matrix hit in a 96 or 128-byte per-view buffer at +48 (96 of 2,465 updates),
while the 1-4 KB per-draw buffers carry only the view origin and pre-view translation, which the replay may leave as
the first eye's (lighting 6 cm off; the geometry is the matrix's). Substitutions fell from 505 to 99 a replay (64 of
them the matrix), the eyes unchanged.

| frame, median (p90) | first render | second render |
|---|---|---|
| both engine renders (sharePass 0, same session): 11.05 ms (12.19) | 4.70 | 4.48 |
| replay, experiment 8: 11.25 (12.29) | 5.99 | 3.20 |
| replay, non-zeroing buffer and in-place: 10.89 (12.00) | 5.81 | 3.04 |
| replay, small-buffer substitution: 10.60 (11.71) and 10.47 (11.61) on two runs | 5.78 | 2.69 |

So the single-pass replay as built gives **0.5 ms at the heaviest cell (11.05 to 10.5, about 5%)**: the second
render's 4.5 ms became 1.4 of resolve plus 1.3 of replay, and the first render grew by 1.1 ms of recording (25,000
entries and 5.6 MB of constants a frame, the irreducible part of "re-issue the first eye's calls"). The draws and
uploads the replay re-issues are the engine's own API traffic; nothing below the engine makes them cheaper (DXVK,
attempt 47).

## Where this leaves the design

- **Feasible and correct.** One engine render a frame, the second eye from its recorded calls, no hang (the second
  render keeps its Umbra resolve so the command ring balances), a second eye that matches the engine's own to within
  the temporal noise, far scenery metric, Halcyon's pass-0 capture and HUD panel unaffected. The one substitution
  that matters is the whole translated view-projection matrix, `T(pvt2 - pvt1) x TVP2`, because UE3 folds each
  view's pre-view translation into every draw; it works for any second view (a different projection per eye on a
  real headset included), and its inverse pair is in place for the passes that reconstruct positions. Not verified
  here: a real headset's per-eye projections, dynamic shadows on (projected shadows composite the inverse with other
  matrices on the CPU and would sit 6 cm wrong in the right eye), and translucency sorted for the first eye.
- **Not the win hoped for, as built: 0.5 ms of 11.** The design assumed a pass is mostly engine work above the API;
  at this spot it is about 60% the API traffic itself, which the replay must repeat, plus the recording to repeat it.
- **The remaining 2.5 ms** is the second Umbra resolve (1.4) and the recording (1.1). The resolve goes only if Halcyon
  issues one FViewportClient::Draw a frame (the ring then balances as in the flat game) and the host synthesises the
  second view's matrices itself (the first eye's translated view with the second eye's projection and the eye
  offset: the same formula the substitution uses now); the recording goes only if the engine's draw calls are made
  cheaper at the source, which is the instanced-duplication design (every draw issued twice by instancing, the
  vertex shaders patched at their end), a different project. The first is a day or two inside Halcyon's Stereo 2
  (its pass-0 capture, HUD mirror and cine paths all assume two Draws) for an estimated 9 ms frame at this cell
  (from 11): worth it only if a 90 Hz heaviest-case is the goal; the second is the 3D Vision driver rebuilt.
- **Kept:** `sharePass = 3` works and stays off by default (0). The recorder's three findings (the eye in the per-draw
  constants, d3d11's per-context vtable variants, Halcyon's hooks as the ear) are the reusable part.
- Seen at every scripted quit of the measuring loop today (console `exit`): a crash at process exit in a game worker
  thread (ntdll writing 0x24, the engine's own frames below), before and after the variant hooks, so not from them;
  the breadcrumbs file is written at each. Not chased.

## The one-Draw design (2026-10-25 02:30, `[Performance] sharePass = 5`)

The player chose to go on. The second Umbra resolve exists only because Halcyon makes two `FViewportClient::Draw`
calls a frame (the game thread pushes each view's Umbra commands; a Draw without a resolve deadlocks, experiment
1). So: one Draw a frame, and the host builds the second eye's view itself.

- **Halcyon, game thread** (`render_hook.cpp`, `[MEVR]` seams): in `HookFViewportClientDraw`, when the host has set
  `MevrBridge::g_oneDrawStereo`, the second Draw (`SfrGuardedReplay`) is not made; `NotifySfrReplayEnqueued` still
  arms the render-side pass capture and the SFR frame counter still counts, so everything downstream sees a Stereo 2
  frame. Pass 0's view edits record what pass 1 would have had: in `ApplyMetricEyeOffset` (and upstream's
  `ApplyReplayProofOffsetSEH` for metric mode 0) the world-space difference between pass 1's and pass 0's camera,
  from the same view-matrix basis Halcyon uses for the offset, plus pass 1's off-centre frustum terms (mode 2); at
  the end of `PostProcessPerspectiveFsv` (after the lean, the head rotation and the frustum) the record is keyed by
  the view's final origin and handed to the host (`MevrBridge::RecordPass1View`, a ring of eight).
- **Host, render thread** (`RenderLevers.cpp` mode 5): the frame's only scene render is recorded; after it returns
  (the renderer still alive) the view fields are captured, the pass-1 record found by the renderer's view origin
  (exact float match: the renderer's view is a byte copy of the game thread's), and the second view synthesised:
  origin + d, pre-view translation - d, the view and world view-projection moved by -d, the GPU matrix rebuilt as
  translated view x pass 1's projection (mode 2) or kept (symmetric), its inverse recomputed (Gauss-Jordan). Then
  the replay as before (`ReplayDrawsSEH`), whose matrix substitution turns the first eye's uploads into the second's.
  The replay's pass-boundary depth clear still takes Halcyon's pass-0 capture, and Present takes pass 1, as in mode 3.
- A second render that Halcyon still makes (the flag read late, a one-shot replay) is rendered by the engine and
  counted (`second renders Halcyon still made` in the levers line).

### 2026-10-25 08:18-08:55: the one-Draw frame, and three hangs that were one bug

The first run (`sharePass = 5` from launch, the heavy cell under the head loop): **one scene render a frame, no
hang, 7.3 ms median (8.1 p90) against 11.05 with two**, the resolve 1.3 ms of the render's 5.15; but "1,361
frames without pass 1's record": the host never matched the record to the renderer's view (an exact float match on
the origin), so no second eye was replayed and the right eye was the stale left. Then three hangs in a row, each
within ten seconds of mode 5 starting its replay (a tolerant 50 uu match in place): the game thread blocked in
Umbra's command-ring writer (`0xD4A5B0`, waiting for space) or on the frame fence, and **the render thread gone from
the process** (a full dump of each hang, `hangstacks.py`: 116-122 threads, none the render thread), with no
exception in either log. Halcyon's `[FIRSTCHANCE]` logger stops after 200 lines, which its own startup probes use
up, and ours recorded fatal-code first chances only into the breadcrumb ring, written on a process crash. A logged
first chance (three per address, now in `CrashBreadcrumbs`) named it: **an access violation on the render thread
reading +0x320 of a null pointer, nine milliseconds after the recorder attached**: `SynthesiseSecondView` reading
the renderer's view origin after `FSceneRenderer::Render` returned, when the renderer's view array is already freed
(the same trap experiment 2 hit; mode 3 reads the views in the deleter and replays in the second render's finish).
What a fault on the render thread does: the engine's own thread guard swallows it and the thread exits; nothing
presents again, the game thread waits on its fence or the ring forever, and nothing is logged. The one-Draw replay
now runs inside the first render's finish hook (after the engine's finish: the first eye complete and in the
backbuffer, its views alive), ends the recording there, and is wrapped in SEH that disables the mode's replay for
the session on any fault.

### 2026-10-25 09:10, the one-Draw design measured: 9.1 ms against 11.4 (the head moving), 7.3 against 9.2 (still)

With the replay in the first render's finish and guarded, `sharePass = 5` ran for minutes at the heaviest cell with
no hang and no fault, the head loop playing and then frozen, 1,024-1,323 second eyes replayed a 10 s window, every
frame finding pass 1's record (the tolerant match: the newest record within 50 uu of the renderer's view origin;
exact float equality never matched, the renderer's copy of the view is not byte-identical to the game thread's), no
second render by Halcyon. The eyes under the head loop: a stereo pair (mean difference 14.8 against the engine's
own 14.1, far scenery +1 px). Frozen-head A/B against the engine's own Stereo 2 at the same pose (`2026-10-25_09-09-28`
one Draw, `09-09-39` sharePass 0): the two left eyes differ by 7.0 (foliage, eleven seconds apart), the replayed
right eye and the engine's right eye by 6.4, the replayed right eye and the engine's left by 13.7: **the synthesised
second eye is the engine's own to within the temporal noise.** (One capture earlier, two seconds after the head
replay stopped, had a black right eye with a sheared red remnant; not reproduced fourteen seconds after the stop,
nor on later stops: a transient at the pose jump, not chased.) The stereo check on the frozen pair read -7 px on
seven far matches where the moving pair read +1 on eleven; the engine's own frozen pair was not checked.

| frame, median (p90) | both engine renders | one Draw + replay | gain |
|---|---|---|---|
| the head loop playing | 11.40 (13.14) | 9.08 (10.28) | 2.3 ms, 20% |
| the head still | 9.22 (10.95) | 7.31 (8.19) | 1.9 ms, 21% |

The one render costs 7.2 ms (4.6 of engine render, ~1.1 of recording, ~1.4 of replay inside its finish); the second
resolve (1.4) and the second render's own CPU are gone; the game thread pushes one Draw's Umbra commands and the ring
stays balanced as in the flat game. In the headset at 90 Hz this is the heaviest spot inside the 11.1 ms budget with
2 ms to spare where Stereo 2 was 0.3 over.

Not yet done, in order of what it would take: the record's key (a frame counter through the view would replace the
origin match); a real headset's per-eye projections (the mode 2 rebuild `TVM x P_eye1` has run only on the null
driver's symmetric eyes, where it is the identity case); dynamic shadows on; a watch for the black-eye transient;
and the cost of the recording itself (1.1 ms: 7 MB of per-draw constants copied a frame), which no replay design
avoids. `sharePass` stays 0 by default.

### 2026-10-25 12:40-13:05, before the headset: the PS VR2's own projections, the recording's cost, the other states

Three checks the user asked for before trying `sharePass = 5` in the PS VR2 (they could all run on the fake headset).

**The replay's `fovs=1`** (`tools/replay.ps1 -Fovs`): the recorded per-eye FOVs stand in for the runtime's, so the
null driver renders the headset's own asymmetric projections. The user's head recordings carry the PS VR2's: left
(-61.5, 43.4, 53.0, -53.0), right (-43.4, 61.5, 53.0, -53.0) degrees. A still-head copy of the 19:34 recording (every
frame at frame 0's pose, scratchpad `stillrec.py`) with the console's `pause` (the world frozen, still rendering) takes
the temporal noise out of the frozen-head A/B: two captures of the same engine-rendered eye now differ by 3.7-4.2
(they differed by 7.0 with the foliage moving), so a synthesised eye can be held to that floor.

**Experiment 10: mode 2 under the PS VR2's projections.** `sharePass = 5` (A) against both engine renders (B) at the same
pose, paused. A.right vs B.right 4.15, A.left vs B.left 4.20 (the floor), stereo 36; far-scenery residual +4.1 px
against B's +4.6. So the rebuild `TVM x P_eye1` holds with the right eye's own off-centre term (p8 = 0.32), which the
null driver's symmetric eyes never exercised. **But one object was missing from the one-Draw right eye**: a piece of
geometry at the image's right edge (x 1809-2343 of 2344), inside the wedge from +43.4 to +61.5 degrees that only the
right eye sees. Not Umbra's eye-matrix queries (`umbraEyeMatrix = 0` gave the same crop, 5.81 against 5.89), and not
the frustum cull (the CPU window +-1.8418 x +-1.3290 holds both eyes' frusta, per the containment line): what is left
is the occlusion test that draws bounding boxes with the GPU matrix (+0x1D0, the eye's own frustum) against the depth
buffer, the engine's own at 0x759240, where a box outside the first render's viewport returns no pixels and the object
stays "occluded" in the history. With two Draws, pass 1's own render saw it. **The frame's only render decides
visibility for both eyes, and its occlusion tests see only its own frustum.** The fix is in the design, not a lever:
one-Draw stereo renders both eyes on the symmetric window (`PlanStereo` picks mode 1 while `g_oneDrawStereo` is set,
and says why in the `[MEVR-STEREO]` plan line). The window holds both frusta, so the first render's tests cover the
right eye's wedge. The price is mode 1's angular resolution: on the PS VR2 the union window is 123 degrees wide against
the eye's own 105, 15% fewer pixels per degree at the same target (`tools/vr-game-settings.ps1 -ResX` 17% wider buys it
back for 17% more GPU pixels; the GPU had 4.9 ms of 11.1 at this spot with shadows off).

**Experiment 11: both on the symmetric window** (Halcyon's `sfr2MetricEyeFrustum = 0` for the session, so B renders mode
1 too; the same pose, paused): A.right vs B.right 3.89, A vs A2 (the same mode twice) 3.70, the right-edge crop 4.40
against 4.18: the object is there and the eyes equal the engine's own to the floor; far scenery +1.2 px (B +2.7). The
HUD panel comes out the same (6.1% of its pixels covered in both).

**The recording's cost is not the maps.** The hypothesis was that the recorder reads the driver's mapped memory back at
every Unmap (write-combined, slow to read). Timed: 16-17 maps, 17-18 KB, 0.05 ms a frame. The 7.7 MB a frame of
recorded constants come through UpdateSubresource from the engine's own cached memory. A redirection of discard maps
to cached memory was built, measured moot and removed within the hour (the map copy's timing stays in the levers line:
`recording: N maps (KB) copied in X ms a frame`). What the recording costs is its 33,000 hook entries a frame: the
sampled render thread (`tools/sample-render-thread.ps1`, 10 s under the head loop) has about 7% of its samples in the
host's hooks under the RHI's set-state and draw functions and about 3% in the replay's own code, against 25% in the
engine's render below our scene-render hook. Per-call bookkeeping, not a copy; the next cut would be fewer entries
(redundant state calls filtered at record time), not cheaper ones.

**The pass-1 record's key** carries the view's forward too (the view matrix's third column, `ReadFsvForwardSEH`), and
the render thread picks the record closest in origin (within 50 uu) and forward. The game thread runs a frame ahead,
so "the newest within 50 uu" paired a frame with the next frame's record whenever the head turned in place (the
replay's stop, a recenter, a cut): the sheared right eye seen once after a replay stopped is that case.

**Frame time today** (the head loop, the runtime's FOVs, the same spot): `sharePass = 5` 8.93 (10.01) and 8.61 (9.68)
ms against 11.34 (13.36) with both engine renders; 0 frames without a record, 0 not replayable, no fault.

**The sweep of the other states** (`sharePass = 5`, the head loop; scratchpad `sweep.ps1`): the game's pause menu (Esc:
Stereo 2 stops rendering the world, mode 0, no eye images to capture, and gameplay comes back as before), our main menu
over the world (both eyes drawn, 46/43 mean brightness against 50/49 without it), three replay stops (the head jumps
from the recording's pose to the null driver's: the right eye present every time, 42.1 / 41.9 / 42.0 against the left's
41.8 / 41.6 / 41.7, far scenery -7 px as that pose always read), then a level load by console (`open BIOA_PRO00`, the
game's intro). **After the load no record matched**: the rendered view sat 70 uu from the newest of pass 0's records
(`no pass-1 record for the view at (-7477, 13294, -34706); newest at (-7408, 13311, -34727)`, 2 records a frame at 23
fps) for the whole text crawl, so the frame had no second eye and the right eye showed the left view (mono, not
black). A view edited after pass 0 recorded it, by something the intro's cine path does that gameplay doesn't; not
chased. **The design's answer is a health check**: while the render thread finds no matching record for 3 frames
running, the host clears `g_oneDrawStereo` and Halcyon's second Draw resumes (the engine renders the right eye as in
`sharePass = 0`; the host stops recording and only asks, in each first render's finish, whether a record would
match); after 30 matches running one Draw resumes. Logged as `one Draw: no pass-1 record matched the rendered view
for 3 frames running: Halcyon's second Draw resumes until they match again` and the reverse; the levers line carries
`its health: N fallbacks, N re-arms, in the fallback now`. Verified on the same load: the fallback engaged within the
first second after `open` and the intro ran with both engine renders and both eyes (6.6 / 5.9 mean, a dark scene). The
re-arm has not been exercised (the intro never returned to gameplay); the check never fired in gameplay (0 misses over
every gameplay period today). The session after that restart also showed the forced window working as a user would see
it: `sharePass = 5` with Halcyon's per-eye frustum on gives `stereo mode 1 ... the symmetric window because one Draw a
frame renders both eyes from the first render's visibility`, each eye's submit still declares its own FOV with its
sub-rectangle of the window (the capture's rect 1775 px of 2344), far scenery -0.3 px.

Not verified today: the re-arm after a fallback; a conversation or in-game cutscene camera under one Draw (the intro's
cine camera is the case that fell back); dynamic shadows on; the weapon wheel and Halcyon's own menu (no key the
scripts can send).
