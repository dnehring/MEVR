# 53: The game's full-screen screens in VR: the audit, and the world UI (2026-11-04)

The user, after the galaxy map ([51](51-galaxy-map.md)): "We're going to need to audit the game to replace all full
screen experiences with something like these holograms. One that immediately comes to mind is buying from a vendor... Always
bias toward reuse of existing assets." This note is the audit (every screen LE1 has, how it opens, what it shows, what VR does
with it today and the plan) and the first answer to it: the **world UI**, the game's own screen standing in the room where it
opened (the shop at its vendor, the map and journal in front of you), the level around it, a controller as its mouse.

Sources: the running game through our reflection layer (`mevr classes`, `mevr panel`, `mevr props`, `mevr find`; the Normandy,
BIOA_NOR00 with the cargo hold streamed in, fake headset), LExSDKv2's generated LE1 headers (read only: `SFXGame_classes.hpp`
EGameModes, `BioUIWorld`, `BioSFManager.SFHandlerTemplate`), snapshots named below.

## 1. How LE1 shows a screen

- **The GUI library.** `BioSFManager.HandlerLibrary` (config, read from its default object) lists 35 templates: a tag, a Flash
  movie, a handler class, a z-order. Each open screen is a `BioSFPanel` the manager keeps in `m_lstPanels`, its tag in `nmTag`,
  its handler in `m_DefaultHandler`, the focused one in `m_FocusPanel`. The live manager is `MassEffectGuiManager_0` under the
  viewport client, reached from `PlayerController.m_oCachedScaleFormManager`.
- **The game modes** (`SFXGameModeManager.EGameModes`): 0 Default, 1 Vehicle, 2 PowerWheel, 3 WeaponWheel, 4 Command,
  5 Conversation, 6 Cinematic, **7 GUI**, 8 Movie, 9 Galaxy, 10 Minigame, 11 Photo, 12 FlyCam. Every full-screen screen below
  runs in 7 except the galaxy map (9) and the bypass minigame (10).
