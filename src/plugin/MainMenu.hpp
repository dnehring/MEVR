#pragma once

#include "core/vr/Controllers.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <openxr/openxr.h>

#include <cstdint>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;

// Our VR main menu (research/28): the approved glass board (assets/menu, RmlUi through plugin/menu), shown as a quad
// fixed in the room where it opened, in front of the head. It replaces Halcyon's ImGui menu for the player:
// holding both sticks or the menu key (\) opens and closes it; Ctrl or Shift + \ (and its Diagnostics row) opens
// Halcyon's own menu instead, as a developer fallback.
//
// While it's open the game sees no input (the pad is neutral, Halcyon's hooks swallow the mouse and keyboard), the
// mod sees no controllers (as during the guided calibration), and the mod pauses the game if [Menu] pause = 1
// (HostServices v16, WorldInfo.PauseGame as the game's own weapon wheel). A controller's aim ray is the mouse (the
// trigger clicks, the right stick scrolls, a tick on each new control under it), the pad moves the focus (d-pad or
// left stick, A activates, B goes back or closes), and on the Display page holding A shows the uncorrected picture.
//
// Closed, it costs nothing: no RmlUi update, no rendering, no swapchain, no layer; its textures stay allocated
// after the first opening. Present thread unless noted.
namespace mevr::host::main_menu {

// ---- Any thread -----------------------------------------------------------------------------------------------
bool IsOpen();
// HostServices v16: kMainMenuOpen, and kMainMenuPause when the player wants the game paused while it's open.
std::uint32_t ModFlags();

// ---- Present thread --------------------------------------------------------------------------------------------
void SetOpen(bool open, char const* why);
void Toggle(char const* why);

// After the controller sync (HalcyonBridge OnFrameBegun): the live controllers, before everything else is shown them
// hidden while the menu is open.
void NoteControllers(vr::controllers::State const& live);
// From the XInput hook (any thread): a real pad's state (before the menu neutralises it), for pad navigation.
void NotePad(XINPUT_GAMEPAD const& pad);

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;            // the head-tracked world is submitted (a backdrop to sample)
    bool headValid = false;
    XrPosef head{};                     // the eyes' centre and the head's orientation, app space
    // The left eye's image as submitted this frame: the backdrop's source.
    bool projectionValid = false;
    XrSwapchain projectionSwapchain = XR_NULL_HANDLE;
    XrRect2Di projectionRect{};
    XrPosef projectionPose{};
    XrFovf projectionFov{};
};
// OverlayLayers, every frame: the menu's quad, or nullptr while it's closed.
XrCompositionLayerQuad const* Layer(Frame const& frame);
// Recentering re-origined the app space: an open menu is placed again in front of the head.
void Recentered();
// For snapshots (plugin/TestLoop): open, where, the pointer, the cost.
std::string StatusJson();

} // namespace mevr::host::main_menu
