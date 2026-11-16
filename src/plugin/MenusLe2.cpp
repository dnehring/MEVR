// SPDX-License-Identifier: MIT
// Our menus on Halcyon's ME2 renderer (plugin/MenusLe2.hpp): LE1's glue for our main menu and the guided calibration
// (plugin/HalcyonBridge.cpp's OnFrameBegun, OnMenuKey and MergeMotionControllers parts, plugin/HalcyonOverlays.cpp's layer
// composition), for ME2's bridge. Kept beside it so that file (lane B's) only calls in.

#include "plugin/MenusLe2.hpp"

#include "halcyon/MevrSeams.h"
#include "engine/GameAdapter.hpp"

#include "core/Log.hpp"
#include "game/GamepadMapping.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/XrHost.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "core/Ini.hpp"
#include "core/vr/HudPieces.hpp"
#include "core/vr/LiveCalibration.hpp"
#include "core/vr/ManualCalibration.hpp"
#include "core/vr/OmniRail.hpp"
#include "core/vr/WorldUi.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/GamePointer.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/MainMenu.hpp"
#include "plugin/OmniToolRail.hpp"
#include "plugin/WorldUi.hpp"
#include "plugin/WristBeam.hpp"
#endif

#include <openxr/openxr.h>

#include <d3d11.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <mutex>

namespace mevr::host::menus_le2 {
namespace {

std::mutex gNamesMutex;
std::string gRuntime;
std::string gSystem;

#if defined(MEVR_WITH_MAIN_MENU)
constexpr unsigned kProfileCheckEvery = 90;  // frames between interaction-profile checks (LE1's bridge: the same)

// Present thread.
XrSession gSession = XR_NULL_HANDLE;
XrTime gTime = 0;               // the last frame's predicted display time (to locate the head for our quads)
unsigned gProfileFrames = 0;
std::string gProfile;
game::gamepad::StickChord gChord;

// The XInput hook (any thread).
std::mutex gPadMutex;
game::gamepad::CloseLatch gLatch;
bool gLatchKept = false;

XrQuaternionf Quat(float const q[4]) {
    return {q[0], q[1], q[2], q[3]};
}
XrVector3f Vec(float const p[3]) {
    return {p[0], p[1], p[2]};
}

// [Hud] in ASI\LE2MEVR.ini, re-read within a second of a change (LE1's HalcyonOverlays::HudSettings): the rail's wrist point is
// the HUD's wrist piece's (wristOffset in the left grip's axes), ME1's default unless the player moved it. Present thread.
vr::hud_pieces::Settings const& HudSettings() {
    static vr::hud_pieces::Settings settings;
    static std::uint64_t nextMs = 0;
    static FILETIME stamp{};
    static bool loaded = false;
    std::uint64_t const now = GetTickCount64();
    if (now < nextMs) {
        return settings;
    }
    nextMs = now + 1000;
    std::wstring const& path = SettingsPath();
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return settings;
    }
    if (loaded && CompareFileTime(&data.ftLastWriteTime, &stamp) == 0) {
        return settings;
    }
    stamp = data.ftLastWriteTime;
    loaded = true;
    std::string problems;
    settings = vr::hud_pieces::ReadSettings(ini::Read(std::filesystem::path(path)).file, &problems);
    if (!problems.empty()) {
        log::Warn("menus le2: [Hud] {}", problems);
    }
    return settings;
}

// ME2's game screens on the wrist (BeforePanel, PanelLayers; Present thread): this frame's, as LE1's HalcyonOverlays keeps it.
int gLastGameMode = -1;                          // the last readable game mode (LE1's gLastGameMode)
bool gWorldUiOwns = false;                       // a screen of ours takes the panel this frame
std::optional<world_ui::Shown> gWorldShown;      // where it stands this frame (nullopt: not placed yet)
XrCompositionLayerQuad gWorldQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gWorldShownLogged = false;
bool gWorldPointerShown = false;
#endif

} // namespace

void OnSessionCreated(XrInstance instance, XrSession session) {
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
    XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};
    if (instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrGetSystem(instance, &systemInfo, &system)) &&
        XR_SUCCEEDED(xrGetSystemProperties(instance, system, &properties)) &&
        XR_SUCCEEDED(xrGetInstanceProperties(instance, &runtime))) {
        std::lock_guard lock(gNamesMutex);
        gRuntime = runtime.runtimeName;
        gSystem = properties.systemName;
#if defined(MEVR_WITH_MAIN_MENU)
        calibration::SetSystem(properties.systemName);  // the calibration's per-controller-type sections
#endif
    }
