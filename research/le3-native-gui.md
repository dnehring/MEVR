<!-- SPDX-License-Identifier: MIT -->
# ME3 native GUI adapter (L3-4)

Status: source and ABI metadata verified; shared input tests and per-game object compilation passed. Native execution and headset acceptance remain open. This increment does not enable the global LE3 module gate or claim a working wrist crop.

## Reuse and native differences

The host remains the shared `MenusLe2`, `MainMenu`, `OmniToolRail`, `GamePointer`, calibration and `WorldPanels` implementation. The native request schedule, bounded page switching, settle delay, and pointer queue remain `game/GameMenuRequest`, `game/GuiCursor` and `core/vr/GuiEvents`. `game/Le3Gui` is a small manager/focus/browser accessor, not another menu implementation.

Read-only inspection used the existing private conversation scanner over the installed LE3 `SFXGame.pcc`, plus the LE3 LExSDKv2 class, property and parameter metadata. The scanner and raw script output remain local and ignored. The private scanner's historical `--game LE2` selector maps to LE3; do not use that selector with the ordinary LE2 scanner. No game code or extracted UI assets are included here.

LE3 replaces `Engine.BioSFManager` and `BioSFPanel` with `SFXGUIInteraction` and `SFXGUIMovie`. The player controller has no LE2 cached Scaleform-manager field. These are the verified public contracts:

| Native contract | Adapter use |
|---|---|
| `BioPlayerController.GetSFXUIController() -> SFXGUIInteraction` | Obtain the player's manager without a guessed field offset. |
| `SFXGUIInteraction.GetFocusMovieForPlayerController(PlayerController) -> SFXGUIMovie` | Observe the actual focused movie; read its reflected `nmTag`. |
| `SFXGUIInteraction.GetBrowserHandler(PlayerController) -> BioSFHandler_BrowserWheel` | Find the live browser, including the PC variant. |
| `SFXGameModeBase.ShowMenu()` | Respect the active mode's pause-menu permission and native implementation. |
| `SFXGameModeBase.GuiKey(BioDefine.BioGuiEvents)` | Native input dispatch through `SFXGUIInteraction.HandleInputEvent`, not an invented dialogue route. |
| `BioSFHandler_BrowserWheel.lstPages`, `ShowSelectedPanel(int, optional SFXGUIInteraction)` | Resolve a live page by type/tag and use the game's own transition. |
| `BioWorldInfo.m_lstBrowserAlerts[11]`, `SubPageStateOverrides` | Preserve disabled-page restrictions, including map overrides being ignored by native initialization. Unknown state refuses the request. |

The native parameter metadata identifies `GuiKey.Event` as one byte at offset 0; the player argument of the focus/browser accessors is at 0 and the returned object at 8; `ShowSelectedPanel.nPanel` is a 32-bit integer at 0 with `oManager` at 8. Our implementation uses named, size-checked reflection parameters, not those offsets.

The shared queue retains LE1 event values. Translation happens only before native dispatch: ME3 motion 6, wheel 8/9, left press/release 31/52, d-pad down/left/right/up 11..14 and releases 32..35. Back is 16 in ME3. Developer `mevr guikey` numbers are already native and are not translated. An offline test sends a complete move/click/release/scroll sequence through the existing queue and scheduler, then checks the resulting ME3 sequence and every d-pad mapping.

Native GUI mode is 9, not 7. Active mode class lookup now uses the shared semantic adapter, also correcting LE2's vehicle/power-wheel mismatch. The wrist tap gate likewise uses semantic on-foot/GUI contexts; mode 7 in ME3 is a conversation and must not open the rail. Authored seated scenes continue to reject traversal; first-person world conversations remain deferred.

## Ownership and restrictions

The existing schedule opens the native browser first, waits for its focus, and selects squad (page type 2), journal (4), map (0), or a codex-tagged page if the live browser has one. There is no assumed fixed array index. If a build/configuration has no independent codex page, that request reports no page; it does not synthesize a direct handler call. Weapon/armour changes remain at native stations; this increment does not enable ME3 holster commands.

`ShowSelectedPanel` removes the parent browser movie when replacing it. LE2's workaround that hides stacked browser panels must not be applied to LE3. A request made from another child first backs out to the browser through native B. Reopening our menu cancels an already-started page transition. Existing retry limits stop missing/refused pages from looping forever.

`GuiCursor` polls the host native queue only when the real native mode, focused movie and `GuiKey` parameter contract are available. Polling itself claims native input ownership in `GamePointer`, so a missing consumer must preserve Win32 fallback. On focus changes it drops queued input belonging to the previous movie. The game tick now passes main-menu and guided-calibration ownership explicitly, independently of pause settings. On takeover, it discards pending actions and developer input, sends only cleanup releases for successfully delivered presses, and drains the host queue once before resuming. It does not poll the native queue while the competing UI owns input. Physical hover, held drag/release through focus loss, and SteamVR session recreation still require native acceptance.

## Crop and art gates

The already-integrated host places the complete uncropped native GUI frame in the room and maps pointer UVs against that whole frame. That is the only accepted source-to-panel geometry in this increment. It is not a cropped wrist projection.

(Superseded 2026-11-16 by "The game's screens on ME3" below, which took these gates in the game; kept as the record.)
`game/WorldUi` still expects LE1/LE2 panel lists, focus/full-screen flags, camera hold and HUD redirect ownership. LE3 uses `SFXGUIMovie.m_bRequiresUIWorld` and a different GFx rendering path. Do not publish `setWorldUi`, reserve a wrist opening, clear that native flag, or enable `MenusLe2::BeforePanel` capture ownership until the following are verified together:

1. Native movie UI draws captured into a separate texture, without a second native panel or stale background.
2. Actual stage/window aspect and per-page crop measurements, including ultrawide and controller/mouse variants. Existing LE1 `DefaultCrop` values are not LE3 evidence.
3. A verified saved on-foot view and correct render-only hold; squad UI-world figures must remain visible without moving the gameplay camera.
4. One input owner during opening, page switch, close, focus loss, recenter and XR session restart; no invisible clickable screen.

