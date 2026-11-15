<!-- SPDX-License-Identifier: MIT -->
# ME3 renderer preview evidence

2026-11-15. These are protected null-headset observations, not physical-headset or
campaign acceptance. Private sessions and images remain under `%LOCALAPPDATA%/MEVR`.
The source branch, exact installed binary hashes, settings, normal save descriptor,
process identity and restoration receipts are recorded by the existing runner.

## Latest completed native checkpoint

**Missing ME2 reuse fixed and tested:** `c483d55e` shares the existing ME2
`IDXGIOutput6::GetDesc1` policy through `plugin/HdrDisplayPolicy` and connects the
previously absent LE3 factory seam. No capture rule, native draw retry or viewport
end operation changed. The protected session `20261115-162209-606-LE3-renderer-preview`
loads the same ordinary Normandy save with the same mode, resolution and mirror
cadence. The hook answers SDR (`hdrDisplay=-1`), and actual backbuffer bindings now
show `R8G8B8A8_UNORM` (format 28), including the refreshed backbuffer identity used
by the gameplay segment. Ordering changes from the observed `C C C C D` to
`C C D C C D`. A steady window reports 304 replays, 608 view builds, **304 captures
for 304 presents**, with no first-eye misses. This controlled change identifies
the omitted display-capability policy as the cause of the prior capture failure.

All four snapshots now contain both world layers, a valid head-look view and no
capture errors. The first-person world-image pair was visually inspected: the
Normandy and hands are visible, with differing left/right viewpoints. Arm IK is
active; the ordinary menu pauses, retains the stationary torso and suppresses
hand targets, then resumes. These are observations from this fixture only.
The session restored tracked files/VR state with zero differences and released the
game lock. The first attempt at this candidate stopped in VR-helper preparation
before a game launch and also restored exactly; the successful retry used unchanged
source/settings. ME2/ME3 development and player builds pass, and the extracted real
descriptor hook passed ten offline cases plus independent review.

**Stereo acceptance remains incomplete:** snapshot metadata shows both submitted
eye positions at `(-0.032,0,0)`, while located eyes are at minus/plus 0.032 m. This
is the imported SFR shared-position policy, not a TestLoop duplication or an
EyePublication regression. Current ME1 already has `[EYETAG fix]` in
`src/halcyon/le1/xr_session.cpp`: per-eye positions from the same rendered pose-history
sample, with shared rendered orientation. Audit/reuse that correction with capture
sequence, stale-pair and recenter/session lifetime handling before claiming metric
stereo or physical-headset/head-turn acceptance. Do not substitute current-eye
positions for stale pixels. Native HDR opt-in still follows the unaccepted output
path described below; this checkpoint restores the existing ME2 default policy.

## Diagnosis before the display-policy correction

**Correction to the earlier SDR characterization:** these runs requested
`DesiredRange=DynamicRange_SDR`, but actual texture descriptors and `EyeCopy` logs
show `R16G16B16A16_FLOAT` backbuffers, including the 15:31, 15:58 and 16:07 runs.
LE3 reused ME2's eye conversion but omitted its factory/display-capability hook
(`IDXGIOutput6::GetDesc1`, `[Colour] hdrDisplay=-1`). Thus the requested SDR setting
did not establish the same runtime output policy as ME2. Reusing that missing hook
and measuring the actual resulting backbuffer/capture ordering was the bounded
test above, before changing the native stereo capture architecture.

Session `20261115-160557-252-LE3-renderer-preview` adds the output-owner probes
(compiled host `a511dac7`). The normal save load, 493 reflection comparisons, four
snapshots and exact restoration all pass again; neither eye has a world layer.
In gameplay the sampled final backbuffer draw reads a full-size float16 texture in
pixel-shader slot zero. Its native caller chain runs through the end-viewport
consumer at `0x4812D0` and the final output-transfer functions `0x54DDC0`/`0x54E5B0`.
Both scene families target the same native viewport, with identical sampled output
fields; the raw family field at `+0x48` is one in both passes.

