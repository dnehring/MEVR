---
type: System
title: ME1 settings files and shipped defaults
description: Where LE1's settings live (ASI\LE1MEVR.ini, MELEVR.ini), how the host and the mod read and write them, the migrations that ran once, and one table of every shipped default that shapes behaviour.
tags: [me1, catalog, settings, defaults, ini, migrations]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- One text file, `Binaries\Win64\ASI\LE1MEVR.ini`, holds every setting of the mod. It is written with comments on the
  first start and can be edited while the game runs: a change applies within a second.
- The in-game menu edits the same file key by key. A slider is saved 300 ms after it last moved, a toggle at once.
- Halcyon's renderer keeps its own file next to it, `MELEVR.ini`, with four profile slots. The menu's Advanced page edits
  the live profile and saves it when the menu closes.
- A fresh install plays as the owner's own tuned setup: nothing has to be changed before the first session.
- A setting whose default changed later moves once in an old file, only if it still held the old default. A value the
  player set is kept, and the log says so.
- An ASI folder that cannot be written does not stop the game: the defaults apply, the menu's rail shows "Can't save
  settings".
- The renderer runs in one supported configuration (life-size stereo, first person, no head aim) whatever `MELEVR.ini`
  says.

# Contracts

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| SET-01 | The host and the mod read one file, `<exe dir>\ASI\<GAME>MEVR.ini`, whatever the host ASI is called. | `LE1MEVR.ini` on LE1 | every game; the name comes from `MEVR_GAME_NAME_W` | [SettingsFile.hpp](../../../src/engine/SettingsFile.hpp) `SettingsFile`; host `MevrBridge::SettingsPath` | code read |
| SET-02 | Section and key names are case-insensitive; the first occurrence of a section or key wins; a UTF-8 BOM is stripped; a UTF-16 file is decoded and never appended to with narrow text. | as Win32's profile API | every reader of ours | [Ini.hpp](../../../src/core/Ini.hpp), [Ini.cpp](../../../src/core/Ini.cpp) `Parse`, `Detect`, `Decode` | unit test (`tests/Main.cpp` TestIni) |
| SET-03 | Numbers are read and written without the C locale: `0.5` is 0.5 on a decimal-comma system. Paths become text through `ini::Utf8`, never `path.string()`. | `std::from_chars` / `to_chars`, `ini::Fixed(value, decimals)` | our code; Halcyon's own readers still use `sscanf %lf` / `atof` | [Ini.hpp](../../../src/core/Ini.hpp) `Number`, `Fixed`, `Utf8` | unit test |
| SET-04 | A missing file is created by the mod on its first tick with every block and its comments. A section missing from an older file is appended as a whole block; a key added to an existing section later is appended alone with its default. | blocks: Hands, Arms, Controls, Holsters, Squad, Sights, Throwables, Comfort, Interact, Vent, Hud, Mako, Cockpit, Galaxy, WorldUi, Game, SquadContext, SquadOrders, Voice; modules append their own (PowerWheel, Cover) | mod loaded; skipped for a UTF-16 file's narrow appends | [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) `Refresh`, `AppendBlock`, `kNewKeys`; [PowerWheel.cpp](../../../src/game/PowerWheel.cpp) `kBlock`; [VrCover.cpp](../../../src/game/VrCover.cpp) `kBlock` | fake headset |
| SET-05 | The mod re-reads the file when its write time changes, checked once a second. | 1000 ms; 10 000 ms while the file cannot be created or read | always | [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) `Refresh` (`gLastCheckMs`, `gStamp`) | code read |
| SET-06 | Modules with their own reader poll on their own clock. | 1 s: RenderLevers `[Performance]`, the menu's `[Menu]`, PowerWheel, Cover; 2 s: `[Body] scale`, `[Debug] requests` | per module | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp), [BodyScale.cpp](../../../src/game/BodyScale.cpp) `ReadSettings`, [DevChannels.hpp](../../../src/core/DevChannels.hpp) `Allowed` | code read |
| SET-07 | The menu writes one key at a time (`WritePrivateProfileStringW`), never the whole file. A slider's value is written once it has rested; until then a re-read shows the pending value, so a dragged slider never jumps back. | 300 ms after the last move; toggles and chips at once | menu open; flushed when it closes | [MenuSettings.hpp](../../../src/core/vr/MenuSettings.hpp) `PendingWrites`; [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) | unit test (`tests/Main.cpp`) |
| SET-08 | Every section a menu row reads is loaded before the rows are filled. A section missing from that list makes its rows snap back to defaults after a write. | 25 sections in `Settings::Read` | menu | [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) `Settings::Read` | fixed f1f8d8b |
| SET-09 | `[Menu]` is written by the host the first time it finds no `pause` key: its four keys, each on its own. Values are clamped on read. | `pause = 1`, `distanceM = 1.30` (0.6..3.0), `widthM = 1.45` (0.6..3.0), `assets =` | host; re-read every 1 s | [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) `ReadMenuSection` | code read |
| SET-10 | An unwritable ASI folder: the mod warns once, applies the built-in defaults and retries every 10 s; the menu shows "Can't save settings" in its rail. | 10 s | file cannot be created | [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) `Refresh`; `save_msg` in [MenuSettings.cpp](../../../src/core/vr/MenuSettings.cpp) | `[verify]` in the game |
| SET-11 | A default that changed moves once in an existing file, only where the file still holds the old default exactly; a marker key (a key added in the same change) makes it happen once. Any other value is logged as kept. | see "Migrations" below | mod's `Refresh`, before the new keys are appended | [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) `Refresh` | code read |
| SET-12 | Halcyon's `MELEVR.ini` holds every setting per profile slot (Default, Custom 1-3). A slot missing from the file loads Halcyon's code defaults (Insert, head aim on, third person), so the fresh file writes all four slots alike. | 4 slots, 22 lines each | fresh install or deploy with no file | [Ini.cpp](../../../src/install/Ini.cpp) `HalcyonReleaseIni`; `tools/deploy-asi.ps1` | fake game (`tools/setup/test-fake-game.ps1`) |
| SET-13 | An existing `MELEVR.ini` is fixed on every install, repair and deploy: `menuKey = 220` and head aim off in every slot, the F-key profile hotkeys off, `firstRunDone = 1`, `hudPanelFollowDeg` 50.0 to 70.0, `sfr2Metric = 1` where missing. A slot that has none of MEVR's own keys counts as foreign and also gets `vrMode = 2`, `fpEnabled = 1`, `mirrorPresentEvery = 2`, `depthPopKeyEnabled = 0`. | MEVR-only keys: `sfr2Metric`, `sfr2MetricDepth`, `sfr2MetricEyeFrustum`, `worldLockedPanels`, `recenterTiltDeg`, `hdrPaperWhiteNits` | Setup and deploy-asi | [Ini.cpp](../../../src/install/Ini.cpp) `FixHalcyonIni` | unit test (install) |
| SET-14 | The renderer's supported configuration is pinned after `LoadFromIni` and `LoadProfile`, and by the menu's Reset, whatever the file says. The log names what was ignored. A changed mode rewrites the game's render size for the next launch. | 19 pins: Stereo 2 on; AER, DIBR, old SBS off; `sfr2Metric`, `sfr2MetricEyeFrustum` on; `sfr2MetricDepth = 1`; `questFovMatch`, `vrFovFillEnabled`, `leanEnabled` on; `lookSensitivity = 1`; `menuFlat`, `galaxyFlat`, `fpEnabled`, `fpHideHead` on; head aim (combat, Mako) off; `decoupledPitch` on; depth-pop key off | LE1 host; the classic menu can still change them for the session | [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp) `ModernBaseline` | fake headset (log `halcyon bridge: the MEVR baseline`) |
| SET-15 | Halcyon's whole-file save writes a `.tmp` and moves it over the file. It is refused for the session if the file existed but could not be read at launch. | `MoveFileExW` replace + write-through | LE1 | [vr_config.cpp](../../../src/halcyon/le1/vr_config.cpp) `WriteIniFromSlots` (`[MEVR]`) | code read |
| SET-16 | Halcyon's profile is saved with its own `SaveProfile` when our menu closes; our menu never writes a reset over a profile. | on close | LE1 menu | [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) | code read |
| SET-17 | The owner's dev settings are the shipped defaults. A setting the owner tunes in their own file becomes a candidate default; a fresh install is diffed against the owner's `LE1MEVR.ini` and active `MELEVR.ini` profile before a release. | rule of 2026-11-05 | release process | [release-checklist.md](../../release-checklist.md); [MenuSettings.hpp](../../../src/core/vr/MenuSettings.hpp) `kHandPitchDefault` | process |
| SET-18 | "Balanced" in Display > Performance equals the shipped defaults: a fresh install reads as Balanced. A bundled value set by hand shows Custom with the differences from the nearest mode. A mode writes every bundled key and empties an old `minScreenRadiusTenths`. | Balanced: headset x 0.85, FSR, cull 8 px, grass 0.5, shadows off, AO on. Quality: headset, 4 px, 0.75. Performance: x 0.75, 24 px, 0.35, AO off | LE1 menu | [PerformanceModes.hpp](../../../src/core/vr/PerformanceModes.hpp), [PerformanceModes.cpp](../../../src/core/vr/PerformanceModes.cpp) `Modes`, `Edits` | unit test (`tests/PerformanceModesTests.cpp`) |
| SET-19 | An empty value (`sharePass=`) is the default, not 0. | `GetPrivateProfileStringW` then `wcstol` | `[Performance]` | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp) `readInt` | code read |
| SET-20 | The render size is not a key of ours: it is Halcyon's `sfr2ResX/Y` and the game's `ResX/ResY`, applied by a restart. Setup's Automatic leaves a low size, and the first VR start sets 0.85 of the headset's recommended eye size once. | x 0.85, rounded to even; marker `[Display] eyeSizeSet = 1` | first VR start with a recommendation; manifest `eyeSizeAtFirstStart` | [MenuSettings.hpp](../../../src/core/vr/MenuSettings.hpp) `FirstStartSize`; [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) `FirstStartEyeSize` | unit test; fake headset |
| SET-21 | The engine settings VR needs are applied by the mod through `scale set`, not trusted to the ini files, again after every level change, each boolean through its opposite first. `[SystemSettings]` in our file overrides any key; `apply = 0` turns it off. | `DynamicShadows False`, `MotionBlur False`, `FoliageDrawRadiusMultiplier 0.5`; `LightEnvironmentShadows False` while the second eye is a replay | a local player exists | [SystemSettings.cpp](../../../src/game/SystemSettings.cpp) | fake headset (research/35 attempt 49) |
| SET-22 | Setup writes the VR game settings into `GamerSettings.ini` and `BIOEngine.ini` with key-level records, so Uninstall restores each key. | VSync off, MotionBlur off, FilmGrain off, MaxAnisotropy 16, BorderlessWindow True, Fullscreen False, DynamicShadows False, Foliage 0.5, `[HDR] DesiredRange = DynamicRange_SDR`, `SmoothFrameRate FALSE`, ResX/ResY | Setup; a game never started keeps them as `pendingSettings` | [GameSettings.cpp](../../../src/install/GameSettings.cpp) | fake game; real game (research/33 sections 11, 13) |
| SET-23 | The developer request files (`console`, `snapshot`, `record`, `replay`, `perf`, `eyediff.request`) are read by a development build and ignored by the players' build unless `[Debug] requests = 1`. | checked every 2 s; absent key = the build's own answer | `MEVR_MONOLITHIC` = players' build | [DevChannels.hpp](../../../src/core/DevChannels.hpp) `Allowed`, `Resolve` | code read |
| SET-24 | The players' build keeps only the newest test-loop output and a quieter log. | snapshots 2048 MB, recordings 512 MB, perf 256 MB (`[Debug] snapshotsMB`, `recordingsMB`, `perfMB`; 0 no limit); heartbeat 30 s (5 s in development; `[Debug] heartbeatS`, read once) | players' build | [TestLoop.cpp](../../../src/plugin/TestLoop.cpp), [DiskBudget.hpp](../../../src/core/DiskBudget.hpp), `src/mod/Mod.cpp` | code read |
| SET-25 | The controller guide opens once by itself on a first run, then never again. | `[Menu] controlsShown = 1` | on foot, focused, after the calibration prompt | [MainMenu.cpp](../../../src/plugin/MainMenu.cpp) `FirstRunWanted`, `FirstRunShown` | `[verify]` headset |
| SET-26 | `[Colour] hdrDisplay` is read by the host at attach; an old `0` moves to `-1` once. | -1 answer SDR, 0 the display's own, 1 answer HDR10; marker `hdrDisplayMoved = 1` | LE1 host | [HdrDisplayPolicy.cpp](../../../src/plugin/HdrDisplayPolicy.cpp) | code read |
| SET-27 | Colour settings are per headset: `[Colour]` for any, `[Colour.<headset key>]` for this one. The three common keys are written once when missing. | `profile = auto`, `compare = 0`, `dither = 0.5` | host | [Colour.cpp](../../../src/plugin/Colour.cpp); [Colour.hpp](../../../src/core/vr/Colour.hpp) | fake headset |
| SET-28 | Calibration is saved per controller type as `[Calibration.<key>]`; `[Calibration] use = 1` is written when missing; `use = 0` or no measured section means the measurements as before. | key = interaction profile + headset | host writes, mod applies | [Calibration.cpp](../../../src/plugin/Calibration.cpp), [BodyCalibration.cpp](../../../src/game/BodyCalibration.cpp) | headset 2026-11-06 |
| SET-29 | Host-read audio settings apply at the next game start; the menu's note says whether what plays is what the file asks for. | `[Audio] spatial`, `gainDb` read once at attach | host | [OpenAlSoft.cpp](../../../src/plugin/OpenAlSoft.cpp); [MenuSettings.hpp](../../../src/core/vr/MenuSettings.hpp) `AudioNote` | fake headset |
| SET-30 | Keys whose reader is gone stay harmless in a file: `assistConeDeg`, `assistRangeM` are no longer used; an old `reticle = game` is kept and falls back to duplex. | since 2026-10-27, 2026-11-04 | `[Sights]` | [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) `kSightsBlock` | code read |

