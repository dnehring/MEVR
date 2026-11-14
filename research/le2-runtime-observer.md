<!-- SPDX-License-Identifier: MIT -->
# LE2 runtime observer: source audit and next implementation package

Status: offline source audit and archive inspection only. **No LE2 runtime observer
is implemented or proved by this work.** No game, SteamVR, debugger, shared request
file, deployment or live profile was accessed. ME1 testing remains owned by the
other session. The audited source baseline is commit
`e4a0e485ee7a994a85615ab3021ecf310e0bff34`; later source changes require rechecking
the interfaces below. This is P9 observer preparation, with P1 engine access and
P4/P7/P8 semantic dependencies from the [port plan](../docs/le2-port-implementation-plan.md).

## What the existing machinery actually provides

| System and source | Verified source behavior | LE2 reuse boundary |
|---|---|---|
| [CMakeLists.txt](../CMakeLists.txt), `mevr_add_plugin`, `MEVR_HALCYON_HOST_SOURCES` | `MEVR_GAME_LE1/LE2/LE3` is defined per target. Halcyon host sources, including TestLoop, PerfProbe, HalcyonBridge and GameThreadCalls, are appended only for `MEVR_HALCYON && game == LE1`. `MEVR_BUILD_LE2` defaults off. | Building an LE2 ASI does **not** provide the LE1 snapshot/record/performance subsystem. Pure helpers can be reused, but host integration must be built explicitly. |
| [plugin/Entry.cpp](../src/plugin/Entry.cpp), `GameEngineTickHook` | All games note the game-thread ID, call the original engine tick, run `host::Tick`, and report off-thread ProcessEvents. Halcyon tick/performance/queue draining is conditional on `MEVR_WITH_HALCYON`. | A post-engine-tick observation point exists without an XR frame. It is a candidate seam, not a verified LE2 state sampler. |
| [engine/GameAdapter.hpp](../src/engine/GameAdapter.hpp), `ReflectionLayout` and `ReflectionGlobalsForGame` | Both return `nullopt` outside LE1. [engine/Legacy.cpp](../src/engine/Legacy.cpp) uses LExSDKv2 for non-LE1 world/player/function access. | Existing `ue3::FieldRef` modules cannot be assumed to read LE2. A bounded LE2 SDK adapter or separately verified reflection layout is required. Do not transplant LE1 offsets. |
| [plugin/TestLoop.hpp](../src/plugin/TestLoop.hpp), [TestLoop.cpp](../src/plugin/TestLoop.cpp), `MetaJson`, `OnFrameBegun` | Captures XR swapchain images and layer composition, eye poses/FOVs, frame/display/Unix times, focus, controller sample, replay metadata and many mod/host diagnostics. GPU copies occur on Present, mapping later, PNG writing on a worker. | Useful image/capture design; no flat-game producer. The existing source producer is LE1-only. Format alone does not attest that any particular archived file came from LE1 or LE2. |
| [mod/Mod.cpp](../src/mod/Mod.cpp), `PublishStatus` | Returns unless host owns VR and `statusWanted`/`setStatus` are available. Publishes build date, mode, arm/aim/weapon pose status, support/holster/UI/movement diagnostics. TestLoop uses a recent cached status (under one second), not a synchronized post-action gameplay sample. | No usable native-baseline API with XR off. `mod.weapon.type/key/status` is not current loaded/spare ammo, reload commit count, plot outcome, probe/fuel or mineral state. |
| [game/Weapon.cpp](../src/game/Weapon.cpp), `TryStartup`, `Tick` | Weapon reflection and pose inspection are guarded for LE1; non-LE1 startup returns false and wanted tick reports `LE1 only`. [ConversationFlow.cpp](../src/game/ConversationFlow.cpp) and [GalaxyMap.cpp](../src/game/GalaxyMap.cpp) use LE1 reflection-era classes and semantics. | Those status names are not evidence of sequel implementations. Thermal clips, timed morality interrupts and mining require their own verified observers. LE1 conversation interruption/range is not the LE2 timed response window. |
| [core/vr/PoseRecording.hpp](../src/core/vr/PoseRecording.hpp), [PoseRecording.cpp](../src/core/vr/PoseRecording.cpp), [tools/mevrrec.py](../tools/mevrrec.py) | Engine-independent v1 binary recording: 32-byte header, 252-byte frames, predicted-display relative time, eye views, hand pose/button/axis samples. Reader/CSV exporter already exists. | Reuse decoder and format concepts offline. Header contains no game/PID/run/fixture/build/module hash. These are sampled controller states, not a timestamped trace of game-consumed actions. Truncated final frames can be dropped and must be reported. |
| [plugin/HalcyonBridge.cpp](../src/plugin/HalcyonBridge.cpp), `OnFrameBegun`, pad mapping | Input sync occurs before TestLoop. TestLoop records live sample or applies replay; calibration/menu capture and manual correction occur before published controller state. The game's returned pad is mapped/merged later with latches and context suppression. | Raw recorded trigger/button activity does not prove a reload/interrupt/probe action reached the game. Record raw source, effective mapping, consumed event and authoritative outcome as separate stages. |
| [game/GamepadBridge.cpp](../src/game/GamepadBridge.cpp), `Bridge` | For the non-Halcyon path, wraps real XInput, merges active VR state into pad 0, rotates movement, updates packets. XInput can be called on any thread. Only aggregate call/real-pad/controller-frame diagnostics exist. | A candidate place for a bounded raw/effective pad trace. It does not cover keyboard/mouse or establish native action dispatch. Do not run UE object reads or ProcessEvent in this hook. |
| [plugin/ProcessEventHook.cpp](../src/plugin/ProcessEventHook.cpp), [engine/ProcessEvent.cpp](../src/engine/ProcessEvent.cpp), [engine/CallFunction.cpp](../src/engine/CallFunction.cpp) | Host hook calls original before callback and records breadcrumbs/off-thread calls. Engine observer slots are single callbacks, alongside existing handlers. | Reuse ownership/lifetime conventions; do not replace the existing callback/observer or hook stack casually. A returned ProcessEvent is not necessarily a successful action. Action-specific pre/post state and normal return outcome still need verification. |
| [game/CallTrace.cpp](../src/game/CallTrace.cpp) | Diagnostic name-filtered trace, capped at 600 lines, with suppression counts; resolves names through our runtime reflection and installs engine observer callbacks. | Not a durable LE2 input/event recorder, and not a complete event count after suppression. LE2 reflection dependency remains. |
| [plugin/GameThreadCalls.cpp](../src/plugin/GameThreadCalls.cpp) | Bounded copied-parameter queue with alive/class checks; drained at engine tick in Halcyon builds. It depends on the reflection layout/globals that are currently LE1-only. | Reuse the design only after an LE2 ownership/lifetime adapter exists. Observer should read existing state, not call native operations to manufacture it. No UnrealScript on Present/worker/XInput threads. |
| [plugin/PerfProbe.cpp](../src/plugin/PerfProbe.cpp), `WriteCapture` | Halcyon/XR frame and engine-tick breakdown into frames/ticks/calls CSV plus probe JSON under `MEVR/perf`. | Not an LE2/native producer today; cannot infer real headset/transport performance from fake-headset measurements. |

