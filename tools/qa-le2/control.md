<!-- SPDX-License-Identifier: MIT -->
# ME2 run-bound control channel

This client and its C++ codec provide the transport for the
[scripted testing slice](../../docs/le2-scripted-testing.md). They do not install
a runtime producer, enable a renderer, select a save or prove a native operation.
The runtime consumer is implemented in `engine/Le2ControlHost.cpp` behind the
opt-in monolithic diagnostic build. Until it publishes
an authenticated-by-session `control/identity.json`, commands fail without writing
the existing shared `console.request` file. The protocol is coordination between
cooperating local tools, not a security boundary against another process owned by
the same user.

## Building the diagnostic host

Run in a clean isolated worktree of the selected commit, with the repository's
pinned submodules available:

```powershell
cmake -S . -B local-tools/le2-diagnostic-build -A x64 -DMEVR_BUILD_LE1=OFF -DMEVR_BUILD_LE2=ON -DMEVR_BUILD_LE3=OFF -DMEVR_HOT_RELOAD=OFF -DMEVR_LE2_DIAGNOSTIC=ON -DMEVR_HALCYON=OFF -DMEVR_MAIN_MENU=OFF
cmake --build local-tools/le2-diagnostic-build --config Release --target MEVR-LE2 mevr-le2-control-host-tests mevr-le2-diagnostic-tests
ctest --test-dir local-tools/le2-diagnostic-build -C Release -R "mevr-le2-(control-host|control-producer-smoke|diagnostic)$" --output-on-failure
```

This produces `Release/LE2MEVR.asi` inside the build directory. Record its SHA256
and embedded commit/version; deploy that exact file only after protected session
preparation. The configuration rejects mixed games and hot reload. It retains
the existing host/engine tick but skips renderer entry, gameplay polling and
the unqualified console channel. It is a development diagnostic, not the ME2 VR
renderer or a distributable sequel release; LE2 still uses the SDK submodule.

`ping` reports the actual host tick/thread and capabilities without engine-memory
reads. `observe` additionally returns the [structural observer](../../research/le2-diagnostic-observation.md)
under `payload.observation`. Authoritative `observeWorld` and `nativeConsole`
capabilities stay false; `observeStructuralWorld` identifies this diagnostic
sample. It does not establish a loaded save or permit gameplay actions.

The host currently hashes the executable and module synchronously around each
request. Keep these setup queries outside performance windows. Admission checks
the current UTC time after file validation, so slow IO cannot admit an expired
request; an admitted request may publish a late result for `collect` to reconcile.

## Protected native sequence

1. Coordinate the game slot and run `session.py prepare PLAN NEW_RUN`. The plan
   should protect the complete profile, LE2 configuration and Win64, plus the
   launcher directory if it may write logs/configuration during the run. A failed
   preflight is not permission to close another game's process.
2. Inspect LE2's actual Bink/ASI loader before deploying. Stock RAD Bink cannot
   load the ASI; a loader installed in the trilogy launcher does not install one
   in ME2. The successful diagnostic reused our clean-built `MEVR-Loader`, with
   stock Bink preserved as `bink2w64_original.dll`, entirely within the backed-up
   Win64 directory. Record and verify both loader and original hashes. Then
   deploy the hash-verified diagnostic ASI inside LE2 Win64/ASI. Preserve the
   build receipt and prove actual mapping; file presence is insufficient.
   Do not run the LE1 deploy/session
   wrapper unchanged or modify OpenXR settings for this diagnostic.
3. Use [launch.ps1](launch.md) with a reviewed capability receipt, or its explicit
   one-attempt selection probe. It records and binds the exact new ME2 process.
   If automatic selection fails, retain the attempt and use the documented
   operator boundary; never retry an uncertain launch blindly.
4. Publish the attachment invitation below, then issue `ping` and `observe`.
   Preserve response bytes, actual loaded module identity and the host log.
   No response means no handshake proof, regardless of an ASI being present.
5. Normal save selection, native save/reload, screenshots and scenario actions
   remain operator/dependency boundaries until the corresponding game-thread
   controls are validated. A title-screen handshake is not a fixture load.
6. Close only the owned game through its normal exit path, verify it is closed,
   run `session.py reset NEW_RUN`, then run attachment `cleanup`. Preserve new
   saves and test artifacts in the run archive. If the game/ownership cannot be
   resolved, retain backups and report the exact blocker instead of restoring
   files into a live process.

## Using a connected diagnostic host

After protected preparation, launch, exact process binding and runtime attachment:

```powershell
python tools/qa-le2/control.py C:\private\le2-run ping
python tools/qa-le2/control.py C:\private\le2-run observe
python tools/qa-le2/control.py C:\private\le2-run collect
```

For a reviewed, already mapped monolithic diagnostic ASI, publish one invitation:

```powershell
python tools/qa-le2/control-attach.py C:\private\le2-run invite --host C:\game\ASI\LE2MEVR.asi --sha256 REVIEWED_BUILD_SHA256
```

The path/hash are placeholders for the actual controlled build. This command
checks the frozen session and mapped module, creates `control/attach-intent.json`,
then exclusively publishes `MEVR/le2-control.attach.json` under the verified
desktop user's local application-data directory. Its success means only
`invitation-published-handshake-pending`; the host must independently validate it
and publish its identity before `ping` can succeed. Do not fabricate `identity.json`.
The first producer supports a monolithic host/mod only: both identity hashes and
paths name that one ASI. A missing consumer times out/fails without enabling one.

