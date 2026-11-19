// SPDX-License-Identifier: MIT
// Our layers over Halcyon's projection on ME2 and ME3: MevrSeams::OverlayLayers, one composition for both sequels, in the
// order of LE1's (plugin/HalcyonOverlays.cpp's OverlayLayers): the comfort vignette, the body-locked HUD panel with the
// interaction label and the HUD's pieces, the sights and the hand reticle, the traversal cue, the power wheel, the squad's
// layers, the spoken-reply panel, the game's own part, our menus. Every layer is the module LE1's host composes (no
// copies); what a renderer cannot do is a capability (plugin/Renderer.hpp), what only a game knows is its adapter's
// (plugin/HalcyonSequelBridge.hpp's `game`: Facts and the Overlay* functions).
//
// LE1 is not on this function yet: its composition reads Halcyon LE1's own state at a dozen places (the wheel crop, the
// world UI, the galaxy window, the sights' release tap, its HudPanel* seams). research/trilogy-seams.md "One overlay
// composition" lists what would have to move first.

#include "plugin/HalcyonSequelBridge.hpp"

#include "core/vr/Colour.hpp"
#include "core/vr/HudPieces.hpp"
#include "core/vr/PowerWheel.hpp"
#include "core/vr/SquadMenu.hpp"
#include "engine/GameAdapter.hpp"
#include "plugin/Colour.hpp"
#include "plugin/GalaxyHologram.hpp"
#include "plugin/GamePointer.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/HudPieceLayers.hpp"
#include "plugin/MenusLe2.hpp"
#include "plugin/PowerWheel.hpp"
#include "plugin/RecenterTilt.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/SequelSquadLayers.hpp"
#include "plugin/SequelWeaponLayers.hpp"
#include "plugin/TraversalCue.hpp"
#include "plugin/Vignette.hpp"
#include "plugin/WheelPanel.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/Thespian.hpp"
#endif

#include <openxr/openxr.h>

#include <d3d11.h>

#include <algorithm>
#include <cstring>

namespace MevrSeams {

namespace sb = mevr::host::sequel_bridge;

unsigned OverlayLayers(const OverlayFrame& frame, const void** layersOut, unsigned capacity) noexcept {
    bool const usable = layersOut != nullptr && capacity != 0 && frame.session != nullptr && frame.context != nullptr;
    sb::game::OverlayBegin(frame, usable);
    if (!usable) {
        sb::Screen() = {};
        return 0;
    }
    mevr::host::recenter_tilt::NoteHead(frame.headOrientation, frame.headValid);  // the tilt-correcting recenter's watch
    mevr::host::renderer::Caps const& caps = mevr::host::renderer::Capabilities();
    sb::game::Facts const& facts = sb::game::Get();
    unsigned count = 0;
    ID3D11Device* device = nullptr;
    frame.context->GetDevice(&device);
    static int lastGameMode = -1;  // Present thread: the last readable game mode
    if (frame.gameMode >= 0) {
        lastGameMode = frame.gameMode;
    }
    auto const session = static_cast<XrSession>(frame.session);
    auto const appSpace = static_cast<XrSpace>(frame.appSpace);
    XrVector3f const headPosition{frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]};
    XrQuaternionf const headOrientation{frame.headOrientation[0], frame.headOrientation[1], frame.headOrientation[2],
        frame.headOrientation[3]};
    bool const ownMenus = mevr::host::renderer::OwnMenuOpen() || mevr::host::menus_le2::PadWanted();

    sb::OverlayContext ctx;
    ctx.frame = &frame;
    ctx.device = device;
    ctx.onFoot = mevr::engine::adapter::GameMode(frame.gameMode) == mevr::engine::adapter::Mode::OnFoot;
    ctx.lastGameMode = lastGameMode;
    // The layers stand down for the renderer's own menu only, as LE1's: under ours or the calibration they're composed as
    // always, and plugin/MenusLe2 submits the menu (or the calibration) alone while it's drawn (LE1's HalcyonOverlays the
    // same). ME3 hid them too until 2026-11-16 (the owner: everything consistent; research/trilogy-seams.md).
    ctx.menuOpen = frame.menuOpen;
    bool const anyMenu = ctx.menuOpen || ownMenus;  // a menu of the renderer's or ours, or the calibration

