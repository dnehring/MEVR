// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ID3D11DeviceContext;
struct ID3D11Resource;
struct ID3D11DepthStencilView;
struct D3D11_BOX;
namespace mevr::hook {
class Context;
}

// research/32, lever 4: the second eye's world passes as a replay of the first eye's D3D11 calls (LE1, Halcyon's
// Stereo 2 in the host; driven by plugin/RenderLevers, [Performance] sharePass = 2).
//
// The first scene render of a frame is recorded as it reaches the game's immediate context (its vtable, patched
// above Halcyon's draw hooks, so the game's own calls are what's recorded). The second render, instead of running the
// engine's passes again on the shared renderer (sharePass = 1), re-issues the recorded tail (from the world DPG's
// light targets on: the depth pre-pass, the passes, the finish) with the view's bytes substituted: every 16-byte
// chunk of a constant-buffer update equal to a first-eye view field that differs between the eyes (a matrix row or
// column, a position) becomes the second eye's. The replay goes through the vtable's next entries, so Halcyon's hooks
// see it as the second eye's rendering (its pass-0 capture keys on the pre-pass's depth clear).
//
// The eye diff (eyediff.request, tools/eye-diff.ps1): one frame recorded both ways (the first render, and the engine
// replay of the second), compared call by call: whether the two streams align, and which bytes of which constant
// buffers differ beyond what the substitution covers. The measurement of lever 4's hypothesis, before the replay is
// trusted. Log lines start with "draw replay:".
namespace mevr::host::draw_replay {

// The view fields that may differ between the eyes (RenderLevers' plain FSceneView ranges), concatenated per view.
struct ViewFields {
    std::vector<std::uint8_t> bytes;  // views x perView
    std::size_t perView = 0;
    int views = 0;
};

// Where, inside a view's captured bytes, the matrices the matrix substitution needs sit (SIZE_MAX: not captured):
// the translated view-projection (what the vertex shaders read), its inverse, and the pre-view translation.
// research/37, experiment 6: the engine folds each eye's pre-view translation into every draw's LocalToWorld, so the
// per-view matrix the shaders read is the same for both eyes and the eye lives in the per-draw constants; the
// replay moves it by replacing that matrix, wherever it is uploaded, with T(pvt2 - pvt1) x TVP2 (the first eye's
// geometry, the second eye's view), and its inverse to match.
struct ViewLayout {
    std::size_t translatedViewProjection = SIZE_MAX, invTranslatedViewProjection = SIZE_MAX, preViewTranslation = SIZE_MAX;
};
void SetViewLayout(ViewLayout const& layout);
// Whether the substitution carries the inverse matrix pair as well (default on; [Performance] replayInverse).
void SetInversePair(bool on);
// The draws stream `stream` (0 or 1) recorded this frame so far.
unsigned RecordedDraws(int stream);

// Render thread, from a Present, with the game's immediate context: hooks its vtable once Halcyon's draw hooks are
// in (so the recorder sits above them). True once hooked; the hooks then stay for the session (idle cost: one
// thread-local read per call). Call it only when a lever wants the recorder.
// `hooks`: the host's hook context, for the 19 entries an overlay restores to d3d11's own at every Present (every
// draw, clear, copy and dispatch: research/37, experiment 6); those are hooked inline on d3d11's functions instead,
// which nothing rewrites. Null: the vtable only (then taken back at every recording, and lost mid-frame).
bool Attach(ID3D11DeviceContext* context, hook::Context* hooks);
bool Attached();
// The game's immediate context once Attach saw it (null before): the render levers' flush after the first eye.
ID3D11DeviceContext* Context();

// Halcyon's draw hooks report every immediate-context call they see ([MEVR] seams in d3d_capture.cpp, through
// mevr_bridge.h): recorded when the recorder's own hooks didn't see the call (research/37, experiment 6).
void NoteDrawIndexed(ID3D11DeviceContext* c, unsigned indexCount, unsigned startIndex, int baseVertex);
void NoteDraw(ID3D11DeviceContext* c, unsigned vertexCount, unsigned startVertex);
void NoteDrawIndexedInstanced(ID3D11DeviceContext* c, unsigned indexCountPerInstance, unsigned instanceCount,
    unsigned startIndex, int baseVertex, unsigned startInstance);
void NoteDrawInstanced(ID3D11DeviceContext* c, unsigned vertexCountPerInstance, unsigned instanceCount,
    unsigned startVertex, unsigned startInstance);
void NoteDrawAuto(ID3D11DeviceContext* c);
void NoteUpdateSubresource(ID3D11DeviceContext* c, ID3D11Resource* resource, unsigned subresource, D3D11_BOX const* box,
    void const* data, unsigned rowPitch, unsigned depthPitch);
void NoteClearDepthStencilView(ID3D11DeviceContext* c, ID3D11DepthStencilView* view, unsigned flags, float depth,
    unsigned char stencil);

// Render thread. Records this thread's calls on the hooked context into stream 0 (the first eye's whole scene
// render) or 1 (the second eye's engine replay, for the diff). MarkTail: the replay starts here (the world DPG's
// light targets, right before the pre-pass); a stream's first mark counts.
void BeginRecording(int stream);
void EndRecording();
void MarkTail();

// Stream 0's tail can be re-issued: it was marked and holds nothing the replay can't repeat (a command list, a
// compute dispatch, an unsupported call, a large dynamic buffer discarded twice). `why` names what blocks it.
bool TailReplayable(std::string* why);

// Render thread: re-issues stream 0's tail with `first`'s differing view bytes replaced by `second`'s. 0 done; else
// a failure code (logged). The caller wraps it in its engine prologue and epilogue.
// `rightHalf`: the re-issued viewports, scissors and copies that address a target twice as wide as they are (the
// game's side-by-side backbuffer) are moved to its right half: the second eye's place (research/37).
int Replay(ViewFields const& first, ViewFields const& second, bool rightHalf = false);

// Streams 0 and 1 recorded for one frame: writes %LOCALAPPDATA%\MEVR\perf\<stamp>[-tag]-eyediff.txt and logs a
// summary. True if the tail aligned with the engine replay and the substitution covered every differing byte.
bool WriteDiff(ViewFields const& first, ViewFields const& second, std::string const& tag);

// Game thread (TestLoop's eyediff.request): the next paired frame records both ways and reports.
void RequestDiff(std::string const& tag);
bool DiffPending();
std::string TakeDiffTag();

// Render thread, at the end of each Present: frees the frame's streams and the references they hold.
void OnPresentEnd();

// Counters for the render levers' log line (reset each call): replays, their failures, the recorded calls and
// bytes per first eye, the replayed calls, substitutions per replay, frames whose tail wasn't replayable.
struct Stats {
    std::uint64_t replays = 0, failures = 0, recorded = 0, recordedCalls = 0, recordedBytes = 0, replayedCalls = 0,
                  substitutions = 0, notReplayable = 0, replayedDraws = 0, shiftedRects = 0, matrixSubstitutions = 0,
                  rehooks = 0, notedByHalcyon = 0, seenByOurs = 0;
    std::uint64_t matrixHits[4] = {};   // TVP, TVP transposed, inverse, inverse transposed
    std::string lastBlocker;
};
Stats TakeStats();

// {"attached": bool, "replays": N, "replayFailures": N}
std::string StatsJson();

} // namespace mevr::host::draw_replay
