// SPDX-License-Identifier: MIT
// LE3-specific composition only. Input, floor, mapping, HDR/colour and test machinery are shared with LE1/LE2.
#include "halcyon/le3/mevr_bridge.h"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "engine/GameAdapter.hpp"
#include "plugin/Colour.hpp"
#include "plugin/CineScreen.hpp"
#include "plugin/SequelCinema.hpp"
#include "plugin/SequelWeaponLayers.hpp"
#include "plugin/SequelSquadLayers.hpp"
#include "plugin/Vignette.hpp"
#include "plugin/PowerWheel.hpp"
#include "plugin/TraversalCue.hpp"
#include "plugin/TraversalPresentation.hpp"
#include "plugin/HalcyonLe3.hpp"
#include "plugin/TransitionFade.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/EyeCopy.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/PadMerge.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/MenusLe2.hpp"
#include "plugin/WorldPanels.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/GamePointer.hpp"
#endif
#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/XrHost.hpp"
#include <openxr/openxr.h>
#include <atomic>
#include <cstring>
#include <mutex>
namespace {
std::mutex gTraversalMutex;
mevr::host::traversal_presentation::State gTraversalPresentation;
void InvalidateTraversalPresentation() {
    std::lock_guard lock(gTraversalMutex);
    gTraversalPresentation.Invalidate();
}
bool TraversalPresentationActive() {
    std::lock_guard lock(gTraversalMutex);
    return gTraversalPresentation.Active();
}
}
namespace mevr::host::halcyon_le3 {
bool SetTraversalPresentation(vr::traversal::Presentation const* value) noexcept {
    vr::floor::State floor;
    xr_host::Floor(&floor);
    std::lock_guard lock(gTraversalMutex);
    return gTraversalPresentation.Set(value, GetTickCount64(), floor.recenters, floor.sessions);
}
}
namespace ME2VR::MevrBridge {
namespace {
std::atomic<std::uint64_t> gFloorTranslatedMs{0};
std::mutex gPadMutex;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;
mevr::host::sequel_cinema::State gScreen;
bool gNativePanelSeen = false;
bool gNativePointerShown = false;
}
void* StaticXrGetInstanceProcAddr() noexcept { return reinterpret_cast<void*>(&xrGetInstanceProcAddr); }
void WrapSessionFunctions(void** acquire, void** release, void** endFrame, void** locateViews) noexcept {
    mevr::host::testloop::WrapSessionFunctions(acquire, release, endFrame, locateViews);
}
void OnSessionCreated(void* instance, void* session) noexcept {
    InvalidateTraversalPresentation();
    mevr::host::sequel_squad_layers::ForgetSession();
    auto const xrInstance = static_cast<XrInstance>(instance);
    auto const xrSession = static_cast<XrSession>(session);
    mevr::host::menus_le2::OnSessionCreated(xrInstance, xrSession);
    mevr::host::world_panels::Recenter();
    mevr::host::testloop::OnSessionCreated(xrSession);
    mevr::host::swapchains::OnSessionCreated(xrSession);
    mevr::host::eye_copy::Reset();
    mevr::host::floor_lock::Reset();
    mevr::vr::controllers::Publish({});
    mevr::host::pad_merge::Reset();
    gFloorTranslatedMs.store(0, std::memory_order_relaxed);
    bool const ready = mevr::host::xr_host::OnSessionCreated(xrInstance, xrSession);
    mevr::log::Info("halcyon le3: shared controller actions {} on the renderer session", ready ? "attached" : "unavailable");
    XrSystemGetInfo info{XR_TYPE_SYSTEM_GET_INFO};
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
    XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystem(xrInstance, &info, &system)) &&
        XR_SUCCEEDED(xrGetSystemProperties(xrInstance, system, &properties)) &&
        XR_SUCCEEDED(xrGetInstanceProperties(xrInstance, &runtime))) {
        mevr::host::colour::SetHeadset(runtime.runtimeName, properties.systemName, properties.vendorId);
    }
}
void OnSessionStopped() noexcept {
    InvalidateTraversalPresentation();
    mevr::host::sequel_squad_layers::ForgetSession();
    gScreen = {};
    mevr::host::hud_panel::SetArmed(false);
    mevr::host::vignette::Forget();
    mevr::host::traversal_cue::Forget();
    mevr::host::menus_le2::OnSessionStopped();
    gNativePanelSeen = gNativePointerShown = false;
    mevr::host::world_panels::Recenter();
    mevr::host::floor_lock::Reset();
    mevr::host::eye_copy::Reset();
    mevr::host::xr_host::ForgetHaptics();
    mevr::vr::controllers::Publish({});
    mevr::host::pad_merge::Reset();
}
void OnFrameBegun(void* session, void* space, std::int64_t time) noexcept {
    if (!session) return;
    mevr::host::world_panels::NoteFrame();
    gScreen = {};
    gNativePanelSeen = false; // renewed only by an actual submitted native panel
    mevr::vr::controllers::State state;
    auto const s = static_cast<XrSession>(session);
    auto const base = static_cast<XrSpace>(space);
    mevr::host::xr_host::OnFrameBegun(s, base, static_cast<XrTime>(time), state);
    auto const live = state;
    mevr::host::testloop::OnFrameBegun(s, base, static_cast<XrTime>(time), mevr::host::xr_host::InputFocused(), state);
    // Inactive samples must replace the previous live sample too (focus loss/unplug).
    mevr::vr::controllers::Publish(mevr::host::menus_le2::OnFrameBegun(s, base, static_cast<XrTime>(time), live, state,
        mevr::host::xr_host::InputFocused()));
    if (mevr::host::renderer::GameMode() != 0 || mevr::host::renderer::OwnMenuOpen())
        mevr::vr::host_view::Reset(); // flat/menu paths do not call DriveHeadLook this frame
}
void OnRecentered() noexcept {
    mevr::host::sequel_squad_layers::Recentered();
    InvalidateTraversalPresentation();
    gScreen = {};
    mevr::host::traversal_cue::Forget();
    mevr::host::menus_le2::OnRecentered();
    mevr::host::world_panels::Recenter();
    mevr::vr::controllers::Publish({}); // old app-space poses must not mix with the new head space
    mevr::host::xr_host::NoteRecentered();
    mevr::host::floor_lock::Reset();
    gFloorTranslatedMs.store(0, std::memory_order_relaxed);
}
void OnViewBuilt(bool worldView, bool headLook, bool headAim, int yaw, int pitch, float const q[4],
    float const p[3], float right, float up, float forward, float unitsPerMeter, bool floorWorld) noexcept {
    mevr::vr::host_view::State view;
    view.valid = worldView;
    view.headLook = headLook;
    view.headAim = headAim;
    view.head.orientation = {q[0], q[1], q[2], q[3]};
    view.head.position = {p[0], p[1], p[2]};
    view.lookYaw = -yaw;
    view.lookPitch = pitch;
    view.offsetRight = right;
    view.offsetUp = up;
    view.offsetForward = forward;
    view.unitsPerMeter = unitsPerMeter;
    mevr::vr::floor::State floor;
    mevr::host::xr_host::Floor(&floor);
    mevr::host::floor_lock::Queue(view, floor, floorWorld);
    auto const now = GetTickCount64();
    if (!worldView || now - gFloorTranslatedMs.load(std::memory_order_relaxed) > 100)
        mevr::vr::host_view::Publish(view, now);
}
float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ,
    float* right, float* up, float* forward) noexcept {
    gFloorTranslatedMs.store(GetTickCount64(), std::memory_order_relaxed);
    return mevr::host::floor_lock::Translate(replay, rawCameraZ, rightZ, upZ, forwardZ, right, up, forward);
}
void OnEyeSource(int eye, void* source) noexcept {
    mevr::host::testloop::OnEyeSource(eye, static_cast<ID3D11Texture2D*>(source));
}
void OnEyeCopied(int eye, void* image) noexcept {
    mevr::host::testloop::OnEyeCopied(eye, static_cast<ID3D11Texture2D*>(image));
}
bool CopyIntoImage(void* context, void* image, void* source, int shiftX, int eye) noexcept {
    return mevr::host::eye_copy::Copy(static_cast<ID3D11DeviceContext*>(context), static_cast<ID3D11Texture2D*>(image),
        static_cast<ID3D11Texture2D*>(source), shiftX, eye);
}
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept {
    if (index != 0 || !state) return result;
    auto const controllers = mevr::vr::controllers::Latest();
    if (!controllers.active) mevr::host::pad_merge::NoControllers();
    if (!controllers.active && !mevr::host::menus_le2::PadWanted() && !TraversalPresentationActive()) return result;
    std::lock_guard lock(gPadMutex);
    mevr::host::pad_merge::Renderer renderer;
    renderer.ownMenuOpen = mevr::host::renderer::OwnMenuOpen();
    renderer.headLookYawUU = &mevr::host::renderer::MoveStickHeadLookYawUU;
    renderer.gateRightClick = true;
    int const mode = mevr::host::renderer::GameMode();
    renderer.conversationTriggers = mevr::engine::adapter::IsConversationMode(mode);
    // No orbital mapping claim until LE3 native scan controls are audited (L3-9).
    auto pad = result == ERROR_SUCCESS ? state->Gamepad : XINPUT_GAMEPAD{};
    if (controllers.active) pad = mevr::host::pad_merge::Merge(pad, controllers, renderer, GetTickCount64());
    mevr::host::menus_le2::FilterPad(result == ERROR_SUCCESS ? &state->Gamepad : nullptr, pad);
    // Compensation follows both existing hand/head mapping and host input ownership. Never rotate
    // a native dialogue wheel or an open menu. Keep the shared renderer head-look value untouched.
    if (mevr::engine::adapter::GameMode(mode) == mevr::engine::adapter::Mode::OnFoot &&
        !renderer.ownMenuOpen && !mevr::host::menus_le2::PadWanted()) {
        mevr::vr::floor::State floor;
        mevr::host::xr_host::Floor(&floor);
        std::lock_guard traversalLock(gTraversalMutex);
        gTraversalPresentation.Apply(pad.sThumbLX, pad.sThumbLY, GetTickCount64(), floor.recenters,
            floor.sessions, mevr::host::xr_host::InputFocused());
    }
    // Conversation mode's native Activated() calls IgnoreMoveInput(true): the authored scene owns
    // traversal. Preserve left-stick axes for its reply wheel (BioSFHandler_Conversation.vInput).
    // Cinematics have no reply wheel. First-person world conversations remain disabled.
    if (mevr::engine::adapter::GameMode(mode) == mevr::engine::adapter::Mode::Cinematic) {
        pad.sThumbLX = pad.sThumbLY = 0;
    }
    if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) { gLastPad = pad; ++gPacket; }
    state->dwPacketNumber = gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
}
void OnPresent() noexcept { mevr::host::menus_le2::OnPresent(); }
unsigned OverlayLayers(OverlayFrame const& frame, void const** out, unsigned capacity) noexcept {
    if (!frame.context || !out || !capacity) return 0;
#if defined(MEVR_WITH_MAIN_MENU)
    if (!gNativePanelSeen && gNativePointerShown) {
        mevr::host::game_pointer::ForgetGameUiQuad();
        gNativePointerShown = false;
    }
#endif
    ID3D11Device* device = nullptr;
    frame.context->GetDevice(&device);
    // One submit owner. Suppress gameplay presentation under either menu or guided calibration.
    OverlayFrame gameplay = frame;
    gameplay.menuOpen = frame.menuOpen || MainMenuOpen() || mevr::host::menus_le2::PadWanted();
    bool const onFoot = mevr::engine::adapter::GameMode(frame.gameMode) == mevr::engine::adapter::Mode::OnFoot;
    unsigned count = 0;
    mevr::host::vignette::Frame comfort;
    comfort.session = static_cast<XrSession>(frame.session);
    comfort.context = frame.context; comfort.device = device;
    comfort.worldFrame = frame.worldFrame; comfort.menuOpen = gameplay.menuOpen;
    comfort.fovValid = frame.projectionValid;
    std::memcpy(comfort.fov, frame.projectionFov, sizeof comfort.fov);
    if (auto const* layer = mevr::host::vignette::Layer(comfort); layer && count < capacity) out[count++] = layer;
    mevr::host::hud_panel::Frame panel;
    panel.session = static_cast<XrSession>(frame.session);
    panel.appSpace = static_cast<XrSpace>(frame.appSpace);
    panel.context = frame.context;
    panel.device = device;
    panel.stereo = frame.stereo;
    panel.headValid = frame.headValid;
    panel.head = {{frame.headOrientation[0], frame.headOrientation[1], frame.headOrientation[2], frame.headOrientation[3]},
        {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]}};
    // This capture increment owns cinematic UI only. Gameplay HUD and native wrist routing have their own acceptance.
    panel.enabled = !gameplay.menuOpen;
    mevr::host::sequel_cinema::FillUi(gScreen, frame.headPosition, panel);
    if (auto const* ui = mevr::host::hud_panel::Layer(panel); ui && count < capacity) out[count++] = ui;
    count = mevr::host::sequel_weapon_layers::Layers(gameplay, device, onFoot ? 0 : -1, out, count, capacity, true);
    // Existing traversal label/blink, above weapon overlays and below menus; authored scenes own traversal.
    if (count < capacity) {
        mevr::host::traversal_cue::Frame cue;
        cue.session = static_cast<XrSession>(frame.session); cue.appSpace = static_cast<XrSpace>(frame.appSpace);
        cue.context = frame.context; cue.device = device;
        cue.worldFrame = frame.worldFrame && onFoot; cue.menuOpen = gameplay.menuOpen;
        cue.headValid = frame.headValid; cue.head = {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]};
        count += mevr::host::traversal_cue::Layers(cue, out + count, capacity - count);
    }
    if (count < capacity) {
        mevr::host::power_wheel::Frame wheel;
        wheel.session = static_cast<XrSession>(frame.session); wheel.appSpace = static_cast<XrSpace>(frame.appSpace);
        wheel.context = frame.context; wheel.device = device;
        wheel.worldFrame = frame.worldFrame && onFoot; wheel.menuOpen = gameplay.menuOpen;
        wheel.head = {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]}; wheel.headValid = frame.headValid;
        XrCompositionLayerQuad const* quads[3]{};
        unsigned const room = capacity - count < 3 ? capacity - count : 3;
        int const n = mevr::host::power_wheel::Layers(wheel, quads, static_cast<int>(room));
        for (int i = 0; i < n; ++i) out[count++] = quads[i];
    }
    gameplay.worldFrame = frame.worldFrame && onFoot;
    count = mevr::host::sequel_squad_layers::Place(gameplay, device, out, count, capacity);
    count = mevr::host::sequel_squad_layers::Panel(gameplay, device, out, count, capacity);
    count = mevr::host::menus_le2::Layers(frame, device, out, count, capacity);
    gScreen = {};
    if (device) device->Release();
    return count;
}
void PlaceNativePanel(OverlayFrame const& frame, float const viewPosition[3], float width, float height,
    void** space, float orientation[4], float position[3]) noexcept {
    namespace wp = mevr::host::world_panels;
    namespace ga = mevr::engine::adapter;
    auto const mode = ga::GameMode(frame.gameMode);
    auto const kind = mode == ga::Mode::Conversation ? wp::Kind::Conversation :
        mode == ga::Mode::Cinematic ? wp::Kind::Cutscene :
        mode == ga::Mode::Galaxy || mode == ga::Mode::Orbital ? wp::Kind::GalaxyMap : wp::Kind::GameMenu;
    auto const placed = wp::Place(kind, viewPosition, frame.viewSpace, frame.appSpace, frame.headValid,
        frame.headOrientation, frame.headPosition);
    *space = placed.space;
    std::memcpy(orientation, placed.orientation, sizeof placed.orientation);
    std::memcpy(position, placed.position, sizeof placed.position);
#if defined(MEVR_WITH_MAIN_MENU)
    // A complete uncropped native GUI frame has a known UV-to-window map. Authored dialogue and galaxy
    // retain native pad input until their own LE3 pointer/interrupt/navigation contracts are accepted.
    if (mode == ga::Mode::Gui && !placed.headLocked && !frame.menuOpen && !MainMenuOpen()) {
        XrPosef pose{{orientation[0], orientation[1], orientation[2], orientation[3]},
            {position[0], position[1], position[2]}};
        mevr::host::game_pointer::NoteGameUiQuad(pose, width, height, mevr::host::game_pointer::PanelKind::GameMenu);
        gNativePanelSeen = gNativePointerShown = true;
    }
#else
    (void)width; (void)height;
#endif
}

bool CineScreenWanted(bool conversation) noexcept { return mevr::host::cine_screen::Wanted(conversation); }
bool CineScreenFullRate() noexcept { return mevr::host::cine_screen::FullRate(); }
float CineScreenHalfSeparationUU(float halfH) noexcept { return mevr::host::sequel_cinema::HalfSeparationUU(halfH); }
unsigned CineScreenLayers(CineScreenFrame const& frame, void const** layers, unsigned capacity) noexcept {
    return mevr::host::sequel_cinema::Layers(frame, layers, capacity, gScreen);
}
bool HudPanelDraw(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool afterLeftEye) noexcept {
    return mevr::host::hud_panel::Redirect(context, draw, user, false, afterLeftEye);
}
void const* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept {
    return mevr::host::transition_fade::Layer(session, viewSpace, context, view);
}

}
