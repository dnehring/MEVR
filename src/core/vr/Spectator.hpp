// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>

// The spectator camera (research/49): a mono 16:9 render of the VR experience for stills and video, from a smoothed
// head pose or a pose set from the console, with our quad layers composited in, shown in the game's desktop window
// in place of Halcyon's one-eye mirror.
//
// The mod (game/Spectator) owns the capture: a SceneCapture2DActor with a 16:9 render target, pointed each tick at the
// camera's pose and published here (HostServices v24, setSpectator). The host (plugin/Spectator) finds the render
// target's D3D11 texture, draws the frame's quad layers over it from the camera's pose and puts the result in the
// backbuffer before each Present. This file is what both share, plus the camera's math: engine-agnostic, unit-tested
// (tests/Main.cpp TestSpectator). Tracking space as core/vr/ScopeCapture (OpenXR axes, Halcyon's app space).
namespace mevr::vr::spectator {

// ---- Settings ([Spectator] in <GAME>MEVR.ini, parsed by the mod) --------------------------------------------------
enum Camera : std::int32_t {
    kHead = 0,  // the head's world pose, smoothed (video that doesn't shake)
    kFree = 1,  // a pose set from the console (`mevr spectator at`, `follow`): a replay filmed from outside
};

struct Settings {
    bool enabled = false;      // enabled: off by default; off it costs nothing (no actor, no render, no composite)
    int width = 1920;          // width, height: the render target (16:9 is expected; 3840x2160 allowed)
    int height = 1080;
    float fovDeg = 85.0f;      // fovDeg: the horizontal field of view, degrees
    Camera camera = kHead;     // camera = head | free
    float smoothS = 0.25f;     // smoothS: the head camera's critically damped follow time (0: the head exactly)
    bool floatTarget = false;  // format = rgba8 (the game's own display-ready SDR pass) | float (FloatRGB, linear)
    float rate = 150.0f;       // rate: SceneCaptureComponent.FrameRate (a re-pointed capture renders every frame anyway)
    bool postProcess = true;   // post: the player's post-process chain on the capture
    float nearUnits = 10.0f;   // near: the near plane, game units (the player's own is 10)
    float farUnits = 0.0f;     // far: 0 = the engine's
    bool mirror = true;        // mirror: the desktop window shows the composited picture instead of one eye
    bool layers = true;        // layers: our quad layers (HUD panel, reticle, sights, ...) composited in
    bool viewActor = true;     // viewActor: the head camera's capture sees what the eyes see (the probe's view actor
                               // set to the pawn: the head hidden, bOnlyOwnerSee parts shown)
    bool hideHead = true;      // hideHead: the head camera hides the pawn's head meshes when the view actor can't be set
};

// EPixelFormat (Engine.Texture's enum, LE1): the two the capture may use (research/47: execCreate takes 2, 3 and 9).
inline constexpr std::int32_t kPixelFormatA8R8G8B8 = 2;
inline constexpr std::int32_t kPixelFormatFloatRGB = 9;

// ---- The camera's field ---------------------------------------------------------------------------------------------
// tan of half the horizontal field; the vertical one from the picture's aspect (a symmetric frustum).
float TanHalfX(float fovDeg);
float TanHalfY(float fovDeg, int width, int height);
// The same as an OpenXR FOV (angleLeft negative, angleDown negative), for the host's composite (core/vr/LayerComposite).
XrFovf Fov(float fovDeg, int width, int height);

// ---- The head camera's follow ---------------------------------------------------------------------------------------
// A critically damped spring on the position and the orientation (Game Programming Gems 4's smoothing, the one
// Unity calls SmoothDamp): no overshoot, settles within a few `smoothS`. The first Step snaps to the target.
struct Follow {
    bool valid = false;
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrVector3f velocity{0.0f, 0.0f, 0.0f};         // m/s
    XrVector3f angularVelocity{0.0f, 0.0f, 0.0f};  // rad/s, a rotation vector in the pose's parent frame
};
void Snap(Follow& follow, XrPosef const& target);
// `dtS` clamped to 0..0.25 s; `smoothS` <= 0 snaps.
void Step(Follow& follow, XrPosef const& target, float dtS, float smoothS);

// Quaternion helpers the follow and the mod share (OpenXR xyzw; a * b applies b first).
XrQuaternionf Multiply(XrQuaternionf const& a, XrQuaternionf const& b);
XrQuaternionf Conjugate(XrQuaternionf const& q);
XrQuaternionf Normalize(XrQuaternionf const& q);
XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v);
// The rotation vector (axis times angle, radians, angle in [0, pi]) of q, and back.
XrVector3f Log(XrQuaternionf const& q);
XrQuaternionf Exp(XrVector3f const& rotationVector);
// The angle between two orientations, radians.
float AngleBetween(XrQuaternionf const& a, XrQuaternionf const& b);

// ---- What the mod publishes each tick (mod -> host, HostServices::setSpectator) ------------------------------------
enum State : std::int32_t {
    kOff = 0,        // no capture (setting off, no player, a failure): the desktop window keeps Halcyon's mirror
    kCapturing = 1,  // enabled and pointed this tick
};

struct Frame {
    std::uint32_t size = sizeof(Frame);  // the mod's struct size: the host ignores a mismatch
    std::int32_t state = kOff;
    std::int32_t camera = kHead;
    std::int32_t pixelFormat = 0;      // EPixelFormat of the render target
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t resource = 0;        // the render target's FTextureResource* (UTexture.Resource): engine memory
    std::uint64_t generation = 0;      // changes whenever `resource` may have (a new render target)
    std::uint32_t enabledTicks = 0;    // ticks since it was enabled (its first renders aren't there yet)
    float tanHalfX = 0.0f;             // the frustum's half-angle tangents (symmetric)
    float tanHalfY = 0.0f;
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};  // tracking space: where it renders from (looks -Z)
    std::uint32_t flags = 0;           // kFlag* below
    std::uint64_t sequence = 0;        // set by Publish
    std::uint64_t timeMs = 0;          // GetTickCount64 at Publish
};
inline constexpr std::uint32_t kFlagMirror = 1u;  // the desktop window shows the picture
inline constexpr std::uint32_t kFlagLayers = 2u;  // our quad layers are composited in

// Any thread. Keeps the last kHistory publishes for BySequence (as core/vr/ScopeCapture).
inline constexpr int kHistory = 32;
void Publish(Frame const& frame);
// The latest, its state forced to kOff when older than `maxAgeMs` or published by a mod with another size.
Frame Latest(std::uint64_t maxAgeMs = 250);
std::optional<Frame> BySequence(std::uint64_t sequence);
std::uint64_t LatestSequence();
void Reset();
std::uint64_t Resets();
// Which publish a Draw renders the capture with (host; research/39 section 5): the publish current when the Draw's
// main pass latched its pair arm.
void NoteDrawArm(std::uint64_t arm);
std::optional<std::uint64_t> SequenceForArm(std::uint64_t arm);

} // namespace mevr::vr::spectator
