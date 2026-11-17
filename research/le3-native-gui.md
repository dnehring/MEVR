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