    // The comfort vignette (research/31): head-locked, under all of ours.
    {
        mevr::host::vignette::Frame vignette;
        vignette.session = session;
        vignette.context = frame.context;
        vignette.device = device;
        vignette.worldFrame = frame.worldFrame;
        vignette.menuOpen = ctx.menuOpen;
        vignette.fovValid = frame.projectionValid;
        std::memcpy(vignette.fov, frame.projectionFov, sizeof vignette.fov);
        if (XrCompositionLayerQuad const* layer = mevr::host::vignette::Layer(vignette); layer != nullptr && count < capacity) {
            layersOut[count++] = layer;
        }
    }

    // The game's own galaxy map as a stereo window on the CIC's projector (plugin/GalaxyHologram, the mod's captures: ME1's
    // research/51 section 8.1), under the map's UI on the panel below (LE1's HalcyonOverlays order).
    if (capacity - count >= 2) {
        mevr::host::galaxy_hologram::Frame galaxy;
        galaxy.session = session;
        galaxy.appSpace = appSpace;
        galaxy.context = frame.context;
        galaxy.device = device;
        galaxy.gameMode = frame.gameMode;
        // Only over the world in the eyes: ME3 plays the relay's travel movie inside the map's mode (11), shown flat by the
        // renderer (CAPTURE_STALL_MONO), and the window stood beside it with stale captures (run gx3a).
        galaxy.menuOpen = ctx.menuOpen || !frame.worldFrame;
        galaxy.headValid = frame.headValid;
        galaxy.head = {headOrientation, headPosition};
        XrCompositionLayerBaseHeader const* window[2]{};
        int const n = mevr::host::galaxy_hologram::WindowLayers(galaxy, window);
        for (int i = 0; i < n; ++i) {
            layersOut[count++] = window[i];
        }
    }