`game/GameArt` remains LE1-only: six specific movie paths and a shared `MEVR/gameart` cache. Do not broaden its preprocessor guard. A later native art port must inspect LE3 movie resources, use a per-game cache namespace and provenance, and preserve existing LE1 caches. Current shared rail styling/art is presentation reuse, not a claim that LE3-native art was extracted.

## Integration and acceptance

CMake links `Le3Gui.cpp` only into the LE3 mod and reuses one sequel host menu source bundle. No new runtime services are enabled by these source files. The coordinator owns `ModLoader`, `Entry`, `TestLoop`, reflection-layout acceptance and game access.

Once the LE3 engine probe accepts reflection, wire the existing `game_menu_request::Tick` / `gui_cursor::Tick` and their diagnostic commands in the bounded candidate path. Keep native GUI input publication gated until readiness is verified. Keep `world_ui` capture/reserve services absent. Preserve the existing full native-panel fallback.

Offline evidence: MSVC Release `/W4 /WX` object compilation of `GameMenuRequest.cpp` and `GuiCursor.cpp` under LE1, LE2 and LE3; LE3 also compiles `Le3Gui.cpp`. The GUI queue/scheduler regression executable exits 0. Whole-host linkage is a coordinator check, not established by these object builds.

Native acceptance must use the existing runner, request files and snapshot flow: open/close browser, switch squad/journal/map, verify unavailable map and story-disabled pages remain refused, exercise any actual codex entry, move/click/scroll/drag, switch movie during a held press, reopen our menu during a pending request, then lose focus/recenter/recreate XR. Record focus tags, page enumeration, native event counters and stereo snapshots. A real headset pass must show the eventual wrist projection meeting the rendered Omni-tool; an input counter or successful ProcessEvent call is insufficient.

## The omni-tool on ME3 (lane OM, 2026-11-16)

Run on the fake headset, renderer preview, Save 31 (the Normandy), `tools/le2-halcyon-run` with holder `OM`; restored with
0 differences. `reflect: 731 of 731 checks agree` with the rows below.

**The pointer's clicks are ME3's own GUI events.** `takeGuiEvents` is served on ME3 (`plugin/ModLoader.cpp` `WithMenus`, both
sequels now; `game/GuiCursor` polls it only while the native `GuiKey` contract is ready, as above). With the Mission
Computer up (`mevr omni computer`) a replayed right ray (`make-galaxy-pick-recording.py --snapshot <json> yes out
--panel-uv 0.40 0.50 --layer quad0`, `replay.ps1 -Inputs`) on its Squad button: `game pointer: the trigger clicked the game's
menu at (1228, 863) px (native GUI events)`, `gui cursor: ... (SFXGameModeGUI.GuiKey(31) ...)`, and the focused movie went
from `MenuBrowser` to `SquadRecord` (`mevr guimouse`). No SendInput line in the log.

**ME3's screens act on B's release.** `GuiKey(16)` (B) alone left the squad record up; `GuiKey(37)`
(BIOGUI_EVENT_BUTTON_B_RELEASE, LExSDKv2's LE3 enum) went back to the Mission Computer, and B with its release again closed
it (game mode 0). `game/GameMenuRequest` sends both for every back-out on ME3. Then `mevr omni squad`, `journal` (returning
from `SquadRecord` to the browser first) and `close` (two back-outs, `the game's screens are closed (game mode 0)`) all ran.

**The gauntlet.** ME3 has no crust template: Shepard's omni-tool is one of ME3's client effects, which its own scripts start
(`SFXCustomAction_OmniWave.StartCustomAction`: `Class'RvrClientEffectManager'.static.GetClientEffectManager().Start(CE_OmniTool,
m_oPawn)`, ended with `Stop(effect, id, bAllowCooldown)`), default `RvrClientEffectMulti
BioVFX_T_TechPowers._OmniTool.VCFX.OmniTool_LeftFull_VCFX_M` in SFXGame.pcc (always loaded): `OmniArm_Loop` at
`LeftElbowTwist1` and `OmniHand_Loop`, looping, attached to the bones by the effect, faded by its own modules. Read offline
with Legendary Explorer's library over the installed packages (scratch only). `game/OmniTool`'s ME3 adapter does the same
(`RvrClientEffectInterface` is a class on ME3, so `pEffect` is a plain object parameter). Its particle systems live on pooled
`RvrClientEffectSpawnable_N` actors, so the display is the `OmniHand_Loop` component nearest Shepard. In the game: the
wrist-tap replay (`make-wrist-recording.py tap.mevrrec 40 4 0 wrist`) opened the rail and `world ui: the omni-tool's rail is
open on foot`, `omni tool: on Shepard's arm (... started by RvrClientEffectManager ...)`; `mevr omnitool on|off|display`
(`the display at (-2262.5, 6471.2, 91.4)`, 27 units from Shepard); the snapshot shows ME3's orange holographic gauntlet on
Shepard's left forearm (third person: the preview has first person off).

**The rail in ME3's look.** ME3 never draws ME1's cached art; it activates the `me3` theme of `assets/menu/omnitool.rcss`:
ME3's Mission Computer colours sampled from the game's own frame (buttons #04567F into #04213C, edges #12A8D4 / #0591C5,
type #DEF5FE, navy #091024 into #040819; the selected entry red #3E0516 into #960C0D, edge #B60504, gold type #FFCA65) on the
rail's own shapes and fonts. Offline: `mevr-menu-preview <out> --le3`. Why not ME3's own art: its menus' frames and buttons
are bitmap fills of textures outside the movies (core/gfx skips bitmap fills: MainWheel's buttons render empty), and its font
library (`SFXGUI_Fonts.pcc`, `GUI_SF_Fonts.gfxfonts`: Biomass and Myriad Pro) is not a live object in the running game (no
object holding "gfxfont" or "gui_sf_fonts"; `DynamicLoadObject` of it fails), so game/GameArt stays LE1's. ME3's journal and
codex panels are vector art core/gfx does draw (`GUI_SF_Jourdex`, sprite 196): a later port could take the frame from there.

**Measured for the wrist (gates 2 and 3).** Focused movie tags: `MenuBrowser` (the Mission Computer, a 1280 x 720 stage
filling the 16:9 frame; its buttons within about x 0.26..0.74, y 0.33..0.67 of it, the title above, the credits and the
bottom bar below), `SquadRecord` (Shepard's 3D model from the UI world behind the powers: gate 3 applies, it keeps the native
panel until the UI-world figure is ported), `Journal`, and the map page. `SFXGUIInteraction.GetAllMovies` returned no open
movies through ProcessEvent (its out arrays stayed empty), so a census of the stack needs another route.
`SFXGUIMovie.m_bRequiresUIWorld` is the flag to read per movie (LExSDKv2 and our layer agree on it).

**What task 2 (screens on the wrist) needs, in order:** lane HU's ME3 UI redirect to the HUD panel (`caps.hudPanelGameplay`;
its arming is the shared `hud_panel` frame, `panel.gameplay || ctx.worldUiOwns`); `caps.worldUi` on LE3; an ME3 seam in
`d3d_capture.cpp`'s menu-presentation decision so `MENU_MONO` is not entered while the world UI owns the GUI (EmitUiDraw
draws flat whenever `GetMenuMode()` is true); ME3's GUI mode 9 in `vr::world_ui::ScreenMode` and `game/WorldUi`'s mode
checks (raw 7/10 today); a `game/WorldUi` ME3 adapter for the open movies (focus via `le3_gui::FocusMovie`, tags above,
`m_bRequiresUIWorld` movies kept on the native panel); then each screen's crop measured on the panel image.

## The game's screens on ME3 (lane OM2, 2026-11-16)

ME1's world UI (`game/WorldUi`, `plugin/WorldUi`, `core/vr/WorldUi`, `plugin/MenusLe2`'s panel glue) now serves ME3: the
Mission Computer and its pages project from the left wrist, the War Terminal's War Assets stands in the room toward the
terminal, the squad record (its 3D Shepard) keeps Halcyon's native panel. Run on the fake headset, renderer preview, Save 31,
holder `OM2`, sessions `20261116-*-LE3-om2-me3*` (the last two, `om2-me3f` and `om2-me3g`, are the evidence); restored with
`verification: 0 differences`; `reflect: ... checks agree` with the new rows.

