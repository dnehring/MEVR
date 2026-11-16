<!-- SPDX-License-Identifier: MIT -->
# LE3 native renderer audit — 2026-11-15

This is an offline executable/source audit and candidate correction, not headset acceptance. The protected runtime capture is MassEffect3.exe timestamp `60AEF0C4`, `.text` RVA `0x1000`, size `0x1065FC1`, SHA-256 `50c79c1c3e64ceca9af90ce87af919f01361cc9f5adb7efa2de8166e58bcd95d`. Raw executable/script/decompiler output remains private.

## Confirmed renderer boundaries

| Boundary | Evidence and result |
| --- | --- |
| CalcSceneView `0x6CFFD0` | Native function boundary and six-argument shape; calls the player view/FOV events. Captured unique prologue checked before hooking. |
| Draw `0x6D1E30` | Native function boundary and three-argument shape; calls CalcSceneView. Captured unique prologue checked before hooking. |
| AllocateViewState `0x3440F0` | Native one-argument allocator, allocates `0xD60` then constructs the state. Captured unique prologue checked before direct calls. |
| FSceneView construction `0x33D7D0` | Constructor supports existing state `0x8`, rect X/Y `0x5C/0x60`, rect width/height `0x64/0x68`, view `0x90`, projection `0xD0`, translated view `0x190`, translated view-projection `0x1D0`, pre-view translation `0x250`, view-projection `0x260`, view origin `0x320` accesses. The earlier census incorrectly started its four-float rectangle at `0x64`, reading two size pairs as position and size; this was diagnostic-only. |
| LocalPlayer native view fields | Native view build agrees with actor `0x68`, state `0x46C`, origin `0x59C`, size `0x5A4/0x5A8`. |

These checks protect the imported entry contracts; they do not prove stereo image correctness or every imported experimental probe.

## FOV source correction

The import searched camera memory for floats equal to the current FOV, then wrote up to 24 matches. Unrelated constants could match; restoration also used the first previous value for every field. This is replaced by the exact installed `Engine.pcc` path:

1. `PlayerController.GetFOVAngle` reads `Engine.PlayerController.PlayerCamera` (`0x684`). The imported `0xE28` is the separate `SFXPlayerController.MyCamera` gameplay-mode member.
2. `Camera.GetFOVAngle` chooses `LockedFOV` (`0x4D4`) when `bLockedFOV` (`0x504`, mask 1) is set; otherwise `CameraCache.POV.FOV` (`0x448 + 0 + 0x18`). Installed source and LE3 SDK metadata agree. Independent live reflection rows are supplied by the extended renderer ABI audit.
3. The CalcSceneView scope changes just that getter field. It revalidates camera and active field before writing, records the exact previous value, and restores only the same live camera/address while it still contains the value we wrote. Camera replacement and newer native writes are preserved. Missing/dying cameras and invalid FOV values refuse the edit.

The director lens remains untouched for shared cinema. This removes speculative writes, but glass/refraction correctness must still be captured in actual stereo gameplay and ADS transitions.

## Known unsupported import path

The old `d3d_capture.cpp` UI-caller constant `0x4850A2` belongs to the native registration function beginning `0x484FB0`, not a verified UI compositor. Its `g_uiPerEye` path is already inactive and must not be enabled using this constant. Shared cinema UI instead uses the existing late SFR HUD capture seam. No native gameplay HUD crop is accepted from this audit; the read-only GFx probe reports local clip geometry and still needs parent transforms and a live combat fixture.

## Validation and remaining acceptance

The modified native translation units compile. A bounded controlled-memory fixture executes the production FOV helper block and passes locked/cache selection, exact restoration, native overwrite preservation, camera swap, lock-bit transition, missing camera, and NaN rejection. The fixture is private test tooling, not runtime evidence. Full integration builds and the independent reflection table are coordinator-owned.

Still required: native hook marker and live camera-field reflection, first stereo frame/capture, independent left/right cinema images, gameplay glass/ADS FOV transitions, menu/cinema ownership transitions, and physical-headset tracking/comfort. Imported cover/crouch/marker probes and optional experimental native paths require their own evidence before being advertised as verified.

## Authored traversal yaw and movement

The LE3 bridge accepts the v41 `Traversal::Presentation` contract only when its schema, timestamp, scene, and floor-space counters are valid. The game applies authored-camera yaw cancellation only after this publication succeeds. After the ordinary shared pad merge and menu filter, on-foot movement receives the inverse yaw correction through the existing `GamepadMapping::RotateStick`; renderer head look is unchanged. Native reply-wheel axes are not rotated.

