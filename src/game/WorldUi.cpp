// SPDX-License-Identifier: MIT
// The game's full-screen screens in the room: the game's side (game/WorldUi.hpp, research/53).
#include "game/WorldUi.hpp"

#include "core/Log.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "core/vr/HudPieces.hpp"
#include "game/CockpitProbe.hpp"
#include "game/HandCalibration.hpp"
#include "game/OmniTool.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mevr::game::world_ui {

namespace {

namespace wu = vr::world_ui;


// The GUI manager (the player controller's cached one) and its panels: each open screen is a BioSFPanel tagged with its library
// tag (the shop's: nmTag "shop", bFullScreen, m_DefaultHandler its BioSFHandler_Shop; measured 2026-11-04).
ue3::FieldRef gCachedManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gPanels{"Engine.BioSFManager", "m_lstPanels"};
ue3::FieldRef gFocusPanel{"Engine.BioSFManager", "m_FocusPanel"};
ue3::FieldRef gPanelTag{"Engine.BioSFPanel", "nmTag"};
ue3::FieldRef gPanelFullScreen{"Engine.BioSFPanel", "bFullScreen"};
ue3::FieldRef gPanelHandler{"Engine.BioSFPanel", "m_DefaultHandler"};
ue3::FieldRef gPanelVisible{"Engine.BioSFPanel", "IsVisible"};
// The loot screen's inventory belongs to the container (or the body) it came from: its outer chain reaches that actor.
ue3::ClassRef gLootHandlerClass{"SFXGame.BioSFHandler_Loot"};
ue3::FieldRef gLootInventory{"SFXGame.BioSFHandler_Loot", "m_oLootInventory"};
ue3::ClassRef gActorClass{"Engine.Actor"};
// The bypass minigame's handler names the placeable it's played on.
ue3::ClassRef gSkillGameHandlerClass{"SFXGame.BioSkillGame_Handler"};
ue3::FieldRef gSkillGameDependent{"SFXGame.BioSkillGame_Handler", "m_oDependent"};
// What A would use (research/40's BioPlayerSelection): the thing a screen opened by a use belongs to.
ue3::FieldRef gPlayerSelection{"SFXGame.BioPlayerController", "m_oPlayerSelection"};
ue3::FieldRef gCurrentTarget{"SFXGame.BioPlayerSelection", "m_oCurrentSelectionTarget"};
ue3::ClassRef gShopHandlerClass{"SFXGame.BioSFHandler_Shop"};
ue3::FieldRef gShopActor{"SFXGame.BioSFHandler_Shop", "m_shop"};
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera"};

// Shepard's view on foot (the last camera update in mode 0), and the player camera's POV fields while a screen is up: the host
// puts the former into the latter for the main view's render only (the shop's own camera looks at a backdrop stage elsewhere).
struct Hold {
    bool haveView = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 90.0f;
    std::optional<engine::TrackingFrame> footFrame;  // the tracking frame of that view (the anchor's conversion while held)
    float* povLocation = nullptr;
    ue3::Rotator* povRotation = nullptr;
    float* povFov = nullptr;
    std::uint64_t povMs = 0;
};
Hold gHold;
bool gScreenUp = false;  // a screen of ours was published last tick (game thread)
bool gWristUp = false;   // ... and it's projected from the wrist (arm IK runs, the view anchor held)

// The last thing A would have used on foot, and when: a screen that opens within kUsedWithinMs of it (the minigame at a crate, a
// locker's equipment, the squad selection at the airlock) stands toward it.
constexpr std::uint64_t kUsedWithinMs = 3000;
struct Used {
    ue3::Object* target = nullptr;
    std::uint64_t ms = 0;
};
Used gUsed;
Used gUsedAtOpening;  // frozen when a screen opens

// A full-screen panel (bFullScreen) makes the game render its UI world (GUI_UIWorld: a backdrop stage of blurred machinery
// under a blue gradient, its own camera) instead of the level; with the flag cleared the level renders behind the screen
// (measured 2026-11-04 at the shop, research/53). Cleared on every screen of ours open while ours stands in the room (the map
// opens over the main wheel: both full screen), set again on those still open if ours goes down.
std::vector<ue3::Object*> gClearedPanels;

void RestoreFullScreen() {
    int restored = 0;
    for (ue3::Object* const panel : gClearedPanels) {
        if (panel != nullptr && ue3::runtime::IsAlive(panel)) {
            gPanelFullScreen.WriteBool(panel, true);  // journalled by the handle
            ++restored;
        }
    }
    if (restored > 0) {
        log::Info("world ui: {} of the game's screens full screen again (its UI world renders behind them)", restored);
    }
    gClearedPanels.clear();
}

struct Status {
    bool published = false;
    // Screens opened (a new mode-7 stretch with one of ours); starts from the clock, so a reloaded mod never repeats a number the
    // host has placed.
    std::uint32_t opening = static_cast<std::uint32_t>(GetTickCount64());
    bool open = false;                 // one of ours is up now
    std::string stack;                 // the open screens' tags, as last read
    std::string screen;                // the one shown
    bool anchored = false;
    float anchor[3] = {};
    std::uint64_t ticks = 0;
    bool loggedMissing = false;
};
Status gS;
std::mutex gStatusMutex;
std::string gStatusJson = "{\"open\": false}";

bool Resolved() {
    return gCachedManager.Get() != nullptr && gPanels.Get() != nullptr && gFocusPanel.Get() != nullptr && gPanelTag.Get() != nullptr &&
           gPanelFullScreen.Get() != nullptr && gPanelHandler.Get() != nullptr;
}

struct OpenPanel {
    ue3::Object* panel = nullptr;
    std::string tag;
    bool fullScreen = false;
    bool visible = false;
    bool focus = false;
    ue3::Object* handler = nullptr;
};

// The GUI manager's open panels, in its order (the HUD's, the reticule's... and the screens).
std::vector<OpenPanel> OpenPanels(ue3::Object* controller) {
    std::vector<OpenPanel> open;
    ue3::Object* const manager = gCachedManager.Read<ue3::Object*>(controller).value_or(nullptr);
    auto const* const panels = manager != nullptr ? gPanels.In<ue3::TArray<ue3::Object*>>(manager) : nullptr;
    if (panels == nullptr || panels->data == nullptr || panels->count <= 0 || panels->count > 64) {
        return open;
    }
    ue3::Object* const focus = gFocusPanel.Read<ue3::Object*>(manager).value_or(nullptr);
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0; i < panels->count; ++i) {
        ue3::Object* const panel = panels->data[i];
        if (panel == nullptr || !r.IsAlive(panel)) {
            continue;
        }
        OpenPanel o;
        o.panel = panel;
        if (auto const tag = gPanelTag.Read<ue3::Name>(panel)) {
            o.tag = r.ToString(*tag);
        }
        o.fullScreen = gPanelFullScreen.ReadBool(panel).value_or(false);
        o.visible = gPanelVisible.ReadBool(panel).value_or(true);
        o.focus = panel == focus;
        o.handler = gPanelHandler.Read<ue3::Object*>(panel).value_or(nullptr);
        open.push_back(std::move(o));
    }
    return open;
}

