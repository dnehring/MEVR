<!-- SPDX-License-Identifier: MIT -->
# ME3's lens flares in mode 2 and the shape of its shared window (lane FL, 2026-11-17)

research/30 section 9.6 points here. The owner wants life-size stereo always (docs/trilogy-plan.md question 4). Since lane
DEF (ee4f33f3) ME3 renders mode 1 by default (`MELE3VR.ini [VR] MetricEyeFrustum = 0`): one symmetric window for both
eyes, because in mode 2 every lamp's lens flare lands at a different place in each eye (lane FXR, research/30 9.4). Mode 1
costs sharpness (research/30 9.5). This note answers the owner's two ways back, written as an implementation guide:

- **B. Fix where ME3 places its flares, so mode 2 can come back.** Found, built (off by default), measured below.
- **A. A taller render image for ME3 in mode 1.** Two separate things set mode 1's sharpness: the window's shape (in
  tangents) and the image's (in pixels). The first was a leftover of upstream's side-by-side mode and is fixed (off by
  default); the second was measured in the game below.

Every address is an RVA in the 2.0.0.48602 exes (ME3 PE timestamp 60AEF0C4, ME2 60AEEFA5, ME1 60AEEFA2). No decompiled
code is quoted; the functions are described in words.

## 1. How ME3 places a lens flare (Ghidra, LE3)

### 1.1 How to find the code again

Run `tools\ghidra\decompile.ps1 -Game LE3 <rva>` on any of these (the names are ours; the exe is stripped):

