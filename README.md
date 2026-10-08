# ME VR Attempt

A native VR mod for **Mass Effect Legendary Edition**: first-person 6DOF, motion-controlled
weapons, body IK, and a VR-friendly HUD. **One VR system for all three games, starting with LE1.**
Mechanics change where VR needs them to; see [docs/trilogy-design.md](docs/trilogy-design.md).

**License:** [MIT](LICENSE). **Status (2026-10-07):** prep. Game downloading via Steam (app 1328670).

## Approach

ME LE runs on BioWare's fork of Unreal Engine 3, compiled into `MassEffect1/2/3.exe`.
VR is a renderer-level feature, so it can't be ported in as content
([research/01](research/01-feasibility.md)). Instead:

- **Native ASI plugin** (C++, loaded by ME3Tweaks' LEBinkProxy) built on
  [LExSDKv2](https://github.com/llamathings/LExSDKv2). It owns OpenXR, D3D11 frame submission,
  camera/projection overrides, and stereo rendering.
- **UnrealScript side** (compiled with Legendary Explorer) for gameplay changes where script is
  the easier lever: weapon aim source, AnimTree IK effectors, HUD render target redirect.
- **Borrowed code only from permissive sources** (mainly REFramework, MIT). GPL and closed mods
  are design references. See [docs/reuse-and-licensing.md](docs/reuse-and-licensing.md).

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

## Layout

| Path         | What lives there |
|--------------|------------------|
| `docs/`      | Plans and decisions |
| `research/`  | Sourced findings, hypotheses to verify, all URLs ([index](research/README.md)) |
| `tools/`     | `inventory-game.ps1` (records the game install), `deploy-asi.ps1` (copies the plugin into the game), `extract-m3m.ps1` (unpacks ME3MM merge mods), `fetch-references.ps1` (clones reference repos) |
| `notes/`     | Generated inventories and reverse-engineering notes |
| `external/`  | Git submodules: LExSDKv2 (`RVA` branch), OpenXR-SDK (release 1.1.63). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) |
| `reference/` | Third-party repos for reading only (gitignored; recreate with `tools/fetch-references.ps1`) |
| `mods/`      | Raw mod downloads (gitignored) |
| `src/core/`, `src/engine/`, `src/plugin/` | The native plugin: game-agnostic core, UE3/LExSDK bridge, loader entry point |
| `src/script/` | UnrealScript side (e.g. `le1/massfps-base/`, MassFPS by Aphar) |

## Docs

- [Prep checklist](docs/prep-checklist.md): tools to install and what to do once the download finishes
- [Trilogy design](docs/trilogy-design.md): one VR system, the per-game mechanic changes, code layout
- [VR features plan](docs/vr-features-plan.md): gun handling, body IK, HUD, camera, conversations
- [Reuse and licensing](docs/reuse-and-licensing.md): what we can copy versus only learn from
