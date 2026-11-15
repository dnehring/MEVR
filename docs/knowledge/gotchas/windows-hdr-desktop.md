---
type: Gotcha
title: With Windows HDR on, the Legendary Edition renders a float scRGB frame that Halcyon's renderers were never built for
description: An HDR desktop makes the game's DXGI question answer HDR10, and LE1 (with an HDR DesiredRange) and ME2 (in gameplay, whatever DesiredRange says) then render an R16G16B16A16_FLOAT frame; Halcyon's eye copies, stereo capture and UI treatment assume 8 bits. Both games now answer SDR by default ([Colour] hdrDisplay = -1) and stay at their 8-bit frame.
tags: [hdr, halcyon, renderer, le1, le2, d3d11, dxgi, players]
status: stable
timestamp: 2026-11-14T23:30:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-14T23:30:00Z }
bitten: [2026-10-08, 2026-11-14]
---

# Symptom

| wording | where, when |
|---|---|
| "a blue screen in the headset" with Windows HDR on (LE1) | before 9f959d7, 2026-10-08 |
| "the eye snapshot is black in both eyes"; "in gameplay the eye images are the main menu's backdrop" (ME2) | lanes A and B, 2026-11-14 ([me2-float-swap-chain](me2-float-swap-chain.md)) |
| the same picture in both eyes; `[SFRDIAG] ... totalCaps=0` (ME2's SFR left eye never captured) | lane B2, 2026-11-14 |
| the UI missing from the HUD panel, the HUD full screen in both eyes; `[UIGATE] ... ui=0 ... fmt=10` | lane B2, 2026-11-14 |
| "people with HDR monitors reported issues when launching me1" | the owner, 2026-11-14 |
| "I needed to restart my monitor for it to play" | the owner, 2026-11-14: **this happened before our mods**, so it isn't ours; not investigated (the owner's decision) |

# Cause

The Legendary Edition decides whether the display is HDR from `IDXGIOutput6::GetDesc1`'s `ColorSpace` (HDR10, 12, means
"HDR capable"; LE1 `0x1409a0050`, research/27 7c). With Windows HDR on for the monitor the game window sits on, the answer is
HDR10, and:

- **LE1** makes its `R16G16B16A16_FLOAT` scRGB backbuffer when `[HDR] DesiredRange` (GamerSettings.ini over BIOEngine.ini) is
  an HDR range (`DynamicRange_HDR10_Float`); with `DynamicRange_SDR` (what `tools/vr-game-settings.ps1`, the deploy and MEVR
  Setup write) it stays at format 28.
- **ME2** makes the float swap chain in gameplay even with `DynamicRange_SDR` (its title stays at format 28).

That frame is not a scene-referred HDR render: it is the game's SDR frame re-expanded by one full-screen pass
(`FReconstructHDRPixelShader`, research/le2-hdr.md section 1). Halcyon's upstream renderers were written for an 8-bit
backbuffer (upstream's installer tells players to turn Windows HDR off): their `CopyResource` into 8-bit eye images is
dropped by D3D11 between format families (LE1 before 9f959d7 showed a blue image, ME2 black or stale eyes), ME2's SFR
left-eye capture counts draws into the swap chain (the float frame has one), and ME2's UI treatment wants a format-28 target.

# Check

- Halcyon LE1's log (`%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt`): `[HDRBLIT] backbuffer format 0 -> 28` (8-bit) or `-> 10`
  (float); `[HDRCHK]` names GamerSettings.ini's `DesiredRange` at the swapchain's creation; `[SCREQ] ... fmt=`.
- The host log (`Logs\LE1MEVR.log`, `Logs\LE2MEVR.log`): `hdr display: the display reports colour space 12 (HDR10) ...;
  answered SDR: the game keeps its 8-bit frame ([Colour] hdrDisplay = -1)`. Colour space 0 means Windows HDR is off.
- ME2: `the game's swap chain is ... format 28` (lane B2's line), `fmt=` in `MELE2VR_Log.txt`.
- A snapshot (`tools/snapshot.ps1`): `-left.png` and `-right.png` differ, neither is blue or black, the HUD on its panel.

# Fix in place

- **Both games answer SDR by default** (`[Colour] hdrDisplay = -1`: LE2 since 5788d641, LE1 since 2026-11-14,
  `MevrBridge::HdrDisplayDesc` in `plugin/HalcyonBridge.cpp`; an LE1 file's old `hdrDisplay = 0` moves to -1 once,
  `hdrDisplayMoved`): the game keeps its 8-bit frame whatever Windows' switch and `DesiredRange` say. `0` passes the display's
  answer through, `1` answers HDR10 on an SDR display (research/27 7c's float path).
- LE1's eye blit handles a float source anyway (9f959d7: the blit source per format, sRGB eyes, the paper-white clip or
  `[Colour] tonemap`); ME2's `plugin/EyeCopy` draws a float source into the eyes instead of dropping it.
- `DesiredRange = DynamicRange_SDR` from `tools/vr-game-settings.ps1` and MEVR Setup (Uninstall puts the player's value back).
- Never change Windows' HDR setting to get around it (this machine has it on: every lane's runs are an HDR desktop's).
- The launch checks on this HDR desktop: [research/le2-hdr.md](../../../research/le2-hdr.md) "HDR desktops: launch checks".

# Citations

- [research/le2-hdr.md](../../../research/le2-hdr.md) (the reconstruct pass; "HDR desktops: launch checks").
- [research/27-colour.md](../../../research/27-colour.md) sections 7b-7d (LE1's float path, `hdrDisplay`).
- [me2-float-swap-chain](me2-float-swap-chain.md) (ME2's black eyes, EyeCopy).
- `notes/upstream-halcyon/README.md` item 2 (the blue image, 9f959d7).
- `src/plugin/HalcyonBridge.cpp` (`HdrDisplayDesc`), `src/plugin/HalcyonLe2Bridge.cpp` (`GetDesc1Hook`),
  `src/halcyon/le1/d3d_capture.cpp` (`OutputGetDesc1Hook`), `src/install/GameSettings.cpp` (`DesiredRange`).
