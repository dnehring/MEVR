<!-- SPDX-License-Identifier: MIT
The GitHub release's text. tools/package-release.ps1 -Zip fills the {{...}} fields and writes
dist\MEVR-<version>-release-notes.md (this comment is dropped): {{changes}} is CHANGELOG.md's entry for the version.
Review it before publishing: the cockpit, the known issues, and whether the checksums are those of the zip you upload. -->
# MEVR {{version}}: Mass Effect Legendary Edition in VR

VR for **Mass Effect Legendary Edition** on PC, played with motion controllers. Built on Halcyon's MassEffect1-VR,
MassEffect2-VR and MassEffect3-VR renderers. **Mass Effect 1**, and **Mass Effect 2 and 3 as an alpha**: much less
played, expect rough edges (the zip's README.md lists what isn't there yet).

## Install

1. Download **{{zip}}** below and unzip it anywhere. Keep the `payload` folder next to `MEVR-Setup.exe`.
2. Start the game once and quit, so it creates its settings file.
3. Start your VR runtime (SteamVR, the Meta app or Virtual Desktop), then run **MEVR-Setup.exe** and press **Install**.
4. Press **Launch**. In the game, hold both thumbsticks (or press `\`) to open the VR menu.

For Mass Effect 2 or 3, pick the game at the top of MEVR Setup's window, then Install and Launch it the same way.

Needs the games at version **2.0.0.48602** (Steam or the EA app, up to date). The zip's README.md has the details: what
Install changes, optional texture packs, uninstalling.

## What's new

{{changes}}

What comes next, and what in this release still needs testing in a headset:
[the roadmap](https://github.com/dnehring/MEVR/blob/main/ROADMAP.md).

## Problems?

- [The troubleshooting guide](https://github.com/dnehring/MEVR/blob/main/docs/troubleshooting.md) covers the common ones.
- MEVR Setup's **Report a problem** saves a zip with the logs on your Desktop (nothing is uploaded); attach it to a
  [bug report](https://github.com/dnehring/MEVR/issues/new?template=bug.yml).
- The binaries aren't code-signed: Windows SmartScreen may ask before MEVR Setup runs ("More info", then "Run anyway").

## Files

| File | What it is | SHA-256 |
|------|------------|---------|
| `{{zip}}` | The release: MEVR Setup and the mod | `{{zip_sha256}}` |

Built from commit `{{commit}}` (tag `v{{version}}`). Source code: https://github.com/dnehring/MEVR, GPL-3.0. Fan
project, not affiliated with or endorsed by EA or BioWare.
