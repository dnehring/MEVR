# 02: Titanfall 2 VR (CircuitLord)

## Status and licensing

- **Public release:** 30 Sep 2026, per a ResetEra thread edit. Earlier coverage called it an alpha.
  The package seen was v1.0.12, about 73.9 MB, labeled beta. [ResetEra, vr.org]
- **Mod: closed source, all rights reserved.** Not to be redistributed without permission. Only
  the installer is open source (MIT). [CircuitLord installer repo]
- **Installer repo contents:** a .NET Framework 4.8 installer (`src/Installer`), validation tests,
  `manifest*.json` files listing releases, URLs and SHA-256 hashes, and THIRD-PARTY-NOTICES.md.
  No mod source. [CircuitLord installer repo]
- **For us:** design reference only. No code reuse.

## How it installs and runs

- The installer is about 350 KB. Start the headset's PC software, then press "Launch in VR".
  [UploadVR]
- It downloads the mod plus **Northstar** (the open-source Titanfall 2 mod loader) into a `TF2VR`
  folder in the game directory. [UploadVR, vr.org]
- Since installer v1.2.6 it refuses to launch if another mod has replaced `dxgi.dll` or `d3d11.dll`.
  This suggests the mod (or Northstar) relies on one of those proxy points. The article doesn't
  say outright that the mod uses them. [vr.org]
- Its listing calls it "OpenXR campaign VR support for Titanfall 2". The README says to start
  "SteamVR/your VR runtime of choice". [vr.org]
- Requires your own copy of the game plus the EA app, even for Steam copies. An install under
  Program Files triggers an admin prompt. [vr.org]
- The same installer handles **Big Walk VR**, which uses BepInEx (Unity). [installer repo]

## Features

- Head tracking, motion-controller weapon aiming, titan weapon controls. [Road to VR]
- **Manual reloads across the roster:** eject the magazine, grab a fresh one from the backpack, seat
  it, then cock the weapon with the off hand. [Road to VR]
- Left-handed support. [ResetEra]
- Snap turn. Wall-running and grapple remain intense for comfort. [Road to VR]
- Campaign only; no multiplayer. [Road to VR]
- Not covered by any source: body IK, HUD handling. [vr.org]

## Reception

- UploadVR: polished from the start, unlike rushed early-access VR mods. [UploadVR]
- Aftermath: possibly one of the best VR experiences since Half-Life: Alyx. [Aftermath]

## Takeaways for ME VR

- The magazine reload choreography (eject, grab from body, insert, cock) maps onto **LE2/LE3
  thermal clips** almost directly.
- A one-click installer that checks for proxy DLL conflicts is a good distribution pattern. For
  us, ME3Tweaks Mod Manager covers most of this.
- Things to watch for when play-testing: grab-zone forgiveness, two-hand engagement, reload timing
  windows.
