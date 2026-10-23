# ME VR Attempt

A native VR mod for **Mass Effect Legendary Edition**: first-person 6DOF, motion-controlled
weapons, body IK, and a VR-friendly HUD. **One VR system for all three games, starting with LE1.**
Mechanics change where VR needs them to; see [docs/trilogy-design.md](docs/trilogy-design.md).

**License:** the project as a whole is [GPL-3.0](LICENSE); our original files are MIT. Each source
file names its licence on its first line; see [Licensing](#licensing) and [NOTICE.md](NOTICE.md).

## Approach

ME LE runs on BioWare's fork of Unreal Engine 3, compiled into `MassEffect1/2/3.exe`.
VR is a renderer-level feature, so it can't be ported in as content
([research/01](research/01-feasibility.md)). Instead:

- **Native ASI plugin** (C++, loaded by ME3Tweaks' LEBinkProxy) built on
  [LExSDKv2](https://github.com/llamathings/LExSDKv2). It owns OpenXR, D3D11 frame submission,
  camera/projection overrides, and stereo rendering.
- **UnrealScript side** (compiled with Legendary Explorer) for gameplay changes where script is
  the easier lever: weapon aim source, AnimTree IK effectors, HUD render target redirect.
- **Built on Halcyon's GPL MassEffect1/2/3-VR** for rendering, stereo, first person and HUD
  (imported into `src/halcyon/`), with our own motion controls and body IK on top. See
  [docs/reuse-and-licensing.md](docs/reuse-and-licensing.md).

## Milestones (LE1 first)

| #  | Milestone                                                 | Difficulty |
|----|-----------------------------------------------------------|------------|
| M0 | ASI loads in LE1, checks the exe version, hooks the engine tick, logs a heartbeat | **Done 2026-10-07** ([notes/milestones.md](notes/milestones.md)) |
| M1 | Plugin controls the game camera                           | **Done 2026-10-07** (hot-reload dev loop + yaw sweep) |
| M2 | Game mirrored onto a virtual screen in the headset        | **Done 2026-10-07** (Oculus and SteamVR/Steam Link) |
| M3 | Head tracking drives the camera (mono)                    | **Done 2026-10-07**; vertical judder fix (render-only head path) built, untested |
| M4 | True stereo with per-eye projection                       | Stage 1 (alternate-eye) built, untested. Stage 2 (two full renders per frame, research/13) next |
| M5 | HUD on composition layers, conversation/cutscene handling | Hard |
| M6 | Motion-controlled weapons, body IK, holsters, reloads     | Hard. Groundwork built: Touch controllers read via OpenXR and fed to the game as an Xbox pad |
| M7 | Port to LE2 and LE3 (adapters plus data tables)           | Medium |

## Build and deploy

Requires VS 2022 (Build Tools is enough) and CMake ≥ 3.27. Clone with submodules
(`git submodule update --init`), which brings in LExSDKv2 on its `RVA` branch.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MEVR-LE1
powershell -File tools/deploy-asi.ps1 -Game LE1
```

Output: `build/Release/LE1MEVR.asi`. Deploying needs the ME3Tweaks ASI loader installed first.
The plugin logs to `<Win64>/Logs/LE1MEVR.log`, or `%LOCALAPPDATA%/MEVR/` if the game folder
isn't writable. Undeploy with `tools/deploy-asi.ps1 -Remove`.

Offline tests (no game needed): `cmake --build build --config Release --target mevr-tests`,
then `ctest -C Release --test-dir build`. Before playing in VR, run
`tools/vr-game-settings.ps1` (VSync, motion blur, film grain off, HDR to SDR; `-Restore` undoes
it). The current test plan and hotkeys are in [notes/next-vr-session.md](notes/next-vr-session.md).

## Layout

| Path         | What lives there |
|--------------|------------------|
| `docs/`      | Plans and decisions |
| `research/`  | Sourced findings, hypotheses to verify, all URLs ([index](research/README.md)) |
| `tools/`     | `inventory-game.ps1` (records the game install), `deploy-asi.ps1` (copies the plugin into the game), `vr-game-settings.ps1` (VR-friendly game settings, with restore), `extract-m3m.ps1` (unpacks ME3MM merge mods), `fetch-references.ps1` (clones reference repos), `verify_rvas.py` (checks addresses against the exe's unwind table), `splice_text.py` (disassemblable exe from the mod's local code dump), `minidump_scan.py` (crash triage) |
| `notes/`     | Generated inventories and reverse-engineering notes |
| `external/`  | Git submodules: LExSDKv2 (`RVA` branch), OpenXR-SDK (release 1.1.63). See [NOTICE.md](NOTICE.md) |
| `reference/` | Third-party repos for reading only (gitignored; recreate with `tools/fetch-references.ps1`) |
| `mods/`      | Raw mod downloads (gitignored) |
| `src/core/`, `src/engine/`, `src/game/`, `src/plugin/`, `src/mod/` | The native plugin: game-agnostic VR core (OpenXR, D3D11, controllers), UE3/LExSDK bridge (scene view, ProcessEvent), ME gameplay adapters (head tracking, gamepad bridge, reflections), the loader host and the hot-reloadable mod |
| `tests/`     | Offline tests for game-independent math (ctest) |

## Docs

- [Prep checklist](docs/prep-checklist.md): tools to install and what to do once the download finishes
- [Trilogy design](docs/trilogy-design.md): one VR system, the per-game mechanic changes, code layout
- [VR features plan](docs/vr-features-plan.md): gun handling, body IK, HUD, camera, conversations
- [Stereo stage 2 design](docs/stereo-stage2-design.md): two full renders per frame
- [Reuse and licensing](docs/reuse-and-licensing.md): what we can copy versus only learn from

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
