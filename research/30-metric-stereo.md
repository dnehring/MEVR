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
- **Shaders that read the projection another way (the performance review, 2026-10-23).** Mode 2 rewrites only
  TranslatedViewProjection. Any shader parameter that carries the projection some other way still describes the
  symmetric window: ambient occlusion and fog that rebuild view or world positions from depth with the projection's
  scale terms, light shafts and other screen-space effects positioned from the CPU's matrices, and
  ScreenPositionScaleBias-style screen-to-UV mappings. Inverse matrices are only refreshed when `[INVMAT]`
  validated them (`invMatrixFix`). Symptoms would be a misplaced or sliding effect in one eye: fog bands, AO halos
  offset from their objects, shafts or flares away from their light. Fallback: `sfr2MetricEyeFrustum = 0`.
  **[verify]** in the headset (step 29).
- **Containment.** The CPU's culling window must hold each eye's own frustum, or objects at that edge pop in. By
  construction it does (the symmetric window is the widest eye angle x the same fill), and it is checked live: the
  render side logs the CPU window next to each pass's frustum (`the CPU window +-a x +-b holds it`) and warns
  `[MEVR-STEREO] WARNING pass N renders WIDER than the CPU's culling window ...` per view, and the plan line adds
  `the CPU window (1/P0, 1/P5) ... holds the eyes' ...`, warning after 10 plans in a row that don't.
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

## 8. Common floor-locked centre (2026-11-07)

See research/58 section 6. The `[MEVR] FloorViewTranslation` seam in render_hook.cpp's ApplyViewOffsetSEH corrects the
common world Z before ApplyMetricEyeOffset and before head look: cylinder bottom + units/m * live STAGE/LOCAL_FLOOR eye
height. The two metric offsets remain displacement from the midpoint; the same latched common correction applies to both
passes, including the one-Draw synthesized view. HostServices v34 carries the pawn sample and the host view's corrected
centre/pose to the mod. `[Body] floorLock = 0` retains the previous ordering and arithmetic. Cinema, map scene cameras,
cover, third person, front end and menus are excluded; world holds carry the corrected on-foot base.

FloorLockTests exercises recenter origins, mode/bone/pitch/framing terms, metric separation, gates and frozen standing scale
under crouch. This does not establish headset alignment, support height on slopes, or runtime reflection success.
**Current replay cannot validate floor lock:** recordings lack the corresponding STAGE/LOCAL_FLOOR sample and replay leaves
that measurement live. A versioned per-frame valid/source/eyeHeight/sequence/time payload, replay-clock freshness mapping
and substitution alongside recorded poses would supply a deterministic floor fixture; see research/58 section 6. No game,
replay, deployment or headset run was performed for this change.

## 9. The sequels (2026-11-16, lane LS)

The owner, 2026-11-16: "It should always be life sized stereo for all games." Halcyon's ME2 and ME3 renderers
(`src/halcyon/le2`, `le3`) shipped upstream's stereo; this section is what they did, read in the code, and what replaced
it.

### 9.1 Upstream on the sequels (read in the code)

Both trees are the same design (ME3's is a port of ME2's); the functions have the same names in both.