An active presentation that expires, loses focus, or crosses a recenter/session blocks walking until a fresh view publication or explicit clear. It is not silently treated as an angle of zero. Recenter/session callbacks invalidate the retained basis, and a fresh inactive publication or null clear releases it. Shared menus retain input ownership. Focused tests cover inverse rotation, stale input, schema/range rejection, recenter/session changes, lifecycle invalidation, focus loss, and release. Native traversal, later camera-blend recovery, and physical comfort remain unaccepted.

## Scripted first-person preview control

Use the existing channel: `tools/snapshot.ps1 -FirstPerson on -Delay 2 -Tag le3-firstperson`, `-FirstPerson off`, or `-FirstPerson status`. It writes `firstperson=on|off|status` alongside the ordinary snapshot options to `snapshot.request`; development/request permission is the existing TestLoop gate. No keyboard injection, extra request channel, persistent ini setting or host ABI was added.

The game-thread request parser queues the option. The owning Present/frame thread applies LE2/LE3's existing `EngineProbe::SetFirstPerson`; the requested capture delay starts after application, and a snapshot cannot start while control remains queued. Invalid values never toggle. LE1 reports unsupported for mutation and preserves its renderer flag. The snapshot script fails a requested mutation when the host is old, reports unsupported, or returns a different actual flag.

Both snapshot JSON and `testloop.status` include `firstPerson.requested`, `result`, the actual enable flag, control support, fresh CalcSceneView hook installation state, raw and semantic game mode, the renderer's existing ownership report, and application time. The enable flag and ownership report are diagnostics, not proof that a particular native camera profile wrote the correct eye position. Visual acceptance still needs the captured first-person body/weapon view.

Focused parser tests cover ordinary snapshots, on/off/status, case handling and malformed requests. All three renderer/TestLoop translation units compile with warnings-as-errors; the native entry getters use the existing atomic hook state. No native preview was launched by this lane.

## Upstream HUD editor ownership correction

The imported `HudProbe::Tick` attempted independent ProcessEvent and CallFunction hooks and enabled a baked per-element HUD layout. In the unified ASI build it now installs neither hook and reports its editor disabled, regardless of an old upstream ini. The standalone upstream build keeps its original behavior. The shared engine route is the only native event owner; `Le3Gui` performs read-only native HUD discovery on the game thread, and measured shared HUD composition owns any later presentation change. This does not enable native crosshair suppression or a default automatic HUD crop.

## Bounded SFR boundary diagnostics (2026-11-15)

The first protected renderer preview (SDR requested, actual float backbuffer) observed two CalcSceneView calls per replay but zero first-eye captures. Four scene-sized depth clears per present passed the size/MSAA gate, while `bbWrittenAtClear` remained zero. A later SFR run produced a visible mono fallback, so black output was not universal. These observations do not establish two completed scene composites or accepted stereo. Mode 1 is migrated to SFR by the imported settings loader; an attempted SBS selection is not SBS evidence.

The candidate reuses LE2's per-pass timing discriminator without its retry behavior. `[SFRTIMING]` reports only actual replay pairs: `pairs`, cumulative `total`, `p0LastUs`/`p1LastUs`, per-window averages/maxima, and `p0Under100us`/`p1Under100us`. It emits the first four pairs and then at most once per five seconds of replay activity. Under 100 microseconds is a heuristic for a declined Draw, not proof of GPU completion. A pair with normal pass-zero cost and consistently short pass-one cost merits inspecting native render suppression after view construction; both short suggests whole-render suppression. No extra Draw is issued.

`[SFRORDER]` samples the first four qualifying SFR presents and three per 300 thereafter. It records up to 48 events, reports truncation and total non-UI draw/copy/clear counts, and prints each event's thread ID and deferred-context status. `C` is a scene-sized depth-clear hook; `D` is an authoritative backbuffer draw passing the existing non-UI classifier; `U` is an authoritative backbuffer draw rejected by that classifier; `P` is a copy/resolve whose destination is the backbuffer (context status `-1` means unavailable). Draw observation continues after the capture flag is armed, so a second eligible write is no longer hidden by that early return. The existing BBARM census consequently includes the extra observations on sampled frames. Events describe CPU hook order, not completed GPU work or identified eyes. Distinct draw calls do not necessarily mean distinct scene composites.

