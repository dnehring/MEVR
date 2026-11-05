<!-- SPDX-License-Identifier: MIT -->
# 54. The omni-tool menu: every game screen from the left wrist (2026-11-04)

The user, after seeing the equipment and squad screens lose their 3D figure in the room (research/53 section 1): "I actually
want to see if we can make the entire menu system available via omni-tool menu like what the VR mod session was able to
accomplish when looking at the hacking menu for crates", with concept art (a wrist projector, a tab rail on the left: Mission,
Map, Inventory, Abilities, Squad; a large centre panel touched with the right fingertip; a detail panel on the right; the squad
as small holograms on a disc), then: "we do need a menu on the left side to allow selecting all of the different menus".

## 1. What already existed (research/53 section 6)

- The wrist projector: `[WorldUi] wrist` lists the screens projected from the left wrist (`WristPlace`, unfolding, a fan of light
  from the omni-tool's display, riding on the wrist). Until now only `SkillGame`.
- Touch: the right index fingertip presses the game's screens (research/53 section 6.2, `vr::game_pointer`), the laser for the rest.
- Shepard's forearm wears the game's own omni-tool (`game/OmniTool`) while a wrist screen is up.

**The experiment (fake headset, a synthetic replay with the left wrist raised, snapshots 2026-11-04 20:41-20:43,
`wrist = all`, `wristWidthM = 0.50`):** Equipment (`Inventory`), Journal and Map read well projected from the wrist at 0.50 m wide,
the orange gauntlet glowing on the forearm. Opening a second screen with `mevr gui` while one was up stacked panels (several
`MenuBrowser`s, the Codex and the pause menu ending on Halcyon's flat panel): see section 3.

**A lesson paid for with the user's mouse:** with a replay that carries controller poses, a screen that falls back to the flat
panel puts the game pointer on it, and its `SetCursorPos` moved the real Windows cursor with the replay's sway until the replay
was stopped. Replays with controller poses near flat screens need `[Pointer] setCursor = 0`, or none.

## 2. The design

- **Every personal screen goes on the wrist by default.** `[WorldUi] wrist` defaults to `SkillGame, Inventory, XMods,
  CharacterRecord, Specialization, Journal, Codex, Map, MenuBrowser, Options, Save, Load` (an old file's `SkillGame` alone, the
  old default, moves once; the marker is `wristMenuWidthM`). **A screen a use opened** (a locker's equipment: the world UI's
  anchor, research/53) **still stands by what was used**; the bypass minigame always goes on the wrist (`world_ui::OnWrist`). The
  shop, loot, party selection, message boxes and the rest stay in the room. `wristMenuWidthM` (0.55) is the width of a whole
  16:9 stage there; `wristWidthM` (0.30) stays the minigame's frame. Our menu's Comfort > Game screens row "Omni-tool screens"
  (On my wrist / In the room) writes the list or `none`.
- **The rail** (`core/vr/OmniRail`, host `plugin/OmniToolRail`, `assets/menu/omnitool.rml` + `.rcss`): a column to the left of
  the wrist screen, in its plane, as tall as it (`kRailAspect` 0.36: its 360 x 1000 canvas), `kGapM` from it; entries Squad,
  Equipment, Map, Journal, Codex, Computer (the main wheel: options, save, load, quit), Close. The screen up lights its entry
  (`ForScreen`: Inventory/XMods Equipment, CharacterRecord/Specialization Squad, MenuBrowser/Options/Save/Load Computer). Drawn by
  a **second RmlUi context** of the main menu's library (`menu::Ui` now lets a second document have a context and renderer of
  its own; RmlUi and the fonts are the first's), in the main menu's style in the omni-tool's orange, the menu's icon decorator
  with new icons (squad, equipment, map, journal, codex, close). Its pointer is `vr::game_pointer::Pointer` on its quad: a ray
  with the trigger, or the index fingertip's touch; its trigger is kept from the game while it's pointed at
  (`omni_rail::OwnsTriggers` in the XInput hook).
- **Opening it:** tap the left wrist (the HUD's wrist piece, where the omni-tool's display is) with the right index fingertip:
  the fingertip coming within 3.5 cm from farther than 7 cm, at most every 0.6 s, not while a gun is drawn (the mod's gun
  panel shows), on foot or in the game's screens (`vr::omni_rail::Tap`). Alone it stands where a wrist screen would, 0.26 m
  tall, with an OMNI-TOOL header. It also shows by itself beside any of its screens the game's own keys or wheel opened. A tap
  while it shows, or its Close, closes it and the screens.
- **Opening a screen** (the mod, `game/GameMenuRequest`, through the main menu's request slot, HostServices v19's
  `takeGameMenuRequest`: new `game_menu::Action` values 3..8, unknown ones None on an older mod): the screens' functions are
  declared on `SFXGameModeBase` only (`ShowEquipmentGui`, `ShowSquadGui`, `ShowJournalGui`, `ShowCodexGui`, `ShowMapGui`; LExSDKv2's
  names: no mode overrides them), so in game mode 7 they open only from the game's Flash wheel. The rail therefore **backs out of
  the screen up first**: B (`BIOGUI_EVENT_BUTTON_B`, 13) to the focused panel's handler (`BioSFHandler.HandleInputEvent`, as
  `mevr panel <tag> input 13` does), 4 ticks apart, at most 6 times, until the game leaves mode 7; then calls the screen's
  function with `bShow` on the mode that has the input (on foot or the Mako), as its key would. Asking for the screen that has
  the focus does nothing. Computer is the pause menu's existing path (`ShowMenuGui` on the active mode).

## 3. Open, [verify] in the game

- Whether the backing out leaves mode 7 cleanly from each screen (a screen over the main wheel takes two Bs) and whether the
  screens then open without stacking. Log lines: `omni-tool: backing out of '<tag>' (B, n of at most 6) for <screen>`,
  `omni-tool: opened <screen> (<mode>.<function>(true); ...)`, `omni-tool: ... the game stayed in its GUI mode after N backs out`.
- The tap's distances on a real wrist; whether the HUD's wrist point is where the player sees the display.
- The rail's size and legibility at 0.42 m (entries 112 dp of a 1000 dp canvas: about 3 cm tall on a 0.31 m rail).
- **The squad as holograms** (the concept art's disc): the equipment and character record's figure lives in the game's UI world
  (research/53 section 1). A scene capture inside that world, shown on a disc beside the wrist screen, is the route
  (`game/SceneCaptureRig`, as the galaxy map's window does); not built.
- Party selection (choosing the squad) exists only on the Normandy and at missions' starts (`PartySelect`, in the room).

## 4. The squad holograms (the concept art's disc): the approach (2026-11-04)

- **Why not a capture of the UI world.** `BioUIWorld` (SFXGame) is a separate engine world: a native `m_pWorld` pointer, its own
  camera actor (`m_pCameraActor`), the pawns it spawned from the party (`m_mSpawnedPawnMap`), its own post-process chain. A scene
  capture renders with its scene's main render, and the game renders the UI world only while a `bFullScreen` panel is up, which the
  world UI clears to keep the level. Drawing the UI world into a texture of ours would need native calls not mapped yet (Ghidra was
  locked by another session).
- **The game's own hologram effect.** Every character material has a `CombinedEffect_EMT_HoloWipe` variant (e.g.
  `EffectsMaterials.Users.HMM_ARM_MASTER_MAT_USER.CombinedEffect_EMT_HoloWipe` in every armour and head package), the
  `EEffectsMaterialType` value 30 (`Engine.BioEffectsMaterials`), switched on a pawn by `BioPawn.SetEffectsMaterialType` and
  `SetFractionOfEffectsMaterialEnabled` (and on `SkeletalMeshActor`): how ME1 turns characters into holograms (comm calls, VIs).
- **The plan:** a miniature copy of the party member the screen shows (Actor.Spawn with the real pawn as its `ActorTemplate`, so its
  armour and headgear come along; collision and physics off; `SetDrawScale` about 0.12, ~20 cm), switched to EMT_HoloWipe, standing
  on a disc beside the wrist screen, rendered by the game in the level in both eyes. Open: whether a template spawn of a BioPawn
  brings its appearance and stays inert (no AI, no squad), its pose while the GUI modes freeze non-player actors (`bPlayersOnly`, the
  omni-tool's lesson: its components may have to ride on Shepard's pawn), and following the wrist each tick.
- **The experiment's verbs** (`game/HoloProbe`, a dev tool): `mevr holo spawn [self|n] [scale]`, `mevr holo effect <type>
  [fraction]`, `mevr holo scale|move|info|clear`.
