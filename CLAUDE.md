# ME VR Attempt

Native VR mod for Mass Effect Legendary Edition (BioWare's UE3 fork): a C++ ASI plugin loaded by
ME3Tweaks' LEBinkProxy, on top of LExSDKv2. One VR system for LE1/LE2/LE3, built LE1 first. Game
mechanics may change to fit VR (see `docs/trilogy-design.md`).

## Git

- **Commit freely.** The user gave standing permission (2026-10-07) to commit without asking.
  Commit at natural checkpoints (milestones, verified changes). No remote is configured; ask
  before pushing anywhere.

## Licensing rules (MIT project)

- Copy code only from permissive sources (REFramework is MIT) and keep their notices.
- Overlord, h2-mod and Legendary Explorer are GPL-3.0: **read for design, never copy or translate code.**
- UEVR and TF2VR are all rights reserved: never clone or copy.
- LExASIs and LEBinkProxy have no license: read for patterns; ask ME3Tweaks before copying.
- LExSDKv2: use as a git submodule only, never vendored into `src/`.

## Layout and workflow

- `research/`: sourced findings. Record new findings here with sources. Mark unverified UE3
  assumptions **[verify]** and update the hypothesis table in `research/08-ue3-hypotheses.md` as
  they're confirmed or refuted.
- `docs/`: plans and decisions. Keep them consistent with `research/`.
- `reference/`: read-only third-party clones (gitignored). Recreate with `tools/fetch-references.ps1`.
- `notes/game-inventory.md`: exe versions and hashes from `tools/inventory-game.ps1`. Re-run after
  game updates. All three exes are 2.0.0.48602 (the build LExSDKv2's RVAs target).
- `mods/`: raw mod downloads (gitignored). Unpack `.m3m` merge mods with `tools/extract-m3m.ps1`.
  The scripts we build on live in `src/script/` (e.g. `src/script/le1/massfps-base/`, MassFPS by
  Aphar, fully permissive).
- Game install: Steam app 1328670, currently `C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition`.
- Dev machine: RTX 4090, Quest via the Oculus OpenXR runtime, VS 2022 Build Tools (no IDE), CMake.

## Build

- Configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`. Build:
  `cmake --build build --config Release --target MEVR-LE1`, producing `build/Release/LE1MEVR.asi`.
- Deploy: `tools/deploy-asi.ps1 -Game LE1` (needs the ME3MM ASI loader). The log is at
  `<Win64>/Logs/LE1MEVR.log` or `%LOCALAPPDATA%/MEVR/`.
- **Hot reload (default build):** `LE1MEVR.asi` is a small host; the code lives in
  `LE1MEVR_mod.dll`, which the host reloads within about a second of each rebuild (the
  `.modpath` file points at `build/Release`). So during a play session, just rebuild
  `MEVR-LE1-mod`; the user stays in game. Changes to `src/plugin/` (the host) need the game
  closed and a redeploy. Release builds use `-DMEVR_HOT_RELOAD=OFF` (one ASI; e.g. `build-mono/`).
- Script-level hooks must wait for the script packages to load (they aren't there at attach).
  `Mod.cpp` retries every second before installing the ProcessEvent hook.
- Dev hotkeys: not F-keys (bound in game), not backtick (UE console). Backslash toggles the
  camera test; `]` toggles VR (the XR session) on and off.
- The game's D3D11 device is SINGLETHREADED: no deferred contexts, and all D3D/XR work happens
  on the render thread inside the host's Present callback. Its backbuffer is FP16 (scRGB, linear).
- Quitting from the headset's menu (session EXITING) must turn VR off, not recreate the session.
  The host hooks ExitProcess so XR shuts down before the Oculus DLLs unload.
- Our targets build with `/W4 /WX /permissive`. The SDK's generated code emits warnings
  (C4315, C4789) that aren't ours.
- The LExSDKv2 submodule tracks the `RVA` branch (hardcoded RVAs for 2.0.0.48602). The plugin
  fails closed on any other exe version.

## Gotchas

- Don't `cd` in Bash tool calls; it moves the session's working directory. Use absolute paths.

- Windows PowerShell 5.1 turns native stderr (e.g. git progress) into errors under
  `$ErrorActionPreference = 'Stop'`. Check `$LASTEXITCODE` instead.
- msbuild isn't on PATH; use a VS developer shell or CMake.