# Arbitration

Four writers share `LE1MEVR.ini`; none rewrites the file.

| writer | what it writes | how | when two meet |
|---|---|---|---|
| the mod (`game/HandCalibration` `Refresh`, PowerWheel, Cover) | a new file, missing blocks, missing keys, migrations | whole blocks appended; single keys through `WritePrivateProfileStringW` | runs on the mod's first tick, seconds after VR is ready: a tool that watches the file must wait for it ([gotcha](../gotchas/mod-settings-appended-after-xr-ready.md)) |
| the host's menu | the key a row changes | `PendingWrites`, key by key | a pending edit overlays the file's value on every re-read, so the mod's append in between never shows a stale value |
| the host's own modules | `[Menu]`, `[Colour]`, `[Calibration.*]`, `[Display] eyeSizeSet`, `hdrDisplayMoved` | key by key | first key wins for all readers, so an append never shadows an earlier key |
| MEVR Setup / `deploy-asi.ps1` | never `LE1MEVR.ini`'s values (deploy only adds `[Debug] openxrRuntime` for the fake headset); `MELEVR.ini` only if missing, then per-slot fixes | text edits recorded in `ASI\MEVR-install.json` | Uninstall removes `LE1MEVR.ini` as recorded user data |

`MELEVR.ini` has two writers: Halcyon's whole-file save (SET-15) and Setup's fixes (SET-13). Setup runs with the game
closed. `ModernBaseline` (SET-14) wins over both at load.

