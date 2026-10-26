# MEVR: Mass Effect 1 in VR

<!-- DRAFT for the GitHub release (research/33). tools/package-release.ps1 copies it into the release as README.md. -->

> **Not for release yet.** The mod is built on LExSDKv2, which has no licence. Its author's permission is needed
> before this build can be published. (Maintainers: remove this note once you have it.)

VR for **Mass Effect 1, Legendary Edition** on PC: stereo 3D, head tracking, and your hands as Shepard's hands. You
aim with the controller, carry guns in body holsters, hold a rifle with both hands, and throw real grenades. Built on
Halcyon's MassEffect1-VR renderer.

## What you need

- **Mass Effect Legendary Edition** from Steam or the EA app, up to date: `MassEffect1.exe` version **2.0.0.48602**.
  The mod stays switched off on any other version.
- **A PC VR headset** and its OpenXR runtime, set as the active one: SteamVR, the Meta app (Quest over Link), or
  Virtual Desktop.
- Windows 10 or 11 (64-bit) and a recent GPU. VR renders the game twice per frame.
- **Tested:** Quest 3 (Steam Link) and PS VR2, both through SteamVR. Other headsets and runtimes may work but are
  untested.

You **don't** need ME3Tweaks Mod Manager. If you already use it, that's fine: the setup finds its ASI loader and uses
it.

## Install

1. Download `MEVR-<version>.zip` from the Releases page and unzip it anywhere. Keep the `payload` folder next to
   `MEVR-Setup.exe`.
2. Start the game once and quit, so it creates its settings file.
3. Run **MEVR-Setup.exe**. It finds the game, checks its version and your VR runtime, and tells you what it will do.
4. Press **Install**.
5. Press **Launch**. It starts Mass Effect 1 directly, past the launcher's game picker.

In the game, `\` (or both thumbsticks held) opens the VR menu.

### What Install changes

Everything goes into the game's own folder, and every change is recorded (`Binaries\Win64\ASI\MEVR-install.json`)
with a backup, so **Uninstall puts the game back exactly**:

- `Game\ME1\Binaries\Win64\ASI\`: the mod (`LE1MEVR.asi`), its menu and fonts, Halcyon's renderer settings
  (`MELEVR.ini`, only if you have none yet), and a copy of the setup (to uninstall or add optional content later).
- `bink2w64.dll`: the ASI loader that loads the mod. The game's own file is kept as `bink2w64_original.dll`. This is
  skipped if ME3Tweaks' loader is already installed.
- The VR game settings in `GamerSettings.ini` (if you leave them on): a borderless window at the VR resolution, and
  VSync, motion blur, film grain and HDR off. Your own values are kept, and **Settings > Restore mine** puts them back.

If Steam's "verify files" removes the loader, run the setup's **Repair**.

## Optional content: texture packs

The **Optional content** page lists texture packs made by others, such as ALOT for ME1, Improved Static Lighting
and LEUITM. They're never included in this download:

- Their authors' terms don't allow re-uploads.
- Nexus Mods only lets premium members' apps download files directly.

So each entry opens the pack's own page. You download it there, point the setup at the file, and it installs the pack
with **Mass Effect Modder** (GPL-3.0, downloaded from its own GitHub releases once you agree).

Texture packs change the game's files **for good**:

- Install them after every other content mod.
- Only a full restore or reinstall of the game removes them.
- Uninstalling the VR mod leaves them in place.

From inside the game, **Display > Optional content > Install after I quit** opens the setup on that page once you
quit.

## Uninstall

Run `MEVR-Setup.exe` (or the copy in `Game\ME1\Binaries\Win64\ASI`) and use **Uninstall**. It removes what it added,
puts back what it replaced, and returns your game settings, except any you changed yourself since. Choose **Keep my
VR settings** to keep your calibration and renderer profiles for a later reinstall.

## Troubleshooting

- **"Unsupported game version"**: update the game in Steam or the EA app.
- **The launcher shows its game picker**: use the setup's **Launch** button, or pick Mass Effect 1 in the launcher.
- **No VR**: make sure your headset's runtime is the active OpenXR runtime (SteamVR: Settings > OpenXR; Meta app:
  General > OpenXR runtime).
- **Logs**:
  - the mod: `Game\ME1\Binaries\Win64\Logs\LE1MEVR.log`
  - the loader: `Game\ME1\Binaries\Win64\Logs\MEVRLoader.log`
  - the renderer: `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt`

## Reporting a problem

Open an issue at <https://github.com/dnehring/MEVR/issues> with:

- **what you did** and what happened (the steps, where in the game, and what you expected);
- **your headset and OpenXR runtime** (for example Quest 3 over Steam Link, SteamVR);
- **the mod's log:** `Game\ME1\Binaries\Win64\Logs\LE1MEVR.log` (and `LE1MEVR.prev.log`, the run before);
- **after a crash:** `%LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt`;
- **the renderer's log:** `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt`;
- **the loader's log:** `Game\ME1\Binaries\Win64\Logs\MEVRLoader.log`.

For anything visual, the VR menu's **Advanced > Diagnostics > Snapshot** saves what each eye sees to
`%LOCALAPPDATA%\MEVR\snapshots`.

**Logs can contain your Windows user name** in file paths. Look through them, and replace it if you prefer, before
you attach them.

## Licences and credits

- **This project:** GPL-3.0 (`LICENSE`); third-party notices in `NOTICE.md`.
- **Halcyon's MassEffect1-VR renderer:** GPL-3.0. Copyright (C) 2026 Halcyon.
- **Overlord** by Aeka0 (GPL-3.0): parts of the arm IK, two-hand aim, support grip and sight assist.
- **Libraries:** Dear ImGui (MIT), MinHook (BSD-2-Clause), RmlUi (MIT), FreeType (FreeType License), the Khronos OpenXR loader
  (Apache-2.0), AMD FidelityFX FSR 1 (MIT).
- **Fonts:** Rajdhani and Titillium Web (SIL OFL 1.1; their licences are in `payload\fonts`).
- **LExSDKv2** (the engine headers the mod is built on): no licence given; used with its author's permission
  *(pending)*.
- **Never included:** RAD's Bink DLL (the setup only renames your own copy), Mass Effect Modder, and any texture
  pack.

Mass Effect is a trademark of EA International (Studio and Publishing) Ltd. This is a fan project, not affiliated with
or endorsed by EA or BioWare.
