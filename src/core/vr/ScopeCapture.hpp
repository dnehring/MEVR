// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <functional>
#include <optional>

// The sniper scope's real zoomed render (research/39): instead of magnifying the other eye's image 4x (about 130
// source pixels stretched over the picture), the engine's own SceneCapture2DComponent renders the scope's field of view
// into a small render target while the scope's picture is up, and the host's scope picture samples that.
//
// The mod (game/ScopeCapture) owns the capture: it spawns a SceneCapture2DActor once per level, makes its
// TextureRenderTarget2D, enables it only while the aiming eye is behind the scope, points it along the sight line each
// tick (SetView, then ForceUpdateComponents so the pose reaches the render thread before the next Draw) and publishes
// what the host needs here (HostServices v20, setScopeCapture). The host (plugin/ScopeCaptureSource) finds the render
// target's ID3D11Texture2D on the render thread and SightLayers samples it in place of the eye copy, with the pose the
// capture was rendered from (the Draw's pair arm, NoteDrawArm/SequenceForArm). Anything missing or stale: the eye copy,
// exactly as before.
//
// Engine-agnostic: no engine or D3D types. Tracking space as core/vr/Sights (OpenXR axes, Halcyon's app space).
namespace mevr::vr::scope_capture {

// ---- Settings ([Sights] scopeCapture* in <GAME>MEVR.ini, parsed by the mod) ------------------------------------------
struct Settings {
    bool enabled = true;           // scopeCapture: on since its cost was measured, 2026-11-04 (research/39 section 12)
    int size = 1024;               // scopeCaptureSize: the render target's pixels across (square), 256..2048
    float rate = 150.0f;           // scopeCaptureRate: SceneCaptureComponent.FrameRate, renders a second at most: 150 renders
                                   // every frame up to 144 Hz but never twice in one (Halcyon's two Draws of a frame are a few
                                   // ms apart, whichever clock the engine compares: research/39 section 3)
    bool postProcess = true;       // scopeCapturePost: the player's post-process chain (tonemap, bloom) on the capture; known
                                   // issue: it puts a black speckle in the capture's dark areas (research/39 section 12), kept
                                   // for now (the user, 2026-11-04) because 0 loses the game's colour grade
    bool floatTarget = true;       // scopeCaptureFormat = float (PF_FloatRGB, linear) | rgba8 (PF_A8R8G8B8, gamma)
    float margin = 0.25f;          // scopeCaptureMargin: the capture's field past the picture's (share of its half-angle)
    float nearUnits = 20.0f;       // scopeCaptureNear: the near plane this far past the muzzle (game units)
    float farUnits = 0.0f;         // scopeCaptureFar: 0 = the engine's infinite far plane [verify]
    float holdSeconds = 0.5f;      // keep it enabled this long after the eye leaves the scope (no reattach per blink)
};

// EPixelFormat (Engine.Texture's enum, LE1): the two the capture may use.
inline constexpr std::int32_t kPixelFormatA8R8G8B8 = 2;
inline constexpr std::int32_t kPixelFormatFloatRGB = 9;  // execCreate takes only 2, 3 (G8) and 9: FloatRGBA (10) returns None (research/47)

// The capture's full field of view (degrees; square) for a scope picture that looks `pictureDeg` across at `zoom`: the
// picture shows `pictureDeg / zoom` of the world (true magnification across it: tan(half) / zoom), plus `margin` of that
// half-angle's tangent so an eye off the sight line (the reticle off centre) still samples inside. Clamped 0.5..90.
float FovDeg(float pictureDeg, float zoom, float margin);
// tan(fovDeg / 2).
float TanHalf(float fovDeg);
// The picture's apparent size for the capture's field: the virtual eyepiece's set field, or for the physical lens
// (fieldDeg 0) the lens seen from 5 cm (wider than anything the pupil shows from further back).
float PictureDeg(float scopeFieldDeg, float lensRadiusM);
// The near plane (game units from the sight's centre, where the capture sits): past the muzzle by `extraUnits`, so the
// gun's own model (the scope tube, the barrel) never fills the picture. At least 10.
float NearPlaneUnits(float sightToMuzzleUnits, float extraUnits);

// The capture camera's orientation in tracking space: looking along `forward` (OpenXR -Z), with +Y toward `up`.
// Nullopt if `forward` is zero or runs along `up`.
std::optional<XrQuaternionf> CameraOrientation(XrVector3f const& forward, XrVector3f const& up);

// ---- What the mod publishes each tick (mod -> host, HostServices::setScopeCapture) ----------------------------------
enum State : std::int32_t {
    kOff = 0,        // no capture (setting off, no scope, the eye not behind it, or a failure): the host uses the eye copy
    kCapturing = 1,  // enabled and pointed along the sight line this tick
};

struct Capture {
    std::uint32_t size = sizeof(Capture);  // the mod's struct size: the host ignores a mismatch
    std::int32_t state = kOff;
    std::int32_t weapon = -1;
    std::int32_t pixelFormat = 0;      // EPixelFormat of the render target
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t resource = 0;        // the render target's FTextureResource* (UTexture.Resource): engine memory
    std::uint64_t generation = 0;      // changes whenever `resource` may have (a new render target)
    std::uint32_t enabledTicks = 0;    // ticks since it was enabled (its first renders aren't there yet)
    float tanHalf = 0.0f;              // the square frustum's half-angle tangent
    float zoom = 1.0f;
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};  // tracking space: where it renders from (looks -Z)
    std::uint64_t sequence = 0;        // set by Publish
    std::uint64_t timeMs = 0;          // GetTickCount64 at Publish
};

