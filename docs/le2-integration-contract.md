<!-- SPDX-License-Identifier: MIT -->
# LE2 first integration contract: engine, renderer and reload

P0 offline contract, 2026-11-13. This defines the first bounded integration order
for [P1-P4](le2-port-implementation-plan.md). It is not a new shared binary ABI,
a verified LE2 layout, or permission to change the running ME1 session. Existing
`ModApi.hpp`, host hooks and shared input routing remain unchanged in this batch.

## Owners and interfaces

| Boundary | Producer and responsibility | Consumer and responsibility |
|---|---|---|
| Engine observation | P1 validates game/build, world/player/weapon identities, field presence and game-thread reads. Publishes availability and monotonically changing object/context generations. | P3/P4/P9 reject absent, stale or unsupported observations. Never replace unavailable values with setup-recipe expectations. |
| Renderer and input | P2 owns the one XR session, frame timing, spaces, eye images and one controller action stream. Host remains responsible for input focus, loss and lifecycle. | P3/P4 consume immutable samples tagged with frame/time/space and validity. No second XR owner, second pad merger or competing Present hook. |
| Physical reload proposal | P4's pure coordinator consumes validated gesture edges, an explicit exclusive hand grant and the current supported weapon/context. | It emits one correlated request after valid insertion. It does not write ammo, call script functions, create native cover/weapon objects or arbitrate global hands itself. |
| Native request dispatch | P1/P4 adapter revalidates request identity, eligibility, active context and thread at execution, then follows the verified native entry route. | It reports rejection, admission and terminal reconciliation separately. A dispatched call or an animation ending is not proof of ammo transfer. |
| Evidence | P9 observes authoritative state/events and actual loaded module generation, with monotonic time and sequence/gap information. | Scenario auditing keeps native, VR, visual, headset and performance gates separate. Pure-coordinator tests cannot establish any native gate. |

The [engine audit](../research/le2-engine-access-discovery.md) and
[renderer audit](../research/le2-renderer-integration.md) name exact existing seams
and blockers. The [observer specification](../research/le2-runtime-observer.md)
defines run/process/module binding. These documents are required context for the
next runtime candidate.

## Thread, lifetime and unit rules

- Engine object traversal, native-field interpretation and any ProcessEvent or
  script call occur only on the verified game thread. Present/XInput/worker
  callbacks may copy bounded primitive samples, not execute game logic.
- The reload coordinator has one owner. Serialize `Step`, `CancelGesture` and
  `Observe` on that owner (the future game-thread adapter); the class supplies no
  concurrent-access synchronization. Drain copied feedback events there instead
  of calling it concurrently from input/render callbacks.
- Observe after the selected engine-tick boundary; event interception may need
  a scoped before/after pair. Verify ordering against game movement/aim writes.
  Rendering uses immutable published data. No worker waits on an engine pointer.
- Native object identity is stronger than an address. Bind validated world/load
  generation, object slot/serial where supported, class and current weapon
  generation. Reused addresses, hot reload and level changes invalidate caches.
  SDK declarations alone do not establish that a serial/liveness mechanism works.
- Controller positions are metres in a named XR space; engine positions retain
  verified game units. P2/P3 own one explicit conversion, handedness, axis and
  anchor transform. Tag samples with the transform generation. Do not assume an
  unlabelled vector is world-space or that one scene proves metric scale.
- Use a monotonic clock for durations and order, plus a wall-clock anchor for
  evidence correlation. Strict input/event serials are scoped to a producer
  epoch. Counter exhaustion fails closed; zero/old/duplicate identities are not
  valid new actions. Frame gaps and queue overflow remain visible.
- Missing capabilities disable only dependent features with a reason. Preserve
  the native control path until the replacement is proven. Do not guess an LE1
  offset or bypass a native gate to make a demo run.

## Reload prototype policy and native boundary

The initial pure coordinator models ejection, clip acquisition and insertion as
virtual interaction stages. The caller supplies the exclusive hand grant,
supported finite-reserve weapon eligibility, tracking/context validity and
coherent contact evidence. Clip identity is tied to the coordinator epoch,
weapon/context generation and grant; touching an old clip cannot reload another
weapon. Gesture/contact geometry and clip art are separate work.

