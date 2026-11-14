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

## Current implementation and remaining P1/P9 work

1. Retain P1a's bounded structural observer and game-thread checks. Entry-menu,
   menu and post-load world/pawn samples now work. The source-backed live resource
   observer now matches all eight prepared values in one normal-load run; broader
   scenario assertions remain pending. Verify built and mapped module hashes
   for every run.
2. Reuse the validated scalar-query and normal menu routes through the existing
   engine/SDK integration. The scalar getter and two menu actions have returned
   normally in protected runs. Normal pause and Save-menu transitions also have
   live proof, including the persistent closed PowerWheel HUD context. General ConsoleCommand still lacks allocator and
   returned-string ownership proof; do not enable it from metadata alone.
3. Keep the implemented session-bound request/result channel: PID/start identity,
   executable/module hashes, fresh lease, request ID and deadline. Live wrong-lease
   and expired-request rejection passed. Preserve uncertain intent; never retry a
   mutating request automatically or leave traffic for the next game's startup.
4. Extend the protected runner's proven frozen-manual-fixture load and live
   resource observation to normal save/reload persistence. Reuse its Steam
   selection, process binding, initialized menu, request channel, captures and
   verified restoration. Compare actual observed values and resulting saves to
   fixture expectations; do not infer them from successful loading.
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
Until these pass, do not call the full runner proven. The native baseline
establishes loading and saving; later diagnostic runs prove read-only
handshake/observation, live wrong-lease/expired-request rejection, a bounded
scalar return, normal menu transitions and one bounded scripted frozen-fixture
load with post-load world/pawn and visible scene evidence, followed by all eight
edited resources matching live inventory and exported starting-state checks.
Subsequent runs also prove opening the normal pause and Save screens. The
explicit owned-foreground desktop capture fallback produces readable images
when PrintWindow returns black; see [capture usage](../tools/qa-le2/capture.md).
The corrected asynchronous Save-panel flow and separate UI-row classification
have now supported one normal New Save request. Its sole new file passed the
collector and independent TSE decoding with all eight edited resources intact.
Fresh normal reload of those exact bytes now preserves all eight values in the
live game and reaches the reviewed Normandy scene. The reload run closed and
restored all four protected roots. Earlier deferred cleanup used qualified
recovery to preserve later ME1/shared-profile changes; inspect its separate
receipt rather than rerunning the old whole-profile reset. The minimal native
resource cycle is proven; scenario/VR acceptance remains pending.

## Implemented transport checkpoint

The [launch adapter](../tools/qa-le2/launch.md) now supports read-only planning and
one guarded Steam forwarding attempt when installed game-selection support is
reviewed, or an explicit first selection probe to collect that evidence. Its
21 offline tests passed. Two protected native selection probes successfully
bound ME2 using the installed launcher files; they did not select a save.

The [control client and codec](../tools/qa-le2/control.md) now bind traffic to the
owned run, process start, module hashes and a fresh runtime lease. Standalone C++
checks and 31 Python tests passed for candidate `71f17c7d`, including
cross-language wire conformance and retained uncertainty after dispatch errors. The separate attachment helper has
15 passing tests using actual temporary protected sessions with fake process and
module providers. It requires an already loaded reviewed diagnostic ASI and
publishes an invitation; it does not deploy or certify a responder.

These changes reuse the protected session and Steam forwarding machinery. They
are a control connection for the existing host, not a replacement ME2 renderer.
The diagnostic responder and bounded structural observer are now compiled into
an opt-in monolithic LE2 candidate. See the [build and protected native sequence](../tools/qa-le2/control.md).
Actual Python-to-C++ filesystem integration passes offline and the clean
`59b5e775` diagnostic answered live `ping` and `observe` on the ME2 game thread.
The observer sampled the entry menu and matching ConsoleCommand metadata, not a
loaded fixture. The existing MEVR loader was required because the game's stock
Bink does not load ASIs. Both protected runs closed normally and restoration
verified. See [native evidence](../research/le2-validation-evidence.md).
The `152c9597` run passed live negative controls but exposed a controller-slot
mismatch. The later `eede67a1` run established same-controller passive forwarding
to the owned base hook and one bounded `GetFOVAngle` return; see the
[target investigation](../research/le2-native-console-reuse.md#observed-controller-target-disagreement).
This resolves that sampled scalar route, not arbitrary native calls.

Candidate `a0596b4f` then completed `advance-splash` and `open-load-menu`, each
once through the exact owned base route. Fresh observations and three reviewed
run-bound desktop captures established Splash, Main Menu and the populated Load
screen. The Load snapshot contained four complete descriptors and showed Save 99
highlighted in the capture. No `LoadGame` call or fixture load occurred; the run
closed normally and all four protected roots were restored and verified. See the
[menu run evidence](../research/le2-validation-evidence.md#normal-menu-transitions-and-rendered-desktop-evidence)
and [control/capture workflow](../tools/qa-le2/control.md#observe-and-open-the-normal-load-menu).

Candidate `71f17c7d` then proved one bounded frozen manual-fixture load. A
reviewed plan mapped the staged file/hash to its complete descriptor; a current
four-row scan uniquely matched UI row 0, and one guarded normal `LoadGame` call
returned. Fresh observation found `Biop_Nor` and the player soldier pawn; the
coordinating agent's image review confirmed Shepard on the Normandy crew deck.
Two pre-load captures were nearly black despite machine `captured` status and
were rejected as visual proof. The prior readable menu capture supported the
mapping attestation; current file/descriptor checks and the post-load evidence
supported this run. Normal close and all four restored roots were verified.
See the [fixture-load evidence](../research/le2-validation-evidence.md#frozen-manual-fixture-loaded-through-the-normal-menu).

For subsequent runs, preserve the frozen mapping and derive the unique current
UI row from the complete descriptor, never from descriptor Index alone. A
nonempty list or `CurrentCareerIndex=-1` does not establish career-picker versus
save-list phase: `-1` is valid and no verified persistent phase flag is exposed.
Revalidate the initialized handler, mode and current descriptor before loading;
there is still no career-picker automation. Candidate `cf764fc7` subsequently
loaded a resource-only edited copy and observed all eight expected values in the
live player inventory. Its correlated exporter passed the frozen starting-state
checks; current menu and loaded-scene images were readable, and restoration
verified. See the [resource run evidence](../research/le2-validation-evidence.md#eight-edited-resources-observed-after-normal-loading).
The next gate is normal save/reload, followed by gameplay scenarios and VR.
Reuse the established engine/SDK and protected runner; this evidence does not
prove persistence or general native-console safety.

The [native console reuse audit](../research/le2-native-console-reuse.md) records
the remaining general-call target and allocator/string-lifetime prerequisites.
Continue to follow the [engine discovery](../research/le2-engine-access-discovery.md)
and [integration contract](le2-integration-contract.md). Preserve the existing
LE2-specific allocator route; do not enable console dispatch merely because the
observed parameter layout matches the SDK.