| RVA | What it is | How it was found |
|---|---|---|
| `0x216F0` | the registration of `FLensFlareVertexFactory` (shader file `LensFlareVertexFactory`; type object `0x1894E70`; shader-parameter constructor `0x262530`) | the only code reference to the wide string `L"FLensFlareVertexFactory"` (`0x10E3BF8`) |
| `0x261A80` | the vertex factory's shader parameters' Bind: `CameraRight`, `CameraUp`, `LocalToWorld` | in the parameters' vtable `0x10E1148` (slot 1) |
| `0x26D0E0` | their Set(view): CameraRight and CameraUp are minus the normalised first and second rows of the view's InvViewProjectionMatrix (`+0x2E0`, `+0x2F0`) | slot 3 |
| `0x26D960` | their SetMesh: LocalToWorld with the view's PreViewTranslation (`+0x250`) added to its translation (translated world space) | slot 4 |
| `0x264A50` | `ULensFlareComponent`'s class registration (`L"LensFlareComponent"`, object size 0x270, InternalConstructor `0x268A20`, class vtable `0x10E2230`) | the reference to `L"LensFlareComponent"` (`0x10E395A`, the tail of `L"ULensFlareComponent"`) |
| `0x262AD0` | its CreateSceneProxy: a 0x1B0-byte `FLensFlareSceneProxy` built by `0x25D460` | the component's vtable, slot +0x4A8 |
| `0x26C160` | the **source element**'s draw: (dynamic data, scene proxy, draw interface, view, DPG) | the proxy-side vtable right after the component's, `0x10E27F8` |
| `0x26B780` | the **reflection elements**' draw, same arguments | `0x10E2800`, the next slot |
| `0x34EF00` | `FSceneView::WorldToScreen`: a world point times the view's ViewProjectionMatrix (`+0x260`), homogeneous | called by both draws |
| `0x34B6E0` | the same, divided by w | called by both draws |
| `0x34D7D0` | `FSceneView::ScreenToPixel` (the view's X, Y, SizeX, SizeY at `+0x5C..+0x68`) | its only callers are the two draws |
| `0x263B10` | one element's values for this view (radial distance from the screen centre, its size, colour and rotation curves) | called by both draws |

The FSceneView offsets are the ones `src/halcyon/MevrMetricView.h` already uses (`+0x90` ViewMatrix, `+0xD0`
ProjectionMatrix, `+0x190` TranslatedViewMatrix, `+0x1D0` TranslatedViewProjectionMatrix, `+0x250` PreViewTranslation,
`+0x260` ViewProjectionMatrix, `+0x320` ViewOrigin), plus three inverses: `+0x210` InvTranslatedViewProjectionMatrix,
`+0x2A0` InvProjectionMatrix, `+0x2E0` InvViewProjectionMatrix (Halcyon's `[INVMAT]` refreshes exactly these three:
le3 `calcview_hook.cpp`'s `RefreshInverseSlots`, after `ApplyEyeFrustum`).

### 1.2 What the two draws do

- **The source element (`0x26C160`)** is drawn at the proxy's own origin: its four vertices are at (0, 0, 0) in the
  component's local space and the mesh element carries the proxy's LocalToWorld. The GPU puts it through the pass's
  TranslatedViewProjection, so it is right in every mode. Its size and fade read the source's screen position through
  `+0x260` (the same for both eyes).
- **The reflection elements (`0x26B780`)**, one quad per element of the `ULensFlare` asset (the ghosts and glows, and in
  ME3's lamps the streak):
  1. the source's world point (the proxy's LocalToWorld origin) through `WorldToScreen` (`+0x260`): screen x, y and w;
     nothing is drawn when w <= 0;
  2. per element, the screen point on the line from the source through the screen's centre: the source's (x, y) times
     (1 - 2 x the element's RayDistance), plus the element's own offset from `0x263B10`;
  3. that screen point at clip depth 0.1 (z = 0.1, w = 1) times the view's **InvTranslatedViewProjectionMatrix**
     (`+0x210`), divided by w, plus ViewOrigin (`+0x320`): the element's world point; its size is scaled by that 1 / w;
  4. the quad is drawn at that world point through the vertex factory above, i.e. through the pass's
     TranslatedViewProjection (`+0x1D0`) on the GPU.

### 1.3 Why it fails in mode 2

In mode 2 `MevrMetricView::ApplyEyeFrustum` writes the eye's own off-centre frustum into `+0x1D0` only, and `[INVMAT]`
then refreshes `+0x210` from it; `+0x260` and the projection `+0xD0` keep the CPU's symmetric window (culling, the HUD's
projected markers). Step 1 projects through the symmetric window, step 3 unprojects through the eye's frustum: the element
comes back on the eye-frustum ray whose screen coordinates are the symmetric window's, so in each eye's image it sits at
the symmetric window's pixel. The same element sits at the same pixel of both eye images, which look different ways.

Checked against lane FXR's measurement (the PS VR2's left eye: tangents -1.8418 .. 0.9472, the window +-1.8418): a lamp
seen at x 1233 of 3072 is at tangent -0.722, the window's x is -0.722 / 1.8418 = -0.392, which is pixel 934 in the eye's
image: FXR measured the flare at 920 and the symmetric prediction at 934. `tests/FlareWindowTests.cpp` reproduces both
numbers (ctest `mevr-tests`).

### 1.4 ME2 and ME1

- **ME2** (LE2 project): its reflection draw is `0x252AB0` (the slot after the source element `0x253430` in the vtable at
  `0x10209A8`). Its step 3 unprojects through the world-space **InvViewProjectionMatrix (`+0x2E0`)**, which `[INVMAT]`
  refreshes from `+0x260`: forward and back through the same symmetric window, so the round trip is consistent and the
  GPU then draws the element where it belongs in each eye. ME2 has no such bug (as FXR saw: no misplaced sprite at
  Save 4). ME3's switch to the translated inverse (better precision far from the origin) is what exposed it.
- **ME1** has no UE3 lens flare in the exe (no `LensFlareComponent`, no lens flare vertex factory; only the material usage
  flag and the show flag). Its sun's flare is BioWare's own `BioSunFlareComponent` / `BioSunFlareStreakComponent`
  (`FlareSprite0..9`, `HideAllFlaresOverride`): registration `0xB24150`, class vtable `0x1284F78`, proxy constructor
  `0xB1B740` (proxy vtable `0x128B980`), draw `0xB1E5D0`: one sprite drawn at a world point (the view origin plus the
  view's forward times a distance, blended toward the sun) through the draw interface's sprite call, sized with the CPU
  projection's p0 and p5 (`0xB1DFB0`). A world-space sprite: the mismatch above can't happen there, and the fix below has
  no site in ME1. ME1's "sun sprite in one eye only near the edges" (research/30 section 6, research/37) is something
  else (most likely a test against the CPU window); not changed in this lane.

### 1.5 The options

1. **Give the flare draw each eye's own matrix** (make step 1 project through the eye's frustum): consistent, but each
   eye then mirrors the elements through its own frustum's centre, which is a different direction per eye (on a PS VR2
   each eye's centre is 24 degrees to its own side, 48 apart); the ghosts would land in different directions in the two
   eyes and still not fuse. Rejected.
2. **Make `+0x260` per eye in mode 2**: the same as 1 for the flares, and it changes every other CPU user of `+0x260`:
   Umbra's portal culling and occlusion queries (`umbraEyeMatrix` already swaps it, locally, around
   `UmbraCommander::IssueOcclusionQueries`), the HUD's projected markers, the flare's own fade. Too wide. Rejected.
3. **Move the sprite** after the fact (a vertex shader or a matrix on the draw): the element's world point is already
   wrong when the vertices are written; correcting it means repeating step 3 anyway. Rejected.