    // The body-locked HUD panel (plugin/HudPanel, LE1's way): the game's UI, which the renderer's draw hooks sent there
    // instead of the eyes (MevrSeams::HudPanelDraw), on a quad in front of the body. Also clears the panel and arms the
    // next frame's redirect. Capability hudPanelGameplay: the whole gameplay HUD; without it only the interaction label's
    // measured crop is captured and the native gameplay HUD stays in the eye images.
    mevr::host::hud_panel::Frame panel;
    XrCompositionLayerQuad const* gameWheelLayer = nullptr;  // the game's wheel at the hand (below)
    {
        mevr::host::renderer::HudSettings const cfg = mevr::host::renderer::Hud();
        if (facts.panelColour) {
            // The game's UI gets the eyes' colour correction (plugin/Colour, research/27), as LE1's HalcyonOverlays hands it
            // to the panel's blit: set before the panel's first blit.
            static bool const colourHooked = [] {
                mevr::host::hud_panel::SetColour({mevr::vr::colour::ShaderPrelude(), &mevr::host::colour::BeginPanel,
                    &mevr::host::colour::EndPanel});
                return true;
            }();
            (void)colourHooked;
        }
        panel.session = session;
        panel.appSpace = appSpace;
        panel.context = frame.context;
        panel.device = device;
        panel.worldFrame = frame.worldFrame && !ctx.menuOpen;
        panel.stereo = frame.stereo;
        panel.headValid = frame.headValid;
        panel.head.orientation = headOrientation;
        panel.head.position = headPosition;
        panel.enabled = caps.hudPanelGameplay ? cfg.hudPanel : !ctx.menuOpen;
        panel.distanceM = cfg.hudPanelDistanceM;
        panel.dropM = cfg.hudPanelDropM;
        panel.followDeg = cfg.hudPanelFollowDeg;
        panel.widthM = cfg.hudPanelWidthM;
        // The game's screens of ours (plugin/MenusLe2, M4.3): their UI on the panel, shown at the screen's place.
        if (caps.worldUi) {
            mevr::host::menus_le2::WorldUiPanel const worldUi = mevr::host::menus_le2::BeforePanel(frame,
                mevr::host::hud_panel::Width(), mevr::host::hud_panel::Height());
            ctx.worldUiOwns = worldUi.owns;
            panel.backdrop = worldUi.backdrop;
        }
        // The interaction label (the native prompt's band at the object): room is reserved for the panel, the label and the
        // pieces before the band is taken out of the panel. Never with a wheel's captured HUD, a screen or a menu.
        namespace hpl = mevr::host::hud_piece_layers;
        bool const room = capacity - count >= (caps.hudPieces ? 4u : 2u);
        bool const labelBase = ctx.onFoot && frame.worldFrame && frame.headValid && !anyMenu && room &&
            !mevr::vr::power_wheel::Latest().open && sb::game::OverlayLabel(ctx);
        // Gameplay as LE1's: not conversations, cutscenes or menus (on foot, the wheels, the squad command, the vehicle; ME3's
        // Atlas and injured Shepard: engine::adapter's IsHudGameplayMode), and a screen of ours.
        panel.gameplay = caps.hudPanelGameplay
            ? (mevr::engine::adapter::IsHudGameplayMode(frame.gameMode) || ctx.worldUiOwns) : labelBase;
        // ME1's 3D cinema screen is up (CineScreenLayers, this frame): the game's UI (the dialogue wheel, subtitles, the
        // interrupt prompt) on a layer of its own just in front of it ([Cinematics] uiLayer, uiForwardM; research/52
        // "Cross-eyed"), the part of the window the screen shows, as large to the eye as on the screen.
        mevr::host::sequel_cinema::FillUi(sb::Screen(), frame.headPosition, panel);
        // The galaxy map open over the CIC's projector (ME1's research/51 section 8.1; LE1's HalcyonOverlays galaxyUi): the map's
        // UI (its labels, title, buttons, the travel question) goes to the panel, which lies on the projector's disc over the
        // map's window, the window's whole frame wide, and is the game pointer's there.
        bool const galaxyUi = mevr::engine::adapter::IsMapMode(frame.gameMode) && mevr::host::galaxy_hologram::Holds(frame.gameMode);
        if (galaxyUi) {
            float const uiWidth = mevr::host::galaxy_hologram::UiPanelWidthM(1.50f);  // LE1's kGalaxyUiWidthM before the window
            panel.gameplay = true;
            panel.screenUi = true;
            panel.screenSpace = appSpace;
            panel.screenPose = mevr::host::galaxy_hologram::UiPanelPose(headPosition);
            panel.screenSize = {uiWidth, uiWidth * static_cast<float>(mevr::host::hud_panel::Height()) /
                static_cast<float>(std::max(1u, mevr::host::hud_panel::Width()))};
            panel.screenPart[0] = panel.screenPart[1] = 0.0f;
            panel.screenPart[2] = panel.screenPart[3] = 1.0f;
            panel.screenForwardM = 0.0f;
        }
        // The HUD keeps the movie's own layout on the panel (Halcyon's per-element HUD moves stand down), as LE1's.
        mevr::host::renderer::SetAuthoredHudLayout(panel.enabled && panel.stereo && mevr::host::hud_panel::Usable());
        bool const panelReady = panel.enabled && panel.stereo && mevr::host::hud_panel::Ready() && device != nullptr;
        // The HUD pieces on the body (plugin/HudPieceLayers, LE1's, research/41; capability hudPieces): on foot, the squad's
        // block on the left wrist and the weapon panel on the gun where the mod says, cut out of the panel meanwhile. They
        // are blitted from this frame's UI after the panel is drawn and before it's cleared (hud_panel's beforeClear), and
        // go on top of the panel's quad.
        struct PiecesPass {
            hpl::Common common;
            hpl::Plan plan;
            void const* layers[2] = {};
            unsigned count = 0;
            mevr::host::wheel_panel::Common wheelCommon;
            mevr::host::wheel_panel::Plan wheelPlan;
            bool onFoot = false;
            XrCompositionLayerQuad const* wheelLayer = nullptr;
        } pass;
        pass.common.session = session;
        pass.common.appSpace = appSpace;
        pass.common.context = frame.context;
        pass.common.device = device;
        pass.common.head = headPosition;
        pass.common.nowMs = GetTickCount64();
        pass.common.options.colourSeed = 3;  // a quad of ours, not the panel (LE1's overlays' options for a piece)
        // The game's weapon and power wheels on a panel of their own at the hand (plugin/WheelPanel, LE1's; HostServices
        // setWheelPanel): their part of the UI cut out of the panel while one is up, blitted before the panel is cleared.
        namespace wp = mevr::host::wheel_panel;
        pass.wheelPlan = wp::Decide(frame.context, frame.gameMode, caps.hudPanelGameplay && panelReady && panel.gameplay &&
            !panel.screenUi && !ctx.worldUiOwns && frame.worldFrame && !anyMenu);
        if (pass.wheelPlan.wanted) {
            panel.masks.Add(pass.wheelPlan.px.x, pass.wheelPlan.px.y, pass.wheelPlan.px.width, pass.wheelPlan.px.height);
        }
        pass.wheelCommon.session = session;
        pass.wheelCommon.appSpace = appSpace;
        pass.wheelCommon.context = frame.context;
        pass.wheelCommon.device = device;
        pass.wheelCommon.head = {headOrientation, headPosition};
        pass.wheelCommon.whiteScale = panel.whiteScale;
        pass.wheelCommon.nowMs = pass.common.nowMs;
        pass.wheelCommon.options = pass.common.options;
        pass.onFoot = ctx.onFoot;
        panel.user = &pass;
        panel.beforeClear = [](void* user, bool shown) {
            auto& p = *static_cast<PiecesPass*>(user);
            p.wheelLayer = wp::Layer(p.wheelCommon, p.wheelPlan, shown, p.onFoot);
        };
        if (caps.hudPieces) {
            hpl::Frame pieceFrame;
            pieceFrame.gameplayOnFoot = lastGameMode == 0;
            pieceFrame.worldFrame = panel.worldFrame;
            pieceFrame.headValid = frame.headValid;
            pieceFrame.panelReady = panelReady;
            pieceFrame.panelWidth = mevr::host::hud_panel::Width();
            pieceFrame.panelHeight = mevr::host::hud_panel::Height();
            pieceFrame.layout = static_cast<mevr::vr::hud_pieces::Layout>(facts.hudLayout);
            pass.plan = hpl::Decide(pieceFrame, pass.common.nowMs, panel.masks);
            panel.user = &pass;
            panel.beforeClear = [](void* user, bool shown) {
                auto& p = *static_cast<PiecesPass*>(user);
                p.wheelLayer = wp::Layer(p.wheelCommon, p.wheelPlan, shown, p.onFoot);
                p.count = hpl::Layers(p.common, p.plan, shown, p.layers, 0, 2);
            };
        }
        // A failed copy leaves the prompt's band on the original panel. Never relocate dialogue or screen UI.
        XrCompositionLayerQuad const* const interactionLayer =
            hpl::Interaction(pass.common, labelBase && panelReady && !panel.screenUi, panel.masks,
                static_cast<mevr::vr::hud_pieces::Layout>(facts.hudLayout));
        XrCompositionLayerQuad const* const panelLayer = mevr::host::hud_panel::Layer(panel);
        if (caps.worldUi && !galaxyUi) {
            count = mevr::host::menus_le2::PanelLayers(frame, device, panelLayer, layersOut, count, capacity);  // or a screen's place
        } else if (panelLayer != nullptr && count < capacity) {
            layersOut[count++] = panelLayer;
        }
#if defined(MEVR_WITH_MAIN_MENU)
        // The front end on ME1's 3D cinema screen (research/56 "The sequels"): its UI layer is the game pointer's, as LE1's
        // CineScreenPointerPlane: the whole game window on the layer's plane, wider and taller by the part the screen leaves out
        // (centred: the eyes' opposite crops, the band's rows), so a ray maps onto the window's pixel. ME3's flat GUI panel had
        // the pointer before the title moved to the screen. Before the galaxy's: forgetting must not undo its note.
        static bool frontEndPointer = false;  // Present thread
        float const partW = panel.screenPart[2] - panel.screenPart[0];
        float const partH = panel.screenPart[3] - panel.screenPart[1];
        if (!galaxyUi && panel.screenUi && panelLayer != nullptr && !anyMenu && partW > 0.05f && partH > 0.05f &&
            ::MevrSeams::FrontEndOnScreen()) {
            mevr::host::game_pointer::NoteGameUiQuad(panel.screenPose, panel.screenSize.width / partW,
                panel.screenSize.height / partH, mevr::host::game_pointer::PanelKind::GameMenu);
            frontEndPointer = true;
        } else if (frontEndPointer) {
            mevr::host::game_pointer::ForgetGameUiQuad();
            frontEndPointer = false;
        }
        // The map's UI on the projector is the game pointer's (a controller's ray and trigger: the game's cursor; LE1's
        // HalcyonOverlays galaxy panel), in the galaxy's mode only: orbit keeps the scanner's own triggers (ME2's LT and RT).
        static bool galaxyPointer = false;  // Present thread
        int const galaxyKind = mevr::host::game_pointer::GalaxyMapPointer(facts.galaxyUiPointer);
        if (galaxyUi && panelLayer != nullptr && !anyMenu && galaxyKind != 0 && mevr::engine::adapter::IsGalaxyMode(frame.gameMode)) {
            mevr::host::game_pointer::NoteGameUiQuad(panelLayer->pose, panelLayer->size.width, panelLayer->size.height,
                galaxyKind == 2 ? mevr::host::game_pointer::PanelKind::GameMenu : mevr::host::game_pointer::PanelKind::GalaxyMap);
            galaxyPointer = true;
        } else if (galaxyPointer) {
            mevr::host::game_pointer::ForgetGameUiQuad();
            galaxyPointer = false;
        }
#endif
        // A failed panel leaves the original native prompt unclaimed: never a detached label alone.
        if (interactionLayer != nullptr && panelLayer != nullptr && count < capacity) {
            layersOut[count++] = interactionLayer;
        }
        for (unsigned i = 0; i < pass.count && count < capacity; ++i) {
            layersOut[count++] = pass.layers[i];
        }
        gameWheelLayer = pass.wheelLayer;
    }