**What was built (the steps of "The omni-tool on ME3" above):**
1. `plugin/RendererLe3.cpp`: `caps.worldUi = true`; `plugin/ModLoader.cpp`'s `WithMenus` serves `setWorldUi`,
   `reserveWorldUi`, `logWorldUiStats` and `takeGameUiRequest` (the rail's origin) on LE3 as on LE2.
2. The seam: `MevrSeams::WorldUiOwnsGui(mode)` (moved from ME2's `mevr_bridge.h`, whose name now forwards; defined in
   `plugin/MenusLe2.cpp`). ME3's `d3d_capture.cpp` `UpdatePresentationState`: `demand = manual ? Manual : (menuInputOwner &&
   !worldUiOwns) ? InputOwner : galaxy ? Galaxy : Gameplay` (`[MEVR]`), so the GUI mode stays in stereo while a screen of ours
   owns it; the galaxy's demand is untouched.
3. `core/vr/WorldUi.hpp`: the modes per game (`kGuiMode` 9 on ME3, 7 on ME1 and ME2; ME1's minigame 10 only on ME1, where
   ME2's 10 is the planet scan; `PlayMode`: an opening is reserved from modes 0..4, ME3's 0..6), used by `Opening::Eligible`,
   `OpeningSource::Observe`, the host's `Ownership`/`HoldFrame`, `game/WorldUi`'s `BeginOpening` and `WristTrackingFrame`.
   ME1's numbers without a game define (the tests).
4. `game/WorldUi`'s ME3 adapter: the screen is the focused `SFXGUIMovie` (`le3_gui::FocusMovie`; `GetAllMovies` returns none
   through ProcessEvent), its `nmTag` through `FromMe3Tag` (`SquadRecord` is ME1's `CharacterRecord`, `Areamap` its `Map`,
   `warAssets` ME3's own new `WarAssets`, which stands by what was used); a message box stands on the last screen of ours in
   focus this GUI stretch (ME1's rule over its parent).
5. **The UI world.** Every Mission Computer page and War Assets set `m_bRequiresUIWorld` (SFXGUIInteraction's script calls
   `SetRequiresUIWorld(TRUE)` as it opens them), and while one does the game renders the UI world's dark nebula instead of
   the level: the first wrist screens stood in a dark void (snapshot `2026-11-16_23-51-21-086`). ME1 has the same in
   `bFullScreen`. So while ours stand, a screen without a squad member in it is told `SFXGUIMovie.SetRequiresUIWorld(false)`
   (the game's native setter, never a raw write) and `SetRequiresUIWorld(true)` again when ours goes down (if the movie still
   lives); the level then renders behind (`2026-11-17_00-01-16-685`: the Normandy's floor, the gauntlet, the Mission Computer
   on the wrist; `00-03-11-853`: War Assets over the War Room). A screen whose squad member is in the UI world
   (`ShowsFigure`: the squad record) keeps the native panel: `world ui: MenuBrowser down (it shows the game's UI world ...)`.
6. Crops: ME3's movies fill the 16:9 window (1280 x 720 stages; the panel image is the whole window at 3072 x 1728), so every
   screen shows its whole stage (`DefaultCrop`'s default, as ME1's journal, codex and map): the Mission Computer and the map
   0.70 x 0.39 m on the wrist, War Assets 1.20 x 0.68 m in the room. No tighter crop was measured worth taking.
7. `game/GameMenuRequest` on ME3: back-outs 12 ticks apart, at most 8 (the map page took about a second to answer B; six
   back-outs 4 ticks apart left it up). Superseded by lane OMX's "Close from the area map" below: each B restarted the map's
   close, and a back-out on ME3 now waits for each screen to go.

**In the game (Save 31):** a replayed wrist tap (`make-wrist-recording.py ... 4 0 wrist`) opened the rail, standing on the
omni-tool's display; a fingertip press on its Squad entry 6 s into `make-rail-press-recording.py` (the rail rides Shepard's
drawn display, which the recording's rising wrist moves: a press at 2 s missed) opened the Mission Computer and its squad
record (`picks 1, last squad`; the record on the native panel); the player's B (replayed with `-Inputs`) went back to the
Mission Computer, projected from the wrist with the rail beside it and the fan of light; `mevr omni map` opened the map on
the wrist; Close returned to game mode 0. At the War Room table (walked as lane HU did, head 30 degrees left and down) A
opened War Assets (`warAssets`): `the WarAssets screen opened by using SFXPointOfInterest_0`, `stands in the room ... toward
what it belongs to`; B closed it; the War Terminal's label was back at the console in first person.

**ME1 unchanged** (its numbers are the code's without a game define; the shared files compile per game): the same-pose pair
`2026-11-17_00-49-15-om2-le1-before` (main f7ef8b88's players' build) and `00-52-12-om2-le1-after` (this lane's): the paused
pose pixel for pixel (both eyes, their world layers, the HUD panel, the label and wrist pieces), the same `hostView` and
layers; then the journal on the wrist (`mevr omni journal`, the world running): the same screen, stack and placement and the
HUD panel's image pixel for pixel; only the screen's place moved with the omni-tool display's drawn position (the world
unpaused), as between any two runs. `mevr-menu-preview`'s 91 PNGs identical before and after.

**ME2 unchanged:** Save 4 (session `20261117-*-LE2-om2-me2b`, restored with 0 differences): `mevr omni squad` and `codex`
projected ME2's Mission Computer, its squad page and the codex from the wrist (placement 1, the fan of light and the rail
beside them), Close back to gameplay. (A first try's `load.ps1` refused: the mod appended its first-run `[Section]` defaults
to `LE2MEVR.ini` while the load ran, `Save or runtime artifact changed during load`; waiting 45 s at the title first avoids
it.)

**Not right yet (for the next lane):**
- **The pointer's clicks don't reach ME3's movie mouse on a world screen.** The ray's and the fingertip's clicks land on the
  right pixel and go in as ME3's GUI events (`game pointer: the trigger clicked the game's menu at (1213, 743) px (native GUI
  events)`, the Mission Computer's Journal button), but `mevr guimouse` shows the movie's own mouse unmoved at (1130, 600) of
  its 1280 x 720 stage, so the page didn't open (three tries: two touches, one ray). `plugin/GamePointer`'s mod-steered path
  posts no window messages and answers `GetCursorPos`; ME3's viewport may take the mouse from window messages instead
  [verify]. The rail's entries and `mevr omni` do open the pages.
- **Behind a screen the eye is the GUI camera's, not first person's.** Halcyon ME3's first person writes only in game mode 0
  (`engine_probe.cpp`'s `fpWriteContext`), so in the GUI mode the camera returns to the game's own offset and Shepard's body
  fills part of the view (`00-03-11-853`). Allowing mode 9 there while `WorldUiOwnsGui` stalled the game thread for seconds
  (run `om2-me3g`: snapshots 30 s late, the presentation flipping MENU_MONO/stereo, B ignored) and was reverted. ME1 holds
  Shepard's on-foot view at render time (`MevrBridge::GalaxyViewSwapBegin`'s world-UI branch, `State::holdRender`); ME3 has
  no such seam yet.
- The galaxy console was not reached from Save 31 (the CIC is beyond the doors the earlier navigation runs stopped at); the
  galaxy map's own path is untouched by this change (its demand and modes 11/12 aren't the world UI's).

(The first two are fixed by lane OM3 below.)

## The movie's mouse and the view behind a screen (lane OM3, 2026-11-17)

Run on the fake headset, renderer preview, Save 31, holder `OM3` (session `20261117-*-LE3-om3-me3*`), scripts after lane OM2's
(`me3-run.ps1`: `mevr omni computer`, a ray or fingertip replay on the wrist screen, `mevr guimouse` while it hovers and after the
press).

### The movie's mouse

**Why the clicks didn't take.** `SFXGameModeBase.GuiKey(Event)` is `SFXGUIInteraction.HandleInputEvent(ControllerId, Event)`,
native (exe+0xB35F00, its body 0xBAEA60 on the interaction's `InputHandler`, `SFXGUIInteraction` +0x430). It hands the event to
the focused movie's script `HandleInputEvent` (the Mission Computer's `BioSFHandler_BrowserWheel` takes only the left stick and
returns false for the rest) and then gives a mouse event (moves 6/7, wheel 8/9, left 31/52, right 30/51) to the movie's GFx
view only if the movie's `m_bMouseVisibleWhenFocused` (`SFXGUIMovie` +0x2E8, bit 0x400) is set and a virtual check of its own
(vtable +0x410; a `return 0` stub on these movies) passes. The mouse step itself (0xBB47A0) reads the viewport's mouse
(`FViewport::GetMouseX/Y`: `GetCursorPos` and `ScreenToClient`, which the host answers with the pointer's point) and sends GFx
the event at that point, unless an in-world render-to-texture movie took it first. `m_bMouseVisibleWhenFocused` follows the last
input device: after any pad input (the motion controllers are Xbox pad 0: B, a stick) the game is in its pad mode
(`SFXGUIMovie.bUsingGamepad` 1, `m_bMouseVisibleWhenFocused` 0) and every mouse event GuiKey sent was dropped before the mouse
step, so the movie's mouse stayed where the real cursor had left it (`mevr guimouse` read (1391, 450) of the 1280 x 720 stage
through the whole A/B below, which followed a B; lane OM2's Journal tries also followed a replayed B). A screen opened by
`mevr omni` with no pad input since was in its mouse mode (1 and 0), and a GuiKey click flips it there too, but only after the
press it carried had been dropped. GuiKey's key path doesn't carry the mouse either: its table maps no key to the left button
(0x13A7390: 11..29 only).

**The fix (`game/GuiCursor`, ME3 only).** `SFXGUIMovie.SendMouseEvent(nEventID)` (native exe+0xB67B00, body 0xBC9BF0) goes to
the same mouse step for the focused movie without that gate; ME3's own scripts use it (the galaxy map's wheel in
`BioSFHandler_PCGalaxyMap.HandleInputEvent`, the power wheel's release in `SFXSFHandler_PCPowerWheel`). The pointer's moves,
clicks and wheel now go to the focused movie that way (the pad's events, the d-pad and B, still through GuiKey); a call that
can't be made falls back to GuiKey, never both for one event. `mevr guimouse route movie|guikey` switches it for an A/B,
`mevr guimouse send <event>` calls it by hand; `mevr guimouse` prints the movie's mouse, `m_bMouseVisibleWhenFocused`,
`bUsingGamepad` and the movie's vtable slots, and reads ME3's own movies (War Assets) through `GFxMovie.GetVariableNumber`.
ReflectCheck rows: `SendMouseEvent` and its block, the two flags, `GetVariableNumber`. Log `gui cursor: ME3's movie takes the
pointer's mouse through its own SendMouseEvent`.

**In the game (session `om3-me3a`):** the Mission Computer on the wrist; a ray from 15 cm on its Journal button (stage 0.395,
0.43): while it hovered, the movie's mouse was at (505.4, 309.6), the button's place; the trigger opened the Journal (focus
`Journal`, `world ui: Journal takes over from MenuBrowser`). B back to the Mission Computer, then a replayed fingertip press on
Journal: the Journal again. The same ray with `mevr guimouse route guikey`: the movie's mouse unmoved at (1391.2, 449.6), the
Mission Computer stayed (the bug as lane OM2 found it). After a pointer click the game counts the mouse as the input device
(`bUsingGamepad` 0, `m_bMouseVisibleWhenFocused` 1); the player's B puts it back in its pad mode.

### Shepard's view held behind a screen

ME1's way, shared now: `plugin/WorldUi`'s `HoldViewBegin/End` puts the view game/WorldUi saved on foot (`State::holdRender`,
the POV's addresses) into the player camera's `CameraCache.POV` for the main view's build only and puts the camera's own back
after it (journalled `WorldView.*`). LE1's `GalaxyViewSwapBegin` calls it for its world-UI branch (its galaxy-map and
conversation branches are unchanged), ME3's renderer through two new common seams (`src/halcyon/MevrSeams.h`):
`WorldViewHoldBegin/End` around the primary player's view in `calcview_hook.cpp`'s `CalcViewDetour` (after the flat menu and
cinematic branch, before the FOV-at-source guard, so it is restored after that guard's restore; both stereo passes), and
`WorldViewHeld(mode)` in `engine_probe.cpp`'s `ApplyFirstPerson`: 1 while the hold is in force for a wrist screen, when the
first person's mesh hides stay as gameplay last wanted them (Shepard's head hidden at the held eyes; arm IK keeps the arm and
body under the view, as on foot) instead of the head coming back outside mode 0; 2 for a screen standing in the room, when the
body is hidden too. The second came from War Assets: in the GUI mode the body follower and arm IK stand down (no head-tracked
view, the view anchor held only for wrist screens, as on ME1), the body turns back to the pawn's facing (30 degrees from the
head's in the run) and the game puts Shepard's hands on the War Terminal, so his dark sleeve and shoulder covered the right half
of the screen from the held eyes (`2026-11-17_01-43-56-080`). No camera mode writes in the GUI mode (OM2's stalled try let first
person write there); the only engine writes are the POV swap on the game thread and the `SetOwnerNoSee` calls first person
already makes on foot (queued to the game thread by `MevrCallProcessEvent`). Logs `world ui: the view held at Shepard's ...
(the game's screen keeps its own camera, N units from the held eyes)`, the stats line's `views held at Shepard's`, and
Halcyon's `[MEVR-HELDVIEW] gm 9: a screen of ours on the wrist ... hides kept` / `in the room ... his head and body hidden` /
`the held view ended`. ME1's renderer doesn't ask: whether ME1's room screens (the shop, loot) show the body the same way when
the game poses Shepard for them is [verify]; nothing there was changed.

**In the game:** the Mission Computer's GUI camera stood 32 units straight below the held eyes (`(+0, +0, -32)`: inside
Shepard's chest); with the hold the eyes are where they were on foot: the snapshot with the Mission Computer on the wrist shows
the same corridor and floor from the same place as the one on foot before it, Shepard's forearm with the omni-tool's glow and
no head or body in front (`2026-11-17_01-39-56-413` on foot, `01-40-10-765` with the screen; again in session `om3-me3c`:
`01-49-21-353`, `01-49-34-813`). About 60 held builds a second (both stereo passes; the stats line counted 104 to 1,924 over
the first 30 s). War Assets in the room (session `om3-me3c`, `01-52-14-382`): the table, the hologram and the whole screen
clear, no sleeve or shoulder; its entries took the ray's clicks (Alien opened its list, then Shadow Broker Support Team was
selected; `01-52-33-715`, `01-52-53-054`), B back to gameplay after closing the opened list first (two presses here, three in session `om3-me3b`: the game's own back-out, not ours). No `off the game
thread`, stall or fault lines in either run; ME3 restored with `verification: 0 differences`.

### ME1 unchanged

LE1 compiles the hold's move (its `GalaxyViewSwapBegin` world-UI branch now calls `plugin/WorldUi`'s `HoldViewBegin`; its galaxy
map and conversation branches untouched) and `game/GuiCursor` (the ME3 route is compiled out). The same-pose pair on the fake
headset, lock held by hand, main 00912777's players' build against this lane's: sessions `2026-11-17_02-02-23-om3-le1-before`
and `02-05-33-om3-le1-after`. The paused pose pixel for pixel (both eyes, their world layers, the HUD panel, the label and wrist
pieces), the same `hostView` and layers. The journal on the wrist (`mevr omni journal`, the world running): the same screen, stack,
placement and held view, the world layers pixel for pixel, the screen's place within 1 mm (it rides the omni-tool's drawn display),
the HUD panel's image differing only in the journal's pulsing new entries (at most 12 levels). A ray and trigger on its
Assignments tab: in the after run it landed on the tab, (1701, 1244) px of the 3840 x 3840 window, and the journal switched to its
assignments; in the before run the screen hadn't settled where the snapshot it was aimed from had it (riding the drawn wrist) and
the click landed above the tabs, at (2339, 1028), changing nothing, which is the replay's timing, not the code: ME1's pointer path
is unchanged (the mod's GUI events, `SFXGameModeGUI.GuiKey(50)`, the host's `GetCursorPos` answer).

### ME2's game mode 10 after lane OM2's per-game modes

Lane OM2's `core/vr/WorldUi.hpp` made ME1's bypass-minigame mode (10) a screen mode on ME1 only. ME2's mode 10 is
`GameMode_Orbital`, the planet scan (`SFXGameModeManager.EGameModes` in LExSDKv2's LE2 headers: 9 Galaxy, 10 Orbital, 11 Photo,
12 CheatMenu; `engine/GameAdapter.hpp`'s LE2 table says the same). Nothing on ME2 relied on 10 being a world-UI screen mode:
- Halcyon's ME2 renderer always shows modes 8, 9 and 10 flat (`d3d_capture.cpp` `GetMenuMode`: "Movie / Galaxy / Orbital -
  genuinely flat") and asks `WorldUiOwnsGui` only for mode 7, so the presentation and its hold never looked at our state in 10.
- The only users of `ScreenMode` on ME2 are game/WorldUi's tick and `WristTrackingFrame`, and plugin/MenusLe2's `BeforePanel`.
  Before OM2, ME2's tick in mode 10 read the open panels and could have claimed a known screen (a message box over the scan):
  `BeforePanel` would then have taken the game's UI to our panel while the renderer showed the mode flat, the scan's UI gone
  from its flat panel. OM2's rule removed that latent conflict; the HUD panel's arming (`panel.gameplay || worldUiOwns`), the
  wrist tap's gate (semantic on-foot and GUI contexts) and the GUI cursor's readiness (`Gui` or `Minigame` contexts: ME2's 10 is
  `Orbital`) never counted 10 as a screen mode.
So nothing is restored for ME2. Not run: the planet scan is reached from the Normandy's galaxy map, not from Save 4 (Freedom's
Progress); the runner's other ME2 saves weren't needed for a mode no ME2 code path reads as a screen mode.

## Room changes, the UI world coming back, the first frame and the omni-tool's display (lane FX3, 2026-11-17)

An adversarial review of main 57ae5b4e named five defects in ME3's wrist screens; each was confirmed in the code first.

**1. Calls on the old room's movies.** On a room or world change (`vr::world_view::Reference`: the world or the player
controller changed: a save load, a level change) `game/WorldUi`'s `Tick` went `Down`, whose `RestoreUiWorld` called
`SFXGUIMovie.SetRequiresUIWorld(true)` on every movie it had told not to ask for the UI world, from the previous room.
`ue3::runtime::IsAlive` only checks that the object's GObjects slot still holds that address, which a new object of another
class can take after the movie was freed, and `runtime::Call` doesn't check the class. `gLe3Parent` (the message box's
parent) was kept the same way. ME1's `RestoreFullScreen` (`bFullScreen` written back on the old room's panels) and ME2's
hidden browsers (`SetMovieVisibility`) had the same pattern. **Now:** a room change forgets them without a call or a write
(`ForgetScreenObjects`, before `Down`; log `world ui: forgot N screen(s) ... in the old room (not called again)`), and every
kept object is checked to still be one before anything is called on it or written to it (`StillMovie`: `SFXGame.SFXGUIMovie`;
`StillPanel`: `Engine.BioSFPanel`; ReflectCheck rows exist for both classes).

**2. The UI-world void coming back.** A movie already in `gUiWorldCleared` was skipped even when it read
`m_bRequiresUIWorld` true again, and ME3's script sets it as pages open (item 5 of "The game's screens on ME3"). **Now:** a
movie of ours that reads true again is told again (log `world ui: the <tag> screen asked for the game's UI world again (its
script, as a page opened): told again it doesn't`, once per movie; the snapshot's `worldUi.uiWorldClearedAgain` counts).

