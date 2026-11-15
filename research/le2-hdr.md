<!-- SPDX-License-Identifier: MIT -->
# HDR on ME2 (and LE1, LE3): what a headset can get, what the renderer needs, what it buys (2026-11-14)

**The owner's request (2026-11-14):** "Ideally, I want to see if we can support HDR just because it looks so much better
for VR than SDR. I recognize that the renderer doesn't necessarily support it, but I am curious if we can pull from their
code base to see what we can and can't use and try to actually support HDR natively."

**The owner's decision the same evening:** ME2 runs the game's SDR frame (`[Colour] hdrDisplay = -1`, LE2's default,
lane B2) **as a stop gap** ("If it's cheap to do sdr, then we can do that as a stop gap while you figure out HDR"). Step 6
of the plan below is the step that sets the default back, and what has to hold first.

**Stopped later that evening (the owner: "We can take the HDR lane out of play. I'm just trying to figure out how we can
get the best picture, when we know that me2 has a lot of dark scenes").** The plan below is kept as the study; none of its
steps landed (Steps 1-2 were written and set aside). Both games answer the HDR question SDR by default now (section 7, the
launch checks on this HDR desktop), and the picture work moved to [research/le2-picture.md](le2-picture.md).

Where this note starts: [research/27](27-colour.md) sections 7b-7d (LE1's float path, `hdrDisplay = 1`, the tonemaps and
their verdict) and [research/le2-renderer-integration.md](le2-renderer-integration.md) "The black eyes" (lane B2: why ME2's
eyes were black, `plugin/EyeCopy`, `hdrDisplay = -1`). **[verify]** marks what a run or a headset still has to settle,
**[guess]** a reasoned guess, **not found** what was searched for and not found.

## Verdicts

1. **Output: no headset the owner has can show HDR from a PC, and no consumer PC headset is documented to.** OpenXR
   composites in linear light and treats every non-sRGB swapchain format as linear values; there is no ratified HDR
   extension. Two Sony vendor extensions appeared in the SDK this year (`XR_SONY_swapchain_color_space` 2026-07-31,
   `XR_SONY_hdr_metadata` 2026-09-01: PQ, HLG and extended-sRGB swapchains, ST 2086 metadata), but no runtime in
   Khronos' inventory lists them (SteamVR Windows, SteamVR Steam Frame, Meta PC, Varjo, Android XR, WMR, Monado). The PS
   VR2's PC adapter lists HDR among the features that don't work on PC (Sony). SteamVR on this machine offers 9 D3D11
   swapchain formats; which ones is **[verify]** (section 2.2). So "HDR for VR" today can only mean: render the game's
   HDR range inside and map it well to an SDR panel.
2. **Rendering: the float frame breaks three things in Halcyon's ME2 renderer, and the fix is to read the right texture,
   not to tonemap the swap chain.** LE's "HDR10_Float" frame is not a scene-referred render: it is **the game's own SDR
   frame, reconstructed** by one full-screen pass, `FReconstructHDRPixelShader` (section 1). Before that pass the frame is
   an FP16 copy of the SDR picture (UI included), drawn into a float target that is not the swap chain. That is why the SFR
   left-eye capture finds nothing (it groups scene clears by draws into the swap chain, and in float there is exactly one:
   the reconstruct pass), why the UI treatment never runs (it wants a format-28 target), and why a plain copy is black.
   LE1's tonemaps (`MEVR_TONE`: clip, ACES, Hable, Reinhard, hue kept) port as code but are the wrong tool for this source;
   `hdrDisplay` ports (lane B2 already did it, with `-1` added).
3. **Quality: on an SDR panel the float frame can at best equal the game's SDR frame, plus some precision.** The
   reconstruction expands the SDR picture's top quarter into highlights (SDR codes from about 187 up become brighter than
   paper white), decodes it with a pure 2.2 gamma (the headset's SDR path decodes sRGB), boosts saturation about 5% (11% in
   highlights), and with the game's HDR contrast above 1 crushes the shadows. Every curve applied to it loses what the SDR
   frame had: modelled at Freedom's Progress, Halcyon's paper-white clip keeps 110 of the SDR frame's 473 colours in the
   darkest patch (37.5% black against 2.7%), LE1's ACES 13 (79% black). The exact inverse of the reconstruction gives the
   SDR frame back, and tapping the pass's FP16 source gives it with no math at all and more precision than 8 bits (the
   measured float frame's bright patch has 2.6x the distinct colours a reconstruction of the 8-bit frame has). That
   precision, carried to the headset through the dither or a float eye swapchain, is the one real gain for an SDR headset;
   real HDR waits for a runtime and a headset (Step 7).

## 1. What LE's HDR frame is: the SDR frame, reconstructed

**Found in all three games' shader caches.** `BioGame\CookedPCConsole\GlobalShaderCache-PC-D3D-SM5.bin` names
`FReconstructHDRPixelShader<FALSE/TRUE>`, `FReconstructHDRVertexShader`, the Scaleform UI's `FGFxPixelShaderHDRGFx_*`
(constant `HDRBrightnessScale`), `FBinkGpuShaderHDR` (movies) and `FCalibrationBoxHDRPixelShader`. The pixel shader's
bytecode (DXBC, its reflection kept: cbuffer `HDRParamsIn`, struct `FHDRParameters`) is byte-identical in ME1, ME2 and
ME3 (disassembled locally with `D3DDisassemble`; the disassembly is game code and stays out of the repository). Its only
texture is `SourceTexture` (t0); its constants:

| `FHDRParameters` (48 bytes, b0) | where it comes from |
|---|---|
| `MaxReconstructedNits` 1000, `ReconstructedColorSaturation` 0, `PaperWhiteNits` 250 | `BIOEngine.ini [HDR]` (ME1 and ME2 both: `MaxReconstructedNits=1000`, `PaperWhiteNitsScene=250`, `PaperWhiteNitsUI=900`, `MaxBrightness=1000`, `Contrast=1.0`, `UseGamutExpansion=TRUE`; ME2 also `ReconstructedColorSaturation=0`) |
| `UIBrightnessScale`, `DisplayGamma` 2.2, `HDRGamma` | `HDRGamma` 1.25 in RenoDX's annotation of a live game; on this install most likely 1.0 (section 4.2) **[verify]** (Step 3 reads it) |
| `SoftShoulderStart2084`, `MaxBrightnessOfDisplay2084`, `MaxBrightnessOfScene2084` | PQ code values: RenoDX's live values 0.508, 0.728, 0.790 are 100, 800 and 1414 nits; 1414 = (1000/250)^1.25 x 250, the brightest the reconstruction makes. The display's peak comes from DXGI (our `hdrDisplay` hook answers it) |
| `bIsHDR10`, `bUseGamutExpansion` | 0 for the float (scRGB) swap chain, 1 |

