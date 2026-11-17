<!-- SPDX-License-Identifier: MIT -->
# Protected ME2 runs and ME3 candidate runs

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
tools\le2-halcyon-run\load.ps1 -Holder <session name> [-Save <career>\<file>.pcsav]   # default LE2: Freedom's Progress
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
- **The mod's first-run settings are waited out at launch** (since 2026-11-17; gotcha
  [mod-settings-appended-after-xr-ready](../../docs/knowledge/gotchas/mod-settings-appended-after-xr-ready.md)): every
  prep leaves only `[Debug] openxrRuntime` in `ASI\LE2MEVR.ini`, and the mod appends its default blocks on its first tick,
  about 2 s after the XR marker at the title (a block the busy file refused about a second later). `load.ps1` hashes that
  file with the save and the binaries before its first request, so a load started at once was refused as `Save or runtime
  artifact changed during load` although the save had loaded. After the XR marker `launch.ps1` now waits for this
  launch's `hands: calibration pitch` and `colour: profile` lines in the mod's log, then for 3 s without a write to the
  file (attributes only: it never opens the file, so it can't make an append fail), at most 90 s, and prints
  `mod settings settled: LE2MEVR.ini unchanged for ...` (a timeout only warns `mod settings did not settle ...`); both go
  to the session's `mod-settings.txt`, the settled file to `effective-mod.ini`. Load at once after `launch.ps1`: no
  waiting at the title. `load.ps1` accepts nothing new: any change to a recorded artifact during a load, an appended block
  included, still refuses it. ME3's renderer preview has waited the same way since 2026-11-15
  (`Wait-PreviewInitializedConfig`, stricter: every known block, nothing unknown) and needed nothing.
- `-Diagnostics` turns `MELE2VR.ini`'s `[VR] Diagnostics` on for the stretch (Halcyon's `[ME2FP]`, `[ME2DISC]` lines; F3
  held 0.3 s dumps the camera graph).
- Never run anything that takes the game slot (`tools/game-slot.ps1`) while a stretch is open, and never `return` a
  slot without asking the owner.
- **A launch shows two processes, and the first one ends** (since 2026-11-16; gotcha
  [ea-steam-launcher-starts-the-launcher-suspended](../../docs/knowledge/gotchas/ea-steam-launcher-starts-the-launcher-suspended.md)):
  Steam hands `-applaunch 1328670` to EA's `EASteamLauncher.exe`, which creates `MassEffectLauncher.exe` **suspended**
  and resumes it 0.2-14 s later; the launcher starts the game and exits. A suspended process has no readable path, which
  the old one-shot check refused (`Live process identity unavailable or changed; not adopted (pid=... path='' ...)`), and
  the launcher's exit looked like a first game process. `launch.ps1` now follows every `MassEffectLauncher` and game
  process over time (`Step-RunAdoption` in `session-common.ps1`): one whose identity can't be read yet is waited for (at
  most 60 s), one that exits is dropped, the launcher is recorded once its identity reads, and the game is adopted only
  as the single live one whose path and start time have stayed readable and unchanged for 5 s. A wrong path, a start
  before the request, a PID whose identity changes or comes back after exiting, a process another open session records
  and two live game processes at once are still refused. Every candidate (with its parent process) and every verdict is
  printed and appended to the session's `launch-candidates.txt`. The recorded `gameProcess`/`launcherProcess` are as
  before, so `-Stop`, `-Kill`, `load.ps1` and `restore.ps1` are unchanged; the by-hand workaround (console `mevr load`,
  `Stop-Process`) is no longer needed.

## ME3 development dump only

The same runner accepts `-Game LE3 -DesktopDump` on **all three commands**. LE2 is still the default;
LE3 without `-DesktopDump` (including `-DesktopDump:$false`) and LE2 with it are refused. This is a protected
native data capture, not ME3 VR support or a save-loading/fixture test. LE3 normal loading requires the separately
enabled engine-probe/request mode described below; a dump-only session cannot issue load requests.

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
  prior aside file under a unique suffix. LE3 desktop/probe restoration does not invoke fake-headset cleanup.

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

