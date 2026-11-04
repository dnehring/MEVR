<!-- SPDX-License-Identifier: MIT
The GitHub release's text. tools/package-release.ps1 -Zip fills the {{...}} fields and writes
dist\MEVR-<version>-release-notes.md (this comment is dropped): {{changes}} is CHANGELOG.md's entry for the version.
Review it before publishing: the cockpit, the known issues, and whether the checksums are those of the zip you upload. -->
# MEVR {{version}}: Mass Effect 1 in VR

VR for **Mass Effect 1, Legendary Edition** on PC, played with motion controllers. Built on Halcyon's MassEffect1-VR
renderer. **LE1 only.**

## Install

1. Download **{{zip}}** below and unzip it anywhere. Keep the `payload` folder next to `MEVR-Setup.exe`.
2. Start the game once and quit, so it creates its settings file.
3. Start your VR runtime (SteamVR, the Meta app or Virtual Desktop), then run **MEVR-Setup.exe** and press **Install**.
4. Press **Launch**. In the game, hold both thumbsticks (or press `\`) to open the VR menu.

Needs `MassEffect1.exe` **2.0.0.48602** (Steam or the EA app, up to date). The zip's README.md has the details: what
Install changes, optional texture packs, uninstalling.

## What's new

{{changes}}

## Problems?

- [The troubleshooting guide](https://github.com/dnehring/MEVR/blob/main/docs/troubleshooting.md) covers the common ones.
- MEVR Setup's **Report a problem** saves a zip with the logs on your Desktop (nothing is uploaded); attach it to a
  [bug report](https://github.com/dnehring/MEVR/issues/new?template=bug.yml).
- The binaries aren't code-signed: Windows SmartScreen may ask before MEVR Setup runs ("More info", then "Run anyway").

## Files

| File | What it is | SHA-256 |
|------|------------|---------|
| `{{zip}}` | The release: MEVR Setup and the mod | `{{zip_sha256}}` |
| `{{symbols_zip}}` | Debug symbols for crash reports (not needed to play) | `{{symbols_sha256}}` |

Built from commit `{{commit}}` (tag `v{{version}}`). Source code: https://github.com/dnehring/MEVR, GPL-3.0. Fan
project, not affiliated with or endorsed by EA or BioWare.
