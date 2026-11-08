<!-- SPDX-License-Identifier: MIT -->
# 61: Virtual Desktop player report: replay rejection and periodic frame jumps (2026-11-08)

The supplied `MEVR-report-2026-11-08-2328_1_.zip` identifies running and installed MEVR **0.1.1,
b9492d8725**, Quest 3 through bundled VirtualDesktopXR, Windows 10 19045.7807, and an RTX 5070 Ti
with driver 617.42. It contains both host sessions, only the last Halcyon session, and a 20-second
performance capture. The supplied video is a separate recording, without a shared frame clock.

## What the report establishes

The first unsupported recorded operation is D3D11 vtable slot 51, `ClearUnorderedAccessViewUint`.
`DrawReplay.cpp` deliberately marks that operation unsupported and then forwards it to D3D11.
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
report, with `--plot` when matplotlib is available. No new game session was launched for this analysis.
Earlier SteamVR null-headset fault injection demonstrated the cascade, not the VDXR trigger; earlier
natural local runs did not reproduce the unsupported operation. An actual connected Quest/VDXR run
is still required to identify its native caller.

The live local inventory on 2026-11-08 is Windows 11 build 26300, RTX 4090, driver 591.86
(32.0.15.9186), with the Virtual Desktop OpenXR manifest selected. The player's Windows 10,
RTX 5070 Ti and 617.42 stack differs in OS, GPU and driver as well as any unrecorded injected
components. Matching resolution alone does not reproduce that stack. This comparison identifies
uncontrolled variables, not a confirmed driver defect.
