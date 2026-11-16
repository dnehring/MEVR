// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the ONE seam vocabulary between Halcyon's renderers (src/halcyon/le1, le2, le3)
// and our host (src/plugin). Every seam all three renderers share is declared here once, with one signature; each
// renderer tree calls it as MevrSeams::X and each game's host implements it (one host ASI links exactly one renderer, so
// there is one definition per ASI). A seam only one renderer has stays in that tree's own mevr_bridge.h, behind a
// capability bit of plugin/Renderer.hpp (mevr::host::renderer::Capabilities()).
//
// Declared without the OpenXR SDK's headers (Halcyon's xr_types.h defines the same names): handles cross as void*.
// No native game object crosses this seam. research/trilogy-seams.md says how to add one; docs/trilogy-parity-ledger.md
// "Seams" lists which renderer calls which.
//
// Rules for a seam here:
//  - one signature for all three games; a renderer passes null, 0 or -1 for what it doesn't have (each says which);
//  - the thread is part of the contract (said per function);
//  - a renderer that doesn't call a seam yet is named at it ("not called by LE1"): the host of that game need not
//    define it, and the matching capability bit is false there.

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

struct ID3D11DeviceContext;

namespace MevrSeams
{
// =====================================================================================================================
// Data
// =====================================================================================================================

// What the renderer knows about the frame it is submitting (its RunFrame, Present thread), for our layers. One struct for
// all three renderers: a renderer leaves what it doesn't have at the default, and the valid flags say what is filled.
struct OverlayFrame
{
    void* session = nullptr;    // XrSession
    void* appSpace = nullptr;   // XrSpace: Halcyon's recentered LOCAL app space (the projection layer's)
    void* viewSpace = nullptr;  // XrSpace: VIEW (head-locked quads)
    ID3D11DeviceContext* context = nullptr;   // the game's immediate context
    bool worldFrame = false;    // this frame submits the head-tracked world (the projection layer), not a flat panel
    bool menuOpen = false;      // Halcyon's own (Insert) menu is open
    bool stereo = false;        // the stereo mode whose eyes carry no HUD while the HUD panel takes it (LE1: Stereo 2
                                // is the active VR mode; ME2/ME3: SFR)
    int gameMode = -1;          // the engine's game mode as the renderer reads it (the game's own numbers), -1 unreadable

    // The head at the frame's display time in appSpace (the located views: their mean position, the left eye's
    // orientation), when headValid.
    bool headValid = false;
    float headOrientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // xyzw
    float headPosition[3] = {};                            // eye centre, metres

