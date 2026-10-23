// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams where our host plugs into Halcyon's code.
// Declared without the OpenXR SDK headers, because xr_types.h defines the same names; the
// implementation is in src/plugin/HalcyonBridge.cpp. Handles are passed as void*.

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

struct ID3D11BlendState;
struct ID3D11Buffer;
struct ID3D11DepthStencilState;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11PixelShader;
struct ID3D11RenderTargetView;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
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

// Scaleform's other blend modes (multiply, screen, add...: blended, not "over", with Scaleform's
// DEST_ALPHA alpha factor) go to the panel too, so no part of the UI stays in the eyes (the doubled
// weapon wheel, notes/todo-vrik.md 2026-10-21). Their colour goes into the panel with the game's blend
// (`colour`: alpha left alone), and what they do to whatever is behind them (the world) into a "tint"
// texture the panel's alpha is made from (`tint`: dest *= the draw's factor; nullptr when the mode
// leaves the world as is, like add). Cached per game state.
void HudPanelTintBlends(ID3D11DeviceContext* context, ID3D11BlendState* game, ID3D11BlendState** colour,
                        ID3D11BlendState** tint) noexcept;
// The tint texture's render target (white = the world unchanged), the panel texture's size; nullptr
// if there is none. Call after HudPanelTarget.
ID3D11RenderTargetView* HudPanelTintTarget() noexcept;
// The game's depth-stencil state without depth or stencil writes (for drawing the same draw a second
// time, into the tint texture, without counting its stencil twice). Cached; nullptr keeps the game's.
ID3D11DepthStencilState* HudPanelNoWrites(ID3D11DeviceContext* context, ID3D11DepthStencilState* game) noexcept;
// Counts one draw that went into the tint texture.
void NoteHudPanelTint() noexcept;

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
    // [MEVR] Gun sights (src/plugin/SightLayers.cpp): each eye this frame (the runtime's views, app space),
    // which eye looks through a sight; and the world layer's views as submitted (swapchain, rect, the pose
    // and FOV each image was rendered for), the eye image a scope magnifies.
    float eyeOrientation[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    float eyePosition[2][3] = {};
    bool projectionValid = false;
    void* projectionSwapchain[2] = {nullptr, nullptr};   // XrSwapchain
    int projectionRect[2][4] = {};                       // x, y, width, height (pixels)
    float projectionOrientation[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    float projectionPosition[2][3] = {};
    float projectionFov[2][4] = {};                      // angleLeft, angleRight, angleUp, angleDown (radians)
};

// Present thread (RunFrame), once per frame after the eyes are filled, every frame (also flat and
// menu frames): copies the HUD panel texture into its swapchain, places the panel and the reticle,
// draws the gun's sight (a red dot or holo reticle, or a scope's picture: src/plugin/SightLayers.cpp),
// draws the squad weapon menu while the mod has it open (src/plugin/SquadPanel.cpp), shows the game's
// weapon or power wheel on a panel of its own at a hand while one is up (cut out of the HUD panel),
// and writes up to `capacity` quad layers (XrCompositionLayerQuad*: panel, reticle, sight (two), squad
// menu, wheel) into `layersOut`.
// Also clears the panel texture, arms the redirect for the next frame, and hides the game's own
// crosshair while the hand reticle replaces it. Returns the number of layers written.
unsigned OverlayLayers(const OverlayFrame& frame, const void** layersOut, unsigned capacity) noexcept;

// True when the frame's panel texture is a float (scRGB) format, so RunFrame passes its white scale.
bool HudPanelIsFloat() noexcept;

// Recentering re-origined the app space: the HUD panel goes straight ahead again, and the world-locked
// flat panels are placed afresh in front of the head.
void OnRecentered() noexcept;

// ---- A recenter that also corrects a tilted base space (src/plugin/RecenterTilt.cpp) --------------------
// DoRecenter, before it creates the new app space: `head` is the head's orientation in the never-moved base
// space (xyzw), `orientation` holds upstream's yaw-only re-origin orientation. Left as it is (bit for bit)
// unless the base space is badly tilted (MELEVR.ini recenterTiltDeg, core/vr/Recenter), when it becomes the
// head's full orientation. `automatic`: Halcyon's first recenter when tracking starts (never corrected).
// Logs which kind of recenter ran; OnRecentered confirms it took.
void RecenterOrientation(float const head[4], bool automatic, float orientation[4]) noexcept;

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

// ---- Our VR main menu (src/plugin/MainMenu.cpp, research/28), which replaces Halcyon's menu for the player ----------
// The menu key (cfg.menuKey: \ in our deploy) went down (xr_session.cpp): opens or closes our menu; with Ctrl or Shift
// held (or while Halcyon's menu is open), toggles Halcyon's menu instead. Present thread.
void OnMenuKey() noexcept;
// Our main menu is open: Halcyon's input block (vr_menu.cpp) keeps the game's mouse and keyboard from it, as for its
// own menu. Any thread.
bool MainMenuOpen() noexcept;

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
// ResolveSessionFunctions, once its session functions are resolved: replaces some of them in place
// (each argument points at a resolved function pointer) with wrappers that call the original. They
// copy each swapchain image just before it's released and read the layers each xrEndFrame submits
// (snapshots, only while one is being taken), note the head located in the app space (recording),
// and during a replay substitute the recorded head poses in xrLocateViews, keeping the runtime's FOVs.
// The frame-breakdown probe (src/plugin/PerfProbe.cpp) times the wait, begin and end of each frame and
// the swapchain image waits.
void WrapSessionFunctions(void** acquireSwapchainImage, void** waitSwapchainImage, void** releaseSwapchainImage,
                          void** waitFrame, void** beginFrame, void** endFrame, void** locateViews) noexcept;

// ---- Frame-breakdown probe (src/plugin/PerfProbe.cpp, research/22) -------------------------------------
// Render thread. Steps of the frame the probe times while it runs (one atomic load each otherwise):
// Present's start and end (PerfPresentScope, the first line of PresentHook) and Stereo 2's first eye
// captured. `context`: the game's immediate context, for GPU timestamps (may be nullptr).
void PerfPresentBegin(ID3D11DeviceContext* context) noexcept;
void PerfPresentEnd(ID3D11DeviceContext* context) noexcept;
void PerfPass0Captured(ID3D11DeviceContext* context) noexcept;

struct PerfPresentScope
{
    explicit PerfPresentScope(ID3D11DeviceContext* c) noexcept : context(c) { PerfPresentBegin(c); }
    ~PerfPresentScope() { PerfPresentEnd(context); }
    PerfPresentScope(const PerfPresentScope&) = delete;
    PerfPresentScope& operator=(const PerfPresentScope&) = delete;
    ID3D11DeviceContext* context;
};

// Immediate-context draws, counted by d3d_capture.cpp's draw hooks (render thread; read by the probe).
extern std::uint32_t g_perfDraws;

// Present thread, at the end of RunFrame: whether to call xrWaitFrame for the next frame there, before the
// game renders it (research/22), rather than at the next Present. [Performance] earlyFrameWait in
// ASI\LE1MEVR.ini, default 1, re-read once a second (0: upstream's order).
bool EarlyFrameWait() noexcept;

// Render thread, per Stereo 2 pass: whether gameplay too renders each eye through its own FSceneViewState (Halcyon
// does it in cine only). With one shared state the second eye's "previous frame" occlusion query is the first eye's
// of the same frame, still on the GPU, and the engine spins for it: 1.3-1.6 ms a frame at a light spot (research/32).
// [Performance] perEyeViewState in ASI\LE1MEVR.ini, default 0, re-read once a second.
bool PerEyeViewState() noexcept;

// ---- Colour correction of the eye images (src/plugin/Colour.cpp, research/27) ---------------------------------
// Present thread. Neutral (the default for anything but a streamed Quest) changes nothing: Halcyon's own shaders,
// its raw copy.
// Once the eye blit's shaders exist: compiles variants of PSLeft, PSRight and PSFill from `shaderSource` with the
// correction at their MEVR_EYE_OUT hook.
void EyeColourShaders(ID3D11Device* device, const char* shaderSource) noexcept;
// The correction is on: an 8-bit source of the eye's size takes the shader blit instead of the raw copy.
bool EyeColourActive() noexcept;
// Right before an eye blit's draw: the pixel shader to bind, `plain` (Halcyon's) unless the correction is on, then
// its variant with the constants bound at b1. `which`: 0 PSLeft, 1 PSRight, 2 PSFill. `linearSource`: the draw
// writes through the eye's sRGB view (a float or _SRGB source). `eye`: 0 left, 1 right (the dither pattern).
ID3D11PixelShader* EyeColourShader(ID3D11DeviceContext* context, int which, ID3D11PixelShader* plain, bool linearSource,
                                   int eye) noexcept;
// Right after that draw: puts b1 back.
void EyeColourDone(ID3D11DeviceContext* context) noexcept;

// ---- Spatial upscaling of the eye images (src/plugin/Upscale.cpp, core/vr/Upscale, research/32) ----------------
// Present thread, in BlitTextureToEyeWithFill right before its fill draw (the eye's RTV, viewport and fixed state
// set): with [Upscale] method = fsr and `source` a smaller 8-bit image, scales it into `eyeRtv` with FSR 1 (EASU
// into an intermediate of the eye's size, RCAS into the eye, the colour correction at its end) and returns true;
// false (nothing drawn, no state changed but b2) for Halcyon's own fill. `fill`: the FillParams uploaded to
// `fillConstants` (fillUv, imageRect, colorScale). `linearSource`: the eye's sRGB view is bound (a float or _SRGB
// source): not upscaled.
bool EyeUpscale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11SamplerState* sampler,
                ID3D11Buffer* fillConstants, ID3D11RenderTargetView* eyeRtv, unsigned eyeWidth, unsigned eyeHeight,
                const float fill[12], bool linearSource, int eye) noexcept;