## Paths that must not be used during the active ME1 run

[tools/common.ps1](../tools/common.ps1) sets one shared
`%LOCALAPPDATA%/MEVR` directory. `Write-MevrRequest` overwrites a named request
through a temporary file; it has no game ID, target PID, run ID or lease token.
`tools/snapshot.ps1`, `record.ps1`, `replay.ps1`, `console.ps1` use that helper.
`snapshot.ps1 -Spectator` also sends a console command that changes spectator
state. Changing a script's `GameRoot` does not isolate these requests.

| Shared path | Consumer and consequence |
|---|---|
| `snapshot.request`, `record.request`, `replay.request`, `perf.request`, `eyediff.request` | `TestLoop.cpp::PollRequests`; LE1 host can consume/delete them. Replay can replace poses and, with `inputs=1`, inputs. A request queued during another game may affect the next eligible LE1 run. |
| `console.request`, `console.result` | `game/ConsoleRequest.cpp::Tick`, called from `Mod.cpp` when developer channels are allowed. The generic console route is compiled for LE2/LE3 too; special `mevr ...` handlers are inside the LE1 guard. An LE2-intended file can be consumed by ME1, execute commands, and replace the result. |
| `testloop.status`, `snapshots/`, `recordings/`, `perf/` | Shared status/output, not a run-discovery protocol. TestLoop can prune old diagnostics under configured disk budgets. Timestamp or newest-file selection does not establish ownership. Archive explicit files before relying on them. |
| `%LOCALAPPDATA%/MELEVR/MELEVR_Log.txt` | Halcyon LE1 renderer log; not an LE2 observer feed. |