After the owned game closes and `session.py reset` reports verified restoration:

```powershell
python tools/qa-le2/control-attach.py C:\private\le2-run cleanup
```

Cleanup requires a quiet desktop and a restored session. It removes only an
invitation byte-identical to this run's preserved intent. It retains another
owner's invitation, all run-local evidence and any unresolved command history.

`collect` reconciles a late result; it does not resend the request. Both ends must
explicitly permit native console commands. The initial runtime integration keeps
them disabled. `--allow-console` only opts the client in; it cannot enable the
host. Never use `streamlevelin`: both the client and the C++ dispatch gate reject
it. A console result marked `dispatched` proves only that dispatch returned, not
that a save loaded, ammunition changed or gameplay completed.

### Live rejection checks

Once a normal ping succeeds, use the bounded negative-control helper:

```powershell
python tools/qa-le2/control-negative.py C:\private\le2-run wrong-lease
python tools/qa-le2/control-negative.py C:\private\le2-run expired
python tools/qa-le2/control.py C:\private\le2-run ping
```

Both probes send only an empty `ping`: one with a deliberately different lease,
one with an expired deadline. Success requires the exact rejection reason, the
producer's actual session identity, correlated request fields and consumption of
the request. A reply that echoes the invalid lease or reports completion fails.
These checks establish admission behavior, not gameplay or native-call safety.

The helper shares the ordinary client lock, sequence and pending-intent files.
Unresolved traffic blocks both clients. After timeout, use
`control-negative.py C:\private\le2-run collect`; it inspects the archived
request and late response without resending. Unexpected responses retain their
bytes and pending intent. Do not delete those files to retry. The normal client's
`collect` refuses a negative-control intent so it cannot mistake an unexpected
completion for a successful rejection. Run the final normal ping only after both
negative controls finish successfully. Live results must be recorded separately
from the temporary-files/C++ endpoint regression checks.

The client checks the owned session lock, frozen plan, actual process PID/start/
path, executable hash, and mapped host/mod paths and file hashes before sending
and accepting a response. Windows process-start identity preserves all seven
fractional digits as an integer FILETIME. Hashes describe the files at mapped
paths, not a hash of relocated process memory. Controlled deployment and the
runtime's independent identity checks remain required.

## Producer contract

`control/identity.json` contains `identity` with exactly these string fields:
`run`, `lease`, `pid`, `start`, `exe`, `host`, `mod`, `game`; `game` is `LE2`.
It also contains absolute `hostPath` and `modPath` naming the actual loaded
modules. The runtime derives its own process/module identity and checks the
protected run's ownership. It must not merely echo identity supplied in a request.
Each new gate/process generation uses a new lease; reusing a lease after losing
its replay history is forbidden. Register the consumer before the first host tick
and run it on that game thread. Do not call the legacy unqualified request poller.

Wire files use `MEVR-CONTROL/1\n`, then ordered `key=value\n` headers:

```text
kind,run,lease,pid,start,exe,host,mod,id,sequence,deadline,verb,status,bytes,game
```

One blank line separates the headers from exactly `bytes` payload bytes. UUIDs
and SHA256 strings are lowercase; numbers are canonical unsigned decimal strings.
`deadline` is UTC Unix milliseconds, bounded to 30 seconds by the gate. Payloads
are at most 16 KiB without NUL; a console command is at most 4 KiB without ASCII
control characters. Unknown, duplicated, reordered, partial or trailing data is
rejected. C++ and Python implementations have executable byte-conformance tests.

Publish `request.control` and `result.control` atomically without replacement.
The runtime consumes the complete request before executing, calls `Gate::Claim`
before dispatch, and never releases a claimed sequence for retry. It checks run
ownership before every operation. Responses retain all correlation fields and
use the runtime's verified identity. Status meanings:

- `completed`: a read-only ping/observation completed; not gameplay acceptance.
- `dispatched`: native dispatch returned; inspect authoritative game state next.
- `rejected`: authoritative rejection before dispatch; no native effect occurred.
- `error`: execution outcome uncertain. Preserve pending intent and block new
  commands until an authoritative terminal response can reconcile it.

The client archives intent and responses under `control/history`, keeps an
exclusive `client.lock`, and retains `outstanding.json` after timeout, mismatched
identity or error. Error evidence uses a separate `.error.result` file so later
reconciliation cannot erase it. An interrupted client lock or unresolved outcome
requires explicit recovery; do not delete it and resend a command. A new protected
run can be created after the owned game closes and restoration is verified.

## Offline verification

```powershell
cmake -S tests/le2-control -B local-tools/le2-control-build -A x64
cmake --build local-tools/le2-control-build --config Release
ctest --test-dir local-tools/le2-control-build -C Release --output-on-failure
$env:LE2_CONTROL_CPP = (Resolve-Path local-tools/le2-control-build/Release/le2-control-tests.exe).Path
python -B tools/qa-le2/test-control.py
python -B tools/qa-le2/test-control-attach.py
```

The tests use temporary files and fake peers. They verify exact identity,
malformed frames, replay rejection, timeouts, error recovery, mapped-file checks
and cross-language encoding. They neither launch ME2 nor prove game-thread calls,
scene loading, visuals or VR controls.
