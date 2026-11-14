<!-- SPDX-License-Identifier: MIT -->
# LE2 physical reload request coordinator

Date: 2026-11-13. P4 offline prototype under the [P0 integration contract](../docs/le2-integration-contract.md). [PhysicalReloadRequest.hpp](../src/core/vr/PhysicalReloadRequest.hpp) is original MIT, engine-independent C++20. It is not included by any production target. No game/VR launch, deployment, native call, profile staging or shared ABI change is part of this work.

## Purpose and authority

The coordinator turns a validated physical gesture into **one correlated request to begin the native reload route after insertion**. It has no ammo quantities, inventory, clip capacity, native timer, script invocation or ammo-transfer function. A virtual clip is an opaque interaction token, not a second ammunition supply. No state or output promises ammunition transfer, a refund, firing permission or tutorial completion.

This provisional policy preserves the native game's entry/transfer distinction described in [ammo source findings](le2-ammo-fixture-source.md): tutorial notification can precede transfer, and native cancellation can transfer ammunition after the early threshold. The future adapter must use the verified native entry path, recheck eligibility at dispatch, and observe acceptance and completion independently. Calling `DoReload` directly is outside this contract. Automatic/button-driven competing reloads, animations, early-cancel behavior and physical/native timing alignment remain runtime dependencies.

No Overlord, Titanfall or other external implementation was copied or adapted. The [reuse map](../docs/le2-port-reuse-map.md) supplies architectural comparisons and future geometry candidates. This bounded state coordinator implements original MEVR policy; it does not import an external game's ammunition ledger, hooks, hand arbiter or assets. No new third-party notice is required for these files.

## Actual API

Namespace: `mevr::vr::reload`.

| API/data | Contract |
|---|---|
| `Coordinator(epoch, Limits={})` | Nonzero fresh host epoch; optionally lower clip/request budgets. Noncopyable/nonmovable; no reset API. |
| `Step(Frame)` → `StepResult` | One serialized, coherent observation. Returns admission/rejection, optional newly acquired `ClipKey`, optional one-shot `RequestKey`, invalidation/cancellation flags and whether native reconciliation is still required. |
| `CancelGesture()` → `StepResult` | Explicit local drop/cancel without falsifying tracking, eligibility or generation. Idempotently invalidates gesture/clip and requires release. An outstanding request remains uncertain; this does not request native cancellation. |
| `Observe(Feedback)` → `Observation` | Correlated native acknowledgement/reconciliation. Never emits another reload request. |
| `GetPhase()`, `HeldClip()`, `Pending()`, `Faulted()` | Read-only state for diagnostics/presentation. Pending state is separate from the gesture phase. |

`Frame` contains:

- A nonzero, strictly increasing `sample` serial. Duplicate/old samples cannot change identity, release input latches or invalidate a current clip.
- `Identity {world, weapon, context}`: nonzero host generations, each monotonically nondecreasing within the epoch; a changed component must increase. These are host-issued identity tokens, not raw UObject addresses or SDK offsets.
- `supportedFiniteReserve`, `eligible`, `contextAllowed`, `trackingValid`: current caller-verified admission facts. Defaults fail closed. Eligibility must be rechecked through insertion. Native reload may subsequently make eligibility false; that does not erase the already issued request.
- `HandGrant {identity, lease, hand, exclusive}`: explicit caller-issued grant for Left or Right. Identity must match the frame. Lease is nonzero and monotonic; continuous ownership can retain its lease, but a revoked/replaced grant requires a new higher lease. Same lease with a different hand is rejected. The core cannot verify global exclusivity; the caller's arbiter must actually prevent sharing with grip, holster, melee, powers or menus.
- `Signals down {eject, acquire, insert}`: held levels, from which the coordinator derives rising edges. The caller must qualify these signals with valid interaction/contact evidence; this header contains no receiver geometry or tracking interpolation. `insertedClip` supplies the exact acquired token on the insertion edge.

`ClipKey` binds epoch, unique clip serial, originating generations and the full hand grant. `RequestKey` binds epoch, unique request serial and originating generations. Zero/invalid tokens, old clip tokens and forged combinations are rejected. Counters never wrap: exhausted clip/request budgets fault the coordinator; regressed generations, wrapped sample serials and revoked/reused hand leases fail admission. A counter's maximum value is a last valid identity, not permission to restart at zero.

All calls must be serialized by one owner. This is not a concurrent queue or lock. The future native adapter owns thread validation and durable request deduplication if transport can repeat a one-shot output.

