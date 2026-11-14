<!-- SPDX-License-Identifier: MIT -->
# ME2 scripted testing handoff

Use scripts and the mod's request channel for repeatable game tests. Computer
use is a fallback for launcher interaction or recovery when no working channel
exists. Do not build a UI-clicking scenario runner.

Read the repository skills before a native session:

- [fake-headset-run](../.claude/skills/fake-headset-run/SKILL.md): ownership,
  backups, game-thread requests, recordings, cleanup and build identity.
- [first-person-scene-audit](../.claude/skills/first-person-scene-audit/SKILL.md):
  measurements, expected failures and capture review for conversations/walks.

## What is reusable, and what is not working in ME2 yet

`tools/console.ps1` and `game/ConsoleRequest.cpp` implement the existing file
transport. The generic native console route already compiles for LE2/LE3 through
`engine::legacy::LocalPlayerController`, `FindFunction` and `ConsoleCommand`.
Validate and extend this path; do not assume the channel must be written again.
Its special `mevr ...` command handlers are inside the LE1 compile guard.

The inspected `fake-headset-session.ps1` deploys LE1 from `build-mono`, backs up
ME1 saves/LE1 settings, and invokes an LE1 launcher/readiness path. It is the
workflow reference, not an ME2 runner. Changing one game-name parameter would
not port its save selection, readiness, restoration or commands. Pose recordings,
snapshot and timing formats are reusable; their LE2 producers and acceptance
remain unproven. See the [observer audit](../research/le2-runtime-observer.md).

The November 13 native baseline had an empty LE2 ASI directory. No mod was
deployed, so it could not consume request files. Normal UI loading proved Save
99 selectable and created Save 100; this does not validate the request channel.

## Next implementation slice: P1 and P9

1. Finish P1a's validated, read-only LE2 player/world observation and game-thread
   lifetime checks. Verify the built and loaded host/mod hashes before accepting
   any response. Keep XR and gameplay modifications out of this first trial.
2. Audit the existing legacy console dispatcher and its tick ownership against
   the [engine discovery](../research/le2-engine-access-discovery.md) and
   [integration contract](le2-integration-contract.md). Prove a harmless native
   query before enabling load/save/quit. Report actual outcomes; dispatch alone
   is not success. Do not use LE1 reflection offsets or generated parameter
   defaults without LE2 validation.
3. Bind requests/results to the protected session, game/PID/start identity,
   executable/module hashes, request ID and deadline. Prefer a per-run directory
   over the unqualified shared `MEVR/console.request`. Reject stale or wrong-game
   requests and ambiguous results; do not auto-retry a mutating request. Preserve
   LE1 compatibility and never leave a command for the next game's startup.
4. Add a bounded ME2 runner around `tools/qa-le2/session.py`: protected staging,
   normal Steam/EA launch, exact process binding, channel handshake, verified
   save selection, observation, normal native save/reload and quit, then archive
   and verified restoration in cleanup. Reuse the launch helper's outside-job
   approach after an explicit LE2 adaptation; do not start the game EXE from the
   harness. Do not invoke the LE1 wrapper unchanged.
5. Reuse snapshot/record/replay/performance producers as the relevant ME2 host
   integration becomes available. Capture raw input, effective game input and
   authoritative outcomes separately. Add reload/interrupt/scanning cases only
   after the minimal scripted load/save/reload cycle is demonstrated.

Use the real desktop user's process inventory and shared lock directory. The
restricted shell in the permission retry redirected `LOCALAPPDATA` into a
sandbox package and could miss desktop processes. An empty inventory in that
context is not proof the game is closed. Run native lifecycle checks under the
approved host context; keep offline parsing/build work in the workspace.

One native owner at a time. Keep complete protected backups and archive new
saves. Never `streamlevelin`, never use an old `game-slot return` rollback for
cleanup, and do not switch the system OpenXR runtime for routine tests. Fake
headset results cannot establish physical comfort or real-headset stutter.

## Acceptance and handoff

Require an owned LE2-only handshake, a deliberately rejected stale/wrong-game
request, a harmless query, normal load/save/reload, useful captures and verified
cleanup after both success and interruption. Preserve private original evidence.
Until these pass, report the bridge and runner as unproven. The current native
baseline establishes loading and saving only; reload remains pending.

## Implemented transport checkpoint

The [launch adapter](../tools/qa-le2/launch.md) now supports read-only planning and
one guarded Steam forwarding attempt when installed game-selection support is
reviewed. Its 18 offline tests passed; native automated selection is not yet
proven. It binds a process, not a loaded save.

The [control client and codec](../tools/qa-le2/control.md) now bind traffic to the
owned run, process start, module hashes and a fresh runtime lease. Standalone C++
checks and 18 Python tests passed, including cross-language wire conformance and
retained uncertainty after dispatch errors. The separate attachment helper has
13 passing tests using actual temporary protected sessions with fake process and
module providers. It requires an already loaded reviewed diagnostic ASI and
publishes an invitation; it does not deploy or certify a responder.

These changes reuse the protected session and Steam forwarding machinery. They
are a control connection for the existing host, not a replacement ME2 renderer.
The next native gate remains a real game-thread handshake and validated state,
followed by the normal load/save/reload cycle. Three scenario and VR acceptance
gates remain open.
