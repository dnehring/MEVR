<!-- SPDX-License-Identifier: MIT -->
<!-- Hero banner: a draft exists (1600x600), but it is built on EA's official screenshot; add it here only once its
     use is cleared (EA's fan-content rules). <p align="center"><img src="assets/readme/hero.jpg" alt="MEVR"></p> -->

# MEVR: Mass Effect in VR

**Play Mass Effect 1 (Legendary Edition) in virtual reality, with your own hands.** MEVR is a free, open-source mod
that puts you inside Shepard: stereo 3D at life size, head tracking, and motion controllers for everything. Draw your
pistol from your hip, steady a rifle with both hands, throw grenades from your chest, cast biotics with a push of your
open hand, order your squad by pointing or by voice, and drive the Mako from its cockpit.

**[Download MEVR-Setup.exe](https://github.com/dnehring/MEVR/releases/latest)** ·
[Website](https://dnehring.github.io/MEVR/) · [Controls](docs/controls.md) · [Report a problem](#report-a-problem) ·
[Discord](https://discord.gg/flat2vr)

> **Development preview.** Mass Effect 1 only for now; Mass Effect 2 and 3 come later. Much of it has been played
> in a headset only a few times, so your reports are very welcome.

<!-- Screenshot: holding a pistol, hand reticle and wrist status visible -->
<!-- Screenshot: the power wheel at the left palm -->
<!-- Screenshot: the Mako cockpit with throttle and stick -->

## Get started

1. **Download** `MEVR-Setup.exe` from the [Releases page](https://github.com/dnehring/MEVR/releases/latest).
2. **Run it and click Install.** It finds your game (Steam, the EA app, or a folder you pick), installs the mod and its
   own loader (no ME3Tweaks Mod Manager needed), and sets the game up for VR. Everything it changes is recorded, so
   Uninstall puts your game and settings back exactly.
3. **Click Launch.** It starts SteamVR when that's your runtime, then Mass Effect 1 straight past the launcher. Put on
   the headset.

For later releases, open **MEVR Setup → Welcome → Updates → Check**, then **Download and install**.
Setup verifies the download, waits for the game to close, and updates the mod while keeping your game settings,
VR profiles and calibration. Older installers that only show **Release page** need one manual installer upgrade
to gain this feature.

In the game, **hold both stick clicks** (or press `\`) for the VR menu: comfort, controls, calibration, display and
more. The first start offers a one-minute **calibration** that measures your height, arms and aim: worth doing.

Windows SmartScreen or an antivirus may warn about the setup, because releases aren't code-signed yet
([code signing policy](docs/code-signing.md)): choose **More info > Run anyway**.

## What you need

- **Mass Effect Legendary Edition** on PC (Steam or the EA app), game version **2.0.0.48602** (the current one). On
  any other version the mod switches itself off and leaves the game untouched.
- **A PC VR headset** through OpenXR:
  - **PS VR2** with Sony's PC adapter, through SteamVR: the headset most testing used.
  - **Meta Quest** through Steam Link (tried), or Quest Link / Air Link on Meta's runtime (expected to work; only an
    early build was tried there).
  - **Others** (Index, Vive, Reverb G2, Windows Mixed Reality): their controllers are supported, but they haven't been
    tried yet. [Which controllers can do what](docs/controls.md#headsets-and-controllers).
- **A strong GPU.** Development runs on an RTX 4090; the heaviest scenes don't hold 90 Hz everywhere. The Display
  page's Performance setting (Quality, Balanced, Performance) trades detail for frame rate.
- Start your headset's runtime and wake the headset before the game: the game connects to VR once, at its start.

## Features

**Your hands**
- You aim with the controller, never your head; your arms, hands and fingers follow the controllers, the gun is in
  your hand, and it kicks when you fire.
- Body holsters: pistol at the right hip, shotgun at the left, rifles over your shoulders. Reach and squeeze to draw.
- Hold rifles with both hands; red dots and holo sights on the guns, and a real zoomed scope on the sniper rifle.
- Physical grenades from your chest, melee by a real strike, and vent an overheated gun by racking it.

**Powers and squad**
- Your powers on a wheel at your left palm, lit from the omni-tool; lock on an enemy with your left arm and push your
  open hand to cast. Powers on yourself by a gesture (palm on your chest, slapped onto the gun).
- Point to order your squad (attack, move, regroup), throw an arc to send them somewhere, or hold your hand to your
  ear like a radio and speak the order. Give them weapons from the wheel at your left hand.

**The world**
- Life-size stereo, rendered from where each eye really is; crouch for real, take cover by ducking behind things.
- Conversations and cutscenes on a large 3D cinema screen at your headset's full frame rate (or in VR, or flat).
- The game's screens (shop, loot, journal, codex, map) as panels in the room, worked by laser pointer or fingertip.
  Tap your left wrist for an omni-tool menu of them; hacking is projected from your wrist.
- The galaxy map as a hologram over the Normandy's projector.
- The HUD on a panel that follows your body, your health on your left wrist, your gun's heat on the gun.

**The Mako**
- Sit in its cockpit with a throttle lever and a flight stick; tilt to steer, twist to turn on the spot, and steer
  the jump jets in the air. A squadmate runs the guns while you drive, or take them with your right hand.
- Repair it from the desk button, call extraction with the overhead handle; the cockpit flashes red when it's hurt.

**Comfort and setup**
- Smooth or snap turning, an optional vignette, walking toward your head or your left hand, per-headset colour
  presets, FSR upscaling.
- Our own VR menu, which pauses the game, with the game's pause menu one click away.
- One-click install, Repair (after Steam's "verify files"), exact Uninstall, and optional texture packs.

The full list is in the [changelog](CHANGELOG.md); every button and gesture is in [Controls](docs/controls.md).

## Known issues

- **Mass Effect 1 only**, on game version 2.0.0.48602.
- **Performance:** the heaviest scenes don't hold 90 frames a second everywhere; a lower render resolution helps.
- **Walking more than about 0.75 m** around your room leaves Shepard behind: recenter (double-click the right stick).
- **Steam's "verify files"** or a game update can remove the loader: run **Repair** in `MEVR-Setup.exe` from the
  downloaded release.
- **Scopes show a plain duplex reticle**, and the sniper scope's dark areas can show a black speckle
  (`[Sights] scopeCapturePost = 0` in `ASI\LE1MEVR.ini` removes it, with the game's colour grade).
- **The galaxy map's travel question** may ignore the pointer's click; the assault rifle's red dot can sit off the
  sight when held with two hands.
- **Not code-signed yet:** SmartScreen or antivirus warnings (see [Get started](#get-started)).

More symptoms and fixes: [Troubleshooting](docs/troubleshooting.md) (your AI assistant can read it too). Still stuck?
Ask on [Discord](https://discord.gg/flat2vr).

## Report a problem

**Having trouble? Join the [Flat2VR Discord](https://discord.gg/flat2vr) and ask in the MEVR channel.**
Ask there first: other players and the developer can often help right away, and it's the quickest way to find out
whether a problem is already known.

To report a bug, open **MEVR Setup > Report a problem** (or, in the game, the VR menu's **Advanced > Diagnostics > Report**). It saves
`MEVR-report-<date-time>.zip` on your Desktop: logs, settings and a summary, with your Windows user name, computer
name and e-mail addresses removed. **Nothing is uploaded**: you look at the zip and decide. Its **Open a GitHub
issue** button opens the [bug form](https://github.com/dnehring/MEVR/issues/new?template=bug.yml) with the versions
filled in; attach the zip there. After a crash, the setup and the menu offer a report themselves. For something
visual, a quick tap of both stick clicks saves what each eye sees.

## Credits

- **Halcyon:** [MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) (GPL-3.0), the stereo renderer MEVR is
  built on: OpenXR, stereo, first person and head look are Halcyon's work.
- **Aeka0:** [Overlord](https://github.com/Aeka0/Overlord) (GPL-3.0): parts of the arm IK, two-hand aim, support grip
  and sight assist.
- **Aphar:** [MassFPS](https://www.nexusmods.com/masseffectlegendaryedition/mods/1731), studied for its first-person
  camera (none of its files are in the repository).
- **ME3Tweaks and llamathings:** LExSDKv2, LEBinkProxy and Legendary Explorer, the LE modding tools this work stands on.
- **Fonts:** Rajdhani (Indian Type Foundry) and Titillium Web (Accademia di Belle Arti di Urbino), SIL OFL 1.1.
- **Libraries:** [RmlUi](https://github.com/mikke89/RmlUi), [FreeType](https://freetype.org),
  [Dear ImGui](https://github.com/ocornut/imgui), [MinHook](https://github.com/TsudaKageyu/minhook),
  [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR), the
  [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK); for voice orders, Moonshine Voice, ONNX Runtime
  and llama.cpp (downloaded by the setup from their own releases).

## Licence

The project as a whole is [GPL-3.0](LICENSE): a build links Halcyon's GPL-3.0 renderer. Our original files are
[MIT](LICENSES/MIT.txt); every source file names its licence on its first line (`SPDX-License-Identifier`), and the
files containing Halcyon's or Overlord's code stay GPL-3.0-only. Third-party components keep their own licences, listed
in [NOTICE.md](NOTICE.md). The players' build contains no LExSDKv2. Details:
[docs/developers.md](docs/developers.md#licensing-in-detail).

Releases will be signed through SignPath Foundation's open-source programme; the mod makes no network connections, and
MEVR Setup goes online only when you ask it to ([code signing policy](docs/code-signing.md)).

## For developers

Building from source, hot reload, tests, the repository layout, how the mod hooks the engine, the voice orders'
downloads and the logs to attach by hand are in **[docs/developers.md](docs/developers.md)**. The design and every
subsystem are documented in [CLAUDE.md](CLAUDE.md) and [docs/development-approach.md](docs/development-approach.md);
sourced findings in [research/](research/README.md). Contributions and reports are welcome.

For repeatable native performance QA, [the cafeteria test](tools/qa-cafeteria/README.md) supplies a protected
fake-headset walk, higher workload, head-turn capture and verified settings/save restoration.

Mass Effect is a trademark of EA International (Studio and Publishing) Ltd. MEVR is a fan project, not affiliated with
or endorsed by EA or BioWare.