**3. The wrong camera for a frame at each opening.** `OnUpdateCamera` recorded where the player camera's POV lives only once a
screen of ours had been published (`gScreenUp`), and the state carries `holdRender` only with that address under 500 ms old,
so the first state of every opening held no view and the frame after it was drawn from the GUI mode's own camera (32 units
below the held eyes on the Mission Computer, inside Shepard's chest). LE1 hides the frames of a reserved opening with its
clean-eye guard (`MevrBridge::WorldUiHoldFrame` in `xr_session.cpp`: no new eye images until the screen is redirected);
ME3's renderer had no such seam. **Now:** the POV's place (the camera's own fields, the same on foot and behind a screen) is
recorded at every update of the player's camera, so the first state already holds the view (every game: on ME1 this removes
the same one frame of the screen's own camera after a game-started opening, the shop or a locker); and ME3's renderer asks
the clean-eye guard as LE1's does: the common seam `MevrSeams::WorldUiHoldFrame(mode)` (`plugin/WorldUi`'s `HoldFrame`, the
function LE1's own seam calls), in `me2_xr.cpp`'s SFR pair: while it holds, no copy into the eye images and the last pair is
submitted with its own tags (`EyePublication::TaggedPair`). Measures: the mod logs `world ui: <screen> (opening N) published
without the held view` (`mevr worldui firstheld off` brings back the old rule for an A/B), the host's stats line counts
`views behind a screen of ours not held` (a reserved opening before its first state) and `clean-eye holds`.

Not changed: an opening the game starts itself (Start, A at the War Terminal) is not reserved, so its first GUI-mode frame is
still `[PRESENTATION] MENU_MONO` until the mod's first state (one tick), on ME3 as on ME1, whose mode-7 decision is the same.
Holding the presentation for an opening nobody announced would need a rule for when to give up; not built.

**4. The Atlas and injured Shepard.** `vr::world_ui::PlayMode` admits ME3's modes 2 (Atlas) and 6 (injured), but the view on
foot is remembered only in mode 0, so a screen opened from them would have held a view from anywhere earlier in the level.
**Now:** `FootViewMode(mode)` (core/vr/WorldUi; a static_assert in the LE3 build) is false for those two; `game/WorldUi`
keeps the last play mode's answer, `BeginOpening` reserves nothing there and a screen opened there keeps the native panel
(`world ui: ... opened from a mode not on foot (the Atlas, injured Shepard): no view on foot is held`), as one opened before
any view on foot. ME1's model is the Mako's cockpit guard (`gCockpit`); the vehicle mode keeps it (ME1's Mako without the
cockpit still holds the view remembered on foot: unchanged, an owner's question).

**5. The omni-tool's display.** `game/OmniTool`'s ME3 adapter took the nearest `OmniHand_Loop` particle system within 250
units, which could be a pooled one (the effect's spawnables keep the template after they stop), a stopped one or a squadmate's.
**Now:** only the player's own live one (`NotOurs`): its owner a `RvrClientEffectActor` whose `m_pCEffectComponent` is
`m_bActive`, not `m_bStopRequested`, and was started for the player's pawn (`m_oTarget.Instigator`, the pawn
`RvrClientEffectManager.Start` was given); the passed-over candidates and why are in the `omni tool: its display is ...` line
(or `no display of the player's own near Shepard yet` after 2 s). `Stop`'s `bAllowCooldown` and the new fields have
ReflectCheck rows. The particle system's own `bWasDeactivated`/`bWasCompleted` are not read: their first rows disagreed
(ours 0x364, LExSDKv2's compiled struct 0x35C: its particle headers are 8 bytes early, as the `Template` comment in
`CheckLe3OmniTool` says), and a development build refuses ME3's gameplay hooks on any disagreeing row (`LE3 startup refused:
SDK reflection cross-check failed`, the first run of this lane). A struct member's row (`CheckPath`) sums the offsets, so the
SDK side of `m_oTarget.Instigator` is `offsetof(component, m_oTarget) + offsetof(target, Instigator)`.

**In the game** (Save 31, renderer preview, holder `FX3`, session `20261117-090142-899-LE3-fx3-me3`, script `mevr omni`
openings without tracked controllers, so the screens stood in the room; restored with `verification: 0 differences`. The
`build` tree was compiling during the second half of the run: the stall below is marked as taken under compiler load):
- `reflect: 789 of 789 checks agree`. (The first try refused ME3's gameplay hooks, `LE3 startup refused: SDK reflection
  cross-check failed`: the particle rows and the struct path above; fixed before this run.)
- `[MEVR-BASELINE] 4 setting(s) ... first person in cover (CoverThirdPerson), ...`: the preview's `MELE3VR.ini` asks for third
  person in cover and the baseline pins it off.
- `mevr cover where`: `cover snapping refused` with no controllers replayed (before FX3 it read `the game's` until the host's
  controls were fresh); `posture: script functions found` with no "cover prompt stays" note (the prompt's handler is in). The
  Normandy has no cover, so no prompt or refusal was seen.
- **The first state's hold, A/B:** with `mevr worldui firstheld off` the Mission Computer's opening logged `world ui:
  MenuBrowser (opening 160415985) published without the held view`; with the fix, none in the next five openings (the
  Mission Computer, the Journal over it, B back to it, a rail opening, the Mission Computer after a load). The host's stats:
  `clean-eye holds` 3 per reserved opening (11 in all), `views behind a screen of ours not held 0`, `flat-panel frames during
  reservation 0`. The Mission Computer stood with the corridor behind it at the held eyes (`2026-11-17_09-04-28-404`).
- **A load with a screen of ours up** (the Mission Computer, then `load.ps1` of Save 31): `world ui: forgot 4 screen(s) told
  not to ask for the UI world in the old room (not called again)`, `MenuBrowser down (player/world changed)`, no fault; the
  next Mission Computer opened as before and the omni-tool's display was the new pawn's (`its display is ...
  RvrClientEffectSpawnable_19 ... the player's own, live`; before the load `_9`, 36 units from Shepard).
- **The flag set again:** in this flow ME3 makes a new movie for each page and for B's return (the four forgotten were four
  objects), so the same movie was never asked again here. As a stand-in, `set SFXGUIMovie m_bRequiresUIWorld True` with the
  Mission Computer up: `world ui: the MenuBrowser screen asked for the game's UI world again (its script, as a page opened):
  told again it doesn't`. `set` changes every movie (and the class default), so the void it left behind
  (`2026-11-17_09-05-57-234`) came from the others; the picture can't judge this one, the log line can.
- Under the compiler load (09:05:42-50, the heartbeat at 17 then 1.8 fps) the mod's state went stale past the host's 400 ms
  and ME3 entered MENU_MONO twice (1.5 s and 4 s) until it came again: the existing freshness rule, not this change.

**ME1 unchanged where it should be** (the shared WorldUi, Posture and plugin/WorldUi code compile into LE1): the same-pose pair
on the fake headset, the lock held by hand, main 57ae5b4e's players' build (`le1-before`; ecd47552 changed no code LE1
compiles) against this lane's merged build (0.3.2+add433ba83): sessions `2026-11-17_09-06-59-fx3-le1-before` and
`09-10-15-fx3-le1-after`. The paused pose pixel for pixel (both eyes, their world layers, the HUD panel, the label), the same
`hostView`, eyes and layers; `mevr cover where`: `cover snapping refused` in both. The journal on the wrist (`mevr omni
journal`, the world running): the world layers pixel for pixel, the same held view, stack and placement; the HUD panel's
image differed only where the journal's new entries pulse (3.6% of its pixels, the rows' highlight caught at another phase);
after `mevr omni close` everything pixel for pixel again. The one frame of the screen's own camera that the first state's
hold removes on ME1 (an opening the game starts itself) is not in a gameplay frame and can't be caught by a snapshot.