These cover mode/holder/game refusals, legacy metadata, path boundaries, PID reuse, launch adoption over time (a
suspended launcher, a candidate that exits before its successor settles, unreadable then readable identities, two live
candidates, earlier or foreign processes), ME2's wait for the mod's settings (stale markers, a late append restarting
the quiet time under an exclusive writer, a file that never stands still, lost ownership), failed/altered/empty backups,
wrong-build and truncated dump metadata, and rejecting hosts without the dump-only gate. They do not replace the
first protected native launch and hash-verified restoration acceptance.

## Owned normal-save loading

`load.ps1` reuses `console.ps1`; it never launches a game, deploys a build, changes VR settings or edits a save.
Every request (including polling) rereads the protected session and global lock, requires the explicit holder,
compares the session metadata hash, and checks the only running game's PID, exact executable path and start time.
The selected install and backed-up profile must also match. A pending request, unrelated reply echo, stale reply,
changed process, changed session or another game causes refusal. `-Holder` is now required for LE2 too; the
repository call-site audit found no executable PS/Python callers to migrate, only these invocation examples.
LE2 remains the default game and retains its existing Save 4 discovery.

For an already prepared and launched **LE3 engine-request session**:

```powershell
tools\le2-halcyon-run\load.ps1 -Game LE3 -Holder <owned holder> -Save <career>\Save_0031.pcsav
```

Prepare that session with `-Game LE3 -DesktopDump -EngineProbe -EngineRequests` on prep, launch, stop and restore.
`-EngineRequests` without `-EngineProbe` is refused. The native reflection check must pass before the mod consumes
the existing console channel; gameplay modules and the VR renderer remain disabled. Prep preserves prior request
and result files before clearing the channel; restore preserves this run's files and restores the originals.
Always stop the owned process and restore in `finally`, including after a failed load.

LE3 requires an explicit save and actual JSON `true` values for `desktopDump`, `engineProbe` and `engineRequests`
in the session. These flags are established by the coordinator's `prep.ps1`/`launch.ps1` engine-request mode;
this load helper does not enable them. Manual/quick/auto/chapter descriptors (types 0..3) are accepted.
LE3 also accepts `LegendSave.pcsav` as its native Legend descriptor (type 5, index -1);
LE2 continues to reject it. This reuses the same queue/load and acceptance gates without renaming or editing the file.
The audited native Legend request failed acceptance: the resulting resume descriptor reported unnamed Auto,
not the requested Legend descriptor. This remains a diagnostic candidate, not a verified load shortcut;
the exact descriptor gate is intentionally unchanged. See `research/le3-save-foundation.md`.
The file must be directly under a career in that game's save root. Reparse points, other-game files, malformed
names, import/export/NG+ saves and command-unsafe career tokens are refused.

A queued acknowledgement is not completion. LE3 must report world/pawn identities, an observed transition
(including a transient absence then recovery), the exact requested current descriptor and on-foot gameplay
which remains stable through a second state query. This permits a same-map reload with changed object identity.
If all observed identities remain unchanged, acceptance times out even if the descriptor already matches.
LE2's old state format remains accepted and requires a changed world name; a future identity-bearing LE2 state
would enable the same stricter same-map check without changing the parser.

Each attempt writes a private `load-<stamp>-<id>/receipt.json` below the protected session. It records session
metadata hash, game/process, executable/host/config and optional hot-reload module hashes, selected save hash,
expected/observed descriptors, before/after states and timestamped request replies. Save and runtime artifact
hashes must still match at the end. The receipt proves only normal-load acceptance for that exact artifact;
combat suitability, resource-variant persistence, VR rendering and physical gestures remain separate gates.
Session restore still owns any autosave/settings changes caused by the game.

Offline checks: `powershell -NoProfile -File tools/le2-halcyon-run/test-load.ps1`. They cover types, state parsing,
identity transitions, gameplay refusals, exact descriptor/response matching, strict mode booleans and process
ownership, and the artifact freeze after launch's settings wait (an appended block, an edited key, a same-length
rewrite or another artifact changed during the load each refuse it). They do not call the request channel or touch a game/profile.


## Explicit ME3 renderer preview

