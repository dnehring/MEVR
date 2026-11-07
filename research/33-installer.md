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
   `tools/setup/test-fake-game.ps1` runs against a fake game folder, and `--check-update`, `--get-mem`,
   `--install-pack`, which `tools/setup/test-network.py` runs against a local server (section 10).
2. **Our own ASI loader** (`src/loader`, `bink2w64.dll`, GPL-3.0, new code): forwards all 79 Bink exports to
   `bink2w64_original.dll`, loads `ASI\*.asi`, and offers LExSDKv2-style plugins (ours) the SPI interface they expect.
   If ME3Tweaks' LEBinkProxy (or another proxy) is already there, the setup uses it instead.
3. **A manifest** (`ASI\MEVR-install.json`) records every file added, replaced (with a backup in `ASI\MEVR-backup`),
   renamed or edited (key by key), so Uninstall restores the game exactly, and never resets a value the player changed
   since.
4. **Launch** starts `MassEffect1.exe` directly with the arguments the Legendary Edition launcher would pass, past its
   game picker; SteamVR first when it's the active runtime (section 9). **Start the game once** makes a never-started
   game write its `GamerSettings.ini`, then applies the VR game settings (section 9).
5. **Optional content**: a catalogue of third-party texture packs. Their Nexus terms forbid re-uploads, and Nexus only
   gives direct download links to premium members' registered apps, so each entry opens the pack's own page; the
   player downloads it there and picks the file; the setup then installs it with Mass Effect Modder's command line
   (GPL-3.0, fetched from its GitHub releases after the player confirms name, size and time). Tested offline against a
   local server and a stand-in MEM (section 10); the real MEM and GitHub are **[verify]**.
6. **Update check** against GitHub Releases (`MEVR-Setup.ini [Update] repo`, default
   `https://github.com/dnehring/MEVR`, the public repository since 2026-10-23; `https://github.com/OWNER/REPO` turns the
   check off). Tested offline against every answer GitHub gives (section 10).
