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

// Render thread -> game thread: half the distance between the headset's eyes (meters).
void PublishHalfIpd(float meters);
std::optional<float> HalfIpd();

// Game thread -> render thread: the view each game frame was rendered with.
struct RenderedView {
    XrPosef pose;  // where the image was rendered from (the head, or one eye; LOCAL space)
    float fovXDegrees;  // the game's horizontal field of view for that frame
    int eye = -1;  // -1: mono (both eyes); 0 / 1: left / right eye only (alternate-eye stereo)
    std::uint64_t engineFrame = 0;  // assigned by PushRendered
};
void PushRendered(RenderedView view);
// A second view for the same game frame replaces the first (one queue entry per frame, so each
// Present still consumes exactly one).
void ReplaceNewest(RenderedView view);
// Frames that draw no player view (loading screens, movies, full-screen GUI) aren't queued: the
// output uses the latest of these, head-locked, whenever no view is queued. Never waited on.
// Cleared by the next PushRendered.
void PublishFallback(RenderedView view);
std::optional<RenderedView> Fallback();
// The view of the frame being presented: the oldest one not yet presented. Each Present
// consumes exactly one view (NotifyPresented), so after a lockstep timeout, when the game has
// already pushed the next frame, the image still gets its own view. UE3's game thread is at most
// one frame ahead of the render thread, so anything beyond two queued views is stale and dropped.
// `depth` reports how many were queued (1 = in sync, 2 = game one frame ahead).
std::optional<RenderedView> PeekForPresent(std::size_t& depth);

// Lockstep (vrframework guide 07: engine and presenter frames must not drift).
// Render thread, after every game Present (whatever path it took): consumes the oldest view
// and releases the game thread waiting for it.
void NotifyPresented();
// Game thread, after the engine tick that pushed a view: blocks until that frame has been
// presented or the timeout passes. Returns false on timeout.
bool WaitForPresent(std::uint32_t timeoutMs);
// Frames pushed but not presented yet: nothing to wait for when this is 0.
bool HasPendingFrame();
// Game Presents so far (any thread): lets the game thread see whether presenting has stopped.
std::uint64_t PresentCount();
// Drift recovery (guide 07): a frame that pushed a view but never presented leaves a stale view
// at the front, after which every image gets the previous frame's view and every lockstep wait
// times out. The game thread calls this after several timeouts in a row: drops the oldest view.
void DropOldest();

// Forget everything (VR off, mode change, session restart).
void Reset();

} // namespace mevr::vr::view_sync