A key present in both `[Performance] minScreenRadiusTenths` and `minScreenRadiusPx`: the tenths are read first
(SET-18 empties them when a mode is chosen).

# Problems ME1 already solved

| problem | fix | source |
|---|---|---|
| A decimal-comma locale broke `strtod`; a Cyrillic folder threw from `path.string()`; host and mod read one file two ways | one reader (`core/Ini`), `from_chars`, `ini::Utf8` | [gotcha](../gotchas/ini-numbers-paths-and-locale.md) |
| A renamed host ASI pointed the host at `<its name>.ini` and the mod at `LE1MEVR.ini` | one path function for both | [SettingsFile.hpp](../../../src/engine/SettingsFile.hpp) |
| The mod's default blocks landed while a test's load watched the file | wait for the mod's markers and a quiet file | [gotcha](../gotchas/mod-settings-appended-after-xr-ready.md) |
| `GamerSettings.ini`'s DynamicShadows and MotionBlur were overridden by `BIOEngine.ini`, which the game rewrites; a level load re-armed old values; `scale set` to the stored value did nothing | the mod applies them itself per level, each boolean through its opposite | [gotcha](../gotchas/system-settings-precedence.md), [gotcha](../gotchas/motion-blur-in-stereo.md) |
| Players with HDR monitors had launch trouble and a blue image | `hdrDisplay = -1` by default, old 0 moved once | [gotcha](../gotchas/windows-hdr-desktop.md) |
| A Halcyon save after an unreadable file put code defaults over all four profiles | saves refused for the session; `.tmp` then move | [vr_config.cpp](../../../src/halcyon/le1/vr_config.cpp) |
| A slot missing from `MELEVR.ini` loaded Insert, head aim and third person | all four slots written; fixes on every deploy | [Ini.cpp](../../../src/install/Ini.cpp) |
| The older `src/halcyon/le1/runtime/MELEVR.ini` pinned stale values (eye separation 0.8, eye swap, VR cinematics off) | install the release file's mode and sizes only | `tools/deploy-asi.ps1` comment |
| A menu row fell back to its default after a write (the Hacking screen row) | every read section listed in `Settings::Read` | [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) |
| `sharePass=` (empty) read as 0 | empty = default | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp) |
| The release shipped without the owner's tuned values (first QA pass, 2026-11-05) | dev settings are the defaults; diff before QA | [release-checklist.md](../../release-checklist.md) |
| A fresh file is busy for a moment (antivirus): a rename or delete fails with error 5 or 32 | the installer retries busy files | [gotcha](../gotchas/fresh-file-busy-for-a-moment.md) |
| A test run autosaved into the player's career and rewrote the profile | back up `Save` and the inis before a run | [gotcha](../gotchas/test-runs-overwrite-saves.md) |
| A migration that failed to write would never retry | the marker is written only after the changes succeed (`powerWheelX`) | [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) |
| Six callers a tick retried creating an unwritable file | one check a second, 10 s when it fails | [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) |

# Code map

| part | file | scope |
|---|---|---|
| ini reader, numbers, encodings | [Ini.hpp](../../../src/core/Ini.hpp), [Ini.cpp](../../../src/core/Ini.cpp) | game-independent (`src/core`), unit-tested |
| settings path | [SettingsFile.hpp](../../../src/engine/SettingsFile.hpp) | every game (`<GAME>MEVR.ini`) |
| menu schema, value formats, pending writes | [MenuSettings.hpp](../../../src/core/vr/MenuSettings.hpp), [MenuSettings.cpp](../../../src/core/vr/MenuSettings.cpp) | game-independent; `le2` / `le3` flags grey rows out |
| menu rows to files | [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) | host; Halcyon's live config only under `MEVR_MENU_LE1`. `CMakeLists.txt` builds the menu into the LE1 and LE2 hosts (`MEVR_MAIN_MENU`) |
| the mod's file, blocks, migrations, per-section readers | [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) | compiled for every game; `#if !defined(MEVR_GAME_LE1)` adds the sequels' `[Reload]` and SMG slots |
| performance modes | [PerformanceModes.hpp](../../../src/core/vr/PerformanceModes.hpp) | game-independent; the row is LE1's |
| render levers | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp), [RenderLeversTable.hpp](../../../src/plugin/RenderLeversTable.hpp) | host, every game; defaults per game in the table (`kLayout`) |
| engine settings by `scale set` | [SystemSettings.cpp](../../../src/game/SystemSettings.cpp) | every game; values per game (`engine::adapter::kVrDynamicShadows`) |
| the baseline pins | [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp) `ModernBaseline` | LE1-only (`MEVR_WITH_HALCYON_LE1`); the sequels have `ApplyMevrBaseline` in their own trees |
| Halcyon's config and save | [vr_config.h](../../../src/halcyon/le1/vr_config.h), [vr_config.cpp](../../../src/halcyon/le1/vr_config.cpp) | LE1-only, GPL-3.0 |
| Setup's ini edits and game settings | [Ini.cpp](../../../src/install/Ini.cpp), [GameSettings.cpp](../../../src/install/GameSettings.cpp), [Manifest.cpp](../../../src/install/Manifest.cpp) | game is a parameter (`GameSpec`); `HalcyonReleaseIni` is LE1's file |
| dev channels, disk budget | [DevChannels.hpp](../../../src/core/DevChannels.hpp), [DiskBudget.hpp](../../../src/core/DiskBudget.hpp) | game-independent |

No HostServices call carries settings: both sides read the file. Settings that travel between host and mod do so as
state (for example `gamepad` flags), not as file values.

# Settings and defaults

## Migrations (each once)

