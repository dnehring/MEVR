// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

// The scope capture's render target as a texture the scope picture can sample (research/39; the mod's side:
// game/ScopeCapture, what's shared: core/vr/ScopeCapture.hpp). Present (render) thread.
//
// The mod publishes the render target's FTextureResource*; here, once per render target, the engine's RHI references
// are followed to the ID3D11Texture2D (vr::scope_capture::FindTexture over SEH-guarded reads; a candidate counts only
// with a vtable in d3d11.dll, QueryInterface for ID3D11Texture2D, the render target's size, one sample and shader-resource
// binding), an extra reference is kept (the engine may release its own) and a view made. Anything that doesn't
// validate: no source, and SightLayers keeps the eye copy, exactly as before.
namespace mevr::host::scope_capture_source {

struct Source {
    ID3D11ShaderResourceView* srv = nullptr;  // owned here; valid until the next ForFrame or Forget
    unsigned width = 0;
    unsigned height = 0;
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};  // tracking space: where it rendered from (looks -Z)
    float tanHalf = 0.0f;                     // its square frustum
    float decodeGamma = 1.0f;                 // the shader raises a sample to this (8-bit gamma-encoded without an sRGB view)
    bool poseFromArm = false;                 // the pose is the one the presented Draw rendered (else the publish before the latest)
};

// The capture to sample for this frame's scope picture of the held `weapon` (vr::sights weapon type), or nullopt: the
// mod isn't capturing (off, stale, another weapon, just enabled), or its texture isn't found. `pairArm`: the submitted
// pair's Halcyon arm (0 unknown).
std::optional<Source> ForFrame(ID3D11Device* device, int weapon, std::uint64_t pairArm);

// For the render levers' classification and diagnostics: the FTextureResource the capture renders into while it's
// enabled (0 none), and the D3D texture found behind it (0 none). Any thread.
std::uint64_t ActiveResource();
std::uint64_t ActiveTexture();

// A new session or device: drop the texture and the view.
void Forget();

// Snapshot JSON (the host's "sights"."capture").
std::string StatusJson();

} // namespace mevr::host::scope_capture_source