Interpret a complete `C...D...C...D` sequence alongside capture counters and separate-eye images. `C...C...D` explains why the current clear-after-write gate cannot capture. `U...C` requires checking the existing UI classifier rather than accepting arbitrary clears. Cross-thread or deferred events require render-command ordering evidence; do not use game-thread replay flags as render-thread boundaries. The mutex protects only bounded diagnostic storage, never surrounds original Draw/clear calls or GPU work, and logging occurs outside it.

Capture eligibility, capture source, copy timing, and replay count are unchanged. This candidate does not loosen the first-eye gate, retry rendering, change user settings, or copy from the game thread. The corrected viewport census uses native constructor X/Y offsets `0x5C/0x60`; the previous `width x height @ width,height` output was not proof of an offscreen viewport. Native acceptance still requires a fresh owned run with matching configuration, two distinct eye captures, and verified world/body/menu presentation; native HDR reconstruction, appearance and actual SDR-path behavior remain unaccepted.

### Native admission versus submission

The protected 15:31 run (SDR requested, actual float backbuffer) reached gameplay and consistently sampled four scene clears followed by one authoritative non-UI backbuffer draw, with no backbuffer copies. Pass zero averaged about 91–102 microseconds; replay averaged 20–22 microseconds. Two camera builds per pair and zero captures persisted. Thus the earlier single-write count was not merely the capture flag hiding a later write. The fixed 100-microsecond LE2 heuristic also classified many first passes as short on this machine; it is not an LE3 admission test.

The private native audit establishes that Draw builds camera views before checking ordinary world submission. That later path requires the viewport's world-render bit to be clear, the native GUI suppression predicate to be zero, and populated view bookkeeping, along with valid viewport/GUI linkage. A nonzero GUI predicate can select a different GUI-mediated path later in Draw. The submission wrapper flushes the canvas, constructs a renderer and executes or queues it; no explicit one-Draw-per-frame rejection was found in that wrapper. Downstream GPU work remains a separate boundary.

`[SFRNATIVE]` adds an observational two-argument submission hook at `0x35F090`. Its 35-byte entry pattern, the backend getter at `0xC1F860`, and the predicate at `0xBBEDA0` each match once in the protected capture identified above. All three are checked in mapped memory before installing the optional observer. The getter's relative operand verifies the backend global; its native predicate verifies the observed integer field. The observer never invokes those getters or writes their data. Failed hook enable removes only the hook it created; failure leaves existing renderer behavior intact. Successful installation lasts with the resident renderer host, across XR session restarts and gameplay DLL reloads, and ends with the process. It does not add another session owner.

The first four eligible SFR pairs and at most one pair per five seconds report one line per pass:

- `preDisable`/`postDisable`: the SDK-confirmed viewport `bDisableWorldRendering` bit; `-1` means unreadable.
- `preBackend`/`postBackend`, `*PredicateKnown`, `*Predicate`: checked snapshots of backend presence and its raw suppression integer. These are snapshots, not the saved return value of the earlier native GUI/class predicate; missing backend does not manufacture a known-zero predicate.
- `calcCalls`: calls entering the existing camera hook during that Draw; not successful images.
- `submits`, `sceneSubmits`, `uiSubmits`, `badFamilies`: actual calls into the submission wrapper, classified by a checked family header's scene pointer. `uiSubmits` means null-scene family, not a guessed screen/crop identity. The wrapper forwards the original canvas and family exactly once.
- `lastViews`/`maxViews`: validated family view counts at submission (`-1` if no valid observation). The header must have a nonnegative count no greater than 64, sufficient bounded capacity, and nonnull storage for nonempty arrays. No view element is dereferenced. Draw's stack-local map is not read by inferred stack offsets.
- `submitDisable`/`submitPredicateKnown`/`submitPredicate`: flags at the last observed submission; `complete` records whether Draw returned without the existing replay SEH catching a fault.

Counts are thread-local to sampled Draw scopes; work submitted asynchronously outside that scope is not attributed to a pass. The wrapper's CPU entry does not prove render-command execution or GPU completion. If replay has no scene submission and admission snapshots differ, inspect that exact native gate before changing behavior. If both passes submit valid scene families, investigate downstream execution and composition ordering. Unchanged snapshots with unequal submission counts require a narrower observation of native internal decisions, not a fabricated explanation. Sampling adds small CPU overhead on sampled pairs; timing is still diagnostic only. This candidate has no retry, native writes, capture relaxation, or game-thread copies. At introduction, native installation and paired logs were pending; the protected gameplay observations below now establish those boundaries.