7. **In game**: Display > Optional content shows the installed packs and "Install after I quit", which writes
   `%LOCALAPPDATA%\MEVR\setup.request`; the host's ExitProcess hook then starts `ASI\MEVR-Setup.exe --request
   --wait-pid <pid>`.
8. **Packaging**: `tools/package-release.ps1` builds `build-mono` and assembles `dist\MEVR-<version>` (and a zip).
9. **Licensing**: LExSDKv2 has no licence; since 2026-10-26 the release ASI doesn't contain it (`MEVR_LE1_LESDK=OFF`,
   research/34 stage 6 for LE1), so the release needs no permission (below).

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
| Ours | "MEVR" in ProductName (`src/loader/Loader.rc`: "MEVR" since 2026-11-05, one product name for code signing; "MEVR ASI loader" before) | updates it |
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
  downloads it on the player's OK into `%LOCALAPPDATA%\MEVR\tools`. It's unpacked with Windows' `tar.exe`, then 7-Zip if
  that fails (section 10: Windows 11's tar.exe reads 7z's LZMA2 and BCJ2; Windows 10's is **[inference]** built without
  liblzma).
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

### The flow built (`install/Packs`, the Optional content page; tested with a stand-in MEM, section 10)

1. "Open its page": the browser on the pack's Nexus page.
2. The setup scans Downloads for a matching file, or the player picks it.
3. "Install" shows a confirmation: permanent, after content mods, size, close the game.
4. MEM `--unpack-archive` into `%LOCALAPPDATA%\MEVR\packs\<id>` (emptied first; or the .mem file as it is), then a
   `.mfl`, `--set-game-data-path`, and `--install-mods --gameid 1 --input <mfl> --verify --ipc` with live progress
   (`install::InstallPack`, shared by the window and `--install-pack`).
5. The pack is recorded in the manifest, and the VR game settings this install wrote are set again where MEM wrote over
   them (only those still in place before MEM ran: `Transaction::ReassertIni`).

Uninstalling the VR mod leaves textures in place, and says so (the uninstall's log names the packs).

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
     follow 50 to 70, `firstRunDone = 1`, `sfr2Metric = 1` where missing; a slot MEVR never wrote also gets a fresh
     install's mode, first person, mirror rate and depth-pop key: section 9). Every other value is the player's.
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
compares versions. A newer release only offers its page: nothing is downloaded. `updates::Check` / `Interpret` (since
2026-11-04) also tell apart no release yet (404), GitHub's API quota (403 with `X-RateLimit-Remaining: 0`, or 429), an
answer that isn't a release, and a pre-release or draft (never offered). Tested against a local server (section 10);
against GitHub itself **[verify]** (no release is published yet).

## 6. Licensing for a release

- Ours: GPL-3.0. The loader, the setup and the install library are new code. MinHook (BSD-2) is in the loader; RmlUi
  (MIT) and FreeType (FTL) in the setup; the fonts (OFL) are in the payload with their licences; the OpenXR loader
  (Apache-2.0) is in the setup and the ASI.
- **No LEBinkProxy code or binaries.** Its source was read for behaviour only (launch arguments, the SPI exports'
  names, its DRM wait idea); the loader's DRM check and everything else is our own.
- **Mass Effect Modder** (GPL-3.0) and **texture packs** are never bundled.
- **LExSDKv2 has no licence** (its README; CLAUDE.md). Resolved 2026-10-26: the release build (`build-mono`) sets
  `MEVR_LE1_LESDK=OFF`, so `LE1MEVR.asi` contains none of it (checked: none of its strings in the binary; the
  project doesn't link `LESDK-LE1`, so the packaging gate below passes without `-LexSdkPermission`).
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

## 8. Report a problem (2026-10-26)

Players are expected to have an AI assistant at hand, so a bug report should be one button, and what it makes should
read well for that assistant and for us. **Nothing is uploaded or sent automatically**: the player gets a zip on the
Desktop, looks at it, and decides.

**What the zip holds** (`src/install/Report`, its own deflate writer `src/install/Zip`, no zlib): `REPORT.md` first;
`logs/` the mod's `LE1MEVR.log` and `.prev.log` (over 4 / 3 MB: the first 1 MB and the last 3 / 2 MB), Halcyon's
`MELEVR_Log.txt` trimmed to its last session (it never resets: 191 MB on the dev machine; from the last
"(Halcyon) inside" start banner, else the last `[XR] starting OpenXR session thread`, read from the file's last 48 MB,
then at most 0.5 MB + 2.5 MB), the loader's log (`MEVRLoader.log`, or LEBinkProxy's `bink2w64_proxy.log`), the crash
breadcrumbs (and `.prev`), `reflect-check.json`; `settings/` `LE1MEVR.ini`, `MELEVR.ini`, `MEVR-install.json`;
`system/folders.txt` (Win64's top level and ASI's tree: names, sizes and dates only, for conflicts; no other mod's
contents); optional `snapshot/` (the newest JSON and its eye PNGs) and `perf/` (the newest `-probe.json`,
`-frames.csv`, `-ticks.csv`). A file the running game holds without read sharing (our loader's log: `fopen` in "w")
is listed as locked, not included.

**Redaction** before anything is written: every text file and REPORT.md. The profile folder (as written, with forward
slashes, JSON-escaped): under `...\Users\` its last part becomes `<user>`, elsewhere all of it `<profile>`; the
account name and the profile folder's name (a Microsoft account's folder is a truncated name) as whole words
(3+ characters; a short name would eat the log); `%COMPUTERNAME%` and the DNS host name as `<computer>`; e-mail
addresses (`local@domain.tld`, the TLD letters only) as `<email>`. The count per kind is shown in the preview. On
the dev machine's real logs (with the snapshot and frame timing): 10 replacements, all the profile path; a search of
every text entry finds neither the user nor the computer name.

**REPORT.md** (under 30 KB; the caps halve until it fits): one paragraph on MEVR with the repository, the
troubleshooting guide and the issue tracker; the system (MEVR as the log's attach line says, with the commit now baked
in by `tools/cmake/BuildInfo.cmake`; the install record's version; MassEffect1.exe's version and the store; Windows
from the registry, `ProductName` corrected to 11 at build 22000+; the first hardware DXGI adapter and its UMD driver
version, with NVIDIA's own number; the active OpenXR runtime; from the logs the headset's system name, the
interaction profile, the colour profile, the eye and render sizes); what went wrong (the last session's `[error]` and
`[warn]` lines merged where they differ only in numbers, a module's `+0x` offset kept, counted, errors first; fewer
for the session before and Halcyon's error/failed/exception lines); the crash (exception, faulting module+offset,
the top native frames, the script frames; labelled "an earlier crash" when the breadcrumbs are older than the
previous session's log, and then not the headline); the settings off their defaults (LE1MEVR.ini's player keys
against the code's defaults, MELEVR.ini's active profile against the release file the setup installs); the files;
and the prompt for the assistant. The parsing and assembly are pure and unit-tested (`tests/ReportTests.cpp`, with an
inflater for our deflate stream); the fake-game script checks the zip with .NET and Expand-Archive and that nothing
personal is left.

**Where it starts:**
- MEVR Setup's **Report a problem** page (rail, Welcome and Repair rows): the preview (files, sizes, notes, the
  redaction count, the headline) gathered on the worker, the snapshot and frame-timing checkboxes re-gather it, then
  Create writes the zip to the Desktop (else Documents, else `%LOCALAPPDATA%\MEVR\reports`) and opens Explorer with
  it selected (`SHOpenFolderAndSelectItems`). **Open a GitHub issue** opens
  `issues/new?template=bug.yml&title=...&version=...&summary=...`: the non-personal summary only (versions, Windows,
  GPU, runtime, headset, controllers, the headline), cut to stay under 7 KB; the player attaches the zip.
- The command line: `--report [--snapshot] [--perf] [--report-dir <folder>] [--local <folder>]` (tests use the last
  two).
- **In game:** Advanced > Diagnostics > Report (with "latest snapshot" and "latest frame timing" toggles).
  `plugin/ReportRequest` (host) writes `report.request` and starts `ASI\MEVR-Setup.exe --report --from-game --game
  <Win64>` **right away**, not at exit like Optional content: the report should hold the session as it is now, the
  player may never quit cleanly (a hang), and a separate process at `PROCESS_MODE_BACKGROUND_BEGIN` (CPU and disk)
  reading a few MB costs the game nothing; it has no window, so the game keeps the focus. The file write and
  `CreateProcess` run on a short thread of their own, never in a frame. The setup writes `report.result`; the menu
  polls it at most once a second and says "Saved to the Desktop: <name>. Open it after the game". Without the setup
  in the ASI folder (a development deploy) the request waits and the setup opens on the page next time.
- **After a crash:** `crash-breadcrumbs.txt` newer than `report.last` (touched by every report) and
  `report.dismissed` (touched by No thanks) makes the setup's Welcome page and the in-game menu offer a report
  (`install/ReportMarks`, header-only so the host doesn't link the installer library).

**Impact:** the host gains `plugin/ReportRequest` (two small files, a few stats and one tiny JSON read while the
menu is open) and the commit in its attach line; the mod nothing; the setup the page and the CLI (`mevr-install` now links `dxgi` for the GPU line).

**The issue form** (`.github/ISSUE_TEMPLATE/bug.yml`, replacing `bug.md`): headset and runtime dropdowns with Other,
the version, what you did / expected / happened, a required "I attached the report zip" checkbox, the optional
summary. `config.yml` keeps blank issues (ideas and questions have no form yet) and links the troubleshooting guide.

## 9. First-run fixes (2026-11-04)

From docs/release-checklist.md section 4 "First minutes" and the release's installer review. All in `src/install` and
`tools/setup`; unit tests in `tests/InstallTests.cpp` (`TestUpstreamHalcyonIni`, `TestFirstStartAndRuntime`,
`TestFirstRunInstall`, additions to `TestHalcyonIni` and `TestQaFixes`); fake-game cases in `tools/setup/test-fake-game.ps1`.

1. **A game never started on this PC.** LE1 keeps `GamerSettings.ini` in the game folder (`Game\ME1\BioGame\Config`,
   `paths::kGamerSettings`) and makes it on its first start; the setup only edits it, key by key (it never writes one
   from scratch: whether the game accepts a partial file isn't known). Before, Install skipped the whole settings step
   with one log line, leaving a bordered window at the monitor's size (Halcyon's stereo breaks) and `MELEVR.ini` at the
   template's 4096 x 4250. Now:
   - Install records the chosen settings as `pendingSettings` in the manifest (target, VSync, blur, grain, HDR) and says
     plainly that the game hasn't been started and what to do; `Status::pendingSettings` exposes them.
   - **Start the game once** (Welcome hero, and a warning row on Install, Settings and Launch whenever the file is missing):
     starts the game (`launch::StartLe1`; or, if it already runs, only waits), polls every 0.5 s, and applies the settings
     (the install's pending ones, or the Settings page's when started there) once `GamerSettings.ini` exists **and** the
     game has exited (it rewrites its ini files when it quits). The decision per poll is pure:
     `launch::first_start::Next` (Starting, WaitingForFile, WaitingForQuit, Apply, Closed, TimedOut; 90 s to appear, 20 s
     gone counts as closed, 15 min for the file, 45 min for the quit). The setup shows the phase ("Quit the game now (main
     menu > Quit)") with a Stop waiting button.
   - **Repair** applies pending settings once the file exists (and says they still wait when it doesn't); the Welcome
     page offers "Apply them" when the file has appeared since. Settings > Apply and Restore clear them.
   - **[verify]** whether LE1 writes `GamerSettings.ini` at its first start (the setup's text since 2026-10-23 says so) or
     only when it quits: either way the flow applies after the exit. Whether `BIOEngine.ini` exists before the first
     start (the step skips it when missing, as before).
2. **Upgrading from Halcyon's upstream MELE-VR.** Upstream keeps its `MELEVR.ini` in `Binaries\Win64` (its dxgi.dll's
   folder; `vr_config.cpp IniPath`), ours reads `ASI\MELEVR.ini` (next to `LE1MEVR.asi`), and Install never touches
   upstream's file: so a standard upgrade gets a fresh `ASI\MELEVR.ini`. The case that reached players is an upstream (or
   old `src/halcyon/le1/runtime/MELEVR.ini`) file **copied into ASI**: it kept `vrMode` 1/3, `fpEnabled 0`,
   `firstRunDone 0` (Halcyon's "INSERT opens this menu" welcome) and `mirrorPresentEvery 1`.
   - `FixHalcyonIni` now marks the welcome seen (`firstRunDone = 1`) in every slot, and a slot with **none of the keys
     only MEVR's Halcyon knows** (`sfr2Metric`, `sfr2MetricDepth`, `sfr2MetricEyeFrustum`, `worldLockedPanels`,
     `recenterTiltDeg`, `hdrPaperWhiteNits`: upstream's code has none, MEVR's in-game save writes all, and every MEVR
     install path writes `sfr2Metric`) gets a fresh install's `vrMode = 2`, `fpEnabled = 1`, `mirrorPresentEvery = 2`,
     `depthPopKeyEnabled = 0`. In a slot MEVR wrote those four are the player's (our menu sets mode, first person and
     mirror) and only added where missing (a missing key loads Halcyon's code default: third person, every frame, P on).
     Every other value stays; a second pass changes nothing.
   - **Recorded**: edits on a `MELEVR.ini` that was there before the install (a userData entry with no hash: we never
     wrote it) are kept on its manifest entry, and an uninstall that keeps the VR settings puts each one back that still
     holds our value (a full uninstall deletes the file, as before). A slot the player saved in game stays recognised
     after that (its other MEVR keys), so a reinstall keeps their menu choices; a slot never saved is normalised again.
     A file we wrote ourselves (fresh, or rewritten by Repair after the player deleted it) records nothing, as before.
   - Limit: a slot an *older* MEVR setup already gave `sfr2Metric = 1` (without these four) counts as MEVR's; only
     testers had such setups (no release yet).
3. **Steam's "verify files".** Repair already restored the loader (RAD's DLL renamed behind ours again) and the cockpit's
   `PCConsoleTOC.bin` entry (`InstallCockpit` on every Repair). New: `Status::needsRepair` lists what Repair would put
   back (a loader another tool installed since isn't one), the Welcome hero says "The mod needs a repair" with the files
   (`bink2w64.dll (the ASI loader)`, `PCConsoleTOC.bin (the cockpit's entry)`) and a Repair button, the Launch page warns,
   and `--status` prints `needs repair:`. **Found while testing it:** after a lost install record and a reinstall, the
   uninstall left the cockpit's script, package and TOC entry (the package and script were "replaced" by themselves, the
   entry's size "kept"); `AdoptOurFiles` now adopts them (`Transaction::AdoptTocEntry`: our package's name is in no game
   TOC). That case only runs with a cockpit in the payload, which `test-fake-game.ps1` now always has (a stand-in
   package when none is built).
