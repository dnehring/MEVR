#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Spatial upscaling of the eye images (research/32, "Lever: spatial upscaling at the eye blit"): the game renders
// its stereo target below the headset's eye size and the eye blit scales it up with AMD's FidelityFX Super
// Resolution 1 (EASU, the edge-adaptive upscale, then RCAS, the contrast-adaptive sharpen; `external/fsr1`, MIT).
// Game-independent: the settings, the constants the shaders read (checked offline in tests/Main.cpp) and the
// shader text (run offline by tools/upscale-preview, ctest mevr-upscale-shader). The pass itself is
// plugin/Upscale.cpp, in Halcyon's eye blit.
//
// Settings, [Upscale] in <GAME>MEVR.ini: method = off | fsr (default off: Halcyon's bilinear fill);
// sharpness = 0..1 (1 the sharpest, 0 none; default 0.9, RCAS's recommended 0.2 stops). The render size itself is
// GamerSettings.ini's ResX/ResY (tools/vr-game-settings.ps1 -Scale 0.75 sets it from the current target).
namespace mevr::vr::upscale {

enum class Method { Off, Fsr };

struct Settings {
    Method method = Method::Off;
    float sharpness = 0.9f;
};

// "off" / "bilinear" / "" -> Off, "fsr" / "fsr1" -> Fsr (any case); the sharpness clamped to [0, 1] (NaN -> default).
Settings Parse(std::string_view method, double sharpness);
char const* Name(Method m);

// RCAS's "stops" of reduced sharpness from the 0..1 setting: (1 - sharpness) * 2 (0 = full, 2 = none).
float RcasStops(float sharpness);

// cbuffer MevrUpscale : register(b2), as the shaders read it.
struct Constants {
    std::uint32_t con0[4];  // EASU: output pixel -> source pixel (scale.xy, offset.zw), as float bits
    std::uint32_t con1[4];  // EASU: 1 / source size and the gather offsets (FsrEasuCon)
    std::uint32_t con2[4];
    std::uint32_t con3[4];
    std::uint32_t rcas[4];  // RCAS: exp2(-stops) as float bits, the same as two halves (FsrRcasCon)
    float sizes[4];         // source width, height, target width, height
};
static_assert(sizeof(Constants) == 96);

// The constants for a source of srcW x srcH scaled to dstW x dstH through Halcyon's fill mapping: a target pixel's
// UV, inside `imageRect` (x, y, w, h of the image in the target), maps to `fillUv` (x, y, w, h of the source
// rectangle) of the source; both (0, 0, 1, 1) for the whole image. EASU's own con0 is replaced by that affine map
// (source pixel = target pixel * scale + offset, texel centres at integers + 0.5 as FSR has it).
Constants MakeConstants(unsigned srcW, unsigned srcH, unsigned dstW, unsigned dstH, float const fillUv[4],
    float const imageRect[4], float sharpness);

// The HLSL: the FSR 1 headers' text (ffx_a.h, ffx_fsr1.h: external/fsr1, embedded at build time) between the GPU
// defines and our entry points: VSMain (a full-screen triangle, Halcyon's), PSEasu (t0 the source, s0 a clamping
// sampler, b0 Halcyon's FillParams for the rectangle test, b2 the constants) and PSRcas (t0 EASU's output at the
// target size; the result through ToEye, Halcyon's HDR clip, and MEVR_EYE_OUT, core/vr/Colour's hook: put its
// prelude in front for the corrected variant, nothing for the plain one). Profiles: vs_5_0 / ps_5_0 (gathers).
std::string ShaderSource(std::string_view ffxA, std::string_view ffxFsr1);

} // namespace mevr::vr::upscale
