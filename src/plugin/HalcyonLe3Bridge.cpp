// SPDX-License-Identifier: MIT
// ME3's part of the sequels' bridge (plugin/HalcyonSequelBridge.cpp has the common seams, plugin/SequelOverlays.cpp the
// layer composition): the adapter for what ME3 alone has (its native map's input lease, the spoken-reply panel's context
// lease, the traversal presentation, its galaxy window), its PlacePanel (every native panel in the room), the eye-image
// seams (lane EY's) and ME3's own seam (src/halcyon/le3/mevr_bridge.h: OnPresent).
#include "halcyon/le3/mevr_bridge.h"
#include "core/Log.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "game/NativeMapInput.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "engine/GameAdapter.hpp"
#include "plugin/HalcyonSequelBridge.hpp"
#include "plugin/TraversalCue.hpp"
#include "plugin/TraversalPresentation.hpp"
#include "plugin/HalcyonLe3.hpp"
#include "plugin/GalaxyHologram.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/HdrDisplayPolicy.hpp"
#include "core/vr/InteractLabel.hpp"
#include "core/vr/PowerWheel.hpp"
#include "core/vr/SquadMenu.hpp"
#include "plugin/EyeCopy.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/MenusLe2.hpp"
#include "plugin/WorldPanels.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/GamePointer.hpp"
#endif
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
#include <d3d11.h>
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
namespace {
namespace sb = mevr::host::sequel_bridge;
bool gNativePanelSeen = false;
bool gNativePointerShown = false;
mevr::game::gamepad::NativeMapInputLease gMapInput; // sb::PadMutex(), including owner-thread publications
std::uint64_t gMapGeneration = 0;
bool gMapTracked = false;
bool gMapSessionOwned = false; // sb::PadMutex(); desktop/no-session input remains native
// Placement and space identity belong to the render/XR owner, never the input polling thread.
mevr::vr::galaxy_map::NativeTablePanel gGalaxyTable;
void* gGalaxySession = nullptr;
void* gGalaxySpace = nullptr;
bool gMapImageReady = false; // owner thread; an input lease needs a visible native map this frame
void ResetGalaxy() {
    gGalaxyTable.Reset();
    gGalaxySession = gGalaxySpace = nullptr;
    gMapImageReady = false;
    std::lock_guard lock(sb::PadMutex());
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
bool MapActive(int mode) {  // sb::PadMutex() held
    return gMapSessionOwned && mevr::engine::adapter::IsMapMode(mode);
}
}

// ---- ME3's adapter (plugin/HalcyonSequelBridge.hpp) ----------------------------------------------------------------------
namespace mevr::host::sequel_bridge::game {

Facts const& Get() noexcept {
    static Facts const facts{
        "halcyon le3 bridge",
        true,   // invalidViewAtOnce: the flat and menu paths stop the floor lock's pass without a last invalid view
        true,   // panelColour: the game's UI on the panel gets the eyes' correction, as on ME1 and ME2 (research/27 section 9)
        0,      // hudLayout: no HUD pieces on ME3 yet (capability hudPieces)
    };
    return facts;
}

void SessionCreated(void* /*instance*/, void* session) noexcept {
    ResetGalaxy();
    { std::lock_guard lock(PadMutex()); gMapSessionOwned = session != nullptr; }
    gThespianContextMs.store(0, std::memory_order_release);
    gThespianFocused.store(false, std::memory_order_release);
    InvalidateTraversalPresentation();
}

void SessionStopped() noexcept {
    ResetGalaxy();
    { std::lock_guard lock(PadMutex()); gMapSessionOwned = false; }
    gThespianContextMs.store(0, std::memory_order_release);
    gThespianFocused.store(false, std::memory_order_release);
    InvalidateTraversalPresentation();
    gNativePanelSeen = gNativePointerShown = false;
}

void FrameBegun(void* session, void* space, bool focused, bool tracked) noexcept {
    if (session != gGalaxySession || space != gGalaxySpace) {
        ResetGalaxy();
        gGalaxySession = session;
        gGalaxySpace = space;
    }
    if (!engine::adapter::IsMapMode(renderer::GameMode())) gGalaxyTable.Reset();
    gMapImageReady = false;
    gNativePanelSeen = false; // renewed only by an actual submitted native panel
    {
        std::lock_guard lock(PadMutex());
        gMapTracked = focused && tracked;
        if (!gMapTracked) gMapInput.Reset();
    }
    gThespianFocused.store(focused, std::memory_order_release);
    if (!focused) gThespianContextMs.store(0, std::memory_order_release);
}

void FramePublished() noexcept {
    if (renderer::GameMode() != 0 || renderer::OwnMenuOpen())
        vr::host_view::Reset(); // flat/menu paths do not call DriveHeadLook this frame
}

// ME3's own state placed in the old app space: its galaxy table and map lease, its spoken-reply lease, its traversal
// presentation. The rest is the one recenter rule (research/trilogy-seams.md "One recenter rule"): until 2026-11-16 ME3 also
// dropped the published controllers, the floor lock's views, the interaction label, the traversal cue and the frame's
// cinema screen; the controllers' drop showed the mod no controllers for the ticks until the next sample (every held grip
// and trigger let go), and the others were sent again by the mod or placed again within the frame.
void Recentered() noexcept {
    ResetGalaxy();
    gThespianContextMs.store(0, std::memory_order_release);
    InvalidateTraversalPresentation();
}

bool PadKeeps(int gameMode) noexcept {
    bool const mapActive = MapActive(gameMode);
    if (!mapActive) gMapInput.Reset();
    return mapActive || TraversalPresentationActive();
}

// Both native map modes need their LT/RT pair; stale on-foot hand/squad flags must
// not remap scan to LB or rotate pursuit steering toward a hand.
bool PadOrbitalTriggers(int gameMode) noexcept {
    return MapActive(gameMode);
}

// Before menu/pointer filters turn held triggers into synthetic zeroes. The lease
// checks real focus/tracking published by the XR owner, never XR on this thread.
void PadMerged(int gameMode, bool controllersActive, XINPUT_GAMEPAD& pad) noexcept {
    gMapInput.Apply(MapActive(gameMode), engine::adapter::IsOrbitalMode(gameMode), gMapGeneration,
        controllersActive && gMapTracked && MapOwnersAllow(), GetTickCount64(), pad);
}

void PadFiltered(int gameMode, bool ownMenuOpen, XINPUT_GAMEPAD& pad) noexcept {
    // Compensation follows both existing hand/head mapping and host input ownership. Never rotate
    // a native dialogue wheel or an open menu. Keep the shared renderer head-look value untouched.
    if (engine::adapter::GameMode(gameMode) == engine::adapter::Mode::OnFoot && !ownMenuOpen && !menus_le2::PadWanted()) {
        vr::floor::State floor;
        xr_host::Floor(&floor);
        std::lock_guard traversalLock(gTraversalMutex);
        gTraversalPresentation.Apply(pad.sThumbLX, pad.sThumbLY, GetTickCount64(), floor.recenters,
            floor.sessions, gThespianFocused.load(std::memory_order_acquire));
    }
    // Conversation mode's native Activated() calls IgnoreMoveInput(true): the authored scene owns
    // traversal. Preserve left-stick axes for its reply wheel (BioSFHandler_Conversation.vInput).
    // Cinematics have no reply wheel. First-person world conversations remain disabled.
    if (engine::adapter::GameMode(gameMode) == engine::adapter::Mode::Cinematic) {
        pad.sThumbLX = pad.sThumbLY = 0;
    }
}

void UiDraw(ID3D11DeviceContext* /*context*/, bool /*afterLeftEye*/) noexcept {}

void OverlayBegin(MevrSeams::OverlayFrame const& /*frame*/, bool usable) noexcept {
    if (!usable) {
        { std::lock_guard lock(PadMutex()); gMapInput.Reset(); }
        gThespianContextMs.store(0, std::memory_order_release);
        return;
    }
#if defined(MEVR_WITH_MAIN_MENU)
    if (!gNativePanelSeen && gNativePointerShown) {
        game_pointer::ForgetGameUiQuad();
        gNativePointerShown = false;
    }
#endif
}

// No LE1 crop is assumed. A measured explicit prompt crop opts into the shared HUD capture;
// otherwise the complete native gameplay HUD stays in the eye images.
bool OverlayLabel(OverlayContext const& /*context*/) noexcept {
    auto const label = vr::interact::Latest();
    return label.visible && vr::interact::ExplicitCrop(label).Valid();
}

int OverlayReticleMode(OverlayContext const& context) noexcept {
    return context.onFoot ? 0 : -1;
}

// The shared ME1 stereo map window; LE3's native adapter supplies measured
// projector geometry and its own map captures. Explicit diagnostic only.
unsigned OverlayGameLayers(OverlayContext const& context, hud_panel::Frame const& panel, void const** layersOut,
    unsigned count, unsigned capacity) noexcept {
    if (capacity - count >= 2) {
        MevrSeams::OverlayFrame const& frame = *context.frame;
        galaxy_hologram::Frame galaxy;
        galaxy.session = static_cast<XrSession>(frame.session);
        galaxy.appSpace = static_cast<XrSpace>(frame.appSpace);
        galaxy.context = frame.context; galaxy.device = context.device;
        galaxy.gameMode = frame.gameMode; galaxy.menuOpen = context.menuOpen;
        galaxy.headValid = frame.headValid; galaxy.head = panel.head;
        XrCompositionLayerBaseHeader const* layers[2]{};
        int const n = galaxy_hologram::WindowLayers(galaxy, layers);
        for (int i = 0; i < n; ++i) layersOut[count++] = layers[i];
    }
    return count;
}

bool OverlaySpokenBlocked(OverlayContext const& /*context*/) noexcept {
#if defined(MEVR_WITH_MAIN_MENU)
    return calibration::Running() || !gThespianFocused.load(std::memory_order_acquire);
#else
    return false;
#endif
}

// Context eligibility, not Layer() success/panel visibility: a turn must be able
// to start. Any hidden context revokes its lease; stopped rendering expires it.
void OverlaySpokenContext(bool eligible) noexcept {
    gThespianContextMs.store(eligible ? GetTickCount64() : 0, std::memory_order_release);
}

void OverlayEnd(OverlayContext const& context) noexcept {
    MevrSeams::OverlayFrame const& frame = *context.frame;
    std::lock_guard lock(PadMutex());
    gMapInput.Publish(engine::adapter::IsMapMode(frame.gameMode), engine::adapter::IsOrbitalMode(frame.gameMode),
        gMapGeneration,
        gMapSessionOwned && frame.session && frame.appSpace &&
            frame.session == gGalaxySession && frame.appSpace == gGalaxySpace &&
            frame.headValid && gMapImageReady && !context.menuOpen &&
            gMapTracked && MapOwnersAllow(),
        GetTickCount64());
}

} // namespace mevr::host::sequel_bridge::game

// ---- The eye-image seams (lane EY's: bodies as they were) ----------------------------------------------------------------
namespace MevrSeams {
void OnFactory(void* factory) noexcept {
    mevr::host::hdr_display::OnFactory(factory);
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
} // namespace MevrSeams
// ---- ME3's own seam (src/halcyon/le3/mevr_bridge.h) ----------------------------------------------------------------------
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
// ---- ME3's PlacePanel: every native panel in the room (capability panelsInRoom), the native map on ME1's table -----------
namespace MevrSeams {
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
    auto const& tableSettings = mevr::host::sequel_bridge::GalaxyTableSettings();
    XrPosef const head{{frame.headOrientation[0], frame.headOrientation[1], frame.headOrientation[2], frame.headOrientation[3]},
        {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]}};
    // The renderer calls only for its ready native image. Start with the original panel
    // above and override only after successful placement; unavailable tracking/image keeps
    // its established fallback. NativeTablePanel's aspect is height / width, like LE2.
    auto const table = gGalaxyTable.Update(ga::IsMapMode(frame.gameMode),
        tableSettings.enabled && frame.session && frame.appSpace &&
            frame.session == gGalaxySession && frame.appSpace == gGalaxySpace,
        frame.headValid && gMapImageReady, head, width > 0 ? height / width : 0, tableSettings.settings);
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
