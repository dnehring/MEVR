#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams where our host plugs into Halcyon's code.
// Declared without the OpenXR SDK headers, because xr_types.h defines the same names; the
// implementation is in src/plugin/HalcyonBridge.cpp. Handles are passed as void*.

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

namespace MELEVR::MevrBridge
{
// The statically linked OpenXR loader's xrGetInstanceProcAddr (upstream loads openxr_loader.dll,
// which our ASI doesn't ship). Our controller actions must come from the same loader instance.
void* StaticXrGetInstanceProcAddr() noexcept;

// The session and its spaces exist (Halcyon's XR thread, before xrBeginSession; published to the
// Present thread through its g_ready): create and attach our controller actions.
void OnSessionCreated(void* instance, void* session) noexcept;

// The game's window (Present thread, once known): brought to the foreground when the headset
// session gains input focus, since UE3 ignores input without window focus.
void SetGameWindow(HWND window) noexcept;

// After xrBeginFrame (Present thread): sync the controller actions and publish their state, with
// poses located in `space` (Halcyon's recentered LOCAL app space, the head pose's space).
void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept;

// From Halcyon's XInput hooks, right after the real read: merges the motion controllers into pad
// 0 (Touch -> Xbox mapping) before Halcyon's own pad handling (menu block, move-follows-head).
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept;

// Present thread, once per frame after the view for the next game frame is set up (head look,
// view offset): publishes how that view relates to the game camera, so our mod can map the
// controllers into the world (hand aim, arm IK). `worldView`: a head-tracked 3D view (head
// valid, no menu, not the flat screen). `head`: the roll-removed head pose the view is built for
// (orientation xyzw, position; Halcyon's app space). Offsets are the view offset in UE units
// along the camera's right/up/forward; `unitsPerMeter` is the view's movement per meter of head
// movement. Head look is read from RenderHook.
void OnViewBuilt(bool worldView, bool headLook, bool headAim, float const headOrientation[4],
                 float const headPosition[3], float offsetRightUU, float offsetUpUU, float offsetForwardUU,
                 float unitsPerMeter) noexcept;

// True while our mod aims weapons with the right controller and that controller is tracked
// (held for a moment through brief tracking loss): Halcyon's on-foot combat head aim then stays
// off, so the head only looks (render-side) and the controller aims. False with a plain gamepad.
bool ControllerAimActive() noexcept;
}