#if defined(MEVR_WITH_MAIN_MENU)
    OnSessionStopped();
    gSession = session;
    gProfileFrames = 0;
    gProfile.clear();
    game_pointer::ForgetSession();  // the pointer's dot went with the old session
#else
    (void)session;
#endif
}

vr::controllers::State OnFrameBegun(XrSession session, XrSpace space, XrTime time, vr::controllers::State const& live,
    vr::controllers::State const& state, bool focused) {
#if defined(MEVR_WITH_MAIN_MENU)
    (void)space;
    if (session != gSession) {
        return state;
    }
    gTime = time;
    if (xr_host::InputReady() && gProfileFrames++ % kProfileCheckEvery == 0) {
        std::string const right = xr_host::InteractionProfile(1);
        std::string const profile = !right.empty() ? right : xr_host::InteractionProfile(0);
        if (!profile.empty() && profile != gProfile) {
            gProfile = profile;
            log::Info("menus le2: the controllers' interaction profile is {} (the calibration is kept per it)", profile);
        }
        if (!profile.empty()) {
            calibration::SetProfile(profile);
        }
    }
    // The guided calibration takes the controllers while it runs, and our main menu while it's open: everyone else sees none.
    calibration::NoteControllers(state);
    main_menu::NoteControllers(state);
    bool const focusedOrReplay = focused || testloop::ReplayDrivesInputs();
    calibration::NoteFocused(focusedOrReplay);  // the first-run prompt's clocks run only while it can be seen
    bool const liveAdjustment = main_menu::LiveAdjusting();
    auto const manual = main_menu::PreviewAdjustments(calibration::ManualAdjustments());
    vr::controllers::State published = calibration::Active() || (main_menu::IsOpen() && !liveAdjustment)
        ? calibration::Hidden(state) : vr::manual_calibration::Apply(state, manual);
    if (liveAdjustment) {
        published = vr::live_calibration::PosesOnly(published);
    }
    // The player's own stick clicks (a replay's never open the menu, as on LE1).
    std::uint64_t const now = GetTickCount64();
    game::gamepad::StickChordStep const chord =
        game::gamepad::UpdateStickChord(gChord, live.active && live.left.stickClick, live.active && live.right.stickClick, now);
    if (chord.hold && calibration::Running()) {
        calibration::Cancel("both sticks held");
    } else if (chord.hold && renderer::OwnMenuOpen()) {
        renderer::SetOwnMenuOpen(false);
        log::Info("menus le2: both sticks held: Halcyon's menu closed");
    } else if (chord.hold) {
        main_menu::Toggle("both sticks held");
    }
    if (chord.tap) {
        log::Info("test loop: both sticks tapped: snapshot");
        testloop::SnapshotRequest request;
        request.trigger = "stick tap";
        testloop::RequestSnapshot(request);
    }
    return published;
#else
    (void)session, (void)space, (void)time, (void)live, (void)focused;
    return state;
#endif
}

WorldUiPanel BeforePanel(MevrSeams::OverlayFrame const& frame, unsigned panelWidth, unsigned panelHeight) {
    WorldUiPanel out;
#if defined(MEVR_WITH_MAIN_MENU)
    if (frame.gameMode >= 0) {
        gLastGameMode = frame.gameMode;  // an unreadable mode (-1) keeps the last one, as LE1's overlays
    }
    // LE1's HalcyonOverlays: a screen of ours standing (the mod's state is fresh in the GUI or minigame mode), or one being opened
    // (the mod reserved it just before the game's call: mode 7, or still 0-4 as the call changes it). Either way the game's UI goes
    // to the panel (it isn't drawn into the eyes), shown only once the screen is placed.
    bool const active = vr::world_ui::ScreenMode(gLastGameMode) && world_ui::Active();
    bool const pending = (world_ui::Ownership(gLastGameMode) & 6u) != 0;
    gWorldUiOwns = active || pending;
    gWorldShown.reset();
    if (active && hud_panel::HasTexture()) {
        vr::controllers::State const controllers = vr::controllers::Latest();
        std::optional<XrVector3f> wrist;
        if (controllers.active && controllers.left.gripValid) {
            wrist = vr::hud_pieces::WristPoint(controllers.left.grip, HudSettings().wristOffset);  // the HUD's wrist piece's point
        }
        gWorldShown = world_ui::Current(XrVector3f{frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]},
            XrQuaternionf{frame.headOrientation[0], frame.headOrientation[1], frame.headOrientation[2], frame.headOrientation[3]},
            frame.headValid, panelWidth, panelHeight, wrist);
    }
    out.owns = gWorldUiOwns;
    out.backdrop = gWorldShown ? std::max(gWorldShown->backdrop, 1.0f / 256.0f) : 0.0f;  // LE1's gPanelBackdrop
