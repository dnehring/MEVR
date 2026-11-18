---
type: Audit
title: ME3 against ME1's settings files and shipped defaults
description: Contract by contract, how LE3 reads, writes and defaults its settings (ASI\LE3MEVR.ini, MELE3VR.ini, the menu's rows, the baseline pins, Setup's game settings) against ME1's catalog, and where it repeats or avoids ME2's findings.
tags: [audit, me3, parity, settings, defaults, ini, menu, baseline]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/settings-and-defaults.md
commit: 583591c6
---

# Verdict

Audited against [ME1's settings catalog](../../me1/settings-and-defaults.md) by reading code and notes at 583591c6; no
game was run, so every row is "read from the code" unless it names a run.

- The file layer is ME1's own code on ME3, exactly as on ME2: one reader (`core/Ini`), one path (`ASI\LE3MEVR.ini`), the
  same blocks, migrations, pending writes, `[Menu]`, `[Colour]`, `[Debug]`. 19 of 30 contracts are "same" or "same core".
- **The render size has no owner on ME3 either.** It is a fixed 3072x2216 (the owner's answer to question 17) whatever
  the headset is; nothing sets it from the headset (SET-20) and the menu's Display group is greyed (SET-18). The refusal's
  log line even says "not on Mass Effect 2 yet" on ME3.
- **The menu refuses writes for features ME3 now has.** `UnavailableInLe3Presentation` drops every `voice_*`, `mic*`,
  `world_ui`, `wrist_*` and `galaxy_holo` write, although the voice host, the wrist screens and the galaxy map were
  ported after that list was written; the Voice page's rows are not greyed, so a toggle there does nothing (SET-07).
- **The controller guide that opens once on a first run is one paragraph on ME3** that says tracked weapon, power and
  squad controls "require their game adapters" (SET-25), while holsters, the power wheel and squad placement run there.
- Better than ME2: the fresh `MELE3VR.ini` is ours and already holds the baseline's values (SET-12). Worse than ME2:
  `MoveFollowsHead` is read from the file, not forced, while the menu assumes it is on (SET-14).

| rating | count | ids |
|---|---|---|
| same | 12 | SET-01, 02, 03, 05, 08, 09, 10, 19, 23, 24, 27, 30 |
| same core, other inputs | 7 | SET-04, 06, 07, 11, 21, 25, 26 |
| adapter | 4 | SET-12, 14, 16, 28 |
| reinvented | 0 | |
| partial | 3 | SET-13, 17, 22 |
| missing | 2 | SET-18, 20 |
| n/a | 2 | SET-15, 29 |
| unknown | 0 | |

# Contract by contract

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| SET-01 | same | `SettingsFile` with the game's name macro; the sequel host reads the same path | none | `src/plugin/HalcyonSequelBridge.cpp:126`, `CMakeLists.txt:1151` |
| SET-02 | same | `core/Ini` is in `MEVR_CORE_SOURCES`, every target | none | `CMakeLists.txt:1039` |
| SET-03 | same | our code as ME1; Halcyon ME3's own readers use `_wtof` and `swprintf_s %.3f` (the C locale), ME1's caveat | none for our file | `src/halcyon/le3/me2_menu.cpp:436`, `:538` |
| SET-04 | same core, other inputs | the same `Refresh`; a new file gets every ME1 block plus `[Reload]`; `[Holsters]` gains the SMG keys and ME3's `le3PutAway` | ME1-only blocks (`[Mako]`, `[Cockpit]`, `[Vent]`, `[Game]`) and ME1-only `[Hands]` keys (`linearWalk`, `linearStick`, `startStopAnims`, `constantEye`, `drawRate`) are written into `LE3MEVR.ini` with ME1's comments though their readers are `MEVR_GAME_LE1`-only | `src/game/HandCalibration.cpp:2962`, `:2964`, `:3023`, `:3031`, `:313` |
| SET-05 | same | same code | none | `src/game/HandCalibration.cpp:2956` |
| SET-06 | same core, other inputs | RenderLevers is in the LE3 host (`MEVR_SEQUEL_RENDER_PERF_SOURCES`) | LE3's layout row: `sharePass` and `minScreenRadiusPx` are unverified there, so those keys are read and do nothing; `umbraShareResults` defaults to 1 on ME3 (measured, ME1's is 0). `plugin/QuestPacing` is not in the LE3 host: `earlyFrameWait`, `pacingRecovery`, `frameLoop` have no reader | `CMakeLists.txt:1184`, `src/plugin/RenderLeversTable.hpp:255`, `:260`; `CMakeLists.txt:914` (LE1 list only) |
| SET-07 | same core, other inputs | `MainMenuSettings.cpp` and `PendingWrites` are in the LE3 host | `Settings::Write` returns at once for `cine_*`, `thespian_*`, `voice_*`, `mic*`, `world_ui`, `wrist_screen`, `wrist_width`, `galaxy_holo`. The Voice page's rows (Voice orders, Recognizer size, the microphone list) are not greyed on ME3 and `plugin/Voice` is in the LE3 host, so those rows change nothing in the file (reading; not run) **Fixed in code, lane AU1 (2026-11-18, not run in the game):** `cine_*` writes again (the 3D screen is ME3's default). | `src/plugin/MainMenuSettings.cpp:857`, `src/core/vr/MenuSettings.cpp:900`, `assets/menu/main.rml:289`, `CMakeLists.txt:1191` |
| SET-08 | same | the same section list | none | `src/plugin/MainMenuSettings.cpp:280` |
| SET-09 | same | same `ReadMenuSection` | none | `src/plugin/MainMenuSettings.cpp:250` |
| SET-10 | same | same code path; never seen in an ME3 run | none known | `src/game/HandCalibration.cpp:2970` |
| SET-11 | same core, other inputs | ME1's migrations compile and run (none can fire on a file LE3 created); the sequels' `[Reload] enabledMoved` | one more migration, one more appended key (`le3PutAway`) | `src/game/HandCalibration.cpp:3314`, `:3032` |
| SET-12 | adapter | `deploy-asi.ps1 -Game LE3` and the release payload install our own `runtime/MELE3VR.ini` next to the exe when none is there; one `[VR]` section | better than ME2: the fresh file already says `HeadAim=0`, `DecoupledPitch=1`, `CineVrConvo=0`, `CineVrCutscene=0`, `MoveFollowsHead=1`, `MetricEyeFrustum=1`, `ConvoFirstPerson=0`. It has no `MenuKey`, `CoverThirdPerson`, `ProfileHotkeys` or `RecenterKey` (code defaults Insert, 1, 1, R), which only the pins correct. `MirrorPresentEvery=8` where ME1 ships 2 | `src/halcyon/le3/runtime/MELE3VR.ini`, `tools/deploy-asi.ps1:281`, `tools/package-release.ps1:314`, `src/halcyon/le3/me2_menu.cpp:547` |
| SET-13 | partial | no file fix at install, repair or deploy; an existing `MELE3VR.ini` is never written. The intent is met at load by `ApplyMevrBaseline` | a foreign or old file keeps every unpinned key: `MoveFollowsHead`, `DecoupledYaw`, `MetricEyeFrustum`, `ConvoFirstPerson`, `MirrorPresentEvery`, `RenderW/RenderH` | `src/install/Installer.cpp:446`, `src/halcyon/le3/me2_menu.cpp:480` |
| SET-14 | adapter | `ApplyMevrBaseline` at the end of every `LoadValues`, logged `[MEVR-BASELINE]`; life-size stereo set just before it | a third hand-written list: 17 pins. Pinned there and not on ME1: first person in cover, body shown in every state, R recenter off, flat conversations and cutscenes. Not pinned: `MoveFollowsHead` (ME2 forces it at `le2 me2_menu.cpp:585`; ME3 reads the file's value), `MetricEyeFrustum` (ME1 pins `sfr2MetricEyeFrustum` on), `DecoupledYaw`, `ConvoFirstPerson`. The menu's Reset is greyed; no render-size rewrite | `src/halcyon/le3/me2_menu.cpp:480`, `:542`, `:689`, `:700`; `assets/menu/main.rml:637` |
| SET-15 | n/a | ME3's `SaveValues` writes key by key with `WritePrivateProfileStringW`; no whole-file write to protect | a file unreadable at launch loads code defaults and a later save writes them back key by key: not guarded (unverified) | `src/halcyon/le3/me2_menu.cpp:705` |
| SET-16 | adapter | `renderer::SaveOwnSettings` (ME3's `SaveValues`) when the menu closes | saves the pinned live values too, as ME1 does | `src/plugin/MainMenuSettings.cpp:796`, `src/plugin/RendererLe3.cpp:104` |
| SET-17 | partial | the mod's file ships ME1's defaults; `MELE3VR.ini` ships the runner's preview values, checked against it by `test-session.ps1` | no "diff against the owner's file" step for ME3 and no owner-tuned ME3 renderer profile; the fresh file is tied to a test runner's config, not to a release rule | `src/halcyon/le3/runtime/MELE3VR.ini:3`, `docs/release-checklist.md` |
| SET-18 | missing | the Display group (Performance, Render resolution, Upscale, Motion smoothing) is greyed by the `le2` flag, which the LE3 host also sets | `[Upscale] method` works on ME3 (shared eye pipeline in `EyeCopy`) but sits in the greyed group | `assets/menu/main.rml:448`, `src/plugin/MainMenuSettings.cpp:301`, `:1095` |
| SET-19 | same | shared `readInt` | none | `src/plugin/RenderLevers.cpp` |
| SET-20 | missing | the size is `MELE3VR.ini [VR] RenderW/RenderH`, default 3072x2216, written to GamerSettings by Halcyon at attach; `FirstStartEyeSize` is an empty function and `SetResolution` refuses with "not on Mass Effect 2 yet" | no 0.85 of the headset's eye size, no `eyeSizeSet`; every headset renders 1536x2216 an eye and FSR scales it to the headset's size | `src/plugin/MainMenuSettings.cpp:801`, `:1505`; `tools/vr-game-settings.ps1:107`; `src/install/Game.cpp:29` |
| SET-21 | same core, other inputs | the same module and values, each boolean through its opposite | (1) "left gameplay" is `mode > 4` and "back" is `mode <= 4`: ME1's and ME2's numbers. ME3's gameplay modes run to 6 (5 squad command, 6 injured), so each squad command and each injured stretch counts as leaving gameplay; with a healthy motion-blur guard (in the LE3 host) the reapply is skipped and only logged. (2) `LightEnvironmentShadows False` always, not only while the second eye is a replay. (3) ME2's 3 s wait after a level change is `MEVR_GAME_LE2`-only; whether ME3 has ME2's fault is not recorded | `src/game/SystemSettings.cpp:183`, `:226`, `:237`, `:143`; `src/engine/GameAdapter.hpp:57`, `:276`; `CMakeLists.txt:1149` |
| SET-22 | partial | `ApplyGamerSettings(..., game)`: VSync, MotionBlur, FilmGrain, anisotropy, borderless, HDR SDR; ResX/ResY and DepthOfField recorded | LE3's `GameSpec` has every VR flag false: no DynamicShadows, no foliage radius (LE2 has it), no `BIOEngine.ini` record, no `SmoothFrameRate`. The mod sets DynamicShadows, LightEnvironmentShadows, MotionBlur and the foliage radius by `scale set` and the game stores them; the dev tool writes DynamicShadows and LightEnvironmentShadows False for LE3. Whether Uninstall puts back what the game stored is untested | `src/install/Game.cpp:25`, `src/install/GameSettings.cpp:119`, `tools/vr-game-settings.ps1:140` |
| SET-23 | same | shared `DevChannels` | none | `src/plugin/HalcyonSequelBridge.cpp:397` |
| SET-24 | same | `TestLoop` is in the LE3 host; the mod's heartbeat is shared | none read | `CMakeLists.txt:1154` |
| SET-25 | same core, other inputs | same `FirstRunWanted` and `[Menu] controlsShown` | on ME3 the guide section is a single paragraph, not the groups built from the settings (`GuideTexts`): it names no button or gesture, and says tracked weapon, power and squad controls "require their game adapters" | `src/plugin/MainMenu.cpp:895`, `assets/menu/main.rml:81` |
| SET-26 | same core, other inputs | shared `HdrDisplayPolicy`, default -1 | the 0 to -1 move is LE1's (`moveOldDefault`); LE3 was -1 from its first day | `src/plugin/HdrDisplayPolicy.cpp:15`, `:32` |
| SET-27 | same | `plugin/Colour` and `core/vr/Colour` in the LE3 host, applied in `EyeCopy` | none in the settings | `CMakeLists.txt:1154` |
| SET-28 | adapter | `plugin/Calibration` in the sequel menu sources, `game/BodyCalibration` in the mod | never run with a real interaction profile (ledger G16 U). The calibration is saved in `LE3MEVR.ini`: a player calibrated in ME1 is uncalibrated in ME3 (the owner's answer of 2026-11-18 is "shared") | `CMakeLists.txt:1001`, `:1187` |
| SET-29 | n/a | `plugin/OpenAlSoft` is not in the LE3 host; the Sound page is greyed | the mod writes nothing for `[Audio]` | `CMakeLists.txt:883` (LE1 list only), `assets/menu/main.rml:323` |
| SET-30 | same | same blocks | none | `src/game/HandCalibration.cpp` `kSightsBlock` |

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| The render size has no owner (SET-20, SET-18) | **same defect**, with a fixed 3072x2216 instead of upstream's 4096x2304. `Settings::SetResolution` and `FirstStartEyeSize` are one `#if !defined(MEVR_MENU_LE1)` body for both sequels |
| `LE2MEVR.ini` written with ME1's whole file, dead keys included (SET-04) | **same defect** (the same `Refresh`), plus `le3PutAway` |
| Setup's `GameSpec` lags the mod and the dev tool (SET-22) | **same defect, wider**: LE3 has no foliage radius either |
| The baseline as a third hand-written list (SET-14) | **same**; ME3's list is the newest (pinned 2026-11-17, commit 8e64bc49) and differs from ME2's in `MoveFollowsHead` |
| The fresh renderer file ships upstream's values (SET-12) | **fine**: ME3's fresh file is ours and holds the baseline's values, except the four keys only the pins correct |
| `VehicleHeadMode` unpinned | **different**: ME3's tree has no such key in its loader; head aim is one pin. Its Atlas and turrets were not looked at here |
| ME2's 3 s wait before engine settings (SET-21) | **not carried**: the guard is `MEVR_GAME_LE2`. Unknown whether ME3 needs it |
| Greyed groups for modules that run (SET-18, "Not forced") | **same**, and ME3 adds refused writes behind rows that are not greyed (SET-07) |
| Mechanism 1 (a gate in ME1's terms) | **present**: `SystemSettings.cpp:183` and `:226` test `mode > 4` |
| Mechanism 5 (ME1's constant taken as the game's) | **present**: ME1's settings block written into `LE3MEVR.ini` |

# Reinvented

| what | ME3's code | what exists | what differs | what unifying takes |
|---|---|---|---|---|
| The supported-configuration pins | `ApplyMevrBaseline` in [le3 me2_menu.cpp](../../../../src/halcyon/le3/me2_menu.cpp) | `ModernBaseline` in [HalcyonBridge.cpp](../../../../src/plugin/HalcyonBridge.cpp), `ApplyMevrBaseline` in [le2 me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp) | three hand-kept lists. ME3's lacks ME2's forced `MoveFollowsHead` and ME1's pinned eye frustum; it has "first person in cover", which the others don't need | the ME2 audit's step 5: one table of intents in `src/halcyon/MevrSeams.h` with a per-tree setter, and a unit test that each intent has a mapping per game |
| "Is this key available on this game" | `UnavailableInLe3Presentation` in [MenuSettings.cpp](../../../../src/core/vr/MenuSettings.cpp), a prefix list | the `le2` / `le3` flags that grey rows in [main.rml](../../../../assets/menu/main.rml), and `renderer::Capabilities()` | two lists that say different things: a row can be live in the markup and refused in the code | one source: the write path asks the same capability the markup greys by |

Nothing else in this category is a second implementation: ME3's settings code is ME1's files behind game guards.

# Rediscovered problems

| problem on ME3 | already in ME1's list | sources | cost |
|---|---|---|---|
| ME3 ran upstream's configuration (third person from the start, the head shown, Insert) until its pins were written | "A slot missing from `MELEVR.ini` loaded Insert, head aim and third person" | commit 8e64bc49 (2026-11-17), three days after ME2's pins and five weeks after ME1's | every ME3 run before it started in third person |
| The mod's default blocks landed after the XR marker and a runner's load refused | "The mod's default blocks landed while a test's load watched the file" | [gotcha](../../gotchas/mod-settings-appended-after-xr-ready.md): ME3's preview met it first, 2026-11-15 17:10; ME2 on the 15th, 16th and 17th | one refused load on ME3; the fix was made per runner, so ME2 paid three more times |
| Dynamic shadows on, then off again within a day | "GamerSettings is overridden by BIOEngine.ini, which the game rewrites" | commits ee4f33f3 and 9f556c1a; [gotcha](../../gotchas/system-settings-precedence.md) | Setup's `GameSpec` comment (`Game.hpp:38`) and `CLAUDE.md` still describe the morning's state for both sequels |

# Forced by the game

- `MELE3VR.ini` next to the exe, one `[VR]` section, profiles as file copies: Halcyon ME3's own format
  (`src/halcyon/le3/me2_menu.cpp:421`). SET-12's four slots and SET-15's whole-file save have no counterpart.
- No OpenAL (SET-29): the LE3 host has no `OpenAlSoft`.
- The baseline's extra pins for R (ME3's reload) and 1-4 (its power keys), and for first person in cover (upstream ME3
  went third person there).
- `sharePass` standing down: ME3's renderer draws each eye itself.
- Game modes 5 and 6 being gameplay (squad command, injured Shepard): `src/engine/GameAdapter.hpp:57`.

# Not forced

- No headset-derived render size (SET-20): `MevrSeams::EyeImageSize` knows the headset's size on ME3 too.
- `MoveFollowsHead` left to the file while the menu assumes it is on (SET-14).
- Refused writes behind live rows (SET-07) and the one-paragraph controller guide (SET-25).
- ME1-only keys and blocks written into `LE3MEVR.ini` (SET-04).
- Setup's `GameSpec` for LE3 against the mod's and the dev tool's values (SET-22).
- `mode > 4` in `game/SystemSettings` where `engine::adapter::IsHudGameplayMode` already translates the modes (SET-21).
- The refusal text "not on Mass Effect 2 yet" and the comment "ME2:" on shared sequel code paths.

# What to do

1. **Make the menu's write path agree with its rows** (SET-07). Files: `src/core/vr/MenuSettings.cpp:900`,
   `assets/menu/main.rml`. Check: `mevr-menu-preview` with the `le3` flag and a unit test that every key
   `UnavailableInLe3Presentation` refuses belongs to a row greyed under `le3`.
2. **Give ME3 the real controller guide** (SET-25): the `GuideTexts` groups for what runs on ME3, as ME2 has. Files:
   `assets/menu/main.rml:81`, `src/plugin/MainMenuSettings.cpp:418`. Check: `mevr-menu-preview`'s guide page for LE3.
3. **Give the sequels' render size ME1's rule** (SET-20, SET-18), one change for ME2 and ME3. Files:
   `src/plugin/MainMenuSettings.cpp:801`, `:1505`, `src/plugin/Renderer.hpp`, a RenderW/RenderH setter in each tree.
   Check: on the fake headset a fresh `MELE3VR.ini` logs `first VR start:` with 0.85 of the eye size.
   **Owner question:** does question 17's 3072x2216 stand as ME3's fixed size, or was it the stand-in until this rule?
4. **Pin or force `MoveFollowsHead` on ME3 as ME2 does** (SET-14), with the single baseline table. Check: a
   `MELE3VR.ini` with `MoveFollowsHead=0` logs the pin and the stick walks where the player looks.
5. **Translate `mode > 4`** (SET-21) through `engine::adapter::IsHudGameplayMode`. Check: ctest
   `mevr-system-settings-tests` built for LE3 with modes 5 and 6.
6. **Bring Setup's LE3 `GameSpec` in line with the mod** (SET-22), with ME2's. Check:
   `tools/setup/test-fake-game.ps1 -Games LE3` install then uninstall byte for byte with an ini that holds the mod's four keys.
7. **Stop writing dead keys into `LE3MEVR.ini`** (SET-04), with ME2's step 4.
8. **Decide the 3 s wait for ME3** (SET-21): one run that applies `scale set` during a level load with depth of field on.

# Citations

- Catalog: [ME1 settings and defaults](../../me1/settings-and-defaults.md); method: [how to catalog](../../how-to-catalog.md);
  ME2's audit of the same file: [settings and defaults](../me2/settings-and-defaults.md), [summary](../me2/summary.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows G16, G19, G47, R11. This audit agrees with G19
  "shared" for the menu's open and resume and adds the refused writes and the guide. It disagrees with G47's LE3 cell
  "no release `MELE3VR.ini`" (`tools/package-release.ps1:314` ships one) and with "the settings and their Restore" being
  complete (SET-22).
- Catalog rows read as wrong about ME1: the code map's "builds the menu into the LE1 and LE2 hosts" (the LE3 host gets
  `MEVR_SEQUEL_MENU_SOURCES` too, `CMakeLists.txt:1187`); the table "Game files written by Setup and
  `tools/vr-game-settings.ps1`" says DynamicShadows is LE1-only, which is true of Setup and not of the tool
  (`vr-game-settings.ps1:140`); SET-21 leaves out the reapply on returning from a non-gameplay mode.
- Code: [le3 me2_menu.cpp](../../../../src/halcyon/le3/me2_menu.cpp), [MELE3VR.ini](../../../../src/halcyon/le3/runtime/MELE3VR.ini),
  [MainMenuSettings.cpp](../../../../src/plugin/MainMenuSettings.cpp), [MenuSettings.cpp](../../../../src/core/vr/MenuSettings.cpp),
  [main.rml](../../../../assets/menu/main.rml), [HandCalibration.cpp](../../../../src/game/HandCalibration.cpp),
  [SystemSettings.cpp](../../../../src/game/SystemSettings.cpp), [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp),
  [RenderLeversTable.hpp](../../../../src/plugin/RenderLeversTable.hpp), [RendererLe3.cpp](../../../../src/plugin/RendererLe3.cpp),
  [Game.cpp](../../../../src/install/Game.cpp), [Game.hpp](../../../../src/install/Game.hpp),
  [GameSettings.cpp](../../../../src/install/GameSettings.cpp), [Installer.cpp](../../../../src/install/Installer.cpp),
  [HdrDisplayPolicy.cpp](../../../../src/plugin/HdrDisplayPolicy.cpp), [CMakeLists.txt](../../../../CMakeLists.txt),
  [deploy-asi.ps1](../../../../tools/deploy-asi.ps1), [vr-game-settings.ps1](../../../../tools/vr-game-settings.ps1),
  [package-release.ps1](../../../../tools/package-release.ps1).
- Research: [le3-renderer-integration](../../../../research/le3-renderer-integration.md),
  [sequel-render-levers](../../../../research/sequel-render-levers.md), [33 installer](../../../../research/33-installer.md).
- Gotchas: [settings appended after XR ready](../../gotchas/mod-settings-appended-after-xr-ready.md),
  [SystemSettings precedence](../../gotchas/system-settings-precedence.md),
  [ME2 scale set during load](../../gotchas/me2-scale-set-during-load.md).
