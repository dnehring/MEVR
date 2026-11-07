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
| Controller guide | Both controllers' labels and lines | read only: `core/vr/MenuSettings` `GuideTexts` from `[Controls] leftTrigger`, `[Squad] enabled`/`style`, `[Holsters] enabled`/`gripHolds`/`powerWheel`, `[Throwables] enabled`/`melee`/`hands`/`grenadeButton`/`meleeButton`, `[Vent] enabled`, the turn and walk chips, `[Hands] physicalCrouch` (section "The controller guide", below) |
| Top bar | Headset chip | `xrGetSystemProperties`/runtime names (`core/vr/MenuSettings` `HeadsetLabel`: SteamVR's driver names mapped: `playstation_vr2` = PlayStation VR2...) |
| | Controllers chip | the calibration's interaction-profile label |
| | Calibrated chip | `[Calibration.<key>]` saved |
| Comfort | Turn style, Snap angle, Turn speed | `[Comfort] turn`, `snapDeg`, `turnSpeed` (the mod's own turn, research/31) |
| | Comfort vignette, Vignette when, Vignette strength | `[Comfort] vignette`, `vignetteWhen`, `vignetteStrength` (research/31) |
| | Walk toward: Where I look / Left hand / Body | Halcyon `moveFollowsHead` / `[Comfort] walk = hand` (research/31) / `moveFollowsHead = 0` (its description warns of Halcyon's "look left, move right" bug, `vr_menu.cpp` `[MOVEFIX]`) |
| | Physical crouch | `[Hands] physicalCrouch` |
| | ~~Life-size movement~~ | Removed 2026-11-06 (the owner: only life-size movement): Halcyon's `leanGain` is always 2 (`vr_config` `[MEVR]` seam) |
| | Conversations / Cutscenes: In VR / 3D screen / Flat screen | each its own: Halcyon `cineVrConvo` / `cineVrCutscene` off for flat, else `[Cinematics] conversations` / `cutscenes` 1 (3D screen) or 0 (research/52); Screen width, distance, 3D depth while either is on the screen |
| | Game screens in the room, Hacking screen, Galaxy map hologram | `[WorldUi] on`, `wrist` (research/53), `[Galaxy] hologram` (research/51) |
| | Pause while open | `[Menu] pause` (HostServices v16, `game/MenuPause`) |
| Controls | Grip holds the gun | `[Holsters] gripHolds` |
| | Body holsters | `[Holsters] enabled` |
| | Holster buzz | `[Holsters] hapticAmplitude` (0-100%) |
| | Left trigger: Weapon wheel / Aim / Off | `[Controls] leftTrigger` = `lb` / `lt` / `none` |
| | Fingers follow yours | `[Hands] fingers` (research/43) |
| | Physical grenades and melee | `[Throwables] enabled` and `melee` |
| | Vent by racking the gun | `[Vent] enabled` (research/45) |
| | Recoil buzz | `[Hands] recoilHaptic` |
| | Aim assist | `[Hands] aimAssist` |
| | Body stays relaxed, Steady view, Draw speed | `[Hands] exploreStance`, `constantEye`, `drawRate` (research/38) |
| | Throttle lever and drive stick, Sit in the cockpit | `[Mako] controls` (research/46), `[Cockpit] enabled` (research/47) |
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
| | Performance Quality / Balanced / Performance (Custom only shown) | each mode's bundle (`core/vr/PerformanceModes`, research/60): `[Upscale] method`, `[Performance] minScreenRadiusPx`, `[SystemSettings] FoliageDrawRadiusMultiplier`, `DynamicShadows`, `AmbientOcclusion` (live), then the render resolution as the row below sets it (restart); selected when the files hold exactly a mode's bundle, else Custom with what differs from the nearest mode (since 2026-11-06) |
| | Render resolution Headset / Headset × 0.85 / Headset × 0.75 | the runtime's recommended eye size (Halcyon's `g_recommendedEyeWidth/Height` through `XrSession::GetResolutionStats`, as the perf probe's `"recommendedEye"`) exactly, or 0.85 / 0.75 of it rounded to even (`menu_settings::HeadsetSize`; PS VR2: 2824 × 2880, 2400 × 2448, 2118 × 2160) into Halcyon `sfr2ResX/Y` and `expectedResX/Y`, `ApplyResolutionForCurrentMode` (GamerSettings ResX/ResY, restart). Selected when `sfr2ResX/Y` equal its size (`ResolutionChoice`). Before VR has started: one disabled chip, "Headset (unknown until VR starts)" (research/32) |
| | Upscale Off / FSR | `[Upscale] method` = `off` / `fsr` (plugin/Upscale re-reads it within a second: live); `sharpness` is Advanced > Performance's (research/32) |
| | Steady cutscenes | Halcyon `cineFpsCap` |
| HUD & sights | Distance | Halcyon `hudPanelDistanceM` |
| | Size | Halcyon `hudPanelWidthM` (100% = 1.3 m; 45-155%, the range Advanced's Width had) |
| | Below your eyes, Comes round past, Bring it in front of me | `hudPanelDropM`, `hudPanelFollowDeg`, `RequestHudPanelReplace` (moved from Advanced, 2026-11-04) |
| | Hand reticle | Halcyon `handReticle` |
| | Status on your wrist, Weapon panel on the gun, Prompts at the object | `[Hud] wristStatus`, `gunStatus` (research/41), `[Interact] label` (research/40) |
| | Pistol / Shotgun / Assault rifle / Sniper rifle | `[Sights] pistol`, `shotgun`, `assaultRifle`, `sniperRifle` (`scope` keeps its zoom; the shotgun has no scope chip) |
| | Sight alignment help, Shots land on the dot | `[Sights] assist`, `converge` |
| | Scope picture in the game's colours | `[Sights] scopeCapturePost` (research/39: the mod applies it to the next capture it makes, so from the next level) |
| Advanced > Tracking | Recenter key, Look speed, Smoothing | `recenterKeyEnabled`, `lookSensitivity` (its description warns of sickness away from 1.00×), `headLookSmoothing` |
| | Flat game menus, Flat galaxy map | `menuFlat`, `galaxyFlat` (the conversation and cutscene toggles that were here are Comfort's rows) |
| Advanced > First person | First person, Hide head | `fpEnabled`, `fpHideHead` |
| | Back, Right, Up | `flatScreenDistance`, `cameraOffsetRight`, `cameraOffsetUp` (metres, the camera alignment's fields; their switches turned on) |
| Advanced > View | Lean | `leanEnabled` (Movement scale removed 2026-11-06: `leanGain` is always 2) |
| | Flat screen width | the flat screens' widths (`menu`, `galaxy`, `convo`, `cutscene`, `movieMonoQuadWidth`) |
| | Fill the field of view | `vrFovFillEnabled` |
| Advanced > Stereo | VR mode Mono / Stereo / Depth | `sfr2Enabled` / `dibrEnabled` (AER and the old side-by-side never), saved, resolution written (restart) |
| | Real eye positions, Extra depth, Each eye's own field of view | `sfr2Metric` (research/30's life-size stereo), `sfr2MetricDepth`, `sfr2MetricEyeFrustum` |
| | Eye distance (cm), Convergence, Swap eyes (only without real eye positions) | 2 × `sfr2HalfEyeUU` (the eyes apart in game centimetres at 100 units per metre), `sfr2Convergence`, `sfr2SwapEyes` |
| | Quest Link image fix | `questFovMatch` |
| | Depth of field | `!disableDof`, `D3DCapture::EnsureDepthOfFieldSetting` (restart) |
| | Desktop mirror Every 8th / Every 2nd / Every frame | `mirrorPresentEvery` 8 / 2 / 1 (the deploy's is 2) |
| Advanced > Performance | Small-object culling Off / 4 / 8 / 16 / 24 / 32 px | `[Performance] minScreenRadiusPx` (plugin/RenderLevers, live; an old file's `minScreenRadiusTenths`, read first, emptied) |
| | Grass distance 25-100% | `[SystemSettings] FoliageDrawRadiusMultiplier` (game/SystemSettings' `scale set`, live) |
| | Dynamic shadows, Ambient occlusion | `[SystemSettings] DynamicShadows`, `AmbientOcclusion` (`True` / `False`, live) |
| | FSR sharpness 0-100% | `[Upscale] sharpness` 0..1 (no mode's) |
| | Fixed sizes 2048 / 3072 / 4096 / 5120 / 6144 (Halcyon's ladder Low to Max; on Display until 2026-11-06) | Halcyon `sfr2ResX` 2048 / 3072 / 4096 / 5120 / 6144, `sfr2ResY` = width / 0.964 even, and `expectedResX/Y` (since 2026-10-23; before, the watchdog kept the old size), `ApplyResolutionForCurrentMode` (restart). Selected when `sfr2ResX` is the tier's width; a size no chip makes selects none |
| Advanced > Game UI | Conversation wheel: Fit it to your view, Size, Left / right, Down / up, Standard | `convoEnabled` and the conversation movie's set the renderer uses (`convo*Dibr` in Depth mode, `convo*Stereo` with the old side-by-side stereo, else `convoScaleX/Y`, `convoOffsetX/Y`): size as a percentage of Halcyon's tuned scale (both axes, their ratio kept), the offsets as a nudge from its tuned place in movie pixels (up +, so `Offset Y` = tuned - value); Standard puts the tuned values back (its HUD tab's Reset) |
| | Mission overlays: the same | `designerUiEnabled`, `designerUiScaleX/Y`, `designerUiOffsetX/Y` (one set for every mode) |
| Advanced > Profiles | Load / Save per slot | `Config::LoadProfile(i)` / `SaveProfile(i)` (Default, Custom 1-3) |
| Advanced > Diagnostics | Snapshot, Record your movements, Measure frame timing | `testloop::RequestSnapshot` (1 s after the menu closes), `RequestRecording` (a `.mevrrec` in `%LOCALAPPDATA%\MEVR\recordings` for the player to attach to an issue: the report zip doesn't take it), `perf::Request` (20 s, after it closes; the note names `AppData\Local\MEVR\perf`) |
| | Reset VR settings | two presses; `Config::ResetDefaults` for the session, keeping the menu key, no head aim (`combatHeadAim`, `makoHeadAim`), the resolution, first person and the first-run flag; **not saved** (Profiles > Save keeps it) |
| | Classic VR menu | Halcyon's menu (also Ctrl + `\`) |

Left out, as agreed: head aim, the experimental alternate-eye (AER) and depth-pop modes. Per-screen scale and offset
sliders came in 2026-11-04 for the two movies that take them on the default path (Advanced > Game UI, below); the combat
HUD's own transform and the per-element editor stay in the classic menu (the HUD panel shows that movie at its authored
layout, research/15, so they do nothing there).

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

**Built (2026-10-25).** Two buttons in the top bar before Resume (`assets/menu/main.rml` `.gamebtns`: an icon, the name,
and what it does on a second line): **Game menu** (`cmd('game_menu')`) and **Holster** (`cmd('game_back')`). They're
ordinary buttons in the focus order (the pad's A, the pointer's trigger). Pressing one closes our menu (`After::GameStart`
/ `GameBack` in `plugin/MainMenuSettings`), which lifts the pause on the mod's next tick (`game/MenuPause`).

**The first way failed (2026-10-26 10:38, the user in the headset).** It faked the button: the host's XInput hook
(`HalcyonBridge::MergeMotionControllers`) waited until the game had read the pad 8 times over 250 ms with our menu
closed, then held Start 100 ms (`gamepad::MenuPress`). The log had `main menu: Game menu: the game's Start goes to the
game once it reads the pad again` at 23.733, `menu pause: the game runs again` at 23.749, and nothing more until the
player opened our menu again at 25.178 (`not sent: our menu opened again first` at 25.188): in 1.45 s of a running game
the hook never counted 8 reads, so Start never went down. Why the reads stopped wasn't found: the game did read the pad
10 ms after our menu opened again (that read logged the drop), and the hook is the same one that feeds the controllers
all game, so it isn't a dead path; it looks like the game reads XInput far less often than every frame right after an
unpause, or not at all for a while. **[verify]** if it matters again; it no longer does, because:

**Built (2026-10-26): the game's own functions, no button.** Each button leaves a request in the host
(`vr::game_menu::Slot` in `plugin/MainMenu`, `core/vr/GameMenuRequest`, unit-tested); the mod takes it each tick
(HostServices **v19** `takeGameMenuRequest`, `game/GameMenuRequest`), waits until our menu is closed and its pause lifted
for 2 ticks in a row (`vr::game_menu::Schedule`: our menu opening again calls it off, as does waiting 600 ticks), and then
on the game thread, through ProcessEvent:
- **Game menu**: `ShowMenuGui` (what Start's `Console_Menu` runs; no parameters, returns a bool, `FUNC_Exec`) on the
  **active** game mode object, the function looked up by name on that object's class so the override runs
  (`SFXGameModeVehicle`, `-PowerWheel`, `-WeaponWheel`, `-Command` and `-Photo` override `SFXGameModeBase`'s; on foot
  the mode is `SFXGameModeDefault`, which doesn't). The active mode: the `GameModeManager.GameModes` entry of
  `CurrentMode`'s class (EGameModes 0 Default, 1 Vehicle, 2 PowerWheel, 3 WeaponWheel, 4 Command); for any other mode the
  active one (`SFXGameModeBase.bIsActive`) of the highest `Priority`. Log `main menu: opened the game's pause menu
  (SFXGameModeDefault.ShowMenuGui (declared on SFXGameModeBase) returned 1; game mode 0)`, or `... didn't open (...
  returned 0 ...)` (the mode allows none: `bAllowPauseMenu`).
- **Holster**: `SFXGameModeDefault.TryHolster` (Back's `Console_Holster`), as `game/Holsters` calls it, only on foot
  (`CurrentMode` 0) and only while `BioPawn.IsInCombatActionState` says a weapon is drawn: it's the holster key's toggle,
  so with the weapon away it would draw (which `[Holsters] onlyHolstersDraw` forbids anyway). Logs `main menu: holstered
  the weapon (SFXGameModeDefault.TryHolster)` or `main menu: holster: no weapon drawn, nothing to put away`. (Back also
  quits a bypass minigame; this button doesn't: it says Holster.)

The host logs `main menu: the game's pause menu: asked the mod ...` (or `holster: asked the mod ...`); the mod `main menu
request: ... taken`, then the lines above, or `main menu request: ... dropped: the main menu opened again first`. A
host whose mod never polled warns at the press (`the mod has never asked for these requests ...`): host and mod are
deployed together (the release build is one ASI anyway). The pad path (`gamepad::MenuPress`) is gone, not kept as a
fallback: it was the part that didn't work, and an older mod with this host only happens mid hot reload. Snapshots:
the mod's JSON has `"gameMenu": {"waiting": ..., "last": ...}`. ReflectCheck rows: the five mode classes, `bIsActive`,
`Priority`, each `ShowMenuGui` declaration and its `ReturnValue`.

**Diagnostics** (the next session, notes/next-vr-session.md step 38): the interaction profile the runtime picked for
each hand (`halcyon bridge: the runtime's interaction profile: left ..., right ...`, on change), and the first time each
controller's menu action reads true in a session (`halcyon bridge: the left controller's menu button reached us
(/input/menu/click, profile ...)`). The menu action is read for both hands; it's bound for the left hand in the Touch
profile (the only menu path it has: a right `/input/menu/click` there would make the runtime reject the whole Touch
suggestion) and for both in `khr/simple_controller`; Index has none. No profile was added: a new one (Vive, WMR) could
change which profile SteamVR picks for an emulated controller and lose A/B/X/Y.

## Rows that show what's in effect (2026-10-26, QA)

An adversarial pass found rows that showed something other than the setting in effect; each now reads the real value:
- **Life-size movement** (removed as a choice 2026-11-06: Halcyon reads `leanGain` as 2 whatever a profile says) is Halcyon's `leanGain` of 2 in the live profile (the separate `[Camera] lifeSize` key could
  disagree with a loaded profile; it's no longer read, by the menu or the VR hands tab's forward emphasis).
- **First person's Back / Right / Up** read 0 while Halcyon's `screenDistanceEnabled` / `cameraOffsetEnabled` are off
  (moving one turns its switch on, as before).
- **Walk toward** has a third chip, **Body** (Halcyon's `moveFollowsHead = 0`), which used to light none.
- **Sniper rifle** has a Red dot chip (`sniperRifle = dot` lit none).
- **Physical grenades and melee** is on only when both are; set apart in the file, the row says which (it writes both).
- **After Reset** (not saved over the profile) the render resolution and VR mode no longer write GamerSettings.ini
  alone: the note says Profiles > Save keeps it, and saving into the active slot writes both.
- The rail shows **"Can't save settings: the game's ASI folder is read-only"** when a write of `LE1MEVR.ini` fails
  (`MainMenuSettings::Wrote`); the Recenter key and Classic renderer menu rows name the keyboard's own keys.

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

## Player-facing polish and the controller guide (2026-11-04)

The release checklist's "Menus and screens" and "First minutes" items (docs/release-checklist.md section 4).

**Words.** No developer speak on any page: "Halcyon's ladder" is "Set sizes from 2048 to 6144 pixels wide per eye";
Advanced is "Fine-tuning and troubleshooting"; Reset is "Reset VR settings" (back to standard, what's kept named); the
classic menu is "Classic VR menu", "the original MELE-VR settings menu"; "explore pose" is "the relaxed walking pose"
(and the row says Experimental); VR mode explains each chip and says Mono and Depth aren't tested with hand aiming; the
Quest row is "Quest Link image fix" (what it fixes, that it does nothing through SteamVR or Virtual Desktop); the desktop
mirror's chips are Every 8th / Every 2nd / Every frame (the schema's default is the deploy's Every 2nd); Record says what
the file is for and where it goes (the report zip doesn't take recordings: attach by hand); the frame timing note names
`AppData\Local\MEVR\perf`. The original stereo's Eye distance reads in centimetres (both eyes apart, 2 × `sfr2HalfEyeUU`
game centimetres at life size; the user's 3.4 reads 6.8 cm) and both its rows sit under an "Expert" line, each with a line
of what it does. Walk toward's Body and Advanced's Look speed keep their behaviour (the owner decides) with an honest
warning each. Slider rows can carry a description now (`.srow.d`: the name and a line in a 330 dp column).

**One control per setting.**
- `leanGain` (until 2026-11-06; now always 2, no row): Comfort's Life-size movement (2 / 1) was the player's; Advanced > View's slider adds the range (0.5-3) and is
  "Movement scale", saying 2.0× is Life-size movement. A gain neither 1 nor 2 shows a note under Life-size movement.
- `hudPanelWidthM`: one Size slider on HUD & sights, its range widened to 45-155% (0.6-2.0 m, what Advanced's Width
  allowed); Below your eyes, Comes round past and the Place button moved there too, so Advanced's HUD panel group is gone.
- Conversations and cutscenes: Comfort's one "Watch them" row (which set both) and Advanced's two "in VR" toggles are now
  two Comfort rows, Conversations and Cutscenes, each In VR / 3D screen / Flat screen.
- Advanced > Stereo's "Life-size stereo" is "Real eye positions" (it was too close to "Life-size movement").
- The sights: "Shotgun and assault rifle" (whose Scope 1.5× was the rifle's only) is two rows; the shotgun has no scope chip.

**Game UI** (the classic menu's pointer "Move and resize a screen ... for now" is gone). Advanced's "HUD placement" tab is
"Game UI": the conversation wheel's and the mission overlays' size and place, the values the classic HUD tab's sliders edit
(table above). The old "Combat HUD" toggle (`pchudEnabled`) is gone: it switched Halcyon's resize of the HUD movie, which
the HUD panel keeps at its authored layout anyway, so it did nothing visible (and its "Health, shields and squad status"
read as if it hid them). [verify] in the headset: the conversation wheel's sliders on the 3D cinema screen (the transform
should apply there, as in VR cine: the wheel is in the eye images), and whether the mission overlays update while our menu
pauses the game (the row says "the next time an overlay is on screen").

**Rows for what was file-only.** Each a toggle with a line on what it does, written through `Settings::Put` and read with the
mod's default (on), its section added to `Settings::Read`'s list (a missing one made the Hacking row snap back, f1f8d8b):
`[Mako] controls`, `[Cockpit] enabled` (Controls > Mako); `[WorldUi] on`, `[Galaxy] hologram` (Comfort > Game screens;
the Hacking row greys out with the screens off); `[Hud] wristStatus`, `gunStatus`, `[Interact] label` (HUD & sights > On
you and in the world); `[Vent] enabled`, `[Hands] fingers` (Controls); `[Sights] scopeCapturePost` (HUD & sights > Sights:
"Scope picture in the game's colours", from the next loading screen, because `game/ScopeCapture` sets it on the capture
actor it spawns once per level).

**The controller guide.** A rail section of its own, first in the list ("Controller guide", icon `guide`: a question mark
in a circle): both controllers drawn with RCSS boxes (the body, the trigger, the grip on the fingers' side, the stick, two
face buttons, the left menu button) with a leader line from each control to a label of what it does on foot, a list under
each drawing saying how, then Gestures (holsters, grenades, melee, both hands, vent, crouch, this menu and recenter) and In
the Mako (lever, stick, before you grab them, getting out). The labels and lines are `core/vr/MenuSettings` `GuideTexts`
(unit-tested), computed from the settings that change them (the left trigger's choice and the squad, the holsters and
`gripHolds`, `[Holsters] powerWheel` (R3 / Y / X / B), the grenade hand and the X and B buttons, the vent, the turn and walk
chips, physical crouch); the gesture and Mako rows follow their toggles. Every mapping is from game/GamepadMapping, the
LE1 bullets in CLAUDE.md and research/19 section 6 (the on-foot bindings), research/46 section 3 (the Mako's): notably the
grenade is the RIGHT hand by default (`[Throwables] hands = right`, at the left of the chest), A is use and hold-to-sprint
(`Console_Action`), Y is medi-gel, the left menu button is the game's Start only where the controller sends one. Button
names are Touch's, with a line saying A and X are the lower face buttons.

**First run.** A player who has never had the guide (`[Menu] controlsShown` absent or 0) gets it once: `main_menu::Layer`
counts 6 s in a row on foot (`Frame::gameMode` 0, HalcyonOverlays' last game mode) in the head-tracked world with the
session focused, Halcyon's menu and ours closed, no calibration running and the calibration's first-run prompt answered
(`calibration::PromptPending`: the controllers known, and calibrated, prompted before, or the prompt answered or timed
out), then opens the menu on the guide with a welcome line and writes `controlsShown = 1`. The player's own first opening
before that lands on the guide too (and counts). The menu's pause applies as for any opening. The calibration's prompt,
in turn, now waits while our menu is open (HalcyonOverlays passes our menu as `menuOpen`): its 8 s delay and 45 s timeout
used to run on under our menu, where it isn't drawn. [verify] in the headset: the moment it opens (a new save's first
on-foot seconds), that nothing else is up then.

## Checking it offline

`mevr-menu-preview [folder] [--warp]` (`tools/menu-preview`): the host's own RmlUi setup, renderer,
decorators, document, data model and backdrop passes on a device of its own, over a stand-in scene,
with sample values. It writes `section-<name>.png` for each section (and `section-display-quality.png` /
`-quality-novr.png`: the image quality rows with the PS VR2's 2824 × 2880 known, and before VR starts; `section-<name>-2`
to `-4` for the guide's, Comfort's, Controls' and HUD's lower screenfuls; `section-guide-first-run.png` and
`section-guide-other.png`, the guide with other settings), `advanced-<tab>.png` for each of
Advanced's tabs, `state-hover.png` (the sweep part-way), `state-pressed.png` (the dip), `state-focus.png`
(the pad's ring), `menu-alpha.png` (the texture alone) and `timing.txt`.

## Open

- **[verify]** In the headset: the glass's look (the grade's numbers are a judgement call), the text size
  at 1.3 m (16 px descriptions are about 0.75 degrees), the pointer's feel, the pause.
- The cylinder layer (above).
- The rows marked Soon: built 2026-10-23 ([31](31-comfort.md)).
- Keyboard and mouse navigation of the menu itself (the desktop): not built (the game's mouse and
  keyboard are blocked while it's open; `\` closes it).
