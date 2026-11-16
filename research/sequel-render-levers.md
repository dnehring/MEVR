<!-- SPDX-License-Identifier: MIT -->
# ME1's render levers in the sequels: native addresses and layouts

ME1's renderer performance work (`plugin/RenderLevers` and its helpers, research/22, 32, 35, 37) is "group 2" in
`docs/trilogy-parity-ledger.md`: the same technique, needing each game's own native addresses. This note gives, per
sequel, the counterpart of every function, global and structure offset the levers use, how each was verified, and a
recipe per lever. `plugin/RenderLevers` finds each address **by its row name** in that game's `kTable` of
`src/engine/Addresses.hpp` (the names are LE1's, exactly); the layout offsets are entered in `RenderLeversTable.hpp` by
lane PL from the "Layouts" tables here.

Native code is described in words with RVAs; no decompiled code is in this file.

## ME2

LE2 is `MassEffect2.exe` 2.0.0.48602 (timestamp `0x60AEEFA5`). All RVAs below are in that build. Written 2026-11-16 (lane R2).

### Summary

- **ME2 has Umbra, and ME1's whole scene-render path.** The render-thread scene render, its pass functions, BioWare's
  Umbra commander, the D3D11 RHI and the scene captures are the same code as LE1's, compiled again: nine of the
  functions below and all six leaf functions are identical instruction for instruction (same registers, same
  displacements), and the others differ only where a structure grew or shrank or the compiler chose other registers.
  Every lever's address is **found**.
- **One layout difference matters:** `FViewInfo` is `0x11A0` bytes in ME2 (LE1 `0x12F0`). `FSceneView` (the first part
  of it) is `0x4E0` bytes (LE1 `0x4E4`) and identical up to there, so every view offset the levers use is the same.
  `FSceneRenderer`, the Umbra commander, the instance record and the whole RHI vtable (408 slots) are the same.
- **What ME2's SFR changes** (Halcyon's ME2 stereo, `src/halcyon/le2`): it gives each of its two Draws its own
  `FSceneViewState`, which LE1's does not. That makes `perEyeOcclusion` and `ue3QueryWait` moot (the stall they cure
  does not exist there) and `umbraEyeMatrix` a no-op until each eye has its own off-centre frustum. One-Draw is possible
  but needs four changes in Halcyon's ME2 code (named under "One-Draw stereo").

### How each address was found (so it can be repeated)

1. Both games' decrypted `.text` dumps (`%LOCALAPPDATA%\MEVR\dump\MassEffect-60AEEFA2.text.bin` LE1,
   `-60AEEFA5` LE2) were disassembled function by function (bounds from `.pdata` of the spliced exes in
   `%LOCALAPPDATA%\MEVR\ghidra\bin`, chained unwind chunks merged). Each instruction became a token: mnemonic and
   operands, with direct call and jump targets and RIP-relative displacements removed, **structure displacements kept**.
2. For an LE1 function, the ME2 function with the most similar token sequence was taken (3-token shingles to rank,
   then a sequence ratio over the whole function). "1.000" below means the two functions have the same instructions with
   the same structure offsets; a lower ratio is followed by what differs.
3. Callees and globals come from **aligning** a matched pair: the n-th call or RIP-relative reference of LE1's function
   and of ME2's name the same thing. LE1's `0x76EE20` aligned with ME2's `0x34BF70` (1.000) gives 18 callees and 9
   globals at once; each callee was then matched on its own as in step 2, and both answers agree for every row.
4. Six of the eight data rows were confirmed a second way: LE1's own table pattern for that global matches exactly once in
   ME2's `.text` and references the same ME2 address.
5. Leaf functions (no `.pdata`) were compared byte for byte.
6. ME2's functions whose ratio is under 1.000 and whose signature a hook depends on were read in Ghidra
   (`tools\ghidra\decompile.ps1 -Game LE2 0x342480 0x34AE50 0x354570 0x3146C0 0x3FF410 0x3F3ED0 0x372F50 0x3DBB80`).
7. The RHI's vtable was located through `RHICreateBoundShaderState` (the only pointer to it in `.rdata`) and compared
   with LE1's slot by slot.

Patterns are from `tools/rva-patterns.py`'s pattern maker over ME2's dump, each unique there; for data rows the
reference site was chosen by hand (a global with thousands of references makes the tool's search of every site take
hours: use the site the alignment gave). ctest `mevr-tests` checks every row against the dump (`tests/ExeTests.cpp`
`TestAgainstLe2Dump`).

### The table