#else
    (void)frame, (void)panelWidth, (void)panelHeight;
#endif
    return out;
}

unsigned PanelLayers(MevrSeams::OverlayFrame const& frame, ID3D11Device* device, void const* panelLayer,
    void const** layersOut, unsigned count, unsigned capacity) {
    if (layersOut == nullptr) {
        return count;
    }
#if defined(MEVR_WITH_MAIN_MENU)
    if (!gWorldUiOwns || panelLayer == nullptr || !gWorldShown || count >= capacity) {
        if (gWorldPointerShown) {
            game_pointer::ForgetGameUiQuad();
            gWorldPointerShown = false;
        }
        if (!gWorldUiOwns && panelLayer != nullptr && count < capacity) {
            layersOut[count++] = panelLayer;  // the HUD in front of the body, as before
        }
        return count;
    }
    world_ui::Shown const& shown = *gWorldShown;
    // A screen projected from the wrist: its fan of light first, under the screen (LE1's order).
    if (shown.beam && count + 1 < capacity && device != nullptr) {
        wrist_beam::Frame beamFrame;
        beamFrame.session = static_cast<XrSession>(frame.session);
        beamFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
        beamFrame.context = frame.context;
        beamFrame.device = device;
        if (XrCompositionLayerQuad const* beam = wrist_beam::Layer(beamFrame, *shown.beam)) {
            layersOut[count++] = beam;
        }
    }
    // The screen's shown part (the stage's content) of the panel image, where it stands; the game pointer gets the whole window
    // on its plane (LE1's HalcyonOverlays word for word).
    gWorldQuad = *static_cast<XrCompositionLayerQuad const*>(panelLayer);
    auto const sw = static_cast<float>(hud_panel::Swapchain().width);
    auto const sh = static_cast<float>(hud_panel::Swapchain().height);
    gWorldQuad.subImage.imageRect = {{static_cast<int32_t>(shown.part.u0 * sw), static_cast<int32_t>(shown.part.v0 * sh)},
        {std::max<int32_t>(1, static_cast<int32_t>((shown.part.u1 - shown.part.u0) * sw)),
            std::max<int32_t>(1, static_cast<int32_t>((shown.part.v1 - shown.part.v0) * sh))}};
    gWorldQuad.pose = shown.panel.pose;
    gWorldQuad.size = {shown.panel.widthM, shown.panel.heightM};
    if (shown.puzzle) {
        game_pointer::HoldOffForPuzzle();
        gWorldPointerShown = false;
    } else if (shown.panel.widthM > 0.0f && shown.panel.heightM > 0.0f) {
        float const touchArea[4] = {shown.part.u0, shown.part.v0, shown.part.u1, shown.part.v1};
        game_pointer::NoteGameUiQuad(shown.whole.pose, shown.whole.widthM, shown.whole.heightM, game_pointer::PanelKind::GameMenu,
            touchArea, shown.onWrist ? 0 : -1);
        gWorldPointerShown = true;
    } else if (gWorldPointerShown) {
        game_pointer::ForgetGameUiQuad();
        gWorldPointerShown = false;
    }
    world_ui::RedirectReady(hud_panel::DrawsLastFrame() > 0 && hud_panel::Armed());
    if (!gWorldShownLogged) {
        gWorldShownLogged = true;
        log::Info("menus le2: the game's screens stand in the world on ME2: the GUI mode in stereo, their UI on the HUD panel's "
                  "image shown at the screen's place (window UV {:.3f}..{:.3f} x {:.3f}..{:.3f} of the panel's {}x{} swapchain)",
            shown.part.u0, shown.part.u1, shown.part.v0, shown.part.v1, hud_panel::Swapchain().width, hud_panel::Swapchain().height);
    }
    if (count < capacity) {
        layersOut[count++] = &gWorldQuad;
    }
#else
    (void)frame, (void)device;
    if (panelLayer != nullptr && count < capacity) {
        layersOut[count++] = panelLayer;
    }
#endif
    return count;
}

