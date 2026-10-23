// SPDX-License-Identifier: MIT
#pragma once

#include <string>

struct ID3D11DeviceContext;

namespace LESDK {
class Initializer;
}

// Render-thread levers and accounting for LE1's VR performance (research/22, "What else can go"). LE1, with
// Halcyon's renderer in the host.
//
// Hooks the scene render (Halcyon's SceneRenderB, FSceneRenderer::Render, 0x76EE20) to count and time the
// scene renders of each frame (between Presents), and the Umbra commander's command() (0x8C34C0, BioWare's side
// of the occlusion resolve) to time the resolve's parts per render (instances, occluder flush and queries, query
// results) and to switch its wait for query results off. Live switches in [Performance] of ASI\LE1MEVR.ini,
// read once a second on the game thread:
//   umbraQueryWait = 1 the game's own: the resolve spins for a needed query result until "10 ms" after its start
//                      (a float stamp of 2 s precision: the wait is anything up to a second when it happens)
//                    0 never waits: an unready query counts as visible (a few more draws, no stall)
//   pCoresOnly   = 1   the game's threads only on the CPU's performance cores (hybrid CPUs, e.g. the 12900K's
//                      P-cores); 0 leaves the affinity as the game had it
//   sharePass    = 1   EXPERIMENTAL (research/32, lever 1): a frame's second scene render (Stereo 2's second
//                      eye) runs on the first one's renderer: its deletion (0x4ED700) is held back, the second
//                      render's view matrices are copied into it, and SceneRenderB's Umbra branch after
//                      InitViews runs again (pre-pass included), so the second eye does no visibility of its
//                      own. Only between compatible renders (same scene, target, flags, view states, rects);
//                      an exception turns it off for the session. 0 = the game's own
//                  2   EXPERIMENTAL (research/32, lever 4): as 1, but the second render re-issues the first one's
//                      recorded D3D11 calls with the view's bytes substituted (plugin/DrawReplay) instead of
//                      running the engine's passes again; a frame whose recording can't be replayed, or a failure
//                      (then for the session), runs as 1. The eye diff (eyediff.request, tools/eye-diff.ps1)
//                      compares one frame's two streams and reports what the substitution misses.
// (Hooks: the scene render, the renderer's deleting destructor, the depth pre-pass, the DPG light targets (the
// replay's mark); with sharePass = 2 or a diff wanted, the immediate context's vtable.)
// Log lines start with "render levers:": the settings on change, and every 10 s the scene renders per frame,
// each render's average time by its place in the frame with its Umbra resolve's parts, and the draw replay's counts.
namespace mevr::host::render_levers {

// At attach (exe version checked): installs the hooks. False if the first two failed (the levers are then off).
bool Install(::LESDK::Initializer& init);

// Game thread, every engine tick: re-reads the switches once a second, applies the affinity, logs.
void Tick();

// Render thread, at the end of each Present, with the game's immediate context (may be null): closes the frame's
// render count; attaches the draw recorder when a lever wants it.
void OnPresentEnd(ID3D11DeviceContext* context);

// {"umbraQueryWait": N, "pCoresOnly": N, "sharePass": N, ..., "hooked": bool}, for the perf probe's JSON.
std::string SettingsJson();

// [Performance] perEyeViewState: Halcyon renders each Stereo 2 eye through its own scene view state in gameplay
// (MevrBridge::PerEyeViewState; research/32).
bool PerEyeViewState();

} // namespace mevr::host::render_levers
