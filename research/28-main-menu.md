# 28: Our VR main menu (2026-10-23)

**Question (user, 2026-10-22).** Replace Halcyon's ImGui menu with a high-end first-person VR menu
after the approved mockup (`MainGlass.dc.html` in the "MEVR settings menu" design artifact): frosted
glass panels over the game, a section rail, rows that drive the real settings, Halcyon's own settings
restyled under Advanced, the guided calibration started from it. Technology agreed with the user:
RmlUi with a Direct3D 11 render interface of our own, the menu as an OpenXR quad, zero cost while
closed.

**[inference]** marks our reasoning, **[verify]** what a headset session has to settle (notes/next-vr-session.md
step 25), and **not found** what was looked for and not found.

## Summary

1. **Libraries.** RmlUi 6.3 (MIT) and FreeType 2.13.3 (the FreeType License, chosen of its FTL/GPLv2
   dual licence; GPL-3.0-compatible) as submodules pinned to their release tags, built static with
   their own warning levels, `EXCLUDE_FROM_ALL` (only the LE1 host and the preview link them). RmlUi's
   core and FreeType build in about 20 s; FreeType without zlib, bzip2, PNG, HarfBuzz or Brotli. RmlUi
   finds FreeType through an alias target (`Freetype::Freetype`), with `find_package` disabled.