State: **V** found and verified (the function identical to LE1's, or read in ME2's own code, and every offset the hook
uses seen in ME2's code); **U** found, some offset or behaviour not verified (named). Nothing is "not found".
"Row" is the row's name in `engine/Addresses.hpp` (LE1's and ME2's rows have the same name); the pattern is that row's.

| Lever | Row | LE1 | ME2 | State | Verified how | Offsets | With ME2's SFR |
|---|---|---|---|---|---|---|---|
| accounting, every lever | FSceneRenderer::Render | `0x76EE20` | `0x34BF70` | V | 1.000; 309 instructions | renderer +0x18, +0x28, +0x5C, +0xD8, target vtable +0x18/+0x20, RHI +0x628/+0x620: all seen in it | yes: two calls a frame in gameplay (pass 0, pass 1) |
| Umbra timing, `umbraQueryWait` | UmbraCommander::command | `0x8C34C0` | `0x3FF410` | V | 0.978; read in Ghidra: codes 0, 1, 3, 5, 6, 9 do what LE1's do | (commander, code); view +0x320 origin | yes |
| `umbraQueryWait` | Umbra resolve start stamp | `0x1795D40` | `0x175D2A0` | V | aligned; code 0 writes it as a float, code 1 zeroes it | a float | yes |
| `ue3QueryWait` | FD3D11DynamicRHI::GetQueryData | `0x9A04B0` | `0x986790` | V | 1.000 | (rhi, query, out, size, bWait) | hook works; **moot** (below) |
| `keepBoundShaderStates` | FD3D11BoundShaderState constructor | `0x9A89F0` | `0x98ECD0` | V | 1.000; its only caller is `RHICreateBoundShaderState` `0x991690` (LE1 `0x9AB3B0`, 1.000) | 9 arguments as LE1; refcount +8; handle = object +0xC | yes, independent of stereo |
| `keepBoundShaderStates`, replays, captures | GDynamicRHI | `0x17938C8` | `0x174C648` | V | aligned in 6 functions; LE1's pattern finds it | vtable `0x1227C50`, 408 slots, each slot's function matches LE1's (`0x120FD20`) at 1.000: +0x8A0 AddRef (ME2 `0x992830`), +0x628, +0x620, +0x520, +0x340, +0x4C0 | yes |
| `minScreenRadiusPx`, `umbraSharedVisibility` | UmbraCommander visible instance | `0x8BE0B0` | `0x3F3ED0` | V | 0.976; read in Ghidra; its only caller is command code 3 | 7 arguments as LE1 (distance squared in xmm2); commander +0x68 view; record +0x38 radius (the record's builder `0x30E5E0` is LE1's `0x8826F0` byte for byte); view +0xD0 projection | yes |
| `umbraEyeMatrix` | UmbraCommander::IssueOcclusionQueries | `0x8B6550` | `0x3DBB80` | V | 1.000 | commander +0x68 view, +0x5C params-set flag; passes view +0x260 to RHI +0x4C0 | hook works; **a no-op today** (below) |
| `umbraShareResults` | Umbra read of one query result | `0xD48380` | `0xD55BC0` | V | the same 4 instructions | context +0x1CC head, +0x1C8 count | hook works; little to gain (below) |
| one-Draw, sharePass | FSceneRenderer deleting destructor | `0x4ED700` | `0x342480` | V | call site aligned (the end of the scene render, `(renderer, 1)`); read in Ghidra | (this, flags); frees `0x110` bytes when flags & 1. ME2's has the destructor inlined (364 bytes; LE1's is a 52-byte thunk) and a unique pattern | yes |
| one-Draw (the tail's start) | FSceneRenderer::RenderDpgLightTargets | `0x77BE00` | `0x372F50` | V | call site aligned in InitViews (`0x345040`): after the Umbra resolve `0x3F8380`, before the base pass, with dpg 1; 0.686 (other registers, two RHI calls made through helpers); read in Ghidra | (renderer, dpg) | yes |
| sharePass 1, 3 | RenderUmbraBasePass | `0x8BF8F0` | `0x3F80C0` | V | 0.994: only the view stride differs | (renderer) | yes |
| sharePass 1, 3 | before-lights pass | `0x759240` | `0x336B20` | V | 0.982: view stride, offsets past `FSceneView`, one RHI call moved | (renderer) | yes |
| sharePass 1, 3 | DPG pass | `0x76D650` | `0x34A660` | V | 1.000 | (renderer, dpg, uint* dirty) | yes |
| sharePass 1, 3 | DPG lights | `0x76D0A0` | `0x34A0B0` | V | 0.997: view stride | (renderer, dpg, flag, uint* dirty, arg5) | yes |
| sharePass 1, 3 | foreground pass | `0x76CA70` | `0x349A20` | V | 0.950 (registers); arguments at the aligned call site as LE1's | (renderer, 2, int* out, uint* dirty, arg5) | yes |
| one-Draw (the replay's place) | scene render finish | `0x76DD10` | `0x34AE50` | V | 0.837: one per-view loop moved; read in Ghidra | (renderer, int translucency, int dirty); renderer +0x60 views, +0x68 count | yes |
| sharePass 1 | FSceneRenderTargets::Allocate | `0x772EB0` | `0x353AB0` | V | 1.000 | (targets, x, y, 0) | yes |
| sharePass 1 | translucency check | `0x7666A0` | `0x343E80` | V | 0.943: view stride | (renderer) | yes |
| sharePass 1, 2 | SetUmbraCommander / wireframe state | `0x8C1AE0` | `0x3FB390` | V | the same 3 instructions; writes the commander index | (byte) | yes |
| sharePass 1 | GSceneRenderTargets | `0x167D0E0` | `0x161DDB0` | V | aligned in 5 functions | back buffer surfaces at +0x3A8, +0x3C0 (LE1 +0x394, +0x3AC): not read by the levers | yes |
| sharePass 1, 2, 5 | scene frame counter | `0x178779C` | `0x175301C` | V | aligned; LE1's pattern finds it | int | yes |
| sharePass 1 | depth dirty flags | `0x1795D38` | `0x175D298` | V | aligned; LE1's pattern finds it | uint | yes |
| sharePass 1 | GSkipBeforeLights | `0x167D49C` | `0x161E180` | V | aligned; LE1's pattern finds it | int | yes |
| sharePass 1 | Umbra commanders | `0x1798E88` | `0x175D348` | V | aligned; LE1's pattern finds it | pointer array | yes |
| sharePass 1 | Umbra commander index | `0x1795D28` | `0x175D288` | V | aligned; LE1's pattern finds it; `0x3FB390` writes it | int64 | yes |
| sharePass (Eligible) | show flags' bits per word | `0x164C720` (a literal in RenderLevers.cpp, no LE1 row) | `0x1618A30` | V | aligned in 5 functions | uint (32) | yes |
| captures | scene capture's CaptureScene thunk | `0x742C80` | `0x329B90` | V | the same 16 bytes; called from the capture loop at the same place | (capture info, main renderer) | yes; nothing in ME2 makes a capture yet |
| captures | FSceneRenderTargets::BeginRenderingBackBuffer | `0x773D30` | `0x354570` | U | call site aligned (the finish); read in Ghidra | (targets, flags); binds RHI +0x520 as LE1. **ME2's sets no viewport** (LE1's calls RHI +0x340 itself), and its two proxy callers make no RHI +0x340 call of their own: where the final pass's viewport comes from (a quad-drawing helper, or the one left set) is not read, so whether a capture's final pass covers its own target is unverified | yes |
| UI world figure | scene capture's Umbra camera thunk | `0x7526C0` | `0x333240` | U | the same bytes, one match | not read further: the feature is LE1's equipment screen | not applicable |
| body bounds | USkeletalMeshComponent::UpdateBounds | `0x58EEC0` | `0x798350` | V | 0.964 over 390 instructions: only component offsets differ (+8); its last call is the Umbra hand-over | (component). `Bounds` comes from reflection | independent of stereo |
| body bounds | UPrimitiveComponent::UpdateUmbraBounds | `0x25CFE0` | `0x3146C0` | V | 0.965; read in Ghidra | (component); the Umbra object at **+0x214** (LE1 +0x210), tested for null inside; safe to call again | independent of stereo |

Not rows (documented, nothing hooks them): the inner `FSceneRenderer::Render` LE1 `0x76BC00` = ME2 `0x348BE0` (1.000);
`InitViews` `0x768090` = `0x345040` (0.974); the Umbra resolve `0x8BFBB0` = `0x3F8380` (0.995); the base pass of one view
`0x76C1D0` = `0x3491B0` (0.914); `UpdatePrimitiveOcclusion` `0x762550` = `0x33FBE0` (0.980); the `FSceneView` copy
constructor `0x763450` = `0x340970`; the commander's results read `0x8B6330` = `0x3DB960` (1.000) and occluder flush
`0x8B5FF0` = `0x3DB620` (0.986); the Umbra camera's parameter getter `0xD4BBA0` = `0xD59740` (the same bytes). All are in
`tools/ghidra/symbols-le2.csv`. The bound-shader-state cache global (LE1 `0x1793518`) was not looked for: no lever reads it.

### Layouts

Enter these in `RenderLeversTable.hpp`. "same" = the same value as LE1, seen in ME2's own code.

| Structure | Field | LE1 | ME2 | Seen in |
|---|---|---|---|---|
| FSceneRenderer | size | 0x110 | same | the deleter `0x342480` frees 0x110 |
| | +0x00 scene, +0x18 render target, +0x28 show flags (12 bytes compared), +0x5C Umbra path flag, +0x60 views, +0x68 view count, +0xD8 scene-begun flag | | same | `0x34BF70`, `0x34AE50` |
| FViewInfo | **stride** (`kViewStride`) | 0x12F0 | **0x11A0** | 11 functions (`add r, 0x11a0` / `imul r, r, 0x11a0`) |
| FSceneView | size | 0x4E4 | 0x4E0 | the copy constructor `0x340970`: LE1's last copied dword (+0x4E0) is absent, everything before identical |
| | +0x08 State; +0x60..+0x8C (the block `Compatible` compares; +0x64, +0x68, +0x7C, +0x80 written back by the finish); +0x90 ViewMatrix; +0xD0 ProjectionMatrix; +0x190 TranslatedViewMatrix; +0x1D0 TranslatedViewProjection; +0x210 its inverse; +0x250 PreViewTranslation; +0x260 ViewProjection; +0x320 ViewOrigin; +0x4D4 the Umbra camera | | same | the copy constructor; `0x3DBB80` (+0x260, +0x320); `0x3FF410` (+0x320); Halcyon ME2's own `kFsv*` constants |
| | `kViewPlainRanges` {0x8C,0x140} {0x190,0x330} {0x480,0x4B0} {0x4C0,0x4D0}; `kViewCaptureRanges`; `kViewCompareEnd` 0x8C; the owned containers at 0x44, 0x140, 0x330 | | same | they are the copy constructor's plain copies, which are identical |
| FViewInfo past FSceneView | (not used by a lever) | e.g. +0x1194, +0x12DC | −0x150 (e.g. +0x1044, +0x118C) | InitViews, the finish |
| FSceneViewState | +0xD10 occlusion history set | 0xD10 | same | `0x33FBE0` |
| | the set's size (0x48) | 0x48 | not measured (same engine TSet; the lever is moot) | — |
| Umbra commander | +0x08 query context; +0x34 + 8i base-pass flag per view; +0x5C view-params-set; +0x68 view; +0x94 | | same | `0x3F8380`, `0x3DBB80`, `0x3F3ED0`, `0x3DB960` |
| Umbra query context | +0x1C8 count, +0x1CC head | | same | `0xD55BC0` |
| | +0x2168 ring base, 0x19-byte entries, result at +9 | | not read (only `umbraSharedVisibility` 2/3 uses them: never set) | — |
| Umbra instance record | +0x00 primitive scene info, +0x20 origin, +0x2C extent, +0x38 sphere radius | | same | `0x30E5E0` |
| Umbra camera | impl +0x420 parameter word | | same | `0xD59740` |
| | camera +0x10 → impl | | not read (`umbraSharedVisibility` only) | — |
| GDynamicRHI vtable | +0x8A0 AddRef bound shader state; +0x628 / +0x620 scene begin / end; +0x520 set render target; +0x340 set viewport; +0x4C0 set view parameters | | same | the two vtables match slot for slot (408 slots) |
| FD3D11BoundShaderState | refcount +8; handle = object +0xC; size 0x84 | | same | `0x98ECD0`, `0x991690` |
| FRenderTarget vtable | +0x18 GetSizeX, +0x20 GetSizeY | | same | `0x34BF70` |
| | +0x08 GetRenderTargetSurface (returns a pointer to the surface reference) | | same | the view finish `0x338B60` (LE1's `0x75B2E0`, 1.000): renderer +0x18 → vtable +8 → RHI +0x520, then RHI +0x340, exactly what `scene_capture_target::Bind` does; the view's +0x4B4 flag too |
| USkeletalMeshComponent | the Umbra object | +0x210 | +0x214 | `0x3146C0` (BodyBounds does not read it) |

Signatures: every hooked function takes the arguments LE1's takes, in the same registers and stack slots (the identical
ones by construction; the others read at their aligned call sites and in Ghidra). All are the Microsoft x64 convention.

### Per lever: can ME2 have it, and the recipe

(Line numbers below are as of main `036d544b`, 2026-11-16; they drift, so each reference also names its function or a
string to search for.)

Halcyon's ME2 stereo ("SFR", `src/halcyon/le2/calcview_hook.cpp`): `DrawDetour` (line 1086) runs the game's
`FViewportClient::Draw` (pass 0, the left eye) and then calls it again (`SfrReplayDrawSEH`, pass 1, `t_replay = true`).
In `CalcViewDetour`'s SFR branch (line 1432) each pass gets **its own `FSceneViewState`** (`g_eyeStateLeft` for pass 0,
`g_eyeState` for pass 1, `SetViewState`), then `ApplyEyeOffset(∓halfEye)` (a shift along the game camera's right axis,
the view matrix's column 0), `ApplyHeadLook`, `ApplyFov`, `RefreshInverseSlots`. Both passes use the same symmetric
projection; convergence is an image shift at submit (`me2_xr.cpp`). On the render thread `d3d_capture.cpp`'s
`SfrMaybeCapturePass0AtClear` (line 1944) copies the backbuffer as the left eye at the scene-sized depth clear that
follows pass 0's composite; the UI is drawn once and mirrored into that copy.

So, on the render thread, a gameplay frame in ME2 is what it is in LE1: two calls of `0x34BF70`, each with its own
Umbra resolve. The differences are the per-eye view states and the symmetric frustum.

#### 1. Accounting (the per-eye render split). Possible; do this first.

1. Hook the row "FSceneRenderer::Render" (`0x34BF70`, `void(void* renderer)`), as `SceneRenderHook`.
2. Count the calls per Present and time each (`gRenderIndex`, `t_renderIndex`). Reset the index where the LE2 host
   presents (its `OnSwapChainPresent` seam).
3. Verify: in gameplay the log's "scene renders per frame" is 2; at the title and in menus 1.
4. The capture test (`IsCaptureRender`) reads renderer +0x18 and the target's vtable +0x18/+0x20: same as LE1.

#### 2. Umbra resolve timing and `umbraQueryWait`. Possible.

1. Hook "UmbraCommander::command" (`0x3FF410`, `void(void* commander, unsigned code)`), as `CommandHook`. The codes mean
   what they mean in LE1: 0 begin (stamps the start time), 1 end, 3 visible instances, 5 occluder flush and queries, 6
   results read, 9 debug.
2. For `umbraQueryWait = 0`: after code 0 returns, subtract 4.0 from the float at "Umbra resolve start stamp"
   (`0x175D2A0`). ME2's code 0 computes the stamp exactly as LE1's (seconds as a float, plus 16777216).
3. Verify: the levers line shows the resolve's four parts for render 0 and render 1; with the lever at 0 the "query
   results" part of render 1 drops.
4. Whether ME2's second render spins in the results read at all is not known (no perf probe on ME2 yet): measure before
   keeping it.

#### 3. `keepBoundShaderStates`. Possible; independent of stereo; the cheapest to port.

1. Hook "FD3D11BoundShaderState constructor" (`0x98ECD0`), `BoundShaderStateCtorHook`'s 9-argument signature unchanged
   (self, declaration, strides, VS, PS, three more, the RHI): the constructor is LE1's instruction for instruction.
2. After the original returns, for a key seen before: read `GDynamicRHI` (`0x174C648`), call its vtable slot `0x8A0`
   with (rhi, object + 0xC). The int at object +8 is the reference count (the log's "+8 went N -> N+1" check applies).
3. Verify: `render levers: bound shader states kept: the RHI's AddRef is at RVA 0x992830`, and the count rises by 1.
4. First check it is worth it: sample ME2's render thread (`tools/sample-render-thread.ps1`) for time under
   `0x98ECD0` / `CreateInputLayout` at a heavy spot. LE1's gain came from the dynamic-mesh drawing policies building one
   per draw; whether ME2's still do is not measured.

#### 4. `minScreenRadiusPx`. Possible.

1. Hook "UmbraCommander visible instance" (`0x3F3ED0`), `UmbraInstanceVisibleHook`'s signature unchanged
   (`int(commander, record, float distanceSq, a4, a5, a6, a7)`).
2. Read the view at commander +0x68, its projection [1][1] at view +0xD0 + 0x14, the radius at record +0x38: all as LE1.
3. Return 0 for an instance under the threshold, else call the original.
4. Verify: the log's first-instance line gives a projection [1][1] near 1 and a plausible radius and distance; the size
   distribution is logged with the threshold at 0 before it is set.
5. One difference to know: at one test ME2's function asks the object at record +0x10 through a virtual call (vtable
   +0x4F8) where LE1's tests bit 0x1000 of record +0x48, and it lacks one check LE1's makes of the renderer's +0xD8
   flag; neither changes the hook.

#### 5. `umbraEyeMatrix`. The hook is possible; it does nothing on ME2 today.

The lever puts the world-space equivalent of view +0x1D0 into +0x260 around "UmbraCommander::IssueOcclusionQueries"
(`0x3DBB80`, `void(commander)`), because LE1's life-size stereo writes each eye's off-centre frustum into +0x1D0 only.
Halcyon's ME2 edits (`ApplyEyeOffset`, `ApplyFov`) rebuild **both** +0x260 and +0x1D0 from the same projection, so the
two agree and the lever's own test (`NearlySame`) passes every call through untouched. Port it together with the
per-eye frustum (ledger R06), not before. The recipe is LE1's unchanged: commander +0x68, +0x5C, view +0x90, +0x190,
+0x1D0, +0x250, +0x260 are all the same. Keep the `ScissorExempt` bracket only if `eyeScissor` is ported.

#### 6. `ue3QueryWait` and `perEyeOcclusion`. Moot on ME2: do not port.

Both cure one thing: LE1's two eyes render through **one** view state, so the second render finds the first eye's
occlusion query of the same frame as "last frame's" and waits for it (`GetQueryData` with bWait). ME2's SFR gives each
pass its own view state (`calcview_hook.cpp` line 1445), so each eye reads its own previous frame's queries, which is
exactly what `perEyeOcclusion` emulates by swapping the history set at view state +0xD10. `BeginEyeHistory` would see
"its own state already" every frame and skip. The addresses are in the table (`0x986790`; +0xD10 the same) if ME2's SFR
ever shares a state. LE1 measured a cost of per-eye history (research/35 attempt 37: every ~10th frame drew three times
the meshes); whether ME2 shows it is worth one look at the draws-per-frame distribution once the accounting runs.

#### 7. `umbraShareResults`, `umbraSharedVisibility`, `perEyeViewState` (commander 1). Do not port.

LE1's own results: `umbraSharedVisibility` failed in three forms and is "never set"; a second render on commander 1
hangs; `umbraShareResults` is off by default. Their addresses exist in ME2 ("Umbra read of one query result"
`0xD55BC0`, the camera parameter getter `0xD59740`, "SetUmbraCommander / wireframe state" `0x3FB390`), with the ring
fields at +0x2168 and the camera's +0x10 not read.

#### 8. One-Draw stereo (`sharePass = 5`) and the D3D replay. Possible; the largest job; last.

Every native address it needs is in the table and verified. The engine side is LE1's: record the first render's D3D
calls from the tail mark (the "FSceneRenderer::RenderDpgLightTargets" call with dpg 1, which `InitViews` makes right
after the Umbra resolve) to the "scene render finish", then replay them in the finish hook with the second eye's view
bytes. What has to change in Halcyon's ME2 code, exactly:

1. **`calcview_hook.cpp` `DrawDetour`, the replay block** (from `t_replay = true;` at line 1141 through the
   `[NOOPRETRY]` retry to the `[SFR] Draw replay #` log at line 1193): while the host's one-Draw flag is set (LE1's
   `MevrBridge::g_oneDrawStereo`), `SfrReplayDrawSEH` must not be called, and neither must its retry. Still count the
   frame as delivered: `g_sfrReplays` up, `g_sfrSkipStreak` to 0, `g_sfrNoopReplays` to 0. Otherwise `me2_xr.cpp`'s
   `[STEREODEAD]` watchdog (line 2322, `GetSfrNoopReplays()`) reports it: it reads both counters, and over 20 means
   "both eyes the same image".

   The seams keep LE1's names: `src/halcyon/MevrSeams.h` has a note "Room left for one-Draw stereo" saying a second
   renderer that takes `g_oneDrawStereo`, `RecordPass1View` / `FindPass1View`, `StereoFrameActive` and the recorder's
   `Record*` calls moves those declarations there unchanged (capability `oneDraw`).