## Close from the area map (lane OMX, 2026-11-17)

**The defect** (lane CTL, research/le2-vrik.md, session `20261117-124509-737-LE3-ctl-le3`): `mevr omni map` opened the
Mission Computer's area map; `mevr omni close` (the rail's Close) then backed out 8 times (`B through SFXGameModeGUI.GuiKey`,
12 ticks apart) with `'Areamap'` in focus every time, gave up (`the game stayed in its GUI mode after 8 backs out`), and the
Mission Computer came up 63 ms later and stayed. A player who opened the map from the wrist and pressed Close stayed in the
game's menus.

**The cause: the map's B starts a close animation, and another B starts it again.** Read with the conversation scanner's `--script` mode
over ME3's `SFXGame.pcc` (a scratch build with `--game LE3`, lane FX2's; the repository's takes LE1 and LE2; output in scratch
only): `SFXSFHandler_AreaMap.HandleInputEvent` answers
`BIOGUI_EVENT_BUTTON_B_RELEASE` (and either stick click) with `OnBeginClose`, which removes the depth of field and calls the
movie's ActionScript `OnBeginClose`; at the end of that animation the movie calls back `CloseMap`, which goes back to the
Mission Computer (`ReturnToBrowserWheel`) when the map was opened from it (`Initialize(bFromBrowserWheel)`), else hides the
map (`SFXGUIInteraction.HideAreaMap`, out of the GUI mode). It is the only screen of the rail's whose script acts on B's release
(the other handlers that do are the PC galaxy map and personalization screens); the Mission Computer, the squad record and the
journal have no such script and went 20-50 ms after their B. Measured on Save 31 (sessions `20261117-135058-709-LE3-omx-me3` and
`20261117-140155-459-LE3-omx-me3`, `mevr guikey` and the new back-out, times from the log):