4. **Built: unproject through the window that projected.** For the reflection draw only, `+0x210` holds
   Inv(TranslatedView x CPU projection), the inverse that pairs with `+0x260`; the engine's own inverse is put back as
   the call returns. Each element's world point is then on the ray the symmetric window describes (the source element
   on the ray to its lamp, from each eye's own place), the ghosts along the same directions for both eyes: **mode 1's
   flares in mode 2**, which lane DEF saw sit on their lamps in both eyes. Nothing else reads the swapped matrix.

### 1.6 The fix as built

- `src/core/vr/FlareWindow.{hpp,cpp}` (MIT, unit-tested in `tests/FlareWindowTests.cpp`): `WindowInverse(TV, P)`,
  `Differs(a, b)` (1e-4 relative), `Unproject(inverse, x, y, z)` (the model of step 3).
- `src/plugin/FlareWindow.{hpp,cpp}` (host, ME3's host only): a hook on the row `FLensFlareDynamicData::RenderReflections`
  (`engine/Addresses.hpp`, LE3: `kLensFlareReflections = 0x26B780`, pattern `48 8B C4 4C 89 40 18 55 53 56 41 56 41 57
  48 8D` from `tools/rva-patterns.py` over the 60AEF0C4 dump; the exe gate checks it at attach). The detour, on the
  render thread: when enabled, read TV (`+0x190`), P (`+0xD0`) and the engine's inverse (`+0x210`) under SEH; compute
  the window's inverse; if it doesn't differ (mode 1) call the original untouched; else write it (journalled
  `FlareWindow.inverse`), call the original inside `__try/__finally`, and write the engine's inverse back. Counters:
  passes, rewritten, already the window's, faults; a `flare window:` line every 10 s while they change.
- **The setting:** `ASI\LE3MEVR.ini [Stereo] flareWindow`, read once a second (live); **1 when the key is missing since the
  owner's answer to question 17 (2026-11-17, section 5)**, 0 before. A development build also reads
  `%LOCALAPPDATA%\MEVR\stereo.measure`'s `[Stereo] flareWindow = 0 | 1`, which wins. It acts only where mode 2 runs, ME3's
  default again since then (`MELE3VR.ini [VR] MetricEyeFrustum = 1`).
- Logs: `flare window: hooked the reflection pass; lens flares' reflections in mode 2 ...` at attach, `flare window: N
  reflection passes, M through the window's inverse, K already the window's (mode 1), F faults`.
- To port it to another game: find the game's reflection draw as in 1.1, check which inverse its step 3 reads; only a
  game whose step 3 reads the translated inverse needs the row.

## 2. The shape of ME3's shared window and of its render image

### 2.1 The window (in tangents)

le3 `me2_xr.cpp` `ApplyFovFill` (the fill window `SetFovFill` publishes and `calcview_hook.cpp`'s `ApplyFov` writes into
the projection): the vertical half-angle is the headset's (eye 0's up/down), the horizontal is that times `aspect =
g_swapW / g_swapH`, and `g_swapW` is `g_bbW / 2` (the per-eye width of upstream's side-by-side mode, set where the
swapchains are made): **0.889 for any 16:9 backbuffer, whatever the headset**. `[LINKFOV2]`'s cover (on every runtime with
life-size, `MevrSeams::LifeSizeStereo() & 2`) then scales both tangents by one factor until the window holds both eyes.
With the PS VR2's eyes (+-61.5 degrees outward, 43.4 inward, +-53.0 vertical) that is +-1.8418 x +-2.0720 tangents
(+-61.5 x +-64.2 degrees): **56% taller than the eyes need** (+-1.3270). The two-pass render never had side-by-side's
half-width eyes; the uniform scale only kept the in-eye UI's shape, and ME3's UI is on the HUD panel since lane HU.

**Built (off by default):** `[Stereo] windowEyes = 1` in `ASI\LE3MEVR.ini` (or `ASI\LE2MEVR.ini`; development builds:
stereo.measure `[Stereo] window = eyes | aspect`, read at start) sets bit 2 of `MevrSeams::LifeSizeStereo()`, and both
trees' `ApplyFovFill` then take the window as the eyes' union, each axis on its own (`tH = hNeed`, `tV = vNeed`). The plan
(`plugin/MetricStereo`) already handles any window shape (the CPU window is the fill; the declared FOVs and image rects
follow). The game's projected UI is rescaled per axis by `[UIRATIO]` (d3d_capture.cpp `ComputeUiRatioViewport`:
tan(game) / tan(render) on each axis), so a per-axis window should keep the HUD's elements on their world points; that the
HUD panel looks right is **[verify]** (Save 31 draws no HUD, section 3).

### 2.2 The image (in pixels)

