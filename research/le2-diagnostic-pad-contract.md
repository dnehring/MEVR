<!-- SPDX-License-Identifier: MIT -->
# Bounded LE2 diagnostic pad source

This pure helper is an offline candidate, disabled until a separately reviewed
diagnostic hook and host admission path use it. It installs no hooks, touches no
game objects, calls no game function, driver or OS input API, and changes no
settings. It is independent of VR controller state, grip logic and head yaw.

## Exact core contract

`src/core/qa/DiagnosticPad.hpp`, namespace `mevr::qa::diagnostic_pad`:

- `Source::BeginRun(nonzeroRunToken)` opens a host-owned run only when the previous
  run is closed and no release is pending. The immediately previous token cannot
  reopen; the host is responsible for issuing fresh tokens and mapping them to
  authenticated run identities. A numeric token is not wire authentication.
- `Publish(run, sequence, Pad, durationMs, nowMs)` accepts one nonneutral pulse,
  with strictly increasing nonzero sequence, duration 1–1000 ms and no deadline
  overflow. There is no queue, overwrite or implicit renewal. A tap is a short
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

Phases are `Idle`, `Published`, `Consumed`, `DrainPending`, `NeutralEmitted`.
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

## Small hook integration proposal — not implemented

Reuse the existing `game/GamepadBridge.cpp` loaded-module discovery, standard
`XInputGetState`/ordinal-100 boundary and owned hook/trampoline lifecycle. In an
explicit `MEVR_LE2_DIAGNOSTIC` route, obtain the original physical result first,
map only `ERROR_SUCCESS` to Connected and `ERROR_DEVICE_NOT_CONNECTED` to
Disconnected (all other errors remain Error), then consult this source. Do not
route diagnostic input through `controllers::Latest`, `FromControllers`, support
grip, head-yaw rotation, Halcyon menus or a renderer. Do not load a virtual device
driver or write gamepad/profile settings.

Passthrough preserves the original result and state bytes. Replacement writes
the returned pad/packet and returns success immediately at the same hook boundary.
Never enqueue, cache for later application or replay a PollResult. The source
cannot revoke an already-returned result in an in-flight callback: adapter
serialization/quiescence and caller provenance must be reviewed before claiming
release completion or unloading hook code. Keep the source/hook lifetime through
pending release and in-flight callbacks; process exit is a distinct termination.
The pure helper is not a complete shutdown protocol or proof of game consumption.

The root-owned host must bind opt-in, run identity, sequence, deadline admission,
current player/mode and control allowlist. Inspect actual mode/input bindings:
base configuration is not proof of current remapped controls. Physical conflict
and failed neutral drain remain explicit uncertain outcomes, not automatic retry.
Normal movement/use/fire/reload may then share this same bounded input boundary;
no generic console command, function name or pointer is accepted.

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