`-RendererPreview` is a separate, opt-in candidate mode. LE3 without an explicit mode
still refuses; it cannot be combined with `-DesktopDump`, `-EngineProbe` or
`-EngineRequests`. LE2 defaults and the LE3 desktop/probe route are unchanged.
Every prep/launch/stop/restore and preview load must supply the same flag and holder;
metadata uses actual JSON booleans, not truthy strings. Default LE3 `load.ps1` still
requires the engine-request session.

Start only in an assigned exclusive game window, with all games, launchers and
SteamVR processes stopped and no existing fake-headset undo record or enabled null
driver. The runner refuses to stop/reconfigure someone else's existing VR session.
This is one extension of the existing runner, using its global lock, verified
profile/config/Win64 backups, PID/path/start-time ownership, console, and save-load
proof flow. It does not use computer automation.

```powershell
$holder = 'owned-me3-preview'
# The finally block attempts recovery even if prep or launch fails after publishing a session.
try {
    tools\le2-halcyon-run\prep.ps1 -Holder $holder -Game LE3 -RendererPreview -BuildDir <candidate build>
    tools\le2-halcyon-run\launch.ps1 -Holder $holder -Game LE3 -RendererPreview
    tools\le2-halcyon-run\load.ps1 -Holder $holder -Game LE3 -RendererPreview -Save <career>\Save_0031.pcsav
    # Existing tools: console.ps1, snapshot.ps1, record.ps1, replay.ps1, perf-probe.ps1.
    # Only issue a replay with Inputs intentionally after inspecting its source.
} finally {
    # If prep failed before opening a session, inspect the lock/backup state instead.
    # Do not release an unresolved lock or stop a process that ownership checks reject.
    tools\le2-halcyon-run\launch.ps1 -Holder $holder -Game LE3 -RendererPreview -Stop
    tools\le2-halcyon-run\restore.ps1 -Holder $holder -Game LE3 -RendererPreview
}
```

Prep writes a small `ASI/LE3MEVR.ini` with `[Debug] requests=1` and a process-only
SteamVR OpenXR manifest override. It writes the actual renderer file,
`Binaries/Win64/MELE3VR.ini`; the current deploy script only installs a renderer INI
for LE2. The candidate selects source mode 4 (SFR2), the lowest resolution shown by
ME3's menu (3072x1728), head look, head aim off, native positional scale 2.0 and
first-person conversations off. `vr-game-settings.ps1` supplies matching dimensions,
SDR and borderless/non-fullscreen settings. No nonexistent `[FirstPerson] Enabled`
key is written: first-person activation is an independent existing renderer control.
For a bounded renderer bisection, prep accepts `-PreviewRendererMode 0` (mono), `2`
(alternate-eye stereo) or the default `4` (same-frame stereo), and optional
`-Diagnostics`. The session records these typed options; launch verifies the exact
corresponding config and artifact hashes. Restore before selecting another variant.
Launch checks the renderer config and binary hashes again after XR readiness. The
mod may append its known first-run settings sections; its original Debug/Pointer
prefix cannot change, duplicate or unknown appended sections/keys are refused, and
the effective mod ini is saved in the session receipt. Legacy mode
`1` is refused: upstream unconditionally migrates it to `4`, so an INI requesting
side-by-side mode does not establish which renderer actually ran.
Mono is diagnostic evidence only and cannot satisfy stereo acceptance. Desktop dump
mode rejects these renderer options.
`-PreviewMirrorEvery 1` updates the desktop mirror every frame; the default `8`
retains the imported renderer's throttle. Values `2` and `4` are also explicit
comparisons. The option is recorded and checked before and after startup. Compare
engine frame timings separately from visible monitor updates: full-rate mirroring
can reintroduce desktop-compositor back pressure and does not prove headset pacing.
The preview enables the existing `[Debug] dumpNatives=1` diagnostic once per
session. Its local native-function CSV supplies names to the existing Ghidra
tools; this is research data, never a published artifact or gameplay acceptance.
It is part of the exact prepared configuration and is restored with the other
settings. No extra game launch is needed just to obtain the symbol map.