unsigned Layers(MevrSeams::OverlayFrame const& frame, ID3D11Device* device, void const** layersOut, unsigned count,
    unsigned capacity) {
#if defined(MEVR_WITH_MAIN_MENU)
    if (layersOut == nullptr || frame.session == nullptr || device == nullptr || count > capacity) {
        return count;
    }
    XrSession const session = static_cast<XrSession>(frame.session);
    XrSpace const appSpace = static_cast<XrSpace>(frame.appSpace);
    // The head in the app space at this frame's time (on the flat panel too, where no eye views are submitted).
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    bool headValid = false;
    if (frame.viewSpace != nullptr && appSpace != XR_NULL_HANDLE && gTime != 0 &&
        XR_SUCCEEDED(xrLocateSpace(static_cast<XrSpace>(frame.viewSpace), appSpace, gTime, &head))) {
        XrSpaceLocationFlags const both = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        headValid = (head.locationFlags & both) == both;
    }
    XrVector3f eyes[2] = {head.pose.position, head.pose.position};
    if (frame.projectionValid) {
        eyes[0] = Vec(frame.projectionPosition[0]);
        eyes[1] = Vec(frame.projectionPosition[1]);
    }
    // ---- The guided calibration (plugin/Calibration): while it runs, its quads are the only ones of ours ----
    {
        calibration::Frame cal;
        cal.session = session;
        cal.appSpace = appSpace;
        cal.context = frame.context;
        cal.device = device;
        cal.worldFrame = frame.worldFrame;
        cal.menuOpen = frame.menuOpen || main_menu::IsOpen();  // our menu hides the first-run prompt as Halcyon's does
        cal.gameMode = frame.gameMode;
        cal.headValid = headValid;
        cal.head = head.pose;
        cal.eyes[0] = eyes[0];
        cal.eyes[1] = eyes[1];
        XrCompositionLayerQuad const* layers[6] = {};
        bool const running = calibration::Running();
        unsigned const room = std::min<unsigned>(running ? capacity : capacity - count, 6u);
        unsigned const added = calibration::Layers(cal, layers, room);
        if (calibration::Active()) {
            count = 0;
        }
        for (unsigned i = 0; i < added && count < capacity; ++i) {
            layersOut[count++] = layers[i];
        }
    }
    // ---- The omni-tool's rail (plugin/OmniToolRail, research/57; M4.3): beside the game's screen on the wrist, or alone on
    // the wrist when the player taps it open; then the tap's target. As LE1's HalcyonOverlays, after the calibration's ----
    {
        vr::controllers::State const controllers = vr::controllers::Latest();
        vr::hud_pieces::Pieces const pieces = vr::hud_pieces::Latest();
        bool const piecesValid = pieces.size == sizeof(vr::hud_pieces::Pieces);
        omni_rail::Frame rail;
        rail.session = session;
        rail.appSpace = appSpace;
        rail.device = device;
        rail.context = frame.context;
        rail.headValid = headValid;
        rail.eyes = head.pose.position;
        if (controllers.active && controllers.left.gripValid) {
            rail.wristValid = true;
            rail.wrist = vr::hud_pieces::WristPoint(controllers.left.grip, HudSettings().wristOffset);
        }
        rail.display = vr::world_ui::LatestEmitter(400);  // the mod sends it every tick while the rail stands alone
        if (piecesValid && pieces.bodyValid) {
            // Shepard's drawn arms (the mod's HUD pieces, HostServices v22): the tap's target on his forearm, his fingertip.
            rail.body = vr::omni_rail::Body{pieces.leftWrist, pieces.leftElbow, pieces.rightIndex1, pieces.rightIndex2};
        }
        rail.gunDrawn = piecesValid && pieces.gunVisible;
        rail.gameMode = frame.gameMode;
        // The game's screen this frame (BeforePanel): the rail slides out beside it, as LE1's.
        rail.screen = gWorldShown;
        // A game screen of ME2's on Halcyon's flat panel (not one of ours: [WorldUi] screens, a screen we don't know): the GUI
        // mode (7) shows flat (ME2 also reports 7 in play under some HUD pop-ups, which stay in the world). Close closes it too.
        rail.gameScreenUp = engine::adapter::GameMode(frame.gameMode) == engine::adapter::Mode::Gui && !frame.worldFrame;
        rail.othersOwnInput = frame.menuOpen || main_menu::IsOpen() || calibration::Active();
        XrCompositionLayerQuad const* const railLayer = omni_rail::Layer(rail);
        if (railLayer != nullptr && count < capacity) {
            layersOut[count++] = railLayer;
        }
        if (XrCompositionLayerQuad const* const target = omni_rail::TargetLayer(); target != nullptr && count < capacity) {
            layersOut[count++] = target;  // the wrist tap's target
        }
        // ---- The game pointer (plugin/GamePointer, research/42): a controller's ray or the fingertip on a game screen of ours
        // (noted by PanelLayers) is the game's mouse, as LE1's overlays: stepped every frame (it lets go of what it holds when
        // the screen goes), its dot over the screen ----
        game_pointer::Frame pointer;
        pointer.appSpace = appSpace;
        pointer.halcyonMenuOpen = frame.menuOpen;
        pointer.railOwnsInput = railLayer != nullptr && omni_rail::OwnsTriggers();
        if (piecesValid && pieces.bodyValid) {
            pointer.body = vr::omni_rail::Body{pieces.leftWrist, pieces.leftElbow, pieces.rightIndex1, pieces.rightIndex2};
        }
        pointer.headValid = headValid;
        pointer.head = head.pose.position;
        game_pointer::Dot dot;
        if (game_pointer::Step(pointer, dot) && count < capacity) {
            if (XrCompositionLayerQuad const* const dotLayer = game_pointer::DotLayer(session, appSpace, frame.context, device, dot)) {
                layersOut[count++] = dotLayer;
            }
        }
    }
    // ---- Our main menu (plugin/MainMenu): while it's open, its quad is the only one of ours. Closed, it returns at once ----
    {
        main_menu::Frame menu;
        menu.session = session;
        menu.appSpace = appSpace;
        menu.context = frame.context;
        menu.device = device;
        menu.worldFrame = frame.worldFrame;
        menu.gameMode = frame.gameMode;  // the first run waits for on foot
        menu.headValid = headValid;
        menu.head = head.pose;
        menu.projectionValid = frame.projectionValid;
        menu.projectionSwapchain = static_cast<XrSwapchain>(frame.projectionSwapchain[0]);
        menu.projectionRect = {{frame.projectionRect[0][0], frame.projectionRect[0][1]},
            {frame.projectionRect[0][2], frame.projectionRect[0][3]}};
        menu.projectionPose.orientation = Quat(frame.projectionOrientation[0]);
        menu.projectionPose.position = Vec(frame.projectionPosition[0]);
        menu.projectionFov = {frame.projectionFov[0][0], frame.projectionFov[0][1], frame.projectionFov[0][2],
            frame.projectionFov[0][3]};
        if (XrCompositionLayerQuad const* layer = main_menu::Layer(menu); layer != nullptr && capacity > 0) {
            if (!main_menu::LiveAdjusting()) {
                count = 0;
            } else {
                calibration::Frame markers;
                markers.session = session;
                markers.appSpace = appSpace;
                markers.context = frame.context;
                markers.device = device;
                markers.worldFrame = frame.worldFrame;
                markers.menuOpen = frame.menuOpen;
                markers.headValid = headValid;
                markers.head = head.pose;
                markers.eyes[0] = eyes[0];
                markers.eyes[1] = eyes[1];
                XrCompositionLayerQuad const* refs[2]{};
                unsigned const added =
                    calibration::ManualMarkerLayers(markers, main_menu::LiveSelectedHand(), refs, capacity - 1);
                main_menu::NoteLiveMarkers(added == 2);
                count = std::min(count, capacity - added - 1);
                for (unsigned i = 0; i < added; ++i) {
                    layersOut[count++] = refs[i];
                }
            }
            if (count >= capacity) {
                count = capacity - 1;  // the menu always keeps its slot
            }
            layersOut[count++] = layer;
        }
    }
    return count;
#else
    (void)frame, (void)device, (void)layersOut, (void)capacity;
    return count;
#endif
}