// The vendor: the BioShop actor names no one, but it's placed at its vendor (the Normandy's requisitions officer 68 units from
// his), so the nearest pawn other than the player's within kVendorUnits of it; found once per opening.
constexpr float kVendorUnits = 250.0f;
struct Vendor {
    std::uint32_t opening = 0;
    ue3::Object* pawn = nullptr;
};
Vendor gVendor;
std::uint32_t gAnchorLogged = 0;  // the opening whose anchor was logged (loot, a use)

ue3::Object* NearestPawn(ue3::Vector const& at, ue3::Object* player) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* best = nullptr;
    float bestD = kVendorUnits;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || o == player || !player::IsBioPawn(o) || !r.IsAlive(o) || r.NameText(o).starts_with("Default__")) {
            continue;
        }
        if (auto const p = player::Location(o)) {
            float const d = std::hypot(p->x - at.x, p->y - at.y, p->z - at.z);
            if (d < bestD) {
                bestD = d;
                best = o;
            }
        }
    }
    return best;
}

// The actor an object belongs to: itself, or the first actor up its outer chain (an inventory of a placeable or a pawn).
ue3::Object* OwningActor(ue3::Object* o) {
    ue3::Reflection& r = ue3::runtime::Get();
    for (int depth = 0; o != nullptr && depth < 8; ++depth, o = r.OuterOf(o)) {
        if (ue3::IsA(o, gActorClass) && r.IsAlive(o)) {
            return o;
        }
    }
    return nullptr;
}