The preview also sets `[Pointer] setCursor=0`, as the fake-headset skill requires for
synthetic controller replays, so the recorded pointer cannot move the desktop cursor.
Source: `src/halcyon/le3/me2_menu.cpp` `LoadValues`/resolution rows and
`d3d_capture.cpp` `RenderW`/`RenderH` startup handling. These are candidate settings,
not a measured optimal resolution or accepted metric calibration.

Before fake-headset setup, preview preserves the exact current bytes or absence of
all four files that `fake-headset.ps1` changes: null-driver defaults, SteamVR settings,
chaperone data and the helper's LE1MEVR.ini. It uses a fresh per-session
`-BackupDirectory`, preserving the global helper's unrelated backups. Restore first
validates the owned SteamVR processes and backup hashes, invokes the existing helper's
`-Off`, then restores the exact saved files/absence. This retains an original null
universe and original LE1 runtime key, which the standalone helper's cleanup would
otherwise remove. `preview-environment-restored.json` records the final hash result.
No registry OpenXR runtime is changed. An unexpected/replaced VR server or helper
keeps the session owned for inspection; it is not permission to kill that process.

The console backup primitive also preserves and quarantines recognized pending
`snapshot`, `record`, `replay`, `perf`, `eyediff`, `mainmenu`, `voice` and `setup`
`.request` files before prep/each launch. The initial request/result bytes and missing
files are restored after the game is stopped; newer files are retained in the
session's aside directory. In particular, an old replay-with-inputs cannot silently
start with this preview. These original pending requests become eligible again in
a future session after restoration; do not launch an unrelated test over them.

The real LE3 renderer log is `%LOCALAPPDATA%/MELEVR/ME3PassiveDxgiProbe.log`, with an
exe-directory fallback of the same name (`src/halcyon/le3/logger.cpp`). Readiness
requires a newly appended/fresh `[ME3XR] VR session ready; starting frame submission.`
line from this launch (`me2_xr.cpp`); an existing line is not accepted. Launch defaults
remain bounded to 240 seconds for the process and 80 seconds for readiness; the
optional `-LaunchTimeoutSeconds`/`-ReadyTimeoutSeconds` can shorten them. A failure
retains ownership and a receipt for stop/restore.

Renderer-preview launch then waits up to 30 seconds for compiled settings initialization.
Fresh `hands: calibration pitch` and `colour: profile` markers must appear in the
same host log (the game `Logs/LE3MEVR.log`, or its `%LOCALAPPDATA%/MEVR` fallback).
The effective ini must remain unchanged for 1.25 seconds and contain all 21 known ([Reload] since 2026-11-16),
populated default sections. Suffix parsing waits for stable text so a readable
half-written append is not mistaken for corruption. Every poll still verifies
process/lock ownership, the exact protected prefix, renderer ini and runtime binaries.
The receipt records the completion log and quiet interval. This startup allowance
ends before `load.ps1` freezes hashes: any settings or binary change during load
still rejects the run. An XR-ready marker or a console reply alone is insufficient;
console requests are consumed before settings refresh later in the same game tick.

`renderer-preview-launch.json` records launch/process identity, expected and current
exe/host/module/config hashes (including the actual `.modpath` target), readable
loaded-module paths/hashes or an explicit read error, readiness source/freshness,
and errors. `stereoAccepted` and `physicalHeadsetAccepted` remain false: XR startup
proves neither correct two-eye rendering nor a playable VR view. Preserve this
receipt and the existing snapshots/replays privately. The first protected preview
still needs native evidence of hook/frame progression, valid separate eye captures,
loaded-save world/controllers, first-person body/weapon alignment, stable projection,
menu ownership, and a zero-difference restoration. Physical headset comfort is a
later gate; the null headset cannot establish it or judge real headset stutter.

Offline: `test-session.ps1` now covers preview flag/type refusals, stale-channel
quarantine, exact environment restoration/corrupt backups, fake-owner/VR-process
refusal, PID reuse and helper ownership, candidate config/artifact identity and fresh
log detection. `test-load.ps1` covers explicit preview load mode. Both run under
Windows PowerShell 5 using synthetic files; no native launch is part of those tests.
