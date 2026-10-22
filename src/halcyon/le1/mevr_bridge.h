#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams where our host plugs into Halcyon's code.
// Declared without the OpenXR SDK headers, because xr_types.h defines the same names; the
// implementation is in src/plugin/HalcyonBridge.cpp. Handles are passed as void*.

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

struct ID3D11BlendState;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;

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

// ---- Body-locked HUD panel and hand reticle (src/plugin/HalcyonOverlays.cpp) --------------------
// The game's UI draws of a head-tracked world frame go into a panel texture instead of the eye
// images (d3d_capture.cpp's draw hooks), and RunFrame shows that texture as a quad fixed in the
// tracking space in front of the player's body, plus a small reticle quad on the weapon's aim ray.

// Render thread (draw hooks): UI draws go to the HUD panel. Armed by OverlayLayers for the frame
// after a head-tracked world frame, while the panel is on.
bool HudPanelArmed() noexcept;

// Render thread, for one UI draw into a `width` x `height` target whose render target view has
// DXGI format `format`: the panel texture's render target (made or remade on demand), or nullptr if
// it can't take the draw (the draw then goes to the frame as before).
ID3D11RenderTargetView* HudPanelTarget(ID3D11DeviceContext* context, unsigned width, unsigned height,
                                       int format) noexcept;

// The game's blend state for that draw, but with alpha accumulated "over" (one, inverse source
// alpha) and alpha writes on, so the panel ends up with premultiplied colour and coverage. Cached per
// game state; nullptr keeps the game's own.
ID3D11BlendState* HudPanelBlend(ID3D11DeviceContext* context, ID3D11BlendState* game) noexcept;

// Counts one UI draw that went to the panel (OverlayLayers logs and resets the count).
void NoteHudPanelDraw() noexcept;

// What RunFrame knows about the frame it is submitting.
struct OverlayFrame
{
    void* session = nullptr;                 // XrSession
    void* appSpace = nullptr;                // XrSpace: Halcyon's recentered LOCAL app space
    ID3D11DeviceContext* context = nullptr;  // the game's immediate context
    bool worldFrame = false;   // this frame submits the head-tracked world (the projection layer)
    bool menuOpen = false;     // Halcyon's Insert menu is open
    bool sfr2 = false;         // Stereo 2 is the active VR mode (the only one with a HUD-free frame)
    int gameMode = -1;         // USFXGameModeManager::CurrentMode: 0 on foot, -1 unreadable
    bool headValid = false;
    float headOrientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // xyzw, app space
    float headPosition[3] = {0.0f, 0.0f, 0.0f};            // eye center, app space, meters
    float floatWhiteScale = 1.0f;  // for a float (scRGB) panel: multiplier that maps SDR white to 1.0
};

// Present thread (RunFrame), once per frame after the eyes are filled, every frame (also flat and
// menu frames): copies the HUD panel texture into its swapchain, places the panel and the reticle,
// and writes up to `capacity` quad layers (XrCompositionLayerQuad*, panel first) into `layersOut`.
// Also clears the panel texture, arms the redirect for the next frame, and hides the game's own
// crosshair while the hand reticle replaces it. Returns the number of layers written.
unsigned OverlayLayers(const OverlayFrame& frame, const void** layersOut, unsigned capacity) noexcept;

// True when the frame's panel texture is a float (scRGB) format, so RunFrame passes its white scale.
bool HudPanelIsFloat() noexcept;

// Recentering re-origined the app space: the HUD panel goes straight ahead again, and the world-locked
// flat panels are placed afresh in front of the head.
void OnRecentered() noexcept;

// ---- World-locked flat panels (src/plugin/WorldPanels.cpp) -------------------------------------------
// Halcyon's flat screen (menus, galaxy map, flat conversations, cutscenes, movies) and its Insert menu are
// head-locked upstream (VIEW space). With MELEVR.ini worldLockedPanels on they stay put in the app space
// instead: placed in front of the head when they appear (yaw only, level, at their head-locked offset),
// kept there while the player looks around or leans (parallax), and placed afresh when they reappear, when
// the flat screen's kind changes, on recenter, or once the head has stayed turned past panelFollowDeg.

enum class PanelKind : int
{
    InsertMenu = 0,     // Halcyon's own menu (its own slot)
    GameMenu = 1,       // the flat screen: the game's menus
    GalaxyMap = 2,
    Conversation = 3,
    Cutscene = 4,       // cutscenes and movies
};

