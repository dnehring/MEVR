// SPDX-License-Identifier: MIT
// LE3-specific composition only. Input, floor, mapping, HDR/colour and test machinery are shared with LE1/LE2.
#include "halcyon/le3/mevr_bridge.h"
#include "core/Log.hpp"
#include "core/Ini.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "game/NativeMapInput.hpp"
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
#include "plugin/GalaxyHologram.hpp"
#include "plugin/TransitionFade.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/HdrDisplayPolicy.hpp"
#include "plugin/HudPieceLayers.hpp"
#include "core/vr/InteractLabel.hpp"
#include "core/vr/PowerWheel.hpp"
#include "core/vr/SquadMenu.hpp"
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
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/Voice.hpp"
#include "plugin/Thespian.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/MainMenu.hpp"
#include "plugin/OmniToolRail.hpp"
#include "plugin/WorldUi.hpp"
#endif
#include <openxr/openxr.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
namespace {
std::atomic<std::uint64_t> gThespianContextMs{0};
std::atomic_bool gThespianFocused{false};
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
bool ThespianInputBlocked() noexcept {
#if defined(MEVR_WITH_MAIN_MENU)
    return !gThespianFocused.load(std::memory_order_acquire) ||
        !vr::thespian::InputContextFresh(gThespianContextMs.load(std::memory_order_acquire), GetTickCount64()) ||
        renderer::OwnMenuOpen() || calibration::Running() || main_menu::IsOpen() || menus_le2::PadWanted();
#else
    return true;
#endif
}
bool SetTraversalPresentation(vr::traversal::Presentation const* value) noexcept {
    vr::floor::State floor;
    xr_host::Floor(&floor);
    std::lock_guard lock(gTraversalMutex);
    return gTraversalPresentation.Set(value, GetTickCount64(), floor.recenters, floor.sessions);
}
}
namespace MevrSeams {
void OnFactory(void* factory) noexcept {
    mevr::host::hdr_display::OnFactory(factory);
}

namespace {
std::atomic<std::uint64_t> gFloorTranslatedMs{0};
std::mutex gPadMutex;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;
mevr::host::sequel_cinema::State gScreen;
bool gNativePanelSeen = false;
bool gNativePointerShown = false;
mevr::game::gamepad::NativeMapInputLease gMapInput; // gPadMutex, including owner-thread publications
std::uint64_t gMapGeneration = 0;
bool gMapTracked = false;
bool gMapSessionOwned = false; // gPadMutex; desktop/no-session input remains native
// Placement and space identity belong to the render/XR owner, never the input polling thread.
mevr::vr::galaxy_map::NativeTablePanel gGalaxyTable;
void* gGalaxySession = nullptr;
void* gGalaxySpace = nullptr;
bool gMapImageReady = false; // owner thread; an input lease needs a visible native map this frame
bool gGalaxyEnabled = false;
mevr::vr::galaxy_holo::TableSettings gGalaxySettings;
std::uint64_t gGalaxySettingsRead = 0;
void ResetGalaxy() {
    gGalaxyTable.Reset();
    gGalaxySession = gGalaxySpace = nullptr;
    gMapImageReady = false;
    std::lock_guard lock(gPadMutex);
    ++gMapGeneration;
    gMapTracked = false;
    gMapInput.Reset();
}
bool MapOwnersAllow() {
    if (mevr::host::renderer::OwnMenuOpen() || mevr::host::menus_le2::PadWanted() ||
        mevr::vr::power_wheel::Latest().open || mevr::vr::squad_menu::Latest().open) return false;
#if defined(MEVR_WITH_MAIN_MENU)
    if (mevr::host::calibration::Active() || mevr::host::main_menu::IsOpen() ||
        mevr::host::omni_rail::Shown() || mevr::host::omni_rail::OwnsTriggers() ||
        mevr::host::game_pointer::OwnsTriggers() || mevr::host::world_ui::Active() ||
        mevr::host::thespian::Shown() || mevr::host::thespian::HidesGameWheel()) return false;
#endif
    return true;
}
void ReadGalaxySettings() {
    auto const now = GetTickCount64();
    if (gGalaxySettingsRead && now - gGalaxySettingsRead < 1000) return;
    gGalaxySettingsRead = now;
    auto const file = mevr::ini::Read(mevr::host::SettingsPath()).file;
    // LE2's existing opt-in table settings, limits and defaults. No new map controls.
    gGalaxyEnabled = mevr::ini::GetNumber(file, "Galaxy", "nativeTable").value_or(0) != 0;
    auto number = [&](char const* key, float fallback, float lo, float hi) {
        auto const value = static_cast<float>(mevr::ini::GetNumber(file, "Galaxy", key).value_or(fallback));
        return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
    };
    gGalaxySettings.distanceM = number("tableDistanceM", 0.70f, 0.3f, 3.0f);
    gGalaxySettings.belowM = number("tableBelowM", 0.45f, 0.1f, 1.5f);
    gGalaxySettings.radiusM = number("tableRadiusM", 0.48f, 0.2f, 1.5f);
    gGalaxySettings.tiltDeg = number("worldTiltDeg", 35.0f, 0.0f, 80.0f);
}
}
void* StaticXrGetInstanceProcAddr() noexcept { return reinterpret_cast<void*>(&xrGetInstanceProcAddr); }
void WrapSessionFunctions(void** acquire, void** /*waitImage*/, void** release, void** /*waitFrame*/, void** /*beginFrame*/,
    void** endFrame, void** locateViews) noexcept {
    mevr::host::testloop::WrapSessionFunctions(acquire, release, endFrame, locateViews);
}
void OnSessionCreated(void* instance, void* session) noexcept {
    ResetGalaxy();
    { std::lock_guard lock(gPadMutex); gMapSessionOwned = session != nullptr; }
    gThespianContextMs.store(0, std::memory_order_release);
    gThespianFocused.store(false, std::memory_order_release);
    InvalidateTraversalPresentation();
    mevr::host::sequel_squad_layers::ForgetSession();
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::thespian::ForgetSession();
#endif
    mevr::host::hud_piece_layers::ForgetSession();
    mevr::vr::interact::Reset();
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
    ResetGalaxy();
    { std::lock_guard lock(gPadMutex); gMapSessionOwned = false; }
    gThespianContextMs.store(0, std::memory_order_release);
    gThespianFocused.store(false, std::memory_order_release);
    InvalidateTraversalPresentation();
    mevr::host::sequel_squad_layers::ForgetSession();
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::thespian::ForgetSession();
#endif
    mevr::host::hud_piece_layers::ForgetSession();
    mevr::vr::interact::Reset();
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
    if (session != gGalaxySession || space != gGalaxySpace) {
        ResetGalaxy();
        gGalaxySession = session;
        gGalaxySpace = space;
    }
    if (!mevr::engine::adapter::IsMapMode(mevr::host::renderer::GameMode())) gGalaxyTable.Reset();
    gMapImageReady = false;
    mevr::host::world_panels::NoteFrame();
    gScreen = {};
    gNativePanelSeen = false; // renewed only by an actual submitted native panel
    mevr::vr::controllers::State state;
    auto const s = static_cast<XrSession>(session);
    auto const base = static_cast<XrSpace>(space);
    mevr::host::xr_host::OnFrameBegun(s, base, static_cast<XrTime>(time), state);
    const bool focused = mevr::host::xr_host::InputFocused(); // owner thread only
    {
        std::lock_guard lock(gPadMutex);
        gMapTracked = focused && state.active;
        if (!gMapTracked) gMapInput.Reset();
    }
    gThespianFocused.store(focused, std::memory_order_release);
    if (!focused) gThespianContextMs.store(0, std::memory_order_release);
    auto const live = state;
    mevr::host::testloop::OnFrameBegun(s, base, static_cast<XrTime>(time), mevr::host::xr_host::InputFocused(), state);
    // Inactive samples must replace the previous live sample too (focus loss/unplug).
    mevr::vr::controllers::Publish(mevr::host::menus_le2::OnFrameBegun(s, base, static_cast<XrTime>(time), live, state,
        mevr::host::xr_host::InputFocused()));
    if (mevr::host::renderer::GameMode() != 0 || mevr::host::renderer::OwnMenuOpen())
        mevr::vr::host_view::Reset(); // flat/menu paths do not call DriveHeadLook this frame
}
void OnRecentered() noexcept {
    ResetGalaxy();
    gThespianContextMs.store(0, std::memory_order_release);
    mevr::host::sequel_squad_layers::Recentered();
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::thespian::Recentered();
#endif
    mevr::vr::interact::Reset();
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
void OnEyeCopied(int eye, void* image, std::uint32_t /*index*/, int /*acquired*/, int /*waited*/) noexcept {
    mevr::host::testloop::OnEyeCopied(eye, static_cast<ID3D11Texture2D*>(image));
}
void OnEyeReleased(int eye, bool published) noexcept {
    mevr::host::testloop::OnEyeReleased(eye, published);
}
bool CopyIntoImage(void* context, void* image, void* source, int shiftX, int eye) noexcept {
    return mevr::host::eye_copy::Copy(static_cast<ID3D11DeviceContext*>(context), static_cast<ID3D11Texture2D*>(image),
        static_cast<ID3D11Texture2D*>(source), shiftX, eye);
}
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept {
    if (index != 0 || !state) return result;
    auto const controllers = mevr::vr::controllers::Latest();
    if (!controllers.active) mevr::host::pad_merge::NoControllers();
    std::lock_guard lock(gPadMutex);
    int const mode = mevr::host::renderer::GameMode();
    bool const mapActive = gMapSessionOwned && mevr::engine::adapter::IsMapMode(mode);
    if (!mapActive) gMapInput.Reset();
    if (!controllers.active && !mapActive && !mevr::host::menus_le2::PadWanted() && !TraversalPresentationActive()) return result;
    mevr::host::pad_merge::Renderer renderer;
    renderer.ownMenuOpen = mevr::host::renderer::OwnMenuOpen();
    renderer.headLookYawUU = &mevr::host::renderer::MoveStickHeadLookYawUU;
    renderer.gateRightClick = true;
    renderer.conversationTriggers = mevr::engine::adapter::IsConversationMode(mode);
    // Both native map modes need their LT/RT pair; stale on-foot hand/squad flags must
    // not remap scan to LB or rotate pursuit steering toward a hand.
    renderer.orbitalTriggers = mapActive;
    auto pad = result == ERROR_SUCCESS ? state->Gamepad : XINPUT_GAMEPAD{};
    if (controllers.active) pad = mevr::host::pad_merge::Merge(pad, controllers, renderer, GetTickCount64());
    // Before menu/pointer filters turn held triggers into synthetic zeroes. The lease
    // checks real focus/tracking published by the XR owner, never XR on this thread.
    gMapInput.Apply(mapActive, mevr::engine::adapter::IsOrbitalMode(mode), gMapGeneration,
        controllers.active && gMapTracked && MapOwnersAllow(), GetTickCount64(), pad);
    mevr::host::menus_le2::FilterPad(result == ERROR_SUCCESS ? &state->Gamepad : nullptr, pad);
    // Compensation follows both existing hand/head mapping and host input ownership. Never rotate
    // a native dialogue wheel or an open menu. Keep the shared renderer head-look value untouched.
    if (mevr::engine::adapter::GameMode(mode) == mevr::engine::adapter::Mode::OnFoot &&
        !renderer.ownMenuOpen && !mevr::host::menus_le2::PadWanted()) {
        mevr::vr::floor::State floor;
        mevr::host::xr_host::Floor(&floor);
        std::lock_guard traversalLock(gTraversalMutex);
        gTraversalPresentation.Apply(pad.sThumbLX, pad.sThumbLY, GetTickCount64(), floor.recenters,
            floor.sessions, gThespianFocused.load(std::memory_order_acquire));
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
} // namespace MevrSeams
namespace ME2VR::MevrBridge {
using namespace MevrSeams;
void OnPresent() noexcept {
    mevr::host::testloop::OnPresent();
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::voice::OnPresent();
#endif
    mevr::host::menus_le2::OnPresent();
}
} // namespace ME2VR::MevrBridge
namespace MevrSeams {
unsigned OverlayLayers(OverlayFrame const& frame, void const** out, unsigned capacity) noexcept {
    if (!frame.context || !out || !capacity) {
        { std::lock_guard lock(gPadMutex); gMapInput.Reset(); }
        gThespianContextMs.store(0, std::memory_order_release);
        return 0;
    }
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
    // No LE1 crop is assumed. A measured explicit prompt crop opts into the shared HUD capture;
    // otherwise the complete native gameplay HUD stays in the eye images.
    namespace il = mevr::vr::interact;
    namespace hpl = mevr::host::hud_piece_layers;
    auto const label = il::Latest();
    bool const labelWanted = onFoot && frame.worldFrame && frame.headValid && !gameplay.menuOpen &&
        label.visible && il::ExplicitCrop(label).Valid() && capacity - count >= 2 &&
        !mevr::vr::power_wheel::Latest().open && !mevr::vr::squad_menu::Latest().open;
    panel.worldFrame = frame.worldFrame && !gameplay.menuOpen;
    panel.gameplay = onFoot && labelWanted;
    panel.enabled = !gameplay.menuOpen;
    auto const cfg = mevr::host::renderer::Hud();
    panel.distanceM = cfg.hudPanelDistanceM; panel.widthM = cfg.hudPanelWidthM;
    panel.dropM = cfg.hudPanelDropM; panel.followDeg = cfg.hudPanelFollowDeg;
    mevr::host::sequel_cinema::FillUi(gScreen, frame.headPosition, panel);
    hpl::Common common;
    common.session = panel.session; common.appSpace = panel.appSpace;
    common.context = frame.context; common.device = device; common.head = panel.head.position;
    common.nowMs = GetTickCount64();
    auto const* interaction = hpl::Interaction(common,
        labelWanted && !panel.screenUi && panel.stereo && mevr::host::hud_panel::Ready(), panel.masks);
    auto const* ui = mevr::host::hud_panel::Layer(panel);
    if (ui && count < capacity) out[count++] = ui;
    // A failed panel leaves the original native prompt unclaimed. Never submit a detached label alone.
    if (interaction && ui && count < capacity) out[count++] = interaction;
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
#if defined(MEVR_WITH_MAIN_MENU)
    // The existing spoken-reply panel is a cinema overlay. Native adapter owns the publication;
    // a menu/calibration, missing tracking, other mode or exhausted budget always hides it.
    mevr::host::thespian::Frame spoken;
    spoken.session = static_cast<XrSession>(frame.session);
    spoken.appSpace = static_cast<XrSpace>(frame.appSpace);
    spoken.context = frame.context; spoken.device = device;
    spoken.head = {{frame.headOrientation[0], frame.headOrientation[1], frame.headOrientation[2], frame.headOrientation[3]},
        {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]}};
    spoken.othersOwnInput = !mevr::engine::adapter::IsConversationMode(frame.gameMode) ||
        !frame.headValid || count >= capacity || gameplay.menuOpen ||
        mevr::host::renderer::OwnMenuOpen() || mevr::host::calibration::Running() ||
        !gThespianFocused.load(std::memory_order_acquire);
    // Context eligibility, not Layer() success/panel visibility: a turn must be able
    // to start. Any hidden context revokes its lease; stopped rendering expires it.
    gThespianContextMs.store(spoken.othersOwnInput ? 0 : GetTickCount64(), std::memory_order_release);
    if (auto const* layer = mevr::host::thespian::Layer(spoken)) out[count++] = layer;
#endif
    // The shared ME1 stereo map window; LE3's native adapter supplies measured
    // projector geometry and its own map captures. Explicit diagnostic only.
    if (capacity - count >= 2) {
        mevr::host::galaxy_hologram::Frame galaxy;
        galaxy.session = static_cast<XrSession>(frame.session);
        galaxy.appSpace = static_cast<XrSpace>(frame.appSpace);
        galaxy.context = frame.context; galaxy.device = device;
        galaxy.gameMode = frame.gameMode; galaxy.menuOpen = gameplay.menuOpen;
        galaxy.headValid = frame.headValid; galaxy.head = panel.head;
        XrCompositionLayerBaseHeader const* layers[2]{};
        int const n = mevr::host::galaxy_hologram::WindowLayers(galaxy, layers);
        for (int i = 0; i < n; ++i) out[count++] = layers[i];
    }
    count = mevr::host::menus_le2::Layers(frame, device, out, count, capacity);
    {
        std::lock_guard lock(gPadMutex);
        gMapInput.Publish(mevr::engine::adapter::IsMapMode(frame.gameMode),
            mevr::engine::adapter::IsOrbitalMode(frame.gameMode), gMapGeneration,
            gMapSessionOwned && frame.session && frame.appSpace &&
                frame.session == gGalaxySession && frame.appSpace == gGalaxySpace &&
                frame.headValid && gMapImageReady && !gameplay.menuOpen &&
                gMapTracked && MapOwnersAllow(),
            GetTickCount64());
    }
    gScreen = {};
    if (device) device->Release();
    return count;
}
bool PlaceNativePanel(OverlayFrame const& frame, float const viewPosition[3], float& width, float& height,
    void** space, float orientation[4], float position[3]) noexcept {
    namespace wp = mevr::host::world_panels;
    namespace ga = mevr::engine::adapter;
    auto const mode = ga::GameMode(frame.gameMode);
    gMapImageReady = ga::IsMapMode(frame.gameMode) && std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0;
    auto const kind = mode == ga::Mode::Conversation ? wp::Kind::Conversation :
        mode == ga::Mode::Cinematic ? wp::Kind::Cutscene :
        mode == ga::Mode::Galaxy || mode == ga::Mode::Orbital ? wp::Kind::GalaxyMap : wp::Kind::GameMenu;
    auto const placed = wp::Place(kind, viewPosition, frame.viewSpace, frame.appSpace, frame.headValid,
        frame.headOrientation, frame.headPosition);
    *space = placed.space;
    std::memcpy(orientation, placed.orientation, sizeof placed.orientation);
    std::memcpy(position, placed.position, sizeof placed.position);
    ReadGalaxySettings();
    XrPosef const head{{frame.headOrientation[0], frame.headOrientation[1], frame.headOrientation[2], frame.headOrientation[3]},
        {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]}};
    // The renderer calls only for its ready native image. Start with the original panel
    // above and override only after successful placement; unavailable tracking/image keeps
    // its established fallback. NativeTablePanel's aspect is height / width, like LE2.
    auto const table = gGalaxyTable.Update(ga::IsMapMode(frame.gameMode),
        gGalaxyEnabled && frame.session && frame.appSpace &&
            frame.session == gGalaxySession && frame.appSpace == gGalaxySpace,
        frame.headValid && gMapImageReady, head, width > 0 ? height / width : 0, gGalaxySettings);
    if (table) {
        *space = frame.appSpace;
        auto const& q = table->pose.orientation;
        auto const& p = table->pose.position;
        orientation[0] = q.x; orientation[1] = q.y; orientation[2] = q.z; orientation[3] = q.w;
        position[0] = p.x; position[1] = p.y; position[2] = p.z;
        width = table->width;
        height = table->height;
    }
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
    return gMapImageReady;
}

// The common PlacePanel on ME3: every native panel through PlaceNativePanel above (ByGameMode).
PanelPose PlacePanel(PanelKind /*kind*/, OverlayFrame const& frame, float const viewPosition[3], float widthM, float heightM,
    bool /*imageReady*/) noexcept {
    PanelPose out;
    out.widthM = widthM;
    out.heightM = heightM;
    out.nativeMap = PlaceNativePanel(frame, viewPosition, out.widthM, out.heightM, &out.space, out.orientation, out.position);
    out.headLocked = out.space == frame.viewSpace;
    out.placed = true;
    return out;
}
} // namespace MevrSeams
namespace ME2VR::MevrBridge {
using namespace MevrSeams;
float CineScreenHalfSeparationUU(float halfH) noexcept { return mevr::host::sequel_cinema::HalfSeparationUU(halfH); }
} // namespace ME2VR::MevrBridge
namespace MevrSeams {
unsigned CineScreenLayers(CineScreenFrame const& frame, void const** layers, unsigned capacity) noexcept {
    return mevr::host::sequel_cinema::Layers(frame, layers, capacity, gScreen);
}
bool HudPanelDraw(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool textBatch, bool afterLeftEye) noexcept {
    return mevr::host::hud_panel::Redirect(context, draw, user, textBatch, afterLeftEye);
}
void const* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept {
    return mevr::host::transition_fade::Layer(session, viewSpace, context, view);
}

} // namespace MevrSeams