void OnSessionStopped() {
#if defined(MEVR_WITH_MAIN_MENU)
    main_menu::SetOpen(false, "VR session stopped");
    calibration::Cancel("VR session stopped");
    calibration::NoteFocused(false);
    main_menu::NoteControllers({});
    calibration::NoteControllers({});
    game_pointer::ForgetGameUiQuad();
    game_pointer::Dot released;
    game_pointer::Step({}, released); // release a native mouse-down even if no more XR frames arrive
    omni_rail::HideForSessionStop();
    world_ui::Forget();
    gWorldShown.reset();
    gWorldUiOwns = gWorldPointerShown = gWorldShownLogged = false;
    gLastGameMode = -1;
    gTime = 0;
    // STOPPING may resume the same session without OnSessionCreated; retain its identity.
    gChord = {};
    std::lock_guard lock(gPadMutex);
    gLatch = {};
    gLatchKept = false;
#endif
}

void OnRecentered() {
#if defined(MEVR_WITH_MAIN_MENU)
    main_menu::Recentered();
    calibration::Recentered();
#endif
}

void OnPresent() {
#if defined(MEVR_WITH_MAIN_MENU)
    main_menu::OnPresent();
#endif
}

bool PadWanted() {
#if defined(MEVR_WITH_MAIN_MENU)
    if (main_menu::IsOpen() || calibration::Active()) {
        return true;
    }
    std::lock_guard lock(gPadMutex);
    return gLatch.buttons != 0 || gLatch.leftTrigger || gLatch.rightTrigger;
#else
    return false;
#endif
}

