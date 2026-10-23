#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ID3D11DeviceContext;

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

// Render thread, from a Present, with the game's immediate context: hooks its vtable once Halcyon's draw hooks are
// in (so the recorder sits above them). True once hooked; the hooks then stay for the session (idle cost: one
// thread-local read per call). Call it only when a lever wants the recorder.
bool Attach(ID3D11DeviceContext* context);
bool Attached();

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
int Replay(ViewFields const& first, ViewFields const& second);

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
                  substitutions = 0, notReplayable = 0;
    std::string lastBlocker;
};
Stats TakeStats();

// {"attached": bool, "replays": N, "replayFailures": N}
std::string StatsJson();

} // namespace mevr::host::draw_replay