[core/DevChannels.hpp](../src/core/DevChannels.hpp) enables request channels by
default in development builds and disables them in monolithic builds, with a
`[Debug] requests` override. Do not change that setting or assume the live
session ignores requests. This package neither calls those scripts nor reads
their live outputs. Do not clear another session's requests or release its lock.

## Existing identity evidence and its limits

`Entry.cpp::SpiOnAttach` logs game name, project version, compiled commit and PID;
`ExeGate` checks the supported game version and address table. These are useful
startup diagnostics, not binary-content hashes or a proof of successful loading.
[core/Log.cpp](../src/core/Log.cpp) prefers
`Game/ME2/Binaries/Win64/Logs/LE2MEVR.log`, falling back to
`%LOCALAPPDATA%/MEVR/LE2MEVR.log`; the previous log is rotated. Log line clocks are
local time-of-day, so a complete capture needs a dated UTC/QPC anchor.

[plugin/ModLoader.cpp](../src/plugin/ModLoader.cpp) can load a shadow copy of
`LE2MEVR_mod.dll`; its generation log identifies the source path, while the loaded
module is the shadow DLL. Hashing only the current build output/source file is
insufficient after hot reload. The future observer must identify the actual
mapped host and mod generation and preserve dirty-build provenance. Monolithic
builds have a different identity shape and must be explicit.

`session.py` already binds exact game executable path/hash, PID and process start,
freezes scenario/recipe/prepared-save/receipt, and verifies restoration. The
auditor now requires the session binding. Extend those identities; do not create
another unrelated run ID. Legacy snapshots and recordings cannot acquire that
provenance merely by attaching their bytes to an LE2 run afterward.

## Safe offline archive inspector delivered here

[tools/qa-le2/observe.py](../tools/qa-le2/observe.py) accepts only explicitly named
archived snapshots/recordings and their expected SHA256. It never searches for
the newest capture, opens a process or writes a request. It rejects symlink/reparse
ancestors, known live MEVR/MELEVR folders and known game/profile trees. Outputs
must be new `.json` files in an existing private archive directory.

```text
python tools/qa-le2/observe.py snapshot PRIVATE_CAPTURE.json --sha256 CAPTURE_SHA256 --output NEW_INSPECTION.json
python tools/qa-le2/observe.py recording PRIVATE_RECORDING.mevrrec --sha256 RECORDING_SHA256 --output NEW_INSPECTION.json
python tools/qa-le2/test-observe.py
```

Snapshot inspection records hashes and header dimensions for explicit sibling
PNG files, missing images, eye metadata, producer errors and declared replay.
It does not decode/review pixels, authenticate the camera or prove stereo.
Recording inspection reuses `tools/mevrrec.py` after strict v1 layout, finite
sample and increasing-time checks, records decoder hash, frame counts/duration
and trailing partial bytes. Its flags do not prove physical hardware/human input.

