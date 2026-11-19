<!-- SPDX-License-Identifier: MIT -->
# ME3's gameplay HUD on the body-locked HUD panel (lane HU, 2026-11-16)

ME1 and ME2 put the game's HUD on our body-locked HUD panel (`plugin/HudPanel`, research/15, research/le2-renderer-integration.md
"Step 4"). ME3's renderer already sent each SFR UI draw through the shared seam (`src/halcyon/le3/d3d_capture.cpp` `EmitUiDraw`
calls `MevrSeams::HudPanelDraw`, which is `hud_panel::Redirect`), but its renderer said `hudPanelGameplay = false` and
`Hud().hudPanel = false`, so the panel was armed only for a hand-set interaction crop and the gameplay HUD stayed in the eye
images. That kept ME3's stereo plan in mode 1 (the symmetric window), ME1's rule while UI is in the eyes (research/30).

## What changed

- `plugin/RendererLe3.cpp`: `Caps::hudPanelGameplay = true`; `Hud()` returns ME1's defaults (`renderer::HudSettings{}`: the panel
  on, 1.5 m, 1.3 m wide, 0.25 m below the eyes, following the head past 70 degrees), as ME2's does.
- `plugin/SequelOverlays.cpp`: which game modes are gameplay for the panel (and for hiding the crosshair) is
  `engine::adapter::IsHudGameplayMode`: on foot, the vehicle, the power and weapon wheels, the squad command, and ME3's Atlas and
  injured Shepard. Until now it was ME2's raw numbers 0..4, which on ME3 are on foot, vehicle, Atlas, power wheel and weapon
  wheel (no squad command, 5, nor injured, 6). ME2's set is unchanged.
- Nothing in `d3d_capture.cpp`: the draw site existed. Halcyon ME3's per-element HUD moves (`hud_probe.cpp`'s baked `[HUD]`
  groups, ME2's PcHud counterpart) never run in our host (`[MEVR]` in its `Tick` and `SetEditorEnabled`), so the movie keeps its
  own layout on the panel and `SetAuthoredHudLayout` stays a no-op.

## In the game (fake headset, renderer preview, Save 31, the Normandy's War Room)

Sessions `20261116-215028-548-LE3-hu-me3a`, `20261116-215450-633-LE3-hu-me3b`, `20261116-215851-868-LE3-hu-me3c` (two runs); each
restored with 0 differences.