| | ME2 (`src/halcyon/le2`) | ME3 (`src/halcyon/le3`) | ME1's life-size path (sections 1-2) |
|---|---|---|---|
| Passes | `DrawDetour` (calcview_hook.cpp): the game's Draw is pass 0, a second `FViewportClient::Draw` is pass 1 (`t_replay`), each pass its own `FSceneViewState` (lane SP's block) | the same | two Draws (or one, sharePass 5) |
| Eye offset | `ApplyEyeOffset(sv, -+HalfEyeUU)` along the game camera's right (ViewMatrix column 0) before head look: ViewOrigin, PreViewTranslation, `VM[12]`, both view-projections rebuilt. `SwapEyes` flips the sign and the routing together | the same | each eye's real offset from the eye centre (`xrLocateViews`), along right, up and forward, turned with the head |
| Eye distance | `HalfEyeUU` 3.35 in the deployed `MELE2VR.ini` (code default 3.55): a fixed 6.7 cm at the view's scale | `HalfEyeUU` 1.6 (code default): 3.2 cm, half a real eye distance | the runtime's (6.3 cm on the fake headset) |
| Scale (the lean) | `ApplyHeadPosition`: 50 x `PosScale` uu per metre, `PosScale` 2.00 deployed = 100 uu/m; forward x3 (`kFwdLeanEmphasis`) | 50 x `PosScale` 2.0 = 100 uu/m, forward x3 (also in the GUI marker's centre-eye camera) | 100 uu/m, forward x1 (`ForwardLeanEmphasis`: life-size movement only since 2026-11-06) |
| Lean centre | `DriveHeadLook(views[0], ...)`: the LEFT eye's position, not the eyes' centre (research/le2-renderer-integration.md measured "exactly the left eye's lean") | the same | the eyes' centre (`headPos`, xr_session.cpp) |
| Orientation | `ApplyHeadRotation`: the smoothed head's yaw and pitch, no roll | the same | the same (roll removed) |
| Projection | `ApplyFov`: one symmetric window for both passes, written into the CPU projection and both products (`WidenProjection`) and pre-written as the camera's FOV (`[FILLSRC]`); the window is the left eye's vertical half angle with the horizontal from the eye's recommended aspect (`ApplyFovFill`, me2_xr.cpp), scaled up to cover both eyes only on Meta's runtime with `QuestFovMatch` (`[LINKFOV2]`). On the fake headset: +-41.6 x +-45 degrees for eyes of +-45 x +-45 (`[EYETAG2]`) | the same | mode 2: each eye's own off-centre frustum on the GPU matrix, the CPU window the eyes' union |
| Convergence | at submit: each finished eye image shifted by `Convergence`/2 of its width, the sign bound to the pass (`CopyTextureToEyeFullFrame(..., +-convPx)`); `Convergence` 0.0197 with `SwapEyes` 1: **divergent**, as ME1's upstream | `SfrConvergence` 0.03 with `SwapEyes` 0: **crossed**, infinity pulled near | none |
| Declared | each eye at its real position (the delayed pose tag), the symmetric window; SteamVR and Meta's runtime crop the rect to the runtime's frustum at submit | the same | each eye's own frustum in mode 2 |

What a viewer reads, from the pair model of section 1 (`tests/MetricStereoSequelTests.cpp` keeps the facts as checks; the
viewer's eyes 6.3 cm apart, the fake headset's window, tan 0.889 horizontally, a point straight ahead):

| true | ME2 upstream | ME3 upstream | life-size |
|---|---|---|---|
| 0.2 m | 0.21 | 0.30 | 0.20 |
| 0.5 m | 0.64 | 0.54 | 0.50 |
| 1 m | 1.97 | 0.74 | 1.00 |
| 2 m | beyond infinity | 0.91 | 2.00 |
| 10 m | beyond infinity | 1.11 | 10.0 |
| far | the eyes diverge 2.0 degrees | 1.18 m | far |

On a PS VR2 (the window's tan about 1.18) ME2's far scenery needs 2.7 degrees of divergence and ME3's sits at 0.89 m.
ME2 repeats ME1's upstream error (a pistol at 0.5 m reads 0.64 m: "the pistol looks huge"); ME3 squeezes everything from
30 cm to infinity into 0.3-1.2 m and halves every disparity: a miniature world on a near plane.

### 9.2 Life-size on the sequels: ME1's plan, one render-side header

- **The plan is ME1's.** `plugin/MetricStereo` is compiled into all three hosts; `PlanStereo`, `NoteStereoSubmit` and
  their structs moved from LE1's `mevr_bridge.h` into `src/halcyon/MevrSeams.h` (group 2) unchanged but for one input,
  `windowHoldsEyes` (default true, LE1's case). The plan now also asks `hud_panel::Armed()` itself (LE1 already passed it
  in: the same value). `MevrSeams::LifeSizeStereo()` is the sequels' switch.
- **The per-game facts** (each tree's `MevrPlanStereo`, me2_xr.cpp, after `DriveHeadLook` and `ApplyFovFill` in the SFR
  frame): the runtime's two views, head look as applied (`GetHeadLookApplied`), the roll-free smoothed head, the
  units per metre (50 x `PosScale`), the window (the fill target), whether the window holds both eyes
  (`core/vr/MetricStereo::WindowHolds`), whether the stereo renders the world (SFR, not the menu, a cinematic or the 3D
  cinema screen, which keeps its own parallel cameras). The plan goes to `MevrMetricView::Publish`.
- **The render side** is one header both trees include, `src/halcyon/MevrMetricView.h` (GPL, adapted from Halcyon's
  `ApplyEyeOffset` and LE1's metric frustum): pass 0 latches the newest plan, pass 1 keeps it; `ApplyEyeOffset` moves
  the camera by the pass's offset along the camera's right, up and forward (ViewOrigin, PreViewTranslation, the
  ViewMatrix's translation row, both products) in place of Halcyon's half eye, before head look (both are translations
  along the pre-look axes; the head rotation then turns the view about the eye's own place); `ApplyEyeFrustum` in mode 2
  writes the eye's own off-centre frustum into the GPU matrix only, scaled by the renderer's zoom (ADS) so a zoom
  magnifies alike in both modes, and logs whether the CPU window holds it. The pass after the cinema screen's branch
  and lane SP's view-state block; with life-size off it is Halcyon's `ApplyEyeOffset` exactly.
- **The lean** from the eyes' centre (upstream: the left eye) at 1:1 forward (upstream x3), in `ApplyHeadPosition`, the
  view published to the mod (`OnViewBuilt`) and ME3's GUI marker camera alike; `PosScale` pinned to 2 (100 uu/m, the
  scale both sequels' deploys already had, and the hands' and the floor lock's).
- **The submit** routes a pair by the plan it was rendered with (`MevrMetricView::LatchedPlan`: pass 0 the left eye, no
  swap), shifts nothing, and in mode 2 declares each eye's own frustum (a mono fallback, pass 1's image in both eyes,
  declares pass 1's). `HalfEyeUU`, `Convergence`/`SfrConvergence` and `SwapEyes` stay in the renderers' inis, unused.
- **The baseline**: ME2's `ApplyMevrBaseline` and ME3's `LoadValues` set it after every ini load, as ME1's
  `ModernBaseline` pins `sfr2Metric` (the owner's rule is the yes). For a before/after measurement, `[Debug]
  upstreamStereo = 1` in `ASI\<GAME>MEVR.ini`, or in a development build `%LOCALAPPDATA%\MEVR\stereo.measure` with
  `[Stereo] mode = upstream` (the ME3 preview runner keeps LE3MEVR.ini exact), brings back upstream exactly; `mode =
  nocover` (or `[Debug] stereoWindowCoversEyes = 0`) keeps the renderer's own window (below). Logs: `[MEVR-STEREO] life-size stereo: ...` at the start, then ME1's plan and submit lines
  and the render side's `pass N (eye E) from its eye` and `pass N frustum` lines; the snapshot's `"stereo"` block on
  the sequels too.

**Each eye's own frustum, and the window.** The sequels' symmetric window is the left eye's vertical half angle with
the horizontal from the eye's recommended aspect, not the eyes' union (ME1's): on the fake headset +-41.6 x +-45
degrees for eyes of +-45 x +-45 (so 3.4 degrees of each eye's side were never shown), on a PS VR2 narrower than the
eyes' outer edge too. An eye rendered through its own frustum in that window would be culled at its outer edge
(objects popping in), so the plan uses mode 2 only where the window holds both eyes (`windowHoldsEyes`), and with
life-size on the renderers widen their window on every runtime by the one uniform factor they already used on Meta's
runtime (`[LINKFOV2]`), until it holds both eyes: ME1's union window. The worry was `[UIRATIO]` (the sequels scale the
game's UI by tan(game) / tan(render) per axis); measured in 9.3, the HUD panel is pixel-identical with and without the
widening. `[Debug] stereoWindowCoversEyes = 0` keeps the renderer's own window (mode 1 except on Meta's runtime).
The widened window lets the CPU cull a wider window (vertically 1.125x on the fake headset, about 1.4x on a PS VR2):
more objects pass, a cost lane SP's levers can measure; the GPU renders each eye's own frustum at the same size.

### 9.3 Measured (fake headset, 2026-11-16)

ME2, Save 4 (Freedom's Progress, standing in the open), the head held at 0, 35 and -40 degrees by replayed recordings
(`tools/make-look-recording.py --yaw`), one snapshot each, `tools/stereo-check.py`'s median over its grid on far
scenery (crossed +, divergent -; a few px crossed is right for scenery tens of metres away):

| ME2 | yaw 0 | yaw 35 | yaw -40 |
|---|---|---|---|
| upstream (`stereo.measure mode = upstream`) | -72 px = -0.90 deg per eye | -60 px (1 match) | -79 px = -0.98 deg |
| life-size, the renderer's window (mode 1) | +6 px = +0.07 deg | +5 px | +2 px |
| life-size, the window holding the eyes (mode 2, now the default) | +2 px = +0.03 deg | +7 px | +5 px |

- Upstream matches the model's 1.0 degree per eye of divergence (40 px of 4096 each).
- Life-size: the plan line `stereo mode 2: life-size, each eye's own frustum; eye distance 0.0640 m = 6.40 uu at 100.0
  uu/m`; the eyes turned with the head (at yaw 35 the left eye 1.84 uu forward); the declared eyes 6.4 cm apart; the
  render side `pass 0 frustum: tan L -1.0000 R 1.0000 ...; the CPU window +-1.0000 x +-1.1250 holds it`; each eye
  declares its own +-45 degrees (upstream +-41.6).
- The HUD panel (`-hud.png`): life-size against upstream 0.4% of pixels apart by more than 32/255 (what animates);
  mode 2 against mode 1 0.0%, the same bounding box and coverage.
- No mesh missing between the eyes (the pair at yaw 0 looked at side by side; each pass culls with its own CPU window,
  which holds its frustum). With asymmetric eyes (a PS VR2) the culling edge is untested: the fake headset's eyes are
  symmetric, and no recording with per-eye FOVs was at hand.

ME3, the renderer preview (`tools/le2-halcyon-run` `-RendererPreview`), Save 31 inside the Normandy (third person in the
preview; the walls about 3 m away, so nothing is far: crossed disparity is right here). The preview's snapshots have no
`-left-world.png`; the composited eyes are the world alone there (no layers of ours), copied to that name for the tool:

| ME3 | yaw 0 | yaw 35 | yaw -40 |
|---|---|---|---|
| upstream | +112 px = +1.86 deg per eye | +113 px = +1.87 deg | +109 px = +1.81 deg |
| life-size (mode 1) | +35.5 px = +0.66 deg | +39 px = +0.73 deg | +47.5 px = +0.89 deg |

- Upstream's 1.86 degrees are its 1.53-degree crossed shift plus 3.2 cm eyes: the walls read about a metre away. The
  same number puts them at 2.8 m, where life-size's 6.4 cm eyes should give 0.65 degrees per eye: measured 0.66.
- ME3 stays in mode 1 (the plan line: `the symmetric window because the game's UI is in the eye images`): ME3's game UI
  is still drawn into the eyes (the HUD panel isn't armed there), ME1's rule for that case; the window was widened (each
  eye declares +-45 x +-48.4 degrees) and mode 2 follows once the panel takes the UI.
- Each variant needs its own `prep.ps1` on the ME3 preview (the first run rewrote the renderer's config, and `launch.ps1`
  then refuses with `Preview configuration changed since prep`).

### 9.4 After the review: no frame of upstream's stereo, narrow views, the PS VR2's eyes (2026-11-17, lane FXR)

An adversarial review of main 57ae5b4e (lane REV) named six defects in 9.2's render side. Each was checked in the code
before anything changed; what each turned out to be:

1. **"ME2's flat frames render through the left eye's life-size frustum."** Not so for the screens it names. Both
   calcviews return the raw view before the SFR branch whenever `D3DCapture::GetMenuMode()` or the cinematic flag is
   set (le2 calcview_hook.cpp at the `Milestone B1` comment, le3 the same test), and `GetMenuMode` is true in game modes
   8, 9 and 10 and in a paused mode 7 (with `MonoMenus`, which the baseline pins): the galaxy map, the planet scan,
   paused GUI screens, movies and the flat cinematic never latch a plan. What was real: the frames that are shown flat
   while the renderer's flags still say stereo (ME2: a loading movie, `moviePlayback` without mode 8; ME3: the wait for
   a fresh pair after any flat owner, `SOURCE_WAIT`, and a stalled capture) rendered pass 0 with the last plan, one eye's
   off-centre frustum on the flat panel. Now each RunFrame calls `MevrMetricView::Expire()` on a frame it shows flat, and
   the next pass 0 renders from the eyes' centre (mode 1, no offset) until a stereo present publishes again.
2. **One upstream frame at mode changes.** Real, on both sequels. The plan the Present thread publishes while the 3D
   cinema screen is up is mode 0 (`sfr2Active` is false there), and nothing was published before a process's first
   stereo frame, so the first pass 0 after the screen (and the first of a run) latched mode 0: upstream's half eye, and
   at the submit upstream's routing (`SwapEyes`) and convergence shift. The latch (`ChooseLatch`, pure and unit-tested)
   now never takes mode 0 while life-size is on: the last plan made for stereo, else the eyes' centre. A Draw rendered
   for the cinema screen latches the screen's routing (pass 0 the left eye, mode 1), so the submit after the screen goes
   down routes that last screen pair as it was rendered (it used `SwapEyes` and the convergence: the swapped frame).
   `[Debug] upstreamStereo = 1` still gives upstream exactly. The latch logs each change of source (`[MEVR-STEREO] pass 0
   renders ...`, with counts).
3. **ME2's narrow views in mode 2.** Real. ME2's `ApplyFov` leaves a narrow view without a weapon at the game's raw FOV
   (`FovExit("narrowNoWeapon")`: a conversation's first frames before the cinematic debounce, a camera blending back from
   one), while the plan, made from the fill target, is mode 2: the GPU rendered the eye's whole frustum through a CPU
   window of about +-31 degrees, so the edges were culled (the old line said `does NOT hold it` and wrote it anyway). Now
   `ApplyEyeFrustum` checks the CPU window against both passes' frusta (x the zoom) before writing; a window that can't
   hold them lowers the latch to mode 1 for the Draw's two passes and the submit (`LowerLatchToSymmetric`), which then
   declares the symmetric window it rendered. ADS is unaffected (its CPU window narrows by the zoom the frusta are scaled
   by). ME3 has no such exit; the check covers it alike.
4. **Render-thread guards.** `umbraEyeMatrix` (RenderLevers `UmbraIssueQueriesInner`) read the commander's view and
   rewrote FViewInfo +0x260 with no SEH, now on ME2's and ME3's render thread every eye render: the reads go through
   `ReadQueryView` and both writes through `WriteViewProjection`, under SEH (a fault counts in the levers line and the
   game's own matrix is used). ME3's `umbraCommandGate` (0x1767208) is the LE3 table's `Umbra command gate` row now
   (`exe::Entry::tail`: its only reference has an immediate after the displacement), read under SEH.
5. **Tests.** `tests/MetricViewTests.cpp` (ctest `mevr-metric-view`: the latch's every source, the submit's plan, the
   containment on a synthetic FSceneView, the offset); `RenderLeversTableTests.cpp` compiled as each sequel
   (`mevr-le2-render-levers-table`, `mevr-le3-render-levers-table`: the layout, `perEyeViewStateDefault`, the verified
   groups, the gate row); ExeTests checks an entry with a tail.
6. **The PS VR2's eyes, replayed** (the check nobody had done). Both sequels' hosts wrap `xrLocateViews` through the test
   loop (`HalcyonSequelBridge`'s `WrapSessionFunctions`), so `tools/replay.ps1 -Fovs` works on them as on LE1 (a
   development build, or `[Debug] requests = 1`). A still, level head with the owner's PS VR2 eyes (left -61.5, 43.4,
   +-53.0 degrees, the right mirrored, 63 mm apart; taken from the 2026-10-23 19:34 recording, every frame the same:
   `fxr-psvr2-still-level.mevrrec`), one stretch on the fake headset (`%LOCALAPPDATA%\MEVR\fxr`):
   - **ME2, Save 4:** mode 2, each eye through its own frustum (`pass 0 frustum: tan L -1.8418 R 0.9472 U 1.3290 D -1.3290
     ... the CPU window +-1.8418 x +-2.0720 holds it`; declared `(-61.5, 43.4, 53.0, -53.0)` and the mirror), far scenery
     +3.2 px = +0.06 degrees per eye (`stereo-check.py`, 5 matches): parallel. Side by side, the left eye sees the left
     wedge, the right the right; nothing missing at either outer edge; the lamps' glow on the lamps in both eyes. The
     CPU window is 1.56x the eyes' vertically (the renderer keeps the backbuffer's aspect: more objects pass the cull,
     lane SP's cost, no error).
   - **ME3, Save 31 (renderer preview):** mode 2 the same way; +28.6 px = +0.74 degrees per eye crossed on the Normandy's
     corridor (about 2.4 m: right for a room; upstream read +1.86). **But every light's lens flare is misplaced in each eye:**
     the flare sprites are placed through the CPU's symmetric window and drawn into the eye's off-centre image. Measured on
     one ceiling lamp in the left eye: the lamp at x 1233 of 3072, its flare at 920; the symmetric window predicts 934. The
     same flare lands at the same pixel of both eye images, which are different directions (0.9 tan apart), so the two
     can't fuse: a doubled, diverging flare by every lamp. The fake headset's symmetric eyes hid it (no off-centre term).
     ME2's lamps at Save 4 showed no such sprite. ME1 knew the same mechanism for the sun's sprite, near the edges only
     (section 6, research/37). Not fixed here: the sprite's screen position comes from a CPU matrix (most likely the view's
     ViewProjectionMatrix, +0x260, which the HUD's projected markers also read), so the cure is in ME3's flare path or in
     what +0x260 means in mode 2; an owner question until then (mode 1 on ME3 avoids it at mode 1's angular resolution).
   - **The latch in the game:** ME2's load ends on the cinema screen for 0.3 s (`[VRSTATE] gm 6 ... -> 3D screen (MEVR)`):
     `pass 0 renders the cinema screen's routing`, and the submit after it `pair rendered in mode 1` (it was mode 0:
     `SwapEyes` and the convergence shift), then `the newest plan: mode 2`. ME3's load (`SOURCE_WAIT`,
     `CAPTURE_STALL_MONO`) rendered 3 passes from `the eyes' centre (the last present was flat: the plan expired)`, then
     `the newest plan`. No narrow view occurred in these runs (its check is the unit test).
   - **The other symmetric-window assumptions:** ME3's `[INVMAT]` refreshes each inverse from the matrix it pairs with after
     the frustum write (the translated view-projection's inverse follows the eye's frustum; the projection's stays the CPU
     window's, as its CPU users need). `[FILLSRC]` pre-writes the CPU window (the union), consistent. `[DISTFIX]`'s
     reprojection of the distortion buffer about the screen's centre (`(uv - 0.5) / scale + 0.5`, scale tan(raw) /
     tan(render)) assumes the rendered window is centred: in mode 2 the GPU's window is off-centre by (L + R) / 2 (0.45 tan
     on a PS VR2) and its half width is (R - L) / 2, not tan(render); if the distortion buffer is drawn at the raw window, as
     the fix assumes, refraction lands off its glass in mode 2. No distortion drew in these runs (`[DISTFIX] draw` absent),
     so it is unmeasured; it needs a scene with glass or heat haze. ME2's motion blur's previous-frame matrix would mismatch
     the off-centre frustum the same way, but `MotionBlur False` is applied on every game (game/SystemSettings). Black
     corners on head roll: the render and the declared pose are roll-free on all three games and mode 2 declares exactly
     the eye's frustum (fill 1.0), on ME1 as on the sequels: the same, so no change.
   - **LE1 at the same pose** (the shared RenderLevers and ExeCheck changed): the same replay and save, before (main's
     files) and after: the eye images identical to the pixel in both snapshot pairs, the second pair identical in every
     layer and `hostView` (the first pair's HUD 0.19% apart: what animates; sessions `2026-11-17_08-55-01-fxr-le1-before`,
     `08-57-41-fxr-le1-after`).