void FilterPad(XINPUT_GAMEPAD const* real, XINPUT_GAMEPAD& pad) {
#if defined(MEVR_WITH_MAIN_MENU)
    bool const menu = main_menu::IsOpen();
    if (menu && real != nullptr) {
        main_menu::NotePad(*real);  // a real pad navigates our menu
    }
    if (omni_rail::OwnsTriggers() || game_pointer::OwnsTriggers()) {
        pad.bLeftTrigger = 0;  // the omni-tool's rail or a game screen is pointed at: the trigger is its click (LE1's pad merge)
        pad.bRightTrigger = 0;
    }
    if (game_pointer::OwnsWheel()) {
        pad.sThumbRY = 0;  // Scaleform receives wheel events for this axis and stays in mouse mode.
    }
    std::lock_guard lock(gPadMutex);
    if (menu || calibration::Active()) {
        game::gamepad::HoldCloseLatch(gLatch);  // what's down when it closes stays ours until let go
        pad = XINPUT_GAMEPAD{};
        return;
    }
    if (gLatch.buttons == 0 && !gLatch.leftTrigger && !gLatch.rightTrigger) {
        return;
    }
    bool const kept = game::gamepad::ApplyCloseLatch(gLatch, pad);
    if (kept != gLatchKept) {
        gLatchKept = kept;
        log::Info("menus le2: {}", kept ? "buttons held as our menu or the calibration let go of the pad: kept from the game "
                                           "until released"
                                         : "what was held at the close is let go: the pad is the game's again");
    }
#else
    (void)real, (void)pad;
#endif
}

std::string RuntimeName() {
    std::lock_guard lock(gNamesMutex);
    return gRuntime;
}

std::string SystemName() {
    std::lock_guard lock(gNamesMutex);
    return gSystem;
}

} // namespace mevr::host::menus_le2

// ---- ME2's seams (src/halcyon/le2/mevr_bridge.h) ----------------------------------------------------------------------------
namespace ME2VR::MevrBridge {

bool WorldUiOwnsGui(int mode) noexcept {
#if defined(MEVR_WITH_MAIN_MENU)
    // LE1's xr_session.cpp decision: active (bit 0) or an eligible reservation in the GUI mode (bit 1) keeps it live.
    return (mevr::host::world_ui::Ownership(mode) & 3u) != 0;
#else
    (void)mode;
    return false;
#endif
}

} // namespace ME2VR::MevrBridge