2. **The renderer.** RmlUi 6.3 ships an official Direct3D 11 renderer
   (`Backends/RmlUi_Renderer_DX11.cpp`); the brief assumed only GL, Vulkan and DX12 existed (true up to
   6.1). It already has everything the GL3 renderer has: geometry, textures, scissor and stencil clip
   masks, transforms, the layer stack with MSAA and render-to-texture (`PushLayer`, `CompositeLayers`,
   `SaveLayerAsTexture`, `SaveLayerAsMaskImage`), the filters (blur, drop-shadow, opacity, the colour
   matrices: brightness, contrast, invert, grayscale, sepia, hue-rotate, saturate) and the shaders
   (linear, radial and conic gradients, repeating, and the "creation" shader). So instead of porting
   GL3, we compile it from the submodule (`rmlui-d3d11`, 4x MSAA) and wrap it
   (`src/plugin/menu/MenuRenderer`):
   - **State.** The game's device is SINGLETHREADED and its immediate context is shared with the game
     and Halcyon. RmlUi's own backup misses the render targets and depth view, the second constant
     buffer and texture slot, and leaves a bound geometry shader active; `StateGuard` saves and
     restores all of it (IA, VS and PS with b0-b1 and t0-t1, s0, RS, OM) and turns GS/HS/DS off while
     we draw. The backdrop's passes use the same guard.
   - **Our textures.** RmlUi asks the renderer for textures by name; `mevr:<name>` (an `<img src>` or a
     decorator's image) returns one of ours, updated in place: the blurred backdrop and the colour
     test strip. The system interface's `JoinPath` keeps those names away from the document's folder.
   - **Output.** RmlUi blends premultiplied colour in sRGB-encoded space, like a browser. The canvas is
     R8G8B8A8_UNORM holding those bytes, copied raw into an `_SRGB` swapchain (as `SquadPanel` does), so
     the compositor decodes them once.
3. **The look.** `assets/menu/main.rml` and `main.rcss`, deployed to `ASI\menu`, reloaded within a
   second of a change (`[Menu] assets` can point at the repo's folder). The mockup's CSS mostly maps to
   RCSS; three things it draws with CSS that RCSS lacks are our own decorators
   (`src/plugin/menu/MenuDecorators`):
   - `chamfer(...)`: `clip-path` cut corners (top-left and bottom-right), a vertical gradient fill inside
     a ring of a vertical gradient (the buttons' top-lit 1 px edge), a 1 px inner highlight;
   - `backdrop(...)`: the whole panel: the blurred world across the 28 px-chamfered outline, its 1 px
     cyan edge, the two orange corner accents and the faint scanlines (clipped to the outline, so the cut
     corners stay clear in the headset);
   - `icon(name)`: the mockup's inline SVG icons (and a few more for Diagnostics: 21) as mitred strokes and
     fills, in the element's colour
     (rebuilt when the colour changes: RmlUi only regenerates decorators on layout).
   The rest is RCSS: `linear-gradient` decorators, `box-shadow` (inset highlights, drop shadows, the
   orange glow), `filter: drop-shadow` on the active section's icon, `font-effect: glow`,
   `letter-spacing`, `text-transform`, transitions (the light sweep and the toggles' knob), `:active`
   transforms (the press dip), `:focus-visible` (the pad's ring).
4. **Where it shows.** A quad in Halcyon's app space, placed when it opens 1.3 m ahead of the head
   (its heading only, level, 0.1 m below the eyes), 1.45 m wide (`[Menu] distanceM`, `widthM`), kept
   there until it closes, placed afresh on recenter. While it's open it is the only one of our quads
   (SteamVR may sort quads by distance: Halcyon's `[MENUVIS]` notes). **Not a cylinder:**
   `XR_KHR_composition_layer_cylinder` isn't enabled by Halcyon's `xrCreateInstance` (it enables
   `XR_KHR_D3D11_enable` and, if offered, `XR_FB_display_refresh_rate`); enabling it would be a seam in
   instance creation, and the pointer and the backdrop would need a cylinder's mapping. Deferred.
   SteamVR does offer it **[verify]**.
5. **The glass.** A true backdrop filter would need the composited picture behind the quad, which only
   the runtime has. Instead (`src/plugin/menu/MenuBackdrop`), ten times a second:
   1. the left eye's image is copied as Halcyon releases it (testloop's release tap, as the sniper
      scope does);
   2. downsampled to at most 960 px (four bilinear taps each);
   3. projected onto the panel: each of 480 x 300 backdrop pixels is a point on the quad, found in that
      eye image through the pose and FOV it was rendered for (so the glass shows what is really behind
      it);
   4. blurred: two rounds of a 9-tap separable Gaussian with taps two texels apart, about 24 of the
      menu's pixels (the mockup's `blur(22px)`);
   5. graded: 62% saturation, about half the brightness, a cool lift, a faint vignette.
   The world being fixed in the room and the panel too, a 10 Hz update reads as glass. Over a flat
   screen (game menus, flat cutscenes) there is no world image: a plain dark ground.
6. **Input.**
   - **The pointer:** a controller's aim ray on the quad is RmlUi's mouse (`core/vr/MenuPointer`'s
     intersection; smoothing and a 1.5 px dead band from `MevrMenuPointer`; the hand that points
     follows the ray on the panel). The trigger is the button, the right stick the wheel. A dot follows
     it. A 12 ms tick on each new control under it, a 20 ms one on a click (`core/vr/Haptics` through
     the host's channel).
   - **The pad:** a real pad (captured in the XInput hook before it's neutralised) or the Touch left
     stick moves the focus with RmlUi's spatial navigation (`nav: auto`; sliders take left and right),
     repeating after 400 ms every 140 ms. A clicks the focused control, B backs out to the rail, then
     closes. On the Display page holding A also holds the colour comparison (`colour::HoldCompare`).
   - **The game sees nothing:** while open the pad is neutral (our `MergeMotionControllers`), and
     Halcyon's mouse, keyboard, DirectInput, raw input and window-message blocks hold as for its own
     menu ([MEVR] `InputBlocked()` in `vr_menu.cpp`). The mod sees no controllers (published hidden, as
     during the calibration), so holsters, grenades, melee and the squad trigger stand down.
7. **Opening, closing, pausing.**
   - Both sticks held (`gamepad::UpdateStickChord`) and `\` toggle it; Resume and B close it. Ctrl or
     Shift + `\` toggles Halcyon's own menu instead ([MEVR] `OnMenuKey` replaces xr_session's
     `Menu::Toggle()`), and Diagnostics has a "Classic renderer menu" row. One menu at a time.
   - **Pause:** HostServices v16 `mainMenu()` gives the mod `kMainMenuOpen | kMainMenuPause`;
     `game/MenuPause` calls `WorldInfo.PauseGame(true)` through ProcessEvent, what ME1's own weapon wheel
     does (`SFXGameModeWeaponWheel.Activated`, research/19), and `PauseGame(false)` when it closes, the
     toggle goes off or the mod unloads. Only on foot or in the Mako (game modes 0 and 1): the game's own
     menus and wheels pause by themselves, and conversations and cutscenes aren't ours to stop. Default
     on (`[Menu] pause = 1`, Comfort > Pause while open).
   - Start calibration closes the menu and starts `plugin/Calibration`.
8. **Cost.** Closed: `Layer()` returns after one atomic load; no RmlUi update, no render, no swapchain,
   no layer, no tap; the input hooks test one atomic. Its textures and RmlUi stay allocated after the
   first opening (first opening: RmlUi, the fonts and the document load on the render thread, logged
   as `main menu: ready in N ms`). Open, it renders only when something changed (input, a value, a
   running transition, a new backdrop: about 10 renders a second with a still pointer, every frame
   while it moves), and reuses the swapchain image otherwise.
   - **Measured offline** (`mevr-menu-preview`, the RTX 4090 while the game ran): one menu frame
     (1440 x 900, 4x MSAA, the Display page) 0.12 to 0.57 ms CPU and 0.12 to 0.15 ms GPU; the backdrop's
     passes are a few small full-screen draws at 480 x 300 plus one eye-sized copy every 100 ms.
   - **In the game [verify]:** logged every 10 s while open: `main menu: open N s: X ms CPU per frame,
     Y ms GPU per frame, R renders/s, B backdrops/s`; snapshots carry it (`"mainMenu"`).

## What each section drives

Our settings live in `ASI\LE1MEVR.ini` (the mod re-reads it within a second), written one key at a time
with `WritePrivateProfileString` as the VR hands tab does (a slider's value once it has rested 300 ms;
a re-read meanwhile shows the pending value: `core/vr/MenuSettings` `PendingWrites`, unit-tested).
Halcyon's are its live `MELEVR::Config::Get()`, edited as its menu edits them and saved with its own
`SaveProfile(ActiveProfileIndex())` when the menu closes or two seconds after the last change (at once
for the VR mode, the resolution and depth of field, which also write `GamerSettings.ini` for the next
launch, as Halcyon's menu does). Nothing writes `MELEVR.ini` wholesale except Halcyon's own save, and
nothing writes defaults over a profile: Reset is for the session (below).

| Section | Row | Setting |
|---------|-----|---------|
| Top bar | Headset chip | `xrGetSystemProperties`/runtime names (`core/vr/MenuSettings` `HeadsetLabel`: SteamVR's driver names mapped: `playstation_vr2` = PlayStation VR2...) |
| | Controllers chip | the calibration's interaction-profile label |
| | Calibrated chip | `[Calibration.<key>]` saved |
| Comfort | Turn style, Snap angle, Turn speed | `[Comfort] turn`, `snapDeg`, `turnSpeed` (the mod's own turn, research/31) |
| | Comfort vignette, Vignette when, Vignette strength | `[Comfort] vignette`, `vignetteWhen`, `vignetteStrength` (research/31) |
| | Walk toward: Where I look / Left hand | Halcyon `moveFollowsHead` / `[Comfort] walk = hand` (research/31) |
| | Physical crouch | `[Hands] physicalCrouch` |
| | Life-size movement | `[Camera] lifeSize` and Halcyon `leanGain` (2 / 1), as the VR hands tab |
| | Pause while open | `[Menu] pause` (HostServices v16, `game/MenuPause`) |
| Controls | Grip holds the gun | `[Holsters] gripHolds` |
| | Body holsters | `[Holsters] enabled` |
| | Holster buzz | `[Holsters] hapticAmplitude` (0-100%) |
| | Left trigger: Weapon wheel / Aim / Off | `[Controls] leftTrigger` = `lb` / `lt` / `none` |
| | Physical grenades and melee | `[Throwables] enabled` and `melee` |
| | Recoil buzz | `[Hands] recoilHaptic` |
| | Aim assist | `[Hands] aimAssist` |
| Body & calibration | Hero, Start calibration | `plugin/Calibration` `GetSummary`, `Start` (the menu closes) |
| | Tiles | `[Calibration.<key>]` `standEyeM`, `crouchEyeM`, `eye`, `pistolSpreadDeg`, `rifleSpreadDeg`; `[Arms] armLengthM` |
| | Recenter | `Menu::RequestRecenter` |
| | Line up with Shepard's head | `MevrBridge::StartCameraAlign` (the VR hands tab's left-trigger alignment; the menu closes) |
| | Measure arms only | a T pose and a trigger pull: `MevrBridge::TakeArmSample` (`[Arms] measure*`) |
| | Hand angle trims | `[Hands] handPitch`, `handYaw`, `handRoll` (±30 degrees here) |
| Display | Colour profile Auto / Neutral / Vivid / Custom | `[Colour.<headset>] profile` (`colour::SelectProfile`); Quest stays a file setting |
| | Brightness, Contrast, Saturation, Mid-tones, Black level | the headset's Custom values (`colour::SetCustom`: live while moving, saved once at rest) |
| | Hold A | `colour::HoldCompare` |
| | Preview | `core/vr/ColourStrip` through `core/vr/Colour` `Correct` (the shader's CPU reference) |
| | Render resolution Headset / Headset × 0.85 / Headset × 0.75 | the runtime's recommended eye size (Halcyon's `g_recommendedEyeWidth/Height` through `XrSession::GetResolutionStats`, as the perf probe's `"recommendedEye"`) exactly, or 0.85 / 0.75 of it rounded to even (`menu_settings::HeadsetSize`; PS VR2: 2824 × 2880, 2400 × 2448, 2118 × 2160) into Halcyon `sfr2ResX/Y` and `expectedResX/Y`, `ApplyResolutionForCurrentMode` (GamerSettings ResX/ResY, restart). Selected when `sfr2ResX/Y` equal its size (`ResolutionChoice`). Before VR has started: one disabled chip, "Headset (unknown until VR starts)" (research/32) |
| | Fixed sizes Low / Performance / Balanced / Sharp / Max | Halcyon `sfr2ResX` 2048 / 3072 / 4096 / 5120 / 6144, `sfr2ResY` = width / 0.964 even, and `expectedResX/Y` (since 2026-10-23; before, the watchdog kept the old size), `ApplyResolutionForCurrentMode` (restart). Selected when `sfr2ResX` is the tier's width; a size no chip makes selects none |
| | Upscale Off / FSR | `[Upscale] method` = `off` / `fsr` (plugin/Upscale re-reads it within a second: live); `sharpness` stays a file setting (research/32) |
| | Steady cutscenes | Halcyon `cineFpsCap` |
| HUD & sights | Distance | Halcyon `hudPanelDistanceM` |
| | Size | Halcyon `hudPanelWidthM` (100% = 1.3 m) |
| | Hand reticle | Halcyon `handReticle` |
| | Pistol / Shotgun and assault rifle / Sniper rifle | `[Sights] pistol`, `shotgun` + `assaultRifle`, `sniperRifle` (`scope` keeps its zoom) |
| | Sight alignment help | `[Sights] assist` |
| Advanced > Tracking | Recenter key, Look speed, Smoothing | `recenterKeyEnabled`, `lookSensitivity`, `headLookSmoothing` |
| | Conversations / Cutscenes in VR, Flat game menus, Flat galaxy map | `cineVrConvo`, `cineVrCutscene`, `menuFlat`, `galaxyFlat` |
| Advanced > First person | First person, Hide head | `fpEnabled`, `fpHideHead` |
| | Back, Right, Up | `flatScreenDistance`, `cameraOffsetRight`, `cameraOffsetUp` (metres, the camera alignment's fields; their switches turned on) |
| Advanced > View | Lean, Lean gain | `leanEnabled`, `leanGain` |
| | Flat screen | the flat screens' widths (`menu`, `galaxy`, `convo`, `cutscene`, `movieMonoQuadWidth`) |
| | Fill the field of view | `vrFovFillEnabled` |
| Advanced > Stereo | VR mode Mono / Stereo / Depth | `sfr2Enabled` / `dibrEnabled` (AER and the old side-by-side never), saved, resolution written (restart) |
| | Eye distance, Convergence, Swap eyes, Quest FOV match | `sfr2HalfEyeUU`, `sfr2Convergence`, `sfr2SwapEyes`, `questFovMatch` |
| | Depth of field | `!disableDof`, `D3DCapture::EnsureDepthOfFieldSetting` (restart) |
| | Desktop mirror Normal / Every 2nd / Every frame | `mirrorPresentEvery` 8 / 2 / 1 |
| Advanced > HUD placement | Width, Below eyes, Follow past, Place | `hudPanelWidthM`, `hudPanelDropM`, `hudPanelFollowDeg`, `RequestHudPanelReplace` |
| | Combat HUD, Conversation wheel, Mission overlays | `pchudEnabled`, `convoEnabled`, `designerUiEnabled` |
| | Move and resize a screen | opens the classic menu (its HUD tab's element editor) |
| Advanced > Profiles | Load / Save per slot | `Config::LoadProfile(i)` / `SaveProfile(i)` (Default, Custom 1-3) |
| Advanced > Diagnostics | Snapshot, Record, Measure frame timing | `testloop::RequestSnapshot` (1 s after the menu closes), `RequestRecording`, `perf::Request` (20 s, after it closes) |
| | Reset renderer settings | two presses; `Config::ResetDefaults` for the session, keeping the menu key, no head aim (`combatHeadAim`, `makoHeadAim`), the resolution, first person and the first-run flag; **not saved** (Profiles > Save keeps it) |
| | Classic renderer menu | Halcyon's menu (also Ctrl + `\`) |

Left out, as agreed: head aim, the experimental alternate-eye (AER) and depth-pop modes, per-screen
scale/offset sliders.

## RCSS notes (what bit while matching the mockup)

- `rgba()`'s alpha is 0-255 or a percentage in RmlUi, not 0-1: `rgba(255,255,255,0.09)` is transparent.
  The stylesheet uses percentages.
- Text straight inside a flex container doesn't lay out: the chips are `display: block` with centred
  text.
- A child moved by a `transform` doesn't count as overflow, so `overflow: hidden` alone doesn't clip
  the button's light sweep: `clip: always`.
- Tweens are `<name>-<in|out|in-out>` (`linear-in-out`, `cubic-out`).
- `box-shadow` covers the background and border too (they render with it into one texture, cached per
  element size).

## The game's menus (2026-10-25)

**Problem (user, PS VR2 Sense controllers through SteamVR).** No way into Mass Effect's own menu: every control is
taken. The only route was the left controller's `/input/menu/click` (the Touch profile's one menu button) as the game's
Start (`game/GamepadMapping`: `l.menu` is Start, Back with the left grip), and most players will be on a Quest over
Link, where the controllers' menu and system buttons can't be counted on to reach an application. So **our menu's
buttons are the way into the game's menus**, not a fallback, and nothing depends on the menu button.

**What Start and Back are bound to** (re-read 2026-10-25, read-only, as research/19 section 6: `BIOInput.ini` inside
`CookedPCConsole\Coalesced_INT.bin`):
- `XboxTypeS_Start` = `Console_Menu` = `ShowMenuGui` in `SFXGameModeDefault`, the power and weapon wheels, the Mako
  and photo mode: the game's pause menu (its hub: squad, journal, map, codex, equipment, save, load, options).
- `XboxTypeS_Back` = `Console_Holster` = `TryHolster` on foot (`SFXGameModeDefault`), and `Console_MinigameQuit` =
  `ExitMinigame` in `SFXGameModeMinigame` (the bypass minigames). No other mode binds Back (the GUI mode binds only Y,
  photo mode; `Engine.PlayerInput` has `ReportCustomEvent`). **Back opens no menu in LE1**, so its button says what it
  does: "Holster".
- The PC's own hotkeys open single screens (`PC_ShowMap` M, `PC_ShowEquipment` I, `PC_ShowSquad` U, `PC_ShowJournal` J,
  `PC_ShowCodex` O: `ShowMapGui true` ...); not used: the pause menu reaches all of them.

**Built.** Two buttons in the top bar before Resume (`assets/menu/main.rml` `.gamebtns`: an icon, the name, and what it
opens on a second line): **Game menu** (`cmd('game_menu')`) and **Holster** (`cmd('game_back')`). They're ordinary
buttons in the focus order (the pad's A, the pointer's trigger). Pressing one closes our menu (`After::GameStart` /
`GameBack` in `plugin/MainMenuSettings`), which lifts the pause on the mod's next tick (`game/MenuPause`), and hands the
button to the XInput hook (`main_menu::TakeGamePress`). The hook (`HalcyonBridge::MergeMotionControllers`) presses it
with `gamepad::MenuPress` (`game/GamepadMapping`, unit-tested): it waits until the game has read the pad 8 times over
250 ms with our menu closed (a button that goes down while the game is still paused, or before the window takes input,
can be lost), holds it 100 ms and at least 3 reads, then lets go; it's OR'd into whatever the game sees (the
controllers' mapping or a real pad; with no motion controllers, the real pad). Opening our menu again calls it off; no
reads for 5 s drops it. Logs: `main menu: Game menu: the game's Start goes to the game once it reads the pad again`,
`main menu: sent the game's Start (its pause menu): held 100 ms`, `main menu: the game's Start (its pause menu) let go`
(Back: `Holster`, `Back (holster)`), `main menu: ... not sent: ...`.

**Diagnostics** (the next session, notes/next-vr-session.md step 38): the interaction profile the runtime picked for
each hand (`halcyon bridge: the runtime's interaction profile: left ..., right ...`, on change), and the first time each
controller's menu action reads true in a session (`halcyon bridge: the left controller's menu button reached us
(/input/menu/click, profile ...)`). The menu action is read for both hands; it's bound for the left hand in the Touch
profile (the only menu path it has: a right `/input/menu/click` there would make the runtime reject the whole Touch
suggestion) and for both in `khr/simple_controller`; Index has none. No profile was added: a new one (Vive, WMR) could
change which profile SteamVR picks for an emulated controller and lose A/B/X/Y.

## Letting go of the pad (QA, 2026-10-26)

- **What closed it stays the menu's:** the A on Resume, the B that closes the top level, the trigger that clicked
  Resume and the stick clicks of the chord used to reach the game on the read after the close (a jump, a shot, a
  crouch). The host's XInput hook now keeps every button and trigger that is down when our menu (or the guided
  calibration) gives the pad back from the game until each is released (`gamepad::CloseLatch`, unit-tested
  `TestCloseLatch`; log `halcyon bridge: ... kept from the game until released`). The Game menu / Holster press is OR'd
  in after it, so it still goes through.
- **VR stops with the menu open:** the menu kept the game paused (`kMainMenuPause`), its pad neutral and, through
  Halcyon's `InputBlocked`, its mouse and keyboard blocked, on the desktop with nothing drawn. `main_menu::OnPresent`
  (every Present, from `PerfPresentEnd`) closes it when its quad hasn't been drawn for over 1 s (the session
  STOPPING, EXITING or LOSS_PENDING all end the frames; so does a sleeping headset) and cancels a running calibration
  when no VR frame has run for that long; the pause lifts through the usual path (the mod's `game/MenuPause`).

## Checking it offline

`mevr-menu-preview [folder] [--warp]` (`tools/menu-preview`): the host's own RmlUi setup, renderer,
decorators, document, data model and backdrop passes on a device of its own, over a stand-in scene,
with sample values. It writes `section-<name>.png` for each section (and `section-display-quality.png` /
`-quality-novr.png`: the image quality rows with the PS VR2's 2824 × 2880 known, and before VR starts), `advanced-<tab>.png` for each of
Advanced's tabs, `state-hover.png` (the sweep part-way), `state-pressed.png` (the dip), `state-focus.png`
(the pad's ring), `menu-alpha.png` (the texture alone) and `timing.txt`.

## Open

- **[verify]** In the headset: the glass's look (the grade's numbers are a judgement call), the text size
  at 1.3 m (16 px descriptions are about 0.75 degrees), the pointer's feel, the pause.
- The cylinder layer (above).
- The rows marked Soon: built 2026-10-23 ([31](31-comfort.md)).
- Keyboard and mouse navigation of the menu itself (the desktop): not built (the game's mouse and
  keyboard are blocked while it's open; `\` closes it).
