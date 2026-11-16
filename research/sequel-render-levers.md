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

## ME3

Lane R3, 2026-11-16. `MassEffect3.exe` 2.0.0.48602, timestamp `0x60AEF0C4`. No game run: Ghidra over the decrypted
`.text` dump (`tools\ghidra\decompile.ps1 -Game LE3`) and byte comparison of the two dumps.

### Summary

- **ME3 runs Umbra**, the same library build as ME1's: `Umbra::Camera::resolveVisibility`, the commander's base
  class and the query-result read are byte for byte ME1's. BioWare's commander is the same class with the same
  fields. One difference: its `command()` returns at once while the global at `0x1767208` is zero (a switch ME1
  doesn't have; what sets it is not determined).
- **Every native ME1's levers hook or call has a counterpart in ME3, found and verified.** 31 of them are rows in
  ME3's `kTable` (plus two for body bounds, not used). Nothing is "not found".
- **The renderer's structures are ME1's** where the levers touch them, with three exceptions: the view's stride is
  `0x11B0` (ME1 `0x12F0`), ME3's scene render keeps the Umbra branch in a function of its own (`0x3685C0`) which two
  kinds of scene capture also call, and the visible-instance function is also called with its last argument 0.
- **What ME3's SFR changes:** Halcyon's ME3 renderer gives each of its two Draws its own `FSceneViewState`
  (`calcview_hook.cpp`'s SFR branch), where ME1's gameplay shares one. So the levers that exist because the two eyes
  share one state (`ue3QueryWait`, `perEyeOcclusion`) have nothing to remove on ME3, the kept-renderer modes
  (`sharePass` 1 and 2) can never pair, and ME3 probably has the Umbra query problem ME1 measured when it tried a
  state per eye ("Measure this first" below). One-Draw (`sharePass = 5`) is allowed by the design but needs three seams in
  Halcyon's ME3 code, named in its recipe.

### How each was found

Three kinds of evidence, used together:

1. **Call structure.** ME1's scene render `0x76EE20` was read next to the function ME3's capture loop was already
   known to sit in (`0x368850`, docs/galaxy-projector-handoff.md). The two bodies are the same statement for
   statement (the scope guard, RHI slot `+0x628`, the frame counter, show flags `0x33` and `8`, the capture loop,
   the render, the deletion), so each call in ME1's maps onto the call in the same place in ME3's. The same was done
   one level down: InitViews' tail (`0x768090` against `0x361630`), the resolve (`0x8BFBB0` against `0x3E5890`), the
   commander's `command()` switch (`0x8C34C0` against `0x3E8B80`).
2. **Masked bytes.** With rel32 targets and RIP-relative displacements masked, most of these functions are byte
   for byte ME1's, or differ only in a structure offset. The table says "identical" or names the bytes that differ.
3. **Vtables.** The commander's `command()` is slot 1 of its first vtable, which the resolve's constructor code
   writes (ME1 `0x11D62C0`, ME3 `0x1157F68`). The D3D11 RHI's vtable is `0x12FC7A0` (ME1 `0x120FF00`): 348 slots in
   both, the ones the levers use compared one by one.

Commands that reproduce it:

```powershell
tools\ghidra\decompile.ps1 -Game LE1 0x76EE20          # ME1's, to compare
tools\ghidra\decompile.ps1 -Game LE3 0x368850 0x3685C0 0x361630 0x3E5890 0x3E8B80 0x3E3650 0x3E3B00 0x3D8EB0
python -I tools/rva-patterns.py "$env:LOCALAPPDATA\MEVR\dump\MassEffect-60AEF0C4.text.bin" `
    --exe "$env:LOCALAPPDATA\MEVR\ghidra\bin\LE3-60AEF0C4\MassEffect3.exe" SceneRender=0x368850:function
cmake --build build --config Release --target mevr-tests; build\Release\mevr-tests.exe   # the table against the dump
```

The names are in `tools/ghidra/symbols-le3.csv` (`tools\ghidra\seed.ps1 -Game LE3` applies them).

### The table

"Row" is the entry's name in `engine::addresses::le3::kTable` (the same text as ME1's row). Each row's byte pattern
is there and only there (the exe gate and the dump test read it from the table; `tools/rva-patterns.py` made it).
Every function row is a `.pdata` start (checked with `--exe`) unless marked leaf; every pattern is unique in ME3's
`.text`; ctest `mevr-tests` checks all of them against the dump. Structure offsets are in "Layouts" below. Status: **V** found and verified (the function, its signature and
every offset the hook uses were read in ME3's code), **U** found, an offset or a premise unverified.

| Lever | Row | ME1 | ME3 | Verified how | Status | With ME3's SFR |
|---|---|---|---|---|---|---|
| accounting, every index-keyed lever | FSceneRenderer::Render | `0x76EE20` | `0x368850` | call structure; `void(renderer)`; called from `0x35F090` and the thunk `0x360EA0`, as ME1's from `0x7655A0` and `0x767900` | V | Allowed. Two renders a present (index 0 = pass 0 = left). |
| one-Draw, sharePass | FSceneRenderer deleting destructor | `0x4ED700` | `0x35E6E0` | the scene render's last call; four callers as ME1's; frees `0x110` bytes when `flags & 1`; ME3's has the destructor inlined (364 bytes against 52) | V | Allowed. |
| sharePass 1 | FSceneRenderTargets::Allocate | `0x772EB0` | `0x36CDC0` | called as `(targets, sizeX, sizeY, 0)` with the sizes from the family target's vtable `+0x18`/`+0x20` | V | See sharePass 1. |
| sharePass 1 | GSceneRenderTargets | `0x167D0E0` | `0x1764A20` | Allocate's first argument | V | |
| sharePass 1, 2 | SetUmbraCommander / wireframe state | `0x8C1AE0` | `0x3E84E0` | leaf, same bytes; called with 1 before the capture loop and 0 after; writes `0x189EBF8` | V | Allowed. |
| replay tail (sharePass 2, 3, 5) | FSceneRenderer::RenderDpgLightTargets | `0x77BE00` | `0x38A450` | InitViews' tail calls it `(renderer, 1)` between the resolve and the base pass; the foreground pass calls it too | V | Allowed; see difference 2. |
| sharePass 3 | RenderUmbraBasePass | `0x8BF8F0` | `0x3E55C0` | InitViews' last call; per view it sets commander `+0x64`/`+0x68` and runs `0x3E4550` (ME1 `0x8BEDB0`, 925 bytes both) | V | Allowed. |
| sharePass 1 | translucency check | `0x7666A0` | `0x3600A0` | same bytes but the stride (`0x11B0`) and one view field | V | |
| sharePass 3 | before-lights pass | `0x759240` | `0x351020` | 2,942 bytes both, 22 bytes differ (structure offsets) | V | Allowed. |
| sharePass 3 | DPG pass | `0x76D650` | `0x366F20` | 451 bytes, identical; `(renderer, dpg, uint* dirty)` | V | Allowed. |
| sharePass 3 | DPG lights | `0x76D0A0` | `0x366960` | `(renderer, dpg, translucency, uint* dirty, flag)`: the fifth on the stack | V | Allowed. |
| sharePass 3 | foreground pass | `0x76CA70` | `0x366280` | `(renderer, 2, int* out, uint* dirty, flag)` | V | Allowed. |
| one-Draw, sharePass 3 | scene render finish | `0x76DD10` | `0x3675D0` | `(renderer, translucency, dirty)`; called from the Umbra branch and from `0x365370` | V | Allowed. |
| sharePass 1 | scene frame counter | `0x178779C` | `0x1898678` | incremented right after RHI `+0x628` | V | |
| sharePass 1 | depth dirty flags | `0x1795D38` | `0x189EC04` | read into the local the passes get as `dirty` | V | |
| sharePass 1 | GSkipBeforeLights | `0x167D49C` | `0x1764E18` | the before-lights pass runs only while it is 0 | V | |
| sharePass 1 | Umbra commanders | `0x1798E88` | `0x189ECC8` | two pointers, made by the resolve on first use | V | |
| sharePass 1 | Umbra commander index | `0x1795D28` | `0x189EBF8` | indexes the commanders everywhere | V | |
| eligibility (`ShowFlag`) | show flags' bits per word | `0x164C720` | `0x1756A20` | the divisor of every show-flag test | V | |
| resolve timing, umbraQueryWait | UmbraCommander::command | `0x8C34C0` | `0x3E8B80` | slot 1 of the commander's vtable; the same ten codes; `void(commander, code)` | V | Allowed. |
| umbraQueryWait | Umbra resolve start stamp | `0x1795D40` | `0x189EC0C` | code 0 stamps it (a float, the same expression), code 1 zeroes it, the results read (`0x3D8C90`) reads it | V | Allowed. |
| ue3QueryWait | FD3D11DynamicRHI::GetQueryData | `0x9A04B0` | `0x9A7D10` | 351 bytes, identical; called from RHI slot `+0x268` (`0x9A7C50`) | V | Allowed, but see "What ME3's SFR design allows". |
| keepBoundShaderStates | FD3D11BoundShaderState constructor | `0x9A89F0` | `0x9B0270` | 557 bytes, identical; its two callers (`0x9B24C0`, `0x9B25E0`) are identical to ME1's too | V | Allowed (no stereo in it). |
| keepBoundShaderStates, replay | GDynamicRHI | `0x17938C8` | `0x1894018` | the capture work's row (it was `Capture.GDynamicRHI`), renamed to ME1's; the same vtable layout | V | |
| minScreenRadiusPx, umbraSharedVisibility | UmbraCommander visible instance | `0x8BE0B0` | `0x3E3B00` | 909 bytes both, 3 bytes differ; the same seven arguments | V | Allowed; see difference 1. |
| umbraEyeMatrix | UmbraCommander::IssueOcclusionQueries | `0x8B6550` | `0x3D8EB0` | 559 bytes, identical; code 5 calls it | V | Allowed; a no-op until the eyes get their own frustum. |
| umbraShareResults | Umbra read of one query result | `0xD48380` | `0xE02430` | leaf, identical, as is its one caller `0xE024D0`; `(context, head, count, wait)` | V | Allowed; its premise holds (one Umbra camera for both passes, below). |
| captures | scene capture's CaptureScene thunk | `0x742C80` | `0x344540` | the capture work's row (it was `CaptureScene`), renamed to ME1's | V | In use (`plugin/Le3SceneCapture`). |
| captures | FSceneRenderTargets::BeginRenderingBackBuffer | `0x773D30` | `0x36D760` | the capture work's row (it was `Capture.BeginBackbuffer`), renamed to ME1's | V | In use. |
| UI world figure | scene capture's Umbra camera thunk | `0x7526C0` | `0x34D750` | leaf, same bytes but the vtable slot it jumps through (`+0x48`, ME1 `+0x40`); called once, from `0x35F090` | V | Not a lever. |
| body bounds | USkeletalMeshComponent::UpdateBounds | `0x58EEC0` | `0x793850` | same bytes but two component offsets; ends with the call below | V | Not used on ME3 (group 3). |
| body bounds | UPrimitiveComponent::UpdateUmbraBounds | `0x25CFE0` | `0x32EC10` | same bytes but the Umbra object's offset and a vtable slot | V | Not used on ME3 (group 3). |

Not rows (nothing hooks or calls them, but they carry the evidence): ME3's Umbra branch `0x3685C0` and the render
without it `0x365370` (rows `Capture.RenderUmbra`, `Capture.Render`), InitViews `0x361630`, the resolve `0x3E5890`
(row `FSceneRenderer::UmbraResolve`, added for the record), the base pass of one view `0x365940` (ME1 `0x76C1D0`),
`FSceneView`'s copy constructor `0x35CBB0` (ME1 `0x763450`), UpdatePrimitiveOcclusion `0x35BE20` (ME1 `0x762550`),
`Umbra::Camera::resolveVisibility` `0xDFB650` (ME1 `0xD41240`), RHICreateBoundShaderState `0x9B24C0` (ME1
`0x9AB3B0`), the RHI's AddRef for bound shader states `0x2CB940` (ME1 `0x215B10`).

### Layouts

For `RenderLeversTable.hpp`. "Same" means the value was read in ME3's code, not assumed.

| Layout | ME1 | ME3 | Read in |
|---|---|---|---|
| FSceneRenderer: scene, family copy, family target, show flags (12 bytes), Umbra path, views, view count, scene begun | `+0x00`, `+0x08`, `+0x18`, `+0x28`, `+0x5C`, `+0x60`, `+0x68`, `+0xD8` | same | `0x368850`, `0x3685C0`, `0x361630`, `0x3E5890` |
| FSceneRenderer size | `0x110` | same | `0x35E6E0` |
| FSceneRenderer fields past `+0xD8` | | **4 bytes later** (`+0xF4` to `+0xF8`, `+0xF8` to `+0xFC`) | `0x3E5890`, `0x361630`; no lever reads them |
| family target: surface, size X, size Y (vtable) | `+0x8`, `+0x18`, `+0x20` | same | `0x3685C0`; `plugin/SceneCaptureTarget.hpp` already uses them on ME3 |
| **FViewInfo stride** | `0x12F0` | **`0x11B0`** | `0x3E5890`, `0x3E55C0`, `0x3600A0` |
| FSceneView: state, view matrix, projection, translated view, translated view-projection, its inverse, pre-view translation, view-projection, view origin, Umbra camera | `+0x8`, `+0x90`, `+0xD0`, `+0x190`, `+0x1D0`, `+0x210`, `+0x250`, `+0x260`, `+0x320`, `+0x4D4` | same | the copy constructor `0x35CBB0` copies the same fields in the same order up to `+0x4DC` (ME1's copies one more int at `+0x4E0`); Halcyon's ME3 code edits the same offsets in play (`kFsv...` in `calcview_hook.cpp`, its inverse scan at `+0x210`); `+0x260` and `+0x320` go to RHI slot `+0x4C0` in `0x3D8EB0`, `+0x1D0` and `+0x320` in `0x3E4550`; `+0x4D4` in `0x3E5890`; `+0x8` in `0x3E3B00` |
| the view's plain ranges (`kViewPlainRanges`, `kViewCaptureRanges`), compare block `+0x60..+0x8C` | `{0x8C,0x140} {0x190,0x330} {0x480,0x4B0} {0x4C0,0x4D0}` | same | the copy constructor: the same owned containers at `+0x44`, `+0x140`, `+0x330` |
| FSceneViewState occlusion history (a TSet, `0x48` bytes, count at `+8`) | `+0xD10` | same | `0x35BE20` (1,849 bytes both; it differs from ME1's only in the record's flags offset) |
| commander: context, base-pass flags per view, view params set, view index, view, family, scene, renderer, query count | `+0x8`, `+0x34 + 8i` (and `+0x38 + 8i`), `+0x5C`, `+0x64`, `+0x68`, `+0x70`, `+0x78`, `+0x8C`, `+0x94` | same | `0x3E5890`, `0x3E4550`, `0x3D8EB0`, `0x3E3B00` |
| commander size | `0xA4` | same | `0x3E5890` |
| visible-instance record: primitive scene info, origin, extent, **radius**, min and max draw distance | `+0x0`, `+0x20`, `+0x2C`, `+0x38`, `+0x3C`, `+0x40` | same | `0x3E3650` (origin and extent), `0x3E3B00` (the distances) |
| visible-instance record: flags | `+0x48` | **`+0x54`** | `0x3E3B00`, `0x35BE20`; no lever reads it |
| Umbra context ring: count, head, entries (0x19 bytes, result at `+9`) | `+0x1C8`, `+0x1CC`, `+0x2168` | same | identical library code (`0xE02430`, `0xE024D0`, `0x3D8C90`) |
| Umbra camera: impl, parameter word | `+0x10`, impl `+0x420` | same | identical library code (`0xDFB650`) |
| RHI vtable: begin scene, end scene, set render target, set viewport, view parameters, create bound shader state, AddRef bound shader state | `+0x628`, `+0x620`, `+0x520`, `+0x340`, `+0x4C0`, `+0xA0`, `+0x8A0` | same | vtable `0x12FC7A0` against ME1's `0x120FF00`, slot by slot |
| bound shader state: handle in the object, reference count | `+0xC`, `+0x8` | same | `0x2CB940`: it increments the int 4 bytes before the handle; `0x9B24C0` is byte for byte ME1's |
| capture list in the scene | `+0x3068`, `+0x3078` | `+0x4668`, `+0x4678` | `0x368850`; no lever reads it |
| USkeletalMeshComponent: Bounds; the Umbra object | by reflection; `+0x210` | by reflection (`+0x8C`); `+0x214` | `0x793850`, `0x32EC10` |

Function signatures are ME1's in every row (the argument lists in the table above are ME3's call sites).

### What differs from ME1, and what to do about it

1. **The visible-instance function is also called to keep books, not to show.** ME1's `command()` builds each
   instance's record and distance inline and calls `0x8BE0B0` once, with `(..., 0, 1)`. ME3's calls a wrapper
   (`0x3E3650`: the record from a table, the distance, and a "parent" primitive for some instances) which calls
   `0x3E3B00` in five places, some with the last argument 0 (the function then marks nothing visible unless its
   first check says so) and some with the sixth 1. Two more functions call it (`0x3E33E0`, `0x3E3590`). **A size
   cull on ME3 must only return 0 early when `a7 != 0 && a6 == 0`**, ME1's only call shape; every other call goes
   to the original untouched. The wrapper, on a 0 from the first call, returns before the parent's bookkeeping,
   which is what "not visible" means there.
2. **A capture can take the Umbra branch.** ME1's four capture kinds all call the render without Umbra
   (`0x76BC00`). ME3's `0x6F0EE0` and `0x6F3320` choose: a flag of theirs calls `0x3685C0`, which runs InitViews
   on the Umbra path, so the commander's `command()`, `RenderDpgLightTargets(renderer, 1)`, the base pass and the
   finish all run for a capture, inside the eye's render (the capture loop is at the top of `0x368850`). The
   hooks already carry the capture depth (`t_captureDepth`, `t_renderIndex = -1`); what has to be added on ME3 is
   **`MarkTail()` only when `t_captureDepth == 0`** in the light-targets hook, or a capture marks the replay's tail
   before the eye's own render begins.
3. **`0x3685C0` can still take the render without Umbra.** Show flags `0x47`, `0x15` and `0x1B` send it to
   `0x365370` even with renderer `+0x5C` set. `Eligible()` (ME1: `+0x5C != 0`, 1 to 4 views, not show flag `0x33`)
   should add those three flags on ME3, or a render it calls eligible never reaches the finish hook's Umbra path.
4. **`command()` has a gate.** While the dword at `0x1767208` is 0, ME3's `command()` does nothing for any code.
   Log it once from the command hook (`render levers: Umbra's command gate is N`): if it is 0 in play, none of the
   Umbra levers has anything to act on. It can't be a table row (the only reference is a compare with an
   immediate after the displacement, which the table's Data kind can't express).
5. **Each SFR pass has its own view state.** See the next section.

### What ME3's SFR design allows

Halcyon's ME3 renderer (`src/halcyon/le3`) renders stereo as two `FViewportClient::Draw` calls a present:
`DrawDetour` (`calcview_hook.cpp`) runs the game's Draw (pass 0, the left eye), then calls it again (pass 1,
`t_replay = true`). Its CalcSceneView branch moves each pass's camera by half the eye distance and **gives each
pass a view state of its own** (`g_eyeStateLeft`, `g_eyeState`, set with `SetViewState`), so temporal effects
don't smear. Pass 0's eye (the left, unless its swap-eyes setting is on) is a copy of the backbuffer taken on the render thread at pass 1's first scene clear
(`d3d_capture.cpp`, `SfrMaybeCapturePass0AtClear` into `g_sfrPass0Tex`); the right eye is the backbuffer at
Present; UI draws are mirrored into the held left eye (`SfrMirrorUiDrawIntoPass0`).