Validation: the LE3 renderer translation unit compiled with MSVC Release warnings-as-errors (existing unused-parameter/local suppressions). A private controlled-memory fixture executes the production admission/header readers and passes 12 cases, including unreadable pointers, absent backend, known flags, scene/null-scene classification, invalid count/capacity, and missing nonempty storage. This is offline validation, not native hook or image acceptance.

The protected 15:58 normal-save gameplay run narrows the failure: sampled pass zero and replay each entered submission once, with a nonnull scene and one valid view, zero bad family headers, world rendering enabled and a known-zero backend predicate. Both completed normally. The earlier suspicion of replay admission suppression is therefore ruled out for those observations. Four scene-sized clears followed by one backbuffer draw still produced zero captures. The covered API paths show no intermediate backbuffer composite; this does not establish missing offscreen scene rendering or exclude writes through unobserved APIs. The census deliberately excludes offscreen draws and does not comprehensively observe every Draw variant, Dispatch, command-list execution, or alternate output binding. The submission observer validates the family header and nonnull scene pointer, not the scene object or view contents.

The next bounded `[SFROUTPUT]` line accompanies each sampled pass. It records the original native caller of the detoured Draw (shared by its replay), viewport/self/canvas identity, last submitted family/canvas, checked family target at `+0x10`, and eight raw 32-bit fields at `+0x38..+0x54`. `outputKnown=0` leaves target and raw fields zero; failed reads do not publish partial data. `lastSubmit` identifies the recorded call ordinal when a Draw submits multiple families. These values are diagnostic identities, not D3D resource pointers or named rendering policies. The family constructors in LE2 (`0x324D20`) and LE3 (`0x33F0E0`) have the same field stores; their renderer constructors copy the header and clone views before queuing execution. The inverse native GUI-composition condition reaches family `+0x48`, then renderer `+0x50`, but its final effect consumer and semantic name remain unverified. No target virtual methods are invoked and no fields are changed. Correlate these records with the final backbuffer caller and bound-resource census before selecting a capture source. The expanded production-reader fixture passes 15 cases, including raw output layout and fault rejection; the LE3 translation unit compiles with the same strict options. Fresh native output-field evidence remains pending.

### Native LE2/LE3 comparison

The existing private Ghidra projects contain runtime-spliced LE2 timestamp `60AEEFA5` and LE3 timestamp `60AEF0C4`. Bounded decompilation through the repository's existing tooling found these differences; raw code remains private:

| Boundary | LE2 | LE3 | Implication |
| --- | --- | --- | --- |
| Ordinary world admission | Draw `0x6D0A00` checks the viewport disable bit, saved UI suppression, and populated view map. | Draw `0x6D1E30` additionally nests submission under valid GUI/viewport linkage and a present native GUI backend. | Real native difference, but both passes passed admission in the observed gameplay run. |
| Saved UI suppression | Aggregates active UI-scene flags and a UI-controller virtual predicate. | Reads through SFXGUIInteraction to the native backend's suppression integer. | Do not copy LE2 field assumptions or change flags to bypass suppression. |
| Family submission | `0x342E30`: canvas flush, construct renderer, execute or enqueue. | `0x35F090`: equivalent ownership/queue structure. | Neither inspected wrapper contains an explicit once-per-frame reject. |
| Renderer ownership | `0x340340` copies family fields and clones per-view state with native stride `0x11A0`. | `0x35C580` does the same, native stride `0x11B0`. | No evidence that a queued renderer retains the Draw stack's family header; do not reuse the LE2 clone stride. |
| Execution | `0x34BF70` contains the main render-path selection inline. | `0x368850` delegates selection to `0x3685C0`, which adds three show-flag predicates before choosing the ordinary/alternate path. | Those flags' semantic names are unverified; accepted CPU submission still does not prove identical downstream work. |
| Per-view effects | Effect proxies render through native virtual callbacks. | `0x367880` likewise dispatches per-view effect proxies. | The final output consumer may be a proxy rather than the central Draw/submit wrapper. Trace the observed compositor before editing its behavior. |
| GUI render-texture preparation | `0x9335C0` accepts a movie record with a render texture. | `0x92A680` additionally checks a nonzero movie-record field. | A further native branch difference, not established as the cause of the failed stereo capture. |