| What was sent to the map | When it gave way |
|---|---|
| one B (press, then release) | 235 ms after it (`mevr guikey 16 37`); 234, 237, 247, 266 ms in the new back-out |
| six Bs about 200 ms apart (`16 37` with 10 empty ticks between) | 242 ms after the second, not after the first (the second came before the close ended) |
| two Bs about 490 ms apart | 243 ms after the first |
| lane CTL's eight, 176-205 ms apart | 249 ms after the last |
| the release alone (`mevr guikey 37`), once or five times | never: the game passes a release on only after its press |

So the old back-out, a B every 12 ticks (about 190-200 ms at the fake headset's rate), kept starting the map's 0.24 s close
again and the map went only once the Bs stopped; lane OM2's earlier "the map page took about a second to answer B" was the same
(and its runs at a few frames a second, where 12 ticks were longer than the close, worked). Neither the event codes (B 16 and
its release 37 are right) nor the map being a mode of its own (it is a page in game mode 9, or opened alone by
`ShowAreaMap` in the same mode) was the cause; the 8-try limit only decided when the back-out gave up.

**The fix (ME3 only; ME1 and ME2 keep their tick rule).** `vr::game_menu::StepBackOut` (core/vr/GameMenuRequest, unit-tested in
tests/GameMenuBackOutTests.cpp): a screen is its focused movie's object and tag (ME3 makes a new movie for every page and every
return to the Mission Computer); it gets one B (the press, then the release, in the same tick); the next B goes to the screen that
takes its place once it has had the focus 250 ms; the same screen gets another B only if it is still in focus 1.5 s after its B
(it took no notice: still opening), 3 at most; 8 Bs in all; 3 s with nothing in focus gives up. `game/GameMenuRequest`'s ME3
Close and its page switch (back to the Mission Computer before `ShowSelectedPanel`, which also waits 250 ms for the new
Mission Computer) take it; the game out of its GUI mode counts as closed after 200 ms; Computer asked over a page stops at the
Mission Computer instead of closing and reopening it; the map's "already up" check knows ME3's `Areamap` tag. Logs: `omni-tool:
backing out of '<tag>' (B and its release through ...; its first | again: still up N ms after the last, n of at most 8) for
<what>`, `'<tag>' gave way to '<tag>' N ms after its B`, `the game left its GUI mode N ms after the last B`, `back at the
Mission Computer N ms after the last B`.

