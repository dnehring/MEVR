# 33: A standalone installer (2026-10-23)

**Question (user, 2026-10-23).** Distribute the mod on GitHub as an exe or installer, so a player needn't know
about ME3Tweaks Mod Manager: a small app that installs everything, with a texture pack as an optional extra.

**[inference]** marks our reasoning, **[verify]** what a real install, a headset session or a network run has to
settle, **[untested]** code written against documentation and never run here (no downloads were allowed), and
**not found** what was looked for and not found.

## Summary

1. **`MEVR-Setup.exe`** (`tools/setup`): a Win32 window in the in-game menu's glass style (the same RmlUi setup,
   D3D11 renderer, decorators, fonts and `main.rcss`, plus `assets/menu/setup.rml`/`.rcss` and seven icons added to
   `MenuDecorators`). Pages: Welcome (status), Install, Optional content, Settings (the VR game settings), Repair,
   Uninstall, Launch. The work is the `mevr-install` library (`src/install`), unit-tested and also driven by the exe's
   command line (`--status`, `--install`, `--repair`, `--apply-settings`, `--restore-settings`, `--uninstall`), which
   `tools/setup/test-fake-game.ps1` runs against a fake game folder.
2. **Our own ASI loader** (`src/loader`, `bink2w64.dll`, GPL-3.0, new code): forwards all 79 Bink exports to
   `bink2w64_original.dll`, loads `ASI\*.asi`, and offers LExSDKv2-style plugins (ours) the SPI interface they expect.
   If ME3Tweaks' LEBinkProxy (or another proxy) is already there, the setup uses it instead.
3. **A manifest** (`ASI\MEVR-install.json`) records every file added, replaced (with a backup in `ASI\MEVR-backup`),
   renamed or edited (key by key), so Uninstall restores the game exactly, and never resets a value the player changed
   since.
4. **Launch** starts `MassEffect1.exe` directly with the arguments the Legendary Edition launcher would pass, past its
   game picker.
5. **Optional content**: a catalogue of third-party texture packs. Their Nexus terms forbid re-uploads, and Nexus only
   gives direct download links to premium members' registered apps, so each entry opens the pack's own page; the
   player downloads it there and picks the file; the setup then installs it with Mass Effect Modder's command line
   (GPL-3.0, fetched from its GitHub releases after the player confirms name, size and time). **[untested]**
6. **Update check** against GitHub Releases (`MEVR-Setup.ini [Update] repo`, default
   `https://github.com/dnehring/MEVR`, the public repository since 2026-10-23; `https://github.com/OWNER/REPO` turns the
   check off). **[untested]**
7. **In game**: Display > Optional content shows the installed packs and "Install after I quit", which writes
   `%LOCALAPPDATA%\MEVR\setup.request`; the host's ExitProcess hook then starts `ASI\MEVR-Setup.exe --request
   --wait-pid <pid>`.
8. **Packaging**: `tools/package-release.ps1` builds `build-mono` and assembles `dist\MEVR-<version>` (and a zip).
9. **Licensing blocker**: LExSDKv2 has no licence, and the ASI compiles it in, so a public release needs its author's
   permission (below).

## 1. The loader (`src/loader`)

### What the game needs from `bink2w64.dll`

The game imports RAD's Bink video DLL by the name `bink2w64.dll`. Its export table (dumpbin `/exports` on the game's
own `bink2w64_original.dll`, SHA-256 `59DB3EE4...04A04A`, the same file in `Game\Launcher`; reading an export table
isn't decompiling) has **79 named exports**, ordinals 1-79, from `BinkAllocateFrameBuffers` to `RADTimerRead`
(`src/loader/Forwarders.hpp` lists them). LEBinkProxy 2.0.0.14's DLL exports 76 names (dumpbin; not looked into further).

- **Forwarders.** Each export is a linker forwarder (`/EXPORT:BinkClose=bink2w64_original.BinkClose,@2`, as
  `#pragma comment(linker)` lines): the Windows loader resolves them to the original, so no Bink call runs our code.
  The same entries in a `.def` file failed to link (LNK2001 for every name) **[inference: the MSBuild/CMake .def
  route; not investigated, the pragma form works]**.
- The forwarded module is found by the normal DLL search, which starts in the exe's folder (`Binaries\Win64`).

### What LExSDKv2 plugins need ("SPI")

