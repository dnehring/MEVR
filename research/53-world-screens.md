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
- **The equipment and character record screens show a 3D model in the UI world** (the squadmate; Shepard, with the record's
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
| SkillGame | GUI_SF_SkillGame.PCMiniGame | the bypass minigame on encrypted consoles, crates and doors (mode 10; the level's `BioSkillGame` with the placeable as `oDependent`) | Flash minigame | **room** (mode 10), at the placeable (`BioSkillGame_Handler.m_oDependent`) | done [verify in a mission] |
| IntroText | GUI_SF_Utility.IntroText | the opening text | text | flat | keep |
| MainMenu | GUI_SF_MainMenu.MainMenu | the title | Flash over its flyby | flat | keep (front end) |
| **shop** | GUI_SF_Shop.PCShop | a vendor's conversation, `BioSeqAct_OpenShop` (its `m_shop` is a BioShop actor placed at the vendor) | Flash, the item's picture (a render target in the movie) | **room: at counter height in front of the vendor** | done (section 4) |
| ReplayCharacterSelect | GUI_SF_ReplayCharacterSelect.PC_ReplayCharacterSelect | new game+ | front end | flat | keep |
| GalaxyMap | GUI_SF_GalaxyMap.GalaxyMap | the CIC console (mode 9) | its own 3D map level + Flash | ours: hologram, then the game's map on the projector's disc | done (research/51) |
| PartySelect | GUI_SF_PartySelect.PartySelect | `BioSeqAct_ShowPartySelectionGUI` before missions | Flash (+ UI world? [verify]) | **room**, toward what was used | listed [verify] |
| NewCharacter | GUI_SF_MainMenu.CharacterCreation | character creation | the UI world's Shepard (`BioUIWorld.m_oNCHandler`) | flat | keep (front end); later a mirror |
| AdditionalContent | GUI_SF_AdditionalContent.AdditionalContent | DLC list | Flash | **room** | listed [verify] |
| **MenuBrowser** | GUI_SF_InGameGui.PC_MainWheel | Start / our menu's Game menu (`ShowMenuGui`); the hub for the screens below | Flash | **room, straight ahead** | done |
| Options | GUI_SF_Options.PCOptions | the wheel | Flash | **room** | listed [verify] |
| **Journal** | GUI_SF_Journal.PC_Journal | the wheel, `ShowJournalGui` | Flash | **room** | done |
| **Codex** | GUI_SF_Journal.PC_Codex | the wheel, `ShowCodexGui` | Flash, pictures, narration | **room** | done |
| Inventory | GUI_SF_Inventory.PCInventory | the wheel, `ShowEquipmentGui`; Normandy lockers (`BioSeqAct_ShowInventory`) | Flash + the squadmate in the UI world | **room** (straight ahead; a locker's toward the locker), the 3D model missing | done; next: the squadmate as a hologram (a capture of the UI world [verify]) |
| XMods | GUI_SF_Inventory.PCInventory | upgrades, from the equipment | as Inventory | **room** | with Inventory |
| Loot | GUI_SF_Loot.PC_Loot | containers and bodies with items (`MassEffectGuiManager.ShowLootGui(oLootInv)`) | Flash | **room**, at the container or body (the inventory's owning actor) | done [verify in a mission] |
| Achievement | GUI_SF_Achievement.PC_Achievement | achievement list | Flash | **room** | listed [verify] |
| GameOver | GUI_SF_GameOver.GameOver | death | Flash | **room** | listed [verify] |
| CharacterRecord | GUI_SF_CharacterRecord.PC_CharacterRecord | the wheel, `ShowSquadGui` | Flash + Shepard in the UI world | **room**, the 3D model missing | with Inventory |
| Credits | GUI_SF_Utility.Credits | the end | text | flat | keep |
| DesignerUI | GUI_SF_HUD.DesignerUI | designer minigames and timers (layouts: Quasar, Mira's Hanoi, the laser, the toxin cure; the bomb disarm's timer); always open, hidden until used | an overlay over the live world | **room** in mode 7 while visible, toward what was used | listed [verify] |
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
