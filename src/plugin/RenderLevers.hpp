#pragma once

#include <string>

namespace LESDK {
class Initializer;
}

// Render-thread levers and accounting for LE1's VR performance (research/22, "What else can go"). LE1, with
// Halcyon's renderer in the host.
//
// Hooks the scene render (Halcyon's SceneRenderB, FSceneRenderer::Render, 0x76EE20) to count and time the
// scene renders of each frame (between Presents), and the depth pre-pass (0x76C1D0) to skip, on request, the
// call that pre-draws the whole visible set (from 0x8BEDB0, returning to 0x8BF11B): the base pass then draws
// without early-Z. Live switches in [Performance] of ASI\LE1MEVR.ini, read once a second on the game thread:
//   depthPrepass = 2   the game's own: every scene render pre-draws its visible set
//                  1   only the first scene render of each frame (the second eye's goes)
//                  0   none
//   pCoresOnly   = 1   the game's threads only on the CPU's performance cores (hybrid CPUs, e.g. the 12900K's
//                      P-cores); 0 leaves the affinity as the game had it
//   sharePass    = 1   EXPERIMENTAL (research/32, lever 1): a frame's second scene render (Stereo 2's second
//                      eye) runs on the first one's renderer: its deletion (0x4ED700) is held back, the second
//                      render's view matrices are copied into it, and SceneRenderB's Umbra branch after
//                      InitViews runs again (pre-pass included), so the second eye does no visibility of its
//                      own. Only between compatible renders (same scene, target, flags, view states, rects);
//                      an exception turns it off for the session. 0 = the game's own
// (Hooks: the scene render, the renderer's deleting destructor, the depth pre-pass.)
// Log lines start with "render levers:": the settings on change, and every 10 s the scene renders per frame,
// each render's average time by its place in the frame, and the pre-pass calls and skips.
namespace mevr::host::render_levers {

// At attach (exe version checked): installs both hooks. False if either failed (the levers are then off).
bool Install(::LESDK::Initializer& init);

// Game thread, every engine tick: re-reads the switches once a second, applies the affinity, logs.
void Tick();

// Render thread, at the end of each Present: closes the frame's render count.
void OnPresentEnd();

// {"depthPrepass": N, "pCoresOnly": N, "hooked": bool}, for the perf probe's JSON.
std::string SettingsJson();

} // namespace mevr::host::render_levers
