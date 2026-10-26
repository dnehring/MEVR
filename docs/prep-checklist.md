# Developer machine setup

What a contributor's machine needs to build, deploy and debug the mod. Players don't need any of
this: they run `MEVR-Setup.exe` (`tools/setup`, [research/33](../research/33-installer.md)), which
installs the mod and our own ASI loader without Mod Manager.

## What a development machine needs

| Item | Notes |
|------|-------|
| GPU | A VR-capable one (developed on an RTX 4090) |
| Headset runtime | An OpenXR runtime: SteamVR, Meta (Oculus) or Virtual Desktop |
| C++ toolchain | VS 2022 Build Tools (MSVC x64), CMake, Git |
| .NET | 8.0 desktop runtime (Legendary Explorer needs it) |
| ME LE install | Steam app 1328670 (or the EA app) |

## Tools

| Tool | Why | Where |
|------|-----|-------|
| ME3Tweaks Mod Manager (optional) | Vanilla backups, packaging mods, and its ASI loader (LEBinkProxy), which the mod also loads through. Not needed for loading: MEVR-Setup installs our own loader | https://me3tweaks.com/modmanager/ |
| Legendary Explorer | Decompile and compile UnrealScript; inspect packages, AnimTrees, Scaleform movies, conversations | https://github.com/ME3Tweaks/LegendaryExplorer/releases |
| RenderDoc | Frame captures: find the view/projection constant buffers, the HUD pass, and post-process passes | https://renderdoc.org/ |
| Ghidra + JDK 21 | Static analysis of `MassEffect1.exe` (native camera/view code) | https://ghidra-sre.org/, https://adoptium.net/ |
| x64dbg | Runtime debugging and breakpoints in the live game | https://x64dbg.com/ |
| VS 2022 Community (optional) | IDE debugger to attach to the game while the plugin runs. Build Tools can compile but has no debugger UI | https://visualstudio.microsoft.com/vs/community/ |

## Optional: move the install off Program Files

Writing freshly built DLLs and reading logs under `Program Files (x86)` triggers UAC on every
iteration. Steam can move the game: Properties → Installed Files → Move
install folder → a library on another drive (ME LE is about 120 GB).

## If you use SteamVR (Steam Link, Virtual Desktop's SteamVR mode, or a SteamVR headset)

- Set SteamVR as the OpenXR runtime: SteamVR → Settings → OpenXR → "Set SteamVR as OpenXR
  Runtime".
- **Stop SteamVR putting the game on its Theater Screen:** SteamVR → Settings → enable "Show
  Advanced Settings" → **Dashboard** → turn off the option that presents non-VR applications on
  the Theater Screen at launch. Otherwise SteamVR mirrors the game window onto its own screen on
  top of the mod's output.
  - The old per-game Steam option "Use Desktop Game Theatre while SteamVR is active" (Properties
    → General) was removed with SteamVR 2.1 in late 2023. Don't look for it.

## First setup of a game install

1. Launch LE1 once from Steam, without mods, and get in-game. This confirms the EA App handoff
   works and generates config files.
2. **Make a vanilla backup** (ME3Tweaks Mod Manager, or a copy of the folder) before installing anything.
3. Install an ASI loader: run `MEVR-Setup.exe` from a package built by `tools/package-release.ps1`
   (`dist\MEVR-<version>`) and press Install, which puts our own `bink2w64.dll` in place (`src/loader`); or use Mod
   Manager's Tools → ASI Mod Manager, which installs LEBinkProxy. Then `tools/deploy-asi.ps1` copies
   the development build in.
4. Run `tools/inventory-game.ps1` to record exe versions and hashes in `notes/`. **Check that
   `MassEffect1.exe` is FileVersion 2.0.0.48602.** LExSDKv2 and our RVA table (`engine/Addresses.hpp`)
   hardcode engine addresses for that build; on any other version the plugin logs `unsupported game
   version`, leaves the game untouched and stays off ([research/09](../research/09-me3tweaks-code-survey.md)).
5. ~~Download MassFPS (LE1)~~. **Done 2026-10-07:** downloaded to `mods/` and extracted with
   `tools/extract-m3m.ps1` for study ([research/11](../research/11-massfps.md)). The extracted files
   stay local; they aren't in the repository.
6. Open LE1's startup package and `SFXGame` in Legendary Explorer to start verifying the hypotheses
   in [vr-features-plan.md](vr-features-plan.md).

## Homework while you wait: play Overlord and TF2VR with a notebook

Their code is only partly reusable, but their *design* is free to learn from. Note down:

- **Weapon feel:** where the grip point sits, how two-handed stabilization engages, whether aim
  smoothing is noticeable.
- **Reload choreography:** which steps are physical and which are automatic. LE2/LE3 thermal clips
  map well to a "grab clip from belt, slam it in" gesture. LE1 has no ammo; weapons overheat
  instead.
- **Holsters:** locations, sizes, and how forgiving the grab zones are.
- **HUD:** what moved onto the gun or wrist, what stayed on a floating panel, and how menus are
  pointed at.
- **Comfort:** turning, vignettes, and how forced camera moves (cutscenes, vehicles) are handled.