## Gesture and outstanding-request behavior

The gesture phases are `Idle → Ejected → HoldingClip`. Only an insertion edge with the exact current clip, active grant, tracking/context and eligibility produces a request. The token is consumed, the gesture returns to Idle, and the separate outstanding-request gate blocks further gestures. Ejection and acquisition alone produce no native request or timer.

The first activation, generation/grant transition, interrupted gesture and terminal settlement all require an observed release of **all** gesture signals before starting. Every fresh sample updates held-input history even when admission fails. Simultaneous conflicting rising edges cannot skip stages. Repeated insertion, held acquisition, replayed samples and old clip tokens cannot emit a second request.

Tracking/context/support/eligibility loss before insertion discards the held clip/stage. Missing or changed hand grants also invalidate the clip. World, weapon and context generation changes invalidate local gesture state. Deliberate `CancelGesture` provides the same local invalidation without requiring a fake loss of tracking/ownership. Presentation/firing recovery for an interrupted virtual gesture still belongs to the adapter; returning the coordinator to Idle is not an assertion about a native weapon's mechanism.

After a request was emitted, interruption/cancellation retains its exact originating identity and marks uncertainty. Changing to another weapon does not permit a new request while that gate remains unresolved. There is no elapsed-time input and no timeout/retry transition. `FeedbackKind::Uncertain` records a timeout, missing event or other uncertainty without releasing the gate.

Feedback must match the entire outstanding request key:

| Feedback | Effect |
|---|---|
| `Accepted` | Records that the native path was accepted/began; retains the gate and any prior uncertainty. Does not prove transfer. |
| `Rejected` | A definitive pre-execution refusal; requires `nativeReconciled=true` to settle. Rejection after acceptance is contradictory and keeps the gate uncertain. Later observed completion/abort must reconcile it. |
| `Finished` | Requires authoritative native reconciliation before settling. May arrive without a separate acceptance message if the adapter observed immediate completion. |
| `Aborted` | Requires authoritative native reconciliation before settling. Any native transfer must be accounted for externally; this is not a refund/no-op guarantee. |
| `Uncertain` | Retains the gate, regardless of time or visual state. |

`nativeReconciled` is an adapter attestation backed by observed native outcome/quiescence, not a timer, animation disappearance or button release. An unconfirmed terminal report cannot unblock a retry. A matching report from an old generation may settle only its retained old request; it cannot resurrect an old clip or advance the current weapon's gesture. All other stale feedback is ignored.

A **new coordinator lifetime requires a new host epoch and resolution of all old native requests**. Destroying/reconstructing this object during hot reload is not a recovery procedure. The future host must preserve the coordinator or retain its uncertainty gate outside replaceable code until authoritative reconciliation. This prototype does not supply that persistence mechanism.

## Standalone evidence and remaining work

[tests/le2-reload-core](../tests/le2-reload-core/CMakeLists.txt) is a separate CMake project with one executable and one CTest entry. It has no runtime, ASI, XR, SDK or shared-build dependency. The 18 test functions use always-active throwing checks, not `assert` disabled by Release builds. They cover phase order, one-shot output, held/conflicting input, old/forged/consumed clips, every admission guard, explicit grant loss/change, deliberate drop/cancel, stale samples/generations/feedback, pending interruption, 5000 uncertain samples without retry, contradictory rejection, and counter exhaustion.

Validated using Visual Studio 2022 Build Tools/MSVC 19.44, `/W4 /WX`: **Release and Debug builds pass, and CTest passes in both configurations**. The executable reports 18 passing coordinator tests. Example from an isolated worktree, with private build output:

```powershell
cmake -S tests/le2-reload-core -B local-tools/le2-reload-core-build -G "Visual Studio 17 2022" -A x64
cmake --build local-tools/le2-reload-core-build --config Release
ctest --test-dir local-tools/le2-reload-core-build -C Release --output-on-failure
cmake --build local-tools/le2-reload-core-build --config Debug
ctest --test-dir local-tools/le2-reload-core-build -C Debug --output-on-failure
```

These are deterministic offline logic results. They do not establish native exactly-once ammo transfer, valid engine layouts, a working hand arbiter, physically usable socket geometry, tutorial progression, both-eye correctness, comfort, performance or campaign behavior. The P1/P2 foundation, P3 weapon transforms and P4 native dispatch/reconciliation adapter must be verified before connecting this header. Follow the P0 sequence; do not wire it to the active ME1 runtime or start native work when a scheduling estimate expires.
