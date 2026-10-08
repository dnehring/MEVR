# 13: Existing Mass Effect LE VR mods (2026-10-07)

The user found a closed ME2 VR mod in their downloads. Following it up turned up open-source
prior art for all three games. That corrects [01](01-feasibility.md), which said no LE VR mod
existed.

## What exists

| Project | Games | License / source | Stereo | Input | Loader |
|---------|-------|------------------|--------|-------|--------|
| **Halcyon's MELE VR** ([MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR), [2](https://github.com/jrush64/MassEffect2-VR), [3](https://github.com/jrush64/MassEffect3-VR)) | LE1, LE2, LE3 (three separate repos, one per game) | **GPL-3.0, full source.** "Copyright (C) 2026 Halcyon", pushed 2026-09-20/30 | **Stereo** (two full primary renders per present), AER, DIBR, Mono | Gamepad/keyboard. Head aim (weapon and Mako turret follow the head), lean, HMD-relative movement | `dxgi.dll` proxy, MinHook, Dear ImGui in-headset menu |
| **ME2VR v0.54.0** (package named v0.5.6; the user's `F:\downloads\ME2VR-v0.5.6-Steam`) | LE2 only | **Closed.** Binaries only, unsigned, no license, no author named. No public page found | "Geometric alternate-eye" (AER) by default; an experimental same-frame "SFR replay" with an isolated `FSceneViewState` | **Quest Touch motion controls**: arms with IK, weapon in hand, two-hand grip, impact reticle, chords for squad/powers | `dxgi.dll` proxy that loads `ME2VR.dll` |
| Nexus "Mass Effect Legendary Edition Trilogy VR Mod" ([3397](https://www.nexusmods.com/masseffectlegendaryedition/mods/3397)) | All three | Unknown (page 403s to the fetcher) | Stereo/AER/DIBR per press coverage | No motion controls per [Generation Amiga](https://www.generationamiga.com/2026/08/23/mass-effect-vr-mod-brings-the-entire-legendary-edition-to-pc-vr/) | n/a |

Probably one author: the Nexus mod, the Generation Amiga article (Patreon "dhalcyon"), the Hostile
VR teaser ("modder Marauder") and the GitHub repos all list the same Stereo/AER/DIBR/Mono modes.
**[verify]** whether these are all Halcyon's.

ME2VR's relationship to Halcyon's code is unknown. Its strings share vocabulary (AER, SFR) but no
names, paths or messages. Don't assume either way.

### ME2VR package notes (from its README, ini and log strings only)

We didn't disassemble, decompile or run any of it.

- The installer checks for `ME2VRInjector.exe`, which isn't in the package, so as shipped
  `INSTALL_ME2VR.bat` stops at its file check.
- The installer forces `GamerSettings.ini` to 2560×1440 windowed and turns off AA, motion blur,
  DoF, film grain, bloom and dynamic resolution. It depends on MassFPS (LE2).
- Concepts visible in its log strings:
  - a `CalcSceneView` observation hook at LE2 RVA `0x6CEB70`;
  - square game resolution enforced through a window-resize hook;
  - "camera-tagged" AER pairs through a tag FIFO (the same pairing problem as our M3 lockstep);
  - SFR replay through `FlushRenderingCommands` plus an isolated `FSceneViewState`, falling back
    to AER;
  - a post-`InvokeFunction` `GetAdjustedAim` override for controller aim (our H8);
  - a post-animation pose-update hook that writes arm IK results back into the skeleton (our H3
    alternative);
  - HUD separation: the world is captured before Scaleform draws, and the HUD is extracted as
    final minus world;
  - an OpenXR action-to-XInput bridge;
  - a Steam Overlay Present detour bypass.

## Facts from Halcyon's ME1 write-up

Source: `reference/MassEffect1-VR/docs/STEREO_FIX_DOCUMENTATION.md`. All are **[verify]** against
our own build; our exe is the same 2.0.0.48602.

1. **UE3-LE denies the secondary view of a multi-view family its per-frame lighting and post
   work.** Deferred lighting, bloom, light shafts and DoF run for the first view only. Their SBS
   attempt gave a right eye missing bloom and pure-black conversation characters, and fixes on
   the composite were falsified. **This refutes our H2** (split-screen as the stereo path), at
   least in its two-views-per-family form.
2. **Same-frame stereo by rendering twice.** After the game's own `FViewportClient::Draw` (LE1
   `exe+0x4C71A0`), call it once more, with the eye offset applied in the `CalcSceneView` path
   (−halfEye on pass 0, +halfEye on pass 1). The game thread only enqueues; D3D work stays on the
   render thread. Each eye is a full primary render, so lighting and post are correct.
3. **Capturing pass 0** means finding the boundary between the two renders in the D3D command
   stream:
   - gameplay: the first scene-sized depth clear after the first uber-composite draw (PS hash
     `0x9AA7F54A`);
   - conversations use another path.
   Pass 1 is the backbuffer at Present.
4. **Scaleform draws the HUD once per present, after both passes.** Mirror each UI draw into the
   held pass-0 texture using the game's own DSV, because Scaleform masks with stencil.
5. **Square render resolution** for full-frame eyes. Wide buffers squash screen-space UI and blur
   when upscaled.
6. **Frame pacing:** two renders per present, free-running against the headset refresh, beat in
   phase and flicker. The fix locks presents to a whole number of display periods (for example 2
   at 120 Hz with 60 fps), adaptively stepping to 3 under load. A *regular* cadence reprojects
   smoothly; an irregular 2/3 mix judders.
7. **Planar floor reflections** (`SceneCaptureReflectComponent`) are throttled by a stock UE3
   FrameRate (60). Captures skip frames, so reflections lag the view: about 30 Hz judder on head
   turns in *every* mode. Fix: call `SetFrameRate(1000)` on each reflect component.
8. **Conversations and cutscenes** sit in a fixed cinema window (one constant 16:9 FOV), because
   ME1 changes lens on every cut. DoF is forced off.
9. **Misc:**
   - `FSceneView+0x320` holds the camera translation inverse (−position).
   - The 96-byte camera constant buffer is a view-projection matrix, a pad row, then the camera
     world position.
   - EGameModes byte: 0–4 gameplay, 5 conversation, 6 cutscene, 7 menu, 8 movie, 9 galaxy map.
10. **"The game records world rendering on a DEFERRED context."** This **conflicts with our H16**
    (device flags `0x1` = SINGLETHREADED; our `CreateDeferredContext` failed). Resolve before
    hooking constant buffers.
11. **HDR must be off** in the game's options (it shows as a stuck blue or doubled image).

## Code-level findings (Halcyon ME1 source, read 2026-10-07)

These were read from `reference/MassEffect1-VR/src/` and are described in our own words. File
references are to that tree. Only dllmain, dxgi_proxy, logger, d3d_capture, xr_session,
render_hook, vr_menu, vr_config, head_aim and pchud are built; `game_camera.cpp` is retired.

### Verified against our exe (static, 2026-10-07)

| What | LE1 RVA | Our check |
|------|---------|-----------|
| `ULocalPlayer::CalcSceneView` | `0x4C5300` | **Function start** in the exe's `.pdata` unwind table (ends `0x4C67B2`) |
| `FViewportClient::Draw` | `0x4C71A0` | **Function start** (ends `0x4C9434`) |
| Gameplay CalcSceneView return address | `0x4C7A4E` | Lies inside Draw. The call bytes can't be checked on disk |
| `AllocateViewState` (Halcyon: "likely") | `0x742B70` | Function start, 62 bytes long |
| ProcessEvent, GameEngineTick (LExSDKv2) | `0x12DD20`, `0x3CA090` | Function starts (consistent with what we already use) |

**LE2 and LE3** (from Halcyon's ME2/ME3 repos), also all confirmed as function starts:

| Function | LE2 | LE3 |
|----------|-----|-----|
| CalcSceneView | `0x6CEB70` | `0x6CFFD0` (matches ConsoleExtension's) |
| FViewportClient::Draw | `0x6D0A00` | `0x6D1E30` |
| AllocateViewState | `0x329750` | `0x3440F0` |

The gameplay call-site return addresses for LE2 and LE3 aren't known yet. Until they're
pinned, our scene-view hook counts any call from inside `FViewportClient::Draw` as the player
view, and logs the caller's RVA (`scene views: N primary (from exe+0x...)`) so the first run on
each game pins it. All three games' plugins build (2026-10-08).

**`.text` is encrypted on disk.** The function-start bytes are noise, and an `.ooa` section (EA
DRM) decrypts the code at runtime. So signature and byte checks must run in-process after
startup. `.pdata` is plain and gives reliable function boundaries. Tool:
`tools/verify_rvas.py`.

### How the head pose is applied (render-only, native)

- **One hook: `CalcSceneView` (`0x4C5300`).**
  - It calls the original, then edits the returned `FSceneView` (`render_hook.cpp:2360-2551`).
  - The primary player view is recognised by its return address `0x4C7A4E`.
  - Orthographic views are skipped.
- **FSceneView offsets** (same in ME2; LE3 matches ConsoleExtension, see [09](09-me3tweaks-code-survey.md)):

  | Offset | Field |
  |--------|-------|
  | `0x8` | State |
  | `0x90` | ViewMatrix |
  | `0xD0` | ProjectionMatrix |
  | `0x190` | TranslatedViewMatrix |
  | `0x1D0` | TranslatedViewProjection |
  | `0x210` | its inverse |
  | `0x250` | PreViewTranslation |
  | `0x260` | ViewProjection |
  | `0x2A0` | inverse Projection |
  | `0x2E0` | inverse ViewProjection |
  | `0x320` | ViewOrigin (sign **[verify]**; their doc and code disagree) |

  Matrices are row-major with row vectors.
- **Order of edits:**
  1. eye/lean translation, applied to ViewOrigin and PreViewTranslation on the *body* basis;
  2. FOV, by rewriting projection [0] and [5];
  3. head yaw then pitch, post-multiplied into the View and TranslatedView matrices, then
     rebuilding the view-projection products;
  4. recompute the three inverse slots.

  Stale inverses after a pitch change put a screen-locked dark panel in height fog and light
  shafts.
- **Roll is never rendered.** It's removed from the submitted pose, and the compositor shows head
  roll.
- **No GetPlayerViewPoint or UpdateCamera hooks.** With the weapon holstered, only the rendered
  view turns; the left stick is rotated by head yaw in an XInput hook.
- **With the weapon out ("head aim"),** head deltas are written into `ControlRotation`
  (`AActor.Rotation` `0x114`). Pitch is clamped to ±85°, and render-side look is zeroed so it
  isn't applied twice. They note that **driving ControlRotation makes the third-person camera
  arc around Shepard**.
- **FOV:** the larger per-eye half-angle times a fill factor goes into the projection. The angle
  is read back and declared as a symmetric FOV. They never write `FOVAngle`.

### Frame pacing

- **Loop.** One XR frame per game Present, all on the render thread:
  1. optional pacing hold;
  2. `xrWaitFrame`, `xrBeginFrame`;
  3. locate the head at **this frame's** `predictedDisplayTime` and publish it for the *next*
     CalcSceneView;
  4. copy the eyes;
  5. `xrEndFrame`.

  **The game thread is never blocked.** UE3's game thread runs about one frame ahead of the
  render thread.
- **Pose tag.** The layer is tagged with the pose from **2 presents back** (fractional blend, a
  3-entry ring, tuned in the headset). An "exact sequence" tag **shook in gameplay**, because the
  game thread runs ahead and made the tag intermittently too fresh. Late-latching was worse.
  When a stale image is resubmitted, its tag and FOV are frozen too.
- **Smoothing.** `headLookSmoothing` 0.4, a speed-adaptive nlerp. It feeds *both* the render and
  the tag, so they can't disagree.
- **Locking to display periods.** `StereoLockPeriods` / `PaceDisplayLockedAer` lock to N display
  periods when the natural interval is within 0.25 of an integer. An adaptive variant steps to
  N+1 under load. It's off by default in gameplay and on for cinematics.
- **60 fps cap = `BIOEngine.ini` SmoothFrameRate / MaxSmoothedFrameRate=60.**
  - Every attach rewrites it to `TRUE`/`120`; the game re-syncs the file, so a one-off edit
    doesn't stick.
  - It stays on because tick smoothing keeps cutscene cameras from juddering.
  - **This answers H14.**
- **Other stutter sources:**
  - A 60 Hz monitor caps Present without `DXGI_PRESENT_ALLOW_TEARING`. Not our case: we measured
    89.6 fps.
  - Reflection captures (`SceneCaptureReflectComponent.FrameRate`, default 30) are fixed with
    `SetFrameRate(1000)` through ProcessEvent, found via GObjects.
  - Look toggling on and off.

### D3D11 device

- **The "deferred context" claim is unsupported.** Their hooks never saw the game create one.
  ME2's doc found the real cause: the engine **caches D3D11 draw function pointers** at startup,
  so vtable patches miss draws; hook the draw functions inline. LE1 renders on the immediate
  context, consistent with our H16.
- **SINGLETHREADED vs SteamVR.** Since 2026-07-20 they strip the `0x1` flag (hooking
  `D3D11CreateDevice*`). SteamVR's client needs a keyed-mutex sync texture, which a
  SINGLETHREADED device refuses. Without it, SteamVR silently drops the projection layer while
  `xrEndFrame` succeeds. **We haven't hit this** (M2/M3 work on SteamVR 2.17.10 via Steam Link),
  but it's the first suspect if SteamVR ever shows only its void.
- **FP16 backbuffer = in-game HDR is on.** Their installer forces
  `GamerSettings.ini [HDR] DesiredRange=DynamicRange_SDR`.

### Submission and HUD

- **Swapchains.** One per eye at the recommended size. The format is the backbuffer's sRGB twin,
  so the copy is a plain `CopyResource`. No depth layer.
- **SteamVR rejects views declared wider than its frustum**, so they crop the declared FOV and
  sub-image to the runtime's per-eye frustum.
- **Flat content.** Menus, movies and the galaxy map go on a head-locked quad (about 2.35 m wide
  at 1.4 m).
- **Mono HUD.** Moved through Scaleform variables (`BioSFHandler_PCHUD`,
  `BioSFPanel.SetVariableFloat`). Subtitles are redrawn through `BioHUD.PostRender`.
- **Stereo HUD.** UI draws (R8G8B8A8 target at backbuffer width, full viewport, `INV_SRC_ALPHA`
  blend) are mirrored into the held left eye with the game's DSV. Shader IDs are FNV-1a hashes
  of pixel-shader bytecode.

### First person

- **Camera.** Write `USFXCameraMode.Offset` (`0xB4`) per camera mode, plus `Interpolate.To`
  (`0x168`) during blends. The native `bFirstPerson` bit makes the game ignore the offset.
- **Keeping the pawn visible.** `SetHidden(false)` plus `bIgnoreHiddenActorsMembership`. Editing
  `HiddenActors` crashed.
- **Hiding the head.** On BioPawn head parts (`0x7EC`–`0x80C`): `SetOwnerNoSee` plus wireframe.
  On the body: `HideBoneByName` for the eyes and tongue. MassFPS does the same ([11](11-massfps.md)).
- **Thread.** They make these calls from the Present thread. We'll use the game thread.

### Other LE1 offsets they use [verify]

| Type | Offsets |
|------|---------|
| UObject | index `0x08`, Outer `0x40`, Name `0x48`, Class `0x50` |
| AActor | Location `0x108`, Rotation `0x114` |
| Controller | Pawn `0x3DC`, PlayerCamera `0x6B0`, FOVAngle `0x758` |
| ACamera | CameraCache `0x45C` (FOV +`0x18`), CurrentMode `0x568`, flags `0x514`; bit 2 = bConstrainAspectRatio, which causes the cutscene letterbox |
| AWorldInfo | DeltaSeconds `0x730` |
| GameMode | `ABioPlayerController+0xA70` → `USFXGameModeManager+0xB8` gives the EGameModes byte: 0 gameplay, 1 vehicle, 5 conversation, 6 cinematic, 7 GUI (also in-world GUIs), 8 movie, 9 galaxy, 10 minigame |

Most of these are also in LExSDKv2's generated headers, so cross-check there first.

## What this means for our M3 vertical stutter

Ranked suspects:

1. **Our pitch goes through the game camera.**
   - We override pitch in `GetPlayerViewPoint` and the camera.
   - In third person, the camera boom then arcs and interpolates with pitch, so position and
     rotation fight each frame. That would explain *vertical* judder specifically.
   - Halcyon deliberately keeps head look render-only.
   - Fix: apply head rotation in `CalcSceneView` (render-only). That's also where M4's stereo
     eye offsets go.
2. **Irregular cadence.** 51–80 fps against 90 Hz. Our `GamerSettings.ini` has
   `UseVSync=True`, so every Present also waits on the desktop monitor's vblank: two clocks
   beating. First test: VSync off, HDR off (zero code). Then lock to whole display periods if
   needed.
3. **Reflection captures at 30 fps.** Only on reflective floors.
4. **Tag age.** Our lockstep makes the tag exact by blocking the game thread. Halcyon found an
   exact tag shook *because* the game thread runs ahead. Blocking it, as we do, avoids that,
   but costs throughput.

