<!-- SPDX-License-Identifier: MIT -->
# Shared ME2/ME3 HDR eye-copy regression

2026-11-15: the headless WARP regression passed on this machine, including the D3D11 debug
layer negative control. No game, headset, window or Windows HDR setting was used or changed.
This is converter evidence, not native ME3 stereo or headset acceptance.

## Reused production path

ME3's `HalcyonLe3Bridge` forwards its eye-copy seam to the same `plugin/EyeCopy` as ME2.
That adapter draws incompatible float sources through `core/d3d11/Blit` into an sRGB render
target view, instead of relying on a raw copy between different format families. Neutral
same-family SDR keeps its raw-copy path. The test links these actual implementation files;
it does not reproduce the conversion shader in a test helper.

`tests/EyeCopyWarpTests.cpp` replaces only `host::colour::EyeConstants`, returning neutral
settings without reading user profiles. Actual `Colour`, `Ini` and `Log` code is linked.
Its software device is single-threaded, with no swapchain. Sources are render-target-only
textures, forcing Blit to use its real shader-readable intermediate copy.

## Verified cases

- A deliberately incompatible `R16G16B16A16_FLOAT` to `R8G8B8A8_TYPELESS` raw copy produces
  a D3D11 validation error and leaves a distinctive destination sentinel unchanged.
- The real EyeCopy conversion fills that destination, and a typed `R8G8B8A8_UNORM_SRGB`
  destination, with independently fixed sRGB expectations. Linear .25/.5/1 becomes
  approximately 137/188/255, within two byte values. Distinct coloured stripes detect
  missing/double gamma conversion, black output and swapped channels.
- Positive and negative two-pixel convergence shifts produce the shifted image while
  retaining the unshifted vacated strip. Both eye indices and the mono index are exercised.
- Same-family neutral SDR uses the shared operation's actual raw-copy path, preserving all
  four bytes exactly. BGRA-to-RGBA SDR takes the draw path and preserves encoded RGB within
  two byte values, proving decode before the sRGB target's encode.
- The converter restores the caller's viewport, primitive topology and render target.
  No D3D11 error remains in the valid conversion cases.

The neutral pass clips values above 1 at the 8-bit destination; the test explicitly checks
this behavior. It does **not** certify HDR highlight tone mapping, a particular game's
HDR transfer function, headset colour grading or gamma metadata. Native proof still needs
the current source format/content, successful stereo captures and comparison of source,
converted eyes and headset output under the protected session workflow.

## Registration

Use the existing CMake/CTest suite, with a standalone `mevr-eye-copy-warp-tests` executable
containing the test plus `src/plugin/EyeCopy.cpp`, `src/core/d3d11/Blit.cpp`,
`src/core/vr/Colour.cpp`, `src/core/Ini.cpp` and `src/core/Log.cpp`. Include `src`; link
`d3d11`, `dxgi` and `d3dcompiler`; use the existing C++20 warning policy and `NOMINMAX`,
`WIN32_LEAN_AND_MEAN`, `_CRT_SECURE_NO_WARNINGS` definitions. The isolated build passed
`/W4 /WX /permissive-`.

If Windows' D3D11 debug layer is unavailable, the executable returns 77 with an explicit
skip reason instead of claiming the negative control ran. Register `SKIP_RETURN_CODE 77`.
The full result on this machine was `EyeCopy/Blit WARP: PASS (HDR, SDR, +/-shift,
raw-copy refusal, pipeline restore)`.

## Failure propagation follow-up

The complete-copy bool contract is now explicit: `EyeCopy::Copy` and the existing LE2/LE3
`CopyIntoImage` bridge return true only after issuing a validated copy/shift or draw, and
false on failure. Their binary bool signatures are unchanged, but all callers must use the
new semantics; rebuild host and Halcyon archives together. There is no caller raw-copy
fallback. The operation checks device/context, dimensions, sample counts and supported
resource shape before choosing a raw copy. Size mismatch draws instead. Unsupported target
views and unsupported shader sources refuse without modifying the image.

`Blit::Draw/DrawWith` return success rather than swallowing intermediate-copy failures.
Their cached source now distinguishes single-sample copying from MSAA resolve. Shared
standalone `core/vr/Output` does not publish a failed draw. LE2/LE3 full-frame eye helpers
return the complete-copy result, do not notify a successful capture on failure, and flat
mono readiness follows the copy result. LE1's separate Halcyon blit is retained; its mono
helper now stops after a required failed blit and respects swapchain-wait failure instead
of attempting a raw dimension mismatch.

Raw and drawn sequel convergence share ME2's unshifted fill followed by shifted overwrite,
with offsets clamped to the image width. This preserves valid edge pixels and makes LE3's
raw path agree with its HDR draw path; the old LE3 raw shift could leave a stale edge despite
its comment claiming a clear. No UI clear/blend/alpha behavior is changed.

The expanded WARP regression passed unsupported sRGB target/view, missing render-target
bind, a real `EnsureCopy` refusal for a typeless source, null inputs, self-copy, arrays,
uninitialized Blit, same-family resizing, raw shifts and single-sample/MSAA/single-sample
transitions. Valid cases had no D3D11 errors. Output compilation passed for LE1/LE2/LE3,
and all three Halcyon XR translation units compiled with their existing warning policies.

