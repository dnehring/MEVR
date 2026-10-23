# 30: Life-size stereo: each eye from where it really is (2026-10-23)

User reports (PS VR 2 on SteamVR): the pistol looks huge, close things are hard on the eyes, the scope quad (a
metric layer) doesn't sit on the gun model's scope lens, the calibration orbs looked closer than they were. The
user approved replacing Halcyon's stereo core with a metric mode and keeping the rest of Halcyon. Built without
the headset (the user was away): unit-tested math, a build of all three trees, logs and snapshot fields to check
it with next session.

Sources: `src/halcyon/le1` (render_hook.cpp, xr_session.cpp, d3d_capture.cpp), vrframework guides 08 (stereo:
off-centre projections from the runtime's frustum), 09 (the eye term is the only per-eye piece) and 10
(submission), [18](18-camera-and-hands.md), [21](21-sights-and-scopes.md) section 9 (the sights job's snapshot
measurement), [22](22-performance-levers.md), [25](25-calibration.md) section 9, [13](13-existing-me-vr-mods.md)
("World scale": Shepard's skeleton is about 100 units per metre).

## 1. How Halcyon's Stereo 2 works (read in the code)

| | Upstream Stereo 2 |
|---|---|
| Passes | `FViewportClient::Draw` twice per frame: pass 0 = the game's Draw, pass 1 = the replay (`render_hook.cpp` `HookFViewportClientDraw`) |
| Eye position | Head centre (the lean: `ApplyViewOffsetSEH`, 50 x `leanGain` units per metre, forward x3 unless life-size movement) plus `+-sfr2HalfEyeUU` (3.4) along the **game camera's right axis before head look** (`ApplyReplayProofOffsetSEH`). Swap on (the default) flips the sign and the routing together |
| Orientation | Game camera x head yaw and pitch (`ApplyHeadRotation`), roll removed |
| Projection | One symmetric window for both: the widest half angle of both eyes each way x `vrFill` (`ApplyTargetFovOverride`, `[RENDER_FOV_MATCH] halfH=1.073377 halfV=0.925725` on the PS VR2), written into the CPU and GPU matrices |
| Convergence | At submit: each finished image is shifted by `sfr2Convergence / 2` of its width (`CopyStereoPassCaptureToEyes` -> `CopyTextureToEyeFullFrame(eye, src, -+conv/2)`); the sign is bound to the pass, so with swap on (pass 0 = the right eye) the right image moves right and the left image left: **divergent** |
| Declared | Each eye at the runtime's real eye position, the roll-removed head orientation, the symmetric window; on SteamVR cropped to the eye's own frustum (`[STEAMVRFOV]`: 2138 of 2824 px kept) |

Every error is in the render, not the declaration: the runtime shows each image as if seen from the true eye, so
whatever the render's eye positions and the shift put into the image is what the player fuses.

**The measured error** (`TestMetricStereo`; the user's profile `Custom 1`: `sfr2HalfEyeUU 3.4`, lean gain 2 =
100 units per metre, `sfr2Convergence 0.012`, swap on; the PS VR2's 61.5-degree half window; eyes 6.21 cm apart as
in the sights job's snapshot). A point straight ahead of the head at the true distance reads at:

| true | metric | upstream (user, swap on) | upstream, swap off | upstream, head turned 45 deg, no shift |
|---|---|---|---|---|
| 0.2 m | 0.200 | 0.210 | 0.162 | 0.255 |
| 0.3 m | 0.300 | 0.340 | 0.229 | 0.385 |
| 0.5 m | 0.500 | 0.677 | 0.345 | 0.644 |
| 1 m | 1.000 | 2.61 | 0.553 | 1.291 |
| 2 m | 2.000 | beyond infinity | 0.794 | 2.58 |
| 10 m | 10.00 | beyond infinity | 1.218 | 12.9 |
| far | far | the eyes diverge 2.53 deg | 1.405 | far |

- The shift is 0.012 NDC x tan(61.5 deg) = 0.0221 tangent units per eye: 1.27 degrees, 16.9 px of a 2824 px eye.
  The sights job measured exactly that on far scenery (research/21 section 9: 649-652 px against 685.6), and
  `tools/stereo-check.py` reproduces it from the same snapshot (median -28 px over a default grid, -30 to -35 on
  the far points).
- With swap on everything past 1.5 m needs the eyes to diverge; a pistol held at 0.5 m reads at 0.68 m at the same
  angular size, so it looks 35% bigger: "the pistol looks huge". Metric quads (the scope disc, the orbs) sit at
  their true depth, so they read nearer than the game's picture: "the orbs looked closer".
- Without any shift, 3.4 uu per eye at 100 uu/m against 3.1 cm puts everything 9% near.
- Upstream's eyes stay on the **body's** right axis while head look turns the view: at 45 degrees of head turn
  the baseline is 30% short and the eyes sit fore and aft; at 90 degrees the picture is flat. (On foot with hand
  aim, head look is always on.)
- Head roll moves the real eyes up and down; upstream renders both at the same height.

## 2. The metric mode (`MELEVR.ini sfr2Metric`, our deploy turns it on)

**Pose.** The plan (`src/plugin/MetricStereo.cpp`, `MevrBridge::PlanStereo`, once per frame next to
`SetViewOffset`) takes each eye's position from `xrLocateViews` (`g_subViews`, the same sample the lean and the
delayed pose tags use) and gives each pass its eye's offset from the eye centre, in game units along the game
camera's right, up and forward axes: the axes `ApplyViewOffsetSEH` and the lean use, with the lean's own mapping
(x -> right, y -> up, -z -> forward). So pass 0's camera is at `lean(eye centre) + offset(left eye)`: exactly the
eye's real place, roll included, at the view's scale (`core/vr/MetricStereo` `BodyOffsets`, unit-tested with the
PS VR2's logged poses: 6.30 cm -> 6.30 uu at 100 uu/m, +-3.15 along the head's right). When head look is off (the
Insert menu, the Mako, head aim) the eyes' head-relative offsets are laid along the camera's axes instead, so the
pair keeps a horizontal baseline in its own view. The render-side edit is upstream's own (ViewOrigin +
PreViewTranslation, `ApplyMetricEyeOffset` in place of `ApplyReplayProofOffsetSEH`), latched by the main pass and
reused by the replay (`[PAIRLATCH]`), recorded with the pair's head-look arm.

**Scale.** The view's own units per metre: Halcyon's 50 x lean gain, the scale the lean and our hands (arm IK,
`TrackingFrame::unitsPerMeter`) already use, so binocular depth, motion parallax and the hands agree. Life-size is
100 (Shepard's skeleton), i.e. "Life-size movement" on (lean gain 2), which the user's active profile has. At lean
gain 1 the plan line says `NOT life-size: ... the world reads 2.00 times its size`. `sfr2MetricDepth` ("Extra
depth", 0.5-2, default 1) multiplies the real eye distance.

**No convergence.** A metric pair is submitted with no shift (`convHalfU = 0`), and routed by its own plan: pass 0
is always the left eye, so `sfr2SwapEyes` (whose job was to pair a pass with its camera side, and whose
pass-bound shift sign made the convergence divergent) is ignored and greyed out while the mode is on.

**Projection: two levels.**
- **Mode 1** (life-size, the symmetric window): Halcyon's projection untouched. Used wherever screen-space UI is
  drawn into the eye images: VR conversations and cutscenes (their UI stays in the eyes, Halcyon's layout), live
  GUI overlays (mode 7), any frame the HUD panel isn't armed, and `sfr2MetricEyeFrustum = 0`. The UI is mirrored
  into both images at the same pixels; with one window for both eyes the same pixel is the same direction, so the
  UI reads at infinity (upstream with swap on put it beyond infinity, 2.5 degrees divergent).
- **Mode 2** (life-size, each eye's own frustum): in gameplay with the HUD panel taking the UI, the GPU's matrix
  (`TranslatedViewProjection`, what the shaders read) gets the eye's own off-centre frustum (the runtime's angles x
  vrFill): `p0 = 2/(r-l)`, `p8 = -(r+l)/(r-l)`, `p5 = 2/(u-d)`, `p9 = -(u+d)/(u-d)`, scaled by the projection's
  `M[2][3]` (guide 08 section 3.2, in UE3's row-vector, w = z form; `ApplyMetricFrustumSEH`, after head look,
  before the `[INVMAT]` refresh, which then inverts this matrix). The CPU's projection and ViewProjection keep
  Halcyon's symmetric window, which contains both eyes' frusta (unit-tested for fills 0.8-1.2). Why only the GPU:
  ME1 places its projected HUD markers through the CPU matrices (`[SFRCONV-UI]`), and the panel's markers must not
  move; culling, Umbra and the CPU's whole view stay exactly as upstream. The submit declares the eye's own FOV for
  each eye (from the pair's record), so SteamVR's crop becomes a no-op: the whole image is used.
- Why mode 2 can't hold screen-space UI: the same NDC is a different direction in each eye's frustum, 24 degrees
  left in the left eye and 24 right in the right eye at the centre on a PS VR2 (unit-tested). A UI pixel mirrored
  into both images would split 48 degrees.

**Tag what you rendered.** The main pass records what it latched (mode, the eye of pass 0, each pass's offset and
frustum) under its head-look arm (`RenderHook::GetMetricRecord`). The captured pair carries that arm
(`g_sfrPairArmThisFrame`), so the submit's routing, shift and declared FOVs come from the pair's own record, held
pairs included; a miss falls back to the newest record. A live-mono fallback (the pair hold ran out: the replay
stopped) declares pass 0's frustum for both eyes in mode 2.

## 3. What had to change, and what didn't

| Path | Metric mode |
|---|---|
| HUD panel (`HalcyonOverlays`, UI draws after both passes) | Unchanged: the panel's UI is placed through the CPU matrices, which keep the symmetric window. Mode 2 is gated on `HudPanelArmed()` |
| UI in the eye images (VR cine, menus over the world, panel off) | Mode 1: the symmetric window, same pixels in both eyes = the UI at infinity, no longer beyond it |
| VR conversations and cutscenes | Mode 1, real eye poses, `cineSepScale` multiplies the extra depth as it multiplied the separation; cine zoom keeps Halcyon's projection path as is |
| Flat cine, movies, mono menus, the galaxy map (flat) | Not Stereo 2 (`forceFlatCine`): upstream exactly |
| vrFill | Mode 2 fills each eye's own angles (Halcyon scales angles, not tangents); mode 1 is Halcyon's |
| Scope picture, menu backdrop, snapshots (`SetReleaseTap`, `projectionFov`, `LayerComposite`) | They read the submitted views' pose, FOV and rect, so they follow the per-eye declaration by themselves |
| Colour blit (`MEVR_EYE_OUT`) | Unchanged: the same eye blit, with a zero source offset instead of the shift |
| Upstream's convergence slider, eye separation, swap | Ignored while metric is on; greyed out in Halcyon's menu, hidden behind "Extra depth" in ours |

## 4. Cost

- **Render count and frame loop: unchanged.** Still two `FViewportClient::Draw` per frame; the early
  `xrWaitFrame`, `xrBeginFrame`/`xrEndFrame` and their order are untouched; no new D3D work on the render thread.
- **Visibility: unchanged.** The CPU matrices (culling, Umbra's camera, the pre-pass's visible set) get exactly
  upstream's symmetric window in both modes; mode 2 changes only the GPU matrix. No frustum is wider than today.
  (Observation for lever 3, research/22: the eye offsets, upstream's and ours, live only in ViewOrigin and
  PreViewTranslation, so both passes' CPU `ViewMatrix`/`ViewProjectionMatrix` are already identical; whether
  Umbra's camera takes its position from them or from ViewOrigin decides whether pass 1 resolves the same query
  again **[verify]** with Ghidra at 0x7655A0 / 0x4FD550. A shared visibility would need nothing wider than today's
  window plus the eyes' 3 cm.)
- **CPU added:** per perspective view, one 4x4 multiply and a small copy (mode 2), one uncontended mutex
  (the record); per frame, the plan (a few hundred float operations). Microseconds.
- **GPU:** the same render target, so the same pixel count. In mode 2 every rendered pixel is shown: horizontally
  1101 px per tangent unit instead of 834 (3072 px over 2.79 instead of 3.68 tangent units), 32% sharper at the
  same cost; or `sfr2ResX` 24% lower (3072 -> about 2340 wide) for upstream's sharpness at 24% fewer pixels
  (a lever for the performance work, untested).
- **Measuring it:** the perf probe already splits the render thread at pass 0's capture (`kPass0`); its
  `-probe.json` now says `sfr2Metric` and `sfr2MetricEyeFrustum`, so one capture with each setting
  (`tools/perf-probe.ps1 -Tag metric` / `-Tag upstream`) compares them. Expected: no difference beyond noise.

## 5. Logs and snapshot fields

- `[MEVR-STEREO] stereo mode 2: life-size, each eye's own frustum; eye distance 0.0630 m = 6.30 uu at 100.0 uu/m x
  1.00 extra depth; runtime FOV left (...) right (...) deg; the symmetric window +-61.5 x +-53.0 deg (...); the eyes'
  orientations 0.00 deg apart; pass 0 = left eye from (-3.15, ...) uu through tan (...); pass 1 = right eye ...`
  (Halcyon's log and ours; at the first plan and whenever the mode, the eye distance or the scale changes; says why
  mode 1 and whether the scale isn't life-size).
- `[MEVR-STEREO] submit (pair rendered in mode N): left eye declared at (...) fov (...) deg, right eye ...,
  0.0630 m apart; orientation (...)` (on a mode change, else once a minute).
- Render side: `[MEVR-STEREO] pass 0 (eye 0) from its eye: right -3.15 up 0.00 forward 0.00 uu (world ...)` (first
  eight, then every 3600) and `[MEVR-STEREO] pass 0 frustum: tan L ... -> p0 ... p8 ... (game's own p8 ... p9 ...
  p11 ...)` (the game's own p8/p9 should be 0 and p11 1; if not, say so).
- Snapshot JSON: `"stereo"`: mode, eye distance (m, uu), units per metre, extra depth, cant, the runtime's FOVs,
  each pass's eye, offset, terms and tangents, and what was declared per eye. The layers already carry the
  submitted poses, FOVs and rects.
- `tools/stereo-check.py [stamp|latest] [x,y ...]`: the far-scenery disparity against the declared geometry (about
  0 when metric; -30 to -35 px upstream with swap on).

## 6. Risks

- **Screen-space effects placed from the CPU matrices but drawn into the eye images** (lens flares, light shafts'
  screen position, coronas, if ME1 has them) would be off by up to the frustum's centre shift in mode 2 (24 degrees
  at the image centre). The world, its lighting and fog (the inverse matrices are refreshed after the edit with
  `invMatrixFix`, on in the user's profile) aren't. Fallback: `sfr2MetricEyeFrustum = 0` (mode 1). **[verify]**
- A UI draw the HUD panel can't take (a second target kind in one frame) is mirrored into both eyes at the same
  pixels; in mode 2 those copies split. Rare; the `[MEVR-HUD]` lines name the draws.
- The game's own projection could carry a non-zero off-centre term (`p8`/`p9`) that mode 2 overwrites: the
  frustum line logs the game's own values.
- Canted displays (Pimax, some Varjo): each pass still renders facing the head; the plan line reports the
  eyes' angle (0 on the PS VR2 and Quest).
- Not life-size at lean gain 1 (50 uu/m): consistent with the lean and the hands, but the world reads twice its
  size; the log says so. Turning on "Life-size movement" fixes both.
- The record lookup can miss at a transition (the capture reads the arm a frame late when the game thread runs
  ahead); then the newest record is used: a frame of the wrong routing or FOV at a mode change at worst.
- Conversation UI at infinity behind close faces may be uncomfortable; a UI depth would be a per-eye shift of the
  mirrored UI draws (not built).
- Untested in the headset.

## 7. Files

`src/core/vr/MetricStereo.{hpp,cpp}` (math), `src/plugin/MetricStereo.{hpp,cpp}` (plan, logs, snapshot block),
`src/halcyon/le1/render_hook.{h,cpp}` (`SetSfrMetric`, `ApplyMetricEyeOffset`, `ApplyMetricFrustumSEH`, the
records), `src/halcyon/le1/xr_session.cpp` (the plan call, the pair's routing and shift, the declared FOVs, the
submit note), `src/halcyon/le1/mevr_bridge.h` (`PlanStereo`, `NoteStereoSubmit`), `vr_config.{h,cpp}`
(`sfr2Metric`, `sfr2MetricDepth`, `sfr2MetricEyeFrustum`), `vr_menu.cpp` (Insert menu rows), `assets/menu/main.rml`
and `src/plugin/MainMenuSettings.cpp`, `src/core/vr/MenuSettings.cpp` ("Life-size stereo", "Extra depth"),
`src/plugin/TestLoop.cpp` (`"stereo"`), `src/plugin/PerfProbe.cpp`, `tools/deploy-asi.ps1`,
`tools/stereo-check.py`, `tests/Main.cpp` (`TestMetricStereo`).
