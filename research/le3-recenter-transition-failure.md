<!-- SPDX-License-Identifier: MIT -->
# ME3 recenter transition: native resource failure

2026-11-15. Initial failures and their diagnosis are preserved below. The final
protected run with the shared motion-blur guard passes this bounded recenter/reload
scenario; broader VR acceptance remains open. This follows the stereo captures in
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

## Reusing the existing motion-blur guard

The next candidate adapts ME1's existing `plugin/MotionBlurGuard.cpp`; see its
[original native audit](62-motion-blur-and-menu-pacing.md). No second guard or
resource-replacement mechanism is introduced.

ME3's native settings table identifies MotionBlur at settings offset `0x38`.
Propagation copies that setting to render-thread RVA `0x189A8E4`. Changing it
releases and reinitializes the render-target aggregate; initialization allocates
VelocityBuffer only when the render-thread setting is enabled. A null slot is
therefore expected after disabling blur. The blur-specialized render path
`0x3E3F20 -> 0x3D1F80 -> 0x3E62B0` can still bind that slot without rechecking
the setting. This establishes a settings/proxy mismatch mechanism, not proof that
Back itself releases the resource. The failed run applied the existing settings
fallback about three seconds before the fault.

The native no-blur render body at `0x3E4250` uses its matching shader path and
avoids that velocity bind. Both bodies share the six integer/pointer argument
slots and 64-bit return contract used by the existing guard. ME3 has seven pairs:

| Blur table | Matching no-blur table |
| --- | --- |
| `0x1154B20` | `0x1154990` |
| `0x1154B70` | `0x11549E0` |
| `0x1154BC0` | `0x1154A30` |
| `0x1154C10` | `0x1154A80` |
| `0x1154C60` | `0x1154AD0` |
| `0x1157E78` | `0x1157DD8` |
| `0x1157EC8` | `0x1157E28` |

All ten slots of each table are fingerprinted relative to the executable base;
all pairs agree on destructor and render slot. Five base constructor comparisons
match after replacing only their final vtable identity. The two extended factory
branches allocate the same `0x100` bytes and initialize the same fields through
`0xFC`; their base constructor pair also matches. The intermediate third pair
appears while constructing these extended objects; no independent smaller
allocation is inferred from that intermediate table. Private comparisons and
native code stay outside the repository.

The shared implementation selects the game-specific address map, retains strict
exact-address/signature and full-table fingerprint checks, and converts recognized
proxies on their owning render thread. Changed identities persist for the host
lifetime, including subsequent eyes. Unknown classes still use the original path
and latch guard health false. ME3 publishes that health through the existing host
service so healthy protection suppresses repeated settings resets. Engine-probe
sessions do not install the hook. Snapshots expose the existing guard statistics
directly, without importing LE1's unrelated render hooks.

## Protected native result with the shared guard

Session `20261115-180239-899-LE3-renderer-preview` completes the formerly failing
sequence: ordinary Normandy load, first-person baseline, live-head Back replay,
four transition captures, post-recenter capture, same-save reload and final capture.
The renderer logs a new recenter at 18:03:46.187, after the replay begins, distinct
from its startup recenter. Native saving is visible in the transition capture;
QuickSave changed and was restored. Both load receipts pass exact descriptor,
world/pawn transition, stable gameplay and unchanged source/runtime artifact checks.
No fresh crash report was produced, and the owned game stopped normally through
the runner. Restoration reports zero differences; the four actual VR/LE1 file
hashes were independently checked and match the restoration receipt.

All seven captures have two world images, no capture errors and matching eye
orientations. At the stationary pose, each submitted eye equals its corresponding
located input. Synthetic head replay uses a 64 mm pair; live runtime head tracking
uses a 63 mm pair. The earlier private audit's fixed 64 mm assumption rejected the
live captures at its strict lower bound. The stationary transition audit now
checks submitted-versus-located poses instead of treating the synthetic recording's
IPD as the runtime's IPD. This does not change the turning-history acceptance rule.

Every capture reports the guard installed and healthy with zero unknown variants.
Conversions increase from 55 before the action to 2,825 after reload; this scene
exercises pair seven. The other six retain static-layout and dispatch-test coverage,
not claims of being seen in this native scene. The native settings fallback logs
that healthy protection suppresses the settling/menu-return resets.

Development ME2/ME3 and all three player builds pass. The existing core suite
checks 28 ME3 patterns against the private dump and rejects all 16 guard entries
after a code shift. Six focused tests pass: both guard adapters, settings fallback,
eye pose, publication and software-D3D copy. Guard tests exercise every pair,
cached/recreated proxies, native argument/field forwarding, unknown-class fallback,
missing/moved addresses and foreign fingerprints. Independent code review found
no production blocker.

The transition and post-reload eye images were visually inspected. A collar enters
the lower view after the finite controller replay expires (`controllers.active=false`);
tracked-controller transition and reload captures keep the central view clear.
This is a separate tracking-loss/body presentation concern, not a height change:
all seven captures retain active floor lock and `stageEyeM = finalEyeM = 1.752600`.
Physical blur/comfort, other scenes/variants and full body acceptance remain open.
