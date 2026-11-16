<!-- SPDX-License-Identifier: MIT -->
# ME3 recenter transition: native resource failure

2026-11-15. This is a failed native test, not accepted recenter or reload parity.
It follows the successful stationary/turning/menu stereo captures documented in
[per-eye evidence](sequel-eye-pose-tags.md). The first trial used `86fd9e72`;
the subsequent diagnostic and ownership trials below use newer candidate builds.

## Reused route and evidence

The existing sequel input handler detects two Back rising edges within 400 ms.
`GamepadMapping` maps left menu with left grip held to Back. The recording generator
now supports explicit `back-button` and `r3` plan segments; no new runtime request
protocol was added. ME1 uses R3; the imported sequel route deliberately uses Back.
Back also reaches the native game, so a replay acknowledgement cannot prove recenter.

Protected session `20261115-172905-243-LE3-renderer-preview` launched cleanly, passed
the final settings-initialization gate, and loaded normal Normandy Save 31 with
unchanged source/artifact hashes. Snapshot `2026-11-15_17-29-59-783` is the baseline.
The first Back edge was scheduled about 2 seconds after replay began at 17:30:03.830.
At 17:30:05.903, the render thread faulted at native RVA `0x9AD7ED`, reading `0x1C`.
No requested transition snapshots or new recenter completion were recorded; the
same-save reload step was not reached. The first-chance report alone does not prove
a fatal crash, but rendering stopped progressing and the owned game was stopped.

QuickSave changed during the run and was restored. The timing and save change are
clues, not proof that saving or Back caused the resource failure. The test did not
exercise a combat weapon and did not establish a general recenter failure.

## Private native investigation

The existing read-only ME3 Ghidra project and decompile cache were reused; game code,
instruction windows and crash reports remain private. The faulting helper is at
`0x9AD7A0`: its incoming resource argument is null and the native null branch leads
to the observed `0x1C` dereference. Return address `0x3E6B18` identifies the first
resource bind in `0x3E62B0`; the second resource is loaded only afterward. Cached
decompilation associates the first bind with global RVA `0x1764C18`; its semantic
resource name is not yet established. The stack traverses native per-view
post-processing, rather than our OpenXR image-copy helper.

Reuse audit: ME1's `render_hook.cpp` identifies primary gameplay views by native
caller **and** local-player identity, because auxiliary views can reuse that player.
LE3's original `CalcViewDetour` checked local-player identity alone before applying
private eye view state and camera edits. The ownership correction below now uses
ME3's independently verified caller, not a copied LE1 address.

Next: correlate the resource's availability with CalcView caller, owning family and
render target, original/replaced view state, and the native save/Back transition.
Reuse the existing bounded renderer observers. Do not suppress the exception,
blanket-disable post-processing, or skip a required native action to claim success.
The existing menu Recenter command provides an independent route to test the XR
transition once its pointer action is confirmed; it does not close the Back failure.

## Recording and cleanup details

`TestLoop::SubstituteHead` also services the base-space locate used by recenter.
A normal head-relative recording can therefore supply the wrong recenter origin.
Use `make-walk-recording.py --live-head` for future recenter trials: controller
poses/inputs are replayed, while the runtime supplies the head. The failed trial
used a zero-origin head recording and stopped before recenter; it does not validate
this live-head route. Independent decoding confirms two Back edges, zero movement
axes, and head substitution disabled in the new live-head recording.

`Stop-Process` returned before the game's teardown finished, so immediate restoration
correctly refused. After fresh process checks confirmed exit, the original profile,
game files and exact VR environment were restored with zero differences; LE1's ini
was unchanged and the shared lock released. Stop now waits on the verified process
object for up to 15 seconds before reporting success, preserving ownership on timeout.
The launch identity helper also defers a newly appearing launcher's incomplete first
read only when all known fields agree with a valid second sample; a later pair of
complete matching observations is still required for adoption. Wrong identities,
changed creation times and persistent unreadability remain refusals.

## Live-head reproduction and primary-view correction

Session `20261115-174029-207-LE3-renderer-preview` used live runtime head tracking
and replayed only controllers/inputs. It reproduced the same native fault at
17:41:30.340, before recenter completion. Head substitution is therefore not
required for the failure. A valid baseline preceded the fault; reload was not reached.

The bounded, diagnostics-only `SFRVIEWOWNER` observer found an independent bug:
auxiliary caller `0x480E24` shared the primary local player and received a private
eye state. The gameplay call returns to `0x6D26AE`; both callsites were verified
in the existing private native project. ME3 now reuses ME1's caller-plus-player
ownership rule and validates the gameplay CALL bytes before installing hooks.
Auxiliary views retain their native camera and view state.

Session `20261115-174634-476-LE3-renderer-preview` verifies that correction:
all five logged auxiliary calls report `primary=0` and identical original/final
view-state pointers. Gameplay calls retain separate left/right states. Baseline
`2026-11-15_17-47-31-320` has both world images, no capture errors, equal eye
orientations and a 64 mm submitted baseline. Development/player builds and the
three focused eye-pose/publication/copy tests pass.

The same transition still faulted at `0x9AD7ED` at 17:47:37.482. No transition
captures or recenter completion arrived, and reload was not reached. Thus the
ownership bug is fixed, but it was not sufficient to resolve the resource failure.
Both later runs stopped their owned processes and restored with zero differences.
The final run's four actual VR/LE1 file hashes were compared with its restoration
receipt and matched, independently of the success flag.

The native aggregate at `0x1764A20` owns the failing slot at offset `0x1F8`.
Its allocator labels that slot `VelocityBuffer`; the aggregate release path clears
it along with the other render targets. The observer samples this resource on the
game thread at view creation, not at the later render-thread bind. Zero is also
the observer's unreadable sentinel, and normal gameplay often reports zero, so
these samples alone cannot establish a bad lifetime or prove the crash's cause.

Next reuse target: ME1's existing `plugin/MotionBlurGuard.cpp` and
[motion-blur research](62-motion-blur-and-menu-pacing.md). ME3 currently uses the
shared settings fallback, not that native guard. Establish ME3's allocation and
proxy-selection conditions before adapting the guard. Do not turn off arbitrary
post-processing or swallow the null-resource fault as a fix.