- What sets it: `MELE3VR.ini [VR] RenderW/RenderH`, which Halcyon writes into `GamerSettings.ini` ResX/ResY at attach
  (`ArmResolutionSpoof`, `[VRRES]`) and again at `ExitProcess` (the engine writes its in-memory size back on exit), the
  `[DISPQ]` spoof of `GetMonitorInfo`/`EnumDisplaySettings`/`GetSystemMetrics` (the game clamps its size to the primary
  monitor's; armed only when the target exceeds the desktop), and a borderless window (`vr-game-settings.ps1`: a titled
  window's client height is clamped to the desktop; upstream's comment that the game "keeps its aspect" (3072x3072 asked,
  3072x1620 made, 1.896:1) is most likely that clamp: 1.896 is a 2560-wide desktop's client area under a title bar and a
  taskbar, 2560x1350 [verify]). The runner's preview used 3072x1728.
- Tested in the game: see 3.

### 2.3 FSR's eye size

`MevrSeams::EyeImageSize` (`core/vr/Upscale` `EyeImageSize`) makes the eye images larger than the backbuffer only when the
backbuffer is smaller than the runtime's recommended eye size on both axes. With a 3072-wide render and the PS VR2's
2824x2880 it never is (3072 > 2824): FSR stands down and the eye images are the backbuffer's size, each eye's image rect a
crop of it. FSR would apply to a render below the recommendation on both axes (for instance 2400x2448 in mode 2); in
mode 1 the comparison should be the eye's crop, not the whole image (not changed).

## 3. Measured (fake headset, Save 31, the PS VR2's eyes replayed)

