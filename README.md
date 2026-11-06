# MEVR: Mass Effect Legendary Edition in VR

**A free VR mod for Mass Effect 1 (Legendary Edition) on PC.** Play it in stereo 3D at life size with head tracking,
and with motion controllers: aim with your hand, draw from body holsters, hold rifles with both hands, throw real
grenades, and order your squad by pointing or by voice. For PC VR headsets through SteamVR (tested on Quest 3 over
Steam Link and on PS VR2). Built on Halcyon's [MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) renderer.

**[Download](https://github.com/dnehring/MEVR/releases/latest)** | [Website](https://dnehring.github.io/MEVR/) |
[Code signing policy](docs/code-signing.md)

Under the hood (the project's working name is ME VR Attempt): stereo 3D and 6DOF on Halcyon's renderer, with our
motion controls on top: controller aim, arm and body IK, body holsters, a two-handed rifle grip, physical grenades and
melee, gun sights and scopes, a VR main menu and a guided calibration.

**Status: development preview, LE1 only.** LE2 and LE3 build (on our own early renderer) for development,
but they aren't playable in VR. **Downloads:** players' builds (MEVR-Setup, a one-click installer) go on the
[Releases page](https://github.com/dnehring/MEVR/releases); until the first one is published, build from source. The
voice orders' recognizer and models aren't in the download: the setup's **Optional content** page fetches them from
their authors' releases ([research/33](research/33-installer.md) section 14; on a development machine
`tools/fetch-voice.ps1`). The
players' build runs entirely on our own reflection layer (`src/ue3`, [research/34](research/34-reflection.md)): it
contains none of [LExSDKv2](https://github.com/llamathings/LExSDKv2), which development builds still use for a
cross-check.

- **Game:** `MassEffect1.exe` **2.0.0.48602** (Steam or the EA app). On any other version the mod logs
  `unsupported game version` and switches itself off, leaving the game untouched. **Mass Effect 1 only:** Mass
  Effect 2 and 3 are not playable in VR.
- **Headsets:** tried on a **PS VR2** and (until 2026-10-21) a **Quest 3 over Steam Link**, both through SteamVR. See
  [Headsets and controllers](#headsets-and-controllers).
- **Before the game:** start SteamVR (or your headset's runtime) and wake the headset. The game connects to VR once,
  at its start; if the runtime isn't ready then, that run stays flat.

**License:** the project as a whole is [GPL-3.0](LICENSE); our original files are MIT. Each source
file names its licence on its first line; see [Licensing](#licensing) and [NOTICE.md](NOTICE.md).

## Headsets and controllers

**Headsets tried so far**, all with SteamVR as the OpenXR runtime:

- **PS VR2** (with Sony's PC adapter): the headset most test sessions used.
- **Quest 3 over Steam Link:** used until 2026-10-21, before most of today's features existed.
- **Meta's own runtime** (Quest Link, Air Link): only an early build ran on it (a Quest Pro, 2026-10-07), before the
  current renderer. Not tried since.
- **Virtual Desktop** and every other headset: not tried.

Many features added since late October were checked only on SteamVR's simulated headset (the null driver), not yet by
a person wearing one ([docs/release-checklist.md](docs/release-checklist.md) section 2).

**Controllers.** The mod suggests bindings for six interaction profiles (`src/core/xr/Input.cpp`); the runtime picks
one:

| Profile | Controllers | What works |
|---------|-------------|------------|
| `oculus/touch_controller` | Touch (Quest, Rift); the PS VR2 Sense controllers, which SteamVR presents as Touch | Everything. The only ones tried |
| `valve/index_controller` | Valve Index | Everything; the left controller's A and B are X and Y. Not tried |
| `hp/mixed_reality_controller` | HP Reverb G2 (when the runtime offers `XR_EXT_hp_mixed_reality_controller`) | Everything, laid out as on Touch. Not tried |
| `microsoft/motion_controller` | Windows Mixed Reality | The trackpad's click is A; no B, X or Y. Not tried |
| `htc/vive_controller` | HTC Vive wands | The trackpad is the stick; no A, B, X or Y. Not tried |
| `khr/simple_controller` | the runtime's fallback | Trigger and menu only: guns, holsters, walking and turning don't work |

The log names the profile the runtime chose and what's missing on it (`halcyon bridge: controller profile ...`).

## Controls

The controllers reach the game as its gamepad (Xbox pad 0, `src/game/GamepadMapping.*`), with hand gestures on top.
The **grip** is the button under the middle finger, the **trigger** the one under the index finger. Left-handed play
isn't supported yet. Settings named below are in `Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini`; most also have a row in
the VR menu.

### On foot

| Control | What it does |
|---------|--------------|
| Left stick | Walk, toward where you look (or where the left controller points: VR menu, Comfort) |
| Right stick | Turn (the game's own turn; snap turning in the VR menu's Comfort page) |
| Right trigger | Fire |
| Right grip at a holster | Draw that weapon. Keep the grip squeezed to hold the gun. Let go at its holster to put it away; let go anywhere else and it goes back after 0.3 s unless you squeeze again |
| Left grip on the gun | Hold it with both hands (anywhere along the gun). With the gun raised to your cheek the game gives you its aimed fire |
| Left trigger | The weapon wheel, at your left hand. Point your left hand at a squadmate as you pull to start on their weapon; the right stick highlights a weapon; let go of the trigger to give it. B closes the wheel without a change |
| Right stick click | A tap uses your mapped power. Hold it and steer with the right stick for the power wheel; let go to use the highlighted power |
| Right stick, double-click | Recenter the view |
| Left stick click | Crouch on or off |
| A | Use what you look at (talk, open, pick up). Hold to sprint |
| B | Melee, with a weapon drawn |
| X | Throw a grenade the game's way, with a weapon drawn |
| Y | Medi-gel |
| Both stick clicks, held | Open or close the VR menu (or `\` on the keyboard) |
| Both stick clicks, tapped | Save what each eye sees, for a bug report (`%LOCALAPPDATA%\MEVR\snapshots`) |
| Left menu button | The game's pause menu (with the left grip held: holster). Not every headset sends it (a Quest over Link may not): the VR menu's **Game menu** button does the same |

The gamepad's d-pad (squad orders) has no controller button.

**Holsters:** the pistol at your right hip, the assault rifle over your right shoulder, the shotgun at your left hip (a
cross-draw with the right hand), the sniper rifle over your left shoulder. The controller buzzes while your hand is in a
holster. Their places scale with your standing height; the calibration measures it. Only a holster draws a weapon on
foot: the game's own buttons don't pull your gun out for you (medi-gel and powers still do).

**With your hands:**

- **Grenade:** reach across to the left side of your chest with your right hand and squeeze the right grip: you hold
  a grenade. Throw it with a quick release; let go slowly and it goes back. If your weapon is away, taking a grenade
  draws it (Mass Effect 1 only throws with a weapon out). `[Throwables] hands = both` lets the left hand take them too.
- **Melee:** a fast punch or swing at an enemy within about 2 m in front of you. B works as well.
- **Vent:** cools a hot gun at once (a change to Mass Effect 1's rules). When the gun's heat is at least 30%, put your
  left hand on the gun just above and ahead of your right hand (a tick in the controller says you're there), squeeze
  the left grip, pull back fast and let go, like racking a slide. Once every 4 s. `[Vent] enabled = 0` turns it off.
- **Crouch:** crouch for real and Shepard crouches; stand up and he stands. The game's cover snapping is off in VR:
  take cover by crouching or leaning behind things.
- **Use:** look at what you want to use. Its name and action float at the object; the A button appears once you're
  close enough.
- **Fingers:** Shepard's index finger follows your trigger, his other fingers your grip.

### Screens and menus

- **VR menu:** both stick clicks held (or `\`). Point with either controller and pull the trigger, or move with the
  left stick and press A. The right stick scrolls; B goes back, then closes. Its top bar has **Game menu** (the game's
  pause menu) and **Holster**.
- **The game's screens** (shop, loot, equipment, journal, codex, map, pause menu) stand as a panel in the room. Point a
  controller at one: its ray is the mouse, the trigger clicks, the right stick scrolls. The gamepad controls work
  there too. The screen's button hints switch to mouse and keyboard once you point; a press of A switches them back.
- **Hacking** (the bypass minigame) is projected from your left wrist, like the omni-tool. Point at it with the right
  controller and pull the trigger. VR menu: Comfort > Game screens > Hacking screen (On my wrist, In the room).
- **Galaxy map:** walk up to the console in the CIC and the galaxy stands over the projector. Point at a cluster,
  system or planet and pull the trigger to open it. The game's own map then shows over the table; answer its travel
  question with the pointer. Walk away from the table to close the map.
- **Conversations and cutscenes** play on a 3D screen in front of you. VR menu: Comfort > Conversations and cutscenes
  (In VR, 3D screen, Flat screen). The dialogue wheel works as on a gamepad.

### In the Mako

Until you take hold of a control, the Mako drives as in the flat game: the left stick drives, the right stick turns the
camera, the left trigger boosts, A fires the jump jets, B gets out. The right controller aims the turret wherever you
point it: the right trigger fires the machine gun, the right grip the cannon.

**The cockpit's controls** (`[Mako] controls = 1`, on by default): you sit in the Mako's cockpit, with the throttle
lever at your left armrest and the drive stick at your right. Bring your left hand to the lever, or your right hand to
the stick, and squeeze its grip to take it. The chase view is on the centre screen, the turret's view on the right one.

| Control | What it does |
|---------|--------------|
| Lever (left grip held) | Push forward to drive, pull back to reverse. It has a notch at neutral and returns there when you let go. X: jump jets (they also right a flipped Mako). Y: boost |
| Stick (right grip held) | Tilt left or right to steer. Twist it to steer harder; with the lever at neutral, a twist turns the Mako on the spot. While the jets or the boost fire, tilt it forward or back to pitch the Mako. It springs back when you let go. Trigger: machine gun. A: cannon. Thumbstick: aims the turret |
| Right hand off the stick | Point to aim the turret. Trigger: machine gun. Grip: cannon |
| B | Get out |

Once you've taken a control, the left stick doesn't drive any more, X, Y and the left trigger work only with the lever
held, and A only on the stick. The game's own controls come back when you get out, open the VR menu, or a controller loses tracking.
`[Cockpit] enabled = 0` keeps the game's chase camera (the lever and stick then show as two flat markers at your knees);
`[Mako] controls = 0` keeps the game's own controls.

## How it works

ME LE runs on BioWare's fork of Unreal Engine 3, compiled into `MassEffect1/2/3.exe`. VR is a renderer-level
feature, so it can't be added as content ([research/01](research/01-feasibility.md)). Instead:

- **A native ASI plugin** (C++), loaded by our own `bink2w64.dll` ASI loader (`src/loader`, installed by
  `MEVR-Setup.exe`) or by ME3Tweaks' LEBinkProxy if Mod Manager installed it.
- **Halcyon's GPL-3.0 renderer** (`src/halcyon/le1`, imported verbatim, then modified with marked `[MEVR]`
  seams) owns OpenXR, stereo (two renders per frame), first person and head look.
- **Our part is the body and the hands** (VRIK): the right controller aims (no head aim), arm IK puts the gun
  in the hand, plus the two-handed grip, holsters, grenades, sights, comfort options and a HUD on panels.
  Game mechanics change where VR needs them to (no cover snapping, a physical crouch).
- **Engine access** goes through the game's own UE3 reflection: our layer `src/ue3`, replacing LExSDKv2
  step by step ([research/34](research/34-reflection.md)).

The current design is in [CLAUDE.md](CLAUDE.md) and [research/](research/README.md). The early milestone
plan is in [notes/milestones.md](notes/milestones.md).

## Build (contributors)

Requires Windows, VS 2022 (Build Tools is enough), CMake 3.27 or later, and Git. Clone with submodules
(`git submodule update --init`): LExSDKv2 (`RVA` branch), the Khronos OpenXR SDK, RmlUi and FreeType.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MEVR-LE1
powershell -File tools/deploy-asi.ps1 -Game LE1
```

- **The loader:** deploying needs an ASI loader in the game: our own `MEVR-Loader` (`bink2w64.dll`,
  installed by `MEVR-Setup.exe`, see [research/33](research/33-installer.md)) or ME3Tweaks' LEBinkProxy
  (installed by ME3Tweaks Mod Manager). Undeploy with `tools/deploy-asi.ps1 -Remove`.
- **Hot reload:** the default build is a small host ASI plus `LE1MEVR_mod.dll`, which the running game
  reloads about a second after each rebuild of `MEVR-LE1-mod`. `-DMEVR_HOT_RELOAD=OFF` builds the single ASI
  that releases use (`tools/package-release.ps1` packages it with `MEVR-Setup.exe`).
- **Tests** (no game needed): `cmake --build build --config Release --target mevr-tests`, then
  `ctest -C Release --test-dir build`.
- **Game settings:** before playing in VR, run `tools/vr-game-settings.ps1` (VSync, motion blur and film grain
  off, a borderless window, the stereo target; `-Restore` undoes it).
- Machine setup: [docs/prep-checklist.md](docs/prep-checklist.md). Hotkeys, the test loop and the current test
  plan: [CLAUDE.md](CLAUDE.md) and [notes/next-vr-session.md](notes/next-vr-session.md).

In the game, `\` or both thumbsticks clicked in and held open the VR menu ([Controls](#controls)).

## Known issues

- **LE1 only.** LE2 and LE3 aren't playable in VR yet.
- **Performance:** in the heaviest scenes the render thread needs about 11 ms a frame for the two scene
  renders ([research/35](research/35-performance-attempt-log.md), [37](research/37-single-pass-stereo.md)),
  so 90 Hz isn't held everywhere. A lower render resolution (Display in the VR menu) helps the GPU.
- **Much of it is new:** the project started in October 2026; many features have been tried in the headset only
  a few times, some not yet (see [notes/next-vr-session.md](notes/next-vr-session.md) and
  [docs/release-checklist.md](docs/release-checklist.md) section 2).
- **Windows SmartScreen or an antivirus may warn** about `MEVR-Setup.exe` or the loader: releases aren't code-signed
  yet. Click **More info > Run anyway**, or unblock the zip in its Properties before unzipping. The
  [code signing policy](docs/code-signing.md) says how releases will be signed and what the programs do.
- **Steam's "verify files"** (or a game update) can put back the game's own `bink2w64.dll` and `PCConsoleTOC.bin`,
  which removes the ASI loader and the Mako cockpit's table entry: run **Repair** in the `MEVR-Setup.exe` from the
  downloaded zip (the copy in the game's `ASI` folder has no payload and can't repair).
- **Equipment and character record in the room** show no 3D model of Shepard or the squadmate (it lives in the game's
  own UI scene, research/53). `[WorldUi] screens` without `Inventory`, `XMods` and `CharacterRecord` puts those
  screens back on the flat panel, model included.
- **The galaxy map's travel question** may ignore the pointer's click ([research/51](research/51-galaxy-map.md)
  section 8.1).
- **Panels have no depth:** the game's UI quads draw over everything, so the radar can cover the cockpit's throttle
  and the galaxy map's disc the console's front.
- **The dialogue wheel is small** on the 3D cinema screen (about 0.9 degrees tall, [research/52](research/52-cine-3d-screen.md)).
- **Scopes show a plain duplex reticle** (the default since 2026-11-04): the game's own scope view (`[Sights] reticle =
  game`) can't be read yet ([research/21](research/21-sights-and-scopes.md) section 16), and falls back to the duplex.
- **The assault rifle's red dot held with two hands** can sit off the gun's sight.
- **The sniper scope's dark areas show a black speckle.** The zoomed view is a real render of the scene, and the game's
  post-processing adds the speckle to it ([research/39](research/39-scope-capture.md) section 12). `[Sights]
  scopeCapturePost = 0` in `ASI\LE1MEVR.ini` removes it, but the view then loses the game's colour grade.

## Reporting a bug

Before reporting, [docs/troubleshooting.md](docs/troubleshooting.md) covers the common problems; your AI assistant can read it.

**First: Report a problem.** MEVR Setup's **Report a problem** page (or, in the game, the VR menu's **Advanced >
Diagnostics > Report**, which runs the setup in the background) saves `MEVR-report-<date-time>.zip` on your Desktop:
the mod's, renderer's and loader's logs, the crash breadcrumbs, your settings and install record, a listing of the
game's Win64 and ASI folders (names, sizes and dates only), optionally the latest snapshot and frame-timing capture, and
`REPORT.md` first: versions, headset, GPU, the last session's errors and warnings, the crash, the settings you
changed, and a prompt to paste into your AI assistant. Your Windows user name, profile folder, computer name and
e-mail addresses are removed from every file. **Nothing is uploaded or sent**: you look at the zip and decide. The
page's **Open a GitHub issue** opens the [issue form](https://github.com/dnehring/MEVR/issues/new?template=bug.yml)
with the versions and the error filled in; attach the zip there. After a crash, the setup and the VR menu offer a
report themselves. From a script: `MEVR-Setup.exe --report [--snapshot] [--perf]`.

**By hand**, open an issue at <https://github.com/dnehring/MEVR/issues> with what you did, what you expected and
what happened, your headset and OpenXR runtime, and:

- **the mod's log:** `Game\ME1\Binaries\Win64\Logs\LE1MEVR.log` (and `LE1MEVR.prev.log`, the run before; if the
  game folder isn't writable they are in `%LOCALAPPDATA%\MEVR\`);
- **after a crash:** `%LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt` (the one before is `crash-breadcrumbs.prev.txt`);
- **the renderer's log:** `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt` (its end is enough; over 16 MB at a launch it moves to
  `MELEVR_Log.prev.txt` and a new one starts);
- **the loader's log:** `Game\ME1\Binaries\Win64\Logs\MEVRLoader.log` (ours) or `bink2w64_proxy.log` (ME3Tweaks').

A snapshot helps with anything visual: Advanced > Diagnostics > Snapshot in the VR menu (or a quick tap of both
stick clicks) saves what each eye sees to `%LOCALAPPDATA%\MEVR\snapshots`. Logs attached by hand can contain your
Windows user name in file paths: replace it if you prefer.

## Layout

| Path | What lives there |
|------|------------------|
| `src/core/` | Game-independent VR core: OpenXR, D3D11, controllers, IK, calibration, comfort, colour and sight math (unit-tested) |
| `src/ue3/` | Our UE3 reflection layer: objects, names, classes, fields, functions and calls, read from the engine's own reflection data ([research/34](research/34-reflection.md)) |
| `src/engine/` | Engine hooks and adapters: ProcessEvent, bone poses, the scene view, per-game field spellings, the RVA table, and `engine/Legacy` (the last LExSDKv2 uses) |
| `src/game/` | ME gameplay: hand aim, holsters, squad weapons, grenades, posture, comfort, sights, calibration applied |
| `src/plugin/` | The host ASI: the bridge into Halcyon's renderer, our quads (HUD, reticle, wheel, sights, vignette), the test loop, the perf probe, crash breadcrumbs |
| `src/plugin/menu/` | The VR main menu's engine: RmlUi's DX11 renderer wrapped for the game, our decorators and icons, the data model, the frosted backdrop |
| `src/mod/` | The hot-reloadable mod DLL's entry and API |
| `src/halcyon/` | Halcyon's MassEffect1-VR renderer (GPL-3.0), imported verbatim and then modified; `mevr_bridge.h` is our seam interface into it |
| `src/install/` | The install library behind `MEVR-Setup.exe`: game detection, the manifest, ini edits, exact uninstall (unit-tested) |
| `src/loader/` | Our ASI loader, `bink2w64.dll`: forwards the game's Bink calls and loads `ASI\*.asi` with the SPI interface ([research/33](research/33-installer.md)) |
| `assets/` | The menu's look: `assets/menu/main.rml`/`.rcss` (in game) and `setup.rml`/`.rcss` (the installer) |
| `tools/` | Deploy, game settings, test loop, perf probe, Ghidra scripts, RVA checks, offline previews (`menu-preview`, `sight-preview`, `calibration-preview`...) and packaging |
| `tools/setup/` | `MEVR-Setup.exe`, the players' installer (Welcome, Install, Optional content, Settings, Repair, Uninstall, Launch) |
| `tests/` | Offline tests (ctest) |
| `third_party/` | Dear ImGui, MinHook and the menu's fonts (Rajdhani, Titillium Web), each with its licence |
| `external/` | Git submodules (LExSDKv2, OpenXR-SDK, RmlUi, FreeType) and AMD's FSR 1 headers (`external/fsr1`). See [NOTICE.md](NOTICE.md) |
| `docs/` | Plans and decisions |
| `research/` | Sourced findings, hypotheses to verify, all URLs ([index](research/README.md)) |
| `notes/` | Milestone log, test plans, inventories, upstream patches for Halcyon |
| `reference/`, `mods/` | Third-party repos for reading and raw mod downloads (both gitignored; `tools/fetch-references.ps1` recreates the first) |

## Docs

- [Developer machine setup](docs/prep-checklist.md): tools to install, first setup of a game install
- [Development approach](docs/development-approach.md): build on Halcyon, own the VRIK, the test loop, Ghidra
- [Squad weapons design](docs/squad-weapons-design.md): giving squadmates weapons from the left hand
- [Reuse and licensing](docs/reuse-and-licensing.md): what we can copy versus only learn from
- [Release README](docs/release-readme.md): the players' README for a future release
- [Troubleshooting](docs/troubleshooting.md): symptoms, the log lines that confirm them, and fixes (for players and
  their AI assistants)
- Historical plans (2026-10-07): [trilogy design](docs/trilogy-design.md), [VR features plan](docs/vr-features-plan.md),
  [stereo stage 2 design](docs/stereo-stage2-design.md)

## Credits

- **Halcyon:** [MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) (GPL-3.0), the renderer this mod is
  built on.
- **Aeka0:** [Overlord](https://github.com/Aeka0/Overlord) (GPL-3.0): parts of the arm IK, two-hand aim, support
  grip and sight assist.
- **Aphar:** [MassFPS](https://www.nexusmods.com/masseffectlegendaryedition/mods/1731), studied for its
  first-person camera (none of its files are in the repository).
- **ME3Tweaks and llamathings:** LExSDKv2, LEBinkProxy and Legendary Explorer, the LE modding tools this work
  stands on.
- **Fonts:** Rajdhani (Indian Type Foundry) and Titillium Web (Accademia di Belle Arti di Urbino), SIL OFL 1.1.
- **Libraries:** [RmlUi](https://github.com/mikke89/RmlUi), [FreeType](https://freetype.org),
  [Dear ImGui](https://github.com/ocornut/imgui), [MinHook](https://github.com/TsudaKageyu/minhook),
  [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR), the
  [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK).

## Code signing policy

Releases will be signed through SignPath Foundation's free open-source programme. Until then they are unsigned. See
[docs/code-signing.md](docs/code-signing.md) for what is signed, who approves it, and the privacy policy: the mod makes
no network connections, and MEVR Setup goes online only when you ask it to.

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
  without one. LExSDKv2 has no licence: it is only a submodule for development builds, and the players' build
  doesn't contain it ([docs/reuse-and-licensing.md](docs/reuse-and-licensing.md)).

Mass Effect is a trademark of EA International (Studio and Publishing) Ltd. This is a fan project, not affiliated
with or endorsed by EA or BioWare.