// Where what the screen belongs to stands in the world: the shop's vendor (else its BioShop actor); the loot's container or body;
// for a screen a use opens (not from the main wheel), the thing that was used.
std::optional<ue3::Vector> AnchorOf(wu::Screen screen, ue3::Object* handler, ue3::Object* controller, std::uint32_t opening,
    bool fromWheel) {
    if (screen == wu::Screen::Loot && handler != nullptr && ue3::IsA(handler, gLootHandlerClass) && ue3::runtime::IsAlive(handler)) {
        ue3::Object* const inventory = gLootInventory.Read<ue3::Object*>(handler).value_or(nullptr);
        if (ue3::Object* const owner = inventory != nullptr ? OwningActor(inventory) : nullptr) {
            if (gAnchorLogged != opening) {
                gAnchorLogged = opening;
                log::Info("world ui: the loot comes from {}", ue3::runtime::FullName(owner));
            }
            return player::Location(owner);
        }
    }
    if (screen == wu::Screen::SkillGame && handler != nullptr && ue3::IsA(handler, gSkillGameHandlerClass) && ue3::runtime::IsAlive(handler)) {
        ue3::Object* const dependent = gSkillGameDependent.Read<ue3::Object*>(handler).value_or(nullptr);
        if (ue3::Object* const owner = dependent != nullptr ? OwningActor(dependent) : nullptr) {
            if (gAnchorLogged != opening) {
                gAnchorLogged = opening;
                log::Info("world ui: the minigame is played on {}", ue3::runtime::FullName(owner));
            }
            return player::Location(owner);
        }
    }
    bool const usedScreen = screen == wu::Screen::SkillGame || screen == wu::Screen::DesignerUI || screen == wu::Screen::PartySelect ||
        screen == wu::Screen::Specialization || screen == wu::Screen::Loot ||
        ((screen == wu::Screen::Inventory || screen == wu::Screen::XMods) && !fromWheel);
    if (usedScreen && gUsedAtOpening.target != nullptr && ue3::runtime::IsAlive(gUsedAtOpening.target)) {
        if (gAnchorLogged != opening) {
            gAnchorLogged = opening;
            log::Info("world ui: the {} screen opened by using {}", wu::Name(screen), ue3::runtime::FullName(gUsedAtOpening.target));
        }
        return player::Location(gUsedAtOpening.target);
    }
    if (screen == wu::Screen::Shop && handler != nullptr && ue3::IsA(handler, gShopHandlerClass) && ue3::runtime::IsAlive(handler)) {
        ue3::Object* const shop = gShopActor.Read<ue3::Object*>(handler).value_or(nullptr);
        auto const at = shop != nullptr && ue3::runtime::IsAlive(shop) ? player::Location(shop) : std::nullopt;
        if (!at) {
            return std::nullopt;
        }
        if (gVendor.opening != opening) {
            gVendor = {opening, NearestPawn(*at, player::Pawn(controller))};
            ue3::Reflection& r = ue3::runtime::Get();
            log::Info("world ui: the shop's vendor: {}", gVendor.pawn != nullptr ? r.FullName(gVendor.pawn)
                                                                                : std::string("none near its shop actor (the screen faces the shop actor)"));
        }
        if (gVendor.pawn != nullptr && ue3::runtime::IsAlive(gVendor.pawn)) {
            if (auto const v = player::Location(gVendor.pawn)) {
                return v;
            }
        }
        return at;
    }
    return std::nullopt;
}