**How.** Two protected stretches in the runner's `-RendererPreview` mode (lane FL's script `%LOCALAPPDATA%\MEVR\fl-lane\
scripts\me3-fl.ps1`; runs `%LOCALAPPDATA%\MEVR\fl-lane\runs\LE3-20261117-131028` and `...-133442`). Per variant: the
window's shape in `%LOCALAPPDATA%\MEVR\stereo.measure` (`[Stereo] window = eyes | aspect`), `prep.ps1 -RendererPreview
[-PreviewEyeFrustum] -PreviewRenderSize <W>x<H>` (both new, this lane), launch, `load.ps1 ... -Save <career>\Save_0031.pcsav`
(the Normandy), lane FXR's still, level PS VR2 recording replayed with `-Fovs`
(`%LOCALAPPDATA%\MEVR\recordings\fxr-psvr2-still-level.mevrrec`: left eye -61.5 / +43.4 / +-53.0 degrees, the right
mirrored, 63 mm apart), snapshots (paused in mode 2, once per `[Stereo] flareWindow` value written into
`ASI\LE3MEVR.ini`), then `tools/perf-matrix.ps1 -Fovs` (new switch) over the flare switch, 2 rounds x 15 s at the same
still pose; stop, restore (every variant restored hash for hash). Analysis: `analyse.py` (each eye's projection layer: rect
and declared FOV, so pixels per tangent; DEF's lamp-and-streak finder), `crops.py` (the same angular window around a lamp in
each eye), `stereo-same.py` (`tools/stereo-check.py` at the same nine directions in every variant), all in the lane's
scripts folder.

**The flares (mode 2, 3072x1728, the eyes' window; snapshots 13-11-57 off, 13-12-09 on):**

| | left eye: the left ceiling lamp (tan -0.734, x 1220 of 3072) | right eye: the right ceiling lamp (tan +0.590, x 1694) |
|---|---|---|
| fix off | its flare's streak centred at x 928 (-292 px; FXR measured 1233 -> 920) | at x 2016 (+322 px) |
| fix on | at x 1197 (-23 px: the streak's own asymmetry) | at x 1702 (+7 px) |

With the fix off the left eye also shows a flare core at x 2058 and the right eye one at 2036 that belong to no lamp;
with it on they are gone. `crops.py`'s picture (`m2u-lamp-left.png` in the first run's folder): off, the glow and streak
float left of the lamp in the left eye and are missing in the right; on, they sit on the lamp in both, as in mode 1.
The same at 3072x2216 (snapshots 13-30-46 off, 13-31-01 on). The hook's log: `flare window: 191380 reflection passes,
190260 through the window's inverse, 1120 already the window's (mode 1), 0 faults` (about 2,200 a second: a dozen lamps,
two eyes, 90 frames). **Cost: none measurable** (perf-matrix, the two settings alternated: frame median 12.85/13.17 ms off,
12.79/12.97 on; GPU 3.92/3.96 off, 3.85/3.94 on).

**Life-size in every variant:** `stereo-check.py` at the same nine directions reads the same crossed disparities in all six
working variants, point for point within 0.05 degrees (+1.27 .. +1.33, +1.06 .. +1.11, +0.68 .. +0.78 degrees per eye; the
room's walls at about 2.4 m), as lane FXR's mode 2 and lane DEF's mode 1 read them: the window's shape and the image's
size change only how many pixels each eye gets.

**Sharpness, at each eye's forward direction** (pixels per tangent across x down, and per degree):

| variant | mode | window (tangents) | render | each eye's image | px/tan | px/deg |
|---|---|---|---|---|---|---|
| ME3 today (`m1a-1728`) | 1 | +-1.8418 x +-2.0720 | 3072x1728 | 2326x1108 crop | 834 x 417 | 14.6 x 7.3 |
| the eyes' window (`m1u-1728`) | 1 | +-1.8418 x +-1.3290 | 3072x1728 | 2326x1728 crop | 834 x 650 | 14.6 x 11.3 |
| mode 2 + the fix (`m2u-1728`) | 2 | +-1.8418 x +-1.3290 | 3072x1728 | 3072x1728 | 1101 x 650 | 19.2 x 11.3 |
| taller (`m1u-2216`) | 1 | +-1.8418 x +-1.3290 | 3072x2216 | 2326x2216 crop | 834 x 834 | 14.6 x 14.6 |
| taller, mode 2 (`m2u-2216`) | 2 | +-1.8418 x +-1.3290 | 3072x2216 | 3072x2216 | 1101 x 834 | 19.2 x 14.6 |
| wider, mode 1 (`m1u-4056x1728`) | 1 | +-1.8418 x +-1.3290 | 4056x1728 | 3071x1728 crop | 1101 x 650 | 19.2 x 11.3 |
| (the PS VR2 asks for) | | | | 2824x2880 | 1013 x 1085 | 17.7 x 18.9 |

`m1u-4054x1728` rendered nothing in stereo: ME3's `FSceneRenderTargets::Allocate` (`0x36CDC0`) rounds its buffers up to a
multiple of 8 (4056), so the scene's depth target never matched the 4054-wide backbuffer and the renderer's pass-0 capture
never fired (`[SFRCLEAR] clears=0 ... biggest depth seen 0x0`; the frame fell to `CAPTURE_STALL_MONO`). **ME3 renders any
shape whose sides are multiples of 8** (3072x2216, 4056x1728 both worked: the swap chain, the scene targets and the capture
at that size); the runner now refuses other sizes. The 16:9 was only ever the runner's and the menu's choice.

**Cost** (frame median / render thread median / GPU median, ms, at the still pose; the last column the other lanes'
compilers running during the captures, which add noise of about half a millisecond):

| variant | pixels | frame | render thread | GPU | compilers |
|---|---|---|---|---|---|
| `m2u-1728` | 5.3 M | 12.8 - 13.2 | 12.2 - 12.6 | 3.85 - 3.96 | 0 |
| `m1a-1728` (today) | 5.3 M | 13.0 - 13.6 | 12.4 - 12.9 | 3.93 - 4.11 | 1 |
| `m1u-1728` | 5.3 M | 13.5 - 13.7 | 12.8 - 13.0 | 4.06 - 4.13 | 1 |
| `m1u-2216` | 6.8 M | 13.1 | 12.5 | 4.08 | 0 - 1 (the other round, 5-10 compilers: 14.1 / 13.4 / 5.1) |
| `m2u-2216` | 6.8 M | 13.5 - 14.0 | 12.8 - 13.2 | 4.5 - 4.8 | 1 - 2 |
| `m1u-4056x1728` | 7.0 M | 14.3 - 14.8 | 13.6 - 13.9 | 5.1 - 5.4 | 2 |

The render thread (6,000-7,000 draws a pass) sets the pace on this machine; pixels move the GPU. **At the same pixel count
mode 1 and mode 2 cost the same** within the noise (5.3 M: 3.9-4.1 ms GPU either way; 6.8 M: 4.1-4.8). **Mode 1 at mode 2's
sharpness needs 32% more pixels** (`m1u-4056x1728`: +1.2-1.5 ms GPU, about +1.3 ms render thread, +1.5 ms a frame).

**FSR** never applied (the probe's `upscale fsr active False`): every render here is wider than the PS VR2's 2824-pixel
eye, and `EyeImageSize` scales only a picture smaller than the recommendation on both axes (section 2.3).

**Not measured:** the HUD panel under the eyes' window (Save 31 on the Normandy draws no HUD: the panel's texture was empty
in every variant); a real headset; a scene with lens flares in combat; ME2 with `windowEyes` (built for both trees, run
only on ME3).

## 4. Recommendation (the owner decides; nothing changed by default)

1. **Mode 2 back on ME3, with the flare fix** (`MELE3VR.ini [VR] MetricEyeFrustum = 1` and `ASI\LE3MEVR.ini [Stereo]
   flareWindow = 1` as ME3's defaults). The flares sit on their lamps in both eyes, the stereo is the same life-size, and
   each eye gets mode 2's 19.2 x 11.3 px/deg instead of today's 14.6 x 7.3, at no cost. ME3 then runs ME1's and ME2's mode.
   To ship it: the default of `MetricEyeFrustum` in le3 `me2_menu.cpp` `LoadValues` (or the baseline) and of `flareWindow`
   in `plugin/FlareWindow.cpp` `ReadEnabled`; the owner's headset check of a lamp-lit room (notes/headset-checklist.md).
2. **The eyes' window on both sequels** (`[Stereo] windowEyes = 1` as the default): mode 1's vertical sharpness without a
   pixel more (7.3 -> 11.3 px/deg), and in mode 2 a culling window no taller than the eyes (today's is 1.56x their height).
   Needs a look at the HUD panel in a scene that has a HUD before it becomes the default.
3. **A taller render for ME3, if the GPU allows it:** the 16:9 image is now the only thing between ME3 and the headset's
   vertical (11.3 px/deg against the PS VR2's 18.9). 3072x2216 gives 14.6 down for 28% more pixels (about +0.6 ms GPU
   here). A per-game choice of `RenderW`/`RenderH` (multiples of 8); the menu's resolution list is all 16:9 today.
4. **Not recommended:** mode 1 widened to mode 2's sharpness (4056x1728): the same picture for 32% more pixels.

**What the owner must decide:** 1 (ME3's mode and the flare fix as its defaults), 2 (the window's shape on ME2 and ME3),
3 (ME3's render size). ME1 is untouched by all three: its window is already the eyes' union and it has no UE3 lens flare.

## 5. Decided and shipped (2026-11-17, lane DEF2)

The owner: "All recommendations" (docs/trilogy-plan.md question 17): 1 yes, 3 yes, 2 after a look at the HUD panel in a scene
with a HUD. ME2 is unchanged.

**What ME3 ships now, and where each default lives:**

| | value | where |
|---|---|---|
| each eye's own frustum (mode 2) | `MELE3VR.ini [VR] MetricEyeFrustum = 1` | le3 `me2_menu.cpp` `LoadValues` (`GetPrivateProfileIntW(..., 1)`), `me2_xr.cpp` `g_mevrEyeFrustum{true}` |
| the flare fix | `ASI\LE3MEVR.ini [Stereo] flareWindow = 1` | `plugin/FlareWindow.cpp` `ReadEnabled` (1 when missing) |
| the render size | `MELE3VR.ini [VR] RenderW = 3072`, `RenderH = 2216` | le3 `d3d_capture.cpp` `ArmResolutionSpoof` (the code default when the keys are missing; `0` still leaves the game's own); Halcyon's classic menu lists it first |
| the installed renderer ini | the runner's preview text without `Diagnostics` | `src/halcyon/le3/runtime/MELE3VR.ini` (GPL-3.0, as the tree): `tools/deploy-asi.ps1 -Game LE3` copies it next to `MassEffect3.exe` when there is none, `tools/package-release.ps1 -Games LE3` ships it as `payload\MELE3VR.ini` for MEVR Setup; `test-session.ps1` checks it equals the preview's defaults |
| GamerSettings' ResX/ResY | 3072x2216 | `tools/vr-game-settings.ps1 -Game LE3` without `-ResX/-ResY` reads `MELE3VR.ini`'s RenderW/RenderH, else 3072x2216; MEVR Setup's uninstall knows them as the renderer's (`GameSpec::rendererDefaultW/H`, `settings::ModWrites`) |
| the runner | `prep.ps1 -RendererPreview` defaults | `-PreviewRenderSize 3072x2216`, `MetricEyeFrustum=1` written; `-PreviewSharedWindow` writes 0 (mode 1); `-PreviewEyeFrustum` still accepted. A stretch prepared before (no key in its file) fails `Assert-PreviewConfig` and must be restored |
| the eyes' window | `[Stereo] windowEyes = 0` | unchanged: after the HUD check |

No migration: no player has an ME3 install yet, and nothing wrote `MetricEyeFrustum = 0` except Halcyon's classic menu when
closed since lane DEF (that file is the player's).

**Seen on the fake headset** (Save 31 on the Normandy, the runner's `-RendererPreview` with prep's defaults only and no
`[Stereo]` key, lane FXR's still level PS VR2 recording replayed with `-Fovs`; run `%LOCALAPPDATA%\MEVR\def2-lane\runs\
LE3-20261117-144209`, script `def2-lane\scripts\me3-def2.ps1`, session `20261117-144209-867-LE3-def2`, restored with 0
differences):
- The deploy installed the runtime file (`Installed ME3's MELE3VR.ini ...`), then prep wrote the preview's;
  `vr-game-settings.ps1` wrote ResX 3072, ResY 2216.