struct PanelPose
{
    void* space = nullptr;                             // XrSpace to submit the quad in
    float orientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // xyzw, in `space`
    float position[3] = {0.0f, 0.0f, 0.0f};            // meters, in `space`
    bool headLocked = true;                            // `space` is VIEW (upstream's placement)
};

// Present thread, each frame a flat panel quad is submitted. `viewPosition`: where Halcyon puts it
// head-locked (VIEW space, oriented like VIEW). `head`: the eye centre in the app space this frame
// (orientation xyzw, position; ignored while !headValid). Returns VIEW and `viewPosition` unchanged
// with the setting off (or no app space yet), else the app space and the pose fixed in it.
PanelPose PlacePanel(PanelKind kind, float const viewPosition[3], void* viewSpace, void* appSpace, bool headValid,
                     float const headOrientation[4], float const headPosition[3]) noexcept;

// Insert menu: re-place the panel in front of where the head points now (next frame).
void RequestHudPanelReplace() noexcept;

// Insert menu status: UI draws that went to the panel in the last world frame.
unsigned HudPanelDrawsLastFrame() noexcept;

// ---- Halcyon's menu: laser pointer and our tab (src/plugin/MevrMenuPointer.cpp, MevrMenuTab.cpp) ---
// All on the Present thread (RunFrame / RenderFrame).

// Each frame the menu quad is submitted (xr_session.cpp): where it is. `headOrientation` (xyzw) and
// `headPosition`: the VIEW space's pose in Halcyon's app space (the controllers' space). `placed`: the
// quad's space and pose as submitted (PlacePanel): head-locked (VIEW) or fixed in the app space. Size in
// meters.
void OnMenuQuad(float const headOrientation[4], float const headPosition[3], PanelPose const& placed, float width,
                float height) noexcept;

struct MenuPointer
{
    bool hit = false;    // a controller's aim ray is on the menu (or holds a press that began there)
    bool left = false;   // no hit, but the last sample had one: the pointer just left the menu
    float x = 0.0f;      // menu texture pixels
    float y = 0.0f;
    bool click = false;  // the pointing hand's trigger: ImGui's left button
    float wheel = 0.0f;  // right stick: ImGui wheel steps for this frame
};

// ApplyGamepadInput (before ImGui::NewFrame): the pointer on a `width` x `height` menu texture,
// `dt` seconds after the last frame. True when it is on the menu.
bool SampleMenuPointer(int width, int height, float dt, MenuPointer* out) noexcept;

// ApplyGamepadInput: whether the OS cursor at screen (x, y) should drive ImGui's mouse. The pointer
// takes the mouse while it is on the menu and keeps it until the OS cursor moves.
bool MenuMouseFromOs(long x, long y, bool pointerHit) noexcept;

// After BuildUI, inside the ImGui frame: the pointer's dot.
void DrawMenuPointer() noexcept;

// Inside BuildUI's tab bar: our tabs ("VR hands": hand angle trims, arm-length measurement,
// camera at Shepard's eyes, life-size movement).
void BuildMenuTabs() noexcept;

// The lean's forward/back emphasis: upstream's 3 (a dolly reads weaker than sideways parallax),
// or 1 with the VR hands tab's life-size movement on, since a body that follows the player only
// lines up with the player's own movement 1:1.
float ForwardLeanEmphasis() noexcept;

// Whether first person (K) is on at launch. Upstream always boots it off (experimental, opt-in per
// session); our VRIK is a first-person experience, so ours keeps what the active profile saved.
bool FirstPersonAtBoot(bool saved) noexcept;

// ---- Test loop: eye snapshots, pose recording and replay (src/plugin/TestLoop.cpp) ------------------
// ResolveSessionFunctions, once its session functions are resolved: replaces four of them in place
// (each argument points at a resolved function pointer) with wrappers that call the original. They
// copy each swapchain image just before it's released and read the layers each xrEndFrame submits
// (snapshots, only while one is being taken), note the head located in the app space (recording),
// and during a replay substitute the recorded head poses in xrLocateViews, keeping the runtime's FOVs.
void WrapSessionFunctions(void** acquireSwapchainImage, void** releaseSwapchainImage, void** endFrame,
                          void** locateViews) noexcept;

// [MENUVIS] One-shot diagnostics for Halcyon's Insert menu quad (Present thread): reads back
// `texture` (the menu's ImGui texture, or the swapchain image right after the copy into it) a few
// frames later without stalling, and logs how many pixels are visible (alpha > 0). `tag` names it.
void ProbeTextureAlpha(ID3D11DeviceContext* context, ID3D11Texture2D* texture, const char* tag) noexcept;
}