2. **`calcview_hook.cpp` `CalcViewDetour`, the SFR branch** (line 1432): for pass 0 (`!t_replay`), after
   `RefreshInverseSlots(svu)`, record the view pass 1 would have had (LE1's `RecordPass1View`): key = this view's final
   ViewOrigin (+0x320) and forward (the view matrix's third column: +0x90 elements 2, 6, 10); `delta` = pass 1's camera
   minus pass 0's = `2 × halfEye × right`, where `right` is the view matrix's column 0 (elements 0, 4, 8) **as
   `ApplyEyeOffset` read it, before `ApplyHeadLook`** (take it from `ApplyEyeOffset`'s own `wx, wy, wz` for pass 0:
   delta = −2 × (wx, wy, wz)); `mode` = 1 (symmetric: ME2 has no per-eye frustum, so `SynthesiseSecondView` keeps the
   projection and only moves the origin). `halfEye` must be the value this branch used, the swap and the cinema screen's
   override included. (`ApplyHeadPosition` moves both passes' origins alike and `ApplyHeadRotation` writes neither
   ViewOrigin nor PreViewTranslation, so the delta holds through `ApplyHeadLook`.)
3. **`d3d_capture.cpp`, the draw hooks**: the recorder's seams (LE1's `MevrBridge::RecordDrawIndexed`, `RecordDraw`,
   `RecordDrawIndexedInstanced`, `RecordDrawInstanced`, `RecordDrawAuto`, `RecordClearDepthStencilView`,
   `RecordUpdateSubresource`, one line at the start of each matching hook) and `NotePixelShaderCreated` in the
   CreatePixelShader hook (for the screen-space matrices, ledger R04). ME2's file has hooks for the draws and the depth
   clear; any of the seven it lacks must be added, or the recorder's own inline hooks must cover them.
4. **`d3d_capture.cpp` `SfrMaybeCapturePass0AtClear`** (line 1944): it takes the left eye at the scene-sized depth
   clear after the backbuffer was bound. With one Draw there is no second engine pass; the replayed tail issues that
   clear (it starts before the base pass), so the capture should fire inside the replay, as LE1's does ("the replay's
   pass-boundary clear takes pass 0"). **This is not verified** and is the main risk. ME2's rule is stricter than
   LE1's: a "pass opening" is two scene-sized depth clears at one backbuffer-draw count, and the copy is made at exactly
   the **second** opening of a present (`g_pass0Anchor`, `g_sfrClearRunLen`, `g_sfrPassOpens`). So the replay must
   re-issue both of the pass's opening clears, after pass 0 has drawn to the backbuffer. Whether both are issued after
   the tail mark (inside the base pass and later) or one is issued before it (then the replay has only one and the copy
   never fires) is not read. Measure first: with `[CLEARMAP]` diagnostics on, log where the tail mark falls among a
   pass's clears. If one clear is before the mark, either start ME2's tail earlier or give the capture a seam the
   replay calls ("pass 0 is complete: copy now") in place of the clear count. Then check with a snapshot pair that the
   left eye is pass 0's image and the right eye the replay's.

Then, on the host side (lane PL): `kViewStride` `0x11A0`; `StereoFrameActive()` = `DrawDetour`'s `sfrGameplay`
condition; the draws counted in ME2's draw hooks (`MevrSeams::PerfDraw()`, LE1's `++g_perfDraws`); the fallback (`g_oneDrawStereo` cleared after 3 frames without a
matching record) as LE1's. The right-eye view state `g_eyeState` simply goes unused while one-Draw runs.