    // Each eye this frame (the runtime's views, app space), when eyesValid. Projection poses can instead be centred or
    // taken from frame history. eyeOrientation is filled by LE1 only (eyeOrientationValid).
    bool eyesValid = false;
    bool eyeOrientationValid = false;
    float eyeOrientation[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    float eyePosition[2][3] = {};

    // The world layer's views as submitted (each eye's swapchain, rect, pose and FOV), when projectionValid.
    bool projectionValid = false;
    void* projectionSwapchain[2] = {nullptr, nullptr};   // XrSwapchain
    int projectionRect[2][4] = {};                       // x, y, width, height (pixels)
    float projectionOrientation[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    float projectionPosition[2][3] = {};
    float projectionFov[2][4] = {};                      // angleLeft, angleRight, angleUp, angleDown (radians)

    // ---- Filled by LE1's renderer only so far (the sequels leave the defaults) ----
    float floatWhiteScale = 1.0f;  // for a float (scRGB) HUD panel: the multiplier that maps SDR white to 1.0
    // research/39: the head-look arm the submitted pair was rendered with (0 unknown): which Draw it came from, and so
    // which pose the scope's scene capture rendered.
    std::uint64_t pairArm = 0;
    // The 3D cinema screen (research/52 "Cross-eyed") where the RENDERER composes it (LE1's xr_session.cpp): where it
    // stands this frame, so the game's UI can go on a layer of its own just in front of it. The sequels' screen is
    // composed by the host (CineScreenLayers), which keeps this itself. screenActive false: no screen this frame.
    bool screenActive = false;
    void* screenSpace = nullptr;                       // XrSpace of the pose: the app space, or VIEW while head-locked
    bool screenHeadLocked = false;
    float screenOrientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float screenPosition[3] = {};
    float screenWidthM = 0.0f, screenHeightM = 0.0f;
    float screenCrop = 0.0f;                           // each eye's dropped share of its image's width
    float screenRows[2] = {0.0f, 1.0f};                // the shown band's rows, fractions of the eye image's height
};

// The 3D cinema screen where the HOST composes it (CineScreenLayers): the eyes as located and each eye's image.
struct CineScreenFrame
{
    void* session = nullptr;     // XrSession
    void* appSpace = nullptr;    // XrSpace: the LOCAL app space the views were located in
    void* viewSpace = nullptr;   // XrSpace: VIEW
    ID3D11DeviceContext* context = nullptr;
    bool conversation = false;   // semantic conversation (else cutscene)
    bool stereoSource = true;    // false: identical fallback image, never manufacture disparity with opposite crops
    bool headValid = false;
    float eyeOrientation[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    float eyePosition[2][3] = {};
    float eyeFov[2][4] = {};     // angleLeft, angleRight, angleUp, angleDown (radians)
    void* eyeSwapchain[2] = {nullptr, nullptr};   // XrSwapchain: pass 0 (left) in [0], pass 1 (right) in [1]
    unsigned eyeWidth = 0;
    unsigned eyeHeight = 0;
};

// Which flat panel PlacePanel places. The values are plugin/WorldPanels' Kind's.
enum class PanelKind : int
{
    ByGameMode = -1,    // the host picks the kind from OverlayFrame::gameMode (ME2/ME3: one native panel for every mode)
    InsertMenu = 0,     // Halcyon's own menu (its own slot)
    GameMenu = 1,       // the flat screen: the game's menus
    GalaxyMap = 2,
    Conversation = 3,
    Cutscene = 4,       // cutscenes and movies
};

// Where a flat panel's quad goes.
struct PanelPose
{
    bool placed = false;                               // false: the host has no place for it, the renderer's own stands
                                                       // (nothing below is meaningful)
    void* space = nullptr;                             // XrSpace to submit the quad in
    float orientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // xyzw, in `space`
    float position[3] = {0.0f, 0.0f, 0.0f};            // metres, in `space`
    bool headLocked = true;                            // `space` is VIEW (upstream's placement)
    float widthM = 0.0f, heightM = 0.0f;               // the size to submit: the renderer's, or the host's (the galaxy table)
    bool nativeMap = false;                            // the panel is the game's galaxy map or orbit image with a ready
                                                       // image: a separately captured full-frame UI follows this placement
};

// =====================================================================================================================
// 1. Session and frame
// =====================================================================================================================

// The statically linked OpenXR loader's xrGetInstanceProcAddr (upstream loads openxr_loader.dll, which our ASI doesn't
// ship). Our controller actions must come from the same loader instance. Any thread, before the instance exists.
void* StaticXrGetInstanceProcAddr() noexcept;

// The session functions are resolved; each argument is the address of the renderer's function-pointer slot. We may replace
// a slot with a wrapper that calls the real function (plugin/TestLoop: the snapshots' image copies, the frame's layers, the
// head recorded or replayed; plugin/PerfProbe: the wait, begin and end of each frame, where the host has the probe).
// A renderer passes nullptr for a slot it doesn't route through a pointer (LE3: waitSwapchainImage, waitFrame, beginFrame).
void WrapSessionFunctions(void** acquireSwapchainImage, void** waitSwapchainImage, void** releaseSwapchainImage,
    void** waitFrame, void** beginFrame, void** endFrame, void** locateViews) noexcept;

// The session and its spaces exist (before xrBeginSession): create and attach our controller actions. XrInstance,
// XrSession. The renderer's XR thread.
void OnSessionCreated(void* instance, void* session) noexcept;

// The session stopped, was lost or is exiting (after xrEndSession, or when frames no longer run): our quads' swapchains,
// the HUD panel's redirect and the published view go. Present thread.
void OnSessionStopped() noexcept;

// After a successful xrBeginFrame (Present thread): sync the controller actions and publish their state, with poses
// located in `space` (Halcyon's LOCAL app space, re-origined at recenter: the head pose's space).
void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept;

// The LOCAL app space was re-origined at the head: recenter counters, the HUD panel straight ahead again, world-locked
// panels placed afresh. Present thread.
void OnRecentered() noexcept;

// The OpenXR runtime this process uses is SteamVR's: XR_RUNTIME_JSON if set (our [Debug] openxrRuntime, the fake
// headset), else the machine's active runtime. Any thread. Not called by LE1.
bool XrRuntimeIsSteamVr() noexcept;

// =====================================================================================================================
// 2. View
// =====================================================================================================================

// Present thread, once per frame after the view for the next game frame is set up (head look, view offset): how that view
// relates to the game camera, so our mod can map the controllers into the world (hand aim, arm IK).
// `worldView`: a head-tracked 3D view (head valid, no menu, not the flat screen).
// `lookYawUU`/`lookPitchUU`: the renderer's head look in Unreal rotator units, its own signs (yaw + when the view turns
// left, pitch + up), 0 while head look is off.
// `headOrientation` xyzw and `headPosition` metres: the roll-removed head the view is built for, in the LOCAL app space.
// Offsets: the view offset along the game camera's right/up/forward, in UE units; `unitsPerMeter` the view's movement
// per metre of head movement. `floorWorld`: ordinary gameplay on foot (no cinematic), where the floor lock may apply.
void OnViewBuilt(bool worldView, bool headLook, bool headAim, int lookYawUU, int lookPitchUU,
    float const headOrientation[4], float const headPosition[3], float offsetRightUU, float offsetUpUU,
    float offsetForwardUU, float unitsPerMeter, bool floorWorld) noexcept;

// Game thread, for each pass's scene view before the lean moves the camera: the floor lock (plugin/FloorLock).
// `rawCameraZ` the camera's height (-PreViewTranslation.Z), `rightZ`/`upZ`/`forwardZ` the camera axes' Z;
// `right`/`up`/`forward` the lean in UE units, which it may replace with the view the mod was told about. Returns the
// height to add (UE units), 0 where it doesn't apply. `replay`: the renderer's second pass, which reuses the first's.
float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ, float* right, float* up,
    float* forward) noexcept;

// The renderer's recenter, before it creates the new app space (Present thread): `head` is the head's orientation in the
// never-moved base space (xyzw), `orientation` holds upstream's yaw-only re-origin orientation. Left as it is (bit for
// bit) unless the base space is badly tilted (core/vr/Recenter: rolled past 90 degrees, or tilted past the renderer's
// recenterTiltDeg for 1.5 s), when it becomes the head's full orientation. `automatic`: the renderer's own first recenter
// when tracking starts (never corrected). OnRecentered confirms it took.
void RecenterOrientation(float const head[4], bool automatic, float orientation[4]) noexcept;

// =====================================================================================================================
// 3. Eye image. Not called by LE1 (its blit has its own EyeColour* / EyeUpscale seams until the eye-image lane).
// =====================================================================================================================

// Present thread: `source` (an ID3D11Texture2D) is about to be copied into eye `eye`'s image (0 left, 1 right). A
// snapshot armed now saves it too (plugin/TestLoop).
void OnEyeSource(int eye, void* source) noexcept;

// Present thread, in place of CopyResource(image, source) into an eye's or the flat panel's image (ID3D11DeviceContext,
// ID3D11Texture2D): owns the complete validated raw-copy or shader-conversion operation. True means issued; false is
// failure, never permission for a raw-copy fallback. `shiftX`: the stereo convergence shift in pixels. `eye`: 0 left,
// 1 right, -1 the flat panel (the colour correction's dither pattern is per eye).
bool CopyIntoImage(void* context, void* image, void* source, int shiftX, int eye) noexcept;

// Right after the copy into eye `eye`'s image `image` (swapchain index `index`), before its release. `acquired` and
// `waited`: the XrResults of its acquire and wait (0 success; a failed one means no copy and `image` null), counted
// and logged. A renderer that only reports successful copies passes 0, 0.
void OnEyeCopied(int eye, void* image, std::uint32_t index, int acquired, int waited) noexcept;

// The eye's image was released (`published`: it goes out with this frame). Receipt only: no released-image access.
void OnEyeReleased(int eye, bool published) noexcept;

// For each DXGI factory the game makes (an IDXGIFactory*): our hook on the first output's IDXGIOutput6::GetDesc1, where
// the game asks whether the display is HDR ([Colour] hdrDisplay). The thread that created the factory.
void OnFactory(void* factory) noexcept;

// =====================================================================================================================
// 4. Composition
// =====================================================================================================================

// Present thread, every frame right before xrEndFrame, after the renderer's own layers are made (also flat and menu
// frames): our layers (XrCompositionLayerBaseHeader-compatible: quads, and where said a projection layer), up to
// `capacity` into `layersOut`, drawn over the renderer's world and panels. Returns how many. Also arms the HUD panel's
// redirect for the next frame.
unsigned OverlayLayers(OverlayFrame const& frame, void const** layersOut, unsigned capacity) noexcept;

// Render thread, from the renderer's draw hooks: one of the game's UI draws of a stereo world frame. `draw(user)`
// re-issues the game's draw; it is called with our HUD panel bound while the panel is armed (plugin/HudPanel), only once
// the left eye is captured (`afterLeftEye`; earlier ones are counted). `textBatch`: the draw is a text batch (false where
// the renderer can't tell). True if the draw went to the panel: the caller skips its own.
// Not called by LE1: its draw hooks drive the panel through its own HudPanel* seams (src/halcyon/le1/mevr_bridge.h).
bool HudPanelDraw(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool textBatch, bool afterLeftEye) noexcept;

// Present thread, each frame a conversation (`conversation`) or cutscene is decided: whether it goes on the 3D cinema
// screen ([Cinematics] in ASI\<GAME>MEVR.ini; plugin/CineScreen).
bool CineScreenWanted(bool conversation) noexcept;

// Present thread: [Cinematics] fullRate: the 3D cinema screen runs at the headset's rate, not the renderer's cinematic cap.
bool CineScreenFullRate() noexcept;

// Present thread, at the stereo submit while the screen is up: the screen's layers in submit order (the floor and
// horizon's projection layer, then the left and right eyes' quads), at most `capacity` into `layersOut`; 0 if it can't be
// shown (the caller then submits its projection layer as before). The screen's place is kept for this frame's
// OverlayLayers (the game's UI on its own layer in front of it).
// Not called by LE1: its xr_session.cpp composes the screen itself (CineScreenQuadsNow, CinemaBackdropLayer, PlacePanel).
unsigned CineScreenLayers(CineScreenFrame const& frame, void const** layersOut, unsigned capacity) noexcept;

// Present thread, the frame's last layer: this frame's view (0 the world, 1 the 3D cinema screen, 2 a cinematic on the
// flat panel, 3 another flat panel, -1 none submitted); a head-locked black quad (an XrCompositionLayerQuad*) to submit
// last, or null (plugin/TransitionFade).
void const* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept;

// Present thread, each frame a flat panel's quad is about to be submitted: where it goes. `frame`: at least session,
// appSpace, viewSpace, gameMode, menuOpen and the head (headValid, headOrientation, headPosition). `viewPosition`: where
// the renderer puts it head-locked (VIEW space, oriented like VIEW). `widthM`/`heightM`: the renderer's size for it (0 if
// it has none yet). `imageReady`: the panel's image exists this frame. The answer's `placed` false: the renderer's own
// placement and size stand; else submit at the pose, in its space, with its size.
// LE1 (plugin/WorldPanels): fixed in the app space where the panel appeared, or VIEW and `viewPosition` unchanged with
// worldLockedPanels off; always placed, the size the renderer's.
// ME2: only the galaxy map and orbit image on the table ([Galaxy] nativeTable); called every frame, also without a panel
// (imageReady false), so the table's state follows the mode.
// ME3: every native panel through WorldPanels by its game mode, the galaxy map and orbit image on the table.
PanelPose PlacePanel(PanelKind kind, OverlayFrame const& frame, float const viewPosition[3], float widthM, float heightM,
    bool imageReady) noexcept;

// =====================================================================================================================
// 5. Input and threads
// =====================================================================================================================

// From the renderer's XInput hooks, right after the real read: merges the motion controllers into pad 0 (Touch -> Xbox
// mapping, plugin/PadMerge) before the renderer's own pad handling (menu block, move-follows-head). Any thread.
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept;

// The key is down and a window of the game is in front: a hotkey typed in another program (Discord, a browser) must
// not recenter, switch profiles or open the menu. Every key poll of the renderer goes through it. Any thread.
bool KeyDown(int vk) noexcept;

// The renderer's menu key went down (`ownMenuOpen`: its own menu is open). True if our main menu took it (opened or
// closed; plugin/MainMenu): the renderer does nothing. False: the renderer toggles its own menu as upstream (the key with
// Ctrl or Shift held, or while its menu is open). Present thread.
bool OnMenuKey(bool ownMenuOpen) noexcept;

// Our main menu is open (plugin/MainMenu): the renderer keeps the game's mouse and keyboard from it, as for its own
// menu. Any thread.
bool MainMenuOpen() noexcept;

// UnrealScript runs on the game thread only (research/36): the VM keeps its state in process-wide globals, so a script
// call from the Present thread races the game thread's script. True on the engine tick's thread.
bool OnGameThread() noexcept;

// Off the game thread: queues `function` on `object` with a copy of its `size`-byte parameter block for the start of the
// next engine tick, where it runs if both are still in GObjects. `size` kFunctionParmsSize: the host reads the size from
// the function itself (its ParmsSize). True if queued. Results and out parameters are never filled in: setters only.
constexpr std::uint32_t kFunctionParmsSize = 0xFFFFFFFFu;
bool QueueProcessEvent(void* object, void* function, void const* params, std::uint32_t size) noexcept;

// =====================================================================================================================
// 6. Frame-breakdown probe (plugin/PerfProbe, research/22). Optional: a renderer calls these only where its host has
//    the probe (capability perfSeams); each costs one atomic load while no capture runs. The probe's wait, begin and end
//    of each frame come through WrapSessionFunctions' slots.
//    Not called by LE1: its tree has the same steps as MELEVR::MevrBridge::PerfPresentScope, PerfPass0Captured, PerfPoint
//    and the g_perfDraws counter (src/halcyon/le1/mevr_bridge.h) until they move here.
// =====================================================================================================================

// Render thread: the renderer's Present hook, as its first and its last action. `context`: the game's immediate context,
// for GPU timestamps (may be nullptr).
void PerfPresentBegin(ID3D11DeviceContext* context) noexcept;
void PerfPresentEnd(ID3D11DeviceContext* context) noexcept;
// The pair as a scope: the first line of a Present hook with several returns.
struct PerfPresentScope
{
    explicit PerfPresentScope(ID3D11DeviceContext* c) noexcept : context(c) { PerfPresentBegin(c); }
    ~PerfPresentScope() { PerfPresentEnd(context); }
    PerfPresentScope(const PerfPresentScope&) = delete;
    PerfPresentScope& operator=(const PerfPresentScope&) = delete;
    ID3D11DeviceContext* context;
};
// Render thread: right after the stereo frame's first eye is captured (LE1's PerfPass0Captured).
void PerfFirstEyeCaptured(ID3D11DeviceContext* context) noexcept;
// Render thread, in each of the renderer's immediate-context draw hooks: one draw call (LE1's ++g_perfDraws).
void PerfDraw() noexcept;
// Game thread, in the stereo branch of the renderer's CalcSceneView detour: true while each stereo pass is to render
// through a scene view state of its own, false while both go through the game's one state ([Performance]
// perEyeViewState; its default is the game's, plugin/RenderLeversTable: the sequels 1, their SFR's own behaviour;
// research/sequel-render-levers.md "Performance research"). Defined by plugin/RenderLevers in the LE2 and LE3 hosts.
// Not called by LE1: its tree asks MELEVR::MevrBridge::PerEyeViewState, the same lever.
bool PerEyeViewState() noexcept;

// ---------------------------------------------------------------------------------------------------------------------
// Room left for one-Draw stereo ([Performance] sharePass = 5, research/37) on a sequel: LE1's seams for it are
// MELEVR::MevrBridge::g_oneDrawStereo, RecordPass1View / FindPass1View, StereoFrameActive and the draw recorder's
// Record* calls (src/halcyon/le1/mevr_bridge.h). A second renderer that takes them moves those declarations here
// unchanged (capability oneDraw), so both trees call one name.
// ---------------------------------------------------------------------------------------------------------------------
}