| Lever | Verdict on ME3 |
|---|---|
| accounting, resolve timing (`render levers:` line) | **Yes.** Index 0 is pass 0, index 1 pass 1. Needs the bridge's "stereo renders the world" answer (below). |
| `keepBoundShaderStates` | **Yes**, unchanged. The cheapest to port; sample the render thread for `CreateInputLayout` first (ledger R08). |
| `minScreenRadiusPx` | **Yes**, with difference 1. |
| `umbraQueryWait` | **Yes.** The stamp and the results read are ME1's code. |
| `umbraEyeMatrix` | **Yes**, and harmless: it acts only when the view's `+0x1D0` and `+0x260` disagree, which on ME3 happens once metric stereo's per-eye frustum is ported (ledger group 2). Until then it counts `same`. |
| `ue3QueryWait` | **Nothing to gain, by design.** ME1's wait is the second eye asking for "last frame's" occlusion query in a state both eyes share, which is the first eye's of this frame. With a state per pass each eye asks for its own previous frame's query. The hook is safe; measure `reads that asked to wait` in slot 1 before believing either way. |
| `perEyeOcclusion` | **Not applicable**: ME3's SFR already has what the lever builds (a history per eye). `BeginEyeHistory` stands down by itself when the second render's state isn't the first's (`gHistorySkipped`). |
| `sharePass = 1`, `2` (the kept renderer) | **Can't have, as ME3's SFR is.** `Compatible()` needs both renders to go through the same view state (view `+0x8`); ME3's never do. Not wanted either: 1 hung ME1's game thread in Umbra, 2 was superseded by 5. |
| `sharePass = 3` (pass functions skipped, the tail replayed) | **Allowed**: every pass function is found. Superseded by 5 on ME1; port only as a stepping stone. |
| `sharePass = 5`, one-Draw | **Allowed, with three seams in Halcyon's ME3 code** (recipe 8). Nothing in the natives forbids it: research/le3-native-renderer-audit.md found no one-Draw-per-frame rejection either. Not run. |
| `umbraShareResults`, `umbraSharedVisibility`, `umbraSecondEyeClearBits`, `flushFirstEye` | **Allowed.** Their premise holds: ME3's `CalcSceneView` (`0x6CFFD0`) takes the Umbra camera from the local player (`+0x474`, made on first use) and writes it to the view's `+0x4D4`, the same two offsets as ME1's, so both passes resolve through ONE camera whatever state Halcyon sets afterwards. All four are off or "never set it" on ME1; don't port them before the others are measured. |
| body-bounds padding | Found; **group 3**: port only if ME3 culls the body under the eye. `kSkelUpdateBounds` stays 0. |

