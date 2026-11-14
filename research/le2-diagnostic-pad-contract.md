<!-- SPDX-License-Identifier: MIT -->
# Bounded LE2 diagnostic pad source

This pure helper is integrated with the existing `GamepadBridge` and the owned
LE2 control host. Native delivery remains unproven; only an explicitly admitted
`pad-pulse` enables it. The helper itself installs no hooks, touches no
game objects, calls no game function, driver or OS input API, and changes no
settings. It is independent of VR controller state, grip logic and head yaw.

## Exact core contract

`src/core/qa/DiagnosticPad.hpp`, namespace `mevr::qa::diagnostic_pad`:

- `Source::BeginRun(nonzeroRunToken)` opens a host-owned run only when the previous
  run is closed and no release is pending. The immediately previous token cannot
  reopen; the host is responsible for issuing fresh tokens and mapping them to
  authenticated run identities. A numeric token is not wire authentication.
- `Publish(run, sequence, Pad, durationMs, nowMs)` accepts one nonneutral pulse,
  with strictly increasing nonzero sequence, hold duration 1–1000 ms and no
  deadline overflow. Publication starts a fixed 5000 ms activation window; the
  hold starts on the first eligible nonconflicting poll. Before that poll,
  `expiresMs` is zero and `activationDeadlineMs` bounds waiting. A first poll at
  or after the activation deadline emits neutral with `StartTimedOut`, never a
  delayed press. There is no queue, overwrite or implicit renewal. A tap is a short
  button pulse; held input requires another explicitly admitted request after
  the preceding neutral response. Neutral requests and reserved button bits are
  refused. Sticks/triggers use fixed-width XInput ranges, so no float conversion
  or clipping occurs here. The host narrows the allowed controls by game context.
- `Poll(index, Physical, nowMs)` returns either passthrough or a complete synthetic
  pad/packet. Only index zero can inject. Physical read errors and **any** nonzero
  connected physical pad value cancel the pulse and pass through unchanged;
  even tiny stick drift is a conflict. Nothing silently merges or masks it.
- `CancelRun(run, nowMs)` closes the owned run and retains pending neutral output.
  Wrong-run cancellation has no effect. A new run cannot discard that release.
- `Snapshot()` reports the latest request identity, deadline, requested values,
  run-open state, terminal reason, pulse poll count/times, neutral response and
  exact last physical sample/conflict count. All shared primitive state is
  serialized by a mutex. No UObject pointer crosses into a polling thread.

The adapter supplies one monotonic millisecond clock, never wire-provided time.
At or after the deadline the source emits no further nonneutral response, even
if the game-thread host stopped ticking. Clock rollback terminates active input
and rejects regressing publication. Caller timestamps sampled before lock
acquisition can regress under contention; the conservative result is cancellation,
not continued movement. Unrepresentable deadlines are rejected before admission.

Phases are `Idle`, `Published` (waiting to start), `Consumed`, `DrainPending`, `NeutralEmitted`.
`Consumed` means a synthetic nonneutral response was produced for a poll. It does
**not** prove frame processing, pawn movement, interaction, scanning or VR input.
Multiple polls can occur in one frame. `NeutralEmitted` means exactly one safe
neutral poll response was produced, not that a gameplay release handler ran.
No request can replace a `DrainPending` request. A physical conflict or read error
can keep drain pending indefinitely without hiding the physical state.

Changed synthetic state receives a changed packet number, including the neutral
release; repeated identical responses retain their packet. Packet wrap is handled
without treating it as an error. After neutral emission the source returns to
ordinary passthrough. With a physically disconnected pad, the adapter must return
success for the synthetic neutral response before ordinary disconnection resumes.

## Existing bridge integration candidate

The diagnostic policy now lives in **existing** `game/GamepadBridge.cpp/.hpp`,
using its DLL list, Slot array, Hook/AlreadyHooked functions and detour templates.
No second XInput registry/provider was added. Ordinary builds retain their VR
mapping, yaw/grip policy and install/uninstall behavior. Only the conjunction
`MEVR_GAME_LE2 && MEVR_LE2_DIAGNOSTIC` exposes these APIs:

- `DiagnosticTryInstall(hook::Context&, bool explicitOptIn)` installs only exports
  from already-loaded DLLs. Root-owned startup calls it after an owned invitation
  opts in, not automatically at process initialization. Ordinary `TryInstall`
  cannot activate this diagnostic branch. No loaded DLL means unavailable and
  retryable; any discovered required-export/mapping/hook failure means unavailable,
  even if another target installed. Missing ordinal 100 is optional. Alias exports
  share the existing final-target slot. Installed mapped modules are pinned for
  process lifetime; no LoadLibrary, driver or settings changes occur.
- `DiagnosticBeginRun(token)`, `DiagnosticPublish(token,sequence,Pad,durationMs)`
  and `DiagnosticCancelRun(token)` wrap the pure source. The host supplies unique
  never-reused run tokens and performs engine/context/action admission first.