Our host ASI is written against LExSDKv2's `SPI.h` (the SDK is a submodule; we don't copy it). A plugin exports
`SpiSupportDecl` (name, author, version, game flags `1 << game`, minimum SPI version), `SpiShouldPreload`,
`SpiShouldSpawnThread`, `SpiOnAttach(ISharedProxyInterface*)` and `SpiOnDetach`; the interface is six virtual
functions in order: `GetVersion`, `GetBuildMode`, `GetHostGame`, `FindPattern` (PEiD-style, at most 100 bytes / 299
characters), `InstallHook` (writes the original before the hook is live), `UninstallHook`, each returning a 16-bit
`SPIReturn`. `src/loader/SpiAbi.hpp` declares that binary interface on its own (no SDK code in the loader).

- **Verified** by `tools/loader-test` (ctest `mevr-loader`): a host process loads our `bink2w64.dll` beside a
  stand-in original with the same 79 names, and a test ASI **compiled against the SDK's own `SPI.h`**: every
  export resolves, a forwarded call reaches the original, the ASI is attached on its own thread, and through the
  SDK's declaration GetVersion (3), GetBuildMode, GetHostGame (not a game: FailureGeneric), FindPattern (a pattern
  from the host's own code with wildcards; malformed; too long), InstallHook (detour runs, original callable,
  duplicate refused) and UninstallHook all behave.

### Loading and the DRM

1. `DllMain` starts one thread (the work runs after the loader lock is released).
2. In `MassEffectLauncher.exe` it does nothing (we install only into `Game\ME1\Binaries\Win64`).
3. It loads `ASI\*.asi` in name order. SPI plugins are attached by their flags: "preload" at once, the rest after the
   DRM; each on a thread of its own if it asks (ours does: `SPI_PLUGINSIDE_ASYNCATTACH`). A plugin for another game or
   a newer SPI is loaded but not attached. Plain ASIs (no SPI exports) are only loaded.
