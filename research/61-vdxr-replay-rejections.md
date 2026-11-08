<!-- SPDX-License-Identifier: MIT -->
# 61: Virtual Desktop player report: replay rejection and periodic frame jumps (2026-11-08)

The supplied `MEVR-report-2026-11-08-2328_1_.zip` identifies running and installed MEVR **0.1.1,
b9492d8725**, Quest 3 through bundled VirtualDesktopXR, Windows 10 19045.7807, and an RTX 5070 Ti
with driver 617.42. It contains both host sessions, only the last Halcyon session, and a 20-second
performance capture. The supplied video is a separate recording, without a shared frame clock.

**Follow-up result:** enabling the game's compute FXAA naturally reproduces the report's
three-operation replay rejection under a local SteamVR null headset at the player's render
dimensions. Correct FXAA replay is now implemented and passes a local AA-on Normandy run.
Turning AA off restored replay in the original paired control. This identifies a reproducible
engine-side trigger; the original player's effective AA setting was not collected, so attribution
of their specific failure remains provisional. See the local reproduction below.

## What the report establishes

The first unsupported recorded operation is D3D11 vtable slot 51, `ClearUnorderedAccessViewUint`.
The player's 0.1.1 recorder deliberately marks that operation unsupported and then forwards it to D3D11.
This message is our replay eligibility decision, not an HRESULT from a failed right-eye draw.
The log says **three unsupported operations, the first at slot 51**; it does not identify the other two.
The API is a resource clear with no return value. [Microsoft's API documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearunorderedaccessviewuint).

The release re-enables One Draw after 300 matching camera records without checking whether a new
command recording is replayable. Three rejected frames turn it off again. At 80 Hz,
`(300 + 3) / 80 = 3.7875 seconds`. While One Draw is enabled, Halcyon omits its normal second
Draw; rejecting the recorded tail then leaves no new replayed right eye for those frames.

In the player's actual performance capture, six retry cycles coincide with six brief groups of
approximately halved draw counts. Median counts are **5,548 normally and 2,748 in those groups**.
The retry interval is **3.788 seconds**. The six groups contain 18 samples altogether, with
`pass0_marks` falling from four to two. Individual group lengths are 4, 3, 2, 3, 3, 3 samples;
probe sample boundaries and millisecond log timestamps do not identify exact per-eye frames.

| Retry log time | Rejection log time | Probe samples with fewer than 3,500 draws |
|---|---|---|
| 23:27:40.698 | 23:27:40.744 | 5-8 |
| 23:27:44.484 | 23:27:44.521 | 309-311 |
| 23:27:48.277 | 23:27:48.311 | 612-613 |
| 23:27:52.060 | 23:27:52.098 | 915-917 |
| 23:27:55.848 | 23:27:55.886 | 1218-1220 |
| 23:27:59.640 | 23:27:59.678 | 1521-1523 |

`tools/perf-probe.py` reports 1,601 samples over 20.0131 seconds. Excluding its bootstrap sample,
the median interval is 12.47 ms, render-thread work 9.36 ms, and estimated GPU work span 11.80 ms
(p90 12.76 ms) against a 12.50 ms budget. GPU span is an upper bound, including possible idle gaps.
Only two sample intervals exceed 20 ms. Most retries therefore do not require a long frame to
produce a visual discontinuity. Swapchain wait is effectively zero; median `xrWaitFrame` is
1.58 ms, `xrEndFrame` 1.33 ms. There are no access violations during the capture.

Do not treat the pass-0 timing mark as an exact left/right-eye workload split: it occurs multiple
times in each sample. Do not equate submitted game frame rate with headset presentation or network
delivery; this report does not include the VD performance overlay or compositor telemetry.

## The onset is more informative than the repeated failures

In `LE1MEVR.prev.log`:

- The learned game target is **5120 x 5312** at 23:09:49.505. Successful replay continues for
  minutes; the 23:15:47.535 summary reports 3,114 replays, zero rejections and zero fallbacks in its
  last 60 seconds. The scene is the entry menu, so this does not establish gameplay performance.
- At **23:15:45.299**, that target is observed at **800 x 600**. It temporarily fails the learned
  eye-target classification. The recorder relearns at 23:15:51.132, and the first slot-51 rejection
  is logged at **23:15:51.533**. The target is formally learned at 800 x 600 at 23:15:56.949.
- That interval runs at about five game frames per second. The same 300+3-frame loop consequently
  takes about 60.75 seconds. The logs do not establish whether minimization, desktop resizing or
  another action caused the size/frame-rate transition.
- XR state was already VISIBLE (4) by 23:11:06.298, several minutes before the rejection. A new
  XR focus-loss event at the failure onset is not demonstrated.

In the last session, rejection starts at **23:26:12.885**, in the front end before the Normandy
load. It persists at **3072 x 3188**. XR becomes FOCUSED (5) at 23:26:10.459 and no later session
state change is recorded. Thus neither the Normandy nor a new focus transition is necessary for
the observed failure. An immediately failing fresh session also weakens a strictly resize-only theory.

## What is specific to Virtual Desktop, and what remains unknown

The public VDXR implementation can use a separate D3D11 submission device/context or reuse the
application device. Its separate-device path synchronizes through fences at `xrEndFrame`; the
shared-device path can save and restore context state. Its swapchain wait method deliberately
does not block, relying on frame pacing for image availability. These are real differences to
examine when comparing runtimes. [D3D11 implementation](https://github.com/mbucchia/VirtualDesktop-OpenXR/blob/6da20fe478e331c05672243fd82a9a1500cd39f5/virtualdesktop-openxr/d3d11_native.cpp),
[frame submission](https://github.com/mbucchia/VirtualDesktop-OpenXR/blob/6da20fe478e331c05672243fd82a9a1500cd39f5/virtualdesktop-openxr/frame.cpp),
[swapchains](https://github.com/mbucchia/VirtualDesktop-OpenXR/blob/6da20fe478e331c05672243fd82a9a1500cd39f5/virtualdesktop-openxr/swapchain.cpp).

A source search found no direct `ClearUnorderedAccessView*` calls in that public runtime tree.
This does not cover proprietary backend/streamer code, driver internals or a different bundled
runtime version. The report identifies the runtime name, not a binary version/hash matching this source.

Our inline recorder hooks accept calls whenever this thread is recording, without checking the
incoming context against the attached game context. **[verify]** A foreign context used synchronously
on that thread could contaminate a recording. VDXR's separate device makes context provenance worth
checking, but it does not prove this happens: recording normally ends in the scene's FinishHook,
whereas OpenXR frame submission runs later at Present. Work on VDXR's separate asynchronous thread
would not inherit our thread-local recording pointer. A nested call or another injected graphics
component would have to account for the overlap.

The first-call stack and resource owner decide between these cases:

| Evidence from a future natural rejection | Interpretation / next implementation step |
|---|---|
| Game caller, same immediate context, game resource | Identify the engine pass and add correct replay support for its commands and resource/counter lifetime. |
| VD/runtime or overlay caller, foreign context | Scope recording to the intended context, covering all inline hooks, and verify no legitimate eye commands are excluded. |
| Injected caller, same game context | Identify the nested scope; determine whether it belongs in the eye replay or must be suspended. |
| D3D11-internal caller chain | Check variant-hook nesting and duplicate interception before treating it as an engine command. |

None is selected as the proven origin by the supplied logs. D3D11 variant changes alone are not
evidence of an overlay: [the earlier investigation](../docs/knowledge/gotchas/d3d11-per-context-vtable.md)
already established that D3D11 changes its own per-context dispatch entries.

## Follow-up: a concrete engine pass matches all three unsupported calls

Targeted local binary research identifies the game's **compute FXAA** pass as a strong candidate.
The shader registrations name `FFXAAPrepComputeShader`, `FXAAPass1CS`,
`FXAAFilterComputeShaderHorizontal`, and `FXAAPass2CS`. The pass issues this sequence:

1. `ClearUnorderedAccessViewUint` (context slot 51), clearing the dispatch-argument buffer.
2. Ordinary compute dispatches that prepare the work.
3. `DispatchIndirect` (context slot 42) at argument-buffer offset 0.
4. `DispatchIndirect` at offset 12, for the other filtering direction.

The original recorder supports ordinary `Dispatch`, but rejects the clear and both indirect dispatches.
That is exactly **three unsupported operations, first at slot 51**. This is a concrete game-side
explanation; Virtual Desktop does not need to issue a clear for this pattern to occur.

For the locally verified LE1 executable, the clear's return address is `exe+0x99B277`, with the
FXAA pass's caller return at `exe+0x23ABB6`. The indirect-dispatch RHI wrapper is `exe+0x99CEA0`.
These are diagnostic address/name findings, not copied engine implementation. Raw analysis stays
outside the repository in the private Ghidra directory. The static call sequence is not proof that
the player's recording contains these same callers: their report predates caller tracing.

The earlier local negative controls had `AntiAliasing=False` in both `GamerSettings.ini` and
`BIOEngine.ini`. The supplied report contains neither game INI, so their effective anti-aliasing
setting cannot be compared. This missing variable is more directly relevant to the unsupported
command sequence than the runtime name alone.

### Natural local reproduction

The follow-up run `scratch/qa-replay-jump-v011/fxaa-runtime-20261108-094538` uses the existing
private recovery/tracer build, **`qaForceOneDrawMiss=0`**, `sharePass=5`, `oneDrawWindow=0`,
SteamVR's null headset at 90 Hz, and the recorded Quest pose/FOV inputs. The renderer confirms
**3072 x 3188 game source and 2688 x 2880 eye images**. Anti-aliasing was enabled in the game
configuration and explicitly applied again with the game's own `scale set AntiAliasing True` in
gameplay. The game reached **BIOA_NOR00, game mode 0**.

At **09:46:22.284**, the tracer observes the native FXAA clear with `recording=true` and
`afterTail=true`, caller `MassEffect1.exe+0x99B277`, and stack `MassEffect1.exe+0x23ABB6`.
At **09:46:22.313**, the replay guard reports **three unsupported calls, first at slot 51**.
The same native clear remains inside the recording in Normandy at 09:46:56.473 and 09:47:06.164.
The guarded build falls back once and does not repeatedly rearm; the performance probe has zero
replays. This reproduces the original *eligibility failure* through a real engine setting,
without injected failures and without Virtual Desktop.

The control, `fxaa-runtime-20261108-094735`, uses the same build, dimensions and Normandy scene
with anti-aliasing off. It has **no native UAV-clear traces and no unsupported-call rejection**.
A separate missing-view-record fallback during the menu transition recovers at 09:48:44.717;
the probe subsequently reports **1,205 successful replays since the last telemetry reset** and zero replay failures.
The paired probes contain 375 samples with AA on and 376 with AA off, each over 12 seconds with
zero access violations. The replay counter is periodically reset; it is not a count within the 12-second window.
The machine-readable comparison is `scratch/vdxr-report-analysis/fxaa-comparison.json`.

Timing is not representative of the player's machine: this is an instrumented recovery build
(missing-view recovery and unreplayable-tail probes have different thresholds), a null headset, a different refresh rate, and a
different local hardware/driver stack. It establishes a native trigger, not
the player's exact perceived headset experience. An initial tracer attempt at 09:43 did not
establish an XR session and is excluded from the VR result.

Settings, saves, installed ASI and SteamVR configuration were backed up and restored with hash
verification; the selected system OpenXR runtime was unchanged. These are temporary QA runs,
not deployment of a new fix. The player's missing game settings and caller trace still prevent
claiming that their particular instance is conclusively FXAA, but it is now the leading explanation.

### Implemented replay support

`DrawReplay.cpp` now records and reissues `ClearUnorderedAccessViewUint` and `DispatchIndirect`.
The clear copies all four integer values and retains its UAV. The dispatch retains the argument
buffer and byte offset, then reads its GPU-generated contents at that point in the replay after
the preceding clear/preparation commands. No CPU readback or extra engine eye render is added.
[Microsoft's DispatchIndirect documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-dispatchindirect).

UAV bindings now retain their initial counter arrays, including explicit resets, `UINT(-1)` keep
values and null counts. The previous assumption that the engine only unbinds UAVs was false.
[Microsoft's UAV binding documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-cssetunorderedaccessviews).

The GPU regression test found an additional correctness issue: a D3D11 state-validation dispatch
entry can forward through another hooked variant. The original pass appended three items, but its
replay appended four because one dispatch was recorded twice. Compute hooks now pause recording
while forwarding through D3D11, retaining only the outer application command.

`mevr-draw-replay-tests` exercises the real recorder on a D3D11 device. WARP and RTX 4090 runs pass:
GPU-generated arguments at offsets 0 and 12, repeated replay after poisoning the original contents,
four-channel bit-exact clears, retained resource lifetime, UAV unbinding, copied multi-slot counter
arrays, reset/keep/null counter semantics, and continued rejection of unsupported `CopyStructureCount`.
The Release host build also passes. No FXAA or One Draw setting is disabled by this change.

The fixed native run is `scratch/qa-replay-jump-v011/fxaa-fixed-runtime-20261108-095921`:

- **AA enabled, sharePass 5, oneDrawWindow 0**, in **BIOA_NOR00**, with the same 3072 x 3188
  source and 2688 x 2880 eye sizes. The private QA build uses the production recorder and
  recovery code, with the existing process-local SteamVR selection helper; no failure injector.
- A successful title replay at **10:00:05.179**, and a gameplay replay at **10:01:05.683**, each
  contain **one uint UAV clear, two direct dispatches and two indirect dispatches**.
- The 60-second summary at **10:01:03.050** reports **1,418 eye replays, zero unreplayable tails**.
  One missing-view fallback during the menu/load transition recovered before the capture; no
  unsupported-call fallback occurs. The stats counter resets at that summary, so the probe's
  subsequent value of 265 must not be compared directly against its 625 captured frames.
- The **20.0074-second / 625-frame** probe has **zero access violations**, and no fallback or
  rearm events during its interval. The frame draw count stays near 2,417 instead of periodically
  dropping. Render-thread work excluding XR waits has a median of 7.38 ms. The null runtime still
  paces at about 31 fps; this is not evidence of the player's 80 Hz headset delivery.
- Both saved eye images were inspected: each contains a rendered Normandy view, with the expected
  different asymmetric projection. This is a static diagnostic check, not headset visual acceptance.
- The optional engine-versus-recorded eye-diff request was consumed but produced no report within
  20 seconds. No exact pixel/command equivalence with the engine's second-eye path is claimed.

The game and null runtime were closed, and the original installed binary, configuration and profile
were restored and hash-verified. Physical Quest/VDXR validation remains open. The fix is local and
unpublished; it does not claim to eliminate unrelated streaming or compositor stalls.

## Why the title can look stable while gameplay visibly jumps

The latest renderer log distinguishes two projection modes:

- At **23:26:12.883**, title-screen mode 1 declares the same symmetric horizontal FOV for both
  eyes, approximately -15.7 to +15.7 degrees.
- At **23:26:24.496**, gameplay mode 2 declares -54 to +40 degrees for the left eye and -40 to
  +54 degrees for the right eye. These are the runtime's asymmetric eye projections.

`OneDrawSecondEye` returns immediately on an unreplayable tail, after the ordinary second Draw
has already been suppressed. `SfrOnPresent` can still publish the current backbuffer as pass 1
when a pass-0 capture exists. Thus the failure path can deliver first-eye pixels in the second-eye
slot, rather than necessarily holding an old correctly rendered pair. Submission routes the pair
using its metric-eye record and does not know that the tail was rejected.

**Geometric prediction, not a measured video displacement:** using `MetricStereo::Projection`,
the reported gameplay FOVs give opposite horizontal projection offsets of about +/-0.2425.
Duplicating one eye's image into the other gives a displacement of about **24.25% of the image
width** for distant geometry: approximately 745 pixels at the 3072-wide game source, or 652 pixels
at the 2688-wide eye image. This excludes positional parallax, crop, lens distortion and recording
scaling. The symmetric title projection has zero offset difference of this kind.

The title's 3D cinema screen also uses eye-specific quads, while boot movies use a common flat
quad. Neither should be treated as a gameplay projection control. A visually stable title is
therefore compatible with the logged front-end rejection, without proving that the title has no
visible stereo defect. Other runtimes can also have asymmetric projections: this mechanism does
not establish a VDXR-only bug. Calculation: `scratch/vdxr-report-analysis/projection-mismatch.json`.

## Resolution and runtime pacing

VDXR recommends **2688 x 2880 per eye** in the last session; the swapchains use exactly that size.
The larger **3072 x 3188** game target is independently configured, and the install manifest records
Setup writing those game dimensions. This is about 26.5% more source pixels than the runtime eye
recommendation. The 5120 x 5312 target in the earlier log is also a game render target, not proof of
VDXR recommending that eye size. The report does not explain who selected that earlier size.

The excessive source workload can worsen the cost of rendering a full second eye during fallback.
It cannot by itself explain why this particular API is rejected: replay succeeded at the larger
target and then failed at 800 x 600. The installer correction and the guarded rearm address
amplifiers of the problem; they do not identify the original caller.

Quest pacing telemetry shows zero recovery actions/refresh interventions throughout the supplied
capture. No sustained half-rate game loop is present there. This does not rule out VD stream or
reprojection issues in addition to the demonstrated replay problem.

## Changes and validation

The earlier `1be908b` recovery change tests a fresh command tail before rearming One Draw, retaining
the optimization for replayable frames. `d0283dd` separately corrects Automatic setup's first-launch
resolution. Neither is in this player's b9492d8 build.

This investigation adds automatic diagnostics for the **first eight unsupported calls per process**:
slot, recording position, caller and module-relative stack, thread, context COM identity/device
comparison, context type, and UAV resource type/size where available. They run only on unsupported
recorded operations, require no player setting changes, and do not change replay eligibility or
forwarding. Startup also logs the OpenXR-reported runtime version alongside its name. Initial diagnostic
samples can add logging overhead; normal frames and later failures do not perform that sampling work.
The Release `MEVR-LE1` build, whitespace check and SPDX check pass. Native VDXR validation of the new
diagnostics remains open; this build was not deployed or published.

Local analysis artifacts are in `scratch/vdxr-report-analysis`: `analyze.py`, `correlation.json`,
`retry-events.csv`, `perf-summary.txt`, and `retry-correlation.png`. Run the script against an extracted
report, with `--plot` when matplotlib is available. The initial log analysis did not launch a game;
the subsequent FXAA runs are documented above. Earlier SteamVR null-headset fault injection
demonstrated the cascade, while the FXAA follow-up reproduces a natural eligibility failure.
An actual connected Quest/VDXR run remains necessary to validate the player's headset experience
and confirm whether their original failure has this same native caller.

The live local inventory on 2026-11-08 is Windows 11 build 26300, RTX 4090, driver 591.86
(32.0.15.9186), with the Virtual Desktop OpenXR manifest selected. The player's Windows 10,
RTX 5070 Ti and 617.42 stack differs in OS, GPU and driver as well as any unrecorded injected
components. Matching resolution alone does not reproduce that stack. This comparison identifies
uncontrolled variables, not a confirmed driver defect.
