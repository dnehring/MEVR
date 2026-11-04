---
name: game-screen-to-hologram
description: Turns one of LE1's full-screen game screens (a GUI-mode Scaleform screen like the shop, loot, equipment, journal, codex, map, or a mode of its own like the galaxy map or the bypass minigame) into an in-world hologram in VR, reusing the game's own movie, 3D scene and props, from finding the screen and reaching it without a save to placing it, the pointer and the in-game check. Use when a game screen still shows on Halcyon's flat panel, when adding a screen to [WorldUi], or when the user asks to replace a full-screen experience with something in the world.
---

# A game screen to a hologram (LE1)

Two screens are done and are the templates: the **galaxy map** (research/51 section 8: the game's own 3D map scene captured in
stereo onto the projector's disc, its Flash on the same plane) and the **world UI** (research/53 section 4: any GUI-mode screen's
Flash on a panel standing in the room, the level behind it; the shop at its vendor). research/53's table lists every screen,
how it opens and the plan; start there and update its row when you're done.

**The rule (the user, 2026-11-04): reuse the game's own assets.** Show the game's movie, its 3D scene, its props, its message
boxes; open and close through the game's own functions; never redraw what the game draws. Our own drawing only where the game
has nothing (the galaxy hologram on foot, before the map opens).

## 1. Find the screen

| What | How |
|---|---|
| Every screen the game has | `tools/console.ps1 "mevr classes BioSFHandler"`; the library (tag, movie, handler) is `BioSFManager.HandlerLibrary`, printed by `mevr classes BioSFManager` then `mevr props 0` (research/53 section 3 has it) |
| What's open now | `mevr panel list`: each panel's tag, full screen, focus, visible, handler |
| The game mode | the mod's log (`not on foot (game mode 7)`), Halcyon's `[MENUCTX]`; 7 GUI, 9 galaxy, 10 minigame |
| How it opens | the Kismet action (`mevr classes BioSeqAct_Show`, `BioSeqAct_OpenShop`...), the used object (`mevr find`, `mevr origin`), or the game mode's `Show<X>Gui` (`mevr gui Equipment\|Squad\|Codex\|Journal\|Map\|Menu`) |
| Fields and functions | `mevr find <name>` then `mevr props <n>`, `mevr funcs <n> <text>`; the SDK headers (`external/LExSDKv2/Src/LESDK/LE1/SFXGame_classes.hpp`, read only, Grep with an absolute path) for names and offsets |
| Its Flash | `mevr panel <tag> var <path>`, `invoke <method>`, `movie` (the GFx to `%LOCALAPPDATA%\MEVR\galaxy\<tag>.gfx`), `show`/`hide` |
| Native code | `tools/ghidra/decompile.ps1` (never dumpbin) |

## 2. Reach it without a save

