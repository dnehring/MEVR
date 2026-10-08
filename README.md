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
| M0 | ASI loads in LE1, logs, draws an ImGui overlay            | Easy |
| M1 | Plugin controls the game camera                           | Easy–medium |
| M2 | Game mirrored onto a virtual screen in the headset        | Medium |
| M3 | Head tracking drives the camera (mono)                    | Medium |
| M4 | True stereo with per-eye projection                       | **Hard: the research wall** |
| M5 | HUD on composition layers, conversation/cutscene handling | Hard |
| M6 | Motion-controlled weapons, body IK, holsters, reloads     | Hard |
| M7 | Port to LE2 and LE3 (adapters plus data tables)           | Medium |

## Layout

| Path         | What lives there |
|--------------|------------------|
| `docs/`      | Plans and decisions |
| `research/`  | Sourced findings, hypotheses to verify, all URLs ([index](research/README.md)) |
| `tools/`     | `inventory-game.ps1` (records the game install), `fetch-references.ps1` (clones reference repos) |
| `notes/`     | Generated inventories and reverse-engineering notes |
| `reference/` | Third-party repos for reading only (gitignored; recreate with `tools/fetch-references.ps1`) |
| `src/`       | The plugin (from M0 on) |

## Docs

- [Prep checklist](docs/prep-checklist.md): tools to install and what to do once the download finishes
- [Trilogy design](docs/trilogy-design.md): one VR system, the per-game mechanic changes, code layout
- [VR features plan](docs/vr-features-plan.md): gun handling, body IK, HUD, camera, conversations
- [Reuse and licensing](docs/reuse-and-licensing.md): what we can copy versus only learn from