- **`SFXGameModeGUI.StackedGuis` is not the open screens**: with the shop open it held one entry, `None`.
- **The UI world.** A panel with `bFullScreen` makes the game render its *UI world* (`BioWorldInfo.m_UIWorld`, a `BioUIWorld`:
  a level of its own, `GUI_UIWorld`, with its own camera actor, post-process chain and pawns spawned from the party for the
  screens that show a character) instead of the level. At the shop it is a backdrop: blurred machinery planes
  (`GUI_UIWorld.MachineBlur_MAT`) under a blue gradient, letterboxed to the 16:9 stage, its camera at (-1674, -45, 27) (the
  one-Draw log's "no pass-1 record for the view at (-1673.9, -44.7, 27.0)"). Both eyes' world images were identical within 0.02
  of 255 (snapshot 2026-11-04 08:28:05): a backdrop at infinity. **With the panel's `bFullScreen` cleared the level renders
  behind the screen** (`mevr setprop` on BioSFPanel_11, snapshot 08:36:09: the cargo hold, Shepard and the officer behind the
  shop's UI). The flag stayed cleared; the screen worked on.
- **The equipment and character record screens show a 3D model in the UI world** (since 2026-11-05 drawn behind our screen from a
  capture of the UI world, which the mod ticks itself: research/57 section 7) (the squadmate; Shepard, with the record's
  Toggle Helmet button). With the flag cleared they leave a gap where the model stood (snapshots 08:44:41, 08:47:36).
- **Opening the on-foot screens.** `SFXGameModeBase.ShowEquipmentGui`, `ShowSquadGui`, `ShowCodexGui`, `ShowJournalGui`,
  `ShowMapGui` (each `bShow`) and `ShowMenuGui`, what the keys call; `mevr gui <Equipment|Squad|Codex|Journal|Map|Menu>` calls
  them on the on-foot game mode. Equipment opens the `Inventory` panel alone; the others open `MenuBrowser` (the main wheel,
  `GUI_SF_InGameGui.PC_MainWheel`) with the screen over it (`CharacterRecord`, `Codex`, `Journal`, `Map`), both full screen.

## 2. What VR does with them today

Halcyon treats game mode 7 as a menu: its flat panel (2.35 m wide, 1.4 m ahead, fixed in the room: the shop's snapshot
08:11:12 quad0) with the world gone, one eye rendered. Its `[LIVEGUI]` branch (`liveWorldGui`: mode 7 as a live in-world GUI,
both Draws, no flat panel) exists for the Eden Prime bomb disarm but its verdict is disabled ("always static", xr_session.cpp).
Mode 9 is ours (research/51). Mode 10 [verify]. The game pointer (research/42) points at the flat panel.

## 3. The audit

All 35 templates of the GUI library, in its order. "Room" = the world UI below. Content: what the screen draws beyond Flash.

| Tag | Movie | Opens from | Content | VR today | Plan |
|---|---|---|---|---|---|
| Reticule | GUI_SF_HUD.Reticule | always | HUD | hidden; our hand reticle | done (research/15) |
| HUD | GUI_SF_HUD.PC_ME_HUD | always | HUD | HUD panel, wrist and gun pieces | done (research/15, 41) |
| PhotoMode | GUI_SF_PhotoMode.PhotoMode | photo mode (mode 11) | overlay | [verify] | out of scope |
| Tutorial | GUI_SF_Tutorial.Tutorials | tutorial pop-ups | overlay | HUD panel [verify] | check in the headset |
| DeviceSelection | GUI_SF_Tutorial.Tutorials | console storage | - | n/a on PC | none |
| Conversation | GUI_SF_Conversation.Conversation | conversations (mode 5) | the dialogue wheel | Halcyon's VR cine; 3D screen (research/52) | done (separately) |
| BlackScreen | GUI_SF_Utility.BlackScreen | fades | black | fine | none |
| SkillGame | GUI_SF_SkillGame.PCMiniGame | the bypass minigame on encrypted consoles, crates and doors (mode 10; the level's `BioSkillGame` with the placeable as `oDependent`) | Flash minigame | **wrist** (section 6): projected from the left wrist; in the room at the placeable (`BioSkillGame_Handler.m_oDependent`) without a tracked left controller or with `wrist = none` | done, reached on the Normandy (section 6) and at a real encrypted locker on UNC20 (section 5.1); played by touch with the omni-tool's d-pad (section 6.3) |
| IntroText | GUI_SF_Utility.IntroText | the opening text | text | flat | keep |
| MainMenu | GUI_SF_MainMenu.MainMenu | the title | Flash over its flyby | flat | keep (front end) |
| **shop** | GUI_SF_Shop.PCShop | a vendor's conversation, `BioSeqAct_OpenShop` (its `m_shop` is a BioShop actor placed at the vendor) | Flash, the item's picture (a render target in the movie) | **room: at counter height in front of the vendor** | done (section 4) |
| ReplayCharacterSelect | GUI_SF_ReplayCharacterSelect.PC_ReplayCharacterSelect | new game+ | front end | flat | keep |
| GalaxyMap | GUI_SF_GalaxyMap.GalaxyMap | the CIC console (mode 9) | its own 3D map level + Flash | ours: hologram, then the game's map on the projector's disc | done (research/51) |
| PartySelect | GUI_SF_PartySelect.PartySelect | `BioSeqAct_ShowPartySelectionGUI` before missions | Flash (the squad as pictures in the movie: no UI world) | **room**, toward what was used | done, seen on UNC20 (section 5.1) |
| NewCharacter | GUI_SF_MainMenu.CharacterCreation | character creation | the UI world's Shepard (`BioUIWorld.m_oNCHandler`) | flat | keep (front end); later a mirror |
| AdditionalContent | GUI_SF_AdditionalContent.AdditionalContent | DLC list | Flash | **room** | listed [verify] |
| **MenuBrowser** | GUI_SF_InGameGui.PC_MainWheel | Start / our menu's Game menu (`ShowMenuGui`); the hub for the screens below | Flash | **room, straight ahead** | done |
| Options | GUI_SF_Options.PCOptions | the wheel | Flash | **room** | listed [verify] |
| **Journal** | GUI_SF_Journal.PC_Journal | the wheel, `ShowJournalGui` | Flash | **room** | done |
| **Codex** | GUI_SF_Journal.PC_Codex | the wheel, `ShowCodexGui` | Flash, pictures, narration | **room** | done |
| Inventory | GUI_SF_Inventory.PCInventory | the wheel, `ShowEquipmentGui`; Normandy lockers (`BioSeqAct_ShowInventory`) | Flash + the squadmate in the UI world | **room** (straight ahead; a locker's toward the locker), the squadmate behind it (research/57 section 7) | done |
| XMods | GUI_SF_Inventory.PCInventory | upgrades, from the equipment | as Inventory | **room** | with Inventory |
| Loot | GUI_SF_Loot.PC_Loot | containers and bodies with items (`MassEffectGuiManager.ShowLootGui(oLootInv)`) | Flash | **room**, at the container or body (the inventory's owning actor), its frame only (stage x 0.30..0.70, y 0.04..0.95) at the whole stage's scale | done, seen at a real container (section 5.1) |
| Achievement | GUI_SF_Achievement.PC_Achievement | achievement list | Flash | **room** | listed [verify] |
| GameOver | GUI_SF_GameOver.GameOver | death | Flash | **room** | listed [verify] |
| CharacterRecord | GUI_SF_CharacterRecord.PC_CharacterRecord | the wheel, `ShowSquadGui` | Flash + Shepard in the UI world | **room**, Shepard behind it (research/57 section 7) | done |
| Credits | GUI_SF_Utility.Credits | the end | text | flat | keep |
| DesignerUI | GUI_SF_HUD.DesignerUI | designer minigames and timers (layouts: Quasar, Mira's Hanoi, the laser, the toxin cure; the bomb disarm's timer): Kismet's `BioSeqAct_MiniGame` (mode 10, `SFXGameModeMinigame`) with `BioSeqAct_DUISet<Layout>Layout`; always open, hidden until used | an overlay over the live world | **room** in mode 7 or 10 while visible, toward what was used | listed; not reached on the test rig (section 5.1) |
| Specialization | GUI_SF_CharacterRecord.Specialization | `BioSeqAct_ShowSpecialization` | Flash | **room** | listed [verify] |
| Save | GUI_SF_SaveLoad.PCLoadSave | the wheel | Flash | **room** | listed [verify] |
| Load | GUI_SF_SaveLoad.PCLoadSave | the wheel, the title (the title stays flat: no level) | Flash | **room** in a level | listed [verify] |
| **Map** | GUI_SF_AreaMap.PCMap | the wheel, `ShowMapGui` | Flash | **room** | done; later a table hologram |
| Splash | GUI_SF_MainMenu.Splash | boot | splash | flat | keep |
| **MessageBox** | GUI_SF_Utility.WindowPop | over any screen (confirmations) | Flash | **room, on its parent's panel** | done when over one of ours |

**Every flat screen in the room** (the user, 2026-11-04: "we most certainly want to have the augmented display for all flat
interactions, like for things like looting"): `[WorldUi] screens = all` by default. The front end (title, character creation,
credits, splash, intro text) stays flat: no level to stand in. Open: the equipment and character record's 3D models (the UI
world's, which the room hides), and the real-flow checks of loot, the minigame and the listed-but-untested screens in a mission.

## 4. The world UI (2026-11-04)

The galaxy map's pieces made generic (core/vr/WorldUi, game/WorldUi, plugin/WorldUi; HostServices v27 `setWorldUi`;
`[WorldUi]` in `ASI\LE1MEVR.ini`):

- **Which screen.** In modes 7 (GUI) and 10 (the bypass minigame) the mod reads the GUI manager's panels; the first *visible*
  one whose tag is in our table decides the place (a message box over the shop stays on the shop's panel; `DesignerUI` is always
  open and hidden until a designer minigame shows it). If any visible screen of ours isn't listed in `[WorldUi] screens` (all by
  default) the whole stack stays on Halcyon's panel.
- **The level behind it.** Every listed full-screen panel open gets `bFullScreen` cleared (journalled), put back on those still
  open when ours goes down. The level renders instead of the UI world.
- **The view held at Shepard's.** The mod records the player camera's POV on foot (a `Camera.UpdateCamera` post-handler) and,
  while a screen is up, publishes it with the POV's addresses; the host swaps it in around the main view's `CalcSceneView`, the
  galaxy map's seam (`MevrBridge::GalaxyViewSwapBegin/End`, now taking either hold). [verify] whether the shop moves the camera at
  all once the UI world is off; the hold is harmless if it doesn't.
- **Halcyon renders it as on foot.** One `[MEVR]` seam in xr_session.cpp: `liveWorldGui = true` in mode 7 while
  `MevrBridge::WorldUiActive()`: its own live-GUI branch (no flat panel, both Draws).
- **Where it stands.** Placed once per opening, fixed in the room (core/vr/WorldUi `Place`, unit-tested): `distanceM` (0.95) ahead
  of the eyes toward what it belongs to (never nearer it than 0.35 m, nor the eyes than 0.55 m), its centre `belowM` (0.50) below
  the eyes, facing them (its X level), `widthM` (1.20) wide. With no anchor, straight ahead. The shop's anchor: the vendor, the
  nearest pawn within 250 units of the handler's `m_shop` BioShop actor (the requisitions officer `BioPawn_9`, 68 units from
  `BioShop_0`); the actor itself is 50 units beside him, which put the screen 17 degrees off. At counter height the vendor's face
  stays clear above the screen (snapshot 08:41:09; at 0.12 m below the eyes the item's picture covered it, 08:37:23). The loot's
  anchor: the actor its inventory belongs to (`BioSFHandler_Loot.m_oLootInventory`'s outer chain: a container's
  `BioUseable_N.BioArtPlaceableBehavior_N.BioInventory_N`, a body's `BioPawn_N.BioPawnBehavior_N.BioInventory_N`; measured with
  `mevr loot` on the galaxy console's inventory: "the loot comes from BioUseable_0", the screen toward it). The minigame's: its
  handler's `m_oDependent` (the placeable played on). A screen a use opens otherwise (a locker's equipment without the main wheel,
  the squad selection, a specialization, a designer minigame) stands toward what A would have used within the last 3 s on foot
  (`BioPlayerSelection.m_oCurrentSelectionTarget`).
- **What it shows.** The game's UI goes to the HUD panel (all its Scaleform draws, as for the galaxy map), shown as a quad of the
  stage's part only: the stage is the 16:9 box of the window (`Stage`; the shop's rows 537..1855 of 2392, measured from its flat
  frame), the screen's crop inside it (`DefaultCrop`: the shop's content spans stage x 0.17..0.83, cropped to 0.14..0.86; the
  rest the whole stage until measured). Its dark parts at `backdrop` (0.80) opacity, the bright ones solid: the panel shader's
  alpha times `lerp(backdrop, 1, saturate(4 * max channel))` (the galaxy disc's backdrop idea; research/51 section 8.1).
- **The pointer.** The game pointer gets the whole window on the panel's plane (`WholeWindow`, unit-tested: the crop's corners on
  the panel's corners), so a ray's UV is the window's pixel. A replayed controller ray at the shop's Sell button
  (`tools/make-galaxy-pick-recording.py --snapshot ... --panel-uv 0.807 0.818 --layer hud`) landed on window pixel (1690, 1615);
  the button's centre in the game's own flat frame is (1699, 1615). The click itself didn't register on the test rig: SteamVR's
  `vrmonitor` window had the focus and Windows refused the game's `SetForegroundWindow` (LE1's menus take clicks only with the game
  window in front; research/50). [verify] the click in a headset session, where the game is in front.
- **Mode 10.** A second Halcyon seam (render_hook.cpp `SfrModeAllowsReplay`: mode 10 while `WorldUiActive`) and mode 10 in the
  live-GUI seam and the overlays; the minigame renders the world as on foot.
- Logs `world ui:`; the mod's snapshot field `"worldUi"`. Tools: `mevr panel list | <tag> show|hide|var|invoke|movie`,
  `mevr gui <screen>`, `mevr classes <text>`, `mevr instances <Package.Class> [text]` (live objects of a class with their owning
  actor's place), `mevr loot <n>` (`ShowLootGui` on an inventory), `mevr lootcorpse <n>` (`BioPawnBehavior.LootCorpse`).

## 5. Testing without a save: what didn't work, and where the real cases are

- `ShowLootGui` alone opens the loot screen for one tick and the game closes it again (an empty inventory: the galaxy console's;
  a shop's stock: the same), and `LootCorpse` on a living pawn opens nothing: the real flow is a container's use.
- `open BIOA_PRO00` (Eden Prime) loads but starts the prologue: the controller possesses another pawn, a conversation's panel is
  up, `mevr setloc` moves a pawn the controller doesn't own. Eden Prime's first areas (`streamlevelin BIOA_PRO10_0x_*`) hold the
  real cases: encryption consoles (`BIOG_APL_Door_P.Consoles.Console_Encryption`, state "encrypted": the bypass minigame) at
  `PRO10_00_PLC` `BioUseable_0..3` (-36060, 147455, -125) and (-34500, 143650, 660), and ammo cases
  (`BIOG_APL_TYPES.Containers.AmmoCase_00_Con_Type`, "Closed": loot) at `PRO10_01_PLC.BioUseable_0` (-24988, 126722, -1107) and
  `PRO10_03_PLC.BioUseable_4` (-16433, 133959, -72). With a save past the prologue: stand by one, `mevr use` its actor (what A
  does), snapshot.
- The minigame itself needs no save: section 6.

### 5.1 A real mission: the VR career's save on UNC20 (2026-11-04)

- **Getting there.** `tools/launch-game.ps1 -Steam`: the title's Enter resumes the last save, the VR career's autosave in the
  Mako on UNC20 (the cockpit work's planet); the script waits for on-foot play and times out in the Mako, harmlessly.
  `mevr vehicle exit` (the game's `TryExitVehicle`), `mevr setloc` into the planet's base (`BIOA_UNC20_01_DSG`, around
  (80000, 51500, -25350): a storage locker, a junk container, an aid kit, a computer, four doors).
- **Using things without the pad.** The game window has no focus on the test rig, so the pad's A doesn't reach the game. The
  gaze picks the target as in play (`interact: A would use ...` once the replayed head looks down at it), and **`mevr call <n>
  Used` on the live `SFXGameModeDefault`** (`mevr instances SFXGame.SFXGameModeDefault`) is what A does: the game's own
  path, its animation, its checks. (`Actor.UsedBy` on the container returned false and opened nothing.) `mevr call <n> <Function>`
  calls any function without parameters on a found object.
- **The encrypted storage locker** (`StorageLocker_01_Con_Type`, `m_SkillDifficulty` 0): the game's prompt, our label at the
  object, reads "Storage Locker / Decryption: Skill Too Low", and A does nothing: this Shepard has no decryption skill (the
  game's rule). **With the skill raised in memory** (`mevr skillgame decrypt 10`: Shepard's `BioAttributesPawn.m_SkillDecryption`
  base and current, journalled, never saved; it was 0) the prompt reads "Storage Locker / Easy Decryption" and A opens the
  minigame through the game's own path (15:00:18): the omni-tool on the forearm, the screen projected from its display, the
  orange fan (snapshot 15:00:38). With 0 omni-gel the screen offers only "Begin manual override" and "Cancel": the omni-gel
  button needs omni-gel (section 6's open question). `PreGameButtonAOnPress` started the puzzle on the wrist (the rotating
  rings, the timer; 15:01:04); it timed out, the screen and the omni-tool went (15:01:21). The game's hand display draws a pale
  beam of its own up from the back of the hand, toward the screen above it. The saves stayed unchanged (checked against a backup).
- **The junk container** (`JunkS_00_Con_Type`, "Malfunctioning Object / Open"): A opened the loot screen in the room, toward the
  container 0.84 m ahead and 1.76 m below the eyes, 0.55 m from the eyes (the nearest the room placement allows), the lid
  opening under it (snapshot 14:28:10). Its frame filled only stage x 0.317..0.683, y 0.064..0.922 of a 1.20 x 0.67 m panel, the
  rest an empty 0.8 backdrop over the crate; cropped to its frame at the whole stage's scale (`DefaultCrop`, `RoomWidth`) it
  is 0.48 x 0.61 m with the list the same size (14:31:38). `mevr call <n> HideLootGui` on the `MassEffectGuiManager` closes it
  (the first call was followed by a fresh opening, the second closed it [verify why]).
- **The squad selection** (`BioSeqAct_ShowPartySelectionGUI` of UNC20's own sequence, fired with `mevr activate`): the room
  placement straight ahead (nothing used), 1.20 x 0.67 m at 0.95 m, its `bFullScreen` cleared so the level stands behind it
  (snapshot 17:13:58): "Current Squad", the strength bars, Examine Squad / Accept Squad, the squad as the movie's own pictures
  (the chosen one a blue hologram on an orange ring). Before a mission it has no way back: the handler has only `ConfirmParty`
  and `FinalizeParty`, its movie no cancel, and B (`mevr panel PartySelect input 13`: the panel's handler takes a BioGuiEvent,
  12 A, 13 B, 14 X) does nothing; accepting would run the level's squad sequence, so the test closed the game instead.
- **The designer minigames** (Quasar, Mira's Hanoi, the laser, the toxin cure) aren't loaded on UNC20 (no `BioSeqAct_MiniGame`,
  no layout actions). Flux's Quasar machines need the Citadel: `open BIOA_STA00` from this save started the post-Eden Prime
  medbay conversation, and its dialogue wheel takes the choice only through the game's own input (the handler's A did nothing),
  which the unfocused test rig can't give. To do in the headset, or from a save standing in Flux [verify].
  **A player's report (2026-11-12): the Hanoi puzzle's instructions were out of view.** Halcyon moves the whole designer
  panel by its master transform (`designerUi*`, offset -358/-288, tuned for the Saren fight's timer) and exempted only the
  laser layout (`[LASERUI]`), so the Hanoi, Quasar and toxin-cure layouts were pushed up and left off the HUD panel. A
  `[MEVR]` latch in `pchud.cpp` (`MaybeLatchMinigameLayout`) now gives those three layouts the game's own placement while
  their `BioSeqAct_DUISet<Layout>Layout` has `bShow` set (log `[MEVR-DESIGNERUI] puzzle layout latched`); the timer keeps the
  master set. Not yet seen in the game [verify]. The saves stayed
  unchanged. `BioWorldInfo.BioLoadGame(96)` (a load by slot) loaded nothing from the title's
  Normandy: the user's Citadel save is another career's, so a real crate still means the menus.

## 6. The bypass minigame: reached on the Normandy, projected from the wrist (2026-11-04)

The user: "for hacking mini-game we consider allowing for auto hacking. I'm curious if we should have a wrist mounted display
option so that it projects from there instead. Minimal vfx to feel real", then: "Let's scratch removing the mini game and we'll
likely want to make it something where they need to tap on the wrist mounted display", and "Please continue with wrist mounted
projector though".

- **The pattern.** Fallout 4 VR's Pip-Boy sits on the left wrist, raised to read, with a setting that projects it in front of you
  instead (Settings > VR > "Pip-Boy Location": Wrist / Projected, per a 2020 Steam forum post,
  https://steamcommunity.com/app/611660/discussions/0/1621724904371360432; Bethesda's launch notes list "Pip-Boy display
  settings", https://bethesda.net/en-AU/news/fallout-4-vr-launch-details). Half-Life: Alyx's multitool projects its hacking puzzles
  as holograms (Valve's "Holo Hacking Puzzle", https://developer.valvesoftware.com/wiki/HLA_SDK/Holo_Hacking_Puzzle). Mass Effect's
  own: Shepard bypasses with the omni-tool, the holographic forearm device. So the minigame projects from the left wrist.
- **Reaching it without a save.** `mevr skillgame start [type] [difficulty] [n]`: the level's own `BioSkillGame`
  (`BioWorldInfo.m_SkillGame`) `InitializeSkillGame(Shepard, the placeable, type, salvage 1..10, 6 buttons, 1.5 s, 0.5 s,
  difficulty)`, the placeable the (n-1)-th of `mevr find` / `mevr instances SFXGame.BioArtPlaceable` (without n:
  `InitializeSkillGameCustom` with no callbacks and Shepard as the dependent). The game goes to mode 10 with the `SkillGame` panel
  (handler `BioSkillGame_PCHandler`), and the world UI takes it as it would at a crate. Snapshot 10:35:25: the screen in the room
  toward the Normandy's locker `NOR10_00_PLC.BioUseable_4` (with Shepard as the dependent it stood at his feet, 10:31:09).
- **What the screen is.** "DECRYPTING" and three buttons: Begin manual override, Cancel, "Override using N omni-gel" (N the
  handler's `m_nMaxSalvage`). Its frame spans stage x 0.226..0.775, y 0.120..0.848 (`hud.png` of 10:40:44). The movie
  (`mevr panel SkillGame movie GUI_SF_SkillGame.PCMiniGame`: the GFx has no MovieInfo at run time, so the verb takes the package
  name) runs `PreGameButtonAOnPress`/`BOnPress`/`XOnPress` for them and reports `SkillGamePreGameButton` to the game (its strings);
  the handler's `HandleInputEvent(BIOGUI_EVENT_BUTTON_X)` and `HandleSkillGameButtonPress` did nothing before the game starts.
  `mevr panel SkillGame invoke PreGameButtonBOnPress` cancels it as the button does (the way to close a test minigame).
- **Auto-hacking: tried and dropped.** The omni-gel button through `invoke PreGameButtonXOnPress` closes the screen, but on a
  Normandy locker no omni-gel was taken (50 before and after; with 3 against a cost of 10 it closed too), so whether the game
  checks and charges on that path needs an encrypted crate [verify]. The button showed with the custom start and was missing on
  the plain start at `BioUseable_12` with no omni-gel (11:00:52) [verify what decides]; `UpdateSalvage(0)` doesn't change the cost
  shown. Shepard's omni-gel is `SFXPawn_Player.m_oBehavior.m_oInventory.m_fResourceSalvage` (`mevr skillgame gel`; the
  `initsalvage` cheat didn't set it). An auto mode that pressed the override for you was built and removed the same day at the
  user's call: the minigame stays the player's, played next by tapping the wrist screen.
- **The wrist projector** (`[WorldUi] wrist = SkillGame` by default, a comma list like `screens`; our menu's Comfort > Game screens >
  Hacking screen: On my wrist / In the room writes `SkillGame` / `none`). The host places it from the left wrist (the HUD's wrist
  point, `hud_pieces::WristPoint` of the left grip) every frame, smoothed (50 ms): `wristWidthM` (0.30) wide, facing the eyes, its
  bottom edge's middle `wristAboveM` (0.06; 0.10 since section 6.1) above the wrist **across the line of sight** (along its own up), moved
  `wristTowardM` (0) toward the eyes, never nearer them than 0.25 m (core/vr/WorldUi `WristPlace`, unit-tested). Straight up from the
  wrist put it between the eyes and the wrist (11:00:52: tilted to face eyes above it, it hid the wrist and the light). It shows the
  minigame's frame only (`WristCrop`: stage x 0.21..0.79, y 0.10..0.87); in the room it keeps the whole stage (the frame cropped to
  the room's 1.20 m stood 64 degrees wide). It unfolds out of the wrist over 0.18 s (`Unfold`: its height from nothing, its width
  from 60%, eased out, the bottom edge fixed). `wristFollow = 0` leaves it where it was projected, the hand free (its light only
  while it opens). The game pointer gets the whole window on its plane each frame.
- **The light** (`wristBeam`, plugin/WristBeam): one quad in the plane of the wrist and the screen's bottom edge, a fan from the
  screen's width to a point at the wrist, alpha 0.30 at the wrist fading to 0.05 at the screen, soft sides, the pale blue of the
  minigame's frame at first, the omni-tool's orange since section 6.1 (`BeamPixels`, 128 x 128, made once per session). Premultiplied in linear light: the first picture premultiplied
  the sRGB-encoded values and darkened what was behind it (11:09:53, its difference from the world image brown, not blue).
- **Without a tracked left controller** when it opens the screen stands in the room as before, with the room's crop: "would be
  projected from the left wrist, but the left controller isn't tracked: it stands in the room" (11:02:18). A controller lost while
  it rides on the wrist leaves it where it was.
- **Measured** (the fake headset and a synthetic recording: the head pitched 30 degrees down, the left hand raised 0.42 m below and
  0.34 m ahead of the eyes and swaying 3 cm, the right hand aimed at the screen; snapshot 11:15:04): the screen 0.30 x 0.22 m,
  0.45 m from the eyes, centred 0.23 m below them; the fan 0.30 x 0.06 m under it, seen as a pale wedge to a point at the wrist; the
  game pointer's dot on "Begin manual override", window pixel (1175, 1171), UV (0.50, 0.49) of the whole-window quad (0.52 x 0.53 m),
  the same transform as the screen's pixels. The first placement (11:00:52: 0.32 m away, lifted straight up) hid the fan; the
  first fan (11:09:53) needed an 8x brightened difference image to be seen. Without a tracked left controller: the room (11:02:18).
- **Tapping it** (done in section 6.2 for its buttons; the puzzle itself is played with the omni-tool's d-pad, section 6.3). The right
  index finger on the wrist screen as a touch: the fingertip's contact with the panel's plane as the game pointer's press (research/42's
  click, at the touch point), released when it leaves; haptics on both hands at contact.

### 6.1 The game's omni-tool, and Half-Life: Alyx's way (2026-11-04)

The user: "We would follow how half life does it, for the look/assets/style we need it to look like core mass effect with the Omni
tool".

- **What Half-Life: Alyx does** (sources: the Combine OverWiki's "Hacking" and "Multi-tool" pages,
  https://combineoverwiki.net/wiki/Hacking, https://www.combineoverwiki.net/wiki/Multi-tool; Valve's level-design page,
  https://developer.valvesoftware.com/wiki/Half-Life:_Alyx_Workshop_Tools/Level_Design/Holo_Hacking_Puzzle, through a search
  summary: the page itself refused the fetch; PCGamesN, https://www.pcgamesn.com/half-life-alyx/vr-design; GameSpot's puzzle
  guide, https://www.gamespot.com/articles/half-life-alyx-multitool-puzzle-guide-how-to-solve/1100-6475047/): the multitool plugs
  into the device's outlet (`info_hlvr_holo_hacking_plug`), a holographic puzzle appears at a spot the level puts within the
  player's reach (`info_hlvr_holo_hacking_spawn_target`), and it's worked with the hands: the tool traces a point over a globe
  while the other hand turns it (Sphere Trace, Point Search, Point Match, Point Drag; touching an obstacle fails it). No pointer,
  no buttons: the hands in the hologram. What we take: the hologram comes out of the tool on the arm and is worked by hand (next:
  touch, section 6's last item); the look is Mass Effect's.
- **The game's omni-tool is loaded** (`mevr find omni`): `BIOG_V_T_Omni_Tool_Z`, a global package. `Prefab.OmniTool_2PieceLoop`
  (a `BioVFXTemplate`, a crust effect: `bIsCrustEffect`, lifetime 3 s, its sound `snd_vfx.c_snd_omni_imp`) instances
  `Prefab.OmniTool_2PieceLoop_Prefab`, whose emitters play `Particles.OmniArm_Loop` (mesh particles of `Mesh.OmniArm`, the gauntlet)
  and `Particles.OmniHand_Loop` (`Mesh.OmniHand` and `Mesh.Omni_Tool_02`, the hand's display), materials `HologramType1/2`,
  textures `TTMain`, `TTRing02`. The equipment screen puts it on its model through `BioUIWorld.AttachOmnitool` (its
  `m_pTechToolEffectTemplate`); `BioVisualEffect.CreateCrustEffect(template, pawn, lifetime, bUseVFXPool)` on the class's default
  object makes it on any pawn. It attaches each prefab emitter to the bone nearest its place in the prefab: the gauntlet to
  `LeftElbowTwist2` (relative (1.1, 3.0, 0.6) units, rotation (-360, -17208, -32475)), the display to `LeftWrist` ((-0.3, -4.8, -0.9),
  (228, -18443, -31793)).
- **Why it showed nothing at first.** The emitters (and the effect) come out with `bTickIsDisabled` copied from the prefab's
  archetypes: their particle systems never ticked (`AccumTickTime` 0, `bJustAttached` set) though `LastRenderTime` moved.
  `Actor.SetTickIsDisabled(false)` on all of them plus `BioVisualEffect.SetState(BVFX_SPAWN)` made it show on foot (12:57:20).
  In the minigame and the GUI screens the world is `bPlayersOnly` (`WorldInfo.bPlayersOnly` true, `Pauser` none), and the engine
  then ticks a component only for actors the player controls (the actor tick group's loop `0x40D630` calls the component tick
  `0x41C220`, which checks the actor's vtable `+0x490` under `LEVELTICK_ViewportsOnly`, or the component's `bTickInEditor` while
  unpaused, but only for actors the loop reaches): the pawn as the emitters' owner, `bAlwaysTick` and `bTickInEditor` all left them
  frozen. **The fix** (game/OmniTool): each particle system moves off its emitter (`Actor.DetachComponent`) onto Shepard's own mesh
  at the same bone and offsets (`SkeletalMeshComponent.AttachComponent`), and `ActivateSystem`: it ticks with the pawn, on foot or
  frozen (13:10:17). Closing the screen deactivates and detaches them and lets the effect's lifetime end (13:10:41: 1 orange pixel
  in a step-4 sample, 9,126 with it on).
- **Arm IK while a wrist screen is up.** On foot only before, so in the minigame Shepard's arms hung by the animation and the
  omni-tool on them was out of sight (13:01:28). Arm IK now also runs while a screen is projected from the wrist
  (`world_ui::WristScreenUp`), in Shepard's on-foot frame (`view_anchor::Hold`: the frame recorded on foot, as the view is held at
  render time), so the arms follow the controllers there (13:04:18).
- **The projector is the omni-tool's display.** The arm-length calibration can leave the rendered hand short of the controller (the
  synthetic recording's left hand rendered ~0.15 m below it), so the screen above the controller's wrist and the omni-tool on the
  arm came apart (13:22:46). The mod publishes the display's world place (the `OmniHand_Loop` component's `LocalToWorld`, in the
  on-foot frame) as `world_ui::State::emitter` (the State grew 16 bytes; host and mod deploy together), the host projects the screen
  and the fan from it, the controller's wrist as before without it. Its first 150 ms are skipped: the component still has the
  animation's arm then (an opening placed 0.93 m away, 13:34:20).
- **The look.** The fan is the omni-tool's orange, measured from the eye image (12:57:20: the hologram's mid tones (233, 147, 81),
  its core (255, 205, 103) sRGB; the fan's colour (1.0, 0.63, 0.35) sRGB-encoded). The game's minigame screen keeps its own blue
  (its movie, unchanged). `wristAboveM` 0.10 (0.06 covered the gauntlet's top: the display sits on the back of the hand and the
  gauntlet reaches above it as seen). `[WorldUi] omniTool = 1` (0: no omni-tool, the projector the controller's wrist).
- **Measured** (fake headset, the synthetic recording at a 60-degree head pitch, snapshot 13:36:36, `scratch/omni-tool-wrist.png`
  locally): the opening 0.44 m from the eyes from the controller's wrist, then riding on the display; the gauntlet and display
  pulsing orange on the forearm; the fan from the display to the screen's bottom edge. Not seen yet: the headset, a real crate, the
  omni-tool's sound (the template's cue) [verify].


### 6.2 Touching the screens (2026-11-04)

The user asked for the Half-Life: Alyx way (section 6.1): the hands in the hologram, no pointer. The game's screens are
clicked by touching them with the index fingertip; the laser stays for anything out of reach.

- **The rules** (core/vr/GamePointer, unit-tested): each hand's fingertip (`[Pointer] touchTipM` 0.04 m ahead of its aim pose,
  along it) is placed in the quad's own frame (u, v across it, the depth in front of it: the quad's +Z faces the viewer). Within
  `touchHoverM` (0.06) in front of the quad's touchable part (the world UI passes its visible crop, `touchArea` of
  `NoteGameUiQuad`, with a 1 cm margin) it takes over from the rays: the pointer moves under the fingertip (a lighter smoothing,
  0.8 a step). It presses at `touchPressM` (0.005) **only after being in front** (a hand coming through from behind never
  presses) and holds the press while the tip stays within `touchReleaseM` (0.015) or up to 6 cm behind the surface (sliding off
  the edge too, as a drag); the button up always follows. A trigger's open press isn't taken over.
- **The feel** (plugin/GamePointer): a light haptic tick as the fingertip reaches a screen, a click on the press and a lighter
  one on the release; the pointer dot grows with the fingertip's distance (3x at the hover edge, its own size at the surface).
  Logs `game pointer: the right fingertip pressed at (x, y) px, d m from the surface` / `let go`; the snapshot's
  `"gamePointer"` has `"touch"` and `"touchM"`.
- **Measured** (the real encrypted locker on UNC20, the screen on the wrist, a synthetic recording whose right fingertip comes
  along the screen's normal to "Begin manual override", hovers at 3 cm, presses 1 cm through, pulls back to 5 cm, 8 s a loop):
  pressed at (1156, 1161) px at 0.005 m, let go at 0.015 m, every loop; the laser had measured the button at (1175, 1171). The
  press reached the game: the puzzle started (snapshot 16:56:32: the rings and the timer, the dot under the fingertip). On the
  test rig the rendered right hand sits ~0.1 m below the controller (as the left did, section 6.1), so the drawn finger and the
  touch point don't meet there; in the headset `touchTipM` sets where the fingertip is [verify the value in the headset].

### 6.3 Playing the puzzle: the omni-tool's d-pad (2026-11-05)

The user: "I think the hacking mini game may need some work. Or we are comfortable with using the analog stick/face buttons", then
"Yes, proceed with touch for the puzzle. We may need a large d-pad asset to control its movement".

- **What the puzzle is.** Read from its own movie (`mevr panel SkillGame movie GUI_SF_SkillGame.PCMiniGame` writes its RawData to
  `%LOCALAPPDATA%\MEVR\galaxy\SkillGame.gfx`; its ActionScript 2 listed with a small bytecode lister kept locally: the player's game
  data, never committed) and measured through `mevr panel SkillGame var <name>`: a marker starts outside three turning rings at the
  bottom (`nCurrentDistanceIndex` 5, `nCurrentPlayerAngle` pi/2) and has to reach their centre (index 0 wins) before the clock runs
  out (about 14 s at the custom start's difficulty 0). Its key listener: up (38), `DPadUp`, A and X take it a ring in
  (`MovePlayerRing(-1)`), down (40), `DPadDown` and B a ring out; left (37) and right (39) set its turning speed to +1 / -1 while
  held and the key's release stops it (left turns it clockwise on screen: from the bottom toward the left). The orange segments are
  fixed walls (`HitAStaticBlocker` puts the marker back where it was), the red ones turn and send it back to the start (measured:
  index 4 to 5 and the angle back to pi/2). The mouse plays it too: the left button a ring in, the right one out, and the handler
  (`BioSkillGame_PCHandler.Update`, `ResetMousePos` every tick) turns the cursor's offset into a turning speed
  (`MouseMovePlayerDelta`, `MAX_MOUSE_DIFF` 4, `MOUSE_SPEED_MOD` 0.5); any mouse motion (`onMouseMove`) puts it in its mouse mode
  (`bUsingMouse`), where that speed isn't reset each tick.
- **The game's d-pad events drive it exactly.** `mevr guikey 9 w ... 31` (`BIOGUI_EVENT_CONTROL_LEFT`, then its release) through
  `SFXGameModeBase.GuiKey` (here `SFXGameModeGUI`) turned the marker 0.37 rad in 8 ticks, `10 ... 32` (right) 0.51 rad the other
  way, `11 w 33` (up) took it from index 5 to 4 where no wall stood above it; straight above the start an orange wall kept it at 5.
- **The pointer on the puzzle can't play it.** With the controller's ray anywhere on the window (even outside the puzzle's frame,
  at (1934, 2376) px) the puzzle went into its mouse mode and the marker spun at 2.9 rad/s without end (-0.47 to -18.2 rad in 7 s:
  the handler re-centres the cursor every tick and our `GetCursorPos` answer stays off-centre); a fingertip tap on its centre didn't
  take it a ring in. On the wrist, where the right hand naturally points, that loses the puzzle.
- **So, while it's played** (its handler's `BioSkillGame_Handler.m_bGameActive`: false on the opening screen, true from the manual
  override on; the mod sends it as `world_ui::State::puzzle`, a byte that was padding, so the State's size is unchanged): the host
  stops noting the screen to the game pointer, forgets it at once on the first frame of play (the quad, the cursor answer, the events
  not yet taken; five moves of the last 0.1 s otherwise reached it), and until a second after the last frame of play notes no quad
  of the game's at all and doesn't answer `GetCursorPos` (`game_pointer::HoldOffForPuzzle`): in one run a hitch let the mod's state go
  stale for a frame, Halcyon's flat panel came back for it, the live controller's ray landed on it, and the handler, which reads the
  cursor itself, turned the marker 0.37 rad. The mod's `game/GuiCursor` sends the puzzle nothing but pad events
  (`droppedForPuzzle` in its status). The opening screen's buttons keep the pointer and touch (section 6.2). Tried and dropped: the
  game's own switch to the pad (`BioSkillGame_PCHandler.HandleButtonRefresh(true)`, which clears `bUsingMouse`) at the start. The
  movie applies the mouse moves already queued after it, and in its pad mode the turning speed isn't reset each tick, so the last
  mouse speed stayed and turned the marker 1.3 rad. Left in its mouse mode with no more mouse input, the speed is reset to the
  cursor's (now nil) offset every tick: one small step of 0.09 rad at the start, then still.
- **The d-pad** (host `plugin/OmniPad`, rules `core/vr/OmniPad`, unit-tested in `tests/OmniPadTests.cpp`; `assets/menu/omnipad.rml`
  and `.rcss`, RmlUi in a context of its own as the rail is; `[OmniTool] pad = 1`, `padSizeM` 0.17): while the puzzle is played a
  0.17 m square stands 2 cm to the right of its screen in the screen's plane (toward the right hand; each arm about 5.7 cm across),
  four arms around a hub, in the puzzle's own colours (its frame's slate blue and navy, its cyan, its ring's orange; the arm held in
  the game's orange; "In" and "Out" on up and down). The right fingertip (or a ray and its trigger; not the hand the screen is
  projected from) pressing it holds the arm whose 45-degree sector around the centre it's in, outside the hub (0.13 of the size),
  and the game hears that arm's `CONTROL_*` press, then its release when the finger lets go; sliding to another arm lets the first
  go and presses the next, as a thumb on a pad does; a hitch that drops the screen for up to 0.3 s keeps the pad and the arm held.
  The events go through the pointer's queue to the mod (HostServices v30's
  `takeGuiEvents`), never dropped and sent alone, one a tick, with no mouse motion before them (`core/vr/GuiEvents`). Its trigger
  is kept from the game while pointed at. A haptic tick on each press. Logs `omni-tool pad:`, `world ui: the bypass puzzle started`;
  snapshot `"omniPad"` and layer `omnipad`; offline look `mevr-menu-preview`'s `pad-idle.png`, `pad-hover.png`, `pad-held-*.png`.
- **Measured** (fake headset, UNC20, the wrist-look recording's head and left hand with a right fingertip that comes along the pad's
  normal, presses 1 cm through and holds; `tools/console.ps1` reading the movie's variables about once a second): right held 3.3 s
  turned the marker steadily from pi/2 to -3.06 rad (snapshot 12:05:36: the right arm orange, the marker gone round to the top, the
  clock at 10.70, no pointer dot); left held 1.1 s turned it about +1.3 rad; presses on up took it from index 5 to 4 where no wall
  stood above (twice in one run, a red block sending it back to the start each time) and left it at 5 under a wall (another run). The
  pointer's last moves on the opening screen still leave one step of 0.09 rad at the start (`bUsingMouse` true until the first left
  or right press), then the marker stays still. [verify] with a person: reaching and holding the arms with the left wrist up, sliding
  between them, the size, whether In/Out and the arrows read as the puzzle's moves.

### 6.4 The console's button game: in the code, never shown on PC (2026-11-05)

- **What it is.** The user remembered a second hacking game, "clicking face buttons in a sequence". It is the Xbox 360 ME1's
  bypass, and LE1 still carries it: the base class `BioSkillGame_Handler` (the PC puzzle's `BioSkillGame_PCHandler` derives from
  it) shows one face button at a time, picked at random (`SetButtonVisible`, `HandleSkillGameDisplayButton`), and each press must
  match it before `m_fButtonTimeout` runs out (`HandleSkillGameButtonPress`: a wrong button fails the game at once, the timeout
  too); `m_nTotalButtonsToDisplay` right presses win, `m_fButtonDelayTime` apart. Its handler hears `BIOGUI_EVENT_BUTTON_A` to `_Y`
  (12 to 15) in `HandleInputEvent`; `SetSwapXandO` swaps A and B for layouts that confirm with B. Its numbers are the game's
  `BIOGame.ini` `[SFXGame.BioSkillGame] SkillGameConfig`: `NumButtons` 4, 6, 8, a timeout from 2.5 s (Casual) down to 1.0 s
  (Insanity), a start delay of 0.2 s. Its movie, `GUI_SF_SkillGame.MiniGame`, ships in every `Startup_*.pcc` beside `PCMiniGame`.
- **The PC game never opens it.** `MassEffectGuiManager.ShowSkillGame` creates the panel tagged `SkillGame`, and the PC's GUI
  library (`BIOUI.ini` in `Coalesced_INT.bin`: 35 `HandlerLibrary` templates, the console HUD the only one with a `Platform`) has
  one template for that tag, `BioSkillGame_PCHandler` with `PCMiniGame`. A gamepad doesn't change it: the puzzle only swaps its
  button help to the pad's glyphs (`HandleButtonRefresh(usingGamepad)`, the movie's `RefreshButtonHelp`). No other template is a
  minigame. So in VR (Touch reaching the game as Xbox pad 0) every bypass is the rings puzzle, and section 6.3's d-pad covers them.
- **If it's ever wanted** (a VR option: its four buttons are on the controllers): the `SkillGame` template's `HandlerClass` and
  `PanelResource` would have to name `SFXGame.BioSkillGame_Handler` and `GUI_SF_SkillGame.MiniGame` before the game opens the panel
  [verify: whether the native `CreatePanel` reads the default object's library live], and four touch buttons could send 12 to 15
  through the GUI events as the d-pad sends its directions. Not built.
- **How it was read:** the scripts decompiled locally with Legendary Explorer's core (the nightly in `mod-manager/`) and the config
  unpacked from `Coalesced_INT.bin` the same way. The player's game data: nothing of either is in the repository.

## 7. Landing's party selection after a world change (2026-11-08)

The owner landed from the galaxy map with the Mako, reached squad selection, but saw planet exterior partly through the
floor. This is separate from the map's lost table reservation (research/51 section 8.4) and is a separate code commit.
Evidence is the same read-only owner log copied to ignored `scratch/owner-2026-11-08.log`; no game was launched or changed.

**Log evidence (line numbers in that copy):**

- 5180, 22:11:26.158: a new player pawn in **BIOA_UNC71**, still in movie mode 8. 5182: cinematic camera.
- 5184-5194, 22:11:26.420-.421: new-level settings, then mode 7. `PartySelect` with `BioSFHandler_PCPartySelection` is
  visible; the world UI clears its full-screen flag and publishes it straight ahead. There is no on-foot camera interval
  between the level arrival and this screen.
- 5195, 22:11:26.538: `world ui: the view held at Shepard's for the main view's build only`.
  5197-5198, 26.591: the 1.20 x 0.68 m panel is at (0.04, -0.50, -0.98) m, and UI redirects to it.
- 5201-5204, 22:11:27.594: nevertheless the cinema backdrop and **cutscene on the 3D screen** appear, transitioning from
  the movie's flat panel. This is after PartySelect is already active, not evidence that its Flash movie failed to open.

**Two code defects.** `game/WorldUi` remembered a mode-0 POV and tracking frame without world/controller identity, and
accepted mode-0 camera transitions as foot views. Thus a new planet's GUI could hold a Normandy/map camera position. The
log does not contain that held position, so it cannot prove the exact geometry of the reported floor clipping. Separately,
Halcyon's cine latch intentionally survives modes 7/8/9. `liveWorldGui` vetoed `menuFlat` but did not veto the latched
cinematic or the outgoing movie detector. That explains why a published room panel still ended up behind the cinema
presentation. The supplied mod log has no `[VRSTATE]` lines, so it does not identify each internal latch bit.

**Fix.** `core/vr/WorldViewHold::Reference` scopes the saved view to Actor.WorldInfo and controller, and only an actual
on-foot camera may record it. `game/WorldUi` discards the POV, tracking frame, recent use target, vendor and cockpit anchor
on context changes, releases the old publication/reservation, and restores any remaining panel flags. Actor.WorldInfo already
has a ReflectCheck row; no new reflection symbol or engine-memory write is introduced. All panel writes still use journalled
FieldRef writes; the render swap uses the journalled path described in research/51 section 8.4.

Landing can open PartySelect before a foot camera exists in the new world. That screen alone is allowed to publish without
a saved view: a room panel straight ahead, no old-world anchor and **no held-view swap**, over the destination's current
camera. Its actual destination framing remains a headset acceptance check. Other foot-anchored screens still require a
known local foot view; the cockpit path remains unchanged. PartySelect is Flash portraits, not CharacterRecord's mannequin:
`ShowsFigure(PartySelect)` remains false; there is no evidence of a UI-world figure capture causing this failure.

The small `[MEVR]` seam in `CineFinishDecision` uses `core/vr/PresentationHold::WorldGuiOwnsView`: an admitted live-world GUI
in mode 7/10 clears the outgoing cinema/flat decision immediately. A GUI blip without ownership, front-end menus and real
movies/cinematics retain their existing policy. The generic render hold also requires a world-screen mode before swapping
the camera, so a last GUI publication cannot hold a subsequent movie's view.

**Offline verification:** `tests/WorldUiReturnTests.cpp` follows Normandy foot view -> landing movie -> new world ->
PartySelect before any foot view -> destination gameplay, plus wrong-camera samples, controller replacement, missing GUI
ownership, front-end and cinematic guards. Registered in Main.cpp and CMakeLists.txt. At source commit `553163c10e`, both
development targets and the monolithic players' target built successfully; all unit tests and the SPDX check passed.
See research/51 section 8.4's build receipt for exact commands and warning policies. No headset or runtime result is
claimed: follow the dated landing checklist, then regress the normal airlock
squad pick, shop/omni-tool, Mako and extraction screens.