Known limits carried over from LE1, to re-measure in ME2 rather than assume: whether ME2's shaders take
`ScreenToShadowMatrix` / `ScreenToWorld` (R04), the dynamic buffer rings' wrap inside the tail (R05), and
`LightEnvironmentShadows False` while the second eye is a replay (R11).

`sharePass` 1, 2 and 3 use the same rows plus the pass functions. LE1 found 1 hangs outside the resolve bracket and 3
saves little; port only 5. Their one ME2-specific fact: `ReplaySEH` arms commander +0x34 + 8i per view, the same offset.

#### 9. Scene captures. Addresses found; nothing uses them on ME2 yet.

1. Hook "scene capture's CaptureScene thunk" (`0x329B90`) and "FSceneRenderTargets::BeginRenderingBackBuffer"
   (`0x354570`), as `CaptureSceneHook` and `BeginBackBufferHook`. The capture loop is at the same place in `0x34BF70`.
2. `scene_capture_target::Bind`'s calls are right for ME2 (the target's vtable +0x8, RHI +0x520 and +0x340: the same
   three calls ME2's own view finish `0x338B60` makes). Before relying on it, read the one thing this lane did not:
   where the final pass's viewport comes from after ME2's `BeginRenderingBackBuffer` (LE1's sets it itself through RHI
   +0x340; ME2's does not, and its callers, the two post-process proxy renders `0x3A9FE0` and `0x3AAE40`, make no such
   call directly). `Bind` sets the capture's viewport itself; if a helper sets the back buffer's afterwards, a
   capture's final pass needs that corrected too. The check: a capture with a post-process chain whose picture fills
   its own target (LE1's symptom was a stretched picture in the backbuffer and an empty target, research/47).
3. ME3's capture work (`plugin/Le3SceneCapture`) hooks the same four places; ME2's counterparts of its rows are
   `0x329B90` (CaptureScene), `0x348BE0` (the inner render a capture calls), `0x354570`, `0x174C648`.

#### 10. Body bounds. Addresses found; port only if ME2 culls the player's body (ledger R29).

The whole port is two constants in `engine/Addresses.hpp`'s LE2 block: `kSkelUpdateBounds = 0x798350` and
`kPrimitiveUmbraBounds = 0x3146C0` (both are already rows of the table, checked at attach; the constants stay 0 so
nothing changes in ME2 until someone decides). `engine/BodyBounds` needs nothing else: it reads `Bounds` through
reflection and calls the hand-over with the component only. LE1's cause was Halcyon LE1's GPU-only view offset; first
look for the symptom in ME2 (look down with the head turned).

#### Not in this lane: the motion-blur guard (ledger R10)

Found, not added to the table (state U: the structure matches LE1's; the constructor and factory audit LE1 and ME3
each had, research/62 and research/le3-recenter-transition-failure.md, was not repeated; nothing installs the guard on
ME2, and `plugin/MotionBlurGuard.cpp` takes LE1's namespace for any game but LE3).

ME2's two proxy render bodies are `0x3A9FE0` (blur; LE1's `0x279360`) and `0x3AAE40` (no blur; LE1's `0x27A100`):
similarity 0.91, told apart by their order and their 40-byte size difference as in LE1, and by their vtables. Each is
reached through a `jmp` thunk in slot 1 of the proxy vtables (`0x3CE710` blur, `0x3CE720` no blur; LE1's `0x2BB0B0`,
`0x2BB0C0`). ME2 has LE1's five pairs, laid out the same way (in a pair slot 0 and slot 9 are the same function, slots
5 and 6 differ the same way, slots 7 and 8 are the shader selectors). The fingerprint is `MotionBlurGuard.cpp`'s
(FNV-1a over the ten slots as RVAs), computed from the exe's `.rdata`; the same computation reproduces LE1's ten
table values. Each vtable is referenced from one constructor site, with LE1's row patterns.

| Pair | Blur vtable | Sharp vtable | Blur fingerprint | Sharp fingerprint | Blur site | Sharp site |
|---|---|---|---|---|---|---|
| 1 | `0x10916F8` | `0x1091608` | `0xBFC71FCE68229650` | `0x52E76B8A421260FC` | `0x3ACA94` | `0x3AD6F4` |
| 2 | `0x1091748` | `0x1091658` | `0xCE1E0C8FBBC5C108` | `0x21030D6CD55FEDF1` | `0x3AC601` | `0x3AD261` |
| 3 | `0x1091798` | `0x10916A8` | `0x13C2B4294DA71338` | `0x50E1AB55D374BD01` | `0x3AC214` | `0x3ACE74` |
| 4 | `0x10931A8` | `0x1093108` | `0xA48F5C1ED1E104C0` | `0xAA81D23F6690FEA1` | `0x3B2387` | `0x3B2145` |
| 5 | `0x10931F8` | `0x1093158` | `0x2723B679797B8B86` | `0x21761A01E2EB112F` | `0x3B24A4` | `0x3B2262` |

Site pattern, offset 3: pairs 1-3 `48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3` (LE1's); pairs 4-5
`48 8D 05 ?? ?? ?? ?? 48 89 07 45 8B C5 B8 01 00`. To port: add `kMotionBlurProxyRender = 0x3A9FE0`,
`kNoBlurProxyRender = 0x3AAE40` and `kMotionBlurProxyPairs` to the LE2 block with twelve rows (the two functions'
first bytes are the same, so give those two rows no pattern or a longer one: ME2's dump test wants a function's pattern
unique), select `le2` in `MotionBlurGuard.cpp`, and install it in the LE2 host. Before that, the ledger's own next step:
a head-turn snapshot pair looking for the one-eye smear.

### Which levers ME2 can't have

None for want of an address. By design: `ue3QueryWait` and `perEyeOcclusion` have nothing to cure (per-eye view
states); `umbraEyeMatrix` has nothing to correct until each eye has its own frustum; `umbraSharedVisibility`,
`umbraShareResults` and the commander-1 experiment should stay unported for LE1's own reasons.
