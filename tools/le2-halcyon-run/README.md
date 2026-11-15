<!-- SPDX-License-Identifier: MIT -->
# Protected ME2 runs and ME3 desktop dumps

Until `tools/qa-le2`'s responder and Halcyon's renderer share one LE2 host (docs/le2-from-le1.md section 3.3), these
scripts run ME2 as the players' host does: our `halcyon-le2` host from `build-trilogy`, loaded by LEBinkProxy, on
SteamVR's null driver. They are what the ME2 lanes of [the continuation plan](../../docs/le2-continuation-plan.md) use.

One game run at a time on this machine, across every session and agent: **`run-lock.ps1`** names who runs the game
(`%LOCALAPPDATA%\MEVR\game-run.lock`). `prep.ps1` takes it and `restore.ps1` gives it back; take it by hand around any
other run (an LE1 run with `tools/fake-headset-session.ps1`). It follows `tools/qa-le2`'s and `tools/qa-cafeteria`'s
convention: their runners refuse while any `*.lock` is in that folder and hold `qa-cafeteria.lock` while they run, and
this one refuses while theirs is held. A game running while no lock is held is someone else's (the owner, another agent
driving ME2 by hand): leave it alone and wait.

```powershell
tools\le2-halcyon-run\run-lock.ps1 -Status
tools\le2-halcyon-run\prep.ps1 -Holder <session name> [-Tag what] [-Diagnostics]   # once per stretch: backups, loader, deploy, fake headset
tools\le2-halcyon-run\launch.ps1 -Holder <session name> [-Kill]                                           # each run: Steam's -game 2, waits for the XR session
tools\le2-halcyon-run\load.ps1 [-Save <career>\<file>.pcsav]                         # a save's gameplay by script (default: Freedom's Progress)
# ... the run: logs in ME2\Binaries\Win64\Logs\LE2MEVR.log and %LOCALAPPDATA%\MELE2VR\MELE2VR_Log.txt
tools\le2-halcyon-run\launch.ps1 -Holder <session name> -Kill                                             # the next run: closes ME2 and starts it again
tools\le2-halcyon-run\launch.ps1 -Holder <session name> -Stop                                             # only closes ME2 (to rebuild, then prep.ps1 again to redeploy)
tools\le2-halcyon-run\restore.ps1 -Holder <session name>                          # once at the end of the task
```

- **Backups once per task, not per run** (the owner, 2026-11-14): the first `prep.ps1` backs up the profile's saves and
  settings, ME2's `BioGame\Config` and `Binaries\Win64` with hash manifests into `%LOCALAPPDATA%\MEVR\sessions\<stamp>-<tag>`;
  later preps by the same holder only redeploy. `restore.ps1` copies changed files back, moves files the runs added aside
  into the session folder (never deletes them), checks every file hash for hash, and only then releases the lock.
- **Saves by script, no menus** (the owner, 2026-11-14: no computer use for driving the game): `load.ps1` loads the VR
  Shepard career's Save 4 (Freedom's Progress) by default, or `-Save <career folder>\<file>` (or a full path), from the
  title screen or in game, and waits for gameplay (another world, a pawn, game mode 0). It asks the mod's console for
  `mevr load <career> <save name> <index> <type>`: the engine's own load of that save's descriptor
  (`SFXEngine.QueueSaveGameCommand(SaveGame_Load)`, what ME2's Load screen and QuickLoad call; research/
  le2-engine-access-discovery.md "A save loaded by script"). It reads the save and never writes one; the save file's hash
  is checked before and after, and the game's own autosaves after a load are put back by `restore.ps1`. `mevr state`
  (the world, the pawn, the game mode) and `mevr savedesc` (the save the game would resume) are the console's other ME2
  lines. Needs a development build (the console request).
- `-Diagnostics` turns `MELE2VR.ini`'s `[VR] Diagnostics` on for the stretch (Halcyon's `[ME2FP]`, `[ME2DISC]` lines; F3
  held 0.3 s dumps the camera graph).
- Never run anything that takes the game slot (`tools/game-slot.ps1`) while a stretch is open, and never `return` a
  slot without asking the owner.

## ME3 development dump only

The same runner accepts `-Game LE3 -DesktopDump` on **all three commands**. LE2 is still the default;
LE3 without `-DesktopDump` (including `-DesktopDump:$false`) and LE2 with it are refused. This is a protected
native data capture, not ME3 VR support or a save-loading/fixture test. `load.ps1` remains ME2-only.