Every result has `source: offline-recording-inspection`, unknown game attribution,
`acceptance: not-evaluated`, and false runtime/input eligibility. It deliberately
does not emit scenario `identity`, `state`, `inputPath` or `events`. It names the
missing LE2 gameplay/identity fields. It cannot satisfy `check-start` or the
runtime/input artifact gates. It may be retained as a reviewed diagnostic log.
Current tests are synthetic archive tests, not tests against a live capture.

## Next bounded package: LE2 native observer v1

Implement separately after the current game owner hands off. No runtime C++ or
shared ABI change is part of this audit. Proposed files are a small LE2 adapter
under `src/game/` and a pure record/queue/serializer under `src/core/`; final names
and host integration belong to P0/P1 owners. The package has five requirements:

1. **Read-only native operation, independent of XR.** Sample after the original
   engine tick, with valid local player/world and verified game-thread ownership.
   Publish `unavailable` plus a reason when a field/object is absent or the world
   is transitioning; never substitute zero or a value from the preparation recipe.
   Prove observation with no headset/OpenXR session. The current MEVR mod startup
   also attempts scene/input/gameplay hooks (`Mod.cpp::TryInstallScriptHooks`), so
   merely turning the headset off is not a proven inert observer configuration.
   Use an explicitly reviewed observation-only startup path or standalone bounded
   observer build; compare ordinary native behavior against an uninstrumented run.

2. **Session-scoped transport.** Session runner stages an opt-in configuration
   containing expected LE2 exe hash, PID/start after binding, run/case IDs, random
   nonce, expiry and explicit output directory. Proposed request location is
   `RUN/observer/LE2/<pid>-<start-token>/requests/<sequence>.json`; acknowledgment
   and outputs stay under that same private run. Bootstrap must itself be per-game
   and ownership checked, never the existing shared `console.request`. Reject
   another PID/game/run, stale sequence, mismatched nonce, expired lease, traversal
   and an old game's leftovers. Supported requests are only start/mark/stop
   observation, with no arbitrary console commands or mutation operation.

3. **Immutable identity and timeline.** Start header binds the existing scenario
   identity, executable path/hash/version, PID/start, actual host/mod module
   paths/hashes/generation, observer source/build and settings/DLC inventory.
   Each record carries run ID, sequence, world/load generation, game tick,
   monotonic QPC time/frequency and an anchored UTC time. XR display time is an
   optional additional clock, not the only clock. Track source and effective input
   clocks separately; document sampling latency and dropped events. Freeze record
   batches outside game-thread hot paths. Do not wait for workers or inspect UE
   objects during process teardown; preserve partial/truncated output as incomplete.

4. **Separate input from outcome.** Bound and preallocate an input/event queue;
   hooks on arbitrary threads copy primitive facts only. Game-thread sampling
   resolves verified identities/state. Tag source `native-keyboard-mouse`,
   `native-gamepad`, `vr-controller`, `replay`, or `diagnostic`; retain raw action,
   effective post-context mapping, game dispatch and outcome. Do not equate an
   XInput poll or repeated held button with a discrete gameplay commit. Native
   keyboard/mouse input is not covered by GamepadBridge and needs a verified game
   input seam or timestamped manual capture. Overflow/suppression/gaps keep
   event-count assertions pending. Existing observer slots need coordinated
   multiplexing or a non-conflicting seam, not silent replacement.

5. **Capture and honest normalization.** A flat-game video/screenshot producer
   must record process/window identity and capture time without activating XR.
   The current TestLoop cannot provide it. Keep capture frame/time links to game
   observations and manual annotations. Normalize only verified fields into
   scenario runtime/input artifacts, with raw hashes, producer versions, source
   field paths, availability and sampling clock. Require continuous selected-save
   and loading-scene evidence plus reviewer attribution. Save inspection remains
   `save-inspection`; capture inspection remains `offline-recording-inspection`.