- `overlays: game UI goes to the body-locked HUD panel`, then `[MEVR-HUD] HUD panel texture 3072x1728 fmt=28`; the UI draws are
  "over" blended (`src=5 dst=6`, and `dstA=7` for the prompt's), none left to the renderer.
- `[MEVR-STEREO] stereo mode 2: life-size, each eye's own frustum` as soon as the panel is armed (`plugin/MetricStereo`:
  `uiInEyes = in.uiInEyes || !hud_panel::Armed()`); before it, at the load, mode 1. Snapshots' `"stereo"."mode": 2`.
- ME3's UI draws mostly come after the renderer's left-eye capture; a few per second come before it (`... N UI draws before the
  renderer's left-eye capture (replaced by the later batch)`): the panel takes both and clears at the first late draw, as ME2's.
  The War Terminal's prompt on the panel was drawn once (no doubled alpha).
- The eyes' world layers carry no UI (`-left-world.png` at the War Terminal: the console, its 3D selection ring, no prompt); the
  `hud` layer has it, and with the label at the object (research/40 section 6) the prompt's band leaves the panel for the `label`
  layer at the console.

## Not verified

- ME3's combat HUD on the panel: no ordinary combat fixture yet (research/le3-combat-fixtures.md). Its health, squad, ammo and
  power clips are not shown on the Normandy (`mevr guimouse hud`: `WeaponAmmo`, `WeaponClip` at the bottom left, the target
  clips at the top centre).
- ME3's crosshair: hidden since lane CH (below), **unverified in combat**.
- The wheels (power, weapon), the squad command and injured Shepard's mode on the panel; the headset.

## The crosshair hidden (lane CH, 2026-11-17)

Since 2333182f ME3's gameplay HUD goes to the body-locked panel, and with it ME3's own crosshair: it floated on the panel next to
our hand reticle, because `RendererLe3` left `screenReticleHide` false and `SetScreenReticleHidden` was a no-op. ME1's rule
(research/44, 2026-11-07): with the hand reticle on, VR gameplay hides the game's reticle movies, whatever the weapon or the aim
ray, and the native sniper view stays. ME2 has had it since M2 (research/le2-renderer-integration.md "Step 5").

### How ME3 draws its crosshair

From the installed `SFXGame.pcc` (read with `tools/conversation-scan --script`; the decompiled text stays private) and the LE3 SDK
names:

- The crosshair is **a movie of its own**, not a clip of the HUD movie (`BioPlayerController.GetHUDMovie`, `SFXSFHandler_HUD`)
  and not a `BioSFPanel`: `SFXGame.SFXSFHandler_Reticle`, an `SFXGUIMovieLegacyAdapter`. The GUI manager (`SFXGUIInteraction`,
  the controller's `GetSFXUIController`) opens it natively and keeps it under its `MovieTag_Reticle`; `SFXGUIInteraction.GetMovie
  (player, tag)` (native) returns it. No script names that tag.
- It draws the held weapon's reticle, `m_oCurrentWeaponReticle` (an `SFXGUI_WeaponReticleBase`, itself a `GFxValue`: the clip).
  Which class is the reticle's own script event `GetWeaponReticleClass`: the weapon's `SFXWeapon.GUIReticleClass`, or while
  `BioPlayerController.IsZoomed()` its `GUIZoomReticleClass`. The classes: `SFXGUI_CrosshairReticle` (the four-arm crosshair),
  `SFXGUI_ShotgunReticle`, `SFXGUI_HeavyWeaponReticle`, `SFXGUI_WeaponReticleSimple`, and for the sniper rifles' zoom
  `SFXGameContent.SFXGUI_StandardSniperZoomReticle` (the scope view). It also carries the hit markers (`AS_PlayTargetImpact`)
  and the target reticles `m_aReticles` (`SFXSF_ReticleCombatObject`, `SFXSF_ReticleCombatTarget`).
- The legacy adapter keeps ME1's and ME2's panel calls by name: `GetVariableFloat(sPath)` and `SetVariableFloat(sPath, fVar)`.
  It has no `GetVariableBool`; `SFXGUIMovie.GetVisible()` says whether the movie is shown.
- `SFXGUIInteraction` also has a `MovieTag_SniperOverlay` (`BioSFHandler_SniperOverlay`, ME2's), but nothing in `SFXGame.pcc`'s
  script opens it (`ShowSniperOverlay` has no caller there; native callers and other packages unchecked): the scope view is the
  zoom reticle inside the reticle movie. The adapter leaves that overlay alone either way.
- Halcyon's ME3 renderer has no crosshair switch (its `[UIRATIO]` only scales the UI viewport so the crosshair sits on the bullets),
  and the draw site (`d3d_capture.cpp` `EmitUiDraw`) sees draws, not movies: the hiding belongs on the game thread, as ME1's
  and ME2's. Nothing changed in `d3d_capture.cpp`.

### What was built

- `plugin/RendererLe3.cpp`: `Caps::screenReticleHide = true`, so `BuildServices` serves `hideScreenReticle`; the request is
  the sequels' one, `plugin/ScreenReticleRequest.hpp` (LE1's 500 ms rule: our layers ask each XR gameplay frame, the crosshair
  comes back by itself when VR frames stop), which ME2's `RendererLe2` uses too now (the same code it had).
  `plugin/SequelOverlays.cpp` already asked: `Hud().handReticle && engine::adapter::IsHudGameplayMode(lastGameMode)`.
- `game/ScreenReticle.cpp`'s LE3 adapter (in place of the stand-down): each mod tick it asks the GUI manager for the movie under
  `MovieTag_Reticle`; a movie that isn't an `SFXSFHandler_Reticle` is left alone. While the host asks and the game mode is
  gameplay (`IsHudGameplayMode`: ME3's on foot, vehicle, Atlas, wheels, squad command, injured Shepard;
  `vr::screen_reticle::HideInGameplay`, ME1's rule with that test), its `_root._alpha` is set to 0 and the game's alpha kept; when
  the request ends, the mode leaves gameplay or the scope view comes up, only our zero is given back (an alpha the game wrote
  since is the game's). Never `SetVisible`, never the weapon reticle's own show/hide.
- **The scope view stays the game's** (ME1's sniper mode 2, ME2's sniper crosshair): the weapon (`Pawn.Weapon`, an `SFXWeapon`)
  has a zoom reticle of a class of its own (`GUIZoomReticleClass` set and not its `GUIReticleClass`) and it is zoomed
  (`SFXWeapon_NativeBase.bIsZoomed`) or the movie shows that class already. No sniper class name is needed, so a DLC rifle's
  own scope counts too. An unreadable `m_oCurrentWeaponReticle` hides nothing (ME1's unknown mode).
- `mevr reticles` on ME3 (and on ME2, which lacked it) prints the movie, `GetVisible`, `_root._alpha`, the saved alpha, the
  current reticle, the weapon, its zoom and zoom reticle, `m_bReticleVisible`, and the decision. Log `[MEVR-CROSSHAIR]` on every
  change, `restore ... alpha N (<why>)`.
- `engine/ReflectCheck`: `CheckLe3ScreenReticle` rows for every name.
- ME1: nothing it compiles changed but `core/vr/ScreenReticle.hpp`'s new `HideInGameplay` (unused there) and static_asserts that
  it says what `Hide` says on ME1's and ME2's numbers. `game/ScreenReticle.cpp`'s LE1/LE2 part is untouched.

### In the game (fake headset, renderer preview, Save 31; session `20261117-080352-885-LE3-ch-crosshair`)

Restored with `verification: 0 differences`, the environment's files exact; `reflect: 774 of 774 checks agree` with the new rows.

- The movie exists on the Normandy: `SFXSFHandler_Reticle Transient.SFXEngine_0.SFXGameViewportClient_0.SFXGUIInteraction_0.
  SFXSFHandler_Reticle_1`, `GetVisible=1`, the game's `_root._alpha` 100. No weapon there: `reticle=None weapon=None
  reticleVisible=0`, so nothing of it was drawn either way.
- On foot (mode 0) the request came with the first VR frames and the alpha went to 0 (`_root._alpha=0 savedAlpha=100
  suppress=true scope=false`).
- The way back, by `mevr omni computer` (the Mission Computer, mode 9): the mod saw mode 9 first and gave the alpha back
  (`restore ... alpha 100 (no longer asked) ok=true`), the host's request lapsed 2 s later; `mevr omni close` (mode 0): 0 again.
- The request lapsed for 0.2-0.4 s three times (twice in the first 12 s after the load, once as the omni request ran): a gap in
  the host's frames past the 500 ms rule, ME1's and ME2's rule too. Each time the alpha went back to 100 and to 0 again on the
  next tick. With a crosshair drawn that could show it for a frame or two at a hitch; watch for it on the combat save.
- **Unverified:** a crosshair actually drawn and then hidden, the shotgun's and heavy weapon's reticles, the scope view kept,
  the Atlas and injured Shepard. No weapon can be drawn on the Normandy; the combat save's check is research/
  le3-combat-fixtures.md "The crosshair hidden on the first combat save".
- The launch's readiness wait (`launch.ps1`, 80 s) ran out once: the XR session became ready about 2 minutes after the launch
  (the game's device came 95 s after the host attached). The protected load and the run went on normally after it.