// Engine.Camera.UpdateCamera, after the game's: on foot, Shepard's view is recorded; while a screen of ours is up, where the POV
// lives (the host swaps the recorded view in at render time).
void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) {
        return;
    }
    auto const pov = player::CameraPov(camera);
    if (!pov || pov->location == nullptr || pov->rotation == nullptr || pov->fov == nullptr) {
        return;
    }
    auto const mode = player::GameMode(controller);
    if (mode && *mode == 0) {
        gHold.location = *pov->location;
        gHold.rotation = *pov->rotation;
        gHold.fov = *pov->fov;
        gHold.haveView = true;
        return;
    }
    if (gScreenUp) {
        gHold.povLocation = reinterpret_cast<float*>(pov->location);
        gHold.povRotation = pov->rotation;
        gHold.povFov = pov->fov;
        gHold.povMs = GetTickCount64();
    }
}

void SetStatus(std::string json) {
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    gStatusJson = std::move(json);
}

void Down(Link const& link, char const* why) {
    if (gS.published && link.setState != nullptr) {
        link.setState(nullptr);
        log::Info("world ui: {} down ({})", gS.screen.empty() ? "the screen" : gS.screen, why);
    }
    RestoreFullScreen();
    omni_tool::Tick(false);
    if (gWristUp) {
        gWristUp = false;
        view_anchor::Hold(std::nullopt);
    }
    gS.published = false;
    gScreenUp = false;
    gS.open = false;
    gS.anchored = false;
    gS.screen.clear();
    SetStatus(std::format("{{\"open\": false, \"openings\": {}}}", gS.opening));
}

} // namespace

