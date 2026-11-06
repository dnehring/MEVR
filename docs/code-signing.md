<!-- SPDX-License-Identifier: MIT -->
# Code signing policy

**Status:** releases up to now are **not signed**. MEVR is applying for SignPath Foundation's free code signing for
open-source projects. The rules below apply to every signed release from then on.

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org)

## What is signed

For each release, the four programs in `MEVR-<version>.zip`:

- `MEVR-Setup.exe`, the installer;
- `payload\LE1MEVR.asi`, the mod;
- `payload\bink2w64.dll`, its ASI loader;
- `payload\voice\MEVR-Voice.dll`, the voice orders' speech recognizer interface.

The voice orders' other files (ONNX Runtime, llama.cpp's DLLs and the models) are not ours to sign: MEVR Setup downloads
them from their authors' own releases when you ask for them and checks each against the SHA-256 it expects.

They are built by this repository's release workflow ([.github/workflows/release.yml](../.github/workflows/release.yml))
on GitHub's own runners, from a `v<version>` tag of this repository, and nowhere else. The workflow sends them to
SignPath, and nothing is signed until a maintainer approves that request. SignPath's settings for the project are
reviewed here, in [.signpath/artifact-configuration.xml](../.signpath/artifact-configuration.xml). Every signed file
carries the product name `MEVR` and the release's version.

The rest of the zip (texts, fonts, the menu's layout files, the Mako cockpit's game package) is not program code and
isn't signed. A file's signature names **SignPath Foundation** as the publisher.

## Team and roles

- Committers and reviewers: [dnehring](https://github.com/dnehring). A change from anyone else is reviewed by a
  committer before it is merged.
- Approvers: [dnehring](https://github.com/dnehring) (the repository's owner) approves each signing request.

Everyone in these roles uses two-factor authentication on GitHub and on SignPath.

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the user or
the person installing or operating it.

In detail:

- **The mod** (`LE1MEVR.asi` and its loader) makes no network connections. Voice orders, when you install them,
  recognise your speech on your own PC; nothing you say leaves it.
- **MEVR Setup** connects only when you click for it:
  - **Check for updates** asks GitHub (`api.github.com`) for the newest release of this repository;
  - **Optional content** downloads Mass Effect Modder from its GitHub releases, and opens the texture packs' own pages
    in your browser;
  - **Optional content > Voice orders** downloads the speech recognizer and its models from Moonshine AI's GitHub release
    and Hugging Face, llama.cpp from its GitHub release, and the order model from this repository's release
    (`api.github.com` first, to find it).
- **Report a problem** saves a zip of logs to your Desktop, with your user and computer names removed. Nothing is
  uploaded: you decide whether to attach it to a bug report.

## What the programs change

- **MEVR Setup** changes only the game's own folder:
  - our loader in place of the game's `bink2w64.dll` (the game's own is kept as `bink2w64_original.dll`);
  - the mod's folder;
  - the VR settings in the game's settings files;
  - one entry in the game's file table, for the cockpit package.

  Its Install page lists these before you install. The one-click Install on its first page makes the same changes
  with the defaults. Either way, Setup lists each change as it makes it.
- **Every change is recorded**, and **Uninstall** puts the game back as it was, including the settings.
- **The mod** runs inside Mass Effect 1 only. It loads with the game, after the game's own copy protection has
  started, and changes the game's own functions in memory to show it in VR. It doesn't bypass the game's copy
  protection.
- **Outside the game's folder,** the mod writes only its logs and diagnostics, in `%LOCALAPPDATA%\MEVR` and
  `%LOCALAPPDATA%\MELEVR`.

## Checking a download

- **Signed files:** in PowerShell, `Get-AuthenticodeSignature <file>` should say `Valid`, with SignPath Foundation as
  the signer.
- **Any release, signed or not:** `Get-FileHash MEVR-<version>.zip -Algorithm SHA256` should match the release's
  `SHA256SUMS` file.
