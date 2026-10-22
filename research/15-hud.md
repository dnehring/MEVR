# 15: The HUD and menus in VR (M5): options and facts (2026-10-08)

**Problem.** In head-tracked mode the Scaleform HUD is baked into the game image. At our 108°
render FOV its corners (health, ammo, radar) land in peripheral vision: hard to read, and they
move with the head. In menus and conversations we already switch to the virtual screen
(`=`, on by default), so this is about **gameplay HUD**.

## Facts found offline (LE1 SDK, 2026-10-08) [verify in game]

- **The HUD handler.** BioWare's own Scaleform wrapper drives the HUD:
  - `UBioSFHandler_PCHUD` (SFXGame) derives from `UBioSFHandler_HUD` and then `UBioSFHandler`.
  - The handler's panel is at `UBioSFHandler::oPanel` (`+0x80`, a `UBioSFPanel*`).
- **Changing the HUD's layout.** `UBioSFPanel` (Engine) has `SetVariableFloat(FString sPath, float)`,
  `GetVariableFloat`, `SetVariableBool` and `InvokeMethod`. Through them the HUD's `_root` (or named
  clips) can be scaled and moved: `_root._xscale`, `_yscale`, `_x`, `_y`.
  - Halcyon's ME1 mod does exactly this to pull its HUD inward (research/13).
  - Call these through ProcessEvent with their parms structs, never the SDK wrappers.
- **Rendering a movie into a texture (H7).** Generic UE3 Scaleform, `UGFxMovie` (GFxUI), has
  `RenderTexture` (`UTextureRenderTarget2D*`, `+0x160`) and `RenderTextureMode`
  (`RTM_Opaque`/`RTM_Alpha`/`RTM_AlphaComposite`, `+0x1A6`).
  - It's the hook for drawing a movie into its own texture with alpha, which could then be its own
    OpenXR quad layer.
  - Open question: ME1's `UBioSFPanel` holds a `MovieRef` (`UObject*`, `+0x150`). Is that a
    `UGFxMovie` instance? If so, its `RenderTexture` would be the clean route to a separate HUD
    layer.

## Options, in order of effort

