<!-- SPDX-License-Identifier: MIT -->
# Updating from MEVR Setup

Welcome → Updates → Check compares the published release with the installed mod version (or this Setup version
when no install is known). Download and install fetches the package and verifies its size and SHA-256 before
starting the downloaded Setup. The new window waits for the old Setup and the selected game's processes to exit,
then installs. No browser is opened. Downloading and waiting can be cancelled; once file installation starts it
finishes the recorded install steps.

The update path preserves existing `LE1MEVR.ini`, `MELEVR.ini`, `GamerSettings.ini` and `BIOEngine.ini` byte-for-byte.
It skips both game-setting defaults and legacy VR-profile normalization, including pending first-start settings.
Saves and optional downloaded content are not update targets. Missing settings remain missing so the normal game
startup can create them. Install/Repair retain their existing behavior. A failed update does not advance the
manifest's version; its recorded file operations remain available for retry or uninstall.

The copy of Setup retained in the game can update without a local payload. It downloads to a unique directory under
the user's MEVR update cache and hands off to that package's Setup, releasing the installed executable before it is
replaced. Windows elevation is requested only when the target game folder requires it. The original window remains
open if the new process cannot be started. A newer installed version is never downgraded by the update path.

## Release contract

- Publish a new stable numeric version and matching tag; do not replace a release under the same version.
- Include `MEVR-<version>.zip` containing `MEVR-Setup.exe` and its `payload` folder, as produced by
  `tools/package-release.ps1 -Zip`. The Setup, ASI and loader file versions must match the release tag.
- The release asset must include GitHub's SHA-256 `digest`, or publish `MEVR-<version>-SHA256SUMS.txt` alongside it.
  The existing packaging script produces that checksum file. A package without a usable checksum is not offered.
- Assets must be download URLs for the same repository and tag as the release. The symbols ZIP is never selected.
- The downloaded Setup must include `--finish-update` support. The first public release with this feature requires
  users of older installers to upgrade manually once. Subsequent updates use the new flow.

The asset metadata follows [GitHub's release assets API](https://docs.github.com/en/rest/releases/assets).
ZIP entries are streamed with .NET ZipArchive into a fresh directory; paths, Windows aliases, duplicate names,
links and expanded sizes are checked before extraction. Failed staging attempts are removed. Verified packages
remain in the cache for the installer handoff and can also provide Repair's payload.

## Validation

`mevr-tests` covers strict settings preservation and metadata parsing. `mevr-setup-updates` runs the real installer
against a local HTTP server, an isolated LOCALAPPDATA and a disposable game: verified download, checksum fallback,
truncation, bad hashes, foreign assets, unsafe ZIP entries, version mismatch, waiting for the game, downgrade
refusal, and the new UI's automatic apply after the old-process wait. It compares custom INIs and save sentinels
byte-for-byte. `mevr-setup-network` retains the existing release-check, HTTP and optional-content regression coverage.
The new updater has not been published or tested against a newly published live release.