Pinned Halcyon LE3 revision `cfd40019450d3469b44a9b155f03e0263a61f787` and the imported renderer retain the same Draw eligibility, original-then-replay ordering, and clear-after-backbuffer-write capture rule. The shared host changes camera/floor/cinema policy and image transfer, not the number of original Draw calls. LE2's capture-group strategy cannot be adopted blindly: all four observed LE3 clears precede its one backbuffer write, so they form one group and a clear-time copy there would hold the previous frame. Shared UI redirection also must not be described as strictly post-capture: its armed helper can become true from texture allocation plus clear count, and the shared panel supports early batches. The backbuffer census records the native call before that redirection; it cannot conceal a second non-UI draw from the census, but UI routing remains relevant to image interpretation.

### Observed viewport-end reconstruction boundary

The protected normal-save gameplay run at 16:07 establishes a narrower output path. Both sampled Draw passes used the same viewport target and canvas, with raw family fields `+0x38..+0x54` equal to `1,1,0,0,1,0,0x3F800000,1`. In particular, `+0x48` did not differ. The original Draw caller was `0x48105D` inside the outer viewport routine `0x480C80`. That routine constructs the canvas, invokes client Draw (where Halcyon performs both passes), flushes the canvas and queues one viewport-end command after client Draw returns. The captured backbuffer-draw stack proceeds through its consumer `0x4812D0`, then `0x54DDC0`, `0x54E5B0`, `0x3520F0` and native D3D. This final output operation is outside Halcyon's replayed client Draw scope.

Static shader registration identifies the ordinary output shader as `FReconstructHDRPixelShader<FALSE>`, from `HDRReconstruct` / `HDRReconstructPS`; an alternate mode selects the TRUE specialization. The observed draw has a full-size single-sample `R16G16B16A16_FLOAT` texture bound at pixel-shader slot zero and an 8-bit backbuffer destination. Other logged shader slots may be stale bindings; the first-eight-slot census does not prove that every slot is consumed. The native reconstruction routine retains its source/output resource references, binds its output, configures the reconstruction shader and full-screen draw, unbinds the output and releases the retained references. It is not identified as a GUI compositor. The actual backbuffer was float16 despite DesiredRange requesting SDR; the missing ME2 display-capability hook must be tested before attributing a native engine difference.

The focused LE2 comparison prevents a false game-specific conclusion:

| Boundary | LE2 | LE3 | Verified comparison |
| --- | --- | --- | --- |
| Outer viewport driver | `0x4756B0` | `0x480C80` | Same begin, client Draw, canvas flush, one queued end-command structure. |
| End caller | `0x475CE0` | `0x4812D0` | Calls the reconstruction selector from viewport end. LE3's stack is observed live; LE2 is static evidence here. |
| Reconstruction selector | `0x5448A0` | `0x54DDC0` | Same RHI virtual query at `+0x320`, followed by reconstruction if its returned integer is not 2. The semantic enum name is unverified. |
| Reconstruction body | `0x545090` | `0x54E5B0` | Equivalent source/output reference handling, shader variant selection, full-screen draw and reference release; both native bodies are 1286 bytes. |

This proves an observed mismatch between the imported clear-after-backbuffer-write capture assumption and the current LE3 viewport-end output path. It does **not** prove that the replay failed to render, that all backbuffer writes were enumerated, or that LE3 alone has this HDR architecture. Why previously accepted LE2 captures had a usable intermediate backbuffer boundary remains unverified; no particular ini value or display mode is established as the cause.

The correction boundary is per-eye scene completion **before the second pass overwrites the relevant retained source**, followed by the native final reconstruction/output semantics. Before implementation, identify the actual retained source's writer, lifetime and per-eye contents in the render-command stream; verify which reconstruction inputs/settings are required; and preserve menus, UI, SDR and HDR output. A raw float-texture copy through generic colour conversion does not establish preservation of native reconstruction. Replaying the entire viewport-end routine is unsuitable because it also performs resource cleanup and the end/present operation. Neither copying from the game thread nor bypassing native flags is justified. No correction is implemented at this checkpoint; distinct reconstructed eye images and an owned gameplay run remain required. All raw native output stays private, and the bounded offline comparison is complete.
