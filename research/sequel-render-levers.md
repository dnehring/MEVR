<!-- SPDX-License-Identifier: MIT -->
# ME1's render levers on the sequels: the native addresses

ME1's renderer performance work (`plugin/RenderLevers` and its helpers; research/22, 32, 35, 37; CLAUDE.md "Live render
levers") is group 2 of the parity ledger's "Renderer techniques by group": the same technique in every game, needing
each game's own native addresses. This note gives those addresses, one section per sequel, each with a table and a
numbered recipe per lever. Lane PL (`plugin/RenderLevers*`) enables a lever from a row here; the rows themselves are in
`src/engine/Addresses.hpp`, under LE1's row names (PL finds a row by name).

Rules for both sections: native code is described in words with RVAs, never pasted; a structure offset is only
"verified" when that game's own code was read using it.

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

**0. What the bridge must answer first (lane U1's seams).** `RenderLevers.cpp` asks Halcyon's LE1 bridge four
things; ME3's renderer has each fact:

1. `StereoFrameActive()`: "SFR renders the world this frame". ME3: `DrawDetour`'s `sfrGameplay` condition (VR on,
   mode `Sfr`, not menu or cinematic unless `ConvoFp::OwnsCamera()`).
2. `g_perfDraws`: a draw counter. ME3: add `++` in `MhDrawIndexed`, `MhDrawIndexedInstanced` and `MhDraw`
   (`d3d_capture.cpp`).
3. The frame boundary (`gRenderIndex` back to 0): ME3's Present hook, where `g_sfrCapturedThisPresent` is cleared.
4. For one-Draw only: `g_oneDrawStereo`, `RecordPass1View` / `FindPass1View` (recipe 8).

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
     makes on the render thread **between the first eye's finish and the replay** (ME1's `PerfPass0Captured`
     sits at the same moment) that copies the backbuffer into `g_sfrPass0Tex` and sets what the clear path sets
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
