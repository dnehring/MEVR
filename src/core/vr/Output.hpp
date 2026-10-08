#pragma once

struct IDXGISwapChain;

// Sends the game's frames to the headset. Runs on the render thread from the host's Present
// callback; the setters are safe from any thread.
namespace mevr::vr::output {

enum class Mode {
    // M3: the game camera follows the head (vr/view_sync); each frame is submitted as a
    // projection layer carrying the head pose it was rendered with. Mono for now.
    HeadTracked,
    // M2: the game on a flat panel 2 m ahead. Later the fallback for conversations and cutscenes.
    Screen,
};

// Render thread, once per game frame, before Present.
void OnPresent(IDXGISwapChain* swapChain);
// Releases the XR session and GPU resources. Not concurrent with OnPresent.
void Shutdown();

// Disabling tears the XR session down on the next frame (the game keeps running on the
// monitor); enabling starts a new one. Quitting from the headset menu disables it.
void SetEnabled(bool enabled);
bool IsEnabled();

void SetMode(Mode mode);
Mode GetMode();
// While set, frames go to the virtual screen whatever the mode (e.g. during conversations and
// cutscenes); the chosen mode comes back when it's cleared. Any thread.
void SetScreenOverride(bool screen);

// True while the game camera should follow the head: VR running in HeadTracked mode.
bool WantsHeadTracking();
// True while VR is on and its headset session is running, in either mode. Any thread.
bool Running();
// True while the headset is actually showing our frames (session VISIBLE or FOCUSED). When the
// headset is off or the system menu is up, the runtime throttles frames, and the game thread
// shouldn't wait for them.
bool ShowsFrames();
// False after alternate-eye resources failed for this session: the game should render mono.
bool StereoAvailable();

// Logs and resets frame-pacing counters (queue depth at present, frame interval spread).
void LogStats();

} // namespace mevr::vr::output
