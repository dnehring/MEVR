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

`GuiCursor` polls the host native queue only when the real native mode, focused movie and `GuiKey` parameter contract are available. Polling itself claims native input ownership in `GamePointer`, so a missing consumer must preserve Win32 fallback. On focus changes it drops queued input belonging to the previous movie. Physical hover, held drag/release through focus loss, and SteamVR session recreation still require native acceptance.

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