## Initial semantic field contract

These are proposed normalized names from the scenario catalog, **not verified
LE2 memory offsets**. Pin native class/field/function identities and semantics
against the actual game build and ordinary before/after behavior before emitting
them. Keep optional or unresolved values absent with an explicit capability entry.

| Scope | Needed observations | Proof before implementation is accepted |
|---|---|---|
| Common start | `state.map`, world/load generation, selected career/slot evidence, local player identity/class, relevant resources/plot preconditions, current input/camera context | Correct normal Load Game entry, rendered scene and starting state; changing maps/loading clears stale references. No map-open/teleport as replacement. |
| Reload | Stable weapon instance/class, weapon switch epoch, `state.weapon.loaded`, `reserve`, native magazine capacity, float used-count and special-weapon rule; reload phase/early flag, request/accept/transfer/commit/cancel; firing/pickups during window; tutorial context; no-reserve refusal | Observe legal empty/partial reload, then interrupt/switch/no-reserve. Compare HUD/video and authoritative runtime counts. Count actual transfers, not input polls. Cancellation can perform an early reload; a cancelled animation alone must not imply zero ammo transfer. Save/load conversion and clamping require separate observations. |
| Dialogue interrupt | Conversation/entry identity, Paragon/Renegade prompt kind, window open/close timestamps, actual submitted response, accepted/missed result, branch continuation, input context before/after | Capture real prompt and timed accepted/missed/early input, then ordinary save/reload progression. No arbitrary plot index; verify conditions/package dependencies. LE1 flow/range diagnostics are not this window. |
| Planet scanning | Galaxy/system/planet identity, scanning UI mode, scan held/start/stop, probe launch request and actual single launch, `state.resources.probes`, `fuel`, mineral values, scan yield/cost, depleted/discovered/anomaly state | Same planet and configuration before/after ordinary scanning; zero/one/surplus probes, exact actual costs, no duplicate event, normal mission entry where relevant, save/reload comparison. Do not infer discovery from mineral gain alone. |

The [ammo source report](le2-ammo-fixture-source.md) traces `SFXSaveGame`
SaveWeapons/LoadWeapons: saved `ammo_total` maps to spare reserve,
`ammo_used_count` truncates a runtime float, and displayed loaded rounds derive
from integer conversion of magazine capacity minus used count. It also identifies
`Reloading.CancelReload` performing `DoReload` when `bEarlyReload` is set before
clearing timers. Record phase/early flag, capacity/class, float used count,
reserve, transfer events and input timestamps; do not encode "cancel means no
transfer" as a universal assertion. These are source-derived semantics; scoped
native proof is still pending. Native resource getters and interrupt/scanning
function candidates likewise need package/SDK research; this audit intentionally
invents no addresses or plot identifiers.

## Package acceptance and handoff

- Offline: serialization/duplicate-key/nonfinite/unknown-version refusal, bounded
  queue overflow and sequence gaps, process/run mismatch, expiry/path isolation,
  load generation reset, missing field handling, and clock correlation tests.
- Build: LE1 behavior unchanged; LE2 observer compiles with its explicit verified
  adapter and can remain disabled. Compilation is not native validation.
- First owned native experiment: one naturally acquired save, normal native load,
  read-only start record and scene, one legal reload, normal save/reload, protected
  restoration. Verify no XR initialization, synthesized input, mutation requests,
  off-thread script calls or unrelated hook behavior. Record process/module hashes.
- Repeat a timed interrupt and one-probe/zero-probe scan with their verified
  semantic adapters. Keep unimplemented cases pending; do not broaden this package
  into the full VR port. Only then integrate normalized records with
  `scenario.py audit --session RUN`. VR routing, both eyes, physical comfort,
  performance and campaign acceptance remain separate gates.

Until that native slot exists, usable work is source research, pure serializers,
fixture provenance and explicit archived-file inspection. Nothing in this plan
authorizes sending requests to the ongoing ME1 session.