**What the pass does**, in order (written from the bytecode's behaviour; implement from this description, never from the
disassembly):
1. `x = SourceTexture^DisplayGamma`: the SDR picture's encoded values decoded with a **pure 2.2 gamma**.
2. On the pixel's Rec. 709 luminance Y (hue kept, since `ReconstructedColorSaturation` is 0): an **inverse tonemap**
   `F(Y) = Y / (1.0001 - Y)` up to Y = 0.5 (linear), then `b + a Y / (a - Y)` with `k = MaxReconstructedNits /
   PaperWhiteNits`, `a = (k + 0.0001) / (k - 0.9999)`, `b = 1 - 0.5 a / (a - 0.5)`: 0.5 maps to 1.0 (paper white), 1.0
   to about k + 0.2. The pixel is `x * F(Y) / (Y + 0.0001)`.
3. **Gamut expansion:** two fixed 3x3 matrices into BT.2020, blended by `saturate((max channel - 2) / 8)`; with the
   final BT.2020-to-709 matrix their net effect is about +5% saturation everywhere and up to +11% above twice paper white
   (blue +10%).
4. `^HDRGamma` (the HDR "contrast"), times `PaperWhiteNits`, into PQ; above `SoftShoulderStart2084` a quadratic shoulder
   toward the display's peak (kept only where it is below the input), back out of PQ.
5. Out: PQ values for HDR10, or for the float swap chain scRGB (x 125 = nits / 80) in BT.709 primaries.

**What that means.** The game renders the scene, tonemaps it with its own SDR chain (ME2: bloom, `1 - 2^(-1.7 x)`, the
filmic 1D LUT from `[FilmicTonemappingLUT]`, the 16^3 colour-grading LUT, gamma: RenoDX's `tonemap_me2_*` shaders show the
chain), and in HDR mode only re-expands that finished SDR picture. Its HDR is the artists' SDR grade, inverse-tonemapped.
Everything the SDR frame has is in the float frame; nothing scene-referred is added. RenoDX's Mass Effect LE mod (MIT,
ShortFuse, all three games) replaces exactly this pair: the games' tonemap shaders by hash, and this pass (its
`output_0x0765601C`) to pass its own HDR through.

**Why research/27 7d measured what it did on LE1.** The float frame through Halcyon's clip or our ACES lost the sky's
gradation (50,108 colours against 18,672) and crushed the darkest patch (5,853 colours and mean 17.6 against 408 and 1.9,
9.1% black): the paper-white clip whitens SDR codes 187 and up; the 2.2 decode where the headset's SDR path decodes sRGB
darkens the bottom codes; `HDRGamma` above 1 darkens them further; the gamut expansion is the extra saturation 7d found
(0.60 against 0.46) and the reds ACES pushed. 7d's open question ("the game's own grading LUT on the float path") has its
answer: the grade is already in the float frame, under the reconstruction.

