# ME VR Attempt

Native VR mod for Mass Effect Legendary Edition (BioWare's UE3 fork): a C++ ASI plugin loaded by
ME3Tweaks' LEBinkProxy, on top of LExSDKv2. One VR system for LE1/LE2/LE3, built LE1 first. Game
mechanics may change to fit VR (see `docs/trilogy-design.md`).

## Git

- **Commit freely.** The user gave standing permission (2026-10-07) to commit without asking.
  Commit at natural checkpoints (milestones, verified changes). No remote is configured; ask
  before pushing anywhere.

## Licensing rules (GPL-3.0 project since 2026-10-08)

- The project is GPL-3.0 (`LICENSE`, `NOTICE.md`). Code from GPL-3.0-compatible sources may be
  copied and modified: GPL-3.0 (Halcyon's MassEffect1/2/3-VR, Overlord, h2-mod, Legendary
  Explorer) and permissive (REFramework, vrframework, MinHook, ImGui). Keep their copyright and
  license notices, and list them in `NOTICE.md`. GPL-2.0-only code is **not** compatible.
- Halcyon's code lives in `src/halcyon/`: imported verbatim in its own commit, then modified, so
  `git log` shows exactly what we changed.
- Closed binaries (e.g. the ME2VR download): never disassemble, decompile or run them. Their
  READMEs, configs and log strings are fair game for concepts.
- UEVR and TF2VR are all rights reserved: never clone or copy.
- LExASIs and LEBinkProxy have no license: read for patterns; ask ME3Tweaks before copying.
- LExSDKv2: use as a git submodule only, never vendored into `src/`.

## Renderer (LE1): Halcyon's, inside our host

- **Direction (user, 2026-10-08): our work is VRIK** (controller aim, arm IK, the gun in the hand,
  two-handed grip, the reticle along the gun). Halcyon's experience is the core: touch it as little
  as possible, only to fix its bugs (like the HDR blue screen) or where VRIK needs a seam. Before
  changing one of its settings or behaviours, compare with upstream (`reference/MassEffect1-VR/
  release/`, `vr_config.h` defaults). **No head aim:** the deploy writes `combatHeadAim = 0` and
  `makoHeadAim = 0`; the hand aims, the head drives the view only.
- Since 2026-10-08 LE1's VR renderer is Halcyon's (`src/halcyon/le1`, built as the `halcyon-le1`
  static library and linked into the **host** ASI, so it isn't hot-reloaded). It owns OpenXR,
  Present, stereo (two renders per present), first person (K), head look and its Insert menu.
  Its log is `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt`; its settings are `MELEVR.ini` in
  `Binaries\Win64\ASI`. Its keys: `\` menu (`menuKey = 220` in our deploy; upstream uses Insert;
  both thumbsticks held opens or closes it, our addition; the open menu blocks all game input),
  R recenter (or double-click the right stick), K first person. Our own VR hotkeys are off on LE1.
- Its menu takes the pad (Touch merged in as Xbox pad 0: left stick/d-pad, A, B; upstream never
  set ImGui's `HasGamepad` backend flag, so pad nav was dead until we did) and our laser pointer
  (`src/plugin/MevrMenuPointer.cpp`: a controller's aim ray on the head-locked quad is the mouse,
  trigger clicks, right stick scrolls). Our own tab, "VR hands" (`src/plugin/MevrMenuTab.cpp`),
  edits `[Hands]` in `ASI\LE1MEVR.ini` (the mod re-reads it) and writes the arm-length sample
  (`[Arms] measureRequest` counter plus grip/head positions; `armLengthM` is shown if present).
- Our seams into it are marked `[MEVR]` and declared in `src/halcyon/le1/mevr_bridge.h`
  (implemented in `src/plugin/HalcyonBridge.cpp`): static OpenXR loader, our controller actions
  on its session, Touch → Xbox pad 0 in its XInput hooks. Keep changes to its files small and
  marked; `git log -p -- src/halcyon` must show every change.
- The mod learns from `HostServices::controllers` that the host owns VR, and then skips our own
  VR output (`core/vr/Output`), scene view hook, reflection fix, XInput hook and VR hotkeys.
- **UObject::ProcessEvent is hooked by the host at attach and never removed** (all games); the
  mod's handlers run through `HostServices::setProcessEventCallback`. Halcyon hooks it too, with
  its own MinHook, on top of ours: never uninstall a hook another hooker may sit on.
  LE2/LE3 still use our own renderer until Halcyon's ME2/ME3 code is brought in.

## Before each VR milestone

Read the matching chapter of `reference/vrframework/guides/` (MIT, a 17-part guide on adding
VR to closed engines): 07 frame timing, 08 stereo, 09 camera math, 10 submission and TAA,
11 HUD, 12 input, 14 engine quirks. Don't re-derive what it already explains.
`research/12-vr-frameworks-survey.md` explains why we keep our own small core rather than
switching frameworks.

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
- Hotkeys: not F-keys (bound in game), not backtick (UE console). `]` toggles VR (the XR
  session) on and off, and with it every VR feature: once a session runs, alternate-eye stereo,
  6DOF, hand aim and arm IK all come on; VR off turns them off. The rest are dev overrides:
  `[` switches between head-tracked and screen mode; `\` cycles the head path (render-only /
  camera / off); `'` cycles the desktop head simulator (sweep / nod / off, used while VR isn't
  head-tracked); within a session `;` (stereo), `/` (6DOF), `,` (hand aim: shots follow the right
  controller, on foot) and `.` (VRIK arm IK) switch one feature each, until the next `]` on;
  `=` toggles the automatic virtual screen for conversations, cutscenes and menus (default on);
  `-` toggles the lockstep (game thread waits for each Present; default off). These are for our
  own renderer (LE2/LE3); on LE1 Halcyon's keys apply instead.
- Motion controllers reach the game as Xbox pad 0 (`game/GamepadBridge`, XInput hooks). Mapping
  is in `game/GamepadMapping` (unit-tested).
- LE1 HUD (Halcyon's renderer, Stereo 2): the game's UI draws go to a body-locked HUD panel quad,
  and a hand reticle on the aim ray replaces the crosshair (`src/plugin/HalcyonOverlays.cpp`,
  [research/15](research/15-hud.md)). Toggles: `MELEVR.ini` `hudPanel*` / `handReticle*`, or the
  Insert menu's HUD tab.
- Trilogy check: `build-trilogy/` (configured with `-DMEVR_BUILD_LE2=ON -DMEVR_BUILD_LE3=ON`) must
  keep building all three games. Per-game field spellings go in `src/engine/GameAdapter.hpp`;
  per-game addresses sit next to their use (e.g. `engine/SceneView.cpp`).
- Offline tests: `cmake --build build --config Release --target mevr-tests`, then
  `ctest -C Release --test-dir build`. Put game-independent math there.
- `tools/vr-game-settings.ps1` sets VR-friendly GamerSettings.ini values (VSync, motion blur,
  film grain off, HDR to SDR); `-Restore` puts the user's originals back. On LE1 it also does what
  Halcyon's installer (`MELE-VR.bat`) does: a borderless window (a bordered one is capped at the
  monitor's height, which broke stereo), dynamic shadows off, frame smoothing off, and its
  near-square stereo target (`-Quality`, default Performance 3072x3188), mirrored into
  `MELEVR.ini`. The deploy installs Halcyon's *release* `MELEVR.ini` (its tuned code defaults),
  not the older `src/halcyon/le1/runtime/MELEVR.ini`, which pins stale values.
- The mod connects to Windows' active OpenXR runtime (Oculus, or SteamVR for Steam Link). The
  user currently uses Steam Link / SteamVR.
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
- **Never call LExSDKv2's generated function wrappers** (e.g. `USceneCaptureComponent::SetFrameRate`).
  All 5,301 LE1 native wrappers do `FunctionFlags |= ~0x400` (meant `&=`), which permanently sets
  every flag on that UFunction. Find the UFunction yourself and call ProcessEvent directly.
  Class layouts, `StaticClass()` and `IsA()` are fine.
- The exe's `.text` is encrypted on disk (EA DRM). Check RVAs statically with
  `tools/verify_rvas.py` (`.pdata` function bounds); byte checks only work in-process. The mod
  saves the decrypted `.text` once per exe build to `%LOCALAPPDATA%\MEVR\dump`;
  `tools/splice_text.py` turns it into a disassemblable copy (`dumpbin /disasm /range:`).
  Local research data: never commit or share dumps or spliced exes.

- Windows PowerShell 5.1 turns native stderr (e.g. git progress) into errors under
  `$ErrorActionPreference = 'Stop'`. Check `$LASTEXITCODE` instead.
- msbuild isn't on PATH; use a VS developer shell or CMake.