### Remaining native presentation gate

This is command-admission evidence, not GPU completion or full presentation proof. The
following publication policy addresses the source-level pair-validity gap; native runtime
fault/recovery and source-to-eye capture evidence still must pass before HDR acceptance.

### Copy and release publication policy

Both sequel renderers now consume `core/vr/EyePublication`, rather than treating a newer
source-history pose as proof of the image most recently released to OpenXR. Each eye
becomes valid only when its copy and release both succeed. An acquired failed attempt
invalidates that eye; a failed acquire leaves the untouched previous publication intact.
Current-pair readiness requires successful publications for both eyes in this frame.

SFR, mono projection and DIBR guard their projection/cache blocks with that current-pair
test. Flat mono also requires successful release before its readiness flag. AER retains
its existing source history and sequence IDs, with submitted pose/FOV/sequence copied into
the receipt only after successful publication. Unchanged old eyes keep their submitted
tags. A new history capture without successful upload cannot advance the projected pose.
Unpublished history sequences and invalid eyes retry through the existing catch-up path.
Bootstrap reports success only for a completed current pair. History reset, session stop,
swapchain creation and reference-space recenter invalidate previous receipts/history.

`OnEyeCopied` still stages GPU samples **before** release, while image ownership is valid.
The separate `OnEyeReleased` completion hook performs no image access: failed publication
discards the staged copied-eye diagnostic. Pre-release copy-command counts are labeled as
such; they are not release/presentation proof. The wrapped XR layer and actual end-frame
submission remain responsible for runtime presentation.

`tests/EyePublicationTests.cpp` exercises the exact header used by both renderers: partial
left/right failure, failed release, source-history advance without upload, untouched-eye
reuse, catch-up, reset and untagged current-pair versus tagged AER reuse. The executable
passed `/W4 /WX`; both modified XR translation units, both bridge hooks and TestLoop
compiled. Register it as a standalone CTest target with `src` as its include directory;
it needs no game or graphics libraries. The WARP converter test remains separate.

This does not prove that the existing render-pose acquisition itself matches a native
engine frame, or that a compositor displayed accepted commands. Native SFR/AER/mono/DIBR
captures, reference-space reset and induced-copy/release-failure recovery remain required.

### Separate BGRA UI fallback gap

LE3 `EnsureUiSwapchain`/`EnsureGameUiSwapchain` select `g_swapFormat`, while their captured
UI sources are RGBA8. If a runtime selects BGRA rather than RGBA, the UI `CopyResource`
sites in `me2_xr.cpp` are cross-family and need their own validated, alpha-preserving
conversion or a compatible swapchain format. EyeCopy/Blit's eye shaders force alpha to 1;
applying them to those UI quads would break transparency. This patch deliberately does
not route UI through the eye converter. Verify UI channel/alpha preservation separately.

### Cinema publication follow-up

Both sequel SFR cinema callers now require the same `CurrentPair()` receipt as
projection before calling `CineScreenLayers`. A failed acquire, wait, copy or release
cannot submit a new screen quad using an old eye. The shared receipt regression
covers the important distinction: an untouched old tagged eye remains legal for
AER with its old pose, but does not qualify for current cinema placement.

Cinema frame eye poses remain the current located headset poses: `SequelCinema`
uses them to place the screen and render its room backdrop, not to describe the
director camera which rendered the movie. The cinema calcview path preserves the
director's position/rotation and lens. Substituting the delayed world-projection
pose would misplace that screen/backdrop. Native visual acceptance remains open.

Audit of nonprojection consumers of the full-frame eye swapchains found only the
shared cinema path. Flat mono uses its own swapchain and successful-copy/release
gate; overlay/UI and scope images have separate owners. The legacy SBS projection
path is separate from these full-frame eye swapchains and this receipt policy.

### Failed wait/release ownership recovery

LE2 and LE3 full-frame eyes and flat mono now share `SwapchainOwnership` in
`EyePublication.hpp`. After acquiring, only an exact successful wait allows GPU
access or release. Timeout/failure skips both. A failed wait or release quarantines
that handle, invalidates eye publication, and logs a restart requirement once;
subsequent frames cannot acquire another index and mistake release of the older
outstanding image for release of a new copy. Successful release after a failed copy
still permits a later retry, while rejecting that frame's content.

Only actual successful swapchain creation clears ownership quarantine. Source
history resets, recentering, and publication reset do not. This bounded recovery
intentionally omits the affected layer until the handle is recreated; it does not
attempt to repair an uncertain runtime image queue. The shared regression covers
wait failure/timeout, release failure, no subsequent acquire/release, reset
independence, real recreation, and copy-failure retry. Both sequel XR translation
units and both TestLoop configurations compile. Native error injection/compositor
acceptance remains untested.

`TestLoop::WrappedRelease` still stages textures while owned, then discards the
real swapchain's staged snapshot on failed release (the copied-eye synthetic key
already has its separate completion rollback). It no longer retains that failed
release as snapshot evidence. GPU staging/tap issuance itself is not release proof.