1. **Scale and centre the HUD (Halcyon's route).**
   - Each time the HUD loads, set `_root._xscale` and `_yscale` (for example 70%) and recentre
     `_x`/`_y`.
   - The HUD then sits in the central field of view, still baked into the image (head-locked).
   - Cheap, and needs headset tuning: the stage size and good values.
2. **Render the HUD into a texture and show it as a quad layer.**
   - If `MovieRef` is a `UGFxMovie`, point its `RenderTexture` at a render target we own (alpha
     mode).
   - The game then stops drawing it into the backbuffer, and we submit it as an OpenXR quad.
   - The quad can be world-locked or lazy-follow, at a comfortable distance, sharp, and in both
     eyes for stereo.
   - Needs the render target created in game (StaticConstructObject or an existing one), and a
     way to read it on the render thread (its RHI texture).
3. **Separate UI draws at the D3D level** (Halcyon's stereo mirroring, or ME2VR's "final minus
   world" difference):
   - inline hooks on the draw functions (the engine caches D3D function pointers);
   - UI draws recognised by render target format and blend state.

   This is the most work; it's the fallback if 1 and 2 aren't enough.

## Plan

- When the headset is back, check option 1 in game: log `GetVariableFloat("_root._xscale")` and
  the stage size, then add a HUD-scale hotkey.
- Probe the class of `UBioSFPanel::MovieRef` with a one-line log in the skeleton probe's style.
  If it's a `UGFxMovie`, option 2 becomes the M5 plan.
- Stereo stage 2 (docs/stereo-stage2-design.md) needs the HUD in both eyes. Option 2 solves that
  too.

## Built: option 3 on Halcyon's renderer (2026-10-08)

With Halcyon's renderer in place, option 3 turned out to be the least work, because its draw hooks
already single out the UI draws. Headset feedback that drove it: the HUD rode the head, and the
screen-centre crosshair didn't match the controller-held gun.

### Facts from Halcyon's log (2026-10-08 sessions) [verify where marked]

- **SDR backbuffer (format 28).** Scaleform's draws pass Halcyon's `IsUiDrawNow`: an 8-bit
  scene-sized target, full viewport, blended `SRC_ALPHA`/`INV_SRC_ALPHA`. They are `[UIMIR]
  mirrored` with pixel shaders `0F41EA74`, `FB5CF03B`, `9D645E87`, `888D513F` and `85C7403B`.
- **Other full-viewport draws into that target aren't UI.** These are not "over" blended:
  - multiply (`DEST_COLOR`/`ZERO`): `F307EA85`, `1E341BD3`, `11B5823A`, `761A1BD7`, `D3AA513E`;
  - additive (`ONE`/`ONE`): `F59517EC`, `63C28BFA`.
- **FP16 backbuffer (format 10, Windows HDR).** No UI draw was ever mirrored in the 14:19 HDR session,
  though the HUD was up. So the UI goes to a float target, presumably the backbuffer itself
  **[verify]**, Halcyon's left-eye mirror never runs there, and the HUD showed in the right eye only.
- **The crosshair is its own movie.** `BioSFHandler_Reticule` (`[PANELDISC]` #272) is separate from
  `BioSFHandler_PCHUD`. Its panel sits at the same `oPanel` `+0x80`.
- **Halcyon's PCHUD defaults shrink the HUD movie** to 35% × 30-45% and centre it, for head-locked
  viewing. Per-element overrides move the radar, squad, team and equipment groups on top of that.

### What's built

The code is `src/plugin/HalcyonOverlays.cpp`, with seams in `mevr_bridge.h`, `d3d_capture.cpp`,
`xr_session.cpp` and `pchud.cpp`.

- **Redirect.** While armed, a UI draw goes into a panel texture instead of the frame. A UI draw here
  means: after the pass-0 capture of this present, immediate context, full viewport, "over" blended
  (`DestBlend = INV_SRC_ALPHA`).
  - The target must be the swapchain backbuffer (by identity) or, while the backbuffer is 8-bit,
    an 8-bit scene-sized texture. It is never the FP16 scene colour.
  - The draw keeps its own state and the game's depth-stencil, which Scaleform masks with.
  - Its blend state gains `ONE`/`INV_SRC_ALPHA` for alpha, so the texture holds premultiplied
    colour plus coverage.
  - Each distinct pixel shader is logged once: `[MEVR-HUD] to the HUD panel` or `left in the frame`.
- **Panel.** Each frame the texture is box-filtered (factor 2-4) into an sRGB swapchain.
  - 8-bit: un-premultiply, decode, re-premultiply.
  - FP16: scale paper white to 1.0 and clip, keeping the hue.
  - It is submitted as a quad in Halcyon's app space: 1.5 m ahead, 1.3 m wide, 0.25 m below eye
    height, tilted to face the eye.
  - Recenter puts it straight ahead. With the head turned past 70° for a second, it eases round
    (50° until 2026-10-21; see "World-locked flat panels" below).
  - Placement math is in `core/vr/OverlayPlacement` (unit-tested).
- **Scope.** Stereo 2 only (the pass-0 boundary is the "after the world" signal), in gameplay modes
  0-4. VR conversations and cutscenes keep Halcyon's head-locked presentation.
- **PCHUD layout.** While the panel shows the HUD, the PCHUD movie keeps its authored layout
  (scale 1, offset 0, element overrides off), so the panel's size sets the HUD's size.
- **Reticle.** A ring quad 10 m along `core/vr/AimRay`, or the right controller's aim pose until
  the mod publishes the ray. It keeps a constant 1.5° and shows on foot while the controller aims.
  Meanwhile the crosshair movie gets `_root._alpha = 0` (restored to 100 afterwards).
- **Toggles.** `MELEVR.ini` `hudPanel*` and `handReticle*`, and the Insert menu, HUD tab.

## World-locked flat panels (2026-10-21)

**Request (user, 2026-10-21):** the HUD and flat panels should stay placed in space, not move with
the head. The intro's "view box" rode the head.

**Upstream behaviour (src/halcyon/le1/xr_session.cpp, unchanged since the import):**
- the flat screen (`forceFlatCine`: menus, the galaxy map, flat conversations, cutscenes and
  movies) is a quad in `g_viewSpace` (VIEW) at z = -1.4 m, width from `cfg.*MonoQuadWidth`;
- its Insert menu is a VIEW quad at `menuDistanceM` with `menuOffsetX/YM`, which our `[MENUVIS]`
  change pulls in front of the nearest other quad.

Both are head-locked: a cinema screen glued to the face, with no parallax when leaning. The
vrframework guide's HUD chapter (`reference/vrframework/guides/11-hud-ui-and-menus-in-vr.md`,
sections 2 and 4) says fullscreen menus want a stable panel that doesn't swim as you look
around. It rates a world-space quad as stable and physical, but possibly too far away to read,
which is why Halcyon's distances and widths are kept.

**Built:**
- `MevrBridge::PlacePanel` (seam, `mevr_bridge.h`; `src/plugin/WorldPanels.cpp`). Halcyon still
  computes its head-locked pose, then asks the seam for the space and pose to submit.
  - Off: VIEW and the same pose (upstream).
  - On: Halcyon's app space. The head-locked offset is applied in a level frame (the head's yaw
    only) at the eye centre when the panel appeared, so its distance, offsets and size stay
    Halcyon's settings.
- **Placement rules** (`core/vr/OverlayPlacement` `WorldPanel`, unit-tested in
  `TestWorldPanel`). The panel is placed afresh in front of the head:
  - when it appears (not shown for more than 5 frames; a blink of a frame or two doesn't count);
  - when the flat screen's kind changes (game menu, galaxy map, conversation, cutscene/movie);
  - on recenter (Halcyon re-origins the app space: `OnRecentered`, and a new app space handle);
  - once the head is found, if it had to be placed without one.
  - Re-placements that would move it less than 5° and 5 cm keep it exactly where it is.
  - Otherwise it stays put while the player looks around or leans (6DOF parallax). With the head
    turned past `panelFollowDeg` (70°) for a second, it eases round in front of the head, eye
    position included. No other follow, cutscenes included.
- **HUD panel:** the same 70° rule (`hudPanelFollowDeg` default 50 → 70). It stays anchored at the
  recenter origin (body-locked), as before.
- **Laser pointer:** `core/vr/MenuPointer` intersects a quad given as a pose in the tracking space.
  A VIEW quad is composed with the head first (`InTracking`); an app-space quad is used as is,
  since the controllers are in the app space too.
- **Test loop:** the menu quad is named by its submitted position. A world-locked quad is in the
  eyes' space and is placed as is; a head-locked one is placed from the rendered head.
- **Settings:** `MELEVR.ini` `worldLockedPanels` (default 1) and `panelFollowDeg` (default 70),
  `[MEVR]` fields in Halcyon's config next to `hudPanel*`. They're in the HUD tab of its menu.
  - Why Halcyon's config rather than our `LE1MEVR.ini`: the setting changes a Halcyon behaviour,
    the HUD-panel settings already live there, and its menu saves and resets them with no extra
    plumbing.

**Risks [verify in headset]:**
- **Re-placement at kind boundaries.** A cutscene running into its conversation is a kind change,
  so it re-places in front of the head (wanted by the request). If `inConversationMode` and the
  cutscene state flicker, the placement log shows a run of lines.
- **SteamVR quad order.** The `[MENUVIS]` pull-in keeps the menu nearer than the flat screen only
  when both were placed from about the same head pose.
- **No head when a panel appears:** it is placed at the app-space origin (where recenter puts the
  head), facing -Z, until the head is located.

### Known limits and risks

- A full-screen "over" blended effect drawn after the world, such as a fade or a damage tint, would
  land on the panel instead of the world. The `[MEVR-HUD]` shader list shows any such case.
- The desktop mirror loses the HUD while the panel is on.
- The left-eye UI mirror (`[SFR-UI]`) no longer runs for redirected draws. Non-"over" UI draws
  still only reach the right eye, as before. (Since 2026-10-22 Scaleform's own non-"over" modes go
  to the panel too: next section.)

## Scaleform's blend modes and the doubled wheel (2026-10-22)

**The bug (headset, 2026-10-21):** an outline of the weapon wheel's selector stayed locked to the
view while the wheel itself showed on the HUD panel.

**Cause [inference from the logs, verify with a snapshot]:** the redirect took only "over" blended
draws. Scaleform draws its other blend modes (Flash's multiply, screen, add...) with other blend
states, and those stayed in the frame: drawn once after both stereo passes, so in the right eye only,
head-locked. The logs show two on the backbuffer during gameplay: `761A1BD7` and `11B5823A`,
`src=9 dst=1` (DEST_COLOR / ZERO: multiply). What tells them apart from the engine's own full-screen
passes is the alpha factor: Scaleform's states keep the target's alpha (`dstA = 7`, DEST_ALPHA, also on
its "over" draws), the engine's post passes on the 8-bit scene target use ONE or ZERO.

**Built (`d3d_capture.cpp` [MEVR] `MevrHudPanelDraw`, `HalcyonOverlays.cpp`):**
- A blended draw with `DestBlendAlpha = DEST_ALPHA` that isn't "over" goes to the panel too (logged
  once per shader: `to the HUD panel (a Scaleform blend mode, as a tint)`).
- The panel then needs to say what the UI does to whatever is behind it, which a single alpha can't
  for a multiply. The composite is `world * K + B`: B is the panel texture (premultiplied colour, as
  before), K a new "tint" texture of the same size, white when nothing touches the world. An "over"
  draw scales K by (1 - alpha) through the panel's alpha channel as before; a non-"over" draw is drawn
  twice: into B with the game's own blend (B's alpha left alone), and into K with `dest *= f(source)`,
  f from the game's pair: multiply (DEST_COLOR/ZERO) -> SRC_COLOR, screen (INV_DEST_COLOR/ONE) ->
  INV_SRC_COLOR, a source-only factor as is; add (ONE) and MIN/MAX leave K. The second draw uses a
  copy of the game's depth-stencil state without depth or stencil writes, so a mask isn't counted
  twice.
- The blit's alpha is `1 - (1 - coverage) * grey(K)`. Grey: a coloured multiply darkens the world
  behind the panel without its hue (a quad layer has one alpha).
- **Risk:** an engine draw with Scaleform's alpha factor that isn't UI would land on the panel. The
  shader list (`[MEVR-HUD] Scaleform blend mode src=.. dst=..` once per blend state) shows it.

## The wheels on their own panel (2026-10-22)

The user wanted the game's own weapon wheel at the hand (docs/squad-weapons-design.md "The game's own
wheel", research/19 section 10). The wheel is part of the HUD movie, so it can't be told apart by draw;
it's told apart by place.

- **Where the wheel is in the frame:** the HUD movie is a 1280x720 stage (`GUI_SF_HUD.ME_HUD` /
  `PC_ME_HUD` in `Startup_INT.pcc`); its wheel, `squadMC`, is centred on it, (298, 129)-(982, 603)
  stage pixels in the console movie, (357, 77)-(914, 633) in the PC one. How the stage maps onto the
  near-square VR frame depends on the movie's scale mode, which wasn't found: the movie sets
  `Stage.align = "TL"` and snaps its edge groups to `Stage.visibleRect`, which fits GFx's "show all".
  So `core/vr/WheelCrop` starts from that guess (`DefaultCrop`: the union of both clips plus 16 px,
  the stage fitted to the frame's width at the top), and **measures** it once per session: a coarse
  grid (48x50 cells, the most opaque pixel of each, every 4th pixel) of the panel texture now and then
  while on foot, and once 0.6 s after a wheel opens (past its grow animation); the largest 4-connected
  group of cells that became at least 40/255 more opaque is the wheel (`MeasureCrop`, padded by a cell).
  Logged as `[MEVR-WHEEL] measured: the wheel is at (...)` with the guess beside it. `[Squad] wheelCrop
  = x, y, w, h` overrides both.
- **The panel:** while the game's mode is 3 (weapon wheel) or 2 (power wheel), that part of the panel
  texture is blitted into a swapchain of its own and submitted as a quad (on top of ours), placed when
  the wheel opens like the squad menu (`squad_menu::PlaceAtHand`: 0.27 m above the hand, facing the
  eyes, level, 0.40 m wide; the mod's `[Squad] panelDistanceM`, `panelWidthM`): the weapon wheel at the
  left hand, the power wheel at the right (`weaponWheelAt`, `powerWheelAt`: left, right, front, hud).
  Fixed in the room while it's open; placed again on recenter.
- **The HUD panel** shows the rest: its blit leaves that rectangle transparent while the wheel panel
  shows, so the wheel is seen once.
- **Test loop:** the quad's swapchain is named "wheel": `<stamp>-wheel.png`, and it's in the eye
  composites.
- **Settings across the boundary:** the mod publishes them each tick (`HostServices::setWheelPanel`,
  v12, `squad_menu::wheel::Panel`, size-checked); without them the host uses the same defaults.
- **[verify]:** the measured crop against the snapshot; the wheel's squad portraits inside it; nothing
  of the wheel left in the eyes.