// ---- Life-size (metric) stereo (src/plugin/MetricStereo.cpp, core/vr/MetricStereo, research/30) ---------------
// Halcyon's Stereo 2 renders both passes from the head centre +- sfr2HalfEyeUU along the body's right, with one
// symmetric FOV, and shifts each finished image by sfr2Convergence at submit. With MELEVR.ini sfr2Metric on, each
// pass renders from its eye's real position instead (the runtime's views, at the view's own units per metre), with
// no convergence shift, and - where no screen-space UI is in the eye images - through that eye's own off-centre
// frustum. Everything stays as upstream with it off.
struct StereoPlanInput
{
    bool metric = false;            // sfr2Metric
    bool eyeFrustum = false;        // sfr2MetricEyeFrustum
    bool sfr2Active = false;        // Stereo 2 renders the world this frame (not the flat screen)
    bool headValid = false;
    bool viewFollowsHead = true;    // head look on: the rendered view turns with the head
    bool uiInEyes = true;           // the game's screen-space UI would land in the eye images (no HUD panel)
    bool fovOverride = false;       // Halcyon's render FOV override is on (vrFovFillEnabled, not the flat screen)
    bool swapEyes = false;          // sfr2SwapEyes: pass 0 renders the right eye
    float eyeOrientation[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};   // the runtime's views
    float eyePosition[2][3] = {};                                                         // (app space, metres)
    float eyeFov[2][4] = {};                                                              // left, right, up, down
    float viewOrientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // the roll-removed head the view is built for
    float fillH = 1.0f, fillV = 1.0f;                      // VR Fill (ClampVrFill'd)
    float unitsPerMeter = 50.0f;                           // the view's scale (Halcyon's 50 x lean gain)
    float depth = 1.0f;                                    // sfr2MetricDepth (x the cine separation scale)
    // The CPU projection the render last used (RenderHook::GetRenderFovHalfAngles, radians; 0 = none seen yet): the
    // culling window, which must hold each eye's own frustum in mode 2 (logged with the plan, warned when it doesn't).
    float cpuHalfH = 0.0f, cpuHalfV = 0.0f;
};
struct StereoPlan
{
    int mode = 0;                   // 0 upstream, 1 metric (symmetric window), 2 metric (each eye's own frustum)
    int pass0Eye = 0;               // the eye render pass 0 is for (pass 1: the other)
    float passOffsetUU[2][3] = {};  // per pass: camera right, up, forward from the lean's eye centre
    float passTerms[2][4] = {};     // per pass, mode 2: p0, p5, p8, p9 (UE3's off-centre terms, w = z)
    float passTan[2][4] = {};       // per pass, mode 2: the frustum's left, right, up, down tangents
    float ipdM = 0.0f;              // the runtime's eye distance
    float ipdUU = 0.0f;             // as rendered
};
// Present thread, once per frame with the view offset (RunFrame): the plan for the next game frame's passes.
void PlanStereo(const StereoPlanInput& in, StereoPlan* out) noexcept;
// Present thread, with the projection views: what this frame submits. `mode`: what the pair was rendered with
// (0 upstream's, also the flat screen). Declared poses (position xyz, orientation xyzw) and FOVs per eye.
void NoteStereoSubmit(int mode, float const declaredPosition[2][3], float const declaredOrientation[2][4],
                      float const declaredFov[2][4]) noexcept;

// [MENUVIS] One-shot diagnostics for Halcyon's Insert menu quad (Present thread): reads back
// `texture` (the menu's ImGui texture, or the swapchain image right after the copy into it) a few
// frames later without stalling, and logs how many pixels are visible (alpha > 0). `tag` names it.
void ProbeTextureAlpha(ID3D11DeviceContext* context, ID3D11Texture2D* texture, const char* tag) noexcept;
}
