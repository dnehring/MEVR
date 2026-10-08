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

// The session and its spaces exist (Present thread): create and attach our controller actions.
void OnSessionCreated(void* instance, void* session) noexcept;

// After xrBeginFrame (Present thread): sync the controller actions and publish their state, with
// poses located in `space` (Halcyon's recentered LOCAL app space, the head pose's space).
void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept;

// From Halcyon's XInput hooks, right after the real read: merges the motion controllers into pad
// 0 (Touch -> Xbox mapping) before Halcyon's own pad handling (menu block, move-follows-head).
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept;
}
