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
  - The target must be the swapchain backbuffer (by identity) or an 8-bit scene-sized texture,
    never the FP16 scene colour.
  - The draw keeps its own state and the game's depth-stencil, which Scaleform masks with.
  - Its blend state gains `ONE`/`INV_SRC_ALPHA` for alpha, so the texture holds premultiplied
    colour plus coverage.
  - Each distinct pixel shader is logged once: `[MEVR-HUD] to the HUD panel` or `left in the frame`.
- **Panel.** Each frame the texture is box-filtered (factor 2-4) into an sRGB swapchain.
  - 8-bit: un-premultiply, decode, re-premultiply.
  - FP16: scale paper white to 1.0 and clip, keeping the hue.
  - It is submitted as a quad in Halcyon's app space: 1.5 m ahead, 1.3 m wide, 0.25 m below eye
    height, tilted to face the eye.
  - Recenter puts it straight ahead. With the head turned past 50° for a second, it eases round.
  - Placement math is in `core/vr/OverlayPlacement` (unit-tested).
- **Scope.** Stereo 2 only (the pass-0 boundary is the "after the world" signal), in gameplay modes
  0-4. VR conversations and cutscenes keep Halcyon's head-locked presentation.
- **PCHUD layout.** While the panel shows the HUD, the PCHUD movie keeps its authored layout
  (scale 1, offset 0, element overrides off), so the panel's size sets the HUD's size.
- **Reticle.** A ring quad 10 m along `core/vr/AimRay`, or the right controller's aim pose until
  the mod publishes the ray. It keeps a constant 1.5° and shows on foot while the controller aims.
  Meanwhile the crosshair movie gets `_root._alpha = 0` (restored to 100 afterwards).
- **Toggles.** `MELEVR.ini` `hudPanel*` and `handReticle*`, and the Insert menu, HUD tab.

### Known limits and risks

- A full-screen "over" blended effect drawn after the world, such as a fade or a damage tint, would
  land on the panel instead of the world. The `[MEVR-HUD]` shader list shows any such case.
- The desktop mirror loses the HUD while the panel is on.
- The left-eye UI mirror (`[SFR-UI]`) no longer runs for redirected draws. Non-"over" UI draws
  still only reach the right eye, as before.
