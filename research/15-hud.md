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
