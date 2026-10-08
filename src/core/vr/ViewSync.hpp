#pragma once

#include <openxr/openxr.h>

#include <optional>

// Hand-off between the render thread (owns OpenXR) and the game thread (owns the camera).
//
// The game renders frame N with whatever head pose it read at camera-update time; the image of
// frame N reaches Present later. For the runtime's reprojection to keep the image stable, each
// submitted image must carry the exact pose it was rendered with, hence the queue. UE3 ticks
// once per Present, so first-in-first-out pairs them up.
namespace mevr::vr::view_sync {

// Render thread -> game thread: the most recent predicted head pose (LOCAL space).
void PublishHead(XrPosef const& pose);
std::optional<XrPosef> LatestHead();

// Render thread -> game thread: the horizontal field of view (degrees, symmetric) a single image
// needs to cover both eyes of the headset.
void PublishHeadsetFov(float horizontalDegrees);
std::optional<float> HeadsetFov();

// Game thread -> render thread: the view each game frame was rendered with, in order.
struct RenderedView {
    XrPosef head;
    float fovXDegrees;  // the game's horizontal field of view for that frame
};
void PushRendered(RenderedView const& view);
std::optional<RenderedView> PopRendered();

// Forget everything (VR off, mode change, session restart).
void Reset();

} // namespace mevr::vr::view_sync