- `DiagnosticSnapshot()` returns source status plus mapped requested/owner module
  paths/bases/sizes, target, detour, original, backend, aliases and readiness. These
  are mapped-module facts, not signed-image/hash or full hook-chain verification.
- `DiagnosticDisable()` irreversibly closes admission and retains drain. Diagnostic
  `Uninstall` never removes an installed hook and reports failure when hook code
  must remain resident. This path is for the process-lifetime diagnostic host.

The existing Bridge calls original XInput first, then applies the diagnostic
policy **before** VR Latest/grip/yaw code. Only return addresses inside the cached
main executable .text range and currently mapped executable memory owned by that
module can consume a sample. Unknown callers, other pad slots and null buffers
pass through. Thread-local recursion suppression allows forwarded GetState/Ex
calls to reach their originals while only the outer eligible call consumes input.
Physical errors/nonneutral states retain their original result and state bytes.

The adapter mutex covers source calculation and the actual output-buffer write,
and serializes publication/cancellation with those writes. A generation captured
before the physical read is invalidated by accepted publication, run changes,
cancellation or disable. An original call returning late therefore cannot apply
new-run input after cancellation, drain and another admission. There is no queue
of PollResults. The callback's output write is the strongest claim: function return
and game interpretation can still occur later. `outputWrites`/`neutralWrites`
never claim game consumption or gameplay release. `callbacksInFlight` and
`staleCallbackPassthrough` expose that boundary. No hooks are removed to manufacture
quiescence; their code/source/trampolines must remain resident through process exit.

Clock samples are taken inside the same admission/output lock. Main-executable
callers are a bounded provenance gate, not proof that the game reads the pad on a
particular thread or acted on it. Later-loaded XInput DLLs require another opted-in
installation pass. Snapshot readiness covers the exports discovered so far.

Root owns wire/host/startup/CMake integration. This slice has **no native input or
installed-hook evidence**. `tests/le2-diagnostic-bridge` exercises the actual Bridge,
Hook and diagnostic API through stubbed physical exports/installation, including
recursive forwarding, alias deduplication, partial failures, physical conflict,
unknown callers, neutral drain and a stalled original across cancellation/new run.
65 checks passed Release/Debug; real Win32 diagnostic production code and ordinary
LE1/LE2/LE3 bridge objects compiled in both configurations with `/W4 /WX`.
These object checks do not replace integrated ordinary builds or live proof.

## Source evidence and initial controls

The existing LE1 host uses XInput interception in
`src/halcyon/le1/vr_menu.cpp` with `HalcyonBridge::MergeMotionControllers`.
The independent `game/GamepadBridge.cpp` supplies reusable hook mechanics, but
the current diagnostic tick skips its installation and its VR dependencies must
not be accidentally enabled. No existing hook was changed in this slice.

Installed LE2 source inspection found that `GameViewportClient.HandleInputKey/Axis`
and `Interaction.OnReceivedNativeInputKey/Axis` are delegates, not proved native
injection entry points. `UInput` exposes bindings and pressed keys but no reflected
native InputKey/Axis entry. Calling `PlayerInput` again or writing its axis fields
would bypass normal event timing and can duplicate time/sensitivity processing.

Base `Coalesced_INT.bin` SHA256
`a9adef3e0f18201dd8f841a6edecdf6ea2392c88dabed372284dc26220383869`
and its previously extracted private BIOInput support these candidate mappings:

| Context | Control | Normal route |
| --- | --- | --- |
| On foot | Left stick | aStrafe / aBaseY with configured dead zones |
| On foot | Right stick | aTurn / aLookUp with normal camera processing |
| On foot | A / Space | Used, then ordinary climb/cover fallback; hold/release also controls sprint |
| On foot | RT / LMB | FireWeapon, release StopFiringWeapon |
| On foot | X | Tap reload; hold weapon swap (binding timing must be retained) |
| Orbital | LT / RMB | StartScanning, release StopScanning |
| Orbital | RT / LMB | LaunchProbe |
| Galaxy/orbital | Stick axes | Ordinary galaxy GUI routing, reticle movement / planet rotation |

The native binding collector and map-specific UI remain runtime evidence gaps.
The PC galaxy handler can route ordinary input through game-owned Flash methods;
this is not permission for the adapter to invoke UI clicks directly. Observe
player position/selection, scene and orbital state to establish outcomes.

## Checks

Standalone `tests/le2-diagnostic-pad` builds with C++20 and `/W4 /WX` on MSVC.
Release and Debug each passed 107 synthetic checks, including all physical pad
components, exact expiry/late first poll, cancellation, new-run release blocking,
sequence replay, rollback/overflow, packet changes/wrap and concurrent
poll/snapshot/cancellation stress. No hooks or native input were executed.

```text
cmake -S tests/le2-diagnostic-pad -B local-tools/pad-tests
cmake --build local-tools/pad-tests --config Release
ctest --test-dir local-tools/pad-tests -C Release --output-on-failure
```