### Measure this first: a state per eye and Umbra's queries

ME1 tried what ME3's SFR does. research/35 attempts 17 to 21 (`perEyeViewState = 1`: Halcyon's per-eye view states
in gameplay) found: the second eye's wait in UE3's own occlusion went away, but **the second eye's Umbra queries
were never ready** (7,378 `GetQueryData` reads a frame, 7,202 without a result; the results read, command code 6,
polling for its 10 ms budget: 1.0 ms, 6 ms at worst) and one frame in ten drew 2.4 times the meshes. The cause
read then: both eyes keep one Umbra camera and one commander, but the commander's occluder queries are allocated
from and read back through the *current view's state* (its query pool at state `+0x88`), so with a state per eye
the second eye reads, by index, queries that live in the other state's pool.

Every piece of that is the same code on ME3 (the commander, the results read `0x3D8C90`, the camera from the local
player), and ME3's SFR always runs a state per pass. So ME3 very likely pays this today. It costs nothing to find
out once recipes 1 to 3 are in: the levers line's per-slot numbers say it.

- `query reads` for slot 1 in the thousands with most "without a result", and slot 1's `query results` part near
  1 ms or more: ME3 has ME1's attempt-18 pathology.
- Then the choices are ME1's: `umbraQueryWait = 0` (attempt 19: the spin goes, the second eye draws unoccluded:
  2,927 draws against 1,561), or one state for both passes in gameplay as ME1 ships (which brings back the wait
  `ue3QueryWait` and `perEyeOcclusion` exist for, and needs the motion-blur guard ME3 already has), or one-Draw,
  which has no second resolve at all.

This is a measurement to make, not a finding: nothing was run.

### Recipes

Every recipe assumes lane PL's per-game table: an address comes from ME3's row of the given name, and a missing or
moved row stands the lever down with one log line, as on ME1.

**0. What the renderer must answer first.** `RenderLevers.cpp` asks Halcyon's LE1 bridge four things; ME3's
renderer has each fact. The names to give them are the one seam vocabulary's (`src/halcyon/MevrSeams.h`, section 6
and its "Room left for one-Draw stereo" note; capabilities `perfSeams` and `oneDraw` in `plugin/Renderer.hpp`):

1. `StereoFrameActive()`: "SFR renders the world this frame". ME3: `DrawDetour`'s `sfrGameplay` condition (VR on,
   mode `Sfr`, not menu or cinematic unless `ConvoFp::OwnsCamera()`).
