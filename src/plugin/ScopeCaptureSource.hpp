// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/ScopeCapture.hpp"

#include <d3d11.h>
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

// Shared with the spectator camera (plugin/Spectator, research/49): the D3D11 texture of `width` x `height` behind a render
// target's FTextureResource*, by the guarded search above (`hint` tried first, updated on success). Returns it with a
// reference of its own (the caller releases it), its description and the path found; null if none validates.
ID3D11Texture2D* FindTargetTexture(std::uint64_t resource, unsigned width, unsigned height, vr::scope_capture::TexturePath& hint,
    D3D11_TEXTURE2D_DESC* desc);
// The view format a capture's texture is read through, and the gamma the shader decodes it with (1: none). False for a
// format nobody reads.
bool CaptureViewFormat(DXGI_FORMAT format, DXGI_FORMAT& view, float& decode);
// Once, with the game's device: a texture of our own on it shows the vtable its textures have, which the search then accepts
// besides d3d11.dll's (the capture targets' textures were missed by the module check alone). Present thread.
void NoteDevice(ID3D11Device* device);
// Diagnostics: every texture of the known class within the search's window behind `resource` (offset path, size, format,
// samples, array size, mips, bind flags). After NoteDevice. Present thread.
std::string DescribeTextures(std::uint64_t resource);
// What the last FindTargetTexture saw (diagnostics). Present thread.
std::string LastSearch();

} // namespace mevr::host::scope_capture_source
