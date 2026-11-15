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

This is command-admission evidence, not GPU completion or full presentation proof. Some
upper-level LE2/LE3 SFR/mono/DIBR builders still ignore the full-frame copy result when
setting `rendered` and caching poses. AER also has intentional old-eye retention and
bootstrap behavior. Those policies need a separate pair-validity review: only publish/cache
a fresh pose after the corresponding images succeeded, distinguish reuse of a known valid
old pair, and avoid claiming a failed new image as fresh. Do not call native HDR accepted
until that gate and source-to-eye capture evidence pass.

### Separate BGRA UI fallback gap

LE3 `EnsureUiSwapchain`/`EnsureGameUiSwapchain` select `g_swapFormat`, while their captured
UI sources are RGBA8. If a runtime selects BGRA rather than RGBA, the UI `CopyResource`
sites in `me2_xr.cpp` are cross-family and need their own validated, alpha-preserving
conversion or a compatible swapchain format. EyeCopy/Blit's eye shaders force alpha to 1;
applying them to those UI quads would break transparency. This patch deliberately does
not route UI through the eye converter. Verify UI channel/alpha preservation separately.