4. **The DRM wait.** The LE exes carry EA's DRM: an `.ooa` section, and `.text` encrypted on disk (CLAUDE.md). Our
   own check: 256 windows of 16 bytes spread over `.text` are read from the file once and compared with memory
   (through `ReadProcessMemory` on ourselves, so a page the DRM hasn't opened yet fails instead of faulting); decrypted
   once at least 90% differ (`LoaderCore::LooksDecrypted`), polled every 1-10 ms, at most 60 s. **Checked offline**
   against our decrypted dump (`%LOCALAPPDATA%\MEVR\dump`, `MassEffect-60AEEFA2.text.bin`): all 256 windows differ
   from the file, and no 4 KB page start matches (0 of 3995). An exe without `.ooa` isn't waited for.
   LEBinkProxy instead polls for a known byte pattern (read, not copied).
   **[verify]** in the game: that `LE1MEVR.asi` attaches after decryption and Halcyon still hooks DXGI before the
   game makes its device (the same timing LEBinkProxy gives it).
5. Hooks are MinHook (BSD-2, `third_party/minhook`, its own copy in the loader: `mevr-minhook`). Halcyon's ASI has
   its own MinHook too, as with LEBinkProxy.
6. At `DLL_PROCESS_DETACH` it calls `SpiOnDetach` for attached plugins (our host treats that as the process exiting;
   its ExitProcess hook has usually shut down already).
7. The log is `Binaries\Win64\Logs\MEVRLoader.log`.

What LEBinkProxy also does and ours doesn't: the developer console, minidumps (`-enableminidumps`), the launcher's
autoboot (`-game N -autoterminate`) and its launcher working-directory fix.

### Coexistence

`install::detect::ClassifyBink` reads `bink2w64.dll`'s version resource:

| State | How it's recognised | What the setup does |
|-------|--------------------|---------------------|
| Original | CompanyName "RAD Game Tools, Inc." (the user's: product "Bink and Smacker", 2022.05) | renames it to `bink2w64_original.dll`, installs ours |
| Ours | ProductName "MEVR ASI loader" (`src/loader/Loader.rc`) | updates it |
| LEBinkProxy | CompanyName "ME3Tweaks" / ProductName "LEBinkProxy (RELEASE)" (the user's 2.0.0.14) | uses it, installs only the ASI |
| Another proxy | anything else with `bink2w64_original.dll` behind it | uses it, with a note |
| Missing / Broken | no DLL, or a proxy with no original | refuses; says to verify the game's files |

Repair puts our loader back if Steam's "verify files" restored RAD's DLL over it. A loader another tool installed in
place of ours later is never touched by Repair or Uninstall (section 7, item 2).

## 2. Launching LE1 directly

Players starting through Steam meet the Legendary Edition launcher's game picker (with LEBinkProxy in the launcher,
`bink2w64_proxy.log` notes "couldn't find '-game '": it only autoboots when given `-game N`). What the launcher and
the tools around it do:

- LEBinkProxy's launcher module (read for behaviour only, never copied), given `-game 1`, starts
  `..\ME1\Binaries\Win64\MassEffect1.exe` with ` -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles N
  -OVERRIDELANGUAGE=X` (LE3: `-language=`), N and X from `Documents\BioWare\Mass Effect Legendary
  Edition\LauncherConfig.cfg` (`Language`, `EnglishVOEnabled`, `SubtitleSize`; default 20 and INT), and with
  `-autoterminate` exits the launcher. `{locale}` is passed literally.
- LE1's language codes (local voices / English voices): en_US INT/INT, fr_FR FR/FE, de_DE DE/GE, it_IT IT/IE, ja_JP
  JA/JA, es_ES ES/ES, ru_RU RA/RU, pl_PL PLPC/PL.
- ME3Tweaks Mod Manager (GPL-3.0, `GameLauncher.cs`) runs `Game\ME1\Binaries\Win64\MassEffect1.exe` itself. For
  Steam installs it first makes sure Steam runs with a logged-in user (`HKCU\Software\Valve\Steam\ActiveProcess`
  `pid`, `ActiveUser`), sets `SteamAppId`, `SteamGameId` and `SteamOverlayGameId` to 1328670, and writes
  `steam_appid.txt` (`1328670`) next to the exe. If the `link2ea` protocol is registered, it goes through
  `link2ea://launchgame/1328670?platform=steam&theme=met` instead.

**Our Launch button** (`install::launch::StartLe1`): for Steam installs, Steam first (started minimised if needed,
up to 60 s for a logged-in user) and the three variables; then `MassEffect1.exe` with the launcher's arguments from
`LauncherConfig.cfg`, in `Binaries\Win64`. The installer writes `steam_appid.txt` for Steam installs (recorded, so
Uninstall removes it); the user's game already has one. "Through Steam instead" opens `steam://rungameid/1328670`
(the picker shows).

- **[verify]**: a direct start of the Steam build (whether EA's DRM now insists on the EA app, i.e. whether the
  `link2ea` route is needed); the EA app build (Mod Manager does nothing special for it).
- A Steam launch option to skip the picker (`-game 1 -autoterminate`) would need LEBinkProxy in `Game\Launcher`:
  **not found** documented; not offered.

## 3. Texture packs (research by a subagent, 2026-10-23; pages read, nothing downloaded)

| Pack | Author | Size | Install | Terms (Nexus "Permissions") | Automated download |
|------|--------|------|---------|-----------------------------|--------------------|
| **ALOT for ME1** (Nexus 363, v2022.1.4, ~166k unique downloads) | CreeperLava (ALOT team) | 175.5 MB (.mem); 142.8 MB as M3TO | MEM (permanent), or M3TO (a DLC folder, reversible; needs Mod Manager and ME3Tweaks' TextureOverride ASI) | no uploads elsewhere; asset use and modification need permission; no conversions | no: Nexus page only |
| **Improved Static Lighting (ISL) for ME1** (same page) | CreeperLava | 9.7 GB (.mem) | MEM | as above | no |
| **LEUITM** (Nexus 1825, v1.0, June 2023) | Catachrism | 1.2 GB of .mem variants (one per character) | MEM | no uploads elsewhere; permission for asset use | no |
| 4k upscaled armor and cloth textures LE1 (Nexus 3166, v1.3) | cisicisi | 2.9 GB MEM / 3.1 GB M3TO | MEM or M3TO; after ALOT and ISL | no uploads elsewhere | no |

- ALOT for the Legendary Edition exists, on Nexus, not in the old ALOT Installer (ME3Tweaks, May 2021: the installer
  stays original-trilogy only).
- Other LE1 texture mods found on Nexus (Garrus and turians, Liara, the Normandy crash flashback, Sol, FemShep):
  permissions **not checked**.
- **Nexus rules.** Its API gives download links directly to premium members only. Non-premium members' apps need the
  key and expiry from the site's "Mod Manager Download" (`nxm://`) link: "all non-premium members must access the
  website". Public apps must register for an SSO slug, may not use personal API keys, and may not fetch data en masse
  to rehost it. The Terms allow downloads for personal use and redistribution only where the uploader allows it.
  **So no automated download**: our entries open the page; registering an `nxm://` handler is a later option.
- **Recommendation (adopted):** ALOT for ME1 first (the standard pack, made by the MEM authors, moderate size, textures
  only); ISL second, marked large and never preselected; LEUITM third.

### Mass Effect Modder (MEM)

- github.com/MassEffectModder/MassEffectModder: **GPL-3.0** (`COPYING.txt`; file headers GPL-2.0-or-later). The
  latest release found was v534 (2026-04-02); every release is titled "For LE only". The CLI is the asset
  `MassEffectModderNoGui-v534.7z` (6.9 MB). We never bundle it: the setup fetches the latest release's asset list
  from GitHub's API (accepting only `github.com` download URLs), shows its name, size and an estimated time, and
  downloads it on the player's OK into `%LOCALAPPDATA%\MEVR\tools`. It's unpacked with Windows' `tar.exe`, which reads
  7z on current Windows 11 **[untested, inference]**.
- **The CLI** (`CmdLine/CmdLineHelp.cpp`): `--install-mods --gameid <1-3> --input <dir or .mfl> [--repack]
  [--skip-markers] [--ipc] [--alot-mode] [--limit-2k] [--verify]`. The checks are `--check-game-data-after`,
  `-mismatch` and `-vanilla`, `--check-for-markers`, `--detect-mods` and `--detect-bad-mods`. Also
  `--set-game-data-path --gameid N --path <root>`, `--get-game-paths`, `--unpack-archive`, `--version`. A `.mfl` file
  is one .mem path per line. ME3Tweaks runs `--install-mods --gameid N --input list.mfl --verify --ipc` after
  `--set-game-data-path`.
- **Progress:** `--ipc` prints `[IPC]<command> <argument>` lines. The stages are `STAGE_CONTEXT
  STAGE_PRESCAN|SCAN|INSTALLTEXTURES|VERIFYTEXTURES|MARKERS|DONE`. Also `TASK_PROGRESS <percent>`, `PROCESSING_FILE`,
  `ERROR...` and `EXCEPTION_OCCURRED`. `install::packs::ParseIpc` reads them.
- **The game path:** MEM's own ini, then HKLM `...\WOW6432Node\BioWare\Mass Effect Legendary Edition` `Install
  Dir`, then the Steam uninstall key; we pass `--set-game-data-path`.
- **What an install does** (`MiscModsInstall.cpp`): it needs write access; it refuses known-bad mods; it scans once
  into `%LOCALAPPDATA%\MassEffectModder\mele1map.bin`; it installs, verifies and writes markers; it updates the TOCs
  and **writes `GamerSettings.ini`**. So the setup re-applies our VR game settings afterwards.
- **The rules** (Mod Manager's own strings): texture mods go **after all other content mods**; content mods are
  blocked on a texture-modded game; restoring single files isn't allowed; only a full restore (or reinstall) undoes
  textures. The marker is "ThisIsMEMEndOfFileMarker" at the end of packages; for LE1 ME3Tweaks reads it from
  `BioGame\CookedPCConsole\SFXTest.pcc`. `install::packs::TexturesMarked` checks that file. Whether Steam's "verify
  files" fully undoes a texture install is **not confirmed**.

### The flow built (`install/Packs`, the Optional content page) [untested]

1. "Open its page": the browser on the pack's Nexus page.
2. The setup scans Downloads for a matching file, or the player picks it.
3. "Install" shows a confirmation: permanent, after content mods, size, close the game.
4. MEM `--unpack-archive` into `%LOCALAPPDATA%\MEVR\packs\<id>` (or the .mem file as it is), then a `.mfl`,
   `--set-game-data-path`, and `--install-mods --gameid 1 --input <mfl> --verify --ipc` with live progress.
5. The pack is recorded in the manifest, and the VR game settings are applied again.

Uninstalling the VR mod leaves textures in place, and says so.

## 4. The install (`src/install`)

- **Finding the game** (`detect::FindInstalls`): Steam's `libraryfolders.vdf` and `appmanifest_1328670.acf` (as
  `tools/common.ps1`), the Steam uninstall key's `InstallLocation`, and the EA app / Origin key
  `HKLM\SOFTWARE\(WOW6432Node\)BioWare\Mass Effect Legendary Edition` `Install Dir` (what MEM and Legendary
  Explorer read). Both registry views are checked. The player can also pick any folder in or above the root.
- **The version**: `MassEffect1.exe`'s numeric file version must be 2.0.0.48602 (`kSupportedGameVersion`).
  Otherwise the Welcome page says the mod stays off on other versions, and Install refuses (`--ignore-version`
  exists for tests).
- **VR**: `HKLM\SOFTWARE\Khronos\OpenXR\1` `ActiveRuntime` (or `XR_RUNTIME_JSON`), classified by the manifest's file
  name: `steamxr_win64.json` is SteamVR, `oculus_openxr_64.json` Meta, `virtualdesktop-openxr.json` Virtual Desktop,
  `MixedRealityRuntime.json` WMR, `pimax-openxr.json` Pimax, `VarjoOpenXR.json` Varjo. With no runtime it warns.
- **The headset's eye size** is read from Halcyon's log ("[XR] recommended eye image 2824x2880", the last session's),
  else Detect asks OpenXR (an instance, the HMD system, its stereo views; no session) **[untested: it may start
  SteamVR, so it only runs on the player's click]**.
- **Install steps**, each recorded and saved at once:
  1. The loader, if needed: rename, then ours.
  2. `ASI\LE1MEVR.asi`, `ASI\fonts\*`, `ASI\menu\*`.
  3. `ASI\MEVR-Setup.exe`: a copy of the setup, for uninstalling and "Install after I quit".
  4. Halcyon's `MELEVR.ini`: its release file only if missing (`ini::HalcyonReleaseIni`, as `deploy-asi.ps1`), then
     the per-slot fixes on whatever is there (`ini::FixHalcyonIni`: hotkeys off, `menuKey = 220`, head aim 1 to 0,
     follow 50 to 70, `sfr2Metric = 1` where missing). Every other value is the player's.
  5. `steam_appid.txt` for Steam installs.
  6. The VR game settings (`settings::ApplyGamerSettings`, as `vr-game-settings.ps1`): VSync, motion blur and film
     grain off, 16x anisotropy, borderless, not full screen, no dynamic shadows, SDR. The resolution is the headset's
     eye size when known, else Halcyon's Performance (3072 x 3188), or kept. Also `BIOEngine.ini`
     `SmoothFrameRate=FALSE`, and Halcyon's own copy of the target.
- **The manifest** (`install::Manifest`, JSON): every file `added`, `replaced` (backup), `renamed` or `edited` (the
  ini keys with their old and new values), the folders created, the packs, whether the game settings are on.
  - **Uninstall** goes newest first: it deletes what was added, restores backups, renames back, and reverts each ini
    key only if it still holds our value (the player's later changes stay), then removes our folders if empty.
  - `MELEVR.ini`, `LE1MEVR.ini` and the mod's logs are `userData`: kept if the player chooses "Keep my VR
    settings", deleted otherwise (section 7, item 3).
  - **Restore** (Settings page) reverts the game settings' keys only.
  - **Repair** compares SHA-256s with what was written and copies back from the payload.
- **Never the real game in tests**: unit tests on a folder in %TEMP%, and `tools/setup/test-fake-game.ps1` on
  `scratch\fakegame`. The fake game is a stand-in `MassEffect1.exe` with the real version number
  (`tools/setup/FakeGame.rc`) plus a copy of RAD's DLL as `bink2w64.dll`. The script runs install, a tamper (RAD's DLL
  back, a menu file deleted) and repair, restore, apply, an update over the top, and uninstall, then checks the folder
  is byte for byte what it was.

## 5. The update check

`updates::LatestReleaseApi` turns `https://github.com/<owner>/<repo>` into the Releases API URL (the placeholder
`OWNER/REPO` gives none: the check is off). The setup reads `tag_name` and `html_url` (only `github.com` pages) and
compares versions. A newer release only offers its page: nothing is downloaded. **[untested: no repository yet]**

## 6. Licensing for a release

- Ours: GPL-3.0. The loader, the setup and the install library are new code. MinHook (BSD-2) is in the loader; RmlUi
  (MIT) and FreeType (FTL) in the setup; the fonts (OFL) are in the payload with their licences; the OpenXR loader
  (Apache-2.0) is in the setup and the ASI.
- **No LEBinkProxy code or binaries.** Its source was read for behaviour only (launch arguments, the SPI exports'
  names, its DRM wait idea); the loader's DRM check and everything else is our own.
- **Mass Effect Modder** (GPL-3.0) and **texture packs** are never bundled.
- **BLOCKER: LExSDKv2 has no licence** (its README; CLAUDE.md). `LE1MEVR.asi` compiles it in, so distributing the
  ASI needs its author's permission. The user will ask; `docs/release-readme.md` says so.
- RAD's Bink DLL is never shipped: the setup renames the player's own copy.
- `tools/package-release.ps1` (QA fixes below) copies every licence text the binaries need into
  `dist\MEVR-<v>\licenses\`: MinHook (BSD-2), Dear ImGui (MIT), RmlUi (MIT, and its `Core/Containers/LICENSE.txt`
  for the bundled containers), FreeType (FTL), FSR 1 (MIT), the OpenXR loader (Apache-2.0) and the jsoncpp compiled
  into it (`src/external/jsoncpp/LICENSE`: public domain / MIT), our `LICENSES/MIT.txt`, the GPL text, Halcyon's
  licence, the fonts' OFL. The README gets the source pointer (`https://github.com/dnehring/MEVR`, tag `v<version>`,
  GPL-3.0). **Hard gate:** it refuses to package while `MEVR-LE1.vcxproj` links `LESDK-LE1` (or can't be read),
  unless `-LexSdkPermission` is given.

## 7. QA fixes (2026-10-26)

An adversarial QA pass on fake game folders found these; each is fixed, with unit tests in
`tests/InstallTests.cpp` (`TestIni`, `TestHalcyonIni`, `TestQaFixes`) and fake-game cases (`tools/setup/
test-fake-game.ps1`, plus a one-off script covering every item).

1. **Halcyon's upstream MELE-VR** (`dxgi.dll` with the string "MELEVR", as its `MELE-VR.bat` tests with `findstr`,
   plus `openxr_loader.dll`, in `Binaries\Win64`) wasn't detected: after our install two Halcyon renderers ran.
   `detect::FindUpstreamHalcyon` finds it; Install (the Install page says so: installing is the consent) renames both
   DLLs to `*.mevr-off` as recorded `renamed` entries that Uninstall puts back; its `MELEVR.ini` stays. `--keep-upstream`
   (`InstallOptions::moveUpstreamAside = false`) refuses instead. Repair moves them aside again if they came back.
2. **A loader installed after ours** (ME3Tweaks Mod Manager's, say) was clobbered by Repair and removed by Uninstall.
   Now `bink2w64.dll` is touched only when it's still ours (the recorded hash, or our product name), RAD's (Steam's
   verify) or missing; otherwise Uninstall forgets both bink entries and leaves `bink2w64.dll` and
   `bink2w64_original.dll` as they are, and Repair leaves it, each with a log line.
3. **A full uninstall left `ASI\LE1MEVR.ini`, the logs and the ASI folder.** The mod's run-time files
   (`ASI\LE1MEVR.ini`, `Logs\LE1MEVR.log`, `LE1MEVR.prev.log`, `MEVRLoader.log`, and `ASI\MELEVR.ini` when it has no
   record) are recorded at install as `added` user data (`Transaction::RecordRuntimeFile`, nothing written), so a full
   uninstall deletes them and "Keep my VR settings" keeps them. The `Logs` folder (missing, or holding only those
   files) and an ASI folder left by a "keep" uninstall with only our files are recorded as ours (removed once empty).
   The Uninstall page has a box, off by default, that also deletes `%LOCALAPPDATA%\MEVR` (snapshots, recordings,
   frame-timing captures, crash reports, the downloaded MEM and pack work files); CLI `--remove-local-data`.
4. **A lost manifest, then a reinstall, left our loader for good** (it was backed up as "replaced" by itself). In the
   "Ours" state without a record, Install adopts it: `bink2w64_original.dll` as renamed from `bink2w64.dll`
   (`AdoptRenamed`) and `bink2w64.dll` as added (`AdoptAdded`), so Uninstall puts RAD's DLL back. The files our
   install puts in the ASI folder (the ASI, fonts, menu, the setup's copy) and their folders are adopted the same way
   (else each was "replaced" by itself and came back). What the lost record knew can't come back: the game settings'
   old values, and `steam_appid.txt` (left: Mod Manager writes the same file).
5. **"Installed" when only the game settings or a pack were applied.** `Status::installed` now means the manifest has
   `modVersion` or the ASI entry (`hasRecord` says a manifest exists). Repair refuses without it.
6. **Tab + Enter pressed disabled buttons; `Act("repair")` dereferenced an empty payload.** `.disabled` in
   `main.rcss` now has `focus: none; tab-index: none`, and `App::Act` checks every condition the document disables a
   button on (`busy`, `waiting`, the payload, installed, the game, MEM, the dialog being open) before doing anything.
7. **A read-only `GamerSettings.ini` made Install FAIL** ("is the game running?"). It's detected
   (`detect::ReadOnlyFile`): Install goes on and logs a warning (the game settings aren't marked applied), Settings >
   Apply fails with the reason, the Settings page shows it, and write failures name a read-only file as such.
8. **A duplicate ini key lost the player's second value** (Set changed every line but recorded the first's value;
   Revert wrote it into all). Each line is now its own `ini::Edit` (`occurrence`, saved in the manifest) and is reverted
   alone.
9. **Uninstalling from the copy in the ASI folder** deleted itself even when the uninstall failed, `rmdir`ed an ASI
   folder we hadn't created, and passed the path through `cmd /c` where `%` expands. Now: on failure the copy is
   renamed back (for another try); `rmdir` only when the manifest's `dirs` has the ASI folder; the paths reach cmd in
   environment variables (`%MEVR_OLD_SETUP%`, expanded once, never inside the value), with `/d`.
10. **MEM could run while the game was still exiting** ("Install after I quit"). `packs::InstallWithMem` waits up to
    60 s for `MassEffect1.exe` and the launcher to go (`WaitForGameExit`), refuses otherwise, and checks again before
    `--install-mods`.
11. **The Repair page showed the status from when the setup started.** It's recomputed on every refresh and whenever
    the page is shown (`App::UpdateRepairStatus`, from `Tick`).
12. **The setup loaded DLLs from its own folder** (a Downloads folder: DLL planting). `SetDefaultDllDirectories(
    LOAD_LIBRARY_SEARCH_SYSTEM32)` first thing in `wWinMain`, and `/DEPENDENTLOADFLAG:0x800` on the link for the static
    imports. (The OpenXR loader's runtime DLL is loaded by full path, with its own folder on a retry.)
13. **Head aim could stay on in a sparse `MELEVR.ini` slot** (a missing key loads Halcyon's code default, on).
    `FixHalcyonIni` adds `combatHeadAim = 0`, `makoHeadAim = 0` and `menuKey = 220` where missing, as `sfr2Metric`.
14. **The loader could hang the game's exit** when the process ended during the 60 s DRM wait: process detach took
    `gLogMutex` / `gPluginsMutex`, which a killed thread may hold forever. Detach now only try-locks (and skips what it
    can't take); plugins' `SpiOnAttach` / `SpiOnDetach` run under SEH (a faulting plugin is logged and left
    unattached); the plugin list is filled under its lock.
15. **Packaging**: the licence texts, the source pointer and the LExSDKv2 gate (section 6).
16. **Found while re-testing item 9: a retried uninstall could delete RAD's DLL.** An uninstall that failed half way
    (a locked file) saved the whole record again, so the retry removed `bink2w64.dll` a second time: by then RAD's
    own DLL, renamed back by the first try, leaving the game without one. `Transaction::Uninstall` now keeps on
    record only what wasn't undone, and Uninstall leaves a `bink2w64.dll` that is RAD's with nothing behind it.

## Open

- **[verify]** In the game: our loader in place of LEBinkProxy (attach timing, Halcyon's DXGI hook, the log);
  Launch on the Steam build (and whether `link2ea` is needed); the EA app build; "Install after I quit" end to end.
- **[untested]** Everything that touches the network or MEM: the update check, MEM's download and unpacking, a pack
  install.
- The ME2/ME3 variants (the layout is `Game\ME<n>`; the classes take the game as a parameter only where needed).