| date | section, key | old default | new | marker (absent = not yet migrated) | code |
|---|---|---|---|---|---|
| 2026-10-22 | `[Controls] supportAcquireM`, `supportReleaseM` | 0.12, 0.22 (point zone) | 0.18, 0.26 (capsule radii) | `supportZone` | HandCalibration.cpp `Refresh` |
| 2026-10-23 | `[Sights] shotgun`, `assaultRifle` | `holo` | `dot` | `eyeBox` | same |
| 2026-10-27 | `[Sights] assaultRifle` | `scope 1.5` | `dot` | `ads` | same, `vr::sights::MigrateAssaultRifle` |
| 2026-11-02 | `[Mako] throttleTravelM`, `leverOffset`, `stickOffset` | 0.15; `-0.20 -0.55 0.25`; `0.20 -0.55 0.25` (all three exactly) | 0.055; `-0.362 -0.446 0.286`; `0.375 -0.415 0.314` | none (the old values themselves) | same |
| 2026-11-03 | `[Sights] sniperRifle`, `reticle`, `assaultRifleLens`, `sniperRifleLens`, `sniperRifleHide` | `scope 4`, `duplex`, `1.6`, `2`, `Scope_Lens` | `scope auto`, `game`, `auto`, `auto`, `auto` | `zoomStick` | same |
| 2026-11-04 | `[Sights] scopeCapture` | 0 | 1 | `scopeCapturePost` | same |
| 2026-11-04 | `[WorldUi] wrist` | `SkillGame` | the twelve personal screens | `wristMenuWidthM` | same |
| 2026-11-05 | `[WorldUi] wristMenuWidthM` | 0.55 | 0.70 | `figure` | same |
| 2026-11-05 | `[Sights] assistDotMaxM` | 0.03 | 0 | `dotWindow` | same |
| 2026-11-05 | `[Throwables] meleeButton` | 1 | 0 | `meleeFist` | same |
| 2026-11-06 | `[Hands] sprint` | `game` | `speed` | `sprintDefault` | same |
| 2026-11-06 | `[Holsters] releaseGrace` | 0.3 | 0.2 | `[Hands] holsterRate` | same |
| 2026-11-06 | `[Sights] adsReachM` | 0.40 | 0.55 | `scopeAdsPushM` | same |
| 2026-11-07 | `[Throwables] grenadeButton`, `hands` | 1, `right` | 0, `both` | `powerWheelX = 1` (written after success) | same, `vr::foot_controls::MigrateGrenades` |
| 2026-11-14 | `[Colour] hdrDisplay` | 0 | -1 | `hdrDisplayMoved = 1` | [HdrDisplayPolicy.cpp](../../../src/plugin/HdrDisplayPolicy.cpp) |
| 2026-11-06 | `[Performance] minScreenRadiusTenths` | any | emptied when a mode is chosen | none | [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) |
| first VR start | Halcyon's `sfr2ResX/Y`, game `ResX/ResY` | Setup's stand-in | 0.85 of the eye size | `[Display] eyeSizeSet = 1` | [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) |
| first run | controller guide shown | - | - | `[Menu] controlsShown = 1` | [MainMenu.cpp](../../../src/plugin/MainMenu.cpp) |
| Setup / deploy | `MELEVR.ini hudPanelFollowDeg` | 50.0 | 70.0 | the value itself | [Ini.cpp](../../../src/install/Ini.cpp) `FixHalcyonIni` |

The migration in the list above for the reticle moved `duplex` to `game`; the block's default is `duplex` again since
2026-11-04 and a file's `game` falls back to it at run time.

## Shipped defaults, `ASI\LE1MEVR.ini`

"menu" names the page of our menu; "file" means file-only. Defaults are the text of the mod's blocks in
[HandCalibration.cpp](../../../src/game/HandCalibration.cpp) unless another file is named.