2. The draw counter (`PerfDraw()`; LE1's `++g_perfDraws`): ME3 calls it in `MhDrawIndexed`,
   `MhDrawIndexedInstanced` and `MhDraw` (`d3d_capture.cpp`).
3. The frame boundary (`gRenderIndex` back to 0; `PerfPresentBegin` / `PerfPresentEnd`): ME3's Present hook, where
   `g_sfrCapturedThisPresent` is cleared.
4. For one-Draw only: `g_oneDrawStereo`, `RecordPass1View` / `FindPass1View` and the recorder's `Record*`, moved
   into `MevrSeams.h` unchanged as its note says (recipe 8).

**1. Accounting: the scene render hook.**

1. Hook the row `FSceneRenderer::Render` (`0x368850`) with ME1's `SceneRenderHook`, `void(void* renderer)`.
2. Read the family target with `scene_capture_target::Read(renderer)` (renderer `+0x18`, vtable `+0x18`/`+0x20`):
   the same on ME3.
3. `ViewStateOf`: renderer `+0x60` to first view, `+0x8`. The same on ME3.
4. `Eligible()`: add show flags `0x47`, `0x15`, `0x1B` (difference 3). The bits-per-word global is the row `show
   flags' bits per word`.
5. Check: `render levers:` logs 2 renders a frame in gameplay, 1 in a menu; the eye target learned is the
   backbuffer's size.

**2. Resolve timing and `umbraQueryWait`.**

1. Hook the row `UmbraCommander::command` (`0x3E8B80`), `void(void* commander, unsigned code)`, with ME1's
   `CommandHook`. Codes: 0 begin (stamps the start), 3 instances, 5 occluder flush and queries, 6 results read, 1 end.
2. `umbraQueryWait = 0`: after the original returns from code 0, subtract 4.0 from the float at the row `Umbra
   resolve start stamp` (`0x189EC0C`).
3. Log the gate once (difference 4): the dword at exe `+0x1767208`.
4. Check: the levers line's Umbra parts are non-zero in gameplay.

**3. `ue3QueryWait`.** Hook the row `FD3D11DynamicRHI::GetQueryData` (`0x9A7D10`), `int(rhi, query, out, size,
wait)`, with ME1's hook. Read `reads that asked to wait` for slot 1 before turning the lever on: expected near 0.

**4. `keepBoundShaderStates`.**

1. Hook the row `FD3D11BoundShaderState constructor` (`0x9B0270`): nine arguments, ME1's `tBoundShaderStateCtor`.
2. On a repeated key, AddRef through the RHI: `GDynamicRHI` (row, `0x1894018`) to vtable `+0x8A0`, called with
   `(rhi, object + 0xC)`. The reference count is the int at object `+0x8`.
3. Check: `render levers: bound shader states kept: the RHI's AddRef is at RVA 0x2CB940; the int at +8 went N ->
   N+1`. Any other RVA: stop, the slot is wrong.

**5. `minScreenRadiusPx`.**

1. Hook the row `UmbraCommander visible instance` (`0x3E3B00`), ME1's seven-argument signature.
2. View: commander `+0x68`; projection `[1][1]`: view `+0xD0 + 0x14`; radius: record `+0x38`.
3. **Cull only when `a7 != 0 && a6 == 0`** (difference 1). Count every call in the size bins as now.
4. Check: `screen-size cull sees the view's projection [1][1] = ...` with a value between 0.5 and 2; the bins fill.

**6. `umbraEyeMatrix`.** Hook the row `UmbraCommander::IssueOcclusionQueries` (`0x3D8EB0`), `void(commander)`,
with ME1's hook unchanged: commander `+0x68` view, `+0x5C` set flag, view `+0x90`, `+0x190`, `+0x1D0`, `+0x250`,
`+0x260`. Check: `the view matrix is the translated one moved by minus the pre-view translation` (or plus), then
`same` counts until the per-eye frustum exists.

**7. Captures.** Already done for ME3 by `plugin/Le3SceneCapture` (another session's): the rows `scene capture's
CaptureScene thunk` and `FSceneRenderTargets::BeginRenderingBackBuffer` are the rows its hooks already use (found by RVA there), so
PL's shared code can take over those hooks later. Until then **don't install both**: two hooks on `0x344540` and
`0x36D760` would each bind the target.

**8. One-Draw (`sharePass = 5`).** The last to port. The host side is ME1's; the work is in Halcyon's ME3 code.

1. Host hooks, all ME1's functions on ME3's rows: `FSceneRenderer::Render`, `FSceneRenderer::RenderDpgLightTargets`
   (the tail mark; with difference 2's guard), `scene render finish` (the replay runs in it, after the original,
   while the renderer's views are alive), and for captures and `sharePass = 3` the rows `RenderUmbraBasePass`,
   `before-lights pass`, `DPG pass`, `DPG lights`, `foreground pass`, `FSceneRenderer deleting destructor`.
2. `kViewStride = 0x11B0`. The plain and capture ranges, `CapturedOffset` and every matrix offset in
   `SynthesiseSecondView` are ME1's.
3. The recorder (`plugin/DrawReplay`) hears draws through seams in Halcyon's own d3d hooks. ME3's Halcyon hooks
   fewer than ME1's: add `Record*` calls in `MhDrawIndexed`, `MhDrawIndexedInstanced`, `MhDraw` and
   `MhClearDepthStencilView` (`src/halcyon/le3/d3d_capture.cpp`); `DrawInstanced`, `DrawAuto` and
   `UpdateSubresource` have no Halcyon hook on ME3, so the recorder hooks those itself (inline, per variant, as it
   does for the entries Halcyon never hooked on ME1).
4. **Halcyon code that stands down** (three `[MEVR]` seams):
   - `DrawDetour` in `calcview_hook.cpp`: after pass 0, skip the second Draw (`t_replay = true;
     SfrReplayDrawSEH(...)`) while the host's one-Draw flag is set (ME1: `g_oneDrawStereo` in `render_hook.cpp`'s
     replay decision). `g_sfrReplays` then stops counting; `[SFRTIMING]` and `[SFRNATIVE]` only observe.
   - The SFR branch of `CalcViewDetour` in the same file (`const float signedUU = t_replay ? +he : -he;
     ApplyEyeOffset; ApplyHeadLook; ApplyFov; RefreshInverseSlots`): in pass 0, also compute what pass 1's view
     would be (`+he`) and hand it to the host (ME1: `MevrBridge::RecordPass1View`, read back by `FindPass1View`
     from the rendered view's origin and forward). Pass 1's view state (`g_eyeState`) is then never rendered
     through; leave it allocated for the fallback. Whatever else is taken once per pass there (the per-eye pose
     tag of research/sequel-eye-pose-tags.md) has to be taken for the right eye from pass 0 as well.
   - `SfrMaybeCapturePass0AtClear` in `d3d_capture.cpp`: it takes the left eye once a present, at the first
     scene-sized clear after pass 0's first draw into the backbuffer (`g_sfrBbDrawnThisPresent`), which is pass
     1 starting. With one Draw pass 1 never starts, and a later scene-sized clear of the same Draw (its comments
     say such clears follow a composite) would take the backbuffer after the replay: both eyes the right eye. Add a call the host
     makes on the render thread **between the first eye's finish and the replay** (ME1's `PerfPass0Captured`,
     the vocabulary's `PerfFirstEyeCaptured`, sits at the same moment) that copies the backbuffer into `g_sfrPass0Tex` and sets what the clear path sets
     (`g_sfrCapturedThisPresent`; `g_sfrPass0Caps`, the sequence `me2_xr.cpp`'s submit compares to tell a fresh
     left eye from a frozen one; `g_sfrClearsThisPresent >= 1` so the UI mirror arms). The replay then draws the
     right eye into the backbuffer; the UI draws that follow go to the backbuffer and are mirrored into the left
     eye as now. The submit (`GetSfrPass0Texture()` left, the backbuffer right) doesn't change.
5. The fallback is ME1's: when no recorded pass-1 view matches for 3 frames, clear the flag so `DrawDetour` draws
   twice again.
6. Checks, in order, on the fake headset: `render levers:` shows 1 scene render a frame and `one Draw` replays;
   a snapshot's left and right eyes differ by parallax only; `tools/eye-diff.ps1` first (it says whether the
   substitution covers the eye's constants on ME3); then research/37's hazards (the dynamic buffers' discard, the
   screen-space matrices) through the levers line's counters.

**9. `sharePass = 1` (ReplaySEH), for the record only.** ME3's order, from `0x3685C0`: RHI `+0x628`; frame counter
`+1`; renderer `+0xD8 = 1`; `SetUmbraCommander(0)`; Allocate(targets, sizeX, sizeY, 0); light targets
`(renderer, 1)`; re-arm commander `+0x34 + 8i`; base pass; translucency check; `dirty` = the depth dirty flags;
before-lights if GSkipBeforeLights is 0; DPG pass 1; DPG lights `(1, translucency, &dirty, 1)`; renderer `+0x5C =
0`; foreground `(2, &out, &dirty, 0)`; DPG pass 2; DPG lights `(2, translucency, &dirty, 0)`; renderer `+0x5C = 1`;
finish `(translucency, dirty)`; RHI `+0x620`. Identical to ME1's. It can't pair on ME3 (above).

**10. Body bounds, if the bug shows.** Set `le3::kSkelUpdateBounds = 0x793850` and `le3::kPrimitiveUmbraBounds =
0x32EC10` (today `kSkelUpdateBoundsNative`, `kPrimitiveUmbraBoundsNative`); `engine/BodyBounds` needs nothing else
(it finds `Bounds` by reflection and only calls the second function).

### Open

- Whether ME3's second pass polls unready Umbra queries, as ME1's did with a state per eye ("Measure this first").
- What sets the command gate `0x1767208`. It is the dword right after three Umbra switches, and nothing in
  `.text` writes it by its own address. ME3's console has ME1's `UMBRA <word>` command (the viewport client's Exec
  `0x6D4670` holds the table): `CULLING` toggles the byte at `0x1767204` (the one `FViewportClient::Draw` and the
  skeletal mesh's UpdateBounds read), `OCCLUSIONCULLING` `0x1767205`, `HOAX` `0x1767206`, `OCCLUSIONQUERIES`
  `0x189EC02`, `OBJECTBOUNDS` `0x189EC00`, `OBJECTOBBS` `0x189EC01`, `MISC` `0x189EC03`, `VOXELS` `0x189EBEC`,
  `CAMERAFRUSTUM` `0x189EBED`, `FREEZECAMERA` `0x189EBEE`, `FREEZEOCCLUSION` `0x189EBEF`. None of them is the gate.
  Those switches are the cheapest A/B on ME3 (`UMBRA OCCLUSIONCULLING` through the console request) before any
  lever exists. The `.data` section is not readable from the exe on disk, so the gate's start value is unknown.
- The exe gate's view of the three renamed capture rows: `plugin/Le3SceneCapture` asks for them by RVA
  (`exe::Find`), so the names don't matter to it; nothing else read the old names.
- Nothing here was run in the game. The first run with any hook installed should confirm the exe gate accepts all
  rows (`exe:` lines in `LE3MEVR.log`) before a lever is turned on.

## Shared module

### What is shared, and where the per-game part lives

| module | shared file (all three hosts) | per-game part |
|---|---|---|
| frame-timing probe | `src/plugin/PerfProbe.cpp`, `PerfProbe.hpp`, `src/core/vr/FrameProbe.*`, `tools/perf-probe.ps1`, `tools/perf-probe.py` | `src/plugin/PerfProbeHost.hpp` (the adapter's interface), `PerfProbeLe1.cpp` (Halcyon ME1's seams and settings), `PerfProbeSequel.cpp` (both sequels) |
| render levers | `src/plugin/RenderLevers.cpp`, `RenderLevers.hpp` | `src/plugin/RenderLeversTable.hpp` (addresses by row name, layouts), `RenderLeversSeams.hpp` (what the renderer answers) |
| draw recorder and replay | `src/plugin/DrawReplay.cpp` (D3D11 only, no engine address), `core/d3d11/ShaderReflect`, `core/vr/ScreenMatrix` | none; it is fed by the renderer's `Record*` seams, which only Halcyon ME1 has |
| A/B tools | `tools/perf-ab.ps1 -Game`, `tools/measure-levers.ps1 -Game` | the game's `ASI\<GAME>MEVR.ini` and process name, from `-Game` |

CMake: LE1's list is `MEVR_HALCYON_HOST_SOURCES`; the sequels take `MEVR_SEQUEL_RENDER_PERF_SOURCES` (the same files
with `PerfProbeSequel.cpp` in place of `PerfProbeLe1.cpp`). Every host with a Halcyon renderer is compiled with
`MEVR_WITH_PERF_PROBE`; `plugin/Entry.cpp` marks the game thread's tick for the probe, and on the sequels calls
`perf::Tick()`, `render_levers::Tick()` each tick and `render_levers::Install()` at attach (LE1 did all three already).

**LE1 is unchanged.** Its constants moved from `RenderLevers.cpp` into `RenderLeversTable.hpp` and are pinned there by
`static_assert`s against the old literals, so the same numbers are compiled in; its seam calls are the same names
(`namespace seam = MELEVR::MevrBridge`); its probe adapter is its old code moved to `PerfProbeLe1.cpp`.

### The address contract: rows by name

`plugin/RenderLeversTable.hpp` finds each address **by row name** in the current game's `kTable` of
`src/engine/Addresses.hpp`, at compile time (`table::Row("FSceneRenderer::Render")`). LE1's rows have had these names
since research/34 stage 4. **A sequel gets a lever by adding a row with exactly LE1's name to its own `kTable`**, with a
byte pattern (`tools/rva-patterns.py` over that game's `.text` dump), so the exe gate checks it at attach. No named
`k...` constant is needed, and nothing in `plugin/` changes for a new address.

A game without the row reads 0. At attach `render_levers::Install` checks, per lever, that every row it needs exists and
(where the exe check ran) is at its RVA, and that the lever's layout group is verified (below). If not, the lever
**stands down with one log line** and its setting stays the game's own:

```
render levers: keepBoundShaderStates stands down on LE2: no row 'FD3D11BoundShaderState constructor', no row 'GDynamicRHI'; its layout isn't verified for this game
```

ctest `mevr-render-levers-table` (`tests/RenderLeversTableTests.cpp`) checks that every name the module asks for is a row
of LE1's table, that no table has two rows of one name, and prints what each sequel's table can already serve. Run it
after adding rows: a misspelt name shows there as `[no '...']`.

| lever (`[Performance]` key) | rows it needs (exact names) | layout group (`Layout` flag) |
|---|---|---|
| accounting: scene renders per frame, ms each (no key) | `FSceneRenderer::Render` | `sceneRendererVerified`: the family target (+0x18, its vtable's size getters), the views (+0x60, +0x68) |
| the Umbra resolve's timing, `umbraQueryWait` | `UmbraCommander::command`, `Umbra resolve start stamp` (data) | `umbraCommanderVerified` |
| `ue3QueryWait`, the query reads' count | `FD3D11DynamicRHI::GetQueryData` | `umbraCommanderVerified` |
| `keepBoundShaderStates` | `FD3D11BoundShaderState constructor`, `GDynamicRHI` (data) | `boundShaderStateVerified`: the RHI vtable slot of AddRef for a bound shader state (LE1 0x8A0), the handle's offset in the constructed object (LE1 +0xC) |
| `minScreenRadiusPx` | `UmbraCommander visible instance` | `umbraInstanceVerified`: record radius (+0x38), the commander's view (+0x68), the view's projection matrix (+0xD0) |
| `umbraEyeMatrix` | `UmbraCommander::IssueOcclusionQueries` | `umbraQueriesVerified`: the commander's view (+0x68) and view-parameters-set flag (+0x5C); view matrices +0x90, +0x190, +0x1D0, +0x250, +0x260 |
| `umbraShareResults` | `Umbra read of one query result`, `FD3D11DynamicRHI::GetQueryData` | `umbraSharedVerified` |
| `umbraSharedVisibility` | `UmbraCommander visible instance` | `umbraSharedVerified`: the view's Umbra camera (+0x4D4), its impl (+0x10) and parameter word (+0x420); the query ring (commander +8, +0x1C8, +0x1CC, +0x2168, 0x19-byte entries) |
| `perEyeOcclusion` | `FSceneRenderer::Render` | `occlusionHistoryVerified`: FSceneViewState's occlusion history (+0xD10, 0x48 bytes), the renderer's views (+0x60) and the view's state (+0x8) |
| `sharePass` 1-5 (one Draw a frame is 5) | `FSceneRenderer deleting destructor`, `FSceneRenderer::RenderDpgLightTargets`, `RenderUmbraBasePass`, `before-lights pass`, `DPG pass`, `DPG lights`, `foreground pass`, `scene render finish`, `FSceneRenderTargets::Allocate`, `translucency check`, `SetUmbraCommander / wireframe state`, `GSceneRenderTargets`, `GDynamicRHI`, `scene frame counter`, `depth dirty flags`, `GSkipBeforeLights`, `Umbra commanders`, `Umbra commander index` | `sharePassVerified` and `sceneRendererVerified`: FSceneRenderer +0x00, +0x18, +0x28, +0x5C, +0x60/+0x68, +0xD8; the FViewInfo stride; the view's plain byte ranges; the RHI's begin/end scene slots (+0x628, +0x620); each pass function's signature. Also the renderer's seams (`kOneDrawSeams`, below) |
| scene captures nested in an eye (no key) | `scene capture's CaptureScene thunk`, `FSceneRenderTargets::BeginRenderingBackBuffer` | `sceneRendererVerified`, `sceneCaptureVerified`, and `ownsSceneCaptureHooks` (false on LE3: `plugin/Le3SceneCapture` hooks the same two functions, the ledger's Forks 9) |
| `pCoresOnly` | none (Win32) | none: works on every game |

**Layouts are not addresses.** They live in `RenderLeversTable.hpp`'s `Layout` struct: LE1's values are the defaults, a
game's own value goes into that game's `kLayout` where it differs (so far only the FViewInfo stride: 0x11A0 on ME2, 0x11B0
on ME3), and each lever group has a `...Verified` flag. A flag is set only when (1) the layout was read in **that game's
own code** (lanes R2 and R3 write each offset in their sections below) and (2) the group **ran in the game**: a lever
after its A/B, the two passive groups (the accounting, the commander's timing) after a first protected run with their
hooks in. Until then the group stands down, whatever the rows say, so a build from main never hooks something in a
sequel that nobody has run there.

**Where a game calls the same natives differently** is in `Layout` too, each read in that game's code:

| `Layout` field | ME1 | ME2 | ME3 | what the module does with it |
|---|---|---|---|---|
| `ineligibleShowFlags` | 0x33 | 0x33 | 0x33, 0x47, 0x15, 0x1B | `Eligible()`: a renderer with one of them set leaves the Umbra branch, so it is never kept or replayed |
| `instanceCallAlsoKeepsBooks` | no | no | yes | the visible-instance hook answers "not visible" (the size cull, the shared-visibility filter) only for the marking call, `a7 != 0 && a6 == 0`; every other call goes to the original |
| `captureTakesUmbraBranch` | no | no | yes | the light-targets hook marks the replay's tail only outside a capture |
| `umbraCommandGate` | none | none | 0x1767208 | logged once from the command hook (`render levers: Umbra's command gate is N`): while it is 0 ME3's `command()` does nothing |
| `ownsSceneCaptureHooks` | yes | yes | no | the capture hooks are not installed where another module has them |

### Enabling a lever on a sequel (the procedure)

1. The row (or rows) is in the sequel's `kTable` with LE1's names; `ctest -C Release -R mevr-render-levers-table` shows
   them (`LE3: ... 28 of 28 render-lever rows`).
2. The lever's layout is in the sequel's `## ME2`/`## ME3` section; a value that differs from LE1's goes into that
   game's `kLayout` (`src/plugin/RenderLeversTable.hpp`).
3. Make a **try build**: `cmake -S . -B build-trilogy -DMEVR_RENDER_LEVERS_TRY=ON`, build `MEVR-LE2` or `MEVR-LE3`. In it
   the groups whose layouts were read (`TryReadGroups` in the table: the accounting, the commander, the bound shader
   states, the visible instances, Umbra's query matrix) are hooked on the sequels. Never deploy a try build for anything
   but these runs, and turn the option off again afterwards (`-DMEVR_RENDER_LEVERS_TRY=OFF`).
4. Deploy it in a protected run (`## Measurements`). In a try build the two levers that are on by default on ME1
   (`keepBoundShaderStates` 1, `minScreenRadiusPx` 8) start at the game's own (0), so every A/B begins from a clean 0
   and nothing has to be written into `ASI\<GAME>MEVR.ini` before the game starts (a kept bound shader state stays
   kept, so that lever's "0" arm must come first). Read the attach lines in `Logs\<GAME>MEVR.log`: no `stands down` line
   for the lever, and its settings line (for example `render levers: bound shader states live as long as the game's own
   references ... ([Performance] keepBoundShaderStates = 0)` without `not hooked`).
5. A/B at the baseline spot with `tools\measure-levers.ps1 -Game <game> -Key <key> -Values 0,1`, a snapshot at each
   value: no missing meshes, no popping between two snapshots a second apart.
6. If it holds: set the group's `...Verified = true` in that game's `kLayout`, write the numbers under
   `## Measurements`, land. ME1's `[Performance]` key and default apply on that game from then on. If it damages the
   picture the flag stays false and `## Measurements` says why.

### What the renderer has to answer (seams)

`plugin/RenderLeversSeams.hpp` is `render_levers::seam`. On LE1 it is `MELEVR::MevrBridge` itself. On the sequels the same
names answer "no such seam yet": `StereoFrameActive()` false, `FindPass1View()` false, `g_oneDrawStereo` a flag nobody
reads, `kOneDrawSeams = false` (so `sharePass` stands down whatever the table has). The one seam vocabulary is lane U1's
(`src/halcyon/MevrSeams.h`); when a sequel's renderer has one of these, its answer replaces the stub.

The probe's seams are functions in `plugin/PerfProbe.hpp` that a sequel's bridge calls (LE1's are Halcyon ME1's own
`MevrBridge::PerfPresentBegin/End`, `PerfPass0Captured`, `g_perfDraws`):

| what | call | where in the sequel's renderer |
|---|---|---|
| the frame's bounds and the context | `perf::SeamPresentBegin(ctx)`, `perf::SeamPresentEnd(ctx)` | the Present hook's first and last action |
| the first eye's end | `perf::SeamFirstEyeCaptured(ctx)` | right after the first eye's capture in DrawDetour's SFR branch |
| draws | `perf::NoteDraws()` | each D3D draw hook in `d3d_capture.cpp` |
| the waits | `perf::WrapFrameFunctions(waitSwapchainImage, waitFrame, beginFrame, endFrame)` | `MevrBridge::WrapSessionFunctions`, after the test loop's wrap |

Since main 60791416 (lane U1) both sequel bridges call all of them (`MevrSeams::PerfPresentScope`,
`PerfFirstEyeCaptured`, `PerfDraw`, the four slots). **Before the first `SeamPresentBegin`**, or in a build without
those calls (the measurements below were taken in one), the probe works without a seam: a frame is **xrEndFrame's
return to the next one's** (`PerfProbe.cpp` `EndFrameAsFrameBoundary`). The game's immediate context for the GPU timestamps is found from the
frame's own projection layer (its swapchain's first D3D11 texture's device). The capture then has: the frame interval,
the render thread's busy time per frame, the submit's time, GPU timestamps at the frame's start, at the submit and at
its end (the GPU's lag behind the CPU, and its span), and every game-thread tick. It has no draw count, no split between
the eyes and no `xrWaitFrame`/`xrBeginFrame` rows; its JSON says `"frameBounds": "xrEndFrame"` and `tools/perf-probe.py`
prints what that means. The per-eye split comes from the render levers' accounting instead (the `render levers:` line's
scene renders per frame and ms per render by its place in the frame), as soon as the `FSceneRenderer::Render` row exists.

### One Draw a frame on the sequels: the design (not built)

Written before building, as the lane's task asks; it waits for the three things named at the end.

**What the sequels do today.** Halcyon's ME2 and ME3 "SFR" is the same architecture as its ME1 "Stereo 2", ported
from it (`src/halcyon/le2/calcview_hook.cpp` and `le3/calcview_hook.cpp`, `DrawDetour`): the game thread calls
`FViewportClient::Draw` **twice a present** (pass 0, then the "replay", `t_replay = true`), each a full engine frame
from its own eye (`ApplyEyeOffset` by plus or minus the half eye distance in the CalcSceneView detour), each through
**its own scene view state** (`g_eyeStateLeft`, `g_eyeState`; ME1's gameplay shares one state between the eyes). The
render thread snapshots the backbuffer into `g_sfrPass0Tex` at the pass boundary (the next scene depth clear:
`SfrMaybeCapturePass0AtClear` in `d3d_capture.cpp`'s ClearDepthStencilView hook) and mirrors the UI draws into it. So
every cost ME1's one-Draw removed is there: the second game-thread Draw, the second visibility, the second engine render.

**The design is ME1's `sharePass = 5`, unchanged** (research/37 "The one-Draw design"): one Draw a frame; the frame's
only scene render is recorded by `plugin/DrawReplay` from its tail mark, and in the render's finish hook, with the
renderer still alive, the second eye's view is synthesised from the first's (`SynthesiseSecondView`: origin + d,
pre-view translation - d, the translated view-projection rebuilt) and the recorded tail is re-issued with the view's
bytes substituted. All of that is in the shared module already. What each sequel needs:

| piece | where | on a sequel |
|---|---|---|
| the scene render's entry, its finish, the tail mark, the pass functions, the globals | rows of `engine/Addresses.hpp` (the `sharePass` line of the table above) | lanes R2/R3; each pass function's signature must be read in the sequel's code, not assumed |
| layouts | `RenderLeversTable.hpp` `kLayout`: the FViewInfo stride (the ledger notes 0x11A0 for LE2 and 0x11B0 for LE3, [verify]), the renderer's views, target and flags, and the view's plain byte ranges (`kViewPlainRanges`, `kViewCaptureRanges`: the +0x480..+0x4D0 part is LE1's and must be read in each sequel's FSceneView copy constructor) | lanes R2/R3, then `sharePassVerified` |
| the pass-1 view record | a `[MEVR]` seam in the SFR branch of each sequel's CalcSceneView detour, after `ApplyEyeOffset`, `ApplyHeadLook` and `ApplyFov` ran on pass 0: `RecordPass1View({the final view origin, forward, delta = pass 1's camera minus pass 0's (twice the half eye distance along the view's right axis, as the detour applies it), mode 0})` | lane U1 (`MevrSeams.h` reserves the names). The sequels' frusta are symmetric with the convergence applied at submit, so the record's projection terms stay unused |
| the second Draw skipped | `DrawDetour`: while the host's `g_oneDrawStereo` is set, `SfrReplayDrawSEH` is not called, and everything that tells the render side "an SFR frame" still happens (its no-op retry must not take a skipped replay for a refused one) | lane U1 |
| `StereoFrameActive()` | DrawDetour's own gate (`sfrGameplay`) | lane U1 |
| the recorder's ear | `Record*` calls in each sequel's `d3d_capture.cpp` draw, clear and UpdateSubresource hooks; `NotePixelShaderCreated` in a CreatePixelShader hook | lane U1 |
| the pass-0 capture | nothing new if the replayed tail contains the pass-boundary depth clear (LE1: "the replay's pass-boundary depth clear still takes Halcyon's pass-0 capture"); checked first in the eye snapshots | a check |

**Risks specific to the sequels, each settled by a test before the lever is on:**

1. **Per-eye view states.** The sequels gave each pass its own FSceneViewState so temporal effects (anti-aliasing
   history, motion blur, eye adaptation) don't smear between the eyes. With one render there is one state again, and the
   second eye is a replay of the first eye's post-processing. LE1's replay has the same property and its picture was
   accepted, but the sequels' post chains differ. Test: a head-turn snapshot pair, looking for smear in the right eye.
2. **Screen-space matrices.** LE1 needed `core/vr/ScreenMatrix` for `ScreenToShadowMatrix` and `ScreenToWorld`, and its
   character (light-environment) shadows still land wrongly in the replayed eye (research/37, 2026-11-13: off by default
   while the second eye is a replay). Whether the sequels' shaders take those constants is the ledger's open R04. Expect
   the same defect: with one-Draw the sequels would need `LightEnvironmentShadows False`, which they don't need today
   (`## Measurements`, "Character shadows").
3. **One-eye objects.** Whatever the engine culls or sorts for the first eye only shows in one eye. LE1 moved to each
   eye's own frustum for this (`oneDrawWindow = 0`); the sequels render both eyes on one symmetric window and shift at
   submit, which is LE1's `oneDrawWindow = 1` case: whole meshes dropping out frame by frame in the owner's QA of
   2026-11-06. So the per-eye frustum (the ledger's R06) comes **before** one-Draw on the sequels.
4. **The dynamic rings.** The buffer-wrap shadows are D3D11 only and come with the recorder.

**What it waits for:** (a) lanes R2/R3's rows and layouts for the `sharePass` group and their answer to "does the
sequel's SFR allow it" (on LE1 one Draw a frame is the flat game's own state for Umbra's command ring, and a Draw
without its resolve deadlocks: the second Draw is skipped whole, never half); (b) lane U1's seams above; (c) the
baseline's answer to whether it is worth it (the recommendation under `## Measurements`).

## Measurements

### How to measure a sequel (the procedure)

One game run at a time on the machine (`tools\le2-halcyon-run\run-lock.ps1 -Status`); no build and no ctest while it
runs (a build skews the numbers; an installer test's fake `MassEffectLauncher.exe` makes `launch.ps1` refuse, as on
2026-11-16 11:39). Everything is by script, from the repository's root, with a development (hot-reload) `build-trilogy`.

ME2, at Freedom's Progress (the VR Shepard career's Save 4, the runner's default):

```powershell
$h = '<your session name>'
tools\le2-halcyon-run\prep.ps1 -Holder $h -Tag perf            # backups, deploy build-trilogy, fake headset on
tools\le2-halcyon-run\launch.ps1 -Holder $h                    # Steam's -game 2, waits for the XR session
tools\le2-halcyon-run\load.ps1 -Holder $h                      # Save 4, waits for gameplay
python tools\make-look-recording.py "$env:LOCALAPPDATA\MEVR\recordings\look-20s.mevrrec" 20   # the head turning a full circle in 20 s
tools\replay.ps1 -Path "$env:LOCALAPPDATA\MEVR\recordings\look-20s.mevrrec" -Loop
tools\perf-probe.ps1 -Seconds 20 -Tag le2-baseline             # one full turn; prints the breakdown (tools\perf-probe.py)
tools\snapshot.ps1 -Tag le2-baseline -Delay 1                  # both eyes as PNGs, %LOCALAPPDATA%\MEVR\snapshots
tools\sample-render-thread.ps1 -Game LE2 -Top 40               # the render thread by function (needs build\Release\mevr-stack-sampler.exe)
tools\measure-levers.ps1 -Game LE2 -Key <key> -Values 0,1 -Rounds 2 -Seconds 20 -Recording "$env:LOCALAPPDATA\MEVR\recordings\look-20s.mevrrec"   # an A/B
tools\replay.ps1 -Stop
tools\le2-halcyon-run\launch.ps1 -Holder $h -Stop
tools\le2-halcyon-run\restore.ps1 -Holder $h                   # expect "verification: 0 differences"
```

ME3, on the Normandy (the career's `Save_0031.pcsav`; the runner's `-RendererPreview` mode on every command, and
`-BuildDir <repo>\build-trilogy` on prep):

```powershell
tools\le2-halcyon-run\prep.ps1 -Holder $h -Game LE3 -RendererPreview -Tag perf -BuildDir "$PWD\build-trilogy"
tools\le2-halcyon-run\launch.ps1 -Holder $h -Game LE3 -RendererPreview
tools\le2-halcyon-run\load.ps1 -Holder $h -Game LE3 -RendererPreview -Save 'John_12_Vanguard_021024_46e6e39\Save_0031.pcsav'
# ... the same replay, probe, snapshot, sampler (-Game LE3) and A/B (-Game LE3) lines; change [Performance] keys only after load.ps1 returned
tools\le2-halcyon-run\launch.ps1 -Holder $h -Game LE3 -RendererPreview -Stop
tools\le2-halcyon-run\restore.ps1 -Holder $h -Game LE3 -RendererPreview
```

What to read: `tools\perf-probe.py`'s "busy, whole frame" (the render thread's work a frame; on a sequel without the
wait slots wrapped it includes any spinning inside `xrWaitFrame`), "span: whole frame" (the GPU's work, at most) and
"lag at frame end", the game thread's "busy, engine + ours" (both Draws' game-thread side), and in the game's
`Logs\<GAME>MEVR.log` the `render levers: last 10 s:` line (scene renders per frame, ms per render by its place in the
frame: `#0` the first eye, `#1` the second), once the `FSceneRenderer::Render` row exists. The fake headset holds the
frame rate near 31 fps whatever the load (skill `fake-headset-run`), so compare the work, never the frame interval.
For picture damage, Read the two snapshots' `-left.png` and `-right.png` side by side: no missing meshes, and no popping
between two snapshots a second apart.

The whole stretch for one sequel as one script (what lane PL runs): prep, launch, wait for the engine to tick
(`mevr state` answers) plus 30 s (the mod writes its first-run settings then, and `load.ps1` refuses a run whose ini
changes under it: "Save or runtime artifact changed during load", 2026-11-16 13:27), load, two baseline captures under
the replayed head turn, the render thread by function, each lever's A/B (0 first, then 1, a capture and two snapshots a
second apart at each value), four snapshots with `scale set LightEnvironmentShadows False` and four with `True`, then
stop and restore. It is `scratch\pl\run-sequel-queue.ps1 -Game LE2|LE3` in the lane's worktree (not in the repository:
its paths are the worktree's); every command in it is one of the lines above.

### Results

#### LE1 is unchanged by the shared modules (2026-11-16)

The same-pose check the lanes' rules ask for, on the fake headset: `tools\fake-headset-session.ps1` twice, first with a
players' build of main (494942ad) and then with the same build of the lane's branch, each time the world paused
(`pause`), the head held still by a replayed recording (`make-look-recording.py ... --yaw 0`), one snapshot and one
10 s frame capture. Sessions `2026-11-16_12-56-34-pl-before` and `2026-11-16_13-00-14-pl-after`
(`%LOCALAPPDATA%\MEVR\sessions`, each with a `pl` folder holding its snapshot and capture).

| compared | result |
|---|---|
| the snapshot's `hostView` | identical |
| the layers (left, right, hud, label, wrist: names, kinds and every field) | identical, 0 fields differ |
| `-hud.png`, `-left.png`, `-right.png`, `-left-world.png` | identical pixel for pixel (the world was paused) |
| the capture's JSON keys, `halcyon`, `resolution`, the `renderLevers` settings | identical |
| the `render levers:` attach and settings lines of `LE1MEVR.log` | the same 19 lines, the same `the RHI's AddRef is at RVA 0x215B10; the int at +8 went 0 -> 1` |

The same table and seam names compile to the same constants (the `static_assert`s of `RenderLeversTable.hpp`).

#### The sequels' hooks go in (ME2, 2026-11-16 13:26)

The first protected ME2 run of a try build (session `20261116-132559-920-LE2-pl-levers`): the exe gate took all 45 of
ME2's rows (`exe: 45 of 45 addresses at their RVAs (44 by their bytes ...)`), and at attach
`render levers: on LE2: the scene render is counted and timed, the Umbra commander, the query reads, Umbra's query
matrix, the bound shader states, Umbra's visible instances`; `sharePass` and `umbraShareResults` stood down as written.
In the entry menu the visible-instance hook read a sane projection (`[1][1] = 5.638`) and the query-matrix hook found
ME1's sign (`the view matrix is the translated one moved by plus the pre-view translation`). The run ended there
(`load.ps1` refused it, above); nothing was measured in it.

#### ME2: the baseline and the A/Bs (2026-11-16 14:14-14:24)

Session `20261116-141434-005-LE2-pl-levers` (restored: `verification: 0 differences`); the lane's copies in
`scratch\pl\runs\LE2-20261116-141432`. A try build; Freedom's Progress (Save 4), where the save puts Shepard, Jacob and
Miranda in view; the head turning a full circle every 20 s, so each capture covers every direction once; eye images
4096x2304, two a frame; the fake headset at 90 Hz. Other sessions were compiling during it (6 compilers at the first
capture), so read differences under about 0.5 ms as noise. All levers at the game's own unless named.

**What the probe gives without a Present seam** (frames bounded by xrEndFrame, 20 s each, median / p90, ms):

| capture | frames | frame interval | render thread busy | GPU span | game thread busy | xrEndFrame |
|---|---|---|---|---|---|---|
| baseline | 1056 | 13.89 / 20.99 | 7.23 / 14.04 | 4.27 / 8.77 | 3.45 / 5.19 | 0.34 |
| baseline, again | 1100 | 13.98 / 19.56 | 10.42 / 15.00 | 4.31 / 10.36 | 3.60 / 5.33 | 0.36 |
| `keepBoundShaderStates` 0 | 1379 | 13.69 / 16.49 | 12.09 / 14.74 | 4.28 / 7.64 | 3.69 / 4.95 | 0.38 |
| `keepBoundShaderStates` 1 | 1393 | 13.44 / 17.93 | 9.14 / 14.27 | 4.15 / 7.81 | 3.37 / 4.76 | 0.34 |
| `minScreenRadiusPx` 0 | 1361 | 13.41 / 16.05 | 11.46 / 14.48 | 4.24 / 7.16 | 3.59 / 4.95 | 0.37 |
| `minScreenRadiusPx` 8 | 1388 | 12.97 / 16.09 | 10.84 / 14.36 | 4.04 / 7.04 | 3.50 / 4.91 | 0.37 |
| `umbraQueryWait` 1 | 1505 | 12.96 / 15.43 | 10.74 / 14.32 | 4.14 / 6.16 | 3.46 / 4.56 | 0.35 |
| `umbraQueryWait` 0 | 1521 | **11.34** / 16.43 | **6.19** / 14.42 | 4.09 / 8.03 | 3.68 / 5.22 | 0.38 |

The render thread's busy median swings between 7 and 12 ms at one setting (the three "game's own" rows): it is two
populations, a frame whose second eye spins for Umbra's query results and one whose doesn't (below), so the median is
a poor measure here and the per-render lines are the ones to read. ME2 is **not GPU-bound** at this spot (the GPU's
span is 4.3 ms); the render thread sets the pace. No draw count yet (no seam).

**How SFR splits its two Draws** (the `render levers: last 10 s` lines; the head's turn makes alternate 10 s windows
light and heavy):

| window | scene renders a frame | #0, the first eye | #1, the second eye | the second eye's Umbra results read | query reads a frame, #0 / #1 |
|---|---|---|---|---|---|
| light direction | 2 | 1.4-1.8 ms | 5.5-6.0 ms | 3.9-4.5 ms | 60-100 / 21,000-27,000, nearly all without a result |
| heavy direction | 2 | 3.5-4.6 ms | 6.6-7.6 ms | 2.0-3.6 ms | 110-150 / 9,000-22,000, nearly all without a result |

**The second eye costs 3 to 4 ms more than the first, and all of it is one thing:** its Umbra resolve reads its
occluder queries' results (command code 6) and they are never ready, so it polls `GetQueryData` some 20,000 times a
frame until its time budget runs out; the longest single read in a window was 11 to 210 ms (the float stamp's
precision: research/35). The first eye's read takes 0.02 to 0.14 ms. This is the pathology ME1 measured when it tried a
view state per eye (research/35 attempts 17-21), which Halcyon's ME2 SFR always has, and which lane R3 predicted for
ME3. A few frames in a hundred have a third scene render (1.2 to 12 ms; not identified: no capture is spawned by us).

**`umbraQueryWait` 1 then 0** (ME1's default is 1):

| | #0 light / heavy | #1 light / heavy | #1's results read | query reads a frame, #1 | visible instances a frame, light / heavy |
|---|---|---|---|---|---|
| 1 (the game's own) | 1.8 / 3.6-3.7 ms | 5.5 / 6.9 ms | 3.0-4.1 ms | 16,000-22,000 | 205 / 530-580 |
| 0 | 2.0-2.2 / 4.8-5.4 ms | 2.1-2.6 / 4.8-5.2 ms | 0.13-0.21 ms | 60-300 | 204-224 / 730-870 |

With 0 the second eye costs what the first does. Both eyes then draw what an unready query would have hidden (40% more
instances in the heavy direction, 1.2 to 1.7 ms more for the first eye there), so the two renders together go from
7.3 to 4.2 ms in the light direction and from 10.5 to 9.6-10.6 ms in the heavy one; the probe's frame interval fell from
12.96 to 11.34 ms and the render thread's busy median from 10.74 to 6.19 ms (p90 unchanged, 14.4). The picture can only
gain meshes, never lose one (an unready query counts as visible); the snapshots show nothing missing. **Not switched:**
ME1's value is the default, and 0 on ME2 is a change of default for the owner to decide. It is the largest lever found:
it removes the second eye's spin without a seam and without one-Draw.

**`keepBoundShaderStates` 0 then 1.** ME2 builds far fewer than ME1 did (22 to 45 bound shader states a frame in the
heavy direction, under 1 in the light one; ME1's heavy spot: 300). On: 161 kept in all, creations 0.0 to 0.1 a frame.
The first eye in the heavy direction went from 4.3-4.6 to 3.7-4.1 ms (about 0.4 ms, at the noise's edge), the light
direction and the second eye didn't move. The log's check line: `bound shader states kept: the RHI's AddRef is at RVA
0x2B2640; the int at +8 went 0 -> 1 on the first kept object`. **That RVA is not the one lane R2's recipe names
(`0x992830`)**: `0x992830` is slot 0x8A0 of the vtable at `0x1227C50`, and the live `GDynamicRHI` object's vtable is
another one, whose slot 0x8A0 is `0x2B2640`: the counterpart of ME1's `0x215B10` and ME3's `0x2CB940` (both are what
slot 0x8A0 of ME1's `0x120FF00` and ME3's `0x12FC7A0` hold in the exe files). The reference count went 0 to 1 exactly
as on ME1, so the hook is right and the recipe's expected RVA is the thing to correct. No damage in the snapshots.
**On for ME2** (`boundShaderStateVerified`), ME1's default 1.

**`minScreenRadiusPx` 0 then 8.** 73 to 81 of 520 to 585 visible instances a frame are under 8 px in the heavy
direction (14%), 46 of 204 in the light one (23%); neither eye's render time moved measurably (3.9-4.0 / 6.6-7.6 ms
against 3.7-4.0 / 6.9 ms). The snapshots show nothing missing, but they were taken under a turning head, so two
snapshots a second apart can't show pop-in. **Left off for ME2** (`umbraInstanceVerified` false) until a pair under a
still head (`make-look-recording.py --yaw <deg>`), since it gains nothing measurable here either.

**`umbraEyeMatrix`** hooked and idle, as lanes R2 and R3 said: `Umbra's queries with the eye's matrix: 0 renders
fixed, N already right` in every window (both eyes use one symmetric frustum).

**On for ME2 from this run:** the accounting (`sceneRendererVerified`), the commander's timing and the query-read
counts (`umbraCommanderVerified`), `keepBoundShaderStates`, and the idle `umbraEyeMatrix` hook (`umbraQueriesVerified`).

**Character shadows (run 7).** `scale set LightEnvironmentShadows True`, four snapshots: with Jacob and Miranda in
view the dark contact shadows sit under each one's feet in the left eye and in the right eye alike, shifted with the
feet by the eyes' parallax; with `False` the same ground is clean. So on ME2 the shadows are right in both eyes with
them on, as the code says (each eye is a full engine render). One scene, by eye (`sheet-shadows.png`, `crop-on.png`
in the run folder); a 10 s capture with them on read the same as without (render busy 10.4 ms median, in the
noise).

#### ME3: the baseline and the A/Bs (2026-11-16 14:30-14:38)

Session `20261116-143006-726-LE3-pl-levers` (the runner's `-RendererPreview` mode; restored: `verification: 0
differences`); the lane's copies in `scratch\pl\runs\LE3-20261116-143005`. A try build; the Normandy (`Save_0031`), the
head turning a full circle every 20 s; 5 compilers of other sessions were running. The exe gate took all 66 of ME3's
rows (`exe: 66 of 66 addresses at their RVAs`), every hook went in, and `render levers: Umbra's command gate is 1`
(lane R3's difference 4: the commander is live).

| capture | frames | frame interval | render thread busy | GPU span | game thread busy | xrEndFrame |
|---|---|---|---|---|---|---|
| baseline | 1465 | 13.80 / 15.71 | 12.97 / 14.92 | 3.33 / 6.47 | 2.77 / 3.97 | 0.30 |
| baseline, again | 1445 | 13.84 / 16.24 | 12.47 / 14.76 | 3.48 / 6.68 | 2.80 / 4.01 | 0.30 |
| `keepBoundShaderStates` 0 | 1409 | 13.79 / 16.21 | 12.25 / 14.80 | 3.43 / 6.75 | 2.95 / 4.27 | 0.31 |
| `keepBoundShaderStates` 1 | 1210 | 13.97 / 18.66 | 10.85 / 14.68 | 3.76 / 9.11 | 3.41 / 4.86 | 0.35 |
| `minScreenRadiusPx` 0 | 1452 | 13.67 / 16.08 | 11.81 / 14.67 | 3.57 / 6.66 | 3.19 / 4.27 | 0.33 |
| `minScreenRadiusPx` 8 | 1408 | 13.69 / 16.33 | 11.97 / 14.64 | 3.59 / 7.70 | 3.36 / 4.66 | 0.35 |
| `umbraQueryWait` 1 | 1464 | 12.92 / 15.45 | 11.39 / 14.39 | 3.40 / 6.09 | 3.16 / 4.46 | 0.32 |
| `umbraQueryWait` 0 | 1539 | **11.22** / 18.21 | **5.37** / 16.05 | 3.12 / 9.31 | 2.97 / 4.31 | 0.31 |

ME3 is not GPU-bound here either (3.3 ms); the render thread is the limit, busy 12 to 13 ms of a 13.8 ms frame.

**How SFR splits its two Draws:**

| window | scene renders a frame | #0, the first eye | #1, the second eye | the second eye's Umbra results read | query reads a frame, #0 / #1 |
|---|---|---|---|---|---|
| light direction | 2 | 1.3-1.9 ms | 5.4-6.0 ms | 3.9-4.8 ms | 100-160 / 21,000-30,000, nearly all without a result |
| heavy direction | 2 | 3.6-4.5 ms | 7.3-8.5 ms | 3.2-4.3 ms | 220-270 / 16,000-27,000, nearly all without a result |

**Lane R3's "measure this first" is answered: yes.** ME3's second pass polls unready Umbra queries exactly as ME2's
does (and as ME1's did with a view state per eye): 3 to 5 ms of every frame, the longest single read 11 to 334 ms.

**`umbraQueryWait` 1 then 0:** the second eye's results read falls to 0.08-0.31 ms and its query reads to 100-330 a
frame; #1 in the light direction 5.9 to 1.5 ms (the two renders together 7.3 to 3.1 ms). In the heavy direction the one
window measured had #0 at 6.6 and #1 at 7.0 ms (13.6 together, against 11.4 to 12.9 before) with 994 visible instances
against 860 to 916: what the wait saves the unoccluded draws cost again there. The probe's frame interval fell from
12.92 to 11.22 ms and the busy median from 11.39 to 5.37 ms, but the p90s rose (15.45 to 18.21 ms, 14.39 to 16.05 ms).
So on ME3 it is a clear win in light views and about even, perhaps a loss, in heavy ones. **Not switched** (a change of
default; and it needs a heavier ME3 scene than the Normandy before anyone decides).

**`keepBoundShaderStates` 0 then 1.** ME3 builds 86 to 122 bound shader states a frame in the heavy direction (ME2: 22
to 45), 0.0 to 0.1 in the light one. On: 436 kept, creations 0.0 to 0.1 a frame, and the check line is the one lane
R3's recipe gives: `the RHI's AddRef is at RVA 0x2CB940; the int at +8 went 0 -> 1`. The second eye in the heavy
direction went from 8.2-8.5 to 7.3-7.7 ms (about 0.8 ms); the first eye and the light direction didn't move. No damage
in the snapshots. **On for ME3** (`boundShaderStateVerified`), ME1's default 1.

**`minScreenRadiusPx` 0 then 8.** 102 to 219 of 315 to 911 instances a frame are under 8 px (ME3 reports a constant
108 instances under 1 px in every direction); neither eye's time moved measurably (4.7 / 7.3-7.5 ms against 4.5 / 7.7
ms). **Left off for ME3**, for ME2's reasons.

**`umbraEyeMatrix`** hooked and idle (`0 renders fixed, N already right`).

**On for ME3 from this run:** the accounting, the commander's timing and the query-read counts, `keepBoundShaderStates`,
the idle `umbraEyeMatrix` hook.

**Character shadows (run 7): not determined on ME3.** The fixture is a dim corridor of the Normandy; with
`LightEnvironmentShadows True` and `False` Shepard's body shows no shadow that can be told from the floor in either eye
(`sheet-shadows.png`). It needs a lit scene with a character on open ground.

#### The landed configuration with lane U1's seams (2026-11-16 14:54-15:06)

A normal build (no try option) of main 60791416 plus this lane's second landing, one short run per sequel (sessions
`*-LE2-pl-seams`, `*-LE3-pl-seams`; `scratch\pl\runs\LE2-20261116-145403`, `LE3-20261116-145833`; both restored with
0 differences; 8 to 18 compilers of other sessions running, so no timings are taken from these).

- **Attach, both games:** `render levers: on LE2: the scene render is counted and timed, the Umbra commander, the query
  reads, Umbra's query matrix, the bound shader states`, with `sharePass`, `umbraShareResults` and `minScreenRadiusPx`
  standing down by name; `bound shader states kept: ... RVA 0x2CB940` on ME3. That is the configuration a build from
  main has.
- **ME2, the seams run:** the capture says `frames bounded by Present`; draws a frame 961 median, 3,059 at p90 (the
  first draw counts for a sequel); `xrWaitFrame` 0.03 ms, `xrBeginFrame` 0.02 ms (ME2's frame is not held in the wait).
  **`PerfFirstEyeCaptured` does not split the eyes on ME2:** it comes 0.5 ms and 61 draws before Present, after 895
  draws, so "to the last left-eye capture" is nearly the whole frame. The eyes' split is the levers' `#0`/`#1` lines
  until the call sits at the pass boundary (lane U1's file; where ME2's pass-0 copy really happens is lane R2's
  "pass opening" question in its one-Draw recipe).
- **ME3, the seams run: a capture never started.** `perf probe: armed for 20 s` and no `measuring` line: ME3's Present
  hook hands the probe no device context, and the probe starts only with one. Fixed on the probe's side
  (`SeamPresentBegin`/`SeamPresentEnd` fall back to the context found from the frame's projection layer); **that fix
  has not run in the game.** The levers' accounting worked throughout (2 renders a frame, the same spin).

#### Still to measure

| run | answers |
|---|---|
| ME3 with the probe's context fallback; both sequels in a quiet machine | ME3's draw counts; that a capture starts on ME3 with lane U1's seams; clean timings with the seams |
| `minScreenRadiusPx` 0 and 8 under a still head (`make-look-recording.py out.mevrrec 10 --yaw <deg>`), two snapshots a second apart at each | pop-in; then `umbraInstanceVerified` |
| `umbraQueryWait` 0 in a heavy ME3 scene (combat, a hub) and in a heavy ME2 one | whether the second eye's saved spin outweighs the extra unoccluded draws there |
| character shadows on ME3 in a lit scene | the audit's defect on ME3 |
| a real headset | everything here is the fake headset: it shows which thread does the work, not what a player feels |

### Character shadows on the sequels (the audit's defect, for lane GF)

`game/SystemSettings` adds `LightEnvironmentShadows False` whenever `[Performance] sharePass` is 2, 3 or 5, and an empty
key reads as 5 on every game. On ME1 that is right: the second eye is a replay of the first eye's D3D calls, whose
screen-space shadow projection is the first eye's (research/37, 2026-11-13). **The sequels have no such replay.** Their
second eye is a second full engine render (`DrawDetour`'s second `FViewportClient::Draw`, each pass through its own view
state): the engine builds that eye's shadow projection from that eye's own view, as it does for the flat game. Lanes R2
and R3 read the same in the natives (two calls of the scene render a present, each with its own InitViews). And since
2026-11-16 `sharePass` stands down on both sequels in the shared module (`render levers: ... sharePass stands down`), so
nothing there replays an eye.

So by the code the workaround costs the sequels their character shadows for nothing, and **on ME2 the picture
agrees** (run 7 under "ME2" above): with `LightEnvironmentShadows True` the contact shadows sit under the squadmates'
feet in both eyes alike. ME3's fixture was too dark to tell. Recommendation to lane GF: gate the
`LightEnvironmentShadows False` entry on the host's second eye actually being a replay (ME1's host with `sharePass` 2, 3
or 5; a host flag rather than `#if LE1`, since a sequel that takes one-Draw later needs the workaround back).

### One Draw a frame on the sequels: the recommendation

**Not now.** The measurements changed the question. On both sequels the second eye costs 3 to 5 ms more than the first,
and that whole difference is the Umbra results read spinning for queries that are never ready, because Halcyon's SFR
gives each pass its own view state. One-Draw would remove it (no second resolve), but so does `umbraQueryWait = 0`
today, with no seam, no replay and no picture risk beyond drawing more. Without the spin the second eye costs what the
first does: 1.5 to 2 ms in light views, 4 to 5 ms in heavy ones. That is what one-Draw would then save, less its own
recording and replay (on ME1: about 1.1 ms of recording and 1.4 ms of replay for a 4.6 ms render). At these fixtures
that is a gain of 1 to 2 ms at best in heavy views and nothing in light ones, for the largest job on the list and with
ME1's open defect (character shadows wrong in the replayed eye) brought along.

In order:

1. **The owner decides `umbraQueryWait = 0` for the sequels** (ME2: 13.0 to 11.3 ms a frame at Freedom's Progress, the
   render thread's median work 10.7 to 6.2 ms; ME3: 12.9 to 11.2 ms, 11.4 to 5.4 ms, with worse p90s in heavy views).
   The alternative with the same effect and no extra draws is one view state for both passes in gameplay, as ME1
   ships, which is a change inside Halcyon's SFR branch (lane U1's files) and brings back the wait `ue3QueryWait`
   exists for; measure it against `umbraQueryWait = 0` before choosing.
2. **`keepBoundShaderStates`** is on for both (0.4 ms on ME2, 0.8 ms on ME3 in heavy views).
3. **The per-eye frustum before one-Draw** (the ledger's R06): one-Draw on the sequels' symmetric window is ME1's
   `oneDrawWindow = 1`, which the owner's QA rejected (meshes dropping out frame by frame).
4. **Measure again in a heavy scene of each game** (combat, a hub) with lane U1's seams, so the draw counts and the
   eyes' split are in the probe. Only if the second eye's own render (without the spin) is then several milliseconds
   is one-Draw worth its seams (the design above; both R2 and R3 name the pass-0 capture as its main risk).

The host side is ready for it either way: the module, the table and the recorder are in all three hosts.