    // The sights and the hand reticle (plugin/SequelWeaponLayers: LE1's SightLayers and HandReticle).
    OverlayFrame gameplay = frame;
    gameplay.menuOpen = ctx.menuOpen;
    // The ring while the right controller aims weapons, LE1's rule on both sequels (ME3 also wanted a fresh aim ray until
    // 2026-11-16: the mod sends one every tick it reports hand aim, so that only differed with a host and mod that don't match).
    count = mevr::host::sequel_weapon_layers::Layers(gameplay, device, sb::game::OverlayReticleMode(ctx), layersOut, count,
        capacity, sb::ControllerAimActive());
    // The game's own crosshair hidden where the ring replaces it (capability screenReticleHide; a no-op without it). Native
    // hiding stays each renderer's verified adapter, separate from the shared ring's drawing.
    mevr::host::renderer::SetScreenReticleHidden(
        mevr::host::renderer::Hud().handReticle && mevr::engine::adapter::IsHudGameplayMode(lastGameMode));

    // Traversal's cue at the obstacle and its blink (plugin/TraversalCue, research/le2-traversal.md): the blink over the
    // world's layers, under the menus. On any world frame: the mod sends a cue only on foot and a blink only through its own
    // move outside authored scenes (game/Le2Traversal, game/Le3Traversal), on both sequels.
    if (count < capacity) {
        mevr::host::traversal_cue::Frame cue;
        cue.session = session;
        cue.appSpace = appSpace;
        cue.context = frame.context;
        cue.device = device;
        cue.worldFrame = frame.worldFrame;
        cue.menuOpen = ctx.menuOpen;
        cue.headValid = frame.headValid;
        cue.head = headPosition;
        count += mevr::host::traversal_cue::Layers(cue, layersOut + count, capacity - count);
    }

