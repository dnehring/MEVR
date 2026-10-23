// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>

// The view an external renderer (LE1: Halcyon's, in the host ASI) built this frame, relative to
// the game camera it starts from, so the mod can map tracking-space poses (the head, the
// controllers) into the world: engine::FromCameraView with the camera's own location and
// rotation (CameraCache.POV). Published on the renderer's frame thread, read anywhere.
// Engine-agnostic; angles in Unreal units (65536 = 360 degrees).
namespace mevr::vr::host_view {

struct State {
    // A head-tracked 3D view of the world is on screen (not a menu, flat screen or lost tracking).
    bool valid = false;
    // The view turns with the head (render-side head look).
    bool headLook = false;
    // The head turns the game camera itself (Halcyon's head aim writes ControlRotation).
    bool headAim = false;
    // The head pose the view was built for, in the controllers' tracking space (roll removed: the
    // view never rolls).
    XrPosef head{};
    // How the view was turned away from the game camera, in the camera's own frame: yaw (+ right)
    // then pitch (+ up).
    int lookPitch = 0;
    int lookYaw = 0;
    // How far the view was moved from the game camera, along the camera's axes (Unreal units).
    float offsetForward = 0.0f;
    float offsetRight = 0.0f;
    float offsetUp = 0.0f;
    // Unreal units the view moves per meter of head movement: the scale that keeps a still hand
    // still in the world while the head moves.
    float unitsPerMeter = 50.0f;
    // Diagnostics: the right axis (world) of the last game view the renderer built, before head
    // look, if it has seen one. Should match the game camera's own right axis.
    bool baseRightValid = false;
    float baseRight[3] = {0.0f, 0.0f, 0.0f};
    std::uint64_t sequence = 0;  // increments with every published sample
    std::uint64_t timeMs = 0;    // GetTickCount64 when published
};

void Publish(State const& state);
// Stores a sample published elsewhere (by the host, for the mod), keeping its sequence and time.
void Mirror(State const& state);
// The latest sample; `valid` is forced false if it's older than `maxAgeMs` (VR stopped).
State Latest(std::uint64_t maxAgeMs = 250);
void Reset();

} // namespace mevr::vr::host_view