```powershell
tools\le2-halcyon-run\prep.ps1 -Holder <session name> -Game LE3 -DesktopDump -Tag le3-dump -BuildDir <development build>
tools\le2-halcyon-run\launch.ps1 -Holder <session name> -Game LE3 -DesktopDump
# Capture/review the private desktop-dump.json receipt. Stop before restoring.
tools\le2-halcyon-run\launch.ps1 -Holder <session name> -Game LE3 -DesktopDump -Stop
tools\le2-halcyon-run\restore.ps1 -Holder <session name> -Game LE3 -DesktopDump
```

- Desktop dump mode requires `MEVR_HOT_RELOAD=ON` and a built `Release/LE3MEVR.asi` containing the dump-only
  gate marker. Prep writes `[Debug] dumpOnly = 1`; launch refuses changed configuration or an older host.
  The host returns immediately after its existing code-dump attempt, before hooks and renderer initialization.
  This mode never invokes `fake-headset.ps1`, `vr-game-settings.ps1`, or SteamVR startup/maturity checks.
- Both modes back up the complete Legendary Edition profile plus the **selected game's** `BioGame/Config`
  and `Binaries/Win64`. Every robocopy exit code, file count, source hash and copied hash must agree before
  current-session metadata or loader/deployment writes. Reparse roots/children are refused. Keep the lock
  after a failed backup or deployment until the state is inspected; a partial backup is not a restoration receipt.
- Sessions use `le2-current.json` or `le3-current.json` under `%LOCALAPPDATA%/MEVR/sessions`, with explicit
  `game` and boolean `desktopDump`. Redeployment requires the same holder/game/mode and original install/profile.
  An open stretch for the other game is refused even with the same holder (profile backups would overlap).
  Old metadata without those fields means LE2 VR only and remains restorable. A legacy running process without
  a recorded identity is intentionally **not** adopted by `-Stop` or `-Kill`; close it manually, then restore.
- `launch.ps1` now requires `-Holder`, including `-Stop`/`-Kill`. It verifies session and global lock before acting,
  adopts only the selected executable and launcher started after this request, and records PID, executable path,
  and start time. Stop checks all three values again. Another game or an unowned/reused process is refused.
  Audited repository callers were README examples only; no other PowerShell/Python launch caller needed updating.
- Steam is asked for `-applaunch 1328670 -game 3 -autoterminate`. Success requires the fresh host-log marker
  `dump-only: code dump attempted; renderer and hooks disabled`, then a dump binary and metadata whose PE
  timestamp, `.text` RVA, virtual size and binary length agree with the **current installed executable**.
  The private receipt records executable/host/dump/metadata SHA-256 hashes, PID, dump age and `freshDump`.
  The existing dumper may keep a cleaner old dump: `freshDump=false` is explicit, not fresh capture success.
  Header and length checks do not establish byte provenance, decrypted-code correctness, reflection support,
  gameplay or VR acceptance. No save is loaded by this route. Keep dumps and receipts private.
- Restore validates original backup hashes before writes, restores changed/missing files, moves additions aside,
  checks every original hash/count and only then releases the lock. A second restoration attempt preserves any
  prior aside file under a unique suffix. LE3 restoration does not invoke fake-headset cleanup.

### ME3 engine cross-check

Add `-EngineProbe` to **each** LE3 desktop command (prep, launch, stop and restore) to
run the development reflection cross-check instead of stopping after code capture.
Prep writes only `[Debug] engineProbe = 1`. The host skips the renderer; the mod runs
the shared reflection checks on the game thread with gameplay modules disabled.
The runner requires a fresh `reflect-check.json` with consistent counts and no
mismatches, and copies the report into the protected session. This mode does not load
a save or establish gameplay/VR acceptance. Its report and dump receipts remain private.

```powershell
tools\le2-halcyon-run\prep.ps1 -Holder <session name> -Game LE3 -DesktopDump -EngineProbe -BuildDir <development build>
tools\le2-halcyon-run\launch.ps1 -Holder <session name> -Game LE3 -DesktopDump -EngineProbe
tools\le2-halcyon-run\launch.ps1 -Holder <session name> -Game LE3 -DesktopDump -EngineProbe -Stop
tools\le2-halcyon-run\restore.ps1 -Holder <session name> -Game LE3 -DesktopDump -EngineProbe
```

### Offline checks

Synthetic files only; these checks never launch or deploy the game.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\le2-halcyon-run\test-session.ps1
```

These cover mode/holder/game refusals, legacy metadata, path boundaries, PID reuse, failed/altered/empty backups,
wrong-build and truncated dump metadata, and rejecting hosts without the dump-only gate. They do not replace the
first protected native launch and hash-verified restoration acceptance.