// Any thread. Keeps the last kHistory publishes for BySequence.
inline constexpr int kHistory = 32;
void Publish(Capture const& capture);
// The latest, its state forced to kOff when older than `maxAgeMs` or published by a mod with another size.
Capture Latest(std::uint64_t maxAgeMs = 250);
// A publish still in the history by its sequence (any state), or nullopt.
std::optional<Capture> BySequence(std::uint64_t sequence);
std::uint64_t LatestSequence();
void Reset();
// How many times Reset ran (the mod unloaded or shut its capture down): the host's view of a render target's texture
// is dropped when this changes, since a reloaded mod counts its generations from 1 again and a new render target may
// get a freed one's address.
std::uint64_t Resets();

// Which publish a Draw renders the capture with (host). The mod points the capture at tick N and forces the update out,
// so the next Draw (tick N+1, before the mod's tick N+1) renders publish N: the newest when that Draw's main pass latches
// its pair arm (Halcyon's [POSEEXACT] arm, game thread). The submit knows the arm of the pair it presents, and so which
// pose the capture in the texture was rendered from. A small ring (the last 32 arms).
void NoteDrawArm(std::uint64_t arm);
std::optional<std::uint64_t> SequenceForArm(std::uint64_t arm);

// ---- Finding the D3D texture behind the render target (host, render thread) ------------------------------------------
// The engine's FTextureResource holds RHI references (pointers to the RHI's own texture objects), which hold the
// ID3D11Texture2D. Without Ghidra's offsets the host searches a bounded neighbourhood: the resource's first `firstSpan`
// bytes for a pointer, then each pointee's first `secondSpan` bytes (and the pointer itself) for an object that
// `isTexture` accepts (a vtable in d3d11.dll, QueryInterface for ID3D11Texture2D, the render target's size). `read`
// returns the 8 bytes at an address, or nullopt (unreadable). The first match in address order wins; a previously
// found path (`hint`) is tried first.
struct TexturePath {
    std::int32_t first = -1;   // byte offset in the resource of the RHI reference
    std::int32_t second = -1;  // byte offset in its pointee of the D3D texture; -1: the pointer itself is the texture
    std::uint64_t texture = 0; // the object found
    bool Valid() const { return first >= 0; }
};
TexturePath FindTexture(std::uint64_t resource, std::function<std::optional<std::uint64_t>(std::uint64_t)> const& read,
    std::function<bool(std::uint64_t)> const& isTexture, TexturePath const& hint = {}, std::int32_t firstSpan = 0x100,
    std::int32_t secondSpan = 0x80);

} // namespace mevr::vr::scope_capture