4. **The menu key's name.** The Welcome and Launch pages name it from the keyboard layout (`GetKeyNameTextW` of
   `MapVirtualKeyW(220)`, as the in-game menu's `KeyName`): "the \ key" on a US layout. **[untested]** on another layout
   (changing the PC's layouts to test it was out of bounds).
5. **The VR runtime before the game.** Halcyon looks for the headset once, at the game's start. `launch::runtime_start`
   (pure, unit-tested): for SteamVR, Launch starts it first (`SteamVR\bin\win64\vrstartup.exe` next to its OpenXR
   manifest, else `steam://rungameid/250820`) and polls for `vrserver.exe` and `vrcompositor.exe` every 0.25 s, at most
   60 s (Stop waiting cuts it short), then starts the game either way, with a note when SteamVR wasn't up. Other runtimes
   get one sentence on the Launch page and the Welcome hero (the Meta Quest Link app and Link / Air Link, Virtual Desktop's
   Streamer, Mixed Reality Portal, Pimax Play, Varjo Base). **[untested]**: starting SteamVR and the wait (never started
   here), and whether `vrcompositor.exe` without a headset connected ever appears (then the 60 s run out and the game starts).
6. **The running-game check is per folder** (`detect::GameRunningIn`): a `MassEffect1.exe` / `MassEffectLauncher.exe`
   whose image is under the root, or the same file by another path, or one whose path can't be read. Another copy of the
   game (or a fake game folder while a real game runs) no longer blocks Install, Repair and the rest; the unit tests and
   the fake-game test ran with the real game open in another session.

**Verified 2026-11-04** (worktree build, the real game open elsewhere): `mevr-tests` all passed (ctest 6/6);
`tools/setup/test-fake-game.ps1` 76 checks passed, including install on a never-started game (settings wait, no
GamerSettings.ini made up, MELEVR.ini's target waits), Repair before and after the game's first start (applied, then the
game's own file back on uninstall, the folder byte for byte), an upstream `MELEVR.ini` in ASI (both slots as a fresh
install, tuning kept, "keep my settings" puts it back byte for byte), and "verify files" (status lists the loader, a menu
file and the TOC; Repair puts all back; status then "nothing"); `MEVR-Setup.exe --preview` pages
`welcome-firststart*`, `install-firststart*`, `settings-firststart`, `launch-firststart`, `welcome-pending`,
`welcome-repair`, `repair-needed`, `launch-repair`, `launch-meta` looked at. **Not run:** the real game, Steam, SteamVR.

## 10. The web and MEM paths, tested offline (2026-11-04)

docs/release-checklist.md section 5 "Installer paths never run": the update check (`install/Updates`), the downloads
(`install/Http`), MEM's download and unpacking and a pack's install (`install/Packs`), and the setup's side of "Install
after I quit" now run in ctest **`mevr-setup-network`** (`tools/setup/test-network.py`, about 45 s; `--quick` skips the
30 s timeout case, `--keep` keeps its scratch folder). Nothing reaches the internet, the real MEM, the game or the
player's `%LOCALAPPDATA%\MEVR`.

**How.**
- **`MEVR-Setup.exe --test-server http://127.0.0.1:<port>`** (tests only; `http::SetTestServer`): every request goes to
  `<server>/<host>/<path>` (`https://api.github.com/repos/x` to `http://127.0.0.1:<port>/api.github.com/repos/x`), so
  one local server answers for GitHub, its download host and the redirect between them. Plain http only to 127.0.0.1,
  without a proxy; anything but `http://127.0.0.1[:port]` or an `https://` URL is refused (exit 2). Never on by default:
  without it the setup speaks only HTTPS to the URL it's given (unit test: an `http://` URL is refused before anything is
  sent).
- **New command line**, running the same functions as the window's buttons: `--check-update [--repo <url>]`
  (`updates::Check`), `--get-mem` (`packs::FindMemRelease`, `DownloadMem`), `--install-pack <id> --file <download>`
  (`install::InstallPack`), and `--preview <folder> --request [--wait-pid N]`: the window's real `Start` and `Tick` for
  "Install after I quit", its page as PNGs and its state as text (`App::StateText`).