- Logs: `[VRRES] user render override 3072x2216`, `per-eye swapchain 1536x2216`, `[SFRCLEAR] ... biggest depth seen
  3072x2216 ... backbuffer 3072x2216`, `AER full-size eye swapchains created 3072x2216`; `[MEVR-STEREO] ME3's frustum: each
  eye's own ... (mode 2; ... MetricEyeFrustum = 1, ME3's default ...)`; `flare window: hooked the reflection pass; ... placed
  through the CPU's own window ([Stereo] flareWindow = 1)`; with the PS VR2's eyes `stereo mode 2: life-size, each eye's own
  frustum`, `pass 0 frustum: tan L -1.8418 R 0.9472 U 1.3290 D -1.3290 ... the CPU window +-1.8418 x +-2.6571 holds it`.
- Each eye's projection layer `[0, 0, 3072, 2216]` at its own FOV (-61.5 / 43.4 / +-53.0 degrees and the mirror): 1101 x 834
  px per tangent, **19.2 x 14.6 px/deg** (the 16:9 mode 1 had 14.6 x 7.3).
- **The flares** (`analyse.py --flares`, `crops.py`: `lamp-left.png`, `lamp-right.png` in the run folder): the defaults put the
  left lamp's streak at -25 / -22 px (the streak's own asymmetry, FL's -23) in the left eye and the right lamp's at +5 px in
  the right; the canary, `flareWindow = 0` written live, put them off their lamps (the left one's flare far left in the left
  eye, missing in the right; the stray cores at x 2058 and 2036 back), and the defaults again put them back (-27 / +4 px).
- **Life-size:** `stereo-check.py` at FL's nine directions: +1.29 +1.21 +1.19 / +1.08 +1.06 +1.06 / +0.77 +0.80 +0.69 degrees
  per eye crossed (FL: +1.27 .. +1.33, +1.06 .. +1.11, +0.68 .. +0.78).
- **Found:** with the aspect window (`windowEyes` off) the taller render makes the CPU window taller too: the fill's aspect is
  the backbuffer's half width over its height (1536 / 2216 = 0.69), so the cover that holds both eyes is +-1.8418 x +-2.6571,
  twice the eyes' height (+-1.3290); at 3072x1728 it was +-2.0720. Only culling sees it (each eye renders its own frustum),
  and its cost was not measured (FL's 3072x2216 runs used the eyes' window). `windowEyes = 1` brings it to the eyes'.
- ME1 and ME2: no code they run changed. The le3 tree, `plugin/FlareWindow` and `src/halcyon/le3/runtime` are ME3's only; the
  ME1 and ME2 builds of `src/mod/Mod.cpp`, `src/game/Melee.cpp` and `src/plugin/MetricStereo.cpp` preprocess (`cl /EP`, the
  targets' own flags) to the same text as before (ME3's build of `Mod.cpp` differs, the canary); the deploy and
  `vr-game-settings.ps1` changes are LE3 branches; Setup's LE1 and LE2 paths are unchanged (`GameSpec`'s new fields are 0
  there; the setup tests pass).

## 6. The render size from the headset, ME2 and ME3 (lane AU4, 2026-11-18)

The owner (docs/trilogy-plan.md "Answered 2026-11-18"): "Follows headset like me1". ME1's rule: at the first VR start the
render target becomes 0.85 of the runtime's recommended eye size (FSR on), written for the next launch, once
(`[Display] eyeSizeSet`). Section 5's fixed 3072x2216 (and ME2's 4096x2304) is the stand-in that rule replaces.

**What each game renders.** In mode 2 every game renders each eye's own field into the whole target: ME1's `sfr2ResX/Y`, the
sequels' backbuffer (the projection layer's image rect is `[0, 0, W, H]` at that eye's own FOV, section 5). So 0.85 of the
eye covers on a sequel exactly what it covers on ME1; only the engine's constraints on the target differ. (Mode 1, the shared
symmetric window, crops each eye out of a larger window and gets fewer pixels from the same size; it is opt-in on ME3 and the
rule does not compensate for it.)

**The constraints, measured on the fake headset** (SteamVR's null driver at its auto resolution recommends 1912x1952; 0.85 of
it is 1624x1656):
- **ME3** draws everything at a near-square size: 1624x1656 in the combat fixture (`me3-combat.ps1`, Cerberus troopers) gave
  the whole HUD on the panel (172 UI draws a frame: power bar, weapon, health arc, damage arcs), stereo, FSR from 1624x1656 to
  the 1912x1950 eye images. Sides in multiples of 8 (section 3's `FSceneRenderTargets::Allocate`).
- **ME2** renders any multiple of 8 (1624x1656, 2208x1656, 2944x1656 all made their swap chain, the scene targets and the
  pass-0 capture; the eyes differ, mode 2), **but draws no HUD on a near-square frame**: at 1624x1656 the HUD panel stayed
  empty (`[UIGATE] ui=30` in 4 s, and no HUD in the eyes either), at 4:3 (2208x1656) 13,710 and at 16:9 (2944x1656) 14,910
  UI draws in 4 s, the HUD complete and laid out by its edges. So ME2's target is at least 16:9 (`plugin/RendererLe2.cpp`
  `minAspect`), widened at 0.85 of the eye's height; 4:3 is enough for the HUD and is the owner's choice to make (below).
- **The engine writes its in-memory ResX/ResY back at exit** (ME2: GamerSettings said 4096x2304 after the first launch's quit
  although the rule had written 1624x1656 during it). `ArmResolutionSpoof`'s attach-time write of the renderer's
  `RenderW/RenderH` lands before the engine reads its settings in our host (the second launch rendered 1624x1656 with
  GamerSettings saying 4096x2304 at its start); ME3's exit hook writes the new size too (`g_vrresW/H`).

**The rule** (`core/vr/RenderTarget`, unit-tested; `plugin/MainMenuSettings` `FirstStartEyeSize` and `SetResolution` for the
sequels; `renderer::GetRenderTarget`/`SetRenderTarget` over the trees' `[MEVR]` `MevrRenderTarget`/`MevrSetRenderTarget`, which
write `MELE2VR.ini`/`MELE3VR.ini` and GamerSettings.ini): no setup's choice to wait for (MEVR Setup leaves a sequel's size to
its renderer), once per install. Logs `first VR start:` and Halcyon's `[VRRES] [MEVR] next launch WxH`. The menu's Display >
Render resolution and Upscale rows work on the sequels; Performance and Steady cutscenes stay greyed (ME1's levers, Halcyon
LE1's cap). The runner pins `[Display] eyeSizeSet = 1` (ME2's prep unless `-FirstVrStart`, ME3's preview always), so a
stretch's launches keep one size.

| headset (recommended eye) | ME1 | ME3 | ME2 (at least 16:9) | ME2 at 4:3 |
|---|---|---|---|---|
| PS VR2 (2824x2880) | 2400x2448 | 2400x2448 | 4352x2448 (10.7 MP; today 9.4) | 3264x2448 (8.0 MP) |
| Quest 3 over Link (2064x2208) | 1754x1876 | 1752x1880 | 3344x1880 (6.3 MP) | 2504x1880 (4.7 MP) |
| the fake headset (1912x1952) | 1626x1660 | 1624x1656 | 2944x1656 (4.9 MP) | 2208x1656 (3.7 MP) |

**Cost** (`tools/perf-probe.ps1`, 20 s, medians; the fake headset's SteamVR holds the game at a third of 90 Hz, so these are
the work per frame, not the rate):

| game, place | size | render thread | GPU (at most) | game thread |
|---|---|---|---|---|
| ME2, Save 4 standing | 4096x2304 (before) | 10.02 ms (p90 11.99) | 8.72 ms (p90 9.02) | 2.48 ms |
| ME2, Save 4 standing | 2944x1656 (the rule) | 8.08 ms (p90 10.52) | 6.13 ms (p90 6.43) | 2.36 ms |
| ME3, the combat fixture | 3072x2216 (before) | 25.73 ms (p90 33.37) | 6.53 ms (p90 7.84) | 3.52 ms |
| ME3, the combat fixture | 1624x1656 (the rule) | 15.81 ms (p90 19.06) | 7.12 ms (p90 11.56) | 2.09 ms |

ME3's two captures are of a fight that moves (not the same pose): the render thread's drop is mostly the scene, and the GPU's
"at most" is no lower; read them as "no regression", not as a gain. ME2's are the same standing pose. Sessions
`20261118-103825-020-LE2-au4`, `20261118-105802-821-LE3-au4`, `20261118-110207-456-LE3-au4sq`.

**Not verified:** a real headset; the PS VR2's own size on either sequel (the fake headset recommends SteamVR's auto size);
ME2's screens other than the HUD at a widened 16:9 (the menus, the conversation wheel, the GUI screens: all seen at 16:9
before); ME3's GUI screens and our measured crops at a near-square frame; ME2's FSR (a 16:9 target wider than the eye is
copied, not upscaled: `EyeImageSize` scales only a picture smaller on both axes). **For the owner:** ME2 at 16:9 (every ME2
screen has been seen at that shape; on the PS VR2 it renders 13% more pixels than today's 4096x2304) or at 4:3 (the HUD
checked; 25% fewer pixels than 16:9).