void TryStartup() {
    ue3::Function* const update = ue3::runtime::Ready() ? gUpdateCamera.Get() : nullptr;
    if (update == nullptr) {
        log::Warn("world ui: Engine.Camera.UpdateCamera not found: the game's screens can't hold Shepard's view (they stay on Halcyon's panel)");
        return;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
}

void Tick(Link const& link) {
    wu::Settings const settings = hand_calibration::GetWorldUi();
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    if (mode && *mode == 0) {
        if (auto const frame = view_anchor::Current()) {
            gHold.footFrame = frame;  // the room as Shepard's view maps it (the held view's)
        }
        ue3::Object* const selection = controller != nullptr ? gPlayerSelection.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
        ue3::Object* const target = selection != nullptr ? gCurrentTarget.Read<ue3::Object*>(selection).value_or(nullptr) : nullptr;
        if (target != nullptr && ue3::runtime::IsAlive(target)) {
            gUsed = {target, GetTickCount64()};
        }
    }
    if (link.setState == nullptr || !settings.on || !mode || !wu::ScreenMode(*mode)) {
        if (!gS.stack.empty()) {
            gS.stack.clear();
        }
        Down(link, link.setState == nullptr ? "an older host" : !settings.on ? "[WorldUi] on = 0" : "the game left its GUI and minigame modes");
        return;
    }
    if (!Resolved()) {
        if (!gS.loggedMissing) {
            gS.loggedMissing = true;
            log::Warn("world ui: the GUI manager's panels (BioSFManager.m_lstPanels, BioSFPanel.nmTag...) weren't found through our "
                      "reflection layer: the game's screens stay on Halcyon's panel");
        }
        return;
    }
    std::vector<OpenPanel> const panels = OpenPanels(controller);
    std::string stack;
    OpenPanel const* base = nullptr;  // the first of ours opened decides (a message box over the shop stays on the shop's place)
    for (OpenPanel const& o : panels) {
        stack += std::format("{}{}{}{}{}", stack.empty() ? "" : ", ", o.tag, o.fullScreen ? " (full screen)" : "", o.focus ? " (focus)" : "",
                             o.visible ? "" : " (hidden)");
        if (base == nullptr && o.visible && wu::FromTag(o.tag) != wu::Screen::None) {
            base = &o;
        }
    }
    if (stack != gS.stack) {
        log::Info("world ui: the game's open panels: {}", stack.empty() ? "none" : stack);
        gS.stack = stack;
    }
    wu::Screen const screen = base != nullptr ? wu::FromTag(base->tag) : wu::Screen::None;
    if (screen == wu::Screen::None) {
        Down(link, "no screen of ours open");
        return;
    }
    // Every one of ours open must be listed: a screen that shows a 3D model of its own in the game's UI world (the equipment's
    // squadmate, the character record's Shepard) keeps Halcyon's panel, and the main wheel it opened from with it.
    for (OpenPanel const& o : panels) {
        if (wu::Screen const s = wu::FromTag(o.tag); o.visible && s != wu::Screen::None && (settings.screens & wu::Bit(s)) == 0) {
            Down(link, "not one of [WorldUi] screens");
            return;
        }
    }
    // In the Mako's cockpit (the extraction's "Return to Normandy?", the pause menu): the cockpit stays around the eyes through the
    // GUI mode (game/CockpitProbe), so the screen goes to its windshield, where the HUD panel stands while driving, and Shepard's
    // view on foot (from wherever Shepard last stood, long before the Mako) is never held: that showed bare terrain under the
    // question with no cockpit (2026-11-05, research/48 section 27).
    std::optional<XrVector3f> cockpitAnchor;
    XrVector3f eyesNow{};
    if (cockpit_probe::Riding()) {
        vr::hud_pieces::Pieces pieces;
        cockpit_probe::FillHud(pieces);
        if (auto const frame = view_anchor::Current(); pieces.cockpit && frame) {
            cockpitAnchor = pieces.panelAnchor;
            eyesNow = frame->head.position;
        }
    }
    bool const inCockpit = cockpitAnchor.has_value();
    if (!inCockpit && (!gHold.haveView || !gHold.footFrame)) {
        Down(link, "Shepard's view on foot isn't known (opened before any camera update on foot)");
        return;
    }
    std::erase_if(gClearedPanels, [&](ue3::Object* p) {
        return std::none_of(panels.begin(), panels.end(), [&](OpenPanel const& o) { return o.panel == p; });  // closed since
    });
    for (OpenPanel const& o : panels) {
        if (o.fullScreen && wu::FromTag(o.tag) != wu::Screen::None && gPanelFullScreen.WriteBool(o.panel, false)) {
            gClearedPanels.push_back(o.panel);
            log::Info("world ui: the {} screen isn't full screen any more: the level renders behind it, not the game's UI world", o.tag);
        }
    }
    if (!gS.open) {
        gS.open = true;
        ++gS.opening;
        gS.screen = wu::Name(screen);
        std::uint64_t const now = GetTickCount64();
        gUsedAtOpening = gUsed.target != nullptr && now - gUsed.ms < kUsedWithinMs ? gUsed : Used{};
    }
    wu::State state;
    state.active = 1;
    state.screen = static_cast<std::uint8_t>(screen);
    state.opening = gS.opening;
    state.widthM = wu::RoomWidth(screen, settings.widthM);  // the loot keeps the stage's scale, its empty sides cut
    state.distanceM = settings.distanceM;
    state.belowM = settings.belowM;
    state.backdrop = settings.backdrop;
    bool const onWrist = !inCockpit && (settings.wristScreens & wu::Bit(screen)) != 0;
    wu::Rect const crop = wu::DefaultCrop(screen);  // the room's; on the wrist the host shows WristCrop
    state.stageCrop[0] = crop.u0;
    state.stageCrop[1] = crop.v0;
    state.stageCrop[2] = crop.u1;
    state.stageCrop[3] = crop.v1;
    std::strncpy(state.tag, base->tag.c_str(), sizeof(state.tag) - 1);
    state.placement = static_cast<std::uint8_t>(!onWrist ? wu::Placement::Room : settings.wristFollow ? wu::Placement::Wrist : wu::Placement::WristOnce);
    state.beam = settings.wristBeam ? 1 : 0;
    state.wristWidthM = settings.wristWidthM;
    state.wristAboveM = settings.wristAboveM;
    state.wristTowardM = settings.wristTowardM;
    // The projector: the omni-tool's display as Shepard's arm shows it (last tick's pose), in the room's frame on foot.
    if (auto const display = onWrist ? omni_tool::DisplayWorld() : std::nullopt; display && gHold.footFrame) {
        XrVector3f const p = engine::ToTracking(*gHold.footFrame, engine::UnrealVector{display->x, display->y, display->z});
        state.emitter[0] = p.x;
        state.emitter[1] = p.y;
        state.emitter[2] = p.z;
        state.emitterValid = 1;
    }
    bool const fromWheel = std::any_of(panels.begin(), panels.end(), [](OpenPanel const& o) { return o.visible && wu::FromTag(o.tag) == wu::Screen::MenuBrowser; });
    if (inCockpit) {
        wu::CockpitPlacement const place = wu::InCockpit(eyesNow, *cockpitAnchor, state.widthM, settings.distanceM);
        state.anchored = 1;
        state.anchor[0] = cockpitAnchor->x;
        state.anchor[1] = cockpitAnchor->y;
        state.anchor[2] = cockpitAnchor->z;
        state.belowM = place.belowM;
        state.widthM = place.widthM;
    } else if (auto const at = AnchorOf(screen, base->handler, controller, gS.opening, fromWheel)) {
        {
            XrVector3f const p = engine::ToTracking(*gHold.footFrame, engine::UnrealVector{at->x, at->y, at->z});
            state.anchored = 1;
            state.anchor[0] = p.x;
            state.anchor[1] = p.y;
            state.anchor[2] = p.z;
        }
    }
    bool const wasAnchored = gS.anchored;
    gS.anchored = state.anchored != 0;
    std::copy(std::begin(state.anchor), std::end(state.anchor), std::begin(gS.anchor));
    if (!gS.published || wasAnchored != gS.anchored) {
        log::Info("world ui: {} up (opening {}){}{}", gS.screen, gS.opening,
            inCockpit ? std::format(": in the Mako's cockpit, at its windshield toward ({:.2f}, {:.2f}, {:.2f}) m, {:.2f} m wide, {:.2f} m below "
                                    "the eyes (no view held)",
                            gS.anchor[0], gS.anchor[1], gS.anchor[2], state.widthM, state.belowM)
            : gS.anchored ? std::format(": it belongs to what stands at ({:.2f}, {:.2f}, {:.2f}) m in the room", gS.anchor[0], gS.anchor[1], gS.anchor[2])
                        : std::string(": nothing in the world to stand it by; straight ahead"),
            onWrist ? "; projected from the left wrist (in the room if the left controller isn't tracked)" : "");
    }
    std::uint64_t const now = GetTickCount64();
    if (!inCockpit && gHold.povLocation != nullptr && now - gHold.povMs < 500) {
        state.holdRender = 1;
        state.holdLocation[0] = gHold.location.x;
        state.holdLocation[1] = gHold.location.y;
        state.holdLocation[2] = gHold.location.z;
        state.holdRotation[0] = gHold.rotation.pitch;
        state.holdRotation[1] = gHold.rotation.yaw;
        state.holdRotation[2] = gHold.rotation.roll;
        state.holdFov = gHold.fov;
        state.povLocationAddress = reinterpret_cast<std::uint64_t>(gHold.povLocation);
        state.povRotationAddress = reinterpret_cast<std::uint64_t>(gHold.povRotation);
        state.povFovAddress = reinterpret_cast<std::uint64_t>(gHold.povFov);
    }
    link.setState(&state);
    gS.published = true;
    gScreenUp = true;
    ++gS.ticks;
    omni_tool::Tick(onWrist && settings.omniTool);  // Shepard's forearm wears the game's omni-tool while it projects the screen
    if (onWrist != gWristUp) {
        log::Info("world ui: {}", onWrist ? "the arms follow the controllers while the screen is on the wrist (Shepard's view on foot held)"
                                          : "the arms go back to the game");
    }
    gWristUp = onWrist;
    view_anchor::Hold(onWrist ? gHold.footFrame : std::nullopt);
    SetStatus(std::format("{{\"open\": true, \"screen\": \"{}\", \"stack\": \"{}\", \"opening\": {}, \"anchored\": {}, \"anchor\": [{:.3f}, {:.3f}, {:.3f}], "
                          "\"placement\": {}, \"inCockpit\": {}, \"heldView\": {}, \"omniTool\": {}}}",
        gS.screen, stack, gS.opening, gS.anchored ? "true" : "false", gS.anchor[0], gS.anchor[1], gS.anchor[2], state.placement,
        inCockpit ? "true" : "false", state.holdRender != 0 ? "true" : "false", omni_tool::StatusJson()));
}

bool WristScreenUp() {
    return gWristUp;
}

std::string StatusJson() {
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    return gStatusJson;
}

void Shutdown(Link const& link) {
    Down(link, "the mod unloads");
}

} // namespace mevr::game::world_ui
