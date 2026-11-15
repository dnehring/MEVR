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
No game was launched for this implementation.