The private native decompilation now establishes the critical ordering: outer
viewport routine `0x480C80` calls the viewport-client Draw at `0x48105D`. Both
Halcyon scene passes execute inside that call. After it returns, the outer routine
flushes the canvas and queues one end-viewport operation. The live final backbuffer
transfer belongs to that later operation. Thus the observed final output transfer
is outside the replayed Draw scope: replaying Draw does not replay that transfer.
This accounts for why the current backbuffer-based first-eye capture lacks its
expected intermediate image. Static shader registration identifies the ordinary
transfer shader as `FReconstructHDRPixelShader<FALSE>` (`HDRReconstructPS`), with an
alternate TRUE variant. This is native HDR reconstruction, not a proven GUI
compositor, and it remains active in the observed float-backbuffer run. Generic
float-to-sRGB conversion alone is not evidence of preserving this native output.
The next step is to compare the corresponding ME2
target-selection/output path and locate a valid first-eye source without losing
native final processing. No new capture gate, output flag or render retry is enabled.
The ME2 native comparison already shows the same outer begin/Draw/end architecture
and an HDR reconstruction shader registration. This is not proof of an ME3-only
engine design: the runtime target-selection difference from an accepted ME2 run
still needs direct evidence.

Do not duplicate the whole end-viewport consumer: it also releases viewport
resources and performs native end/present bookkeeping. A correction needs a
render-thread source boundary before pass-zero output is overwritten, verified
eye/view provenance, preserved native final processing/UI, one native end/present,
and the existing completed-pair/publication failure rules.

Evidence limits: `SFRORDER` covers three draw entry points plus copy/resolve and
scene-clear hooks; it is not an exhaustive GPU command census. The source probe
observes only the first eight PS slots, whose unused bindings can remain stale.
Native family submission and shared target identity do not themselves prove that
both GPU eye images completed. Proving ordered writes and distinct eye contents
at the chosen source remains necessary before accepting a correction.

## Native admission checkpoint

The newer protected session `20261115-155730-208-LE3-renderer-preview` used host
`d0cf258a` with runner `9df78b18`. It passed the same 493 native comparisons, loaded
the ordinary Normandy save, collected four snapshots, and restored with zero
differences. Every sampled gameplay pair from 15:58:16 through 15:58:51 reports
**one valid scene-family submission per pass**, one view each, no invalid families,
and completed native calls. Both passes see world rendering enabled, a present GUI
backend and a known-zero suppression predicate. Therefore the observed gameplay
failure is downstream of native family admission, not a missing second Draw or a
rejected replay at those gates. CPU submission still does not prove GPU completion.

The same run retains `C C C C D` ordering, zero first-eye captures and `hasWorld=false`
in both eyes. The third-person image is a visible flat Normandy panel; first person
is nearly black. The next investigation follows native offscreen output and final
GUI composition, comparing ME2 and ME3 with the existing Ghidra tooling. Do not
relax the capture gate or call a duplicated mono source stereo.

Two preceding attempts did not reach the fixture: an installer regression's fake
launcher collided with the native runner's process check, then a transient startup
settings writer blocked verification. Both were restored with zero differences.
Installer process tests must finish before a native run. The settings reader now
retries only sharing/lock violations for a bounded interval; all configuration and
artifact checks remain. Its 35 runner cases pass. All 65 registered CTest tests and
the development/player builds for the three games pass after the missing LE3
shared panel source registration was corrected.

## Earlier capture-boundary checkpoint

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
| SFR ordering | Covered APIs report four scene clears followed by one non-UI backbuffer draw, with no observed copy. All sampled events use the immediate context; this is not an exhaustive GPU census. |
| SFR capture | First-eye captures remain zero; no authoritative backbuffer write precedes a scene-sized clear. Do not weaken the gate to capture an arbitrary clear. |
| Draw timing | First native Draw averages roughly 91–102 microseconds; replay averages 20–22 microseconds. Two camera views are built. Neither duration nor view count proves scene submission. |

The black first-person image is not sufficient evidence of an HDR conversion failure:
the same float-backbuffer run has a visible third-person source. In the current fallback path,
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

## Native proof boundary

The bounded `SFRNATIVE` observer now records native world-render suppression and actual
scene-family submission separately for each Draw. Its native entry signatures are
checked before installation; it does not retry, change flags or copy on the game
thread. Correlate its fresh records with `SFRORDER`, successful captures and actual
left/right images. Only distinct, correctly tagged views can advance stereo acceptance.