The fake headset (`tools/fake-headset.ps1 -On`), Steam's launch with no Enter
(`steam.exe -applaunch 1328670 -game 1 -autoterminate`, wait for `framesRunning` in `testloop.status`), then from the title:
`open BIOA_NOR00` (the Normandy; not NOR10), `streamlevelin <sublevel>` for decks that aren't streamed (the cargo hold:
`BIOA_NOR10_11_*`), `mevr setloc X Y Z` and `set BioPlayerController Rotation (Pitch=0,Yaw=..,Roll=0)`, then open the screen:
`mevr activate <n> 0` on its Kismet action (the requisitions officer's shop: `RequisitionOfficer.StoreManager.BioSeqAct_OpenShop_1`),
`mevr used <n>` on an art placeable's behaviour (the galaxy console), `mevr gui <X>`, `mevr use <n>` on a container's actor (what A
does). `mevr instances <Package.Class> [text]` finds the live objects of a class and where their actors stand (crates:
`SFXGame.BioArtPlaceableBehavior`, their `m_oActorType` says encrypted console or ammo case). The bypass minigame opens anywhere:
`mevr skillgame start 0 1 <n>` (the level's own `BioSkillGame.InitializeSkillGame` on the (n-1)-th found placeable), closed with
`mevr panel SkillGame invoke PreGameButtonBOnPress`. Synthetic calls that skip the game's flow close at once (`ShowLootGui` alone,
`LootCorpse` on a living pawn); loot needs a mission past its prologue (research/53 section 5 lists Eden Prime's consoles and
cases). A movie with no MovieInfo at run time: `mevr panel <tag> movie <Package.Name>` (its strings name the buttons' functions). To close a screen without input, reload the level
(`open BIOA_NOR00`). A hot-reloaded mod loses what it recorded on foot: reopen the screen after a rebuild.

## 3. Look at it as VR shows it today

`tools/snapshot.ps1 -Delay 1`: `quad0.png` is Halcyon's flat panel (the whole window); measure the movie's stage and content from it
(LE1's movies are 16:9 stages letterboxed in the window: `world_ui::Stage`). The world layer of a GUI screen is the game's **UI
world** (a backdrop stage, `GUI_UIWorld`), not the level, while any of its panels is `bFullScreen`.

## 4. Choose the presentation

| The screen | Do |
|---|---|
| Flash only (shop, loot, journal, codex, map, the main wheel, message boxes, the minigame) | every screen in `world_ui::Screen`/`kTags` is in the room by default (`screens = all`); a new tag goes in the table (append: the values travel host to mod); measure its crop (`DefaultCrop`, stage UV); give it an anchor in `game/WorldUi.cpp` `AnchorOf` if it belongs to something (the shop: its vendor; loot: the inventory's owning actor; the minigame: `m_oDependent`; else what A would have used) |
| A 3D scene of its own (the galaxy map) | two scene captures from the game's camera (`game/SceneCaptureRig`, core/vr/CineScreen's parallel cameras and crop) on a stereo window; its Flash on the UI panel on the same plane; the view held at Shepard's at render time |
| A small tool the player holds up (the bypass minigame: the omni-tool) | projected from the left wrist: `[WorldUi] wrist` lists it; the host's `WristPlace` each frame from the HUD's wrist point (bottom edge above the wrist across the line of sight, or the screen hides the wrist), a tighter `WristCrop`, `Unfold` on opening, the fan of light (plugin/WristBeam); in the room without a tracked left controller (research/53 section 6) |
| A 3D model in the UI world (equipment, character record) | in the room without the model for now (the room hides the UI world); the plan is the model as a hologram beside the panel (research/53) |
| A prop in the level that already shows it (the CIC projector) | measure the prop live (location, rotation, the mesh component's bounds: research/51 section 8.1's radius method), lay our layer on it, hide the prop's own picture while ours shows |

## 5. The plumbing (what each piece is for)

- **Mod** (game thread, hot-reloaded): detect the screen, find its anchor, convert to tracking space with the frame recorded **on
  foot** (`view_anchor::Current()` while mode 0; a GUI screen's camera may be elsewhere), publish each tick through HostServices.
  Engine writes are journalled (`write_journal::Wrote`, or a `FieldRef::WriteBool`, which journals); ProcessEvent only through
  `ue3::runtime::Call`; every new class, field or function gets a ReflectCheck row.
- **Host**: place the panel once per opening, fixed in the room (`world_ui::Place`, or `galaxy_holo::DiscPose` for a tilted
  surface), draw it as a premultiplied quad, feed the game pointer the whole window on that plane (`world_ui::WholeWindow` +
  `game_pointer::NoteGameUiQuad`). A HostServices struct that grows means host and mod deploy together and the version goes up:
  tell the other sessions (ListAgents, SendMessage) and update the `host-mod-match` memory.
- **Halcyon seams** (`[MEVR]`, declared in `mevr_bridge.h`): render the mode as on foot (galaxy: `GalaxyHologram`/`GalaxyHeld`;
  GUI: `liveWorldGui` from `WorldUiActive`), hold the view at render time (`GalaxyViewSwapBegin/End` around the main view's
  `CalcSceneView`, which takes either hold). Never write the held view into `CameraCache.POV` after `UpdateCamera`: the game's Flash
  labels and picks project through the camera it keeps.
- **Settings**: a `[Section]` block in `game/HandCalibration.cpp` with all three append chains (the default file, the
  `std::pair{"X", kXBlock}` list, `gX = ReadX(ini)`); a value the player tunes is also a key the existing file gets.

## 6. The hologram look

Premultiplied alpha, the dark parts at a backdrop opacity, the bright parts solid (the galaxy disc: `max(brightness, backdrop)`;
the world UI's panel: `alpha * lerp(backdrop, 1, saturate(4 * max channel))`); a disc mask in the quad's own UV when the game's
prop is round. The compositor's quads have no depth: a layer draws over whatever stands in front of it in the level (the console's
front panel over the galaxy disc), so keep panels where nothing stands between them and the eyes, or below the face of whoever
they belong to (the shop at counter height).

## 7. Verify in the game

1. Snapshot each state and read the PNGs: `left.png` (composited), `left-world.png`, `hud.png` (the panel, straight alpha).
2. Both eyes: a backdrop at infinity looks identical in both (`left-world` vs `right-world` difference near 0); the level doesn't.
3. The pointer: `tools/make-galaxy-pick-recording.py --snapshot <snap.json> <label> out.mevrrec --panel-uv U V --layer hud`, then
   `tools/replay.ps1 -Path out.mevrrec -Inputs`; the log's `game pointer: ... at (x, y) px` must be the button's pixel in the flat
   frame. A click needs the game window in front: on the test rig SteamVR's `vrmonitor` often has the focus and Windows refuses
   the game's `SetForegroundWindow` (`game window to foreground: refused`), so a missed click there isn't a mapping bug.
4. Offline variants of a look (opacity, mask) from one snapshot: re-composite `left-world.png` with the quad ray-cast through its
   pose (the galaxy backdrop's script did this) and show the user a side-by-side.
5. Record what you measured in the screen's research note (numbers, snapshot names), update research/53's table, CLAUDE.md's
   bullet, and commit (after `build/`, `build-mono/`, `build-trilogy/`, `mevr-tests`, `tools/check-spdx.py`).

## Pitfalls already paid for

| Pitfall | Where it's written |
|---|---|
| `SFXGameModeGUI.StackedGuis` isn't the open screens (it held `None` with the shop open); read the GUI manager's panels | research/53 section 1 |
| A `bFullScreen` panel renders the UI world instead of the level | research/53 section 1 |
| The equipment and character record's 3D models live in the UI world | research/53 section 1 |
| The shop's BioShop actor stands beside its vendor, not on him (17 degrees off at 1.6 m) | research/53 section 4 |
| ProcessEvent skips natives with an opcode (`SetLocation`, `Trace`...): call through `ue3::runtime::Call` | docs/knowledge/gotchas/process-event-skips-opcode-natives.md |
| Clicks posted from the Present callback are ignored by Flash; the pointer posts from a worker thread | research/51 section 8 |
| A held trigger blocks a Flash click: the pointer keeps the pad's triggers from the game while it points | research/51 section 8 |
| `Windows.h` macros (`SetProp`, `far`) | docs/knowledge/gotchas/windows-h-macros.md |
| An inline edit script loses its backslashes | docs/knowledge/gotchas/bash-heredoc-backslashes.md |
| A mod reload restarts counters the host keys on: seed them from the clock | game/WorldUi.cpp `opening` |
| A screen open across a mod reload stays on Halcyon's panel (the reloaded mod hasn't seen Shepard's view on foot): close it, reopen | research/53 section 6 |
| A picture for an sRGB swapchain: premultiply in linear light, then encode (premultiplied encoded values darken what's behind) | core/vr/WorldUi `BeamPixels` |
| A panel facing eyes above it, lifted straight up from the wrist, hides the wrist: lift it along its own up | core/vr/WorldUi `WristPlace` |