For this prototype, valid insertion produces a **request to start the verified
native reload route**, not a call to `DoReload` and not an ammunition credit.
No native reload begins at physical ejection. This is a provisional integration
policy: alignment with native animation/timers and suppression of automatic or
button-driven competing reloads still need an explicit adapter and live proof.
It must not be presented as a finished Alyx-style interaction.

The existing native sequence includes tutorial notification before ammo transfer
and may transfer ammo when cancelled after its early threshold. Preserve those
semantics or document a deliberate validated gameplay difference. No coordinator
state change implies a refund, zero transfer or successful tutorial progression.
See [native reload findings](../research/le2-ammo-fixture-source.md).

The prototype lives in `src/core/vr/PhysicalReloadRequest.hpp`, namespace
`mevr::vr::reload`. `Coordinator::Step(Frame)` consumes coherent held signals and
derives edges; `CancelGesture()` explicitly drops/cancels only the local gesture;
`Observe(Feedback)` reconciles a matching native request. Cancellation invalidates
the clip and requires release before a new gesture. It neither invokes native
abort nor clears a pending request. The [prototype report](../research/le2-reload-core.md)
records the tested API and standalone build; it is not linked into production.

Only one request may be outstanding. Request identity includes the host epoch,
request serial and originating generations. Native acceptance retains the gate;
only authoritative terminal reconciliation releases it. Silence or elapsed time
never permits an automatic retry. Tracking loss, hand-lease loss, context change
or weapon switch invalidate the visible/held gesture but retain uncertainty over
an already emitted request. Feedback for an old identity can settle its matching
retained request; it cannot revive a clip or advance the new weapon's gesture.

Terminal reconciliation must say what was actually observed: rejection before
dispatch, native completion, or confirmed termination with any ammo transfer
accounted for. An abort is not assumed to be a no-op. A new coordinator lifetime
requires a new epoch and resolution of previous native requests; hot reload must
not erase outstanding uncertainty. The future adapter owns that persistence.

Input held on first activation, after a context transition or after ownership is
regained must be released before it can produce a new gesture. Hand arbitration
must prevent reload from sharing a grant with support grip, holster, melee,
power/menu interactions. The prototype consumes a grant; it does not claim the
existing runtime supplies that arbiter.

## Integration sequence and stop conditions

1. **P1a observation only:** validate the installed LE2 identity, layouts and
   world/player/weapon reads, then compare ammo observations with a normal native
   load and HUD/save output. Keep script calls and XR initialization disabled.
2. **P2 compile-only host candidate:** import pinned licensed Halcyon ME2 source
   separately from adaptations; resolve startup/hook/loader ownership and move
   unsafe script work to the game thread. Compile in isolated outputs before
   any deployment. Do not overwrite a DLL referenced by the active ME1 session.
3. **Owned native foundation trial:** normal load, observation, lifecycle and
   clean restoration; then single-owner XR/controller/stereo proof. Build and
   loaded binary hashes must match the recorded candidate.
4. **P3 firing proof:** establish weapon/muzzle transforms and authoritative shot
   direction through native movement/aim updates, one/two-hand transitions and
   tracking loss. Controller pose publication alone does not prove aiming.
5. **P4 native adapter:** connect the tested coordinator only after request,
   timer/auto-reload interaction and reconciliation semantics are verified.
   Exercise duplicates, partial reserves, every cancellation phase, switching,
   tutorial progression and save/reload persistence.

At every stage, failed identity/layout/thread/lifetime validation blocks the
dependent action instead of falling back to an unchecked call. Preserve the
smallest failing experiment and recovery evidence. P5 traversal/physical cover
and P7/P8 UI/progression consume these same contracts; they must not introduce
independent native writers or input owners.

The user's estimate of an hour until game availability is scheduling information.
It does not transfer native ownership when the hour expires. Continue offline
until an explicit handoff; then recheck ownership, installation and backups.
