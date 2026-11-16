<!-- SPDX-License-Identifier: MIT -->
# Sequel SFR per-eye pose tags

2026-11-15. This ports the existing LE1 `xr_session.cpp` EYETAG and held-source
behavior into both sequel `me2_xr.cpp` SFR paths. Upstream ME2/ME3 shared a single
position between projection eyes; snapshots faithfully reported that submission.
The earlier copy/release receipt changes did not create that convention.

## Reuse and implementation boundary

`core/vr/StereoPoseTag.hpp` shares pair interpolation, source-tag caching and a
capture epoch gate. The existing sequel head rings, smoothed orientation and
fractional delay choice remain authoritative. LE2 writes eye pairs alongside its
four head slots; LE3 writes them alongside its sixteen timestamped circular slots.
Both current implementations select by frame delay (LE3's stored timestamps do not
make its current lookup time-based). The **same indices and fraction** used by the
unchanged quaternion/head interpolation select the eye positions. Source audit,
not a new test of the private quaternion function, establishes this call-site link.

SFR retains the shared rendered orientation, but submits each runtime eye position
from that same delayed sample. Actual asymmetric eye offsets are preserved; no
separation-slider-derived or assumed 64 mm baseline enters the declaration. Eye
positions route by destination eye even when render sources are swapped.

A held stereo source freezes one packet: shared orientation, both positions, and
both FOVs. The immutable source packet is recorded before upload so a failed first
upload can retry the same pixels without acquiring a later head pose. Separately,
`EyePublication` receives each tag only after successful copy **and** release;
projection reads those receipts, and a current pair remains mandatory. A successful
copy in a previous frame cannot complete a partial pair in this frame.

## Source and lifecycle behavior

- LE2 already falls back to two copies of the live backbuffer whenever pass zero
  did not advance. LE3 holds pass zero until its existing age threshold, then makes
  the same mono fallback. These source-selection differences remain.
- Explicit live mono fallback uses current shared pose/FOV, without manufactured
  stereo positions or convergence shifts. The LE2 stale-tag/live-image mismatch is
  removed. This is distinct from an intentionally held stereo pair.
- Locked cinema uses current per-eye positions with its deliberate current shared
  orientation. Cinema screen quads retain their current room-placement pose.
- Recenter/session, invalid orientation/position tracking, swapchain creation,
  leaving/reentering SFR, and cinema/swap routing context changes invalidate the
  ring/cache and old-space orientation smoothing. The captured sequence at reset
  must advance before stereo can resume; unchanged old pixels use explicit mono
  fallback and cannot be relabeled in the new reference space.
- Source texture recreation in the existing capture code produces a new monotonic
  capture sequence when pixels are copied; missing source makes the cache ineligible
  to hold. No new texture lifecycle or capture engine is introduced.
- Intentional standalone mono/DIBR and AER publication are not converted to SFR
  per-eye semantics. No new native camera offsets or head-center render policy.

## Offline proof and remaining acceptance

`tests/StereoPoseTagTests.cpp` exercises the actual shared helper and existing
publication policy: fractional pair lookup including asymmetric turned/translating
samples; circular/linear slot equivalence; finite bounded delay; complete packet
freeze; fresh capture and context reset; same-sequence rejection after epoch reset;
live/locked fallback; failed release and failed acquire; and cross-frame retries
requiring both new publication receipts. LE3's existing freeze counter is retained.
Both sequel XR translation units compile. The test deliberately does not claim to
execute the private renderer's quaternion interpolation or a compositor.

Native acceptance must verify both actual projection-layer positions and the source
images, then stationary and turning motion, same-map/menu/recenter transitions and
physical headset fusion. A static fake headset's 64 mm pair is a useful controlled
check, not general proof of metric stereo, physical comfort or stale-frame behavior.
The implementation-only review launched no game. Integration subsequently built
both development and player variants of both sequels, passed the registered
`mevr-stereo-pose-tag`, `mevr-eye-publication` and software-D3D `mevr-eye-copy-warp`
tests, and passed the SPDX check. Independent source review found no blockers.

## Protected native checks

Compiled source `86fd9e72`, using the existing protected runner, normal save loader,
snapshot tool and pose replay (no button/stick inputs):

| Game | Private session | Fixture | Result |
| --- | --- | --- | --- |
| ME3 | `20261115-165123-287-LE3-renderer-preview` | Normal Normandy Save 31 | Five captures passed; zero-difference restoration |
| ME2 | `20261115-165553-786-LE2-renderer-preview` | Freedom's Progress Save 4 | Five captures passed; zero-difference restoration |

Each run captured third person, first person with hands, menu, resume and a scripted
30-degree head turn. All ten snapshots contain both world layers and no capture
errors. Stationary projection positions are minus/plus 0.032 m, rather than the
previous shared left-eye position. Turning positions rotate the baseline while
preserving approximately 64 mm separation and shared rendered orientation:
ME3 `(-0.027717,0,+0.0159928)` / `(+0.027717,0,-0.0159928)`;
ME2 `(-0.027713,0,+0.0159996)` / `(+0.027713,0,-0.0159996)`.
The source eye images were visually inspected and show the world from different
viewpoints. These validate native submission, not a physical compositor's fusion.

The first ME2 attempt (`20261115-165331-878-LE2-renderer-preview`) loaded the correct
save but correctly failed the artifact hash gate because the mod appended its
first-run settings during loading. The executable, host, mod path/module and save
were unchanged; only the ini changed. It restored with zero differences. The retry
seeded the same build's complete, hash-pinned generated defaults before launch;
no load/artifact guard was relaxed. Both successful runs verified the unchanged
source save and restored game/profile files, with the LE1 ini unchanged and the
run lock released. ME3 also verified its exact VR-environment restoration.

The subsequent ME3 Back/recenter transition trial hit a native post-processing
resource fault before recenter completion or same-save reload. See the
[failed-transition investigation](le3-recenter-transition-failure.md); the earlier
static/menu/turning results do not close this lifecycle gate.

ME3 also now reuses ME1's primary-view ownership rule: both the verified native
gameplay caller and the primary local player must match before eye-state/camera
edits. The protected `20261115-174634-476-LE3-renderer-preview` run confirms five
auxiliary views remained unmodified while gameplay retained separate eye states
and a valid 64 mm baseline capture. The Back transition resource fault persists;
this is ownership acceptance, not recenter or reload acceptance.

Still open: physical Quest 3/Virtual Desktop fusion, real head-motion comfort,
native recenter/mode-transition scenarios, and deliberately induced stale capture
or failed GPU-copy scenarios. Those lifecycle failures have offline policy coverage,
not native fault-injection acceptance. HDR opt-in remains outside this correction.

The subsequent `20261115-180239-899-LE3-renderer-preview` run, with the shared
motion-blur guard adapted to ME3, completes the previously failing Back/recenter
and same-save reload scenario. A fresh recenter log accompanies seven valid
stereo captures and zero-difference restoration. Stationary submitted eye poses
match located input: 64 mm during synthetic head replay, 63 mm from the live null
runtime. This closes that bounded transition scenario; broader mode transitions,
physical fusion and injected GPU/capture failures remain open. See the detailed
[guard validation](le3-recenter-transition-failure.md#protected-native-result-with-the-shared-guard).
