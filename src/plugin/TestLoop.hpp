#pragma once

#include "core/vr/Controllers.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Texture2D;

// The headset test loop (CLAUDE.md, "Test loop"): lets Claude check VR changes without the player
// describing what they see. LE1, with Halcyon's renderer in the host.
//
// Eye snapshots: what each eye shows, saved as PNGs to %LOCALAPPDATA%\MEVR\snapshots\<stamp>-left.png
// and -right.png (the world with our HUD panel, reticle, squad weapon menu and Halcyon's menu drawn
// in, as the compositor shows them), the layers on their own (-left-world.png, -hud.png, -squad.png,
// -menu.png), and
// <stamp>.json (head, controllers, aim ray, the mod's arm IK and weapon state, the layers). The
// swapchain images are copied into staging textures on the Present thread as they are released,
// mapped two or more frames later (no stall), and converted and written on a worker thread.
//
// Pose recording and replay: the head's eye views and both controllers (poses and inputs) per frame
// to %LOCALAPPDATA%\MEVR\recordings\<stamp>.mevrrec (core/vr/PoseRecording), and back as if they were
// live: the controllers where our actions are synced (HalcyonBridge's OnFrameBegun), the head where
// Halcyon locates its views (the wrapped xrLocateViews, keeping the runtime's FOVs). Replayed buttons
// and sticks only when asked (inputs=1), so a replay doesn't fire weapons by default.
//
// Triggers: End (snapshot) and Home (recording on/off) while the game has focus; both stick clicks
// tapped together (snapshot: down and up within 300 ms; held 400 ms they open Halcyon's menu instead,
// HalcyonBridge with gamepad::UpdateStickChord); the menu's VR hands tab; and request files in
// %LOCALAPPDATA%\MEVR, polled once a second, read and deleted:
//   snapshot.request  [count=N] [interval=seconds] [delay=seconds] [tag=word]
//   record.request    [start|stop|toggle] [seconds=N]
//   replay.request    <path.mevrrec | latest | stop> [loop=1] [inputs=1]
// %LOCALAPPDATA%\MEVR\testloop.status (JSON, rewritten once a second while it changes) says what is
// recording or replaying and the last snapshot's JSON.
namespace mevr::host::testloop {

// ---- Present thread (Halcyon's frame loop) ----------------------------------------------------------
// A new session: forget every swapchain seen (their handles die with the old session).
void OnSessionCreated(XrSession session);
// After xrBeginFrame and the live controller sync, before the sample is published: records the frame,
// or replaces `state` with the replay's (poses; buttons and sticks too with inputs=1), and runs the
// snapshot capture. `focused`: the session has input focus.
void OnFrameBegun(XrSession session, XrSpace appSpace, XrTime displayTime, bool focused,
                  vr::controllers::State& state);
// Our own quads (HalcyonOverlays), right before a swapchain image is released: Halcyon's go through
// the wrapped xrReleaseSwapchainImage instead.
void OnImageWritten(XrSwapchain swapchain, ID3D11Texture2D* image);
// A swapchain whose images never change once filled (the reticle): its pixels (premultiplied
// sRGB RGBA8, 0xAABBGGRR), drawn into snapshots instead of a copy.
void SetStaticImage(XrSwapchain swapchain, std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t> pixels);
// Something else that wants one swapchain's image right before Halcyon releases it (the gun sights'
// scope copies the eye image it magnifies: plugin/SightLayers). One at a time; `tap` nullptr (or a null
// swapchain) clears it. Present thread.
using ReleaseTap = void (*)(XrSwapchain swapchain, ID3D11Texture2D* image);
void SetReleaseTap(XrSwapchain swapchain, ReleaseTap tap);
// Names a swapchain's layer in snapshots ("hud", "reticle"); ForgetSwapchain when it's destroyed.
void NameSwapchain(XrSwapchain swapchain, char const* name);
void ForgetSwapchain(XrSwapchain swapchain);
// Halcyon's menu quad this frame (MevrBridge::OnMenuQuad): the quad layer that matches is "menu".
// `position` as submitted; `headLocked`: in VIEW space (placed from the rendered head in snapshots),
// else in the app space (world-locked panels: the eyes' space, placed as is).
void NoteMenuQuad(float const position[3], bool headLocked, float width, float height);

// ---- Any thread ----------------------------------------------------------------------------------------
// A replay drives the controllers (and with inputs=1, the buttons and sticks).
bool Replaying();
bool ReplayDrivesInputs();
bool Recording();

struct SnapshotRequest {
    int count = 1;
    double intervalSeconds = 0.0;
    double delaySeconds = 0.0;
    std::string tag;          // appended to the file names
    char const* trigger = "key";
};
void RequestSnapshot(SnapshotRequest const& request);
// `command`: -1 toggle, 0 stop, 1 start. `seconds` > 0: stop by itself after that long.
void RequestRecording(int command, double seconds = 0.0);
// Loads a recording (empty path or "latest": the newest in the recordings folder) and plays it from
// the next frame. Stops a recording in progress.
bool StartReplay(std::wstring const& path, bool loop, bool inputs, std::string* error = nullptr);
void StopReplay();

struct Status {
    bool recording = false;
    double recordingSeconds = 0.0;
    bool replaying = false;
    bool replayLoop = false;
    bool replayInputs = false;
    double replaySeconds = 0.0;
    double replayDuration = 0.0;
    std::string replayPath;
    std::string lastRecording;
    std::string lastSnapshot;  // the last snapshot's JSON
    int snapshotsPending = 0;
    bool framesRunning = false;  // a headset frame ran in the last second
};
Status GetStatus();

// ---- Game thread -----------------------------------------------------------------------------------
// After each engine tick: the keys, the request files (once a second), the status file.
void Tick(float deltaSeconds);

// ---- The mod's state for snapshots (HostServices v8) ----------------------------------------------
// True while a snapshot is pending: the mod then publishes its status each tick.
bool ModStatusWanted();
// A JSON object (arm IK, hand aim, weapon), copied.
void SetModStatus(char const* json);

} // namespace mevr::host::testloop
