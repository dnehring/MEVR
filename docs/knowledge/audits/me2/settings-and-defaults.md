---
type: Audit
title: ME2 against ME1's settings files and shipped defaults
description: Contract by contract, how LE2 reads, writes and defaults its settings (ASI\LE2MEVR.ini, MELE2VR.ini, the menu's rows, the baseline pins, Setup's game settings) against ME1's catalog.
tags: [audit, me2, parity, settings, defaults, ini, menu, baseline]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/settings-and-defaults.md
commit: 940a75c6
---

# Verdict

Audited against [ME1's settings catalog](../../me1/settings-and-defaults.md) by reading code and notes at 940a75c6; no
game was run, so every row is "read from the code" unless it names a run.

- The file layer is ME1's own code on ME2: one reader (`core/Ini`), one path (`ASI\LE2MEVR.ini`), the same blocks,
  migrations, pending writes and `[Menu]`, `[Colour]`, `[Debug]` handling. 19 of 30 contracts are "same" or "same core".
- **The render size has no owner on ME2.** Nothing sets the eye size from the headset (SET-20), the menu's resolution
  and Performance rows are greyed and their writes are refused (SET-18), and the fresh `MELE2VR.ini` is upstream's
  4096x2304 whatever the headset is.
- **`LE2MEVR.ini` is written with ME1's whole file, including keys nothing on ME2 reads** (`linearWalk`, `linearStick`,
  `startStopAnims`, `constantEye`, `drawRate`, `[Game] pauseUnfocused`, `[Mako]`, `[Cockpit]`, `[Performance]
  minScreenRadiusPx`): the file says a default is in force where the module is LE1-only or its lever is unverified.
- **Setup's ME2 game settings lag the mod and the dev tool** (SET-22): `GameSpec` for LE2 still says "the sequels keep
  dynamic shadows" and records nothing in `BIOEngine.ini`, while the mod now sets them off by `scale set` on every game
  and `tools/vr-game-settings.ps1` writes them for every game.
- The baseline pins exist (`ApplyMevrBaseline`), but as a third hand-written list beside LE1's and LE3's; the fresh
  renderer file ships upstream's values (head aim on, Insert, VR cutscenes, stick pitch) and relies on the pins.

| rating | count | ids |
|---|---|---|
| same | 13 | SET-01, 02, 03, 05, 07, 08, 09, 10, 19, 23, 24, 27, 30 |
| same core, other inputs | 6 | SET-04, 06, 11, 21, 25, 26 |
| adapter | 3 | SET-14, 16, 28 |
| reinvented | 0 | |
| partial | 4 | SET-12, 13, 17, 22 |
| missing | 2 | SET-18, 20 |
| n/a | 2 | SET-15, 29 |
| unknown | 0 | |