    // The power wheel at the left hand, its hint and lock, and the squad wheel at the right (plugin/PowerWheel, research/55).
    if (count < capacity) {
        mevr::host::power_wheel::Frame wheel;
        wheel.session = session;
        wheel.appSpace = appSpace;
        wheel.context = frame.context;
        wheel.device = device;
        wheel.worldFrame = frame.worldFrame && ctx.onFoot;
        wheel.menuOpen = ctx.menuOpen || mevr::host::renderer::OwnMenuOpen();
        wheel.head = headPosition;
        wheel.headValid = frame.headValid;
        XrCompositionLayerQuad const* quads[3]{};
        int const n = mevr::host::power_wheel::Layers(wheel, quads, static_cast<int>(std::min(3u, capacity - count)));
        for (int i = 0; i < n; ++i) {
            layersOut[count++] = quads[i];
        }
    }

    // The squad's placement arc (plugin/SequelSquadLayers).
    OverlayFrame squadFrame = frame;
    squadFrame.worldFrame = frame.worldFrame && ctx.onFoot;
    squadFrame.menuOpen = anyMenu;
    count = mevr::host::sequel_squad_layers::Place(squadFrame, device, layersOut, count, capacity);

#if defined(MEVR_WITH_MAIN_MENU)
    // The spoken-reply panel (Thespian): a conversation's overlay. A menu, the calibration, missing tracking, another mode
    // or an exhausted budget hides it. The context's eligibility (not Layer()'s success) is told to the game: a turn must be
    // able to start, and any hidden context revokes the game's lease on it.
    {
        mevr::host::thespian::Frame spoken;
        spoken.session = session;
        spoken.appSpace = appSpace;
        spoken.context = frame.context;
        spoken.device = device;
        spoken.head = {headOrientation, headPosition};
        spoken.othersOwnInput = !mevr::engine::adapter::IsConversationMode(frame.gameMode) || !frame.headValid ||
            count >= capacity || anyMenu || sb::game::OverlaySpokenBlocked(ctx);
        sb::game::OverlaySpokenContext(!spoken.othersOwnInput);
        if (auto const* layer = mevr::host::thespian::Layer(spoken)) {
            layersOut[count++] = layer;
        }
    }
#endif

    // The game's wheel at the hand: on top of the gameplay layers (LE1's order), under the game's own part and our menus.
    if (gameWheelLayer != nullptr && count < capacity) {
        layersOut[count++] = gameWheelLayer;
        mevr::host::wheel_panel::Submitted();
    }
    count = sb::game::OverlayGameLayers(ctx, panel, layersOut, count, capacity);
    // Our main menu and the guided calibration (plugin/MenusLe2, M4): over the rest, alone while they're up.
    count = mevr::host::menus_le2::Layers(frame, device, layersOut, count, capacity);
    sb::game::OverlayEnd(ctx);
    if (device != nullptr) {
        device->Release();
    }
    sb::Screen() = {};  // this frame's 3D screen used; the next submit places it again
    return count;
}

} // namespace MevrSeams
