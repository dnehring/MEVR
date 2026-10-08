# Prep checklist

## Machine audit (2026-10-07)

| Item | Status |
|------|--------|
| GPU | RTX 4090 |
| Headset runtime | Oculus OpenXR runtime is active (Quest via Link); Virtual Desktop and SteamVR also installed |
| C++ toolchain | VS 2022 **Build Tools** 17.14 with MSVC x64, CMake, Git, gh |
| .NET | 8.0 desktop runtime present (Legendary Explorer needs it) |
| ME LE install | `C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition` (downloading) |
| Free space | C: 529 GB, H: 184 GB |
| Missing | Ghidra (+ JDK 21), RenderDoc, x64dbg, Legendary Explorer, ME3Tweaks Mod Manager |

## Install while the download runs

| Tool | Why | Where |
|------|-----|-------|
| ME3Tweaks Mod Manager | Installs LEBinkProxy (the ASI loader), makes vanilla backups, packages mods | https://me3tweaks.com/modmanager/ |
| Legendary Explorer | Decompile and compile UnrealScript; inspect packages, AnimTrees, Scaleform movies, conversations | https://github.com/ME3Tweaks/LegendaryExplorer/releases |
| RenderDoc | Frame captures: find the view/projection constant buffers, the HUD pass, and post-process passes | https://renderdoc.org/ |
| Ghidra + JDK 21 | Static analysis of `MassEffect1.exe` (native camera/view code) | https://ghidra-sre.org/, https://adoptium.net/ |
| x64dbg | Runtime debugging and breakpoints in the live game | https://x64dbg.com/ |
| VS 2022 Community (optional) | IDE debugger to attach to the game while the plugin runs. Build Tools can compile but has no debugger UI | https://visualstudio.microsoft.com/vs/community/ |

## Optional: move the install off Program Files

Writing freshly built DLLs and reading logs under `Program Files (x86)` triggers UAC on every
iteration. After the download, Steam can move the game: Properties → Installed Files → Move
install folder → `H:\SteamLibrary` (ME LE is about 120 GB; H: has 184 GB free).

## When the download finishes

1. Launch LE1 once from Steam, without mods, and get in-game. This confirms the EA App handoff
   works and generates config files.
2. **Make a vanilla backup** in ME3Tweaks Mod Manager before installing anything.
3. Install the ASI loader through Mod Manager (Tools → ASI Mod Manager). That installs LEBinkProxy.
   Then `tools/deploy-asi.ps1` can copy our plugin in.
4. Run `tools/inventory-game.ps1` to record exe versions and hashes in `notes/`. **Check that
   `MassEffect1.exe` is FileVersion 2.0.0.48602.** LExSDKv2 hardcodes engine addresses for that
   build, and a mismatch aborts the game at plugin attach ([research/09](../research/09-me3tweaks-code-survey.md)).
5. ~~Download MassFPS (LE1)~~. **Done 2026-10-07:** downloaded to `mods/` and extracted with
   `tools/extract-m3m.ps1` into `src/script/le1/massfps-base/` ([research/11](../research/11-massfps.md)).
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