**Why ME2's frame structure changes in float** (Halcyon's own `[CLEARMAP]` lines, this machine, Diagnostics on): in the
8-bit frame each SFR pass draws 76 times into the swap chain (`clears=5 bbDraws=152 ... 27fa0@0 27fa0@0 27fa0@76
27fa0@76* 27fa0@76`: two groups, the capture `*` at the second); in the float frame `clears=5 bbDraws=1 ... 6f8e0@0
6f8e0@0 6f8e0@0 6f8e0@0 6f8e0@0`: every pass draws into the FP16 intermediate, and the one swap-chain draw is the
reconstruct pass after both passes. The UI gate's telemetry agrees: `[UIGATE] calls=71550 ui=0 pre=43950 vp=0 blend=27600
... fmt=10 rtvW=4096 bbW=4096`: the UI's draws land on a format-10 target the size of the swap chain, and the gate's
format test refuses them. Halcyon's `[ME2DISC] OMSetRenderTargets` lines mix runs of both kinds, so which draws go where
in the float frame is **[verify]** (Step 3 logs it).

**Why only ME2 goes float on this machine.** Lane B2: with Windows HDR on and `DesiredRange = DynamicRange_SDR`, ME2 makes
a float swap chain in gameplay, LE1 stays 8-bit. Whether LE3 behaves like ME2 is **[verify]**; if it does, LE3's own
renderer (`core/vr/Output`) needs the same stop gap (Step 0).

## 2. Output: can a headset get HDR at all? (question 1)

### 2.1 Facts

| What | Source |
|---|---|
| OpenXR composites submitted layers in linear colour; sRGB-format images are decoded; "All other formats will be treated as linear values." | OpenXR 1.1, `XrSwapchain` |
| `xrEnumerateSwapchainFormats` lists formats "in order from highest to lowest runtime preference"; runtimes should offer R8G8B8A8 and its sRGB form. Nothing about float or HDR. | OpenXR 1.1, `xrEnumerateSwapchainFormats` |
| No ratified HDR extension. Sony vendor extensions: `XR_SONY_swapchain_color_space` (SDK 1.1.62, 2026-07-31): per-swapchain colour space, `EXTENDED_SRGB_LINEAR`, `BT2020_LINEAR`, `BT2020_PQ`, `BT2020_HLG`, Display P3, DCI-P3, enumerated per format; `XR_SONY_hdr_metadata` (1.1.63, 2026-09-01): SMPTE ST 2086 / CTA-861.3 static metadata per swapchain. Both "not ratified", contributors Sony Corporation, last modified 2026-02-10. | OpenXR-SDK `openxr.h` 1.1.63 (`external/OpenXR-SDK`), OpenXR-Docs `sony_*.adoc`, SDK changelog |
| No runtime in Khronos' inventory lists either Sony extension. Meta PC and Android XR (Galaxy XR) list `XR_FB_color_space` (gamut: Rec.709, Rec.2020, P3..., not dynamic range) and `XR_KHR_composition_layer_color_scale_bias`; SteamVR Windows (37 extensions), SteamVR Steam Frame (44), Varjo, Monado: no colour extension. | KhronosGroup/OpenXR-Inventory `runtimes/*.json` (updated 2026-10-08) |
| OpenVR has `Prop_Hmd_SupportsHDR10_Bool` (2093), a driver-side HMD property, since SDK 1.26.7 (2023-08-08); its colour spaces are Auto, Gamma, Linear. No documentation of what SteamVR does with it, and no driver known to set it (not found). | `openvr.h` history |
| PS VR2 on PC: "HDR, headset feedback, eye tracking, adaptive triggers, and haptic feedback (other than basic rumble)" are not available. | PlayStation Direct's adapter listing, as reported by VGC, Windows Central, UploadVR |
| Meta's PC runtime takes floating-point eye buffers (R11G11B10, R16G16B16A16) and "will use the extra precision ... to eliminate color banding in dark areas" (2017, Rift). Meta staff (2019): R10G10B10A2 "is linear and will give you results that are worse than 8-bit sRGB". Quest panels: LCD, about 100 nits, 8-bit sRGB (research/27 section 3). | Meta tech note; Meta developer forum |
| Steam Frame: LCD 2160x2160 per eye; no HDR claim. Varjo XR-4: mini-LED, 200 nits, local dimming; no HDR claim for the display. Pimax Crystal Super (QLED): mini-LED, 280 nits, local dimming; no HDR10 claim. | Tom's Hardware, Varjo, Pimax / retailers |
| SteamVR 2.18.4 on this machine (the null driver) offers **9** D3D11 swapchain formats to Halcyon's ME2 session; it picks 29. | `MELE2VR_Log.txt`: `[XRAPI] swapchain format chosen=29 (SRGB preferred; offered 9)`; `xrclient_MassEffect2.txt` |

### 2.2 Not settled

- **Which 9 formats** (whether 10, R16G16B16A16_FLOAT, or 26, R11G11B10_FLOAT, are among them) and **whether SteamVR
  exposes the Sony extensions**: **[verify]**, one probe run (Step 5.1). Both runs of this study found the game lock held
  (lanes B2 and C), so it wasn't run; the probe exists as a scratch build (it enumerates the instance extensions, then each
  format with a test swapchain).
- **What SteamVR's and Meta's compositors do with values above 1.0 in a float swapchain: [guess] clip.** They composite
  in linear and drive an SDR panel; no vendor statement of HDR composition was found. An ALVR developer adding HDR
  support found `hello_xr`'s colours "ill-defined and inconsistent, especially across different swapchain formats and
  headsets" (OpenXR-SDK-Source #467, 2024).
- **HDR on any PC headset: not found.** A forum claim that Apple Vision Pro (ALVR) and Quest 3 (ALVR) take HDR is
  anecdotal; ALVR's visionOS listing doesn't say it. The Pimax Crystal Super's micro-OLED variant is "said to have HDR
  capability" by one retailer **[verify]**.

**Verdict 1:** no HDR to any headset the owner has (PS VR2 on PC: HDR disabled; Quest: SDR LCD over an 8-bit or 10-bit
video stream). The API to say "this swapchain is PQ / scRGB" now exists as Sony's vendor extensions, which reads like work
toward the PS VR2 or Sony's own headsets on PC **[guess]**; until a runtime ships it, nothing we render reaches a panel as
HDR. Watch for it with the probe (Step 7).

## 3. Rendering on the float frame (question 2)

### 3.1 What breaks in Halcyon's ME2 renderer, and why

| Piece (file, function) | 8-bit frame | float frame | The fix (Step 4) |
|---|---|---|---|
| Eye copy: `src/halcyon/le2/me2_xr.cpp` `CopyTextureToEyeFullFrame`, the flat panel's `CopyResource(g_monoImages[idx], backbuffer)` | `CopyResource` 28 into the eye images (format 29 swapchain, a format-27 typeless image) | dropped by D3D11 between families: black or stale eyes | lane B2's `MevrBridge::CopyIntoImage` -> `plugin/EyeCopy` draws it through an sRGB view (no scaling: scRGB 1.0 = 80 nits, so paper white is 3.1x over and clips). Later: copy the FP16 SDR intermediate instead (raw, dithered) |
| SFR left eye: `src/halcyon/le2/d3d_capture.cpp` `SfrMaybeCapturePass0AtClear` (from `MhClearDepthStencilView`), `SfrEnsurePass0Tex`; `me2_xr.cpp`'s SFR submit (`GetSfrPass0Texture`, `leftSrc`/`rightSrc`) | scene clears grouped by `g_sfrBbDrawsThisPresent` (draws into the swap chain); at the second group the swap chain holds pass 0's composite | every clear at `bbDraws` 0: one group, `g_sfrPassOpens` never reaches 2, `[SFRDIAG] totalCaps=0`; both eyes the swap chain's one picture | count draws into the FP16 intermediate ("frame target") and snapshot it, not the swap chain |
| Right eye | the swap chain at Present (pass 1's composite) | the reconstructed scRGB frame | the frame target at the reconstruct draw (pass 1's composite, SDR-encoded FP16) |
| UI treatment: `IsUiDrawNow` (`g_currentRtvFormat == DXGI_FORMAT_R8G8B8A8_UNORM && g_currentRtvWidth == g_backbufferWidth`), `CaptureUiCompositeAndSkip` (colour formats 28/29/87/91) | the HUD shrunk into the middle of the eye | `[UIGATE] ui=0`, `pre` refusals on `fmt=10`: the HUD full screen in both eyes | accept the frame target by pointer; the HDR GFx shaders may draw the UI brighter than SDR (**[verify]**, `HDRBrightnessScale`) |
| HDR display answer: lane B2's `MevrBridge::OnFactory` patches `IDXGIOutput6::GetDesc1` (slot 27) | `[Colour] hdrDisplay = -1` answers SDR: the game stays 8-bit | `0` (the display's own answer) on an HDR desktop: float | unchanged; its default is Step 6's |

### 3.2 LE1's path, and what of it ports

LE1's float path exists for `hdrDisplay = 1` (research/27 7c): `src/halcyon/le1/d3d_capture.cpp` `OutputGetDesc1Hook` ->
`MevrBridge::HdrDisplayDesc` (`src/plugin/HalcyonBridge.cpp`); `src/halcyon/le1/xr_session.cpp`: `IsFloatColorFormat`,
`SamplesLinear`, `CopyCompatibleWithEyes` (a float source always blits), `HdrSourceScale` (`colorScale.x = 80 /
PaperWhiteNitsScene` once `HdrProbeTick` latched values above SDR range; `GamePaperWhiteNits` reads GamerSettings.ini then
BIOEngine.ini `[HDR]`), the eye blit's `ToEye` with the `MEVR_TONE` seam, `[MEVR] colour` (`MevrBridge::EyeColourShaders`,
`EyeColourShader`, `EyeColourActive`, implemented in `src/plugin/Colour.cpp`); the curves and their CPU reference in
`src/core/vr/Colour.cpp` (`ShaderPrelude`'s `MevrTone`, `Tone`, `MakeConstants`).

| LE1 piece | Ports to ME2 as it is? |
|---|---|
| `core/vr/Colour` (settings, presets, prelude, dither, the CPU reference) | **yes**: game-independent, ours. The dither is what turns the float frame's extra precision into noise instead of bands |
| `MEVR_TONE` curves (clip, aces, hable, reinhard; `toneHue`, `toneDesat`, `toneSat`) | **the code yes, the use no**: they are curves for scene-referred light; on a reconstructed SDR frame they lose what the SDR frame had (section 4). A `game` mode (the exact inverse, Step 2) is the right curve here |
| The seam itself (`ToEye` in Halcyon LE1's blit shader source) | **no**: Halcyon ME2 has no blit shader (it copies). Lane B2's `EyeCopy` (over `core/d3d11/Blit`) is where a variant goes, compiled with our prelude like `EyeColourShaders` does for LE1 |
| `hdrDisplay` (`HdrDisplayDesc`) | **ported** by lane B2 (`OnFactory`), plus `-1` |
| `HdrSourceScale`, `GamePaperWhiteNits`, `HdrProbeTick` | **the idea yes**: ME2's BIOEngine.ini has the same `[HDR]` keys. Better: read the live `FHDRParameters` (Step 3) |
| `plugin/Colour` (the `[Colour]` sections, the menu's Display rows) | **after M2/M4**: its bridge calls go through the renderer interface; LE1-only today |

**Verdict 2:** three things need the float frame's structure (left-eye capture, UI, right eye) and one needs a different
copy; all four are solved by the same idea: **take the reconstruct pass's source texture** (the FP16 SDR picture with the
UI) as the frame, never the reconstructed swap chain. The exact inverse (Step 1) covers what is only available as the swap
chain (LE1's existing blit, ME2's flat panel).

## 4. Quality (question 3)

### 4.1 Method

Lane B2's two snapshots at Freedom's Progress after `load.ps1` (the same save, the fake headset's still head, 4096x2304
per eye; `%LOCALAPPDATA%\MEVR\snapshots\2026-11-14_20-49-55-730-source-left.png`, 8-bit, `hdrDisplay = -1`, and
`2026-11-14_20-44-58-588-source-left.png`, float through EyeCopy, `hdrDisplay = 0`). A model of section 1's pass
(numpy, from the bytecode's behaviour; scratch, not in the repository: Step 1 is its tested replacement) takes the 8-bit
frame, makes the float frame the game would make from it, and applies each eye mapping. research/27 7d's measures: the
brightest and darkest 400x400 windows of the 8-bit frame (by mean luma, 100-pixel grid), distinct colours, mean luma,
spread, black share; whole-frame mean, share of pixels with a channel at 255, share with luma under 8. A game run for a
same-pose float-against-8-bit capture through a correct float path waits for Step 4's code; this measures the mappings
exactly instead.

### 4.2 The model checked against the game's float frame

| | mean RGB | any channel 255 | luma < 8 | bright window colours (mean) | dark window colours, black |
|---|---|---|---|---|---|
| **measured** float frame through EyeCopy | 80 / 86 / 96 | 8.4% | 20.7% | 23,977 (220) | 306, 27.6% |
| model, `HDRGamma` 1.0, display 400 nits | 85 / 93 / 105 | 10.0% | 17.7% | 9,141 (225) | 295, 22.9% |
| model, `HDRGamma` 1.25, display 800 nits (RenoDX's constants) | 66 / 71 / 82 | 6.3% | 28.0% | 9,540 (204) | 72, 54.7% |

`HDRGamma` 1.0 fits; 1.25 (RenoDX's machine, the HDR contrast raised) doesn't. The owner's monitor reports 400 nits
(lane B2's log `hdr display: ... 400 nits`). The measured bright window has 2.6x the model's colours: the model starts from
8-bit values, the game from its FP16 intermediate. That is the float frame's precision, measured.

### 4.3 What each mapping makes of the float frame (model, `HDRGamma` 1.0, 400 nits)

| mapping of the float frame | mean RGB | any 255 | luma < 8 | bright window (spread) | dark window (mean), black |
|---|---|---|---|---|---|
| **the game's SDR frame** (8-bit, what `hdrDisplay = -1` shows) | 51 / 55 / 62 | 0.04% | 13.4% | 10,639 colours, mean 125 (27) | 473 (5.0), 2.7% |
| EyeCopy as built (scRGB clipped at 1.0) | 85 / 93 / 105 | 10.0% | 17.7% | 9,141, 225 (48) | 295 (2.2), 22.9% |
| Halcyon's paper-white clip (LE1's default) | 51 / 55 / 63 | 0.44% | 25.8% | 10,444, 143 (37) | 110 (0.6), 37.5% |
| LE1's ACES, hue kept, `toneSat` 0.7 | 56 / 57 / 63 | 0% | 33.3% | 9,201, 164 (41) | 13 (0.1), 79.4% |
| **exact inverse** (Step 1) | 51 / 55 / 62 | 0.04% | 13.4% | 10,635, 125 (27) | 437 (5.5), 2.7% |

How the paper-white clip shows each SDR code (encoded values the headset gets): 10 -> 2, 25 -> 18, 50 -> 47, 118 -> 130,
186 -> 250, 190 and up -> 255. With `HDRGamma` 1.25: 25 -> 5, 50 -> 28, 118 -> 110, 187 and up -> 255.

The inverse's round trip: within one 8-bit step for 96.8% of pixels; 99.8% of the misses are under luma 16, where the
pass's `+0.0001` on Y flattens the bottom codes (a numeric solve recovers them), and in a 52^3 colour cube 0.5% miss,
the worst (17 steps) at pure primaries, where the gamut matrices go negative and the pass takes `abs()` (rare in game
frames; clamp-aware solve in Step 1).

**What the float frame buys on an SDR panel:**
- **Nothing the SDR frame lacks in content.** It is the SDR frame; a curve over it is a re-grade, and every curve tried
  loses shadows (the 2.2-against-sRGB decode alone crushes codes under about 25) or highlights (the clip whitens the top
  quarter).
- **Precision.** The FP16 intermediate has no 8-bit quantisation (2.6x the distinct colours in the bright window). Through
  an 8-bit sRGB eye with the dither, it becomes noise instead of bands in dark gradients (Meta's 2017 note: what floating
  eye buffers are for); through a float eye swapchain, the runtime gets it all (whether the panel link or the video
  encoder keeps it is **[verify]**: Steam Link on NVIDIA is 8-bit (research/27 3), Virtual Desktop has 10-bit HEVC and AV1,
  the PS VR2's DisplayPort depth is not found).

**What would buy more, and why not now:**
- **The game's own tonemapper, replaced** (RenoDX's way: the `tonemap_me2_*` shaders by hash take the scene colour and the
  bloom before the game's curve, and output the SDR grade's look with the scene's highlight range, `renodx::draw::
  ToneMapPass(untonemapped, graded)`). For an HDR display that is real scene-referred HDR. For an SDR headset it is a
  different highlight roll-off: a re-grade of the artists' work, not fidelity. And its HLSL files are 3Dmigoto
  decompilations of the games' shaders: game code we may read but never copy into this repository (CLAUDE.md). An
  equivalent of our own would reimplement the game's post chain (bloom, depth of field, both LUTs, vignette, grain) per
  shader variant: L, and only worth it with an HDR headset.
- **An HDR headset** (Step 7).

**Verdict 3:** keep the game's SDR picture as the reference. The float path becomes worth running when it equals that
picture (Step 4, measured at the same pose) and carries its extra precision to the eye (Step 5); real HDR needs a runtime
that takes it (Step 7).

## 5. What can be reused and what can't

| Can | From | For |
|---|---|---|
| The `hdrDisplay` hook (`IDXGIOutput6::GetDesc1`, slot 27) | LE1 `OutputGetDesc1Hook`/`HdrDisplayDesc`; LE2 lane B2's `OnFactory` | choosing the game's range per headset; LE3 needs its own (Step 0) |
| `core/vr/Colour` (prelude, dither, settings, tests) and `tools/colour-preview` | ours | the eye blit's dither and grade on ME2; the GPU check of Step 1's inverse |
| `core/d3d11/Blit`, lane B2's `plugin/EyeCopy` | ours | the float-to-sRGB eye copy; Step 4's raw FP16 copy |
| `core/d3d11/ShaderReflect` | ours (research/37) | finding `FReconstructHDRPixelShader` by its `HDRParameters` variable (Step 3) |
| `MevrBridge::NotePixelShaderCreated` | LE1 Halcyon `CreatePixelShaderHook` | Step 3 on LE1 at once; LE2 needs the same seam |
| The game's own HDR pass and settings (`[HDR]` keys, its calibration screen's shaders) | the game | Step 7: the reconstructed scRGB is exactly what an HDR monitor gets |
| RenoDX's findings (which shaders, the pass's live constants) and MIT framework code (`renodx::color`, `renodx::tonemap`), with its notice | clshortfuse/renodx (MIT) | reading; a future tonemap of our own (with `NOTICE.md`) |

| Can't (or shouldn't) | Why |
|---|---|
| LE1's curves as ME2's float-path tonemap | section 4.3: they lose shadows or highlights against the SDR frame |
| RenoDX's `masseffectle/*.hlsl` | 3Dmigoto decompilations of the games' shaders: never in this repository |
| R10G10B10A2 eye swapchains (research/27 7b's idea) | OpenXR treats them as linear values: 10 linear bits are coarser than 8-bit sRGB in the darks (Meta staff); use R16G16B16A16_FLOAT or R11G11B10_FLOAT if offered |
| Halcyon's upstream ME1/ME2/ME3 renderers' HDR handling | none of the three has any (`reference/MassEffect*-VR`) |
| HDR output to the PS VR2 or a Quest on PC | section 2: not supported by the runtimes or the PS VR2's adapter |

## 6. The plan

Sizes as in [the continuation plan](../docs/le2-continuation-plan.md): **S** a day or less, **M** a few sessions, **L**
a week or more. Files under `src/halcyon/le2` and `src/plugin` are lane B2's until it says otherwise: Steps 3-6 on ME2
are its (or wait for M2 to land). Every engine-memory write gets a `write_journal::Wrote`; nothing here runs UnrealScript.

### Step 0. The stop gap: the game's SDR frame on ME2 (done by lane B2, the owner's decision)

`[Colour] hdrDisplay = -1` as LE2's default (`MevrBridge::OnFactory`, `ASI\LE2MEVR.ini`): the game answers SDR, its swap
chain is format 28, Halcyon's copies, SFR and UI work as upstream. Check (lane B2's): `hdr display: the display reports
colour space 12 (HDR10) ...; answered SDR`, `the game's swap chain is ... format 28`, left and right sources differ.
**LE3:** check whether ME3 also goes float on an HDR desktop with `DynamicRange_SDR` (one launch, Windows HDR on; never
change Windows' setting: run it on the machine as the owner has it); if it does, LE3's host needs the same `GetDesc1`
answer (a hook beside LE3's DXGI hooks in `src/plugin`, reading the same key) before its own renderer's copy goes black.

### Step 1. The reconstruction and its inverse, offline (S, no game, any lane)

1. New `src/core/vr/HdrReconstruct.hpp/.cpp` (`SPDX-License-Identifier: MIT`), namespace `mevr::vr::hdr_reconstruct`:
   - `struct Params` with the 12 fields of section 1's table **in the game's order and sizes** (9 floats, 3 int32: 48
     bytes, `static_assert(sizeof(Params) == 48)`), so the live constant buffer can be copied into it (Step 3); defaults
     `1000, 0, 250, 0.36, 2.2, 1.0, 0.5080784, PQ(400), PQ((1000/250)^1.0 x 250), 0, 1, 0`.
   - `Rgb Forward(Params const&, Rgb encodedSdr)`: section 1's steps 1-5, written from that description.
   - `Rgb Inverse(Params const&, Rgb scRgb)`: BT.709 -> BT.2020, nits -> PQ; the shoulder undone only where it bent the
     value (`max(n, shoulder^-1(n))` above the start); out of PQ, / `PaperWhiteNits`, `^(1/HDRGamma)`; the gamut
     blend undone by fixed-point iteration (four rounds: weight from the current estimate, solve the blended matrix); the
     luminance curve undone by solving `Y F(Y) / (Y + 0.0001) = luma` (closed form above luma 0.01, bisection below);
     `^(1/DisplayGamma)`; clamped to [0, 1]. Returns the game-encoded SDR value.
   - `char const* InversePrelude()`: the same in HLSL (`float3 MevrUnreconstruct(float3 scRgb)`, constants in a
     `cbuffer MevrHdr : register(b2)` holding `Params`).
2. Tests in `tests/HdrReconstructTests.cpp`, called from `tests/Main.cpp` (stage by hunk: shared file): grey ramp 0..255
   round trip within one step; a 52^3 colour cube with max channel >= 16: >= 99.5% within one step, all within 3 except
   pure primaries; code 186 maps to paper white within 1%; `HDRGamma` 1.25 shadows darker than 1.0 (sign check).
3. `tools/colour-preview --check` (`mevr-colour-shader`): the HLSL inverse on a float ramp against `Inverse` within one
   step.
4. List the new sources in CMake only once they compile; build `build`, `build-mono`, `build-trilogy`; ctest.

### Step 2. `tonemap = game` on LE1's float path (S, LE1)

1. `core/vr/Colour`: `Tonemap::Game` (`"game"` in `ParseTonemap`); `MakeConstants` passes it as tonemap 4; the prelude's
   `MevrTone` calls `MevrUnreconstruct` for it and returns the sRGB decode of the result (the sRGB view then stores the
   game's encoded value, as the SDR path's raw copy does).
2. `src/halcyon/le1/xr_session.cpp` `HdrSourceScale`: `[MEVR]` return 1 when the tonemap is `game` (the inverse wants
   raw scRGB; one new bridge call, `MevrBridge::EyeToneIsGame()`).
3. `Params` on LE1: Step 3's live constants when present, else built from `GamePaperWhiteNits`, BIOEngine.ini's `[HDR]`
   and the luminance our `HdrDisplayDesc` answered (log `colour: the game's HDR pass: ...` once).
4. **Check** (research/27 7d's pose: the heavy Eden Prime cell, the still PS VR2 recording paused; `hdrDisplay = 1`,
   `tonemap = game` against `hdrDisplay = 0`): mean luma within 1; bright window >= 95% of 50,108 colours; dark window
   >= 95% of 5,853 colours, mean 17.6 +- 1, black under 0.5%. Record in research/27 section 7e.

### Step 3. Read the game's HDR pass live (S; LE1 at once, LE2 with a seam in lane B2's files)

1. Find the pass: at `CreatePixelShader`, `d3d11::reflect::Variables(bytecode, size)` holds a variable `HDRParameters`
   (size 48, slot 0): remember that `ID3D11PixelShader*` (both variants). LE1: inside `MevrBridge::NotePixelShaderCreated`.
   LE2: a `[MEVR]` seam in `d3d_capture.cpp`'s device hooks (Halcyon ME2 hooks no `CreatePixelShader` today: add
   `ID3D11Device` slot 15 beside its existing vtable patches, calling `MevrBridge::NotePixelShaderCreated`).
2. At its draw (LE2: the `MhDraw*` hooks; LE1: DrawReplay's `MevrBridge::Record*` draw seams), with that shader bound
   (`PSGetShader`): `PSGetConstantBuffers(0, 1)` -> `CopyResource` into a 48-byte staging buffer, mapped two frames later
   with `D3D11_MAP_FLAG_DO_NOT_WAIT` (never a stall); `PSGetShaderResources(0, 1)` -> the **frame target** (its texture,
   format, size); `OMGetRenderTargets` -> whether it writes the swap chain's buffer.
3. Log once and on change: `hdr frame: the game's HDR pass reads <W>x<H> format <F> and writes <the swap chain|...>;
   HDRParameters: maxReconstructedNits ..., paperWhiteNits ..., displayGamma ..., hdrGamma ..., shoulder ... nits, display
   ... nits, scene ... nits, hdr10 .., gamut ..`; and per second for ten seconds after the first, the draws between that
   pass and Present (`hdr frame: draws after the HDR pass: N`; expected 0) and the targets the UI gate's candidates hit.
4. **Check** (one ME2 run, `prep.ps1 -Holder <lane>`, `hdrDisplay = 0` in `ASI\LE2MEVR.ini` after prep, `launch.ps1`,
   `load.ps1`, `launch.ps1 -Stop`, `restore.ps1`): the line names format 10 at the swap chain's size; `hdrGamma` settles
   section 4.2's question; the draws-after count says whether the UI is in the frame target.

### Step 4. The float frame as a first-class source on ME2 (M; lane B2's files, after M2)

1. **Frame draws:** where `g_sfrBbDrawsThisPresent` counts draws into the swap chain (`g_currentRtvResourcePtr ==
   g_bbResourcePtr`), count draws into the frame target instead while the swap chain is float. `[CLEARMAP]` should show two
   groups again.
2. **Left eye:** `SfrMaybeCapturePass0AtClear` snapshots the frame target (not `GetBuffer(0)`) at the second confirmed
   opening; `SfrEnsurePass0Tex` takes the frame target's description (FP16).
3. **Right eye:** at the HDR pass's draw (before it runs), copy the frame target into a per-present FP16 texture;
   `me2_xr.cpp`'s SFR submit uses it as `rightSrc` while float. The AER and mono paths and the flat panel take the same
   texture when it is fresh this present, else the swap chain through Step 1's inverse.
4. **Eye copy:** an `EyeCopy` variant for an FP16 SDR-encoded source: our prelude's dither, written raw through an
   `R8G8B8A8_UNORM` view of the typeless eye image (the SDR path's bytes, with the extra precision as dither), or through
   Step 5's float swapchain. Halcyon's convergence `shiftX` as today.
5. **UI:** `IsUiDrawNow` and `CaptureUiCompositeAndSkip` accept the frame target (by pointer) where they want format 28 at
   the swap chain's width. Measure the HUD's white in both paths; if the HDR UI shaders draw it brighter
   (`HDRBrightnessScale`, `PaperWhiteNitsUI` 900 against 250), scale the UI's draws or answer `PaperWhiteNitsUI` equal to
   `PaperWhiteNitsScene` through the game's settings **[verify]**.
6. **Check** at the same pose (Freedom's Progress from `load.ps1`, the fake headset's still head), `hdrDisplay = 0` against
   `-1`: left and right sources differ (8-bit: mean difference 15/255); `[SFRDIAG] totalCaps` > 0 every second in
   gameplay; `[UIGATE] ui` > 0 and the HUD cropped as in 8-bit; world windows (section 4.1's method) mean within 1,
   distinct colours >= the 8-bit frame's, black share <= the 8-bit frame's + 0.5 points; GPU time (`tools/perf-probe.ps1`)
   within 0.3 ms of 8-bit at p90.

### Step 5. The precision to the eye (S to M)

1. **The probe (S, ten minutes, needs the game lock and SteamVR):** extend `tools/swapchain-probe` (`mevr-swapchain-probe
   --formats`) to print the instance extensions, every format `xrEnumerateSwapchainFormats` offers in order with a test
   swapchain of each (result, image count, texture format), and, where `XR_SONY_swapchain_color_space` is offered, each
   format's `xrEnumerateColorSpacesSONY`. Run it under the lock with the game closed (`tools/fake-headset.ps1 -On` is the
   lanes' state; `XR_RUNTIME_JSON` pinned to SteamVR's manifest for the probe's process only); record the list here.
2. **If 10 or 26 is offered (S-M):** `[Colour] eyeFormat = srgb8 | float` (default `srgb8`): the eye swapchains in that
   format (`me2_xr.cpp` `ChooseSwapchainFormat`/`EnsureAerSwapchains`; LE1 `xr_session.cpp` `SrgbSwapchainFormat`), the
   blit writing linear light. Never R10G10B10A2 (section 5).
3. **Check:** research/27's test strip's near-black steps and a dark gradient (`core/vr/ColourStrip`) in a snapshot (the
   eye image itself, float: the snapshot tool needs a float-to-PNG path that keeps the bottom codes); GPU and compositor
   cost with `tools/perf-probe.ps1`; whether bands are gone in the PS VR2 and the Quest over Virtual Desktop: **headset
   checklist** (the owner's eye, which the measurements can only back).

### Step 6. ME2's default back to the game's float frame (S, after Steps 3-4; the owner's yes)

The stop gap ends here. Set LE2's `[Colour] hdrDisplay` default from `-1` back to `0` (the display's own answer, ME1's
default) in lane B2's `OnFactory` handler, and in `MEVR Setup`'s ME2 settings once M7.2 exists. **What has to hold first,
each measured on the fake headset at the same pose with Windows HDR on (never change it), and written in this note:**
- **Stereo:** left and right differ in every gameplay snapshot; `[SFRDIAG] totalCaps` > 0 each second; no `[AERHZ]
  sameEyeTwice` growth; a VR conversation's `[STALEEYE]` fallback no more often than in 8-bit.
- **The UI:** `[UIGATE] ui` > 0; the HUD at the same place and size as the 8-bit frame's; the HUD's white within 2 levels
  of the 8-bit frame's; menus, the flat panel, movies (`FBinkGpuShaderHDR`) and loading screens all shown (no black or
  stale frame in a menu-to-gameplay-to-menu snapshot sequence).
- **Quality at least the SDR frame's:** section 4.1's numbers (mean within 1, distinct colours >= the 8-bit frame's in the
  bright and dark windows, black share <= 8-bit + 0.5 points), at Freedom's Progress and one darker scene (the Normandy's
  engineering deck).
- **Cost:** GPU p90 within 0.3 ms of the 8-bit path (`tools/perf-probe.ps1`, the same replay).
Then: the owner's yes (a player-facing default), `CHANGELOG.md`, and the headset checklist line closed. With an SDR headset
Step 6 shows the same picture as the stop gap with more precision; if Step 5 finds that precision invisible, the stop gap
can stay the default with no loss.

### Step 7. HDR to a headset (M once a runtime exists; blocked today)

Watch item: the probe (Step 5.1) after every SteamVR or Meta runtime update the owner installs, and the Khronos inventory.
When a runtime offers `XR_SONY_swapchain_color_space` with `XR_COLOR_SPACE_EXTENDED_SRGB_LINEAR_SONY` (or `BT2020_PQ`)
for format 10 (or another HDR extension appears):
1. Enable the extension at instance creation (Halcyon's instance: a `[MEVR]` seam as for the HP controller extension).
2. Create the eye swapchains R16G16B16A16_FLOAT with `XrSwapchainCreateInfoColorSpaceSONY{EXTENDED_SRGB_LINEAR}`; copy
   the game's reconstructed scRGB untouched (what an HDR monitor gets: the game's own HDR, with its own calibration);
   `xrSetHdrMetadataSONY` with BT.709 primaries, D65, max luminance = the headset's peak (answered to the game through
   `hdrDisplay` so its shoulder fits the panel), MaxCLL = `MaxBrightnessOfScene`.
3. Our quads (HUD panel, menu, sights) at `PaperWhiteNitsUI` / 80 in the same space.
4. Then, and only then, a scene-referred tonemap of our own (RenoDX's idea, our code) is worth an L.

### Not planned

A replacement of the games' tonemap shaders for SDR headsets (section 4.3: a re-grade, no fidelity gain, and the only
public implementation is decompiled game code).

## 7. HDR desktops: launch checks (2026-11-14)

The owner: "people with HDR monitors reported issues when launching me1 so we should at the least support those runtimes
so they can launch without issue." The same evening the HDR plan above was stopped (the owner: "We can take the HDR lane
out of play"; Steps 1-2 were written and set aside, nothing of them landed) and **LE1 now answers the game's HDR question
SDR by default too** (`[Colour] hdrDisplay = -1`, as ME2's, `MevrBridge::HdrDisplayDesc`; an LE1 file's old `hdrDisplay =
0`, the default before, moves to -1 once, marked `hdrDisplayMoved = 1`). Gotcha: [windows-hdr-desktop](../docs/knowledge/gotchas/windows-hdr-desktop.md).

**This machine has Windows HDR on** (the display answers colour space 12, HDR10, 400 nits); every run below is an HDR
desktop's. Runs by script on the fake headset (`tools/fake-headset-session.ps1` for LE1 under the run lock;
`tools/le2-halcyon-run` for ME2), each resuming the profile's save (LE1: the career's last, a distant ship over Noveria;
ME2: `load.ps1`'s Freedom's Progress). Saves, inis and `GamerSettings.ini` put back after each, byte for byte.

| game, setting | reaches gameplay | the game's frame | eyes | UI | faults |
|---|---|---|---|---|---|
| LE1, `DesiredRange = SDR`, new default (-1) | yes | `hdr display: ... colour space 12 (HDR10) ...; answered SDR`; `[SCREQ] ... fmt=28`, `[HDRBLIT] backbuffer format 0 -> 28` | not blue or black (mean RGB 13/16/22 of a night sky); left and right differ 0.15 on average (the ship is far: 0 px disparity, as metric stereo puts infinity) | HUD panel and label quads in the snapshot | none (`crash-firstchance.txt`, `crash-breadcrumbs.txt` older than the runs) |
| LE1, `DesiredRange = DynamicRange_HDR10_Float`, new default (-1) | yes | answered SDR; `[HDRCHK] ... DynamicRange_HDR10_Float` yet `[SCREQ] ... fmt=28`, backbuffer 28: **stays 8-bit** | the same picture as the SDR run (mean RGB identical) | as above | none |
| LE1, `DesiredRange = SDR`, `hdrDisplay = 0` (the old default; the first run's start, before the move) | yes | format 28 (as every run of the day) | as above | as above | none |
| ME2, current settings (`DesiredRange` from `vr-game-settings.ps1 -Game LE2`, `hdrDisplay = -1`) | yes (`load.ps1`) | `answered SDR ([Colour] hdrDisplay = -1)`; `the game's swap chain is 4096x2304 format 28` | mean RGB 51/55/62 (the HDR study's SDR frame); left and right differ 16.8 on average | in the eye images (ME2 has no HUD panel yet, lane B3) | none; restore: 0 differences |

**Not run, by the owner's decision** (risky for a player's monitor, and moot with the SDR answer): `DesiredRange` HDR with
`hdrDisplay = 0` or `1`, ME2 with `hdrDisplay = 0`, exclusive fullscreen, a fresh `GamerSettings.ini`. The owner's monitor
needing a restart to show the game happened before our mods (the owner): not ours, not investigated.

**MEVR Setup on an HDR desktop** (`src/install`, `tools/setup`; `tools/setup/test-fake-game.ps1` on this machine, every
check ok): Setup writes `[HDR] DesiredRange = DynamicRange_SDR` into `GamerSettings.ini` when its HDR option is on (the
default, `Choices::hdrOff`), records the player's value (`ini::Edit`), and Uninstall and "restore the game settings" put it
back exactly (the test's fake file starts at `DynamicRange_HDR`: "GamerSettings.ini exactly as it was"); `BIOEngine.ini`'s
`[HDR]` is left alone. Nothing in Setup asks the display about HDR, and its window is an ordinary 8-bit swap chain DWM
composes on an HDR desktop. ME2's Setup (M7.2) waits.

## 8. Open

- **[verify]** SteamVR's 9 formats and its extensions (Step 5.1).
- **[verify]** `HDRGamma` live on this install (Step 3; the model says 1.0).
- **[verify]** what the float frame draws after the HDR pass, and the UI's brightness in it (Steps 3 and 4.5).
- **[verify]** LE3 on an HDR desktop (Step 0).
- **[verify]** whether an 8-bit-plus-dither eye from the FP16 source shows fewer bands than the game's 8-bit frame in a
  headset (Step 5.3), and the PS VR2's DisplayPort colour depth on PC (not found).

## Sources

- OpenXR 1.1: [XrSwapchain](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchain.html),
  [xrEnumerateSwapchainFormats](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEnumerateSwapchainFormats.html),
  [XR_SONY_swapchain_color_space](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_SONY_swapchain_color_space.html),
  [XR_SONY_hdr_metadata](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_SONY_hdr_metadata.html); their
  specification text in KhronosGroup/OpenXR-Docs `specification/sources/chapters/extensions/sony/`; KhronosGroup/OpenXR-SDK
  `CHANGELOG.SDK.md` (1.1.62, 1.1.63); `external/OpenXR-SDK/include/openxr/openxr.h` (1.1.63).
- [KhronosGroup/OpenXR-Inventory](https://github.com/KhronosGroup/OpenXR-Inventory/tree/main/runtimes): `valve_steamvr_windows.json`,
  `valve_steamvr_frame.json`, `meta_pc.json`, `varjo.json`, `android_xr.json`, `microsoft_pc.json`, `monado_windows.json`.
- [ValveSoftware/openvr](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h) `headers/openvr.h`
  (`Prop_Hmd_SupportsHDR10_Bool`, first in SDK 1.26.7, 2023-08-08; `EColorSpace`).
- PS VR2 on PC: [PlayStation Direct](https://direct.playstation.com/en-us/buy-accessories/playstationvr2-pc-adapter),
  [VGC](https://www.videogameschronicle.com/news/playstation-vr2-pc-adapter-coming-this-august-disables-numerous-headset-features),
  [Windows Central](https://www.windowscentral.com/gaming/pc-gaming/playstation-vr2-will-work-on-windows-pc-with-this-newly-announced-adapter-but-some-big-features-wont-work),
  [UploadVR review](https://uploadvr.com/playstation-vr2-pc-adapter-review).
- Meta: [Removing dark scene color banding (2017)](https://developers.meta.com/vr/blog/tech-note-removing-dark-scene-color-banding-in-unity-/),
  [forum: HDR submits (2019)](https://communityforums.atmeta.com/discussions/dev-pcvr/hdr-submits/726095),
  [OpenXR-SDK-Source #467](https://github.com/KhronosGroup/OpenXR-SDK-Source/issues/467) (formats look different across runtimes and headsets).
- Headsets: [Tom's Hardware: Steam Frame](https://www.tomshardware.com/virtual-reality/valve-steam-frame-review),
  [Varjo XR-4](https://varjo.com/products/xr-4/), [Pimax Crystal Super](https://pimax.com/products/pimax-crystal-super),
  [ALVR on the App Store](https://apps.apple.com/us/app/alvr/id6479728026).
- [clshortfuse/renodx](https://github.com/clshortfuse/renodx) (MIT): `src/games/masseffectle/` (`addon.cpp`, `shared.h`,
  `output_0x0765601C.ps_5_0.hlsl` with the live `FHDRParameters` values, `tonemap_me2_*`), and its
  [mods list](https://github.com/clshortfuse/renodx/wiki/mods).
- Local, read-only, not in the repository: the three games' `GlobalShaderCache-PC-D3D-SM5.bin` and `BIOEngine.ini`;
  `%LOCALAPPDATA%\MELE2VR\MELE2VR_Log.txt` (`[CLEARMAP]`, `[UIGATE]`, `[XRAPI]`, `[ME2DISC]`); Steam's
  `logs\xrclient_MassEffect2.txt`; lane B2's snapshots named in section 4.1; lane B2's research text ("The black eyes").