- **A stand-in MEM** (`tools/setup/FakeMem.cpp`, target `mevr-fake-mem`, built at test time as
  `MassEffectModderNoGui.exe`, never committed or shipped): logs every call's arguments; `--unpack-archive` writes two
  .mem files (one in a subfolder); `--set-game-data-path` checks the folder is a game root; `--install-mods` checks each
  `.mfl` line is a .mem file, prints MEM's `[IPC]` stages and progress, appends MEM's end-of-file marker to
  `SFXTest.pcc` and turns MotionBlur back on in `GamerSettings.ini` (MEM writes graphics settings);
  `MEVR_FAKE_MEM_FAIL=<command>` makes one call fail with an `[IPC]ERROR`.
- **The .7z** holding the stand-in is written by the test itself (a minimal 7z writer over Python's `lzma`: LZMA2, one
  solid folder, CRCs), so CI needs no archiver; with 7-Zip installed the test also makes 7-Zip's own archives.
- **The fake game** has stand-ins for "another proxy" loader (no RAD DLL needed, so it runs in CI); every run's
  `LOCALAPPDATA` is a scratch folder under %TEMP%, checked before each run.

**What ran, and what it showed** (111 checks on this PC, all passing after the fixes below; ctest 7/7):

| Path | Cases | Result |
|------|-------|--------|
| Update check | 404; an older, the same and a newer release; a newer pre-release and draft; malformed JSON; a JSON array; `html_url` off github.com; 403 with `X-RateLimit-Remaining: 0`; 429; a 403 that isn't the quota; 500; a 5 MB body; the placeholder repository; another `--repo`; a refused connection; a server that never answers | one GET of `/repos/<o>/<r>/releases/latest` with our User-Agent and GitHub's Accept; "No release is published on GitHub yet.", "Version vX is out." with its page only for a newer full release; the timeout after 30-32 s ("no answer in time"); the placeholder asks nothing |
| `--test-server` | localhost, 127.0.0.2, a host name, ftp, a path, file: | all refused |
| MEM download | the release lookup then the asset through a 302 like GitHub's; the Windows CLI asset picked over the Linux and GUI builds; the listed size and SHA-256 (`digest`); downloading again over the first; a body cut short; no Content-Length (as listed, short, overlong); a Content-Length other than listed; another SHA-256; no digest; a 404; no Windows build; a URL off github.com; a file name with `..\`; malformed JSON; the API quota | each refusal names why and leaves no file (no `.part`, no archive); the exe in a subfolder found; an archive without it said so |
| Unpacking | our LZMA2 .7z; 7-Zip's `-mx=9` archive of an exe (BCJ2 + LZMA2); a Swap4-filtered one; a broken file | Windows 11's tar.exe (bsdtar 3.8.9, liblzma 5.8.3) unpacks the first two and fails on Swap4 ("Unexpected codec ID"), then 7-Zip takes over; the broken one names both tools' errors |
| Pack install | an archive (unpacked by MEM), a .mem as it is (from the game's own setup copy), a stale work folder, a game path ending in `\`, MEM failing in each of its three steps, an unknown pack, a missing file | MEM's three command lines exactly as section 3; the `.mfl` sorted, without the stale version's file; MEM's marker; the manifest's `packs` entry (id, name, `mem`, time); MotionBlur (MEM's) set back, FilmGrain (the player's since) kept, the resolution as installed; a failed install not recorded |
| Status, uninstall | `--status`; `--uninstall --remove-local-data` | the packs, the marker and MEM listed; "texture packs stay (ALOT for ME1, LEUITM ...)"; the game folder byte for byte as before but for `SFXTest.pcc` (the textures stay); the scratch `%LOCALAPPDATA%\MEVR` (MEM, the pack work files) gone |
| "Install after I quit" | the request as `plugin/SetupRequest` writes it (`page`, `packs: []`, `pid`), started as the game starts it (`ASI\MEVR-Setup.exe --request --wait-pid <pid>`, no `--game`); a request naming packs; a broken one | opens on Optional content, "Choose a pack below.", finds its own game from the ASI folder, waits ("Waiting for Mass Effect to close", Launch and installs disabled) until the process exits, then goes on; the request file is removed every time; named packs shown, unknown ids dropped |

**Bugs found and fixed** (each covered by the test or `tests/InstallTests.cpp` `TestNetworkPaths`):
1. **A pack install refused while any MassEffect1.exe ran**, another copy of the game included (`WaitForGameExit` and
   the check before `--install-mods` used `GameRunning`): per game folder now (`GameRunningIn`, as section 9 item 6).
2. **A stale work folder went along**: `packs\<id>` kept an earlier unpack's .mem files (another version of the pack),
   and the recursive scan put them in MEM's list. Emptied before unpacking (a .mem picked from inside it is kept).
3. **MEM's command lines were quoted by hand**: a game folder ending in `\` (the EA app's `Install Dir` value can)
   swallowed the closing quote, so MEM got a wrong `--path`. `packs::QuoteArg` follows CommandLineToArgvW's rules.
4. **After MEM the VR game settings were applied from the Settings page's current choices**: the resolution could
   change (to Performance when the setup started without Halcyon's log) and toggles the player had kept went off. Now
   `Transaction::ReassertIni` sets back only our recorded values that held before MEM ran and changed since.
5. **The update check's messages**: a 404 (no release yet) read "Couldn't check (HTTP 404)", malformed JSON "Couldn't
   check (HTTP 200)"; GitHub's API quota wasn't recognised; WinHTTP errors were bare numbers (`http::ErrorText`: "couldn't
   connect", "no answer in time", ...). A draft or pre-release is never offered.
6. **Downloads trusted what arrived**: the release's listed size and SHA-256 (GitHub's `digest`, `sha256:<hex>`) are now
   checked, more bytes than listed stop the download, a failed write (a full disk) is an error, and every failure removes
   the `.part`. MEM's asset name must be a plain file name (it becomes a path in the tools folder).
7. **MEM's .7z unpacked only with tar.exe**: Windows 10's has no LZMA **[inference: its bsdtar 3.5.2 is built with zlib
   only]**, so the download failed there with "tar exit code 1". `packs::Unpackers`: tar.exe, then 7-Zip (registry or
   Program Files); the message says to install 7-Zip, or where to put the exe by hand.
8. **The uninstall didn't say the textures stay** (only the window's page did): its log names the packs.
9. **Cancel during MEM** read as "MEM failed (exit code 1)": it says cancelled, and during `--install-mods` that the
   game's files may be half changed.

**Still untested** (no internet, no MEM, no game here):
- **GitHub itself [verify]**: the live JSON of MEVR's and MEM's releases, the `digest` field's format (from GitHub's 2025
  API change, not checked here), the real redirect host, HTTPS (certificate, proxy): the test server speaks plain http.
- **MEM itself [verify]**: its argument names (`--unpack-archive --input/--output`, `--set-game-data-path --path` with
  the LE root), the `.mfl`'s encoding (UTF-8 written), its real `[IPC]` lines and exit codes, how its release .7z is
  compressed.
- **The window's clicks**: the buttons run the functions tested here, but the confirmations and progress glue weren't
  clicked. There's no Stop button for the MEM download or a pack install (closing the window cancels between reads, or
  after the 30 s receive timeout on a stalled server); cancel isn't driven by a test.
- **The game's side of "Install after I quit"**: `plugin/SetupRequest`'s write and the ExitProcess hook's start; the
  test writes the same JSON and runs the same command line from the ASI copy.
