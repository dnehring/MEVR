<!-- SPDX-License-Identifier: MIT -->
# 56. The omni-tool menu: every game screen from the left wrist (2026-11-04)

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

- ~~Whether the backing out leaves mode 7 cleanly~~ **Checked 2026-11-04 on the fake headset** (BIOA_UNC20, replayed wrist poses):
  B sent to the focused panel's handler (`BioSFHandler.HandleInputEvent(13)`) never closed the equipment screen (six tries). A call
  trace of the game's own B (a replayed press, `mevr calltrace 15 handleinput inventory close back exit pop remove leave -update
  ...`): `SFXGameModeBase.GuiKey` -> `BioSFManager.HandleInputEvent` on `MassEffectGuiManager_0` -> `HandleInputEvent` on every
  handler on the stack (`BioSFHandler_SharedIGG` twice, `XMods`, `PCInventory`) -> `BioSFManager.RemovePanel` from
  `MassEffectGuiManager.HidePauseMenu` and `HideBrowserWheel` -> `OnPanelRemoved`. The back-out now calls `GuiKey(13)` on the active
  mode (`SFXGameModeGUI`): equipment -> journal -> map -> squad -> codex -> pause menu -> closed, one B each, the next screen 10-25 ms
  later; the pause menu needed the same back-out (`ShowMenuGui` returns 0 over a game screen). A fingertip press on the rail's Map
  (a replay) went rail -> host request -> mod -> map. The world UI counts a screen taking over inside mode 7 as a new opening (the host
  had kept the first screen's label, so the rail highlighted it). Log lines: `omni-tool: backing out of '<tag>' (B through
  <mode>.GuiKey, n of at most 6) for <screen>`, `world ui: <screen> takes over from <screen>`.
- **The rail alone sits at the controller's wrist, the gauntlet where Shepard's arm puts it** (about 11 cm lower and nearer on the
  fake headset's poses, the arm-length scaling): screens project from the omni-tool's display (`State::emitter`), the rail alone
  doesn't yet (no state is published without a screen). [open]
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
- **The template spawn crashes the game (2026-11-04 23:05, fake headset, BIOA_UNC20, the equipment screen up in mode 7).**
  `mevr holo spawn 1 0.12` (Actor.Spawn on the controller with SpawnClass = the squadmate's class and ActorTemplate = the
  live squadmate) ended the process with an access violation in ucrtbase.dll (offset 0x17645, a memory copy; WER event 1000),
  before any line of the spawn's report and without crash breadcrumbs (WER took it first). A live actor isn't an archetype:
  Spawn copies its properties (arrays, components, controller links) as if it were one. **Don't spawn from a live pawn.**
  Next routes to try: the game's own UI-world spawning (`BioUIWorld`'s pawn map is filled from the party's appearance;
  how is [verify] in the decompile), or a `SkeletalMeshActor` spawned bare whose components get the squadmate's meshes
  and materials (body, head, hair) copied by assignment. Whether mode 7 mattered is open (try on foot first next time).
- **Decision (the user, 2026-11-04): what the game provides.** One figure, the party member the screen shows (as the game's own
  Equipment and Squad screens show one), changing when the screen's selection changes. No new squad UI for now: the concept
  art's whole squad on the disc (the selected member brighter) and picking a member by touching a hologram wait.
- **The experiment's verbs** (`game/HoloProbe`, a dev tool): `mevr holo spawn [self|n] [scale]`, `mevr holo effect <type>
  [fraction]`, `mevr holo scale|move|info|clear`.

## 5. The game's own art: the GFx reader, rasterizer and fonts (2026-11-04)

The owner rejected hand-drawn imitations of ME1's look for the rail (geometry, fonts, bulk, icons, colours all off) and chose
the game's own Scaleform art, read from the player's install at run time. Nothing from the game is committed or shipped: the
library only reads bytes it's handed, the tool's PNGs and TTFs stay in a scratch folder, and the tests build their movies
byte by byte.

**What exists** (game-independent, `src/core/gfx`, in the host's source list and the tests'):
- `Swf`: `Movie::Parse` over an uncompressed `GFX`/`FWS` byte span (a compressed `CFX`/`CWS`/`ZWS` fails with a message: no
  zlib in the project). Shapes (DefineShape 1-4: solid, linear, radial and focal gradients, bitmap fills recorded, LINESTYLE2,
  new style tables), **morph shapes** (DefineMorphShape/2, `MorphAt(ratio)`), sprites played into per-frame display lists
  (PlaceObject 1-3 move/replace/matrix/colour transform/ratio/name/clipDepth/visible, filters and blend modes parsed past,
  RemoveObject/2, FrameLabel), DefineScalingGrid, ExportAssets, **ImportAssets/2**, DefineEditText bounds, DefineFont2/3.
  `FrameOf(sprite, label)` (0-based), `Exported(name)`. Bounds-checked reads; a bad tag is counted and skipped.
- `Raster`: exact-area coverage (signed-area accumulation, written from the math), nonzero per fill style from the
  fill0/fill1-oriented edges, curves flattened to 0.05 px. One style table's fills are added up before they meet the
  canvas, so two fills sharing an edge leave no see-through seam. Strokes (round/bevel/miter joins, round/square/no caps,
  at least 1 px). Clip layers mask the depths above them (fills only, as Flash) and never paint; nested clips intersect.
  `Render(movie, id, frame, scale)` (tight bounds + 2 px), `RenderFit(movie, id, frame, w, h, insets)` (a nine-slice from
  DefineScalingGrid, else the insets; `FitOptions::cornerScale` scales the corners), `Bounds`, an outer
  `ColorTransform` in the options. Images are premultiplied RGBA8, R in the low byte.
- `SwfFont`: `FontAsTtf(movie, fontId, family)` builds a TrueType file (cmap 4, glyf/loca long, head, hhea, hmtx, maxp, name,
  post 3, OS/2 v4, kern format 0) from the glyphs U+0020..U+007E plus what the font has of U+00A0..U+017F (`everyGlyph` for
  all); quadratic edges map straight to on/off-curve points; outer contours clockwise, decided once per font from the
  fill1 convention; checksums right. `Fonts(movie)` lists them.
- `tools/gfx-dump` (`mevr-gfx-dump`): `list`, `render`, `frames` (every labelled frame stacked), `fit`, `shape`, `uses`
  (where a character is placed, with its colour transform), `font`, `sample` (FreeType draws text from the TTF);
  `--bg=RRGGBB`, `--alpha=N`.

**Verified on the player's movies** (copies in a local scratch folder; PNGs looked at, not kept):
- All four movies parse with 0 bad and 0 unknown tags: GFx version 8, stage 1280 x 720, 30 fps. The mix: DefineShape 1-4,
  many DefineMorphShape/2 (101 + 42 in PCInventory), DefineEditText, ImportAssets2 (`$AeroLight`/`$BioMass` from
  `gfxfontlib.swf`, the Xbox button icons from `Xbox_ControllerIcons.swf`, `MainController` from `mainController.swf`),
  CSMTextSettings, DoAction/DoInitAction, and Scaleform's DefineExternalGradient (1003; ignored: every gradient fill still
  carries its own stops) and one DefineExternalImage2 (PC_Journal's only bitmap fill, shape #106).
- **PC_Journal's `leftTab` (sprite 59, 260.9 x 41.1 px, no scaling grid)**: 45 frames, labels (0-based) primaryNormal@0,
  grow@1, primaryMouseOn@9, shrink@10, primaryMouseOff@19, selected@20, deselectShrink@21, deselected@24, selectedNormal@25,
  selectedGrow@26, selectedMouseOn@34, selectedShrink@35, selectedMouseOff@44. Normal is shape #42 (dark teal gradient, a
  light blue accent stripe on the bevel), mouse-on #48 (orange gradient) with morph #49 (the yellow accent), selected
  #51/#52, selectedNormal #54/#55, selectedMouseOn #56/#57 and the tweens are **morph shapes**: without DefineMorphShape
  half the states drew nothing. All 13 render as the game's (teal, orange on mouse-on, lighter teal when selected,
  gradients included). Fitting it to the rail's 324 x 64 keeps the bevel's angle with insets left 50, right 15, top and
  bottom 0 and `cornerScale` 64 / 41.1 (only the straight middle stretches); vertical insets bend the diagonal (any
  nine-slice does), a plain stretch skews it.
- **PCInventory**: #324 (DefineShape4, 576.5 x 282.3) is the item list's frame with its double blue outline and gradient
  well. Sprite 517, the whole equipment screen (90 frames, four clip layers, `detailmaskleft` among them), renders with its
  panels and icons, the clip layers' red and green rectangles never painted, its details frames clipped. **#510 (306.4 x
  324.3) has alpha 0 in every colour** (gradient stops and lines) and is placed once (sprite 517 frame 76) with no colour
  transform: it is invisible in the game. `--alpha=255` (`RenderOptions::color.aa`) shows its geometry; the earlier Python
  sheet showed it only because it dropped alpha.
- PC_MainWheel #29 is the squad figures icon.
- **Fonts.gfx**: three DefineFont3 (flags 0x8c: layout, wide offsets, wide codes; EM 1024 x 20): 1 `Biomass_jpn` (7255
  glyphs, ascent 1024, descent 205, leading 205, 6690 kerning pairs), 3 `Tigensu_jpn` (7345, 1040 / 188 / 204, 1040 pairs), 5
  `Myriad Pro+DFGGothicP-W5` (7840, 900 / 256 / 132, 1959 pairs), all with full ASCII. The Latin TTFs have 319 glyphs, 39-53
  KB, built in 2-4 ms; FreeType loads them (family, kerning flag, metrics) and draws "Equipment" in Biomass and "Damage Per
  Second" in Tigensu as the game does; FreeType applies the font's kerning (Myriad's AV -50 and space-T -38 of 1024 units,
  through `FT_Get_Kerning`). Parsing all 22,000 glyphs of the 7 MB file takes 100-150 ms (once, when the cache is built).
- Timing (Release, one thread): the tab 1.5-2 ms at 2x, #324 9 ms, the whole equipment screen about 30 ms at 1x.

**The rail in the game's art** (`plugin/RailArt`, 2026-11-04). The mod (`game/GameArt`) copies the movies' `RawData` to
`%LOCALAPPDATA%\MEVR\gameart\<Package.Movie>.gfx` 90 ticks into a level (packages not in memory loaded through
`DynamicLoadObject`; a file rewritten only when its size changes). The host reads three of them when the rail's document is made
and whenever they change (looked at every 2 s): the fonts (`FontAsTtf`, registered with RmlUi from memory as "ME1 Biomass",
"ME1 Tigensu", "ME1 Myriad"), `leftTab`'s primaryNormal, primaryMouseOn, selectedMouseOff and selectedMouseOn frames at 300 x 60
(insets left 50, right 15, `cornerScale` 60 / 41.1), and #324 at 360 x 620 with insets left 262 (the side bump and the whole
title notch, about 200 x 30, at the game's size: squeezed by a 60 px inset it held no title), top 60, right 60, bottom 50. All
of it in 158 ms, once (the fonts' 7 MB parse is most of it). The document's body gets `art` only when all three loaded; the RCSS
(`body.art ...`, `box-sizing: border-box` since the textures are exactly the boxes' sizes) puts the title in Biomass in the
notch, the entries in Tigensu on the tabs, no icons (the game's tabs have none). Offline look:
`mevr-menu-preview --gameart <folder>` (`rail-alone`, `-squad`, `-hover`, `-equipment.png`). [verify] in the game: the cache
written on a real level, the art picked up without a restart, legibility of 26 dp Tigensu at 0.42 m.

**Not drawn** (counted in `RenderNotes` for the log): bitmap fills (transparent), edit texts and static texts (DefineText;
none in these movies), buttons, imported characters (resolve them from the named movie's exports), filters (glow, drop
shadow, blur) and blend modes other than normal (drawn as normal; layer groups aren't composited as a group). Nested sprites
show their frame 0 unless `playNested`; ActionScript doesn't run, so states a script sets (text, `gotoAndStop` on children,
visibility) aren't applied. Stroke scaling flags and pixel hinting are ignored.

Tests (`tests/GfxTests.cpp`, synthetic movies only): parsing and labels; a hole stays empty and a half-covered edge pixel is
half; gradient end colours; the clip layer hides what it should and never paints; a colour transform; the nine-slice's corner
unscaled (and at `cornerScale` 2); a 2 px stroke; a morph at ratio 0.5; the import counted; the TTF's checksums and
directory; FreeType: family, metrics, advances, the kerning pair, an 'O' with ink on the ring and none in the hole; every
truncation of the movie and 400 random byte flips parse, render and convert without crashing.
