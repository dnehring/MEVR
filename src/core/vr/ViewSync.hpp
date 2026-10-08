#pragma once

#include <openxr/openxr.h>

#include <cstddef>
#include <cstdint>
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

// Game thread -> render thread: the view each game frame was rendered with.
struct RenderedView {
    XrPosef head;
    float fovXDegrees;  // the game's horizontal field of view for that frame
    std::uint64_t engineFrame = 0;  // assigned by PushRendered
};
void PushRendered(RenderedView view);
// The view of the frame being presented: the newest queued one (older entries are dropped).
// Correct because of the lockstep below: the game thread doesn't start frame N+1 until frame N
// has been presented. `depth` reports how many entries were queued (1 = in sync).
std::optional<RenderedView> TakeForPresent(std::size_t& depth);

// Lockstep (vrframework guide 07: engine and presenter frames must not drift).
// Render thread, after submitting a frame:
void NotifyPresented();
// Game thread, after the engine tick that pushed a view: blocks until that frame has been
// presented or the timeout passes. Returns false on timeout.
bool WaitForPresent(std::uint32_t timeoutMs);
// Frames pushed but not presented yet: nothing to wait for when this is 0.
bool HasPendingFrame();

// Forget everything (VR off, mode change, session restart).
void Reset();

} // namespace mevr::vr::view_sync