- Windows 10's and Server 2022's tar.exe (the CI runner shows which branch it takes); headset Detect (it may start
  SteamVR); `tools/setup/test-fake-game.ps1` wasn't re-run (it reads RAD's DLL from the real game install).

## 11. The release zip on the real game: install, verify, Repair, uninstall (2026-11-05)

The user: "we want something like a 1 click installer from the launcher app we built". Pass 1 of the release validation
(docs/release-checklist.md section 1, "Must run once before tagging"): the release zip on this PC's real Steam game, from
the state a new player has, without a person in the headset.

- **One click.** The Welcome page's button said Install but only opened the Install page; it now installs at once with
  the Install page's defaults (the mod, our loader, the recommended VR game settings), Options beside it opens the Install
  page first, and after the install the same button is Launch (or Start the game once). `--launch` on the command line
  runs the Launch button's own start (`install::launch::StartLe1WithVr`, shared by both: SteamVR first when it's the
  runtime, then the game): for a desktop shortcut, and for tests.
- **The vanilla game.** The dev install recorded (a hash list of every file of `Game\ME1` and `Game\Launcher`, the
  setup-touched ones hashed, the player's documents and saves), its files moved aside, then Steam's "verify files": it
  restored `Game\ME1\Binaries\Win64\bink2w64.dll` and `Game\Launcher\bink2w64.dll` (**LEBinkProxy also sits in the
  launcher's folder**, byte for byte the same proxy) and the TOC; 4 files in all. Steam leaves extra files (LEBinkProxy's
  `bink2w64_original.dll` in the launcher folder) in place.
- **The zip** (`tools/package-release.ps1 -Zip -AllowDirty`, 39 MB; `-AllowDirty` only because a worktree's submodules are
  links whose metadata git can't read; the binaries say `0.1.0+7050bc75ed`), unzipped to a fresh folder and run from there.
  Setup's status saw the vanilla game right ("No ASI loader yet: ours will be installed", SteamVR the runtime).
- **Install** (`--install`, the same `install::Install` the button calls with the same defaults): **1.1 s**; LE1's own
  `bink2w64.dll` renamed `bink2w64_original.dll` and ours put in, the ASI, fonts, menu, the cockpit package and its TOC
  entry, `steam_appid.txt`, two game-setting values; status "installed: yes, 0.1.0, loader mevr", nothing to repair.
- **Steam's "verify files" after the install** (what a game update does): 2 of 36,562 files replaced (our loader by
  LE1's `bink2w64.dll`, the TOC without the cockpit's entry), 233 s for all three games. Setup's status named both under
  "needs repair"; **Repair** put them back, and the game then matched the first install file for file.
- **Uninstall**, twice (after the first install, and after the verify and Repair): the game matched the vanilla record
  byte for byte, except `GamerSettings.ini` and the TOC (same content, newer dates) and two LE1MEVR logs that were the dev
  build's (Setup records the mod's logs as its own user data and a full uninstall deletes them; a player only has the
  mod's own there). The empty ASI folder goes too.
- **Launch on the Steam build fails.** Setup's Launch started SteamVR (closed beforehand), waited until it was up, then
  `MassEffect1.exe` directly with the launcher's arguments (`StartLe1`): the exe hands its start back to Steam, as
  `steam://run/1328670// -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles 20 -OVERRIDELANGUAGE=INT`
  (Steam's console_log: `ExecuteSteamURL`), and **Steam then waits for the player to confirm the arguments**
  (`GameAction ... LaunchApp waiting for user response to ShowGameArgs`): its "launch with these arguments?" dialog. The
  EA app was asked too (its log: the game's exe path, then `isGameRunning: false`). The game never reached our loader (no
  `MEVRLoader.log`). Every later launch of the game through Steam (`steam://rungameid`, `-applaunch`) queued behind that
  dialog. Answer to section 2's [verify]: **a direct start of the Steam build doesn't work**; ME3Tweaks' autoboot works
  only because LEBinkProxy sits in the launcher.
- **The way to one click** (built the same day: section 12): Launch asks Steam with `-applaunch 1328670 -game 1 -autoterminate` (no arguments
  dialog: the dev setup's `tools/launch-game.ps1 -Steam` has launched that way for weeks), and our own loader also goes into
  `Game\Launcher` as its `bink2w64.dll` and, loaded into `MassEffectLauncher.exe` with `-game 1`, starts
  `..\ME1\Binaries\Win64\MassEffect1.exe` with the launcher's arguments and ends the launcher with `-autoterminate` (the
  behaviour of LEBinkProxy's launcher module, section 2: read for behaviour only, never copied). Setup records it,
  Uninstall removes it, Repair restores it after a verify; a desktop shortcut can run `MEVR-Setup.exe --launch`.
- **Not run yet:** our loader loading the ASI in the real game (every launch was blocked), Steam's Play with the
  launcher's picker, "Start the game once", the window's own clicks (desktop control of File Explorer and Steam declined:
  the command line runs the same code), the EA app build. The dev install was put back afterwards and compared against its
  record: the same, file for file (the two logs aside), the saves unchanged.

## 12. One click into Mass Effect 1: the launcher's autoboot (2026-11-05)

Built from section 11's finding (the user: "Yes, please proceed").

- **Our loader in the launcher.** The installer puts `bink2w64.dll` (the same file) in `Game\Launcher` too: the
  launcher's own Bink renamed `bink2w64_original.dll` and recorded, like the game's (`paths::kLauncherBink`,
  `InstallLauncherLoader`). Loaded into `MassEffectLauncher.exe` (the launcher's exe is DRM-packed, its only static import
  EA's `Core\Activation64.dll`, so Bink is loaded at run time, as LEBinkProxy there shows), it loads no ASIs; with `-game 1`
  on the launcher's command line it starts `..\ME1\Binaries\Win64\MassEffect1.exe` from that folder with the launcher's
  own arguments (`LauncherConfig.cfg`: `install::launch::Le1Arguments`, now in `src/install/LaunchArgs` so the loader links
  it without the installer), as the launcher's child (it keeps what Steam and the EA app gave the launcher); with
  `-autoterminate` the launcher then ends (`TerminateProcess`: ExitProcess from a worker thread would run every DLL's
  detach while the launcher's threads still run). Without `-game` the launcher is left alone; `-game 2` or `3` aren't ours
  (their arguments differ): the picker. The behaviour of LEBinkProxy's launcher module (section 2), written anew. Its log
  is `Game\Launcher\Logs\MEVRLoader.log`, recorded as the mod's own (and its folder, when the install makes it).
- **The launcher's Bink is another build.** It is RAD's 2.7s (391,360 bytes; LE2's and LE3's `bink2w64.dll` are the same
  size and build, 76 exports), not ME1's (79): it has `BinkOpenMiles`, which ME1's lacks, and lacks `BinkCurrentSubtitle`,
  `BinkGetSubtitleByIndex`, `BinkLoadSubtitles` and `BinkOpenXAudio29` (dumpbin `/exports`). The forwarders now export
  the union (80, `BinkOpenMiles` @80): a forwarder to a name the original lacks only fails if something asks for that name.
  (Section 1's note that the launcher holds the same file as ME1 was LEBinkProxy's `bink2w64_original.dll` there, a copy of
  ME1's.)
- **Launch through Steam.** `install::launch::StartLe1` on the Steam build starts nothing directly any more: it asks
  `steam.exe` (`HKCU\Software\Valve\Steam` `SteamExe`) for `-applaunch 1328670 -game 1 -autoterminate` when the launcher
  boots the game itself (`LauncherBoots`: our loader or LEBinkProxy in `Game\Launcher`), else for `-applaunch 1328670` (the
  picker, with a line saying so). SteamVR first as before (`StartLe1WithVr`). The EA app build still starts the exe
  directly **[verify]**. `--status` says what the launcher does (`launcher: our loader starts Mass Effect 1 for Launch`).
- **Repair and Uninstall.** Steam's "verify files" puts the launcher's own Bink back over ours (it did in section 11):
  status lists `Game/Launcher/bink2w64.dll` under "needs repair" and Repair puts ours back; an install from before this
  gets the launcher's loader at its next Repair. A loader another tool put there since stays (as in the game's folder);
  LEBinkProxy found there at install is left (it boots the game itself) and noted.
- **Checked offline:** `install` tests (the launcher's arguments as Steam passes them, `-game=N`, other games, missing
  numbers), `mevr-loader` (the stand-in Bink with `BinkOpenMiles`: all 80 names resolve), and the fake game
  (`tools/setup/test-fake-game.ps1`, with a `Game\Launcher` of a stand-in launcher and LE2's RAD Bink: install, status,
  "verify files" then Repair, uninstall byte for byte: every check passes).
- **In the game:** section 13 (Launch through Steam to gameplay, the same day).

## 13. Launch on the real game: one click into gameplay (2026-11-05)

The user cleared Steam's waiting dialog (section 11) and said to run it. The release zip of `be5144f` (`0.1.0+be5144ff21`,
packaged with `-SkipTests` after a ctest failure in the packaging run that didn't come back: all 7 suites passed on the same
`build-mono` right after; the failing test's name fell outside the captured output) on the Steam game made vanilla by
Steam's "verify files" (3 files back: the launcher's and the game's Bink, the TOC; identical to section 11's vanilla). The
dev deploy's logs folders (`Win64\Logs`, `Game\Launcher\Logs`, LEBinkProxy's) were moved aside too, so the install met the
folders a new player has.

- **Install** (`MEVR-Setup.exe --install`, 1.1 s): as section 11, plus `Game/Launcher/bink2w64.dll` renamed to
  `bink2w64_original.dll` with ours in its place and `Game/Launcher/Logs/MEVRLoader.log` recorded. `--status`: `launcher:
  our loader starts Mass Effect 1 for Launch`.
- **Launch** (`MEVR-Setup.exe --launch` from a scheduled task; the fake headset on, SteamVR stopped first so Setup had to
  start it): SteamVR up at once (`vrserver` 0.7 s after the start), then Steam asked for `-applaunch 1328670 -game 1
  -autoterminate`. No dialog. At 7.8 s `MassEffectLauncher.exe -locale {locale}  -game 1 -autoterminate` (child of
  `EASteamLauncher.exe`), at 8.6 s `MassEffect1.exe -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles 20
  -OVERRIDELANGUAGE=INT` as the launcher's child, and the launcher gone. `{locale}` is literal, as the real launcher and
  LEBinkProxy pass it (`launcher_args.h`). The launcher's log, whole:

      19:36:05.938 MEVR ASI loader (bink2w64.dll proxy, SPI 3) in MassEffectLauncher.exe
      19:36:05.948 -game 1: started Mass Effect 1 (pid 69132) with -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles 20 -OVERRIDELANGUAGE=INT
      19:36:05.948 -autoterminate: the launcher ends

  The game's `MEVRLoader.log`: the ASI loaded after the DRM (`256 of 256 samples differ from the file; 1 polls, 0 ms`), every
  host hook in place by 19:36:10; `LE1MEVR.log`: `MEVR 0.1.0 (monolithic, commit be5144ff21)`, `47 of 47 addresses at their
  RVAs`, `no LExSDKv2 cross-check in this build`. `tools/launch-game.ps1 -Attach` pressed Enter at the title (6 times) and the
  save was in gameplay at 19:37:48, 1 min 43 s after Setup's start; a snapshot showed the world in stereo on the headset.
  The saves were unchanged afterwards (the 24 files' hashes).
- **Uninstall** after the session: everything recorded removed or renamed back, `Game\Launcher\Logs` and `Win64\Logs` (made
  by our loader) removed, and the game byte for byte the vanilla one **except `BIOEngine.ini`**: the game wrote Setup's
  resolution into its `[SystemSettings]` during play (`ResX=2344`, `ResY=2392` before, `3072`/`3188` after: the
  Performance size Setup put in GamerSettings.ini). Setup edits only `SmoothFrameRate` there, so nothing recorded it. The
  game keeps that section as its own copy of the system settings (floats as `0.500000`; `UseVsync` there stays `True`
  while GamerSettings says `False`, so not every key is copied); the mod's `scale set` values (`DynamicShadows`, `MotionBlur`,
  `FoliageDrawRadiusMultiplier`) land there the same way (CLAUDE.md: the game rewrites the file after every `scale set`).
  This dev machine's file already held those three, so only the resolution showed; a new player's would keep all of them.
  **Fixed the same day:** `settings::ApplyEngineSettings` now sets those keys in `BIOEngine.ini`'s `[SystemSettings]` as
  recorded edits (as `tools/vr-game-settings.ps1` already did for three of them), in the game's own spelling: the target's
  `ResX`/`ResY`, `Fullscreen=False`, `MaxAnisotropy=16`, and the mod's three whatever the choices (`DynamicShadows=False`,
  `MotionBlur=False`, `FoliageDrawRadiusMultiplier=0.500000`). The game's rewrite then leaves the same text, and Uninstall's
  `ini::Revert` puts the player's values back; a value the player changed since stays theirs. Only where the file has a
  `[SystemSettings]` section; `UseVsync` and `[HDR]` are left (the game doesn't copy them: `UseVsync=True` and
  `DesiredRange=DynamicRange_Auto` there while GamerSettings says `False` and SDR). The evidence for the list: this PC's
  `BIOEngine.ini.mevr_backup` (the PS tool's copy from before its first apply) against the dev file differs in exactly the
  resolution, `DynamicShadows`, `MotionBlur` and `FoliageDrawRadiusMultiplier`. Checked by the `install` tests (the
  section applied, reverted to the byte; a file without the section gets none; a player's change kept) and the fake game
  (its `BIOEngine.ini` now has the game's `[SystemSettings]`; 83 of 83 checks).
- **Smaller findings.** Our loader opened `MEVRLoader.log` with `_wfopen_s` (no sharing): nothing could read it while the
  game ran. Fixed: `_wfsopen` with `_SH_DENYWR`, and `mevr-loader` now reads the log while the loader holds it. `--launch` on
  the Steam path printed `started (pid 0)`; now `started (through Steam)`. `LE1MEVR.log` has six first-chance access violations before the session started (three each at
  `LE1MEVR.asi+0x7F6DC6` and `+0x77EB0D`, handled), not looked into.
- **The dev setup** went back byte for byte (the record of 9,311 files, hashes of everything Setup can touch).
- **The mod's own later writes** (found reading the code after the fix above, the same day): `ini::Revert` keeps a value
  "the player changed since", but the mod itself writes the game's settings after the install. Halcyon's
  `ApplyResolutionForCurrentMode` (`WriteGameResolution`) puts the active mode's resolution from its profile into
  GamerSettings.ini's `ResX`/`ResY`: the first VR start's eye size (`main_menu::FirstStartEyeSize`, the "Automatic"
  choice) and every resolution change in our menu. The game copies those into `BIOEngine.ini`. And Halcyon's
  `EnsureDepthOfFieldSetting` writes `DepthOfField` at every launch (adding the line when it's missing). Uninstall kept all
  of them. Now: `settings::ModWrites` reads Halcyon's `MELEVR.ini` at Uninstall (and Restore), before the uninstall removes it,
  and counts a `[SystemSettings]` `ResX`/`ResY` equal to any of its profiles' resolutions, and any `DepthOfField`, as the
  mod's (`Transaction::ModWrites`, `ini::Revert`'s `ours`). For keys Setup doesn't change (`DepthOfField`, the resolution
  with "keep"), `ini::Record` keeps their value at install as an edit whose `after` is its `before` (`EditIni` saves such a
  record without writing the file). A resolution the player picked in the game's own options that none of Halcyon's
  profiles holds stays theirs. Checked: `install` tests (the first VR start's eye size and the added `DepthOfField` line
  reverted to the byte with `ModWrites`, not without; "keep"; a player's 1920 kept) and the fake game (the mod's writes
  made before its uninstall, then byte for byte: 84 of 84). Repair doesn't undo the eye size: it applies the settings only
  while they wait for the game's first start. Left: Halcyon's attach-time `SmoothFrameRate=TRUE`/`MaxSmoothedFrameRate=120`
  in `BIOEngine.ini` ([ENGSMOOTH]), which the game's own save at exit puts back to its loaded values, so only a crash
  leaves them.

## 14. The voice orders as optional content (2026-11-05)

The voice orders (research/54 "The voice path built") need files the release didn't carry: Moonshine Voice's recognizer
runtime and models, and for the model tier llama.cpp and our fine-tuned GGUF. On the dev machine `tools/fetch-voice.ps1`
fetches them and `tools/deploy-asi.ps1` copies them into `ASI\voice`; a player had no way to get them. The user asked for
the setup's optional content to download them, beside the texture packs. Unlike those (section 3: Nexus terms, re-uploads
forbidden), nothing stops the setup fetching them itself: Moonshine Voice and its models are MIT, ONNX Runtime MIT,
llama.cpp MIT (its OpenMP runtime Apache-2.0 with LLVM exceptions), our model Apache-2.0 (Qwen3-0.6B's terms); all in
NOTICE.md. So the setup downloads each from its own release, never from a mirror of ours (except our own model).

**What goes where** (`install/Voice`, the catalogue; paths as plugin/Voice and plugin/VoiceModel look for them):

| Part | From | Into `ASI\voice` | Download | On disk |
|---|---|---|---|---|
| recognizer (always) | Moonshine Voice v0.1.5's Windows release (GitHub), its `lib/onnxruntime.dll` only | `onnxruntime.dll` | 26 MB | 14 MB |
| models-tiny (always) | Hugging Face `moonshine-ai/moonshine-voice-assets`, `tiny-streaming-en/quantized_26_08_21`, 8 files | `models\tiny-streaming-en\` | 45 MB | 45 MB |
| models-small, models-medium (ticked) | the same, `small`/`medium` | `models\<size>-streaming-en\` | 142 / 269 MB | the same |
| llm-runtime (the model tier, on by default) | llama.cpp b12128's `bin-win-cpu-x64.zip` (GitHub): `llama.dll`, `ggml.dll`, `ggml-base.dll`, `libomp.dll`, the 14 `ggml-cpu-*.dll`, `LICENSE-LLVM-OpenMP` | `llm\` | 25 MB | 26 MB |
| llm-model (with the runtime) | this mod's GitHub release `voice-model-1`, asset `model.gguf` | `llm\model.gguf` | 639 MB | 639 MB |

llama.cpp's files are what `llama.dll` needs (its imports: ggml, ggml-base; ggml-base imports libomp; read from the PE
import tables) and the CPU backends `ggml_backend_load_all_from_path` chooses from; not its tools, `mtmd.dll` or the RPC
backend, which `deploy-asi.ps1` copies as "every DLL". Every fixed download is pinned by size and SHA-256 (the hashes of
the files `fetch-voice.ps1` fetched, and of Moonshine's archive, fetched again for this); so is `onnxruntime.dll` inside
it. A pinned file that changes upstream (Hugging Face's `main` can) is refused with "another SHA-256", not installed: the
pins then need updating here. Our own `MEVR-Voice.dll` is in the release (`payload\voice`, `tools/package-release.ps1`):
Install puts it in `ASI\voice` with the mod, Repair puts it back, Uninstall removes it. It links Moonshine's static
library, so Moonshine's LICENSE goes in the release's `licenses\` (`MoonshineVoice-MIT.txt`); it has a version resource
(`src/voice/Voice.rc`, product name "MEVR") and goes through code signing with the other three binaries
(`release.yml`, `.signpath/artifact-configuration.xml`, `docs/code-signing.md`). Without the downloads the host finds no
models and logs "voice off" before it ever loads the DLL (plugin/Voice checks the models first).

**The model's release.** The fine-tuned GGUF is ours, so it comes from a release of this repository, which the user
uploads: tag `voice-model-1` (not `v*`: that would start the release workflow), asset `model.gguf` (639 MB; its SHA-256 in docs/release-checklist.md),
placed as `model.gguf`. The setup asks GitHub's API for the release by tag (`/repos/<owner>/<repo>/releases/tags/<tag>`)
and takes the asset's `browser_download_url` (github.com only), `size` and `digest` (`sha256:...`, which GitHub lists for
every asset), so the file isn't pinned in the code (it was retrained the day it was written). All of it is in
`MEVR-Setup.ini` `[Voice]` (the package writes the defaults as comments): `modelRepo` (default `[Update] repo`), `modelTag`,
`modelAsset`, or `modelUrl` (any https download) with `modelSha256` and `modelSize`. **Not published yet** (a 404 for the
tag, or the release without that asset): the rest installs, the log says why the model tier is missing, and llama.cpp
isn't fetched either; the card then says "Installed without the fine-tuned model ... Apply looks for it again". Any other
failure (offline, the API quota, a foreign URL) stops the install before anything is downloaded.

**The record.** Each placed file is an ordinary `added` (or `replaced`) entry in `MEVR-install.json` with two new fields:
`content` (`voice/<part>`) and `size`. `Transaction::PlaceIn` moves the checked download into place (copying across drives;
never read into memory: the model is 600 MB) with the SHA-256 the download was checked against; `RemoveContent("voice")`
takes them out (a replaced file's backup put back) and the folders it made once empty; `VerifyContent` checks them by size
or by SHA-256. `Transaction::Verify` (Repair's list, `needsRepair`) leaves content files out, because Repair can't put a
download back; the Optional content card shows them instead ("N voice files are missing or changed: Fetch again"). The
manifest's `packs` gets `{"id": "voice", "name": "Voice orders", "method": "download"}`, which the in-game Display >
Optional content row lists (its label is now "Voice orders and texture packs"); the uninstall's "texture packs stay" note
counts only `method: mem` records. Uninstall needs nothing new: content files are files on record.

**The flow** (`voice::Install`, shared by the card's Install/Apply and `--install-voice`): refused while the game runs (it
holds the DLLs open), or without `MEVR-Voice.dll` in the game and no payload to take it from (the setup copy in the ASI
folder has none: "install or Repair this release first"). Then every file on record is checked against its SHA-256 (a
few seconds), the model is looked up if it's wanted and not intact, and every source of a wanted part with a file
missing, of another size or another SHA-256 is downloaded into `%LOCALAPPDATA%\MEVR\downloads\voice` (`http::Download`
with the expected size and sum; a finished download from a stopped run is reused), an archive's files taken out with
Windows' `tar.exe` (bsdtar reads gzip and zip; Windows 10 1803 and later) and checked, then placed. Parts installed but no
longer ticked are taken out. The download folder is emptied. The card's Install asks first, naming the size, the hosts
and the time on a 50 Mbit/s line; its progress row's button stops a download (what's placed stays; Install again fetches
the rest). Check runs the SHA-256 check, Remove takes everything but `MEVR-Voice.dll` out. The in-game "Install after I
quit" request (plugin/SetupRequest) opens on this page as before; a request naming `voice` says "You asked for: Voice
orders.". Command line: `--install-voice [--voice-small] [--voice-medium] [--no-voice-llm]`, `--remove-voice`,
`--check-voice [--deep]`; `--status` prints a `voice orders:` line.

**Tested** (fake games only):

- `mevr-tests` (`TestVoice`): the catalogue's layout against what the plugins look for, the pins, the choices and sizes,
  the release-by-tag URL and asset parsing, `MEVR-Setup.ini`'s keys, the content entries (placed, JSON round trip, the size
  and SHA-256 checks, left out of `Verify`, `RemoveContent` restoring a replaced file and keeping `ASI\voice` for the DLL),
  Install's refusals, the payload's DLL through Install, Repair and Uninstall.
- `tools/setup/test-network.py` section 5 (ctest `mevr-setup-network`): the real archives and models served by the local
  server from the dev machine's own copies (`fetch-voice.ps1`'s in `%TEMP%`, or `--voice-cache`; skipped without them),
  so the pins are exercised: the model not published (looked up first, llama.cpp not fetched), a model file with another
  SHA-256 refused and fetched on the retry, a model release whose digest doesn't match refused, the model tier installed
  and the small models taken out when unticked, a same-size change found only by the deep check and then fetched again
  alone, the game's request naming the voice orders, Remove, `--no-voice-llm`, Uninstall removing `ASI\voice`. All passed.
- `tools/setup/test-fake-game.ps1 -Voice`: the real downloads from GitHub and Hugging Face onto `scratch\fakegame`: every
  pin matched, GitHub's 404 for `voice-model-1` took the "not published yet" path, `--check-voice --deep` passed, and the
  uninstall left the folder byte for byte what it was (110 of 110 checks).

Not yet: the model tier from the real release (the user uploads the asset); the files loaded by the game from a Setup
install (the same files `deploy-asi.ps1` deploys, which the game loads: research/54 "In the game").

**The row reads the files too** (2026-11-06, late): in the 0.1.0 build on the owner's machine the menu's Display > Optional
content row said "Installed: None yet" while the same session's log had loaded `ASI\voice\MEVR-Voice.dll` and the tiny
models: the row listed Setup's record (`MEVR-install.json`'s `packs`), and the developer deploy had put the files there
without a record. `setup_request::InstalledPacksText` now adds "Voice orders (the files are in ASI\voice, not put there
by Setup)" when the DLL and a `*-streaming-en` model folder are on disk and no pack with the voice id or name is recorded,
with or without a manifest; a Setup download records the pack and the note goes.

## 15. Files busy for a moment (2026-11-06)

`mevr-tests` failed now and then and passed on a rerun (twice in the day's merges). Looped alone on the dev machine it failed
3 times in about 80 runs, always the same Install of the fake game right after an Uninstall; the two failures that printed
their errors were both on the installer's own record, and the second gave the code: `couldn't write
...\ASI\MEVR-install.json (error 5)`, access denied. The record is written after every step (a temporary file
renamed over it), dozens of times an install, and a file just written is opened for a moment by another process; Defender's
on-access scanning was on (inferred as the holder, not traced). A rename onto, over or of a file held without delete sharing
is refused with error 5 or 32 until it closes. A player's antivirus does the same to Setup.

The fix (`src/install/Manifest.cpp`): every replace, copy, rename and delete goes through `Retrying` / `RemoveFile`, which on
access denied, a sharing violation or a lock violation waits 10, 20 ... 640 ms (1.27 s in all) and tries again; a read-only
target fails at once. `WriteFileAtomic` is Win32 calls and keeps the error code for the message. The tests print a failed
Outcome's errors (`Ok`), and every test's scratch files are under `%TEMP%\mevr-tests-<pid>` (`tests/TestTemp.hpp`): the fixed
names (`%TEMP%\mevr-install-firstrun`...) were shared by every worktree's tests, a second way for two sessions to break each
other's runs.

Measured after the fix: 0 failures in 150 runs (about 5 expected at the earlier rate), but the retry never fired in them
either, so no file was busy then: the rate depends on what else the machine does (main's unchanged test binary failed 1 run
in 2 in the same hour, while other sessions were building). So the mechanism is tested directly (`TestBusyFile`): the record
held open without delete sharing, as a scanner holds it, refuses a replace at once with error 5, the failures' code, and
`WriteFileAtomic` goes through 106-189 ms after the file is let go at 100 ms; a read-only record still fails at once.

The cost: the test that holds the ASI open on purpose (a running game) now waits the 1.27 s before its expected failure; a
player whose game holds a file waits that long before the message.

## Open

- **[verify]** In the game: the EA app build; "Install after I quit" end to end; "Start the game once" (section 9).
  Settled in sections 11 and 13: a direct start of the Steam build hands back to Steam's arguments prompt; install, verify
  then Repair, and uninstall on the real game; Launch through Steam (SteamVR, the launcher's autoboot, our loader in the
  game, gameplay on the fake headset).
- **[verify]** Against GitHub and the real MEM (section 10's "still untested"): the update check once a release exists,
  MEM's download, unpacking and a real pack install on a real game.
- **The voice orders' model release** (section 14): upload the fine-tuned GGUF as the asset
  `model.gguf` of a release tagged `voice-model-1` (not `v*`); the setup then installs the model tier.
  Another tag or name: `MEVR-Setup.ini` `[Voice] modelTag` / `modelAsset` in the release package
  (`tools/package-release.ps1` writes the file). **[verify]** the game loading a Setup-installed `ASI\voice`.
- The ME2/ME3 variants (the layout is `Game\ME<n>`; the classes take the game as a parameter only where needed).
