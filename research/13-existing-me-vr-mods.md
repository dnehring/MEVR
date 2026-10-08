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

<!-- Deeper code-level findings are appended below by the follow-up read. -->
