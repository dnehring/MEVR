<!-- SPDX-License-Identifier: MIT -->
# ME3 renderer preview evidence

2026-11-15. These are protected null-headset observations, not physical-headset or
campaign acceptance. Private sessions and images remain under `%LOCALAPPDATA%/MEVR`.
The source branch, exact installed binary hashes, settings, normal save descriptor,
process identity and restoration receipts are recorded by the existing runner.

## Latest completed native checkpoint

Session `20261115-153113-544-LE3-renderer-preview` used compiled candidate `652c983e`
with runner correction `6e6a8412`, SFR mode 4, diagnostics on and the imported default
mirror cadence. Native reflection agreed on **493/493** comparisons, with no missing
or mismatched rows. The existing Normandy manual save loaded through its normal
descriptor with observed identity changes. The session finished normally, restored
tracked files with zero differences and released the shared game lock.

| Observation | Evidence boundary |
| --- | --- |
| Third-person snapshot at 15:32:07 | Visible Normandy image on a flat quad; both eyes report `hasWorld=false`. This is not stereo. |
| First-person enabled at 15:32:12 | Request applied, view hook installed, native mode 0; image becomes nearly black. Host view remains invalid and arm IK stays off. No first-person/body acceptance. |
| Shared menu at 15:32:17 | Menu captures without missing-image errors. Native pause reports acquired; closing it resumes the same world. Overlap, prior pause, world replacement and refused restoration still need separate tests. |
| SFR ordering | Every complete sampled gameplay sequence is four scene clears followed by one non-UI backbuffer draw; no second write/copy. All sampled events use the immediate context. |
| SFR capture | First-eye captures remain zero; no authoritative backbuffer write precedes a scene-sized clear. Do not weaken the gate to capture an arbitrary clear. |
| Draw timing | First native Draw averages roughly 91–102 microseconds; replay averages 20–22 microseconds. Two camera views are built. Neither duration nor view count proves scene submission. |

The black first-person image is not sufficient evidence of an HDR conversion failure:
the same SDR run has a visible third-person source. In the current fallback path,
LE3 skips `DriveHeadLook`, which owns host-view publication, while native first-person
camera offsets still apply. This explains the missing IK admission; the exact visual
occluder remains unproved. The guarded menu-torso candidate cannot be accepted from
a run in which arm IK never enabled.

The prior attempt, `20261115-152649-957-LE3-renderer-preview`, reached XR readiness and
the same reflection result but stopped before loading: an overly strict post-start
check rejected the mod appending its compiled defaults. The renderer configuration
was unchanged. The corrected runner permits only known appended sections, preserves
the exact protected prefix and binary/renderer identities, and saves the effective
mod ini. Renderer mode migration is still refused. Both runs restored exactly.

## Monitor cadence versus game performance

The imported `MirrorPresentEvery=8` presents the desktop window once per eight game
frames. At the measured approximately 64–78 game frames per second, the monitor can
therefore show only approximately 8–10 updates per second. This is a source/config
explanation, not a measured headset refresh rate. The runtime advertised 90 Hz.

Steady five-second windows reported 12.85–15.67 ms per game frame. Outside the menu,
the XR submission portion was approximately 0.54–0.56 ms; the remaining interval is
not synonymous with unmodified game cost. It includes game/render work, gameplay
module work and waits outside the Present hook. A capture/menu interval included a
175 ms maximum, so this automated capture run is not a clean stutter benchmark.
The sampled GPU utilization does not establish saturation. No claim that the null
headset alone explains this run's cadence is supported by these measurements.

Use protected `-PreviewMirrorEvery 1` versus `8` on the same candidate and fixture,
with an uncaptured steady interval, to separate monitor updates from engine pacing.
The runner records and checks this setting. Full-rate mirroring can restore desktop
compositor back pressure. `tools/perf-probe.ps1` currently needs LE1's producer seams;
do not invoke it on LE3 and interpret a timeout as game performance evidence. Reuse
that producer via an adapter if finer LE3 timing becomes necessary.

## Next native proof

The bounded `SFRNATIVE` observer records native world-render suppression and actual
scene-family submission separately for each Draw. Its native entry signatures are
checked before installation; it does not retry, change flags or copy on the game
thread. Correlate its fresh records with `SFRORDER`, successful captures and actual
left/right images. Only distinct, correctly tagged views can advance stereo acceptance.
