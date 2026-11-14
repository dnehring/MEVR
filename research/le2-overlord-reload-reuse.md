<!-- SPDX-License-Identifier: MIT -->
# Call of Duty Overlord: reload reuse assessment for ME2

Inspected 2026-11-13. The Call of Duty project in our reuse plan is **Aeka0's
[Overlord](https://github.com/Aeka0/Overlord)**, a VR mod for **Modern Warfare 2
Campaign Remastered**. Its source includes physical magazine handling, chambering,
support-hand transitions and multiple feed mechanisms. The local read-only
checkout is pinned to
[`2d9b0ea14afa6079d69e93ce0a6270fb2bb6a597`](https://github.com/Aeka0/Overlord/tree/2d9b0ea14afa6079d69e93ce0a6270fb2bb6a597).
This assessment inspected the actual headers, runtime entry points and test
sources at that pin. It did not run the mod, build its tests, import code or
establish physical usability in ME2.

The previous local pin was `11f3005034188894976cb8392122ad166d59f927`.
After fetching upstream, its clean checkout fast-forwarded by 93 commits to the
new pin above. The geometry, well-contact, gesture headers and root license are
unchanged between those revisions. The inspected reload/contact/ownership diff
adds pose-scale propagation, scale-aware transform composition and blocked
acquisitions after a carry action consumes a press. Native runtime changes also
include supply selection, ejection presentation and timing instrumentation.
This is a focused comparison, not an audit of all 93 commits. Older research pins
remain historical evidence; new extraction should use the pin above.

The repository's [GPL-3.0 license](https://github.com/Aeka0/Overlord/blob/2d9b0ea14afa6079d69e93ce0a6270fb2bb6a597/LICENSE)
fits the reuse policy already documented in `CLAUDE.md`. Any actual adaptation
needs preserved upstream attribution, the appropriate GPL SPDX line and a scoped
`NOTICE.md` entry. This original summary and the existing original MEVR request
coordinator do not contain adapted Overlord implementation.

## Findings and extraction decisions

Paths below are relative to upstream `src/client/component/vr/gameplay/`.

| Source inspected | Actual behavior/dependencies | ME2 decision |
|---|---|---|
| `physical_reload_geometry.hpp`: `segment_distance`, `box_distance`, `sweep_well` | Small spatial operations; the well test sweeps a segment through a finite radial/depth volume. Includes `falling_trajectory.hpp`, which brings `hands/pose_math.hpp` and further pose types. The primitive alone does not validate tracking continuity or spawn/contact history. | **First direct-adaptation candidate:** extract only the bounded contact math and its necessary vector operations, with attribution. Do not import the pose/weapon runtime include tree just to get these primitives. |
| `magazine_well_contact.hpp`: `well_motion`, `advance_well` | Separates alignment, continuous movement, capture/retention margins, occupied receiver and withdrawal after a discontinuity. Invalid values and a jump into the socket do not become an insertion on a later stationary frame. | **First policy candidate:** translate the continuity and withdrawal rules into a small qualified-contact layer before MEVR's request coordinator. Use measured ME2 thermal-clip socket geometry; retain tests for spawn-inside and teleport. |
| `physical_reload_gesture.hpp`: `geometry`, `controller` | Contact binds weapon-instance generation, reference generation and input sequence. Distances are metres from one solved gun/controller sample. The controller also depends on a large mechanical ammunition model and synchronous commit callback. | Reuse coherence/edge/identity invariants and selected policy, **not the entire controller**. MEVR already has a tested request coordinator; evaluate contact handling as its upstream input. |
| `physical_reload_contact_sample.hpp`: `sample_contact` | Converts tracked wrist and solved gun pose, handedness, magazine grip and authored part data into gun-local contact. Depends on scene/runtime bindings, belt presentation and magazine grip selection. | Reference for constructing coherent samples. Implement the MEVR pose adapter with our calibrated grip/aim frames and actual ME2 weapon transform. It is not a portable standalone sampler. |
| `physical_reload_profile.hpp`: `reload_profile` | Keeps immutable profile identity, native family binding, well axes, part/grasp transforms, mesh/bone contracts and presentation timing; also embeds ammunition rules and H2 asset names. | Adopt a smaller ME2 schema: clip/socket transforms, alignment/capture/retention, supported class and calibration/profile version. Do not copy weapon assets, H2 bone names or chamber/feed assumptions. |
| `hand_interaction/runtime.cpp`, `hand_interaction/object_identity.hpp`, `interaction_schedule.hpp` | Central per-hand authority, ordered offers/claims, input history and object identity. Runtime begin/offer/settlement is tied to H2's server scheduler and publication system. | Use the ownership invariants to design the real producer of MEVR `HandGrant`. Resolve reload versus support/holster/power/menu conflicts before contact admission. Keep one MEVR arbiter; do not install H2's scheduler. |
| `physical_reload_runtime.cpp` | Includes H2 entry points, scene models, native shot/ammo operations, scheduler, loader, hooks and weapon-family presentation. Holds native transactions and published scene state. | Exclude from direct import. Native ME2 ammo, tutorial events, auto reload and cancellation remain behind our validated game-thread adapter. |

The original [MEVR coordinator](le2-reload-core.md) currently trusts caller-qualified
eject/acquire/insert signals and an exclusive hand grant. It does **not** yet
implement those physical contact tests or the grant producer. Overlord therefore
has concrete value in the next implementation step, beyond being a visual example.

## Important differences from ME2

Overlord's gesture controller commits mechanical/ammunition transactions
synchronously and publishes the resulting mechanical state only on accepted
commit. Its model includes magazines, chambering and weapon-specific feed/bolt
rules. ME2's native thermal-clip reload has its own timers, tutorial event order,
reserve transfer and early-cancel behavior. Copying that ammunition model would
create another authority and would not preserve ME2 automatically.

Keep our [P0 request/reconciliation contract](../docs/le2-integration-contract.md):
qualified physical insertion produces one correlated native request; uncertain
execution blocks retry until authoritative reconciliation. Gesture geometry
must never credit ammunition or call `DoReload`. Do not add a mandatory slide,
charging handle or chambered-round mechanic to ME2 solely because a Call of Duty
weapon requires it. Any deliberate interaction change needs a specific design
and native/tutorial proof.

## Concrete P4 extraction task

1. **Compare and isolate:** pin source files/functions and dependencies from the
   table; document which math can be adapted directly and which policy requires
   ME2 changes. Preserve notices and keep derived code separate from original
   coordinator code. This is a bounded extraction, not a new whole-game runtime.
2. **Contact input:** one immutable sample contains solved gun and clip/wrist
   transforms, named space/units, frame/reference/weapon generations, tracking
   validity and active exclusive grant. Preserve explicit pose scale and convert
   once to socket-local metres; do not apply calibrated scale twice.
   Reject stale/mixed frames, nonfinite data and unsupported profiles.
3. **Contact output:** report a qualified insertion edge plus diagnostics. Bind
   it to the exact held `ClipKey`; reset history on context/weapon/reference/grant
   changes. Spawn-inside, tracking reacquisition or a teleport into contact must
   require a valid exit/re-entry before admission. Do not equate a swept contact
   alone with a reload or a valid hand grant.
4. **Tests before native wiring:** adapt observable cases from upstream
   `tests/vr/physical_reload_tests.cpp`, especially fast continuous crossing,
   radial miss, wrong orientation, nonfinite values, jumps, spawn-in-contact,
   withdrawal and asymmetric capture. Inspect
   `manual_magazine_reload_tests.hpp`, `foregrip_reload_tests.hpp` and
   `hand_interaction_lifecycle_tests.hpp` for left/right hand, support handoff,
   held input and stale-instance scenarios. Include same-frame consumed-press
   acquisition blocking and non-unit calibrated pose scale from the newer source.
   Translate invariants; do not count
   upstream test success as a result for our adapter.
5. **Composition proof:** drive the existing coordinator with the qualified
   contact layer. Prove one request for repeated/held contact; no request from
   stale clips, dropped grants or discontinuities; no retry of uncertain native
   work. Then measure reachable seated/standing socket tolerances on an actual
   ME2 pistol/rifle with the native adapter and physical headset.

Deliver a scoped adaptation record, small module/profile schema, focused tests
and exact unresolved native/headset gates. No additional game access is needed
for steps 1-4 or synthetic composition tests. Native timing, two-eye alignment,
ergonomics and native ammo/tutorial persistence remain separate acceptance work.
