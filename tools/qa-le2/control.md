<!-- SPDX-License-Identifier: MIT -->
# ME2 run-bound control channel

This client and its C++ codec provide the transport for the
[scripted testing slice](../../docs/le2-scripted-testing.md). They do not install
a runtime producer, enable a renderer, select a save or prove a native operation.
The runtime consumer is a separate diagnostic-host integration. Until it publishes
an authenticated-by-session `control/identity.json`, commands fail without writing
the existing shared `console.request` file. The protocol is coordination between
cooperating local tools, not a security boundary against another process owned by
the same user.

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
