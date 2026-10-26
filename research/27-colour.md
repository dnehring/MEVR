# 27: Colour correction per headset (2026-10-22)

**Question (user, 2026-10-22).** On the Quest 3 the game looks flat and washed out. Quest owners
fix that outside the game: OpenXR Toolkit's post-processing, Virtual Desktop's gamma (about
0.7), the headset's brightness. Can the mod correct it itself? It should start from known-good
values for the Quest 3, be adjustable (a new menu will come later), and leave the PS VR2 (OLED)
neutral.

**[inference]** marks our reasoning, **[verify]** what a headset session has to settle, and
**not found** what was searched for and not found. The URLs are at the end and in
[sources.md](sources.md).

## Summary

1. **The model.** Brightness (with a gain per channel), contrast, saturation, mid-tones (gamma)
   and black level. All of them work on sRGB-encoded values, the 8-bit numbers the game writes,
   the eye swapchain holds and a streaming encoder compresses. Neutral is the identity, and when
   it is neutral the correction is skipped altogether. A triangular dither of ±0.5 of an 8-bit
   step is added before the 8-bit store. `core/vr/Colour` holds the math, unit-tested; the shader
   is checked against it on a GPU by `tools/colour-preview` (ctest `mevr-colour-shader`).
2. **The seam.** The correction goes at the end of Halcyon's eye blit shader, through
   `MEVR_EYE_OUT` and variants we compile from its source. It also goes into our HUD panel blit
   (the game's UI) and the wheel panel. Our own quads and Halcyon's menu are not touched.
   - **Neutral is today's path exactly.** Halcyon's shaders and the HUD panel's compile to the
     same bytecode as before (checked with fxc), and the raw copy runs as before.
   - **When on,** an eye that would be copied raw goes through the blit instead. In every logged
     session the eyes went through the blit anyway (section 1).
3. **Detection.** The runtime name comes from `xrGetInstanceProperties`, the system name from
   `xrGetSystemProperties`.
   - Every one of the user's sessions ran on `SteamVR/OpenXR`, the Quest 3 over Steam Link
     included.
   - The PS VR2 is `SteamVR/OpenXR : playstation_vr2`.
   - The Quest over Steam Link has no system name in any log yet **[verify]**. We accept both
     `: oculus` and `: vrlink` there.
   - New log line: `[MEVR-COLOUR] headset: ...`.
4. **The presets.**
   - **Quest streamed:** contrast 1.06, saturation 1.12, mid-tones 0.90.
   - **Vivid:** 1.12, 1.25, 0.85.
   - **Neutral:** everything at its identity.
   - Every value is a judgement call. No one has published measurements of the Quest 3's streamed
     picture.
   - The direction comes from what users report: Virtual Desktop gamma 0.6 to 0.73, and contrast
     and saturation a little up.
   - The size is held back by Meta's own note that its LCDs can't show steps below 13/255 (section 5).
5. **The cost.** The PSFill shader grows from about 29 to about 88 instruction slots.
   - Measured offline on the RTX 4090 at the PS VR2's 4164x4244 eye
     (`mevr-colour-preview --bench`): **+0.02 to 0.06 ms per eye** over the plain blit's 0.11 ms.
   - In the game a GPU timestamp pair around each corrected draw is logged every 10 s
     (`colour: GPU time per draw`) **[verify]**.

## 1. What the eyes get today

From `src/halcyon/le1/xr_session.cpp` and Halcyon's log (`%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt`):

- The game's backbuffer is `R8G8B8A8_UNORM` (format 28): 8-bit, already gamma-encoded. The eye
  swapchains are `R8G8B8A8_UNORM_SRGB` (29) (`[HDRBLIT] eye swapchains fmt=29 (backbuffer fmt at
  session start=28)`). The bits land unchanged in the sRGB swapchain, so the runtime decodes them
  once. There is no double gamma.
- **In every session since 2026-10-08 15:18 the eyes went through the shader blit, not the raw
  copy.** The log line is `[HDRBLIT] path=fullframe src=3072x3188 fmt=28 -> shader blit, UNORM
  view (encoded source)`. The render (3072x3188) is not the eye swapchain's size (2204x2352,
  2528x2780, 2880x2956 or 4164x4244, depending on the headset and SteamVR's resolution). So
  `CopyTextureToEyeFullFrame` blits:
  1. `CopyResource` into a staging source;
  2. the downsample chain, if the ratio is over 2x;
  3. `PSFill` into a temporary eye texture through its UNORM view (raw bits);
  4. `CopyResource` into the swapchain image.

  The raw `CopyResource` path only runs when the render size equals the eye's.
- Float (HDR, scRGB) backbuffers are blitted through the temporary texture's sRGB view, with
  Halcyon's `ToEye` (scale to SDR white, hue-preserving clip).
- The game's UI is drawn into our HUD panel texture (premultiplied). Our blit
  (`HalcyonOverlays.cpp`) writes it into a quad swapchain through an sRGB view, decoding 8-bit
  panels first.

## 2. Headsets: the names as the runtimes give them

**The user's own logs.**
- **All Halcyon sessions ran on SteamVR.** Halcyon logged `[XRRUNTIME] name='SteamVR/OpenXR'`
  in all 17 sessions, 2026-10-08 to 2026-10-22, the Quest ones included.
  [research/22](22-performance-levers.md) had already corrected the earlier guess (research/16)
  that they ran on Oculus.
- **Halcyon never logged the system name.** Its `[XRRUNTIME]` line has only the runtime, and our
  host logged the system name only with the guided calibration (merged today).
- **SteamVR's own logs (read-only) name the PS VR2.**
  - `vrserver.txt`: `Active HMD set to playstation_vr2.playstation_vr2_headset`.
  - `vrhw_steamvr.vdf`: driver `playstation_vr2`, model `PS VR2`, controllers `PlayStation VR2
    Sense Left/Right`.
  - The calibration work saw the system name as `SteamVR/OpenXR : playstation_vr2`.
- **SteamVR's logs show the Quest 3 over Steam Link.**
  - Steam Link's driver is `vrlink` (`driver_vrlink.txt`, 2026-10-21 21:06).
  - SteamVR's OpenXR client log for the game loaded `oculus_quest_plus_controller` render models
    that evening (the Quest 3's Touch Plus).
  - The OpenXR system name for that session is in no log **[verify]**.
- **New log line.** The host now logs `[MEVR-COLOUR] headset: runtime '...', system '...',
  vendor 0x.... -> [Colour.<key>]; automatic: ...`, in Halcyon's log and as `colour: headset:`
  in ours.

**Elsewhere** (section 7 sources):

- **SteamVR:** runtime `SteamVR/OpenXR`; system `SteamVR/OpenXR : <tracking system>`, the HMD
  driver's tracking system name; vendor id 0x28DE.
  - Seen in bug reports: `lighthouse`, `oculus`, `playstation_vr2`, `pico`, `cv` (Steam Frame).
  - A Quest through SteamVR shows `oculus` with Link, Virtual Desktop's SteamVR driver and ALVR,
    so the model can't be told.
  - mbucchia's eye-tracker layer treats `: oculus` as possibly Steam Link. That suggests Steam
    Link reports `oculus` rather than `vrlink`, but no log showing it was found.
- **Meta's PC runtime (Link, Air Link):** runtime `Oculus`; system `Oculus Quest2` (runtimes
  1.88 and 1.101) or `Meta Quest 3` (1.108 and 1.117); logged vendor id 0xFFFFD23E.
- **Virtual Desktop (VDXR):** runtime `VirtualDesktopXR`; the system name is the headset's
  product name, e.g. `Meta Quest Pro`.
- **Others:** `Windows Mixed Reality Runtime`, `Varjo OpenXR Runtime`, `PimaxXR (Unofficial)`.

**What we do** (`vr::colour::Detect`, unit-tested on these names):

- **Quest streamed (Auto = Quest):**
  - a system name containing "Quest" (Meta's runtime, VDXR);
  - the `VirtualDesktopXR` runtime;
  - SteamVR's `: oculus` and `: vrlink`;
  - a SteamVR tracking system naming Virtual Desktop or ALVR.
- **Neutral:**
  - OLED: `playstation_vr2`;
  - wired: `lighthouse`, `holographic`, `aapvr` (Pimax), the Rifts;
  - unknown: everything else, including `pico` and `cv`. These are streamed LCD headsets too, but
    the presets are for Quests.
- **A mistake:** a Rift CV1 (OLED) through SteamVR also shows `: oculus` and would get the Quest
  preset. Choose Neutral for it.
- **Section key:** the system name made safe as the calibration spells it, e.g.
  `[Colour.steamvr_openxr_playstation_vr2]`. When the system name doesn't say the runtime, the
  runtime goes in front: `[Colour.oculus__meta_quest_3]` and `[Colour.virtualdesktopxr__meta_quest_3]`,
  because Link and Virtual Desktop stream differently.

## 3. The Quest 3 and its streamed picture

**The display** (Meta's documents and reviewers' specs):

- **Meta's design guide, for all current Quests:**
  - they are LCDs, so "dark pixel values do not get as dim as expected";
  - levels below **13/255** (8-bit sRGB) can't be told apart;
  - brightness is typically **100 nits** at 100%;
  - all cover at least sRGB, and Meta advises authoring content brighter.
- **Quest 3:** LCD, 2064x2208 per eye, pancake lenses, up to 120 Hz, no local dimming. Meta
  claims "50% more contrast than Quest 2".
  - An experimental "content adaptive brightness" (v68) dims dark scenes. Whether it acts over a
    PC stream: not found.
- **Quest 2:** fast-switch LCD at 100 nits, "sRGB, 8 bits per color" (Meta's SID 2022 paper).
  Meta's colour-space page: Rec.709 primaries with a **D75** white point.
- **Quest 3S:** the Quest 2's panel.
- **Quest Pro:** QD-LCD with over 500 local-dimming zones.
- **PS VR2:** OLED, 2000x2040 per eye, PenTile, near-zero black.
- **Not found:** independent measurements of contrast, black level, peak brightness or white
  point for any of them. Every contrast figure above is the vendor's.

**The streaming paths** (Section 7):

- **Link and Air Link:** H.264, or HEVC through the Oculus Debug Tool (users' reports).
  - No colour controls were found.
  - The runtime supports `XR_FB_color_space`.
- **Steam Link:** no codec choice.
  - SteamVR 2.2 added experimental HEVC and 10-bit encoding on AMD cards only, so on the user's
    NVIDIA card it is 8-bit **[inference]**.
  - Known complaint: the image is too **dark** on Quest Pro and 3 (Valve suggested turning local
    dimming off, which only the Pro has); users raise the headset's brightness by 7-8%.
  - OpenVR has `hmdDisplayColorGainR/G/B` setting keys; whether Steam Link honours them: not found.
- **Virtual Desktop:**
  - Codecs: H.264, H.264+, HEVC, HEVC 10-bit, AV1 10-bit. "Automatic" picks HEVC 10-bit on
    NVIDIA (v1.30.1). v1.34.16 "improved color gradients and color accuracy with all codecs".
  - Its headset app has a **gamma** slider: default 1.00, "higher values increase overall
    brightness".
  - Two toggles, as the community describes them: "increase color vibrance" (Rec.2020 versus
    Rec.709, i.e. more saturation) and "increase video nominal range" ("dark colors darker,
    bright colors brighter").
- **ALVR:** `encoding_gamma` (default 1.0; 2.2 favours dark pixels at the cost of mid-tone
  banding).
- **Not found:** the chroma subsampling and the video range (limited or full) of any path. 8-bit
  H.264 and HEVC streams are normally 4:2:0 **[inference]**. A range mismatch between encoder and
  decoder would give exactly the grey blacks and dim whites of "washed out" **[inference]**. That
  is not something we can see or fix from the PC side, but a slightly deeper black level hides
  it.

**What users do** (all for Quest 3 or 2 on PC, mostly Microsoft Flight Simulator; anecdotes):

- **Virtual Desktop gamma:** "brought the Gamma down from 1.0 to around 0.72 and it made a huge
  difference"; another user uses 0.6; "set the Gamma to 70", "68-70 range"; "from 100 percent to
  70".
- **OpenXR Toolkit on a Quest 2:** TruNite sunglasses, contrast 55, brightness 55.5, exposure
  36.5, saturation 49, gains R 44.5, G 47.5, B 51.4 (on its 0-100 sliders, 50 = neutral).
- **OpenXR Toolkit on a Quest 3 through Virtual Desktop** (search snippet only, the page refused
  us): contrast 42.5, brightness 40, highlights 40.
- **What these say [inference]:** people darken the mid-tones and deepen the image. Saturation
  stays near neutral in the Toolkit examples, while Virtual Desktop's "color vibrance" toggle
  exists for the same complaint.

## 4. The model

All on **sRGB-encoded values in [0, 1]**:

- They are the game's own 8-bit numbers, the space where the 8-bit store (and so the dither) and
  the video encoder work, and roughly perceptual.
- They are also what Virtual Desktop's gamma and OpenXR Toolkit act on with a UNORM swapchain.
  The Toolkit runs its math on whatever it samples: with an `_SRGB` swapchain that is linear
  light.
- A linear source (the float HDR backbuffer, an `_SRGB` view) is encoded first and decoded
  after, so the same settings look the same on both paths.

| Step | Formula | Neutral | Range |
|---|---|---|---|
| black level | `x = (x + black) / (1 + black)`: below 0 deeper (inputs at or below `-black` become black), above 0 lifted; white stays | 0 | -0.2 .. 0.2 |
| brightness, gains | `x = x * brightness * gain[c]` (clips at white) | 1, 1/1/1 | 0.5 .. 1.5 |
| mid-tones | `x = x ^ (1 / gamma)`: below 1 darker, Virtual Desktop's direction (its exact formula: not found) | 1 | 0.5 .. 2 |
| contrast | `x = x + t (smoothstep(x) - x)`, `t = 2 (contrast - 1)`: an S-curve through black, mid-grey and white whose slope at mid-grey is `contrast` | 1 | 0.5 .. 1.5 |
| saturation | `x = Y' + (x - Y') * saturation`, Y' the Rec.709 luma of the encoded values | 1 | 0 .. 2 |

Then clamp, add dither, store.

- **Contrast** is OpenXR Toolkit's idea (MIT; a lerp toward smoothstep on luma). Here it is
  applied per channel, so it keeps black and white without clipping. The concept was rewritten;
  no code was copied.
- **Neutral** is detected within half a slider step and skips the correction entirely.

**Dither.**
- Triangular, ±0.5 of an 8-bit step: the sum of two uniform hashes of the pixel position,
  centred.
- The same noise on R, G and B, so it shows as luminance, not colour.
- Static: the same pattern every frame, a different one per eye and per panel. A pattern that
  moves would cost the video encoder bits and flicker **[inference]**.
- Tested: a level 0.4 of a step between two values averages to within 0.08 of a step over an
  area (0.4 off without dither), and never lands more than one step away.
- The encoder may smooth some of it away **[verify]**.
- `[Colour] dither` sets it (0 is off, up to 2 steps).

## 5. The presets (all judgement calls)

| | brightness | contrast | saturation | mid-tones | black | gains |
|---|---|---|---|---|---|---|
| Neutral (PS VR2, unknown) | 1 | 1 | 1 | 1 | 0 | 1/1/1 |
| Quest streamed | 1 | 1.06 | 1.12 | 0.90 | 0 | 1/1/1 |
| Vivid | 1 | 1.12 | 1.25 | 0.85 | 0 | 1/1/1 |

What they do to grey steps (8-bit in, 8-bit out, from `mevr-colour-preview`):

| in | 0 | 8 | 13 | 20 | 32 | 64 | 96 | 128 | 160 | 192 | 224 | 255 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Quest streamed | 0 | 5 | 8 | 14 | 23 | 52 | 84 | 118 | 153 | 189 | 223 | 255 |
| Vivid | 0 | 3 | 6 | 10 | 18 | 44 | 76 | 112 | 150 | 188 | 224 | 255 |

Why these numbers:

- **Mid-tones 0.90.** An exponent of 1.11, where Virtual Desktop's 0.72 would be 1.39 (if its
  slider is the same power, unconfirmed): under a third of the way in exponent terms. We stopped there
  because of Meta's 13/255 floor. With Quest streamed, 20/255 still comes out at 14, above the
  floor. Virtual Desktop at 0.72 would put it near 7, below the floor. LE1 has many dark
  interiors.
- **Contrast 1.06.** Small. The S-curve also flattens the darkest steps (at 1.06 the slope at
  black is 0.88).
- **Saturation 1.12.** For "flat"; Virtual Desktop's colour vibrance option answers the same
  complaint.
- **Black level 0 and gains neutral.** No measurement suggests a value. A range mismatch would
  call for a deeper black, and the Quest 2's D75 white point for a warmer white. Both are
  [verify] in the headset with the Custom sliders.
- **Vivid** is for players who want more on any headset.
- All values sit in the menu and `[Colour.<headset>]`: the user tunes from here.

## 6. Where it applies, and what neutral means

- **Halcyon's eye blit** (`xr_session.cpp`, `[MEVR] colour`).
  - The shader source has a `MEVR_EYE_OUT(c, pos)` hook (the identity) around `ToEye` in
    `PSLeft`, `PSRight` and `PSFill`.
  - `MevrBridge::EyeColourShaders` compiles the three again with `core/vr/Colour`'s prelude in
    front. The prelude declares `cbuffer MevrColour : register(b1)` and the correction.
  - At each eye draw `EyeColourShader` returns Halcyon's own shader, or, with the correction on,
    the variant with its constants bound (b1 is saved and put back).
  - `CopyTextureToEyeFullFrame` and `CopyTextureToEye` take the blit while
    `EyeColourActive()`. At the same size that is a 1:1 sample at texel centres, so the same
    picture.
  - Covered: the full-frame path (Stereo 2's captured pair), mono, side-by-side halves, and the
    flat-screen frames, which fill the eye swapchain too.
- **The HUD panel and the game's wheel panel** (`HalcyonOverlays.cpp`):
  - the panel shader has a `MEVR_PANEL_OUT` hook on the unpremultiplied linear colour, and a
    variant compiled the same way;
  - it is corrected on the linear path (encode, correct, dither, decode), then premultiplied.
- **Not corrected:**
  - the reticle, the sights' own pictures, the squad menu, the calibration's quads and
    Halcyon's menu;
  - the scope magnifies the eye image, which is already corrected.
- **Neutral is today's path exactly.**
  - fxc compiles Halcyon's `PSLeft`, `PSRight`, `PSFill` and `PSCopy` and the panel's `PSMain`
    and `GridPS` to byte-identical DXBC with the hooks and without them.
  - With neutral (or while comparing), `EyeColourActive()` is false and the raw copy runs as
    before.
  - The added work per eye per frame is one call and a tick-count check.
- **Snapshots.** Test-loop snapshots copy the swapchain images just before release, so their
  PNGs show the **corrected** picture (`"colour"` in the JSON says whether it was correcting).
  While comparing they show the uncorrected one.

## 7. Settings, compare, cost

- **`ASI\LE1MEVR.ini`**, read by the host within a second of a change:
  - `[Colour]` (written with its defaults the first time): `profile = auto`, `compare = 0`,
    `dither = 0.5`, and optionally custom values for any headset;
  - `[Colour.<headset>]`: `profile` and `brightness`, `contrast`, `saturation`, `gamma`,
    `black`, `gainR`, `gainG`, `gainB`. Each key overrides `[Colour]`'s.
  - Profiles: `auto | neutral | quest | vivid | custom`. Custom uses the headset section's
    values, then `[Colour]`'s, then the identity.
- **The VR hands tab's Colour section:** the headset and what Auto means for it, the five
  profiles, live sliders (moving one makes the values the headset's Custom, saved when the slider
  is let go), "Hold to compare" and "Custom: back to neutral".
- **Compare:** `[Colour] compare = 1` (persistent), or the host's `colour::HoldCompare()` (call
  each frame while held; it lapses after 150 ms) and `colour::SetCompare(bool)`. These are for
  the coming menu's "hold A".
- **Cost:**
  - `PSFill` grows from about 29 to about 88 instruction slots (fxc), with all of steps 1-5, the
    hash, and the encode and decode behind a uniform `[branch]` on the linear flag. A first
    version that selected between them without a branch ran both, and cost +0.096 ms per eye.
  - `mevr-colour-preview --bench` times the eye blit's shape (bilinear 3072x3188 into 4164x4244,
    20 draws, median of 9 runs) with and without the prelude on the RTX 4090: plain 0.11 ms,
    corrected 0.135 to 0.17 ms, so **+0.02 to 0.06 ms per eye**, under 0.12 ms per frame. The
    blit was already there.
  - The float (HDR) path adds an encode and a decode per pixel (more `pow`); it is not measured.
  - Where the raw copy is replaced (same size), add a full-screen pass and a copy.
  - The log measures it: GPU timestamps around each corrected draw, and around the plain blit
    while comparing. Every 10 s: `colour: GPU time per draw, last 10 s: eye blit with the
    correction X ms (N draws); eye blit without (comparing) Y ms; HUD panel blit with the
    correction Z ms`. The averages go into snapshots (`"costMs"`).
  - The frame probe (research/22) is unchanged.

## 7b. The game's HDR range and the tonemap (2026-10-23)

The user: "does VR support HDR? The colour depth is a little lacking."

- **No HDR output on the headsets we have.** The PS VR2's PC adapter doesn't carry HDR (Sony lists it
  among the features missing on PC), so SteamVR drives the panel in 8-bit SDR; a streamed Quest is 8-bit
  too. The eye swapchains are R8G8B8A8_UNORM_SRGB whatever the game does.
- **Where the colour goes flat.** The deploy forced the game's SDR range (`[HDR] DesiredRange =
  DynamicRange_SDR`, Halcyon's installer does the same) because the game's HDR output is a float scRGB
  backbuffer whose highlights the 8-bit eyes clip. In SDR the game's own tonemapper compresses the range
  before we see the frame. The game knows four ranges (`DynamicRange_Auto`, `HDR10`, `HDR10_Float`, `SDR`)
  and two paper whites (`PaperWhiteNitsScene`, `PaperWhiteNitsUI`, strings in the exe).
- **What Halcyon's blit does with a float source** (`[MEVR] HDR` in `xr_session.cpp`): a probe of the
  frame's luminance latches "HDR range seen", then `colorScale.x = 80 / PaperWhiteNitsScene` maps the
  scene's paper white to 1.0, and `ToEye` clips each pixel hue-preserving (divided by its largest channel)
  before the eye's sRGB view encodes. Everything above paper white is lost to white.
- **The tonemap** takes the clip's place: `MEVR_TONE(c)` in `ToEye` (`[MEVR]` seam; the default is the
  clip, bit for bit), defined by our prelude (`core/vr/Colour`): linear in with paper white at 1.0, linear
  [0, 1] out, then the hardware encode, then the grade of section 4 as before. Curves: `aces` (the
  Narkowicz fit: 1.0 → 0.80 linear = 232 encoded, 2x → 245, 4x → 252, 8x → 255), `hable` (Uncharted 2's
  filmic, with its bias of 2, `white` reaching 1.0; 1.0 → 0.54), `reinhard` (extended, `white` reaching
  1.0), `clip`. `exposure` in stops before the curve. Only a linear source gets a curve; an 8-bit source
  keeps the clip, which is the identity there, so SDR play is unchanged. The HUD panel never sees it.
- **Settings:** `[Colour]` / `[Colour.<headset>]` `tonemap`, `exposure` (-3..3), `white` (1..16; the
  game's brightest reconstructed highlight is `MaxReconstructedNits / PaperWhiteNitsScene`, 4 here). A
  curve other than clip makes the shader variants run even on a neutral grade (with the dither, which the
  8-bit store of an HDR frame wants). Constants: `misc[2]` exposure multiplier, `misc[3]` white, `seed[1]`
  the mode; CPU reference `Tone()`; `mevr-colour-preview --check` runs every curve on an HDR ramp through
  `PSTone` against it (one 8-bit step at most); unit tests in `TestTonemap`.
- **The game's range:** `tools/vr-game-settings.ps1 -Hdr` writes `DesiredRange = DynamicRange_HDR10_Float`
  (`-Sdr` back; a later apply keeps the choice). **Verified 2026-10-23 (the PS VR2 session, research/35
  attempt 26):** with the explicit range and Windows' HDR switch off on the desktop the game still creates an
  `R8G8B8A8_UNORM` backbuffer (`[HDRBLIT] backbuffer format 0 -> 28`, "encoded source, raw"): no float source,
  the tonemap idle. The float backbuffer needs Windows HDR on for the monitor the game window is on (as
  research/13 saw), or a hook that answers the game's HDR-support query itself (not done). Still open: the
  paper white probe's value, the GPU cost of the float backbuffer, and the look in the headset against SDR.
- **10-bit eyes:** not done; worth asking SteamVR for `R10G10B10A2_UNORM` swapchains once the tonemap is
  judged, for the banding the dither hides today.

## 7c. The float backbuffer seen, and what it costs (2026-10-25)

The user: "how can we match the colour fidelity of more modern titles", and "is there any way to force HDR despite
the host monitor not supporting it". The monitor does support it: Windows' DisplayConfig
(`DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO` on the one active path, the Samsung Odyssey G95SC) answered
advanced colour supported, not enabled, 8 bits; the game's `[HDR] DesiredRange = DynamicRange_HDR10_Float` was already
in GamerSettings and every session of the day still got an 8-bit backbuffer (`[HDRBLIT] backbuffer format 0 -> 28`).
With the switch set from a script (`DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE`, enable 1; the display then
reports 10 bits) the next launch made the float backbuffer: `[HDRBLIT] ... fmt=10`, `[HDRWHITE] float source white =
250 nits (scRGB 3.125) from the game's paper white -> scale 0.3200`, the luminance probe latching the HDR range with
5-17% of pixels over 1.0, and our `colour:` line `HDR tonemap aces (exposure +0.00 stops, white 4.0) on a float
backbuffer`.

**The look** (the heavy Eden Prime cell, the still PS VR2 pose, paused; the eye images the headset would show): against
this morning's SDR capture the float frame under ACES is warmer and deeper, the sky a graded orange instead of the SDR
tonemapper's flattened pink-grey, shadows darker (4.4% of pixels crushed against 1.4%), mean luminance 58 against 55,
and nothing clipped (0.00% of pixels at 255; the clip gives 5.0%, Hable 0.07%, ACES a stop up 0.07% at mean 73). The
SDR picture is the game's own tonemap and grade; the ACES one is ours over the scene's linear values. Which of the two
the user prefers is theirs to say; the contact sheet is in this session's scratchpad (`hdr-sheet.png`) and was sent
to them. The capture tool writes no `-world` image for a float source (the layers' files are null; the composited
eye images are complete), which is worth fixing when the float path becomes the default.

**The cost: none measurable.** The same cell under the moving head loop, the same save and evening (about 2,810 draws a
frame with one Draw, 3,560 with two; the morning's probes drew 20% more and are not a baseline), both engine renders:
GPU 7.78 ms median, 9.53 p90 with the float backbuffer against 7.92 / 9.20 with the 8-bit one; CPU 9.08 (11.16)
against 9.27 (10.79). One Draw: 4.70 (7.98) float against 7.83 (11.35) 8-bit, but that 8-bit probe was the first after
a relaunch and is noisy. The float passes do not show in the frame at this resolution.

**The hook (the same evening): `[Colour] hdrDisplay = 1`.** The game's capability query is `IDXGIOutput6::GetDesc1`
(vtable slot 27): `0x1409a0050` takes `DXGI_OUTPUT_DESC1::ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`
(12) as "HDR capable", and the windowed branch (`0x1409a4510`) drops an HDR range to SDR without it (the settings
struct's range at +0x3b4: Auto 1, SDR 2, HDR10 6, HDR10_Float 7; range 7 is the R16G16B16A16_FLOAT swapchain with the
scRGB colour space, 1). The exe imports no DisplayConfig call; the game's video option (`setHDROn`, `SFX_OUTPUT_HDR`,
`BackBufferHDR`) follows the same answer. Halcyon's `d3d_capture` already patches that output's vtable (GetDesc,
FindClosestMatchingMode) when the factory hands it out; a `[MEVR]` seam there queries `IDXGIOutput6` from the same
output and hooks slot 27 (`OutputGetDesc1Hook` -> `MevrBridge::HdrDisplayDesc` in `plugin/HalcyonBridge`): the real
answer first, then, with the key set and the answer SDR, colour space HDR10, 10 bits per colour, and 0.005-1000 nits
only where the display gives no luminance. Seen with Windows HDR off: the hook's log `hdr display: the display reports
colour space 0 (SDR); answered HDR10 to the game` three times at 23:22:25, the game's swapchain created 16 ms after
the last, `[HDRBLIT] backbuffer format 0 -> 10`, the luminance probe latching the HDR range (6-8% of pixels over 1.0),
our `colour:` line on a float backbuffer, and the same still capture as the Windows-HDR-on one above (mean luminance
58.5 against 58.3, nothing clipped, 4.3% of pixels crushed against 4.4%, 24,466 against 26,474 distinct colours in the
bright crop: temporal noise). One Draw stays healthy on it (903 second eyes replayed in 10 s, none without a record),
and the moving head loop costs what it did (one Draw: GPU 5.33 ms median, 9.92 p90; CPU 7.94, 11.01). DXGI takes the
float scRGB swapchain on the SDR desktop: the flat window on the monitor keeps a normal picture (the compositor maps
scRGB to the desktop; highlights clip there, not in the eyes). Windows HDR can stay off; `hdrDisplay = 0`, the default,
passes the display's own answer through. The user's ini has `hdrDisplay = 1`, `tonemap = aces`, `exposure = 0`.

## 7d. The curve applied hue-preserving, and the chroma matched to the game's own pass (2026-10-26)

The user, after the float backbuffer under ACES: "it almost looks saturated on red, however it's hard for me to
tell". Measured at the heavy Eden Prime cell, the still PS VR2 pose, paused (`tonecheck.py` in the session's
scratchpad: the left eye's HSV saturation over pixels with V >= 0.05, hue bins of 30 degrees; the SDR row is a
separate launch with `hdrDisplay = 0` at the same paused pose):

| capture | mean S | pixels S > 0.7 | red-hued share (mean S) | orange share (S) | mean luminance | crushed |
|---|---|---|---|---|---|---|
| the game's own SDR pass | 0.46 | 2% | 33% (0.48) | 61% (0.47) | 63.8 | 0.4% |
| float, ACES per channel (`toneHue = 0`) | 0.67 | 58% | 49% (0.77) | 46% (0.59) | 59.0 | 4.7% |
| float, ACES hue kept (`toneHue = 1`, `toneSat = 1`) | 0.60 | 46% | 40% (0.60) | 57% (0.62) | 60.5 | 4.7% |
| ... hue kept, `toneSat = 0.85` | 0.53 | 25% | 49% (0.55) | 47% (0.52) | 61.4 | 4.7% |
| ... hue kept, `toneSat = 0.7` | 0.46 | 6% | 55% (0.49) | 41% (0.43) | 62.0 | 4.8% |
| ... hue kept, `toneSat = 0.55` | 0.38 | 0.4% | 60% (0.41) | 36% (0.34) | 62.4 | 4.9% |
| float, Hable hue kept, `toneSat = 1` | 0.61 | 49% | 35% (0.59) | 61% (0.64) | 51.3 | 3.6% |
| float, Hable hue kept, `toneSat = 0.7` | 0.46 | 3% | 49% (0.48) | 48% (0.46) | 52.9 | 3.6% |

(The hue shares at low saturation are noise: a nearly grey pixel's hue is its dither.) Two effects. The per-channel
ACES fit (Narkowicz 2015 on sRGB primaries, with none of the desaturation or gamut steps of the full transform) turns
oranges red, half the lit pixels red-hued against a third under Hable or the plain clip of the same float frame, and
raises their saturation to 0.77. And the float frame itself is more saturated than the game's own SDR pass leaves it
(0.60 under the clip or Hable against 0.46), so the game's SDR tonemapper or its grading LUT desaturates, which the
float path skips (not separated).

**The fix** (`core/vr/Colour`, keys in `[Colour]`, the same for every curve): the curve is applied to the pixel's
largest channel and the pixel scaled by the ratio, so hue and chroma survive, blended with the per-channel result by
`toneHue` (0 .. 1, default 1; 0 is the old result); a hue-kept pixel far above its curve moves toward the curve's
grey by `1 - 1 / (1 + toneDesat * (largest channel - its curve))` (`toneDesat` 0 .. 1, default 0.15: the sun's
disc goes white instead of the pink of `toneDesat = 0`, 0.4 whiter still; the idea of the desaturation term in
Khronos' PBR Neutral tonemapper, not its code); and the scene's chroma is scaled toward Rec. 709 luminance before the
curve by `toneSat` (0 .. 1, default 0.7: at this scene the game's own pass's saturation, mean 0.455 against 0.462
and the reds 0.49 against 0.48; 1 is the scene's). The CPU reference and the GPU check (`mevr-colour-shader`, three
more cases) agree within a step; the unit tests pin the ratios kept, the per-channel result at 0, grey unchanged, the
path to white and the chroma scale. Cost: none (GPU 4.91 ms median, 8.20 p90 under the moving head loop with one
Draw: the day's spread). The user's ini: `tonemap = aces`, `toneHue = 1`, `toneDesat = 0.15`, `toneSat = 0.7`.

**Open:** the game's SDR pass also lifts the shadows the ACES toe crushes (94% of pixels lit against 74%, 0.4%
crushed against 4.7%), and its sky is a warmer, more contrasted orange where ours, hue kept, is a pale peach (the sky
sits far above paper white, so the shoulder takes it toward the curve's grey; the game's grade warms its highlights).
A toe lift in linear (the Display page's black level works on encoded values) and the game's own grading LUT on the
float path, if its HDR pass applies one, are the next levers. The per-level grades differ, so `toneSat = 0.7` is
Eden Prime's match, not every level's.

## 8. Open

- **[verify]** The Quest's system name through Steam Link (`: oculus` or `: vrlink`?); the new
  log line settles it.
- **[verify]** Whether Quest streamed looks better on the user's Quest 3 than neutral; A/B with
  Hold to compare. If dark scenes lose detail, raise mid-tones toward 1.
- **[verify]** Whether the dither survives Steam Link's encoder (look at the grey ramp of the
  test strip once the new menu shows it).
- **Not done:**
  - `XR_FB_color_space` (tell Meta's runtime the content is Rec.709; Halcyon doesn't enable the
    extension);
  - a per-path preset (Link versus Virtual Desktop versus Steam Link);
  - Pico and Steam Frame presets.
- **The test strip** (`core/vr/ColourStrip`):
  - grey steps, a ramp, near-black and near-white steps, the ColorChecker's first row (skin
    tones, sky, foliage) and the primaries;
  - ready for the coming menu to show on a quad;
  - `mevr-colour-preview` writes it through each preset.

## Sources

- **Meta:**
  - [display design](https://developers.meta.com/horizon/design/display/)
  - [color space](https://developers.meta.com/horizon/documentation/unity/unity-color-space/)
  - [Quest 3 display blog](https://www.meta.com/blog/vr-display-optics-pancake-lenses-ppd/)
  - [Quest 2 panel paper (SID 2022)](https://research.facebook.com/file/2816674991811976/High-PPI-Fast-Switch-Display-Development-for-Oculus-Quest-2-VR-Headsets.pdf)
  - [UploadVR: Quest 3 specs](https://www.uploadvr.com/quest-3-specs/)
  - [Road to VR: 3S vs 3 vs 2](https://roadtovr.com/quest-3s-quest-3-quest-2-specs-compared/)
  - [Mixed: adaptive dimming](https://mixed-news.com/en/meta-quest-3-adaptive-display-dimming/)
- **PS VR2:**
  - [UploadVR: PenTile teardown](https://uploadvr.com/ifixit-teardown-psvr-2-panels-pentile/)
  - [UploadVR: technical analysis](https://uploadvr.com/psvr2-technical-analysis/)
- **Streaming:**
  - [MSFS forum: Link codecs](https://forums.flightsimulator.com/t/dynamic-bitrates-with-link-and-air-link-a-dynamic-duo/695087)
  - [Khronos inventory: Meta PC runtime](https://github.com/KhronosGroup/OpenXR-Inventory/blob/main/runtimes/meta_pc.json)
  - [Steam: codec request](https://steamcommunity.com/app/353380/discussions/11/4038102936185506853/)
  - [Steam: AV1 request](https://steamcommunity.com/app/353380/discussions/11/4695658272798187222/)
  - [SteamVR 2.2 notes](https://steamcommunity.com/games/250820/announcements/detail/3860211948197696970)
  - [Steam: Steam Link too dark](https://steamcommunity.com/app/353380/discussions/11/4038102936184517580/)
  - [openvr.h](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h)
  - [Virtual Desktop releases](https://github.com/guygodin/VirtualDesktop/releases)
  - [VR Discord VD guide](https://vrdiscord.com/guides/quest-wireless/virtualdesktop.html)
  - [Steam: VD gamma](https://steamcommunity.com/app/382110/discussions/0/1742268886534308692/)
  - [a user's VD toggles note](https://x.com/tonavrc/status/1318333737264771072)
  - [ALVR settings.rs](https://github.com/alvr-org/ALVR/blob/master/alvr/session/src/settings.rs)
- **Users' values:**
  - [MSFS forum: washed out (0.72, 0.6)](https://forums.flightsimulator.com/t/washed-out-colors/666501)
  - [MSFS forum: gamma 70](https://forums.flightsimulator.com/t/how-do-you-coop-with-the-washed-out-colors/705481)
  - [MSFS forum: 100 to 70](https://forums.flightsimulator.com/t/baffled-with-my-quest-3-setup-high-gpu-terrible-brightness-contrast/730399)
  - [MSFS forum: Toolkit on Quest](https://forums.flightsimulator.com/t/open-xr-color-settings-for-quest3/634578)
  - [AVSIM: VDXR and Quest 3](https://www.avsim.com/forums/topic/640807-vdxr-and-quest-3-colour-settings-from-meh-to-wow/) (snippet only)
- **OpenXR Toolkit (MIT):**
  - [features](https://github.com/mbucchia/OpenXR-Toolkit/blob/gh-pages/features.md)
  - [layer.cpp defaults](https://github.com/mbucchia/OpenXR-Toolkit/blob/main/XR_APILAYER_MBUCCHIA_toolkit/layer.cpp)
  - [imageprocess.cpp](https://github.com/mbucchia/OpenXR-Toolkit/blob/main/XR_APILAYER_MBUCCHIA_toolkit/imageprocess.cpp)
  - [postprocess.hlsl](https://github.com/mbucchia/OpenXR-Toolkit/blob/main/XR_APILAYER_MBUCCHIA_toolkit/postprocess.hlsl)
- **OpenXR names:**
  - [Khronos inventory](https://github.com/KhronosGroup/OpenXR-Inventory/tree/main/runtimes)
  - [Blender's runtime map](https://github.com/blender/blender/blob/main/intern/ghost/intern/GHOST_XrContext.cc)
  - [VDXR instance.cpp](https://github.com/mbucchia/VirtualDesktop-OpenXR/blob/main/virtualdesktop-openxr/instance.cpp)
  - [PimaxXR instance.cpp](https://github.com/mbucchia/Pimax-OpenXR/blob/main/pimax-openxr/instance.cpp)
  - SteamVR system names in logs:
    [lighthouse](https://github.com/Rowlhouse/PicaSim/issues/12),
    [oculus](https://github.com/nick10180/FS25VR/issues/5),
    [playstation_vr2](https://github.com/satyaloka93/cyberpunk-vr-port/issues/1),
    [pico](https://github.com/VisorModStudio/Visor/issues/89),
    [cv](https://github.com/V-Sekai-fire/interactor-xr-pilot/pull/56),
    [WMR via Oasis](https://github.com/Gonzorro/GeneralsVR/issues/13)
  - [mbucchia's eye-tracker layer](https://github.com/mbucchia/_ARCHIVE_OpenXR-Eye-Trackers/blob/main/openxr-api-layer/layer.cpp)
  - [OpenXR-Docs #107](https://github.com/KhronosGroup/OpenXR-Docs/issues/107)
  - [ALVR props.rs](https://github.com/alvr-org/ALVR/blob/master/alvr/server_openvr/src/props.rs)
  - Meta runtime logs:
    [Quest 2](https://github.com/LeandroLuna/challenge-toledo/blob/main/AmbienteVR/Saved/Logs/VR_Integration-backup-2023.09.11-20.51.35.log),
    [Quest 3](https://github.com/dariulone/cyberpunk-vr-port/issues/13),
    [VDXR Quest Pro](https://github.com/dariulone/cyberpunk-vr-port/issues/56)
- **ColorChecker sRGB values:** BabelColor's averaged ColorChecker data (widely republished;
  rounded).
- **Local, read-only:** Halcyon's log, our logs, SteamVR's `vrserver.txt`, `vrhw_steamvr.vdf`,
  `driver_vrlink*.txt`, `xrclient_MassEffect1*.txt`.
