# ME VR Attempt

Native VR for **Mass Effect 1, Legendary Edition** (PC): stereo 3D and 6DOF on Halcyon's
[MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) renderer, with our motion controls on top:
controller aim, arm and body IK, body holsters, a two-handed rifle grip, physical grenades and melee, gun
sights and scopes, a VR main menu and a guided calibration.

**Status: development preview, LE1 only.** LE2 and LE3 build (on our own early renderer) for development,
but they aren't playable in VR. **There is no downloadable build yet:** the mod compiles in
[LExSDKv2](https://github.com/llamathings/LExSDKv2), which has no licence, so binaries wait for its author's
permission, or for our own reflection layer (`src/ue3`, [research/34](research/34-reflection.md)) to replace it.
Until then, build from source.

- **Headsets:** tested on a **Quest 3 over Steam Link** and a **PS VR2**, both through SteamVR. Meta's own
  runtime (Link, Air Link) ran the early mod (a Quest Pro, 2026-10-07) but hasn't been tested with the current
  renderer. Controller bindings are suggested for Touch, Index and the simple controller. Other headsets and
  runtimes may work, untested.
- **Game:** `MassEffect1.exe` **2.0.0.48602** (Steam or the EA app). On any other version the mod logs
  `unsupported game version` and switches itself off, leaving the game untouched.

**License:** the project as a whole is [GPL-3.0](LICENSE); our original files are MIT. Each source
file names its licence on its first line; see [Licensing](#licensing) and [NOTICE.md](NOTICE.md).

## How it works

ME LE runs on BioWare's fork of Unreal Engine 3, compiled into `MassEffect1/2/3.exe`. VR is a renderer-level
feature, so it can't be added as content ([research/01](research/01-feasibility.md)). Instead:

- **A native ASI plugin** (C++), loaded by our own `bink2w64.dll` ASI loader (`src/loader`, installed by
  `MEVR-Setup.exe`) or by ME3Tweaks' LEBinkProxy if Mod Manager installed it.
- **Halcyon's GPL-3.0 renderer** (`src/halcyon/le1`, imported verbatim, then modified with marked `[MEVR]`
  seams) owns OpenXR, stereo (two renders per frame), first person and head look.
- **Our part is the body and the hands** (VRIK): the right controller aims (no head aim), arm IK puts the gun
  in the hand, plus the two-handed grip, holsters, grenades, sights, comfort options and a HUD on panels.
  Game mechanics change where VR needs them to (no cover snapping, a physical crouch).
- **Engine access** goes through the game's own UE3 reflection: our layer `src/ue3`, replacing LExSDKv2
  step by step ([research/34](research/34-reflection.md)).

The current design is in [CLAUDE.md](CLAUDE.md) and [research/](research/README.md). The early milestone
plan is in [notes/milestones.md](notes/milestones.md).

## Build (contributors)

Requires Windows, VS 2022 (Build Tools is enough), CMake 3.27 or later, and Git. Clone with submodules
(`git submodule update --init`): LExSDKv2 (`RVA` branch), the Khronos OpenXR SDK, RmlUi and FreeType.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MEVR-LE1
powershell -File tools/deploy-asi.ps1 -Game LE1
```

- **The loader:** deploying needs an ASI loader in the game: our own `MEVR-Loader` (`bink2w64.dll`,
  installed by `MEVR-Setup.exe`, see [research/33](research/33-installer.md)) or ME3Tweaks' LEBinkProxy
  (installed by ME3Tweaks Mod Manager). Undeploy with `tools/deploy-asi.ps1 -Remove`.
- **Hot reload:** the default build is a small host ASI plus `LE1MEVR_mod.dll`, which the running game
  reloads about a second after each rebuild of `MEVR-LE1-mod`. `-DMEVR_HOT_RELOAD=OFF` builds the single ASI
  that releases use (`tools/package-release.ps1` packages it with `MEVR-Setup.exe`).
- **Tests** (no game needed): `cmake --build build --config Release --target mevr-tests`, then
  `ctest -C Release --test-dir build`.
- **Game settings:** before playing in VR, run `tools/vr-game-settings.ps1` (VSync, motion blur and film grain
  off, a borderless window, the stereo target; `-Restore` undoes it).
- Machine setup: [docs/prep-checklist.md](docs/prep-checklist.md). Hotkeys, the test loop and the current test
  plan: [CLAUDE.md](CLAUDE.md) and [notes/next-vr-session.md](notes/next-vr-session.md).

In the game, `\` or both thumbsticks held opens the VR menu.

## Known issues

- **LE1 only.** LE2 and LE3 aren't playable in VR yet.
- **Performance:** in the heaviest scenes the render thread needs about 11 ms a frame for the two scene
  renders ([research/35](research/35-performance-attempt-log.md), [37](research/37-single-pass-stereo.md)),
  so 90 Hz isn't held everywhere. A lower render resolution (Display in the VR menu) helps the GPU.
- **Much of it is new:** the project started in October 2026; many features have been tried in the headset only
  a few times, some not yet (see [notes/next-vr-session.md](notes/next-vr-session.md)).
- **Steam's "verify files"** can remove the ASI loader: run the setup's Repair, or reinstall the loader.

## Reporting a bug

Before reporting, [docs/troubleshooting.md](docs/troubleshooting.md) covers the common problems; your AI assistant can read it.

Open an issue at <https://github.com/dnehring/MEVR/issues> with:

- **what you did** and what happened (the steps, where in the game, and what you expected);
- **your headset and OpenXR runtime** (for example Quest 3 over Steam Link, SteamVR);
- **the mod's log:** `Game\ME1\Binaries\Win64\Logs\LE1MEVR.log` (and `LE1MEVR.prev.log`, the run before; if the
  game folder isn't writable they are in `%LOCALAPPDATA%\MEVR\`);
- **after a crash:** `%LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt` (the one before is `crash-breadcrumbs.prev.txt`);
- **the renderer's log:** `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt`;
- **the loader's log:** `Game\ME1\Binaries\Win64\Logs\MEVRLoader.log` (with our loader).

A snapshot helps with anything visual: Advanced > Diagnostics > Snapshot in the VR menu (or a quick tap of both
stick clicks) saves what each eye sees to `%LOCALAPPDATA%\MEVR\snapshots`.

**Logs can contain your Windows user name** in file paths. Look through them, and replace it if you prefer,
before you attach them.

## Layout

| Path | What lives there |
|------|------------------|
| `src/core/` | Game-independent VR core: OpenXR, D3D11, controllers, IK, calibration, comfort, colour and sight math (unit-tested) |
| `src/ue3/` | Our UE3 reflection layer: objects, names, classes, fields, functions and calls, read from the engine's own reflection data ([research/34](research/34-reflection.md)) |
| `src/engine/` | Engine hooks and adapters: ProcessEvent, bone poses, the scene view, per-game field spellings, the RVA table, and `engine/Legacy` (the last LExSDKv2 uses) |
| `src/game/` | ME gameplay: hand aim, holsters, squad weapons, grenades, posture, comfort, sights, calibration applied |
| `src/plugin/` | The host ASI: the bridge into Halcyon's renderer, our quads (HUD, reticle, wheel, sights, vignette), the test loop, the perf probe, crash breadcrumbs |
| `src/plugin/menu/` | The VR main menu's engine: RmlUi's DX11 renderer wrapped for the game, our decorators and icons, the data model, the frosted backdrop |
| `src/mod/` | The hot-reloadable mod DLL's entry and API |
| `src/halcyon/` | Halcyon's MassEffect1-VR renderer (GPL-3.0), imported verbatim and then modified; `mevr_bridge.h` is our seam interface into it |
| `src/install/` | The install library behind `MEVR-Setup.exe`: game detection, the manifest, ini edits, exact uninstall (unit-tested) |
| `src/loader/` | Our ASI loader, `bink2w64.dll`: forwards the game's Bink calls and loads `ASI\*.asi` with the SPI interface ([research/33](research/33-installer.md)) |
| `assets/` | The menu's look: `assets/menu/main.rml`/`.rcss` (in game) and `setup.rml`/`.rcss` (the installer) |
| `tools/` | Deploy, game settings, test loop, perf probe, Ghidra scripts, RVA checks, offline previews (`menu-preview`, `sight-preview`, `calibration-preview`...) and packaging |
| `tools/setup/` | `MEVR-Setup.exe`, the players' installer (Welcome, Install, Optional content, Settings, Repair, Uninstall, Launch) |
| `tests/` | Offline tests (ctest) |
| `third_party/` | Dear ImGui, MinHook and the menu's fonts (Rajdhani, Titillium Web), each with its licence |
| `external/` | Git submodules (LExSDKv2, OpenXR-SDK, RmlUi, FreeType) and AMD's FSR 1 headers (`external/fsr1`). See [NOTICE.md](NOTICE.md) |
| `docs/` | Plans and decisions |
| `research/` | Sourced findings, hypotheses to verify, all URLs ([index](research/README.md)) |
| `notes/` | Milestone log, test plans, inventories, upstream patches for Halcyon |
| `reference/`, `mods/` | Third-party repos for reading and raw mod downloads (both gitignored; `tools/fetch-references.ps1` recreates the first) |

## Docs

- [Developer machine setup](docs/prep-checklist.md): tools to install, first setup of a game install
- [Development approach](docs/development-approach.md): build on Halcyon, own the VRIK, the test loop, Ghidra
- [Squad weapons design](docs/squad-weapons-design.md): giving squadmates weapons from the left hand
- [Reuse and licensing](docs/reuse-and-licensing.md): what we can copy versus only learn from
- [Release README](docs/release-readme.md): the players' README for a future release
- [Troubleshooting](docs/troubleshooting.md): symptoms, the log lines that confirm them, and fixes (for players and
  their AI assistants)
- Historical plans (2026-10-07): [trilogy design](docs/trilogy-design.md), [VR features plan](docs/vr-features-plan.md),
  [stereo stage 2 design](docs/stereo-stage2-design.md)

## Credits

- **Halcyon:** [MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) (GPL-3.0), the renderer this mod is
  built on.
- **Aeka0:** [Overlord](https://github.com/Aeka0/Overlord) (GPL-3.0): parts of the arm IK, two-hand aim, support
  grip and sight assist.
- **Aphar:** [MassFPS](https://www.nexusmods.com/masseffectlegendaryedition/mods/1731), studied for its
  first-person camera (none of its files are in the repository).
- **ME3Tweaks and llamathings:** LExSDKv2, LEBinkProxy and Legendary Explorer, the LE modding tools this work
  stands on.
- **Fonts:** Rajdhani (Indian Type Foundry) and Titillium Web (Accademia di Belle Arti di Urbino), SIL OFL 1.1.
- **Libraries:** [RmlUi](https://github.com/mikke89/RmlUi), [FreeType](https://freetype.org),
  [Dear ImGui](https://github.com/ocornut/imgui), [MinHook](https://github.com/TsudaKageyu/minhook),
  [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR), the
  [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK).

## Licensing

- **The project as a whole is GPL-3.0** ([LICENSE](LICENSE), also
  [LICENSES/GPL-3.0-only.txt](LICENSES/GPL-3.0-only.txt)). The host ASI links Halcyon's GPL-3.0
  renderer, so a build of the mod is a GPL-3.0 work.
- **Our original files are MIT** ([LICENSES/MIT.txt](LICENSES/MIT.txt)). Every source file names its
  licence on its first line (`SPDX-License-Identifier: MIT` or `GPL-3.0-only`): go by that line, not by
  the folder. An MIT file can be taken into any project under MIT.
- **These files stay GPL-3.0-only**, because they contain GPL-3.0 code (neither upstream says "or later"):
  - `src/halcyon/`: Halcyon's [MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR), imported and
    modified, together with `mevr_bridge.h`, our seam interface into it;
  - the files adapted from [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): `src/core/ik/ArmPose.*`,
    `src/core/ik/TouchReference.*`, `src/core/ik/TwoHand.*`, `src/core/vr/Sights.*` (the alignment
    assist's spring), `src/engine/BonePose.cpp` (`ApplySupport`) and `src/game/GamepadMapping.*`
    (`UpdateSupportGrip`);
  - `notes/upstream-halcyon/*.patch`: diffs of Halcyon's code.
- **Third-party components keep their own licences:** Dear ImGui (MIT), MinHook (BSD-2-Clause) and the
  fonts (OFL-1.1) in `third_party/`, FSR1 (MIT) in `external/fsr1`, and the submodules in `external/`.
  [NOTICE.md](NOTICE.md) lists each one.
- **Docs and data** (the Markdown in `docs/`, `research/` and `notes/`, and data files without an
  identifier) are MIT unless marked otherwise.
- New files get the right identifier on their first line; `python tools/check-spdx.py` lists the files
  without one. Builds aren't published yet: LExSDKv2 has no licence, so binaries wait for its author's
  permission ([docs/reuse-and-licensing.md](docs/reuse-and-licensing.md)).

Mass Effect is a trademark of EA International (Studio and Publishing) Ltd. This is a fan project, not affiliated
with or endorsed by EA or BioWare.
