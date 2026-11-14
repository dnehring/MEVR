<!-- SPDX-License-Identifier: MIT -->
# Protected ME2 Steam launch adapter

`launch.ps1` is read-only by default. It examines an already prepared
`session.py` run, describes the exact forwarding command, and reports dependencies
as JSON. It does not prepare a profile, install a loader, start Steam, choose a
save, send keys, create a scheduled task, issue mod requests or declare gameplay
ready. Implementation is original code in `launch_support.py`, using the existing
session integrity, ownership, host-context and process-binding APIs.

```powershell
tools/qa-le2/launch.ps1 -Plan -RunDirectory C:\private\le2-run `
  -SteamExe 'C:\Program Files (x86)\Steam\steam.exe'
```

Supply a reviewed `-CapabilityReceipt C:\private\launcher-review.json` to inspect
automated game selection eligibility. `-Plan` prints executable, argument array,
hashes, expected process/save, host-inventory status and dependency codes without
changing files or creating a Python bytecode cache. The default without either
switch is also Plan. No native-provider override is exposed on the CLI.

Plan returns exit 0 when it produces a report, including a blocked report;
callers must inspect `canLaunch` and `dependencies`. Invalid argument syntax is
nonzero. An unknown/sandboxed desktop or unreadable process identity is a blocker,
never an empty process inventory. File inspection still works in that context.
Large protected backups are hashed, so preflight duration depends on their size.

## What is actually supported

The reviewed existing route in `tools/launch-game.ps1 -Steam` forwards
`-applaunch 1328670` through a newly invoked Steam client to the already-running
Steam outside the harness job. That existing Steam owns the subsequent Steam/EA
launch chain. This adapter uses the same forwarding mechanism with the argument
array `-applaunch 1328670 -game 2 -autoterminate`. It never directly starts the
game, launcher or EA executable. The forwarding client is hidden.

Game selection is a separate dependency. Local read-only source review of
[LEBinkProxy at 1c435d8df0367642d9fc8572b67b6ae92295967e](https://github.com/ME3Tweaks/LEBinkProxy/tree/1c435d8df0367642d9fc8572b67b6ae92295967e)
found `src/modules/launcher_args.h::parseCmdLine_` accepting game numbers 1–3,
with `gameToPath_` and `gameToWorkingDir_` mapping 2 to ME2. Its Activate path
constructs the ME2 arguments and starts the selected game. No upstream code was
copied. Its lack of a repository license precludes importing it under our MIT
file license.

In contrast, our current `src/loader/Loader.cpp::BootFromLauncher` explicitly
returns to the picker for games other than 1. A file named `bink2w64.dll`, its
version string, or this source review cannot authenticate an installed binary's
ME2 selection behavior. Steam/EA can also drop forwarded arguments. Therefore
routine automated launch requires a private, reviewed installed-binary capability receipt.
No receipt was generated and no installed binary was approved by these tests.

Without that dependency, the report contains `operator_selection_required`.
Use the existing session `request-load` boundary and select ME2 through the normal
launcher, then `session.py bind-process` and Load Game. This adapter does not
send Space/Enter to an ambiguous launcher or assume the last-played game/save.

### First empirical selection trial

When installed support has not yet been observed, use `-ProbeSelection -Plan`
to inspect a bounded first trial, then `-ProbeSelection -Launch` for the authorized
protected run. This is mutually exclusive with `-CapabilityReceipt`. It records
the installed launcher/proxy hashes without asserting that they support the
command, forwards one `-game 2` request through Steam, and observes the result.
All ownership, backup, host-context, process identity and interruption checks
remain in force. Changed installed files before dispatch refuse the trial.

The probe never changes launcher files, sends fallback keys or retries. An exact
new ME2 process records `selectionObserved: true`; a picker, timeout, wrong game
or failure remains unresolved and requires normal recovery. `ready` stays false.
Preserve the attempt report and native logs, then review that evidence for a
future `observed-game2-selection` receipt. The probe does not create a receipt
or automatically promote its result to reusable capability evidence.

## Capability receipt

This is an **attributed review**, not an automatically discovered capability.
The reviewer must inspect evidence connecting the installed files to either a
source-matched build supporting the command or an actual observed game-2
selection. A boolean assertion, stock Bink binary, our ME1-only loader, or source
pin without binary provenance is insufficient. Keep the receipt and its evidence
outside the repository, unchanged for the lifetime of the run.

Required JSON fields:

| Field | Meaning |
|---|---|
| `schemaVersion` | `1` |
| `kind` | `reviewed-le2-launcher-selection` |
| `provider` | `LEBinkProxy` |
| `sourcePin` | `1c435d8df0367642d9fc8572b67b6ae92295967e` |
| `arguments` | `["-game", "2", "-autoterminate"]` |
| `method` | `source-matched-build` or `observed-game2-selection` |
| `reviewedBy`, `reviewNotes` | Nonempty attribution and explanation of the binary-to-behavior evidence |
| `launcherExeSha256` | Exact installed `Game/Launcher/MassEffectLauncher.exe` hash |
| `proxySha256` | Exact installed `Game/Launcher/bink2w64.dll` hash |
| `evidence` | Nonempty array of `{ "path": "relative-artifact", "sha256": "..." }` under the receipt directory |

The adapter checks the schema, installed hashes and evidence bytes, including
path containment and reparse rejection. It cannot judge the truth of review
notes or reproduce a binary build from a hash. A reviewer remains responsible
for the attribution; the result explicitly reports
`nativeSelectionVerifiedByThisTool: false`. Do not manufacture a receipt merely
to remove a dependency. Native selection remains unproven for this new adapter.

## Authorized launch and outcomes

After explicit native handoff, a protected prepared session, completed dependency
review and the real desktop host checks, use the same command with `-Launch`
instead of `-Plan`. A single already-running Steam must match `-SteamExe`, expose
its exact PID/start/path and be outside all job objects. Preflight refuses any
existing game, launcher or VR process, other QA locks, or game-slot ownership.
No bypass is provided. `session.py`'s new `require_native_context` API is required;
an older session module fails closed.

The adapter verifies frozen plan/artifacts, staged save, executable, complete
backup manifests and shared ownership; records `launch-attempt.json` exclusively;
records the session's load request; rechecks ownership and identities; then sends
one Steam request. It waits at most `-TimeoutSeconds` (1–180, default 120) after
dispatch for the exact ME2 process. The wait rejects wrong games/installations,
old processes, changed Steam ownership and ambiguous ME2 processes. Launcher
presence is permitted during the wait only at this installation's expected path.

Successful binding returns `process-bound-operator-load-required`, `processBound:
true` and exit 0. **`ready` remains false.** The operator still selects the staged
save through Load Game; runtime handshake, loaded module verification, expected
scene and all scenario gates are separate. The tool does not promote native or
VR acceptance.

Failure, timeout or missing dependency returns exit 2 with structured detail.
The adapter never kills a game, retries a mutating request, or restores profiles
while a game might be live. The attempt marker is retained even after interruption
before a known outcome; a subsequent launch is blocked. Inspect the actual
desktop/session state and preserve the attempt before explicit recovery. After
the owned game closes, use `session.py reset` to archive new saves and verify
restoration. Create a fresh protected run for a later launch attempt. Never use
an old game-slot rollback for cleanup.

## Offline verification

```powershell
python -B tools/qa-le2/test-launch.py
```

Tests use temporary fake installations and injected Python providers. They cover
no-write plan behavior (including the PowerShell entry point), stale/wrong
identities, missing/changed launcher evidence, conflicting owners, Steam job
membership, exact process binding, timeout and interruption recovery markers.
They also cover a first selection probe, unchanged ownership checks, pre-dispatch
binary changes and separation from reviewed capability receipts.
No test dispatches Steam, schedules a task, writes a live profile or issues a mod
request. These tests establish adapter behavior only, not a successful ME2 launch.