| section | key | default | read by | menu row |
|---|---|---|---|---|
| Hands | `handPitch`, `handYaw`, `handRoll` | 90, 0, 0 | mod | Body: hand angle (trim around 90, -30..30) |
| Hands | `gunRoll` | 0 | mod | file |
| Hands | `bodyFollowsHead`, `bodyMove` | 1, 1 | mod | file |
| Hands | `leanM`, `bodyMoveMaxM` | 0.20, 0.75 | mod | file |
| Hands | `legsFollowMotion`, `legsTwistDeg` | 1, 60 | mod | file |
| Hands | `steadyIdle` | 0 | mod | file |
| Hands | `armStretch`, `trackingScale`, `reachNearFaceM` | 1.25, 0, 0.30 | mod | file |
| Hands | `noCover` | 1 | mod | file |
| Hands | `physicalCrouch` | 1 | mod | Comfort: crouch |
| Hands | `crouchBelow`, `standAbove`, `crouchDwellS` | 0.75, 0.85, 0.2 | mod | file |
| Hands | `levelCamera` | 1 | mod | file |
| Hands | `recoilHaptic` | 0.5 | mod | Controls: recoil buzz |
| Hands | `gunKick`, `gunKickScale` | 1, 1.0 | mod | Controls: gun kick (scale file) |
| Hands | `aimAssist` | 0 | mod | Controls: aim assist |
| Hands | `exploreStance`, `exploreStanceStorm`, `exploreRamp` | 0, 1, 1 | mod | file (schema field kept, no row since 2026-11-04) |
| Hands | `linearWalk`, `linearWalkS` | 1, 0.10 | mod | file |
| Hands | `linearStick`, `linearFullAt` | 1, 0.90 | mod | file |
| Hands | `startStopAnims` | 0 | mod | file |
| Hands | `drawRate`, `holsterRate` | 6.0, 15.0 | mod | Controls: draw speed (holster file) |
| Hands | `constantEye`, `blendFix`, `crouchDrop` | 1, 1, 0 | host | Controls: constant eye (others file) |
| Hands | `sprint`, `sprintStaminaS`, `sprintRefillS` | `speed`, 6, 8 | mod | file |
| Hands | `fingers`, `fingerCurlDeg`, `fingerTouch` | 1, 70, 1 | mod | Controls: fingers (others file) |
| Arms | `armLengthM`, `measureRequest` | absent (off), 0 | mod / host | classic menu's VR hands tab |
| Controls | `supportGrip`, `supportZone` | 1, `capsule` | mod | file |
| Controls | `supportAcquireM`, `supportReleaseM`, `supportFrontM`, `supportPastMuzzleM` | 0.18, 0.26, 0.10, 0.10 | mod | file |
| Controls | `sniperAims`, `aimAtShoulder` | 0, 1 | mod | file |
| Controls | `leftTrigger` | `lb` | mod | Controls: left trigger |
| Controls | `leftGripAway` | `none` | mod | file |
| Holsters | `enabled`, `gripHolds` | 1, 1 | mod | Controls: holsters, grip holds |
| Holsters | `releaseGrace` | 0.2 | mod | file |
| Holsters | `powerWheel` | `r3` | mod | file |
| Holsters | `weaponWheel`, `onlyHolstersDraw`, `holster` | 1, 1, 1 | mod | file |
| Holsters | `eyeHeightFromFloor`, `eyeHeightM` | 1, 1.65 | mod | file |
| Holsters | `hapticAmplitude` | 0.5 | mod | Controls: holster buzz |
| Holsters | `hipRight` | `pistol, 0.20, -0.62, -0.05, 0.14` | mod | file |
| Holsters | `shoulderRight` | `rifle, 0.18, -0.08, -0.20, 0.20` | mod | file |
| Holsters | `hipLeft` | `shotgun, -0.20, -0.62, 0.00, 0.14` | mod | file |
| Holsters | `shoulderLeft` | `sniper, -0.18, -0.08, -0.20, 0.20` | mod | file |
| Holsters | `seatedHipRight` / `Left` | `±0.28, -0.42, 0.18, 0.20` | mod | file |
| Holsters | `seatedShoulderRight` / `Left` | `±0.28, -0.04, -0.14, 0.20` | mod | file |
| Holsters | `seatedGrenade`, `seatedWeaponWheel` | `-0.10, -0.18, 0.08, 0.10`; 1 | mod | file |
| Squad | `enabled`, `style` | 1, `native` | mod | file (guide reads them) |
| Squad | `wheelStick`, `wheelPreselect` | `right`, 1 | mod | file |
| Squad | `wheelPanel`, `weaponWheelAt`, `powerWheelAt`, `wheelCrop` | 1, `left`, `hud`, `auto` | mod / host | file |
| Squad | `pointConeDeg`, `pointMaxM` | 10, 40 | mod | file |
| Squad | `panelDistanceM`, `panelWidthM` | 0.27, 0.40 | mod / host | file |
| Squad | `confirmOnRelease`, `pause`, `hapticAmplitude` | 1, 1, 0.5 | mod | file |
| Throwables | `enabled`, `melee` | 1, 1 | mod | Controls: grenades and melee (one row) |
| Throwables | `hands` | `both` | mod | file |
| Throwables | `slot` | `-0.13, -0.33, 0.00, 0.10` | mod | file |
| Throwables | `throwSpeedMinMS`, `throwGain`, `throwSpeedMaxMS` | 1.5, 2.5, 15 | mod | file |
| Throwables | `flight`, `fromHand`, `drawForThrow`, `earlyRelease` | `disc`, 1, 1, 0.3 | mod | file |
| Throwables | `grenadeButton`, `meleeButton`, `meleeFist` | 0, 0, 1 | mod | file |
| Throwables | `meleeSpeedMS`, `meleeGunSpeedMS`, `meleeTravelM`, `meleeCooldownS` | 3.0, 3.5, 0.12, 0.8 | mod | file |
| Throwables | `meleeNeedsEnemy`, `meleeReachM`, `meleeConeDeg` | 1, 2.0, 75 | mod | file |
| Throwables | `hapticAmplitude`, `holdBuzz` | 0.5, 1 | mod | file |
| Sights | `enabled` | 1 | mod | file |
| Sights | `pistol`, `shotgun`, `assaultRifle`, `sniperRifle` | `dot`, `dot`, `dot`, `scope auto` | mod | HUD & sights: one row each |
| Sights | `<gun>Place` (four) | `auto` | mod | file |
| Sights | `dotColor`, `dotSizeDeg`, `ringSizeDeg` | `ff3020`, 0.25, 1.6 | mod / host | file |
| Sights | `windowUnits`, `eyeBox`, `frame` | 3, 6, 1 | mod / host | file |
| Sights | `zeroM`, `converge` | 0, 1 | mod | HUD & sights: converge |
| Sights | `lensUnits`, `reticle`, `reticleColor`, `zoomStick` | 2, `duplex`, `ff2020`, 1 | mod | file |
| Sights | `source`, `focusM`, `eyeReliefM`, `resolution` | `other`, 40, 0.12, 768 | mod / host | file |
| Sights | `scopeFieldDeg`, `scopeEyeBoxM`, `scopeMaxM` | 22, 0.05, 0.70 | mod | file |
| Sights | `ads`, `adsReachM`, `adsReachExitM` | 1, 0.55, 0.80 | mod | file |
| Sights | `scopeReliefM`, `scopePullMaxM`, `scopeAdsPullMaxM`, `scopeAdsPushM` | 0.12, 0.15, 0.35, 0.15 | mod | file |
| Sights | `shoulderAds` | 1 | mod | file |
| Sights | `assaultRifleLens`, `sniperRifleLens`, `sniperRifleHide` | `auto` | mod | file |
| Sights | `replaceRing`, `aimedFire` | 1, 1 | mod | file |
| Sights | `assist`, `assistStrength`, `assistMaxM`, `assistDotMaxM` | 1, 1, 0.10, 0 | mod | HUD & sights: assist (others file) |
| Sights | `dotWindow` | 1 | mod | file |
| Sights | `scopeCapture`, `scopeCaptureSize`, `scopeCaptureRate` | 1, 1024, 150 | mod | file |
| Sights | `scopeCapturePost` | 1 | mod | HUD & sights: scope grade |
| Comfort | `turn` | `game` | mod | Comfort: turn (Smooth / Snap) |
| Comfort | `snapDeg` | 45 | mod | Comfort: snap angle (30 / 45 / 60) |
| Comfort | `deadzone`, `snapThreshold`, `snapRearm`, `snapCooldownS` | 0.15, 0.7, 0.3, 0.25 | mod | file |
| Comfort | `turnSpeed` | 120 ([Comfort.hpp](../../../src/core/vr/Comfort.hpp); not in the written block) | mod | file |
| Comfort | `vignette`, `vignetteStrength`, `vignetteWhen` | 0, 0.6, `turn` | mod | Comfort: vignette rows |
| Comfort | `walk` | `head` | host | Comfort: walk toward |
| Comfort | `screenShake`, `viewShake` | 0, 0 ([CameraShake.cpp](../../../src/game/CameraShake.cpp); not in the block) | mod | Comfort: camera shake (off / low 0.3 / full 1), running sway |
| Interact | `gaze`, `label`, `labelHeightDeg`, `combatLabel`, `crop` | 1, 1, 6, 0, empty | mod / host | HUD & sights: interact label (others file) |
| SquadContext | `enabled`, `refreshS`, `joinM`, `splitAboveM`, `maxGroups` | 1, 0.25, 6, 2.5, 6 | mod | file |
| SquadContext | `nearM`, `farM`, `landmarkRangeM`, `maxLandmarks` | 10, 30, 25, 5 | mod | file |
| SquadOrders | `enabled`, `gesture`, `followWithinM` | 1, 1, 2 | mod | file |
| SquadOrders | `enemyConeDeg`, `memberConeDeg`, `minHoldS`, `maxHoldS`, `minConfidence` | 8, 12, 0.08, 8, 0.6 | mod | file |
| SquadOrders | `standing`, `holdFormation`, `reassertS`, `holdRadiusM`, `calmS` | 1, 1, 4, 4, 8 | mod | file |
| SquadOrders | `retreatHealth`, `overwhelmCount`, `retreatToCover`, `retreatM`, `coverSearchM`, `retreatCooldownS` | 0.3, 3, 1, 8, 10, 10 | mod | file |
| SquadOrders | `ackFallback`, `ackFallbackS`, `ackMoveSet`, `ackHoldSet`, `ackAttackSet` | 1, 1, 70, 59, 61 | mod | file |
| SquadOrders | `powerRoute` | `tactical` | mod | file |
| SquadOrders | `place`, `placeSpeedMS`, `placeRangeM`, `placeMinHoldS` | 1, 14, 30, 0.15 | mod | file |
| Voice | `enabled`, `model`, `device`, `key` | 1, `tiny`, empty, 0 | host | Voice: on, size, microphone, key |
| Voice | `tailMs`, `duckDb`, `intervalMs`, `threads`, `wordTimestamps` | 200, 12, 250, 1, 1 | host | file |
| Voice | `gesture`, `earCue`, `earCueStrength` | 1, 1, 0.4 | mod | Voice: radio hum (strength) |
| Voice | `radioRadiusM`, `earSideM`, `earDownM`, `minHoldS`, `maxHoldS` | 0.2, 0.08, 0.03, 0.15, 15 | mod | file |
| Voice | `earlyDispatch`, `settleMs`, `deicticLagS` | 1, 250, 0.3 | mod | file |
| Thespian | `on`, `strictness`, `handPick`, `hideWheel` | 0, `performer`, `voice`, 1 | mod | Voice: Thespian rows (hideWheel file) |
| Thespian | `endMs`, `pauseMs`, `staleMs` | 200, 900, 3500 ([Thespian.hpp](../../../src/core/vr/Thespian.hpp)) | mod | file |
| Vent | `enabled` | 1 | mod | Controls: vent |
| Vent | `ventCooldownS`, `ventMinHeat`, `ventMaxHeat` | 4, 0.3, 1.0 | mod | file |
| Vent | `ventSpeedMS`, `ventTravelM`, `ventHoldMaxS` | 1.0, 0.05, 1.5 | mod | file |
| Vent | `effect`, `hapticAmplitude` | 1, 0.5 | mod | file |
| Vent | `zoneBackM`, `zoneFrontM`, `zoneAboveM`, `zoneBelowM`, `zoneSideM` | 0.06, 0.14, 0.14, 0.03, 0.09 | mod | file |
| Vent | `animate`, `<gun>Part` (four), `partTravelUnits`, `partReturnHz`, `partDamping` | 1, `auto`, `auto`, 9, 0.5 | mod | file |
| Hud | `wristStatus`, `wristWidthM`, `wristOffset` | 1, 0.08, `-0.03 0.06 0.05` | mod / host | HUD & sights: wrist status |
| Hud | `gunStatus`, `gunWidthM`, `gunBackM`, `gunUpM` | 1, 0.06, 0.15, 0.04 | mod / host | HUD & sights: gun status |
| Hud | `pistolSideM`, `pistolUpM`, `wristCrop`, `gunCrop` | -0.05, 0, empty, empty | mod | file |
| Mako | `handAim`, `turret`, `turretSeam`, `reticle`, `vignette` | 1, 1, 3, 1, 1 | mod | file |
| Mako | `moveSpeed`, `turnRate`, `aimRangeM`, `calibratedLine` | 150, 20, 200, 1 | mod | file |
| Mako | `controls` | 1 | mod | Controls: Mako controls |
| Mako | `grabRadiusM`, `stickTwist`, `stickTiltDeg`, `stickHeightM`, `leverReturns` | 0.15, 0, 20, 0.20, 1 | mod | file |
| Mako | `stickTwistDeg`, `stickDeadZoneDeg`, `stickInvert` | 42, 4, 0 | mod | file |
| Mako | `throttleTravelM`, `throttleDetent` | 0.055, 0.15 in a new file (0.08 when appended to an old section) | mod | file |
| Mako | `leverOffset`, `stickOffset` | `-0.362 -0.446 0.286`, `0.375 -0.415 0.314` | mod | file |
| Mako | `stickAimRate`, `rumble`, `cameraFollow` | 90, 0.12, 1 | mod | file |
| Mako | `thrusterBurnScale`, `thrusterStabilize`, `thrusterPitchDeg`, `thrusterRollDeg`, `thrusterRateDeg` | 0.4, 1, 20, 15, 60 | mod | file |
| Mako | `stickPitchDeadZoneDeg`, `steerInPlaceBelow`, `tiltSteerMax`, `spinRateDeg` | 8, 0.2, 0.5, 60 | mod | file |
| Mako | `gunner`, `gunnerRangeM`, `gunnerHandbackS`, `gunnerCannon`, `gunnerCrowd`, `gunnerCrowdRadiusM` | 1, 120, 3, 1, 3, 6 | mod | file |
| Cockpit | `enabled`, `script`, `seatPosition` | 1, `cockpit\cockpit.txt`, unset | mod | Controls: cockpit, Mako seat (top / bottom) |
| Cockpit | `followCamera`, `seated`, `showTurret` | 1, 1, 1 | mod | file |
| Cockpit | `hazard`, `hazardBelow`, `hazardPeriodS`, `hazardWash` | 1, 0.30, 1.1, 1 | mod | file |
| Galaxy | `hologram`, `inWorld`, `onFootHologram` | 1, 1, 0 | mod | Comfort: galaxy hologram |
| Galaxy | `worldRadiusM`, `worldTiltDeg`, `worldLiftM` | 1.40, 35, 0 | mod / host | file |
| Galaxy | `window`, `windowWidthM`, `windowDepth`, `windowRound`, `windowOnHologram`, `windowScale`, `windowBackdrop` | 1, 2.0, 0.03, 1, 1, 0.7, 0.8 | mod / host | file |
| Galaxy | `tableDistanceM`, `tableBelowM`, `tableRadiusM`, `distanceM`, `belowM` | 0.70, 0.45, 0.48, 0.9, 0.30 | mod / host | file |
| WorldUi | `on`, `screens` | 1, `all` | mod | Comfort: game screens in the room |
| WorldUi | `widthM`, `distanceM`, `belowM`, `backdrop` | 1.20, 0.95, 0.50, 0.80 | mod / host | file |
| WorldUi | `wrist` | SkillGame, Inventory, XMods, CharacterRecord, Specialization, Journal, Codex, Map, MenuBrowser, Options, Save, Load | mod | Comfort: omni-tool screens (wrist / room) |
| WorldUi | `wristWidthM`, `wristMenuWidthM` | 0.30, 0.70 | mod / host | Comfort: wrist screen size (0.4..1.0; the minigame's file) |
| WorldUi | `wristAboveM`, `wristTowardM`, `wristFollow`, `wristBeam`, `omniTool` | 0.10, 0.00, 1, 1, 1 | mod / host | file |
| WorldUi | `figure`, `figureOpacity` | 1, 0 | mod / host | file |
| OmniTool | `enabled`, `tap`, `tapOnBody`, `railSide` | 1, 1, 1, `right` | host | file |
| OmniTool | `aloneHeightM`, `pad`, `padSizeM` | 0.32 ([OmniRail.hpp](../../../src/core/vr/OmniRail.hpp)), 1, 0.17 ([OmniPad.hpp](../../../src/core/vr/OmniPad.hpp)) | host | file |
| Pointer | `gameMenus`, `flatCine`, `gameCursor` | 1, 0, 1 | host | file |
| Pointer | `moveMessages`, `setCursor`, `sendInput`, `postThread`, `logMessages` | 1, 1, 0, 1, 0 | host | file |
| Pointer | `wheel`, `wheelPerSecond`, `dotDeg` | 1, 6, 0.6 | host | file |
| Pointer | `touch`, `touchTipM`, `touchHoverM`, `touchPressM`, `touchReleaseM` | 1, 0.04, 0.06, 0.005, 0.015 | host | file ([GamePointer.cpp](../../../src/plugin/GamePointer.cpp)) |
| PowerWheel | `on`, `widthM`, `towardM`, `deadZoneM`, `keepDeg`, `stickPick`, `tapS` | 1, 0.42, 0.04, 0.025, 6, 0.5, 0.30 | mod | file (guide reads `on`) |
| PowerWheel | `emblem`, `upperCase`, `hintOpenings`, `haptic`, `coach`, `omniTool` | 1, 0, 3, 0.35, 3, 1 | mod / host | file |
| PowerWheel | `lock`, `lockAcquireDeg`, `lockKeepDeg`, `lockRangeM`, `lockGraceS`, `lockReachM` | 1, 9, 16, 35, 0.35, 0.20 | mod | file |
| PowerWheel | `pushSpeedMS`, `pushTravelM`, `pushCooldownS` | 1.6, 0.10, 0.6 | mod | file |
| PowerWheel | `autoLock`, `autoLockHandDeg`, `autoLockGazeDeg`, `selfGestures` | 1, 30, 20, 1 | mod | file |
| Cover | `protect`, `reachM`, `log` | 1, 1.2, 0 | mod | file ([VrCover.cpp](../../../src/game/VrCover.cpp)) |
| Game | `pauseUnfocused`, `cameraCollision` | 0, 0 | mod | file |
| Body | `floorLock` | 1 | host | file |
| Body | `scale` | `auto` (clamped 0.85..1.25; `off` or a number) | mod | file ([BodyScale.cpp](../../../src/game/BodyScale.cpp)) |
| Menu | `pause`, `distanceM`, `widthM`, `assets` | 1, 1.30, 1.45, empty | host | Comfort: pause in menu (others file) |
| Cinematics | `conversations`, `cutscenes`, `frontEnd` | screen (1), 1, 1 | host | Comfort: conversations, cutscenes (In VR / 3D screen / Flat; First person = `world`) |
| Cinematics | `distanceM`, `widthM`, `depth` | 3.5, 2.9, 0.010 | host | Comfort: screen distance (1..6), width (1.5..8), depth (0..150%) |
| Cinematics | `convergeM`, `aspect`, `fullRate` | 0.8, 16:9, 1 | host | file ([CineScreen.hpp](../../../src/core/vr/CineScreen.hpp)) |
| Cinematics | `backdrop`, `backdropBrightness`, `fadeS` | 1, 1.0, 0.35 | host | file |
| Cinematics | `uiLayer`, `uiForwardM` | 1, 0.5 | host | file |
| Cinematics | `worldStay`, `worldWalk`, `worldMove`, `worldCircle`, `worldArrange` | 1, 1, 1, 1, 0 | mod | file ([ConversationWorld.cpp](../../../src/game/ConversationWorld.cpp) `ReadSettings`) |
| Cinematics | `worldMoveLeashM`, `worldMoveSpeedMS`, `worldMoveClearM`, `worldWalkInS` | 4.0, 1.4, 0.7, -1 | mod | file |
| Cinematics | `worldNearPlane`, `worldCamera`, `worldDipWithinM`, `worldBlackMoves` | 1, 0, 8, 1 | mod | file |
| Cinematics | `worldLookEyes`, `worldLookReach`, `worldGlanceDeg`, `worldTurnComfortDeg`, `worldTurnSettleDeg`, `worldTurnWithinM`, `worldTurnHomeS` | 1, 0, 70, 80, 40, 4, 3 | mod | file |
| Cinematics | `worldWalkAlong`, `worldWalkAlongPauseM`, `worldWalkAlongResumeM`, `worldWalkAlongMaxS` | 1, 3.0, 1.5, 120 | mod | file |
| Cinematics | `worldSquadGather`, `worldCircleOwnerStays`, `worldCircleLookahead`, `worldCircleRegather` | 1, 1, 4, 0 | mod | file |
| Cinematics | `worldVoiceWithinM`, `worldVoiceKeepM`, `worldVoiceNearM`, `worldVoiceFalloffDb`, `worldVoiceFloorDb`, `worldVoiceGain` | 8, 10, 2, 3, 6, 1 | mod | file |
| Cinematics | `worldMomentPatienceS`, `worldStaySource`, `worldMoveTriggers`, `worldShowEarly` | 12, 1, 1, 1 | mod | file |
| Cinematics | `worldTurn`, `worldOpening`, `worldMoments`, `worldAutoScreen`, `worldSetPieces`, `worldVoices`, `worldFlow` | `roles` `[verify]`, `auto`, `view`, `on`, `screen`, `spatial`, `all` `[verify]` (struct defaults in ConversationWorld.cpp: `opening = Auto`, `autoScreen = 2`, `setPiecesOnScreen`, `voices`, `moments` true; the two marked ones are CLAUDE.md's) | mod | file |
| Colour | `profile`, `compare`, `dither` | `auto`, 0, 0.5 | host | Display: colour profile; hold A compares |
| Colour | `brightness`, `contrast`, `saturation`, `gamma`, `black`, `gainR/G/B` (custom, per headset) | 1, 1, 1, 1, 0, 1 | host | Display: sliders (-20..20 %, -20..30 %, -30..40 %, 0.7..1.3, -0.05..0.05) |
| Colour | `tonemap`, `exposure`, `white`, `toneHue`, `toneDesat`, `toneSat` | `clip`, 0, 4.0, 1.0, 0.15, 0.7 | host | file ([Colour.hpp](../../../src/core/vr/Colour.hpp) `Hdr`) |
| Colour | `hdrDisplay` | -1 | host | file |
| Upscale | `method`, `sharpness` | `fsr`, 0.9 | host | Display: upscale; Advanced > Performance: FSR sharpness |
| Performance | `sharePass` | 5 | host (mod reads it for the shadows rule on an older host) | file |
| Performance | `minScreenRadiusPx` | 8 | host | Advanced > Performance: cull (0, 4, 8, 16, 24, 32) |
| Performance | `keepBoundShaderStates`, `umbraEyeMatrix`, `umbraSecondEyeClearBits` | 1, 1, 2 | host | file |
| Performance | `umbraQueryWait`, `perEyeOcclusion`, `perEyeViewState`, `umbraSharedVisibility`, `umbraShareResults`, `flushFirstEye`, `pCoresOnly` | 1, 0, 0, 0, 0, 0, 0 ([RenderLeversTable.hpp](../../../src/plugin/RenderLeversTable.hpp) LE1 layout) | host | file |
| Performance | `oneDrawWindow`, `eyeScissor`, `replayInverse`, `replayScreenMatrices`, `sfrReplayRetry` | 0, 0, 1, 1, 1 | host | file |
| Performance | `earlyFrameWait`, `pacingRecovery`, `pacingRecoveryTries`, `frameLoop` | `auto`, `auto`, 3, `halcyon` | host | file ([QuestPacing.cpp](../../../src/plugin/QuestPacing.cpp)) |
| Performance | `motionBlurGuard` | 1 | host | file ([MotionBlurGuard.cpp](../../../src/plugin/MotionBlurGuard.cpp)) |
| SystemSettings | `apply` | on | mod | file |
| SystemSettings | `DynamicShadows`, `MotionBlur`, `FoliageDrawRadiusMultiplier` | False, False, 0.5 (built in; a key here overrides) | mod | Advanced > Performance: shadows, grass (25..100 %) |
| SystemSettings | `AmbientOcclusion` | the game's own (on) | mod | Advanced > Performance: ambient occlusion |
| SystemSettings | `LightEnvironmentShadows` | False while the second eye is a replay | mod | file |
| Audio | `spatial`, `gainDb`, `trace` | `hrtf`, 3, 0 | host, at attach | Sound: 3D sound, level (0..9 dB) |
| Audio | `overheard`, `gun` | 1, 1 | mod | Sound: people nearby, the gun |
| Audio | `overheardWithinM`, `overheardKeepM`, `overheardNearM`, `overheardFalloffDb`, `overheardFloorDb`, `overheardGain`, `overheardMax` | 8, 10, 2, 3, 6, `[verify]`, 16 | mod | file ([SoundPlace.cpp](../../../src/game/SoundPlace.cpp)) |
| Calibration | `use`, `eye`, `grabOffset` | 1, `auto`, zero vector in the struct ([Calibration.cpp](../../../src/plugin/Calibration.cpp)); shipped value `[verify]` | host / mod | Body & calibration |
| Display | `steamvrSmoothing`, `eyeSizeSet` | `steamvr`, 0 | host | Display: motion smoothing (SteamVR only) |
| Spectator | `enabled` | 0 | mod | file |
| Debug | `requests`, `heartbeatS` | absent (the build decides), absent | host and mod | file |
| Debug | `snapshotsMB`, `recordingsMB`, `perfMB`, `swapchainCap`, `openxrRuntime` | 2048, 512, 256, 0, empty | host | file |

## Shipped defaults, `MELEVR.ini` (each of the four profile slots)

| key | fresh file | pinned by `ModernBaseline` | menu row |
|---|---|---|---|
| `vrMode` | 2 (Stereo 2) | yes | none |
| `sfr2ResX`, `sfr2ResY` | 4096, 4250 (replaced by Setup's choice or the first VR start's 0.85 of the eye size) | no | Display: render resolution |
| `monoResX/Y`, `aerResX/Y`, `dibrResX/Y` | 3840x2160, 3072x3072, 3072x3072 | modes pinned off | none |
| `expectedResX/Y` | 0, 0 | no | none |
| `firstRunDone` | 1 | no | none |
| `menuKey` | 220 (`\`) | no (fixed by Setup / deploy) | controller guide names it |
| `fpEnabled` | 1 | yes | none |
| `combatHeadAim`, `makoHeadAim` | 0, 0 | yes | none |
| `mirrorPresentEvery` | 2 | no | Advanced: desktop mirror (smooth) |
| `depthPopKeyEnabled` | 0 | yes | none |
| `sfr2Metric` | 1 | yes (with `sfr2MetricEyeFrustum = 1`, `sfr2MetricDepth = 1`) | none |
| `decoupledPitch` | 1 | yes | none |
| `cameraOffsetEnabled`, `cameraOffsetRight`, `cameraOffsetUp` | 1, 0.078, -0.082 | no | Advanced > Tracking |
| `screenDistanceEnabled`, `flatScreenDistance` | 1, 0.674 | no | Advanced |
| `hudPanelDistanceM`, `hudPanelWidthM`, `hudPanelFollowDeg` | code defaults 1.5, 1.3, 70 ([vr_config.h](../../../src/halcyon/le1/vr_config.h)) | no | HUD & sights: distance (1..3), size (45..155 % of 1.3 m), follow (0..120) |
| `worldLockedPanels`, `panelFollowDeg`, `recenterTiltDeg` | code defaults 1, 70, 60 | no | Advanced |
| `[hotkeys] enabled` | 0 (F1-F4 are game keys) | - | none |
| `[active] profile` | Default | - | Advanced > Profiles |

Every other key of a slot is absent from a fresh file and loads Halcyon's tuned code default.

## Game files written by Setup and `tools/vr-game-settings.ps1`

| file | key | value |
|---|---|---|
| GamerSettings.ini `[SystemSettings]` | `UseVSync`, `MotionBlur`, `FilmGrain` | False |
| GamerSettings.ini `[SystemSettings]` | `MaxAnisotropy` | 16 |
| GamerSettings.ini `[SystemSettings]` | `BorderlessWindow`, `Fullscreen` | True, False |
| GamerSettings.ini `[SystemSettings]` | `DynamicShadows` (LE1 only) | False |
| GamerSettings.ini `[SystemSettings]` | `ResX`, `ResY` | the stereo target |
| GamerSettings.ini `[HDR]` | `DesiredRange` | `DynamicRange_SDR` |
| BIOEngine.ini `[SystemSettings]` | `DynamicShadows`, `MotionBlur`, `FoliageDrawRadiusMultiplier`, `Fullscreen`, `MaxAnisotropy`, `ResX/ResY` | False, False, 0.500000, False, 16, the target |
| BIOEngine.ini (every section that has it) | `SmoothFrameRate` | FALSE |

# Checks

| what | how |
|---|---|
| the mod's settings as read | log `hands:`, `posture: settings:`, `holsters:`, `squad: settings:`, `throwables: settings:`, `comfort: settings:`, `interact: settings:` (each when its text changes) |
| blocks and keys added, migrations | log `hands: added ... (defaults)`, `hands: [Section] key old -> new`, `... kept (your choice; the default is ...)` |
| baseline pins | log `halcyon bridge: the MEVR baseline (...)` |
| HDR answer | log `hdr display:` |
| dev channels | log `dev channels: the developer request files ... are read / ignored` |
| engine settings applied | log `system settings:` |
| performance mode | log `main menu: performance mode ...` |
| first VR start's size | log `first VR start:` |
| unit tests | `mevr-tests`: `tests/Main.cpp` (TestIni, MenuSettings, PendingWrites), `tests/PerformanceModesTests.cpp`; ctest `mevr-system-settings-tests` |
| installer on a fake game | `tools/setup/test-fake-game.ps1` |
| offline look of the rows | `mevr-menu-preview` (`section-display-perf-*.png`, `advanced-perf.png`) |
| a fresh install against the owner's files | diff a fresh `LE1MEVR.ini` and `MELEVR.ini` with the owner's (SET-17) |

# Citations

- Code: [HandCalibration.cpp](../../../src/game/HandCalibration.cpp), [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp),
  [MenuSettings.cpp](../../../src/core/vr/MenuSettings.cpp), [Ini.hpp](../../../src/core/Ini.hpp),
  [SettingsFile.hpp](../../../src/engine/SettingsFile.hpp), [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp),
  [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp), [SystemSettings.cpp](../../../src/game/SystemSettings.cpp),
  [Ini.cpp (install)](../../../src/install/Ini.cpp), [GameSettings.cpp](../../../src/install/GameSettings.cpp),
  [Thespian.cpp](../../../src/game/Thespian.cpp), [ConversationFlow.cpp](../../../src/game/ConversationFlow.cpp).
- Research: [28 main menu](../../../research/28-main-menu.md), [31 comfort](../../../research/31-comfort.md),
  [33 installer](../../../research/33-installer.md), [35 performance log](../../../research/35-performance-attempt-log.md),
  [60 performance modes](../../../research/60-performance-modes.md), [27 colour](../../../research/27-colour.md),
  [21 sights](../../../research/21-sights-and-scopes.md), [20 melee and grenades](../../../research/20-melee-and-grenades.md),
  [57 omni-tool menu](../../../research/57-omni-tool-menu.md).
- Gotchas: [ini numbers, paths and locale](../gotchas/ini-numbers-paths-and-locale.md),
  [settings appended after XR ready](../gotchas/mod-settings-appended-after-xr-ready.md),
  [SystemSettings precedence](../gotchas/system-settings-precedence.md),
  [Windows HDR desktop](../gotchas/windows-hdr-desktop.md).
- Process: [release checklist](../../release-checklist.md).