**In the game** (Save 31, `-RendererPreview`, holder OMX; both stretches restored with `verification: 0 differences`; runner
and logs in the lane's scratch folder): Close returned to game mode 0 from every screen the rail opens, each screen with one B:

| Opened by | Close |
|---|---|
| the rail's Map | the map gave way in 234-266 ms, then the Mission Computer left the GUI mode 24-34 ms after its B |
| the rail's Squad | the squad record gave way in 29-47 ms, then the Mission Computer (it left the GUI mode 20-43 ms after its B, once 442 ms) |
| the rail's Journal | the journal in 39-49 ms, then the Mission Computer |
| the rail's Computer | the Mission Computer in 29 ms |
| the rail's Codex | ME3's Mission Computer has no codex page (Options, Resume, Load, Areamap, SquadRecord, Journal, Save, INVALID): the Mission Computer stays up (`no page for it`); Close closed it |
| the rail's Equipment | nothing opens on ME3 (`use the game's weapon/armour stations`); Close had nothing to do |
| Map, then Journal, then Squad (page switches) | the map gave way in 237 ms, the journal in 37 ms, each page opened from the new Mission Computer; Close closed the squad record and the Mission Computer |
| Journal, then Computer | one B: the journal gave way, the Mission Computer stayed up (`the Mission Computer is up`) |
| the game's Start (the left controller's menu button replayed as pad Start: `make-walk-recording.py --plan stand:1,start-button:0.15,stand:3`), alone and then Map, Squad or Journal from it | the same as from the rail |
| the console's `ShowAreaMap` (the game's own R3 route, which CTL keeps from the game: the map not from the Mission Computer) | the map left the GUI mode 228 ms after its one B |

The replayed Start opened the Mission Computer 4 times in 7 (the other three presses showed nothing; not investigated: the
game's Start binding, not the back-out).

**ME1 unchanged:** the ME1 and ME2 back-out code is the old code under `#else`; LE1's object code for
game/GameMenuRequest.cpp compiled with the build's own flags from main's source and from this lane's is identical (the
disassembly and the symbol table compared; only the debug records naming the pdb differ). The new core function compiles into
LE1 and isn't called there. No LE1 game run was needed for that reason.