# Contract by contract

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| SET-01 | same | `SettingsFile` with `MEVR_GAME_NAME_W` = LE2; the sequel host reads the same path | none | `src/engine/GameAdapter.hpp:25` (name macros), `src/plugin/HalcyonSequelBridge.cpp:126` |
| SET-02 | same | `core/Ini` is in `MEVR_CORE_SOURCES`, every target | none | `CMakeLists.txt:322` |
| SET-03 | same | our code as ME1; Halcyon ME2's own readers use `_wtof` and `swprintf %.3f` (the C locale), the same caveat as ME1's `sscanf` | none for our file; `MELE2VR.ini` has the caveat | `src/halcyon/le2/me2_menu.cpp:558`, `:722` |
| SET-04 | same core, other inputs | the same `Refresh`, a new file gets every ME1 block plus `[Reload]`; `[Holsters]` gains the SMG keys | ME1-only blocks (`[Mako]`, `[Cockpit]`, `[Vent]`, `[Galaxy]` keys of the CIC, `[Game]`) and ME1-only keys of `[Hands]` are written into LE2's file with ME1's comments although their readers are LE1-only | `src/game/HandCalibration.cpp:2962`, `:2991`, `:3023`, `:661` |
| SET-05 | same | same code | none | `src/game/HandCalibration.cpp` `Refresh` |
| SET-06 | same core, other inputs | RenderLevers is in the LE2 host (`MEVR_SEQUEL_RENDER_PERF_SOURCES`); `[Body] scale` and `[Debug] requests` shared | the levers' defaults come from LE2's layout row: `sharePass` stands down and `minScreenRadiusPx` is unverified, so those keys are read and do nothing | `CMakeLists.txt:1050`, `src/plugin/RenderLeversTable.hpp:210` |
| SET-07 | same | `MainMenuSettings.cpp` is in `MEVR_SEQUEL_MENU_SOURCES` | none | `CMakeLists.txt:1022` |
| SET-08 | same | the same section list | none | `src/plugin/MainMenuSettings.cpp:280` |
| SET-09 | same | same `ReadMenuSection` | none | `src/plugin/MainMenuSettings.cpp:250` |
| SET-10 | same | same code path; never seen in an ME2 run | none known | `src/game/HandCalibration.cpp:2972`, `src/plugin/MainMenuSettings.cpp:632` |
| SET-11 | same core, other inputs | ME1's migrations compile and run on LE2's file (none can fire on a file LE2 created); the sequels add `[Reload] enabledMoved` with ME1's marker-after-success rule | one more migration | `src/game/HandCalibration.cpp:3198`, `:3314` |
| SET-12 | partial | `deploy-asi.ps1 -Game LE2` copies upstream's `runtime/MELE2VR.ini` next to the exe when none is there; one `[VR]` section, profiles are file copies | the fresh file holds upstream's values (`HeadAim=1`, `MenuKey=45`, `CineVrConvo=1`, `DecoupledPitch=0`, `ProfileHotkeys=1`, `RenderW=4096`, `RenderH=2304`); only the pins of SET-14 correct them at load. Nothing like ME1's tuned non-pinned values (camera offset, screen distance) is written | `tools/deploy-asi.ps1:263`, `src/halcyon/le2/runtime/MELE2VR.ini` |
| SET-13 | partial | no file fix at install, repair or deploy; an existing `MELE2VR.ini` is never written. The same intent is met at load by `ApplyMevrBaseline` (menu key, head aim, hotkeys) | a foreign or old file keeps every unpinned key; no "foreign slot" rule, no `mirrorPresentEvery`, no HUD-follow migration | `src/install/Game.hpp:28`, `src/halcyon/le2/me2_menu.cpp:490` |
| SET-14 | adapter | `ApplyMevrBaseline` after every `LoadValues` (also after `LoadProfile`), logged `[MEVR-BASELINE]` | its own list in Halcyon's tree: 16 pins plus life-size stereo. Pinned there and not on ME1: body shown in every state, R recenter off, VR conversations and cutscenes off. Not pinned: `VehicleHeadMode` (ME1 pins `makoHeadAim` off), `DecoupledYaw`. The menu's Reset is greyed on ME2; no render-size rewrite | `src/halcyon/le2/me2_menu.cpp:490`, `:713`, `:887`; `assets/menu/main.rml:637` |
| SET-15 | n/a | ME2's `SaveValues` writes key by key with `WritePrivateProfileStringW`; there is no whole-file write to protect | a file unreadable at launch loads code defaults and a later save writes them back key by key: not guarded (unverified) | `src/halcyon/le2/me2_menu.cpp:718` |
| SET-16 | adapter | `renderer::SaveOwnSettings` (ME2's `SaveValues`) when the menu closes | saves the pinned live values too, as ME1 does | `src/plugin/MainMenuSettings.cpp:796` |
| SET-17 | partial | the mod's file ships ME1's defaults; `MELE2VR.ini` ships upstream's | no "diff against the owner's file" step exists for ME2, and there is no owner-tuned ME2 renderer profile to diff against | `docs/release-checklist.md` |
| SET-18 | missing | the Display group (Performance, Render resolution, Upscale, Motion smoothing) is greyed; a `perf_mode` or `quality` write logs "no Mass Effect 2 setting" | `[Upscale] method` works on ME2 (shared eye pipeline) but sits in the greyed group; the bundle's cull lever is unverified on LE2 **Render resolution and Upscale work since lane AU4 (b1fb0c50); the Performance bundle stays greyed.** | `assets/menu/main.rml:448`, `src/plugin/MainMenuSettings.cpp:800`, `:1095` |
| SET-19 | same | shared `readInt` | none | `src/plugin/RenderLevers.cpp` |
| SET-20 | missing | the size is `MELE2VR.ini [VR] RenderW/RenderH`, written to GamerSettings by Halcyon at attach; `FirstStartEyeSize` is an empty function, `SetResolution` refuses | no 0.85 of the headset's eye size, no `eyeSizeSet`; every headset renders 2048x2304 an eye and FSR scales it to the headset's size **Fixed in code, lane AU4 (b1fb0c50): ME1's first-start rule at ME2's shape (multiples of 8, at least 16:9), `eyeSizeSet`, `SetResolution` through `renderer::SetRenderTarget`.** | `src/plugin/MainMenuSettings.cpp:800`, `:1504`; `tools/vr-game-settings.ps1:131` |
| SET-21 | same core, other inputs | the same module and values | LE2 holds a new level's settings until 3 s of gameplay (a fault otherwise); `LightEnvironmentShadows False` always, not only while the second eye is a replay | `src/game/SystemSettings.cpp:237`, `:143`; `src/engine/GameAdapter.hpp:267`, `:276` |
| SET-22 | partial | `ApplyGamerSettings(..., game)`: VSync, MotionBlur, FilmGrain, anisotropy, borderless, foliage 0.5, HDR SDR; ResX/ResY and DepthOfField recorded | `GameSpec` LE2 has `engineIni = false` and `dynamicShadowsOff = false`: no `BIOEngine.ini` record, no `SmoothFrameRate`, no DynamicShadows, while the mod sets shadows off by `scale set` and the dev tool writes them for every game. Whether Uninstall puts back what the game then stored is untested | `src/install/Game.cpp:21`, `src/install/Game.hpp:38`, `src/install/GameSettings.cpp:94`, `tools/vr-game-settings.ps1:140` |
| SET-23 | same | shared `DevChannels` | none | `src/plugin/HalcyonSequelBridge.cpp:397` |
| SET-24 | same | `TestLoop` is in the LE2 host; the mod's heartbeat is shared | none read | `CMakeLists.txt:1149` |
| SET-25 | same core, other inputs | same `FirstRunWanted` | the guide's texts take the `le2` flag (no Mako group, other power texts) | `src/plugin/MainMenu.cpp:895`, `src/plugin/MainMenuSettings.cpp:423` |
| SET-26 | same core, other inputs | shared `HdrDisplayPolicy`, default -1 | the 0 to -1 move is LE1's (`moveOldDefault`); LE2 was -1 from its first day | `src/plugin/HdrDisplayPolicy.cpp:30` |
| SET-27 | same | `plugin/Colour` and `core/vr/Colour` in the LE2 host, applied in `EyeCopy` | none in the settings | `CMakeLists.txt:1103` |
| SET-28 | adapter | `plugin/Calibration` in the sequel menu sources, `game/BodyCalibration` in the mod | never run with a real interaction profile (ledger G16 U) | `CMakeLists.txt:1023` |
| SET-29 | n/a | ME2's audio is Wwise: `plugin/OpenAlSoft` is not in the LE2 host and the Sound page is greyed | the mod still writes nothing for `[Audio]`; the row text says "native audio (Wwise)" | `CMakeLists.txt:905`, `assets/menu/main.rml:323`, `src/plugin/MainMenuSettings.cpp:660` |
| SET-30 | same | same blocks | none | `src/game/HandCalibration.cpp` `kSightsBlock` |

# Reinvented

| what | ME2's code | what exists | what differs | what unifying takes |
|---|---|---|---|---|
| The supported-configuration pins | `ApplyMevrBaseline` in [le2 me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp), copied again into `src/halcyon/le3/me2_menu.cpp` | `ModernBaseline` in [HalcyonBridge.cpp](../../../../src/plugin/HalcyonBridge.cpp) | three hand-kept lists; each time the owner adds a pin (body shown, 2026-11-17) it lands in one tree first | one table of intents in `src/halcyon/MevrSeams.h` (stereo life-size, first person, head hidden, body shown, no head aim, no stick pitch, flat menus, menu key, no profile hotkeys, cinema presentation, vehicle head aim off) with a per-tree function that maps an intent to its setter; a unit test that every intent has a mapping per game |
| Fixing the renderer's file | nothing at install; the pins at load | `FixHalcyonIni` in [install/Ini.cpp](../../../../src/install/Ini.cpp) | ME1 fixes the file and pins at load; ME2 only pins | acceptable as it is, if the pins cover every key `FixHalcyonIni` fixes; today `MirrorPresentEvery` and the vehicle head mode are in neither |

# Rediscovered problems

| problem on ME2 | already in ME1's list | sources | cost |
|---|---|---|---|
| The mod's default blocks landed seconds after the XR marker and a runner's load refused ("artifact changed during load") | "The mod's default blocks landed while a test's load watched the file" | [gotcha](../../gotchas/mod-settings-appended-after-xr-ready.md) (bitten 2026-11-15, 16, 17); commit 5578b770 | three days running, each a refused first load |
| Shadows differing between the eyes, and whose value wins in the game's ini | "GamerSettings is overridden by BIOEngine.ini, which the game rewrites" | [gotcha](../../gotchas/system-settings-precedence.md); commits ee4f33f3 (on, morning of 2026-11-17) and 9f556c1a (off again that night); `GameAdapter.hpp:261` | a default flipped twice in a day; Setup's `GameSpec` and `CLAUDE.md` still describe the morning's state |
| The game's HDR answer made a float frame the renderer could not copy | "Players with HDR monitors had launch trouble and a blue image" | [windows-hdr-desktop](../../gotchas/windows-hdr-desktop.md), [me2-float-swap-chain](../../gotchas/me2-float-swap-chain.md) | lanes A, B and B2 on 2026-11-14 before the shared `hdrDisplay = -1` |
| New to ME2, not a rediscovery: a `scale set` during a level load faults the render thread | (none) | [me2-scale-set-during-load](../../gotchas/me2-scale-set-during-load.md) | 9 of 9 runs hung, 2026-11-14 |

# Forced by the game

- `MELE2VR.ini` next to the exe, one `[VR]` section, profiles as file copies: Halcyon ME2's own format
  (`src/halcyon/le2/me2_menu.cpp:423`, `:846`). SET-12's four slots and SET-15's whole-file save have no counterpart.
- The 3 s wait before engine settings (SET-21): the fault is ME2's ([research](../../../../research/le2-renderer-integration.md) "Defaults from ME1").
- No OpenAL (SET-29): ME2 plays through Wwise.
- The baseline's extra pins for R (ME2's reload key) and 1-4 (ME2's power keys).
- `sharePass` standing down: ME2's renderer draws each eye itself.

# Not forced

- No headset-derived render size and no first-start size (SET-20): `MevrSeams::EyeImageSize` already knows the
  headset's size on ME2; nothing writes `RenderW/RenderH` from it.
- The greyed Display group while `[Upscale]` works, and greyed groups for holsters and sights whose modules run on ME2
  (`assets/menu/main.rml:217`, `:498`). The owner's rule is "greyed until I can see them" (commit dfd91df2), so this is
  a decision waiting on a headset check, not an oversight; the list of what is ready to un-grey is not kept anywhere.
- ME1-only keys and blocks written into `LE2MEVR.ini` (SET-04).
- Setup's `GameSpec` for LE2 against the mod's and the dev tool's values (SET-22).
- The fresh `MELE2VR.ini` carrying upstream's head aim, Insert and VR cutscenes instead of the baseline's values (SET-12).
- `VehicleHeadMode` left unpinned where ME1 pins the Mako's head aim off (SET-14).

# What to do

1. **Give ME2's render size ME1's rule** (SET-20, SET-18). Files: `src/plugin/MainMenuSettings.cpp` (`FirstStartEyeSize`,
   `SetResolution` for the sequels through `renderer::` in `src/plugin/Renderer.hpp`), `src/halcyon/le2` (RenderW/RenderH
   setter). Check: on the fake headset a fresh `MELE2VR.ini` logs `first VR start:` with 0.85 of the eye size and the
   next launch's `[SFRDIAG]` eye size matches; then un-grey the rows.
2. **Bring Setup's LE2 `GameSpec` in line with the mod** (SET-22). Files: `src/install/Game.cpp`, `Game.hpp:38`,
   `GameSettings.cpp` (`ModWrites` for what `scale set` leaves in the game's ini). Check:
   `tools/setup/test-fake-game.ps1 -Games LE2` install then uninstall byte for byte, with an ini that holds the mod's
   `DynamicShadows`, `LightEnvironmentShadows`, `MotionBlur` and `FoliageDrawRadiusMultiplier`.
3. **Write a fresh `MELE2VR.ini` with the baseline's values** (SET-12), as `HalcyonReleaseIni` does for ME1. Files:
   `tools/deploy-asi.ps1:269`, `src/install/Ini.cpp`. Check: a fresh install logs `[MEVR-BASELINE] MELE2VR.ini already
   matches the MEVR baseline`.
4. **Stop writing dead keys into `LE2MEVR.ini`** (SET-04), or mark them: guard the LE1-only blocks and keys in
   `src/game/HandCalibration.cpp` by game as `[Reload]` already is. Check: a fresh `LE2MEVR.ini` holds no key whose
   reader is under `MEVR_GAME_LE1` (a script over the blocks and `#if` guards; could be a ctest).
5. **One baseline table** (SET-14, "Reinvented"). Check: a unit test listing each intent per game; `VehicleHeadMode`
   decided with it.
6. **Keep a list of greyed rows against module status** (SET-18, "Not forced"): a table in `research/le2-menus.md`
   regenerated from `main.rml`'s `data-class-disabled="le2"` and the ledger. Check: ctest `mevr-parity-ledger` fails
   when a row is greyed for a module the ledger rates shared or adapted and verified.

# Citations

- Catalog: [ME1 settings and defaults](../../me1/settings-and-defaults.md); method: [how to catalog](../../how-to-catalog.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows G16, G19, G47. This audit agrees with G19
  "shared, unported rows greyed" and adds that rows of ported modules are greyed too; it disagrees with G47's "the
  settings and their Restore" being complete for LE2 (SET-22).
- Code: [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp), [MELE2VR.ini](../../../../src/halcyon/le2/runtime/MELE2VR.ini),
  [MainMenuSettings.cpp](../../../../src/plugin/MainMenuSettings.cpp), [main.rml](../../../../assets/menu/main.rml),
  [HandCalibration.cpp](../../../../src/game/HandCalibration.cpp), [SystemSettings.cpp](../../../../src/game/SystemSettings.cpp),
  [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp), [RenderLeversTable.hpp](../../../../src/plugin/RenderLeversTable.hpp),
  [Game.cpp](../../../../src/install/Game.cpp), [Game.hpp](../../../../src/install/Game.hpp),
  [GameSettings.cpp](../../../../src/install/GameSettings.cpp), [HdrDisplayPolicy.cpp](../../../../src/plugin/HdrDisplayPolicy.cpp),
  [CMakeLists.txt](../../../../CMakeLists.txt), [deploy-asi.ps1](../../../../tools/deploy-asi.ps1),
  [vr-game-settings.ps1](../../../../tools/vr-game-settings.ps1).
- Research: [le2-renderer-integration](../../../../research/le2-renderer-integration.md),
  [le2-menus](../../../../research/le2-menus.md), [33 installer](../../../../research/33-installer.md).
- Gotchas: [settings appended after XR ready](../../gotchas/mod-settings-appended-after-xr-ready.md),
  [ME2 scale set during load](../../gotchas/me2-scale-set-during-load.md),
  [SystemSettings precedence](../../gotchas/system-settings-precedence.md),
  [Windows HDR desktop](../../gotchas/windows-hdr-desktop.md).
