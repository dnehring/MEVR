// SPDX-License-Identifier: MIT
// The game's full-screen screens in the room: the game's side (game/WorldUi.hpp, research/53).
#include "game/WorldUi.hpp"

#include "core/Log.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "core/vr/HudPieces.hpp"
#include "core/vr/WorldViewHold.hpp"
#include "game/CockpitProbe.hpp"
#include "game/HandCalibration.hpp"
#if defined(MEVR_GAME_LE3)
#include "game/Le3Gui.hpp"
#endif
#include "game/OmniTool.hpp"
#include "game/Player.hpp"
#include "game/UiWorldFigure.hpp"
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
#include <sstream>
#include <string>
#include <vector>

namespace mevr::game::world_ui {

namespace {

namespace wu = vr::world_ui;

// A screen's tag as this game names it: ME1's and ME2's GUI library tags, ME3's movie tags (core/vr/WorldUi's FromMe3Tag).
wu::Screen ScreenOf(std::string_view tag) {
#if defined(MEVR_GAME_LE3)
    return wu::FromMe3Tag(tag);
#else
    return wu::FromTag(tag);
#endif
}

// The GUI manager (the player controller's cached one) and its panels: each open screen is a BioSFPanel tagged with its library
// tag (the shop's: nmTag "shop", bFullScreen, m_DefaultHandler its BioSFHandler_Shop; measured 2026-11-04).
// Every game (ME2: its pause menu's pages, research/le2-menus.md "ME2's screens on the wrist"); the loot, shop and minigame
// handles below stay ME1's (ME2 has no BioSFHandler_Loot, _Shop or BioSkillGame_Handler: tools/le2-name-audit.py game/WorldUi).
ue3::FieldRef gCachedManager{"Engine.PlayerController", "m_oCachedScaleFormManager", ue3::kEveryGame};
ue3::FieldRef gPanels{"Engine.BioSFManager", "m_lstPanels", ue3::kEveryGame};
ue3::FieldRef gFocusPanel{"Engine.BioSFManager", "m_FocusPanel", ue3::kEveryGame};
ue3::FieldRef gPanelTag{"Engine.BioSFPanel", "nmTag", ue3::kEveryGame};
ue3::FieldRef gPanelFullScreen{"Engine.BioSFPanel", "bFullScreen", ue3::kEveryGame};
ue3::FieldRef gPanelHandler{"Engine.BioSFPanel", "m_DefaultHandler", ue3::kEveryGame};
ue3::FieldRef gPanelVisible{"Engine.BioSFPanel", "IsVisible", ue3::kEveryGame};
#if !defined(MEVR_GAME_LE3)
// A panel kept from an earlier tick is checked to still be one before anything is written to it or called on it: IsAlive only
// says its GObjects slot holds that address, which a new object of another class can take after the panel was freed.
ue3::ClassRef gPanelClass{"Engine.BioSFPanel", ue3::kEveryGame};

bool StillPanel(ue3::Object* panel) {
    return panel != nullptr && ue3::runtime::IsAlive(panel) && ue3::IsA(panel, gPanelClass);
}
#endif
#if defined(MEVR_GAME_LE2)
ue3::FunctionRef gSetMovieVisibility{"Engine.BioSFPanel.SetMovieVisibility", ue3::kEveryGame};
std::vector<ue3::Object*> gHiddenBrowsers;  // still on the native stack so Back can return to them

bool SetMovieVisibility(ue3::Object* panel, bool visible) {
    auto* const fn = gSetMovieVisibility.Get();
    if (fn == nullptr || !StillPanel(panel)) return false;
    ue3::Params p(ue3::runtime::Get(), fn);
    return p.SetBool("bVisible", visible) && ue3::runtime::Call(panel, fn, p);
}

void RestoreBrowsers() {
    for (auto* const panel : gHiddenBrowsers) {
        SetMovieVisibility(panel, true);
    }
    gHiddenBrowsers.clear();
}
#endif
// The loot screen's inventory belongs to the container (or the body) it came from: its outer chain reaches that actor.
ue3::ClassRef gLootHandlerClass{"SFXGame.BioSFHandler_Loot"};
ue3::FieldRef gLootInventory{"SFXGame.BioSFHandler_Loot", "m_oLootInventory"};
ue3::ClassRef gActorClass{"Engine.Actor"};
// The bypass minigame's handler names the placeable it's played on.
ue3::ClassRef gSkillGameHandlerClass{"SFXGame.BioSkillGame_Handler"};
ue3::FieldRef gSkillGameDependent{"SFXGame.BioSkillGame_Handler", "m_oDependent"};
// Its puzzle is being played (set by the manual override; research/53 section 6.3).
ue3::FieldRef gSkillGameActive{"SFXGame.BioSkillGame_Handler", "m_bGameActive"};
bool gPuzzleLogged = false;
// What A would use (research/40's BioPlayerSelection): the thing a screen opened by a use belongs to.
ue3::FieldRef gPlayerSelection{"SFXGame.BioPlayerController", "m_oPlayerSelection", ue3::kEveryGame};
ue3::FieldRef gCurrentTarget{"SFXGame.BioPlayerSelection", "m_oCurrentSelectionTarget", ue3::kEveryGame};
ue3::ClassRef gShopHandlerClass{"SFXGame.BioSFHandler_Shop"};
ue3::FieldRef gShopActor{"SFXGame.BioSFHandler_Shop", "m_shop"};
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame}; // existing ReflectCheck row

// Shepard's view on foot (the last camera update in mode 0), and the player camera's POV fields while a screen is up: the host
// puts the former into the latter for the main view's render only (the shop's own camera looks at a backdrop stage elsewhere).
struct Hold {
    bool haveView = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 90.0f;
    std::optional<engine::TrackingFrame> footFrame;  // the tracking frame of that view (the anchor's conversion while held)
    vr::host_view::State footView;
    float rawZ = 0.0f;
    float* povLocation = nullptr;
    ue3::Rotator* povRotation = nullptr;
    float* povFov = nullptr;
    std::uint64_t povMs = 0;
};
Hold gHold;
vr::world_view::Reference gRoomReference;
bool gRoomChanged = false;
bool gScreenUp = false;  // a screen of ours was published last tick (game thread)
// The last play mode the player was in (each tick) may hold the view remembered on foot (vr::world_ui::FootViewMode): not ME3's
// Atlas or injured Shepard, whose screens keep the native panel instead of a view remembered anywhere earlier in the level.
bool gFootViewMode = true;
// `mevr worldui firstheld off`: the old rule (the POV's place recorded only once a screen was up), an A/B of the first state's
// hold; on by default.
bool gPovAlways = true;
unsigned gUnheldStates = 0;              // states published without the hold they wanted (the POV not seen yet)
std::uint32_t gUnheldLoggedOpening = 0;

// The Mako's cockpit as the last frame with a head-tracked view saw it (tracking space; the cockpit stands still in the room
// while it rides): its HUD anchor at the windshield and the eyes. Kept through the game's GUI mode, where the view anchor is gone
// as soon as Halcyon shows its flat menu screen, which it does until our screen is up (2026-11-05 08:01: the box went flat).
struct Cockpit {
    bool valid = false;
    XrVector3f anchor{};
    XrVector3f eyes{};
};
Cockpit gCockpit;

void NoteCockpit() {
    if (!cockpit_probe::Riding()) {
        gCockpit = {};
        return;
    }
    vr::hud_pieces::Pieces pieces;
    cockpit_probe::FillHud(pieces);
    if (auto const frame = view_anchor::Current(); pieces.cockpit && frame) {
        gCockpit = {true, pieces.panelAnchor, frame->head.position};
    }
}
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
wu::Opening gOpening;
std::uint32_t gRequestSerial = static_cast<std::uint32_t>(GetTickCount64());
wu::OpeningSource gOpeningSource;
bool gOpeningSawGui = false;

// A full-screen panel (bFullScreen) makes the game render its UI world (GUI_UIWorld: a backdrop stage of blurred machinery
// under a blue gradient, its own camera) instead of the level; with the flag cleared the level renders behind the screen
// (measured 2026-11-04 at the shop, research/53). Cleared on every screen of ours open while ours stands in the room (the map
// opens over the main wheel: both full screen), set again on those still open if ours goes down.
std::vector<ue3::Object*> gClearedPanels;

void RestoreFullScreen() {
    int restored = 0;
    for (ue3::Object* const panel : gClearedPanels) {
#if defined(MEVR_GAME_LE3)
        if (panel != nullptr && ue3::runtime::IsAlive(panel)) {  // never filled on ME3 (no bFullScreen)
#else
        if (StillPanel(panel)) {
#endif
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
    // Screens opened (a new mode-7 stretch with one of ours, or another of ours taking over inside it: the omni-tool's rail
    // switching from the equipment to the journal); starts from the clock, so a reloaded mod never repeats a number the host has
    // placed. The host places and labels a screen once per opening (plugin/WorldUi).
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

struct OpenPanel {
    ue3::Object* panel = nullptr;
    std::string tag;
    bool fullScreen = false;
    bool visible = false;
    bool focus = false;
    ue3::Object* handler = nullptr;
    bool uiWorld = false;  // ME3: the movie shows the game's UI world (SFXGUIMovie.m_bRequiresUIWorld)
};

#if defined(MEVR_GAME_LE3)
// ME3's adapter (research/le3-native-gui.md "The game's screens on ME3"): no BioSFManager and no list of open movies to read
// (SFXGUIInteraction.GetAllMovies returned none through ProcessEvent), so the screen is the movie the GUI interaction has in
// focus (SFXGUIInteraction.GetFocusMovieForPlayerController, game/Le3Gui), its nmTag ME3's movie tag. A message box over one of
// ours stands on that screen's place, as ME1's does over its parent (the parent is the last screen of ours in focus this GUI
// stretch). A movie that shows the game's UI world (m_bRequiresUIWorld: the squad record's Shepard) keeps Halcyon's native
// panel until ME1's figure (game/UiWorldFigure) is ported.
ue3::FieldRef gRequiresUiWorld{"SFXGame.SFXGUIMovie", "m_bRequiresUIWorld", ue3::kEveryGame};
OpenPanel gLe3Parent;  // game thread; cleared when the GUI mode ends and forgotten on a room change
// A movie kept from an earlier tick (gLe3Parent, gUiWorldCleared) is checked to still be one before anything is called on it:
// IsAlive only says its GObjects slot holds that address, which a new object of another class can take after the movie was
// freed, and runtime::Call doesn't check the class.
ue3::ClassRef gGuiMovieClass{"SFXGame.SFXGUIMovie", ue3::kEveryGame};

bool StillMovie(ue3::Object* movie) {
    return movie != nullptr && ue3::runtime::IsAlive(movie) && ue3::IsA(movie, gGuiMovieClass);
}

// Every Mission Computer page and the War Terminal's War Assets ask for the UI world (SFXGUIInteraction's script calls
// SetRequiresUIWorld(TRUE) as it opens them), and while one does the game renders the UI world's dark backdrop instead of the
// level: the screen stood in a dark void, not the room (2026-11-16, Save 31). So, as ME1's bFullScreen (RestoreFullScreen),
// our screens without a squad member in it are told the game's own way (the native setter, never a raw write) that they don't
// need it while they stand in the room, and told again that they do when ours goes down.
ue3::FunctionRef gSetRequiresUiWorld{"SFXGame.SFXGUIMovie.SetRequiresUIWorld", ue3::kEveryGame};
std::vector<ue3::Object*> gUiWorldCleared;  // game thread
std::vector<ue3::Object*> gUiWorldAskedAgain;  // ... of those, the ones whose script asked for the UI world again (logged once)
unsigned gUiWorldClearedAgain = 0;           // times told again (the heartbeat's world ui line)

bool SetRequiresUiWorld(ue3::Object* movie, bool required) {
    ue3::Function* const fn = gSetRequiresUiWorld.Get();
    if (fn == nullptr || !StillMovie(movie)) return false;
    ue3::Params p(ue3::runtime::Get(), fn);
    return p.SetBool("bUIWorldRequired", required) && ue3::runtime::Call(movie, fn, p);
}

void RestoreUiWorld() {
    int restored = 0;
    for (ue3::Object* const movie : gUiWorldCleared) {
        if (SetRequiresUiWorld(movie, true)) ++restored;
    }
    if (restored > 0) {
        log::Info("world ui: {} of the game's screens ask for its UI world again (SFXGUIMovie.SetRequiresUIWorld(true))", restored);
    }
    gUiWorldCleared.clear();
    gUiWorldAskedAgain.clear();
}

bool Resolved() {
    return gRequiresUiWorld.Get() != nullptr;  // game/Le3Gui's handles answer per call (no movie: no screen of ours)
}

std::vector<OpenPanel> OpenPanels(ue3::Object* /*controller*/) {
    std::vector<OpenPanel> open;
    ue3::Object* const focus = le3_gui::FocusMovie();
    if (focus == nullptr) {
        return open;
    }
    OpenPanel top;
    top.panel = focus;
    top.handler = focus;
    top.tag = le3_gui::Tag(focus);
    top.visible = true;
    top.focus = true;
    top.uiWorld = gRequiresUiWorld.ReadBool(focus).value_or(false);
    wu::Screen const screen = ScreenOf(top.tag);
    bool const dialog = screen == wu::FromTag("MessageBox");  // avoid Windows.h's MessageBox macro at the enum's use site
    if (dialog && gLe3Parent.panel != nullptr && gLe3Parent.panel != focus && StillMovie(gLe3Parent.panel)) {
        OpenPanel parent = gLe3Parent;
        parent.focus = false;
        open.push_back(parent);
    } else if (!dialog && screen != wu::Screen::None) {
        gLe3Parent = top;
    }
    open.push_back(std::move(top));
    return open;
}
#else
bool Resolved() {
    return gCachedManager.Get() != nullptr && gPanels.Get() != nullptr && gFocusPanel.Get() != nullptr && gPanelTag.Get() != nullptr &&
           gPanelFullScreen.Get() != nullptr && gPanelHandler.Get() != nullptr;
}

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
#endif

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
        screen == wu::Screen::Specialization || screen == wu::Screen::Loot || screen == wu::Screen::WarAssets ||
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

void ObserveRoom(ue3::Object* controller) {
    auto* const world = controller != nullptr ? gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    if (!gRoomReference.Observe({reinterpret_cast<std::uintptr_t>(world), reinterpret_cast<std::uintptr_t>(controller)})) return;
    if (gHold.haveView) log::Info("world ui: discarded the saved view and use target (player/world changed)");
    gHold = {};
    gUsed = {};
    gUsedAtOpening = {};
    gVendor = {};
    gCockpit = {};
    gScreenUp = false;
    gOpeningSource.Clear();
    gRoomChanged = true; // Tick releases the host's state/reservation and restores panel flags.
    if (gWristUp) {
        view_anchor::Hold(std::nullopt);
        gWristUp = false;
    }
}

// Engine.Camera.UpdateCamera, after the game's: on foot, Shepard's view is recorded; while a screen of ours is up, where the POV
// lives (the host swaps the recorded view in at render time).
void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) {
        return;
    }
    ObserveRoom(controller);
    auto const pov = player::CameraPov(camera);
    if (!pov || pov->location == nullptr || pov->rotation == nullptr || pov->fov == nullptr) {
        return;
    }
    // Where the POV lives, at every update of the player's camera (the camera's own fields, the same on foot and behind a
    // screen): the first state published for an opening then already holds the view. Recorded only once a screen was up, the
    // first frame after every opening was drawn from the screen's own camera (ME3's: inside Shepard's chest).
    if (gPovAlways || gScreenUp) {
        gHold.povLocation = reinterpret_cast<float*>(pov->location);
        gHold.povRotation = pov->rotation;
        gHold.povFov = pov->fov;
        gHold.povMs = GetTickCount64();
    }
    auto const mode = player::GameMode(controller);
    if (mode && gRoomReference.Remember(*mode, player::FloorLockSample().cameraOwned, gScreenUp)) {
        gHold.location = *pov->location;
        gHold.rotation = *pov->rotation;
        gHold.rawZ = pov->location->z;
        auto const view = vr::host_view::Latest();
        auto const basis = engine::ToBasis(engine::UnrealRotation{gHold.rotation.pitch, gHold.rotation.yaw, gHold.rotation.roll});
        gHold.location.z = static_cast<float>(vr::host_view::HoldBaseZ(view, gHold.rawZ,
            basis.right.z, basis.up.z, basis.forward.z));
        gHold.fov = *pov->fov;
        gHold.haveView = true;
    }
}

bool gRailOmniTool = false;  // Tick: the host's rail is open alone on foot (Down keeps the omni-tool for it)
bool gEmitterOnly = false;   // an inactive state with only the omni-tool's display is published (the rail alone stands there)

unsigned UiWorldClearedAgain() {
#if defined(MEVR_GAME_LE3)
    return gUiWorldClearedAgain;
#else
    return 0;
#endif
}

void SetStatus(std::string json) {
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    gStatusJson = std::move(json);
}

// A room or world change (game/WorldViewHold's Reference): the screens' objects kept from the old one (the panels made not
// full screen, ME2's hidden browsers, ME3's movies told not to ask for the UI world and the message box's parent) are
// forgotten without a call or a write: they went with the old world, and an address the engine gave back can hold another
// object by now (research/le3-native-gui.md "Room changes").
void ForgetScreenObjects() {
    if (!gClearedPanels.empty()) {
        log::Info("world ui: forgot {} screen(s) made not full screen in the old room (not touched again)", gClearedPanels.size());
    }
    gClearedPanels.clear();
#if defined(MEVR_GAME_LE2)
    gHiddenBrowsers.clear();
#endif
#if defined(MEVR_GAME_LE3)
    if (!gUiWorldCleared.empty()) {
        log::Info("world ui: forgot {} screen(s) told not to ask for the UI world in the old room (not called again)",
            gUiWorldCleared.size());
    }
    gUiWorldCleared.clear();
    gUiWorldAskedAgain.clear();
    gLe3Parent = {};
#endif
}

void Down(Link const& link, char const* why) {
    ui_world_figure::Off(why);
    gPuzzleLogged = false;
    if (gS.published && link.setState != nullptr) {
        link.setState(nullptr);
        log::Info("world ui: {} down ({})", gS.screen.empty() ? "the screen" : gS.screen, why);
    }
    RestoreFullScreen();
#if defined(MEVR_GAME_LE2)
    RestoreBrowsers();
#endif
#if defined(MEVR_GAME_LE3)
    RestoreUiWorld();
#endif
    omni_tool::Tick(gRailOmniTool);  // the rail alone on the wrist still wears it
    // ... and stands at its display, as the screens are projected from it (the rendered arm can fall short of the controller):
    // an inactive state carrying only the emitter (the host shows no screen for it).
    std::optional<ue3::Vector> const display = gRailOmniTool && link.setState != nullptr ? omni_tool::DisplayWorld() : std::nullopt;
    if (link.setState != nullptr && (omni_tool::OnMs() != 0 || (display && gHold.footFrame))) {
        wu::State state;
        state.omniOnMs = omni_tool::OnMs();
        if (display && gHold.footFrame) {
            XrVector3f const p = engine::ToTracking(*gHold.footFrame, engine::UnrealVector{display->x, display->y, display->z});
            state.emitter[0] = p.x;
            state.emitter[1] = p.y;
            state.emitter[2] = p.z;
            state.emitterValid = 1;
        }
        link.setState(&state);
        gEmitterOnly = true;
    } else if (gEmitterOnly && link.setState != nullptr) {
        link.setState(nullptr);
        gEmitterOnly = false;
    }
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
    ue3::Function* const update = ue3::runtime::Up() ? gUpdateCamera.Get() : nullptr;  // every game (LE1: Up() is Ready())
    if (update == nullptr) {
        log::Warn("world ui: Engine.Camera.UpdateCamera not found: the game's screens can't hold Shepard's view (they stay on Halcyon's panel)");
        return;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
}

void CancelOpening(Link const& link, std::uint32_t request) {
    bool const cancelled = gOpening.Cancel(request);
    if ((cancelled || request == 0) && link.reserve != nullptr) link.reserve(request, 0, 0);
    if (cancelled || request == 0) gOpeningSource.Clear();
}

std::uint32_t BeginOpening(Link const& link, vr::game_menu::Action action, vr::game_menu::Origin origin) {
    ObserveRoom(player::Controller());
    CancelOpening(link);
    if (link.setState != nullptr) link.setState(nullptr);  // an earlier screen cannot authorize this request
    wu::Screen screen = wu::Screen::None;
    using Action = vr::game_menu::Action;
    switch (action) {
    case Action::PauseMenu: screen = wu::Screen::MenuBrowser; break;
    case Action::Equipment: screen = wu::Screen::Inventory; break;
    case Action::Squad: screen = wu::Screen::CharacterRecord; break;
    case Action::Journal: screen = wu::Screen::Journal; break;
    case Action::Codex: screen = wu::Screen::Codex; break;
    case Action::Map: screen = wu::Screen::Map; break;
    default: return 0;
    }
    wu::Settings const settings = hand_calibration::GetWorldUi();
    // Squad and map open over the browser wheel. The entire known opening stack must be admitted before its call.
    std::uint32_t const required = wu::Bit(screen) | ((action == Action::Squad || action == Action::Map) ? wu::Bit(wu::Screen::MenuBrowser) : 0u);
    auto const mode = player::GameMode(player::Controller());
    if (link.setState == nullptr || link.reserve == nullptr || !settings.on || !Resolved() ||
        (settings.screens & required) != required || !mode || !wu::PlayMode(*mode) ||
        (!gCockpit.valid && (!gHold.haveView || !gHold.footFrame || !wu::FootViewMode(*mode)))) {
        return 0;
    }
    if (++gRequestSerial == 0) ++gRequestSerial;
    gOpening.Begin(gRequestSerial, screen, origin, GetTickCount64());
    gOpeningSource.Begin(screen, origin);
    gOpeningSawGui = false;
    link.reserve(gRequestSerial, static_cast<std::uint32_t>(screen), static_cast<std::uint32_t>(origin));
    return gRequestSerial;
}

void Tick(Link const& link) {
    wu::Settings const settings = hand_calibration::GetWorldUi();
    ue3::Object* const controller = player::Controller();
    ObserveRoom(controller);
    if (gRoomChanged) {
        gRailOmniTool = false;
        CancelOpening(link);
        ForgetScreenObjects();  // before Down, which would restore them
        Down(link, "player/world changed");
        gRoomChanged = false;
    }
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    if (mode && wu::PlayMode(*mode)) {
        bool const footViewMode = wu::FootViewMode(*mode);
        if (footViewMode != gFootViewMode) {
            log::Info("world ui: game mode {}: {}", *mode, footViewMode ? "Shepard on foot again: screens may hold the view on foot"
                                                                         : "Shepard isn't on foot (the Atlas, injured): a screen opened "
                                                                           "here holds no view remembered on foot (the native panel)");
        }
        gFootViewMode = footViewMode;
    }
    if (gOpening.Expire(GetTickCount64())) {
        if (link.reserve != nullptr) link.reserve(0, 0, 0);
    }
    if (mode && wu::ScreenMode(*mode)) {
        gOpeningSawGui = true;
    } else {
        gOpeningSource.Clear();
        if (gOpeningSawGui || !mode) CancelOpening(link);
    }
    // The host's rail alone on the wrist (a tap, no screen yet): the game's omni-tool on Shepard's arm, on foot only.
    bool const railOmniTool = link.railOpen && settings.omniTool && mode && *mode == 0;
    if (railOmniTool != gRailOmniTool) {
        log::Info("world ui: {}", railOmniTool ? "the omni-tool's rail is open on foot: Shepard's arm wears the omni-tool"
                                               : "the omni-tool's rail closed (or a screen took over)");
    }
    gRailOmniTool = railOmniTool;
    NoteCockpit();
    if (mode && *mode == 0 && gRoomReference.Available() && player::FloorLockSample().cameraOwned && !gScreenUp) {
        if (auto const frame = view_anchor::Current()) {
            gHold.footFrame = frame;  // the room as Shepard's view maps it (the held view's)
            gHold.footView = vr::host_view::Latest();
            auto const basis = engine::ToBasis(engine::UnrealRotation{gHold.rotation.pitch, gHold.rotation.yaw, gHold.rotation.roll});
            gHold.location.z = static_cast<float>(vr::host_view::HoldBaseZ(gHold.footView, gHold.rawZ,
                basis.right.z, basis.up.z, basis.forward.z));
        }
        ue3::Object* const selection = controller != nullptr ? gPlayerSelection.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
        ue3::Object* const target = selection != nullptr ? gCurrentTarget.Read<ue3::Object*>(selection).value_or(nullptr) : nullptr;
        if (target != nullptr && ue3::runtime::IsAlive(target)) {
            gUsed = {target, GetTickCount64()};
        }
    }
    if (link.setState == nullptr || !settings.on || !mode || !wu::ScreenMode(*mode)) {
        if (!settings.on || link.setState == nullptr) CancelOpening(link);
        if (!gS.stack.empty()) {
            gS.stack.clear();
        }
#if defined(MEVR_GAME_LE3)
        gLe3Parent = {};  // a message box of a later GUI stretch never stands where this one's screen stood
#endif
        Down(link, link.setState == nullptr ? "an older host" : !settings.on ? "[WorldUi] on = 0" : "the game left its GUI and minigame modes");
        return;
    }
    if (!Resolved()) {
        CancelOpening(link);
        if (!gS.loggedMissing) {
            gS.loggedMissing = true;
#if defined(MEVR_GAME_LE3)
            log::Warn("world ui: SFXGUIMovie.m_bRequiresUIWorld wasn't found through our reflection layer: ME3's screens stay on "
                      "Halcyon's native panel");
#else
            log::Warn("world ui: the GUI manager's panels (BioSFManager.m_lstPanels, BioSFPanel.nmTag...) weren't found through our "
                      "reflection layer: the game's screens stay on Halcyon's panel");
#endif
        }
        return;
    }
    std::vector<OpenPanel> const panels = OpenPanels(controller);
    std::string stack;
    OpenPanel const* base = nullptr;  // the first of ours opened decides (a message box over the shop stays on the shop's place)
#if defined(MEVR_GAME_LE2)
    std::vector<OpenPanel const*> browsers;
    OpenPanel const* focusedBrowser = nullptr;
    OpenPanel const* browserPage = nullptr;
#endif
    for (OpenPanel const& o : panels) {
        stack += std::format("{}{}{}{}{}{}", stack.empty() ? "" : ", ", o.tag, o.fullScreen ? " (full screen)" : "", o.focus ? " (focus)" : "",
                             o.visible ? "" : " (hidden)", o.uiWorld ? " (UI world)" : "");
        if (base == nullptr && o.visible && ScreenOf(o.tag) != wu::Screen::None) {
            base = &o;
        }
#if defined(MEVR_GAME_LE2)
        auto const tag = wu::FromTag(o.tag);
        if (tag == wu::Screen::MenuBrowser) {
            browsers.push_back(&o);
            if (o.focus) focusedBrowser = &o;
        } else if (!browsers.empty() && browserPage == nullptr && o.visible && wu::RailPanel(tag) &&
                   tag != wu::FromTag("MessageBox")) {
            browserPage = &o;  // a dialog over a page still uses the page's placement
        }
#endif
    }
#if defined(MEVR_GAME_LE2)
    std::erase_if(gHiddenBrowsers, [&](ue3::Object* panel) {
        return std::none_of(browsers.begin(), browsers.end(), [&](OpenPanel const* browser) { return browser->panel == panel; });
    });
    bool const fromBrowser = !browsers.empty() &&
        (base == nullptr || wu::FromTag(base->tag) == wu::Screen::MenuBrowser || base == browserPage);
    if (fromBrowser) {
        if (browserPage != nullptr) base = browserPage;
        else if (focusedBrowser != nullptr) base = focusedBrowser;
    }
#endif
    if (stack != gS.stack) {
        log::Info("world ui: the game's open panels: {}", stack.empty() ? "none" : stack);
        gS.stack = stack;
    }
    wu::Screen const screen = base != nullptr ? ScreenOf(base->tag) : wu::Screen::None;
    std::uint32_t visibleScreens = 0;
    for (OpenPanel const& o : panels) {
        if (o.visible && ScreenOf(o.tag) != wu::Screen::None) visibleScreens |= wu::Bit(ScreenOf(o.tag));
    }
    if (screen == wu::Screen::None) {
        (void)gOpeningSource.Observe(*mode, screen, visibleScreens);
        Down(link, "no screen of ours open");
        return;
    }
    // Every one of ours open must be listed: a screen that shows a 3D model of its own in the game's UI world (the equipment's
    // squadmate, the character record's Shepard) keeps Halcyon's panel, and the main wheel it opened from with it.
    for (OpenPanel const& o : panels) {
        if (wu::Screen const s = ScreenOf(o.tag); o.visible && s != wu::Screen::None && (settings.screens & wu::Bit(s)) == 0) {
            CancelOpening(link);
            Down(link, "not one of [WorldUi] screens");
            return;
        }
        // ME3: a screen whose squad member stands in the game's UI world (the squad record's Shepard; m_bRequiresUIWorld, which
        // every Mission Computer page sets, measured 2026-11-16) keeps the native panel, as ME1's did before its figure
        // (game/UiWorldFigure, LE1 only so far).
        if (o.visible && o.uiWorld && wu::ShowsFigure(ScreenOf(o.tag))) {
            CancelOpening(link);
            Down(link, "it shows the game's UI world (a 3D model behind it): Halcyon's native panel");
            return;
        }
    }
    // In the Mako's cockpit (the extraction's "Return to Normandy?", the pause menu): the cockpit stays around the eyes through the
    // GUI mode (game/CockpitProbe), so the screen goes out through its windshield (NoteCockpit's anchor), and Shepard's view on
    // foot (from wherever Shepard last stood, long before the Mako) is never held: that showed bare terrain under the question
    // with no cockpit (2026-11-05, research/48 section 27).
    std::optional<XrVector3f> cockpitAnchor;
    XrVector3f eyesNow{};
    if (gCockpit.valid) {
        cockpitAnchor = gCockpit.anchor;
        eyesNow = gCockpit.eyes;
    }
    bool const inCockpit = cockpitAnchor.has_value();
    bool const haveFootView = gRoomReference.Available() && gHold.haveView && gHold.footFrame.has_value() && gFootViewMode;
    if (!inCockpit && !wu::CanPlaceWithView(screen, haveFootView)) {
        CancelOpening(link);
        Down(link, gFootViewMode ? "Shepard's view on foot isn't known (opened before any camera update on foot)"
                                 : "opened from a mode not on foot (the Atlas, injured Shepard): no view on foot is held");
        return;
    }
    bool const matched = gOpening.Matches(gOpening.Request(), visibleScreens);
    if (!matched && gOpening.Pending() && screen != wu::Screen::MenuBrowser) CancelOpening(link);
    auto const origin = gOpeningSource.Observe(*mode, screen, visibleScreens);
    bool const railOpening = origin == vr::game_menu::Origin::OmniRail;
    std::erase_if(gClearedPanels, [&](ue3::Object* p) {
        return std::none_of(panels.begin(), panels.end(), [&](OpenPanel const& o) { return o.panel == p; });  // closed since
    });
    for (OpenPanel const& o : panels) {
        if (o.fullScreen && ScreenOf(o.tag) != wu::Screen::None && gPanelFullScreen.WriteBool(o.panel, false)) {
            gClearedPanels.push_back(o.panel);
            log::Info("world ui: the {} screen isn't full screen any more: the level renders behind it, not the game's UI world", o.tag);
        }
#if defined(MEVR_GAME_LE3)
        // ME3's way of the same (the squad record's movie never gets here: it keeps the native panel above). ME3's script sets
        // the flag again as a page opens in the same movie (research/le3-native-gui.md item 5), so a movie already told is
        // told again whenever it reads true again; else the void came back behind the next page.
        if (o.uiWorld && ScreenOf(o.tag) != wu::Screen::None && SetRequiresUiWorld(o.panel, false)) {
            if (std::find(gUiWorldCleared.begin(), gUiWorldCleared.end(), o.panel) == gUiWorldCleared.end()) {
                gUiWorldCleared.push_back(o.panel);
                log::Info("world ui: the {} screen doesn't ask for the game's UI world any more (SFXGUIMovie.SetRequiresUIWorld(false)): "
                          "the level renders behind it", o.tag);
            } else {
                ++gUiWorldClearedAgain;
                if (std::find(gUiWorldAskedAgain.begin(), gUiWorldAskedAgain.end(), o.panel) == gUiWorldAskedAgain.end()) {
                    gUiWorldAskedAgain.push_back(o.panel);
                    log::Info("world ui: the {} screen asked for the game's UI world again (its script, as a page opened): told again "
                              "it doesn't", o.tag);
                }
            }
        }
#endif
    }
    if (matched || !gS.open || gS.screen != wu::Name(screen)) {
        if (gS.open) {
            log::Info("world ui: {} takes over from {} (a new opening: placed and unfolded again)", wu::Name(screen), gS.screen);
            gS.published = false;  // logged as up again below
        }
        gS.open = true;
        ++gS.opening;
        gS.screen = wu::Name(screen);
        std::uint64_t const now = GetTickCount64();
        gUsedAtOpening = wu::UseAnchor(origin) && gUsed.target != nullptr && now - gUsed.ms < kUsedWithinMs ? gUsed : Used{};
    }
    wu::State state;
    state.active = 1;
    state.screen = static_cast<std::uint8_t>(screen);
    state.opening = gS.opening;
    state.openingRequest = matched ? gOpening.Request() : 0;
    state.visibleScreens = visibleScreens;
    state.widthM = wu::RoomWidth(screen, settings.widthM);  // the loot keeps the stage's scale, its empty sides cut
    state.distanceM = settings.distanceM;
    state.belowM = settings.belowM;
    state.backdrop = settings.backdrop;
    bool const fromWheel = std::any_of(panels.begin(), panels.end(), [](OpenPanel const& o) { return o.visible && ScreenOf(o.tag) == wu::Screen::MenuBrowser; });
    if (!wu::UseAnchor(origin)) {
        state.anchored = 0;  // explicit rail provenance: neither a remembered selection nor a handler owns this opening
    } else if (inCockpit) {
        wu::CockpitPlacement const place = wu::InCockpit(eyesNow, *cockpitAnchor, state.widthM, settings.distanceM);
        state.anchored = 1;
        state.anchor[0] = cockpitAnchor->x;
        state.anchor[1] = cockpitAnchor->y;
        state.anchor[2] = cockpitAnchor->z;
        state.belowM = place.belowM;
        state.widthM = place.widthM;
    } else if (haveFootView) {
        if (auto const at = AnchorOf(screen, base->handler, controller, gS.opening, fromWheel)) {
            XrVector3f const p = engine::ToTracking(*gHold.footFrame, engine::UnrealVector{at->x, at->y, at->z});
            state.anchored = 1;
            state.anchor[0] = p.x;
            state.anchor[1] = p.y;
            state.anchor[2] = p.z;
        }
    }
    // On the wrist (the omni-tool, research/57): a listed screen nothing in the world owns; one a use opened (a locker's equipment)
    // stands by what was used. The bypass minigame is the omni-tool's own, so it always goes on the wrist; never in the Mako's
    // cockpit (its screens stand at the windshield), except explicit rail picks, which always request the wrist.
    bool const onWrist = (inCockpit || haveFootView) && (!inCockpit || railOpening) && wu::OnWrist(settings, screen, state.anchored != 0, origin);
#if defined(MEVR_GAME_LE2)
    if (onWrist && fromBrowser) {
        // Back from a child can push a fresh browser over the old one. Only the current page's movie belongs on the wrist.
        for (auto const* browser : browsers) {
            auto const hidden = std::find(gHiddenBrowsers.begin(), gHiddenBrowsers.end(), browser->panel);
            if (browser == base) {
                if (hidden != gHiddenBrowsers.end() && SetMovieVisibility(browser->panel, true)) {
                    gHiddenBrowsers.erase(hidden);
                }
            } else if (browser->visible && hidden == gHiddenBrowsers.end() && SetMovieVisibility(browser->panel, false)) {
                gHiddenBrowsers.push_back(browser->panel);
            }
        }
    } else {
        RestoreBrowsers();
    }
#endif
    wu::Rect const crop = wu::DefaultCrop(screen);  // the room's; on the wrist the host shows WristCrop
    state.stageCrop[0] = crop.u0;
    state.stageCrop[1] = crop.v0;
    state.stageCrop[2] = crop.u1;
    state.stageCrop[3] = crop.v1;
    std::strncpy(state.tag, base->tag.c_str(), sizeof(state.tag) - 1);
    state.placement = static_cast<std::uint8_t>(!onWrist ? wu::Placement::Room : settings.wristFollow ? wu::Placement::Wrist : wu::Placement::WristOnce);
    state.beam = settings.wristBeam ? 1 : 0;
    state.wristWidthM = wu::WristWidth(settings, screen);
    state.wristAboveM = settings.wristAboveM;
    state.wristTowardM = settings.wristTowardM;
    // The bypass puzzle being played: the host keeps the game pointer off its screen and shows the d-pad (research/53 section 6.3).
    bool const puzzle = screen == wu::Screen::SkillGame && base->handler != nullptr && ue3::IsA(base->handler, gSkillGameHandlerClass) &&
                        ue3::runtime::IsAlive(base->handler) && gSkillGameActive.ReadBool(base->handler).value_or(false);
    state.puzzle = puzzle ? 1 : 0;
    if (puzzle != gPuzzleLogged) {
        gPuzzleLogged = puzzle;
        log::Info("world ui: the bypass puzzle {}", puzzle ? "started (m_bGameActive): the omni-tool's d-pad plays it, the pointer stays off it"
                                                          : "isn't being played");
    }
    // The projector: the omni-tool's display as Shepard's arm shows it (last tick's pose), in the room's frame on foot.
    if (auto const display = onWrist && !inCockpit ? omni_tool::DisplayWorld() : std::nullopt; display && gHold.footFrame) {
        XrVector3f const p = engine::ToTracking(*gHold.footFrame, engine::UnrealVector{display->x, display->y, display->z});
        state.emitter[0] = p.x;
        state.emitter[1] = p.y;
        state.emitter[2] = p.z;
        state.emitterValid = 1;
    }
    bool const wasAnchored = gS.anchored;
    gS.anchored = state.anchored != 0;
    std::copy(std::begin(state.anchor), std::end(state.anchor), std::begin(gS.anchor));
    if (!gS.published || wasAnchored != gS.anchored) {
        if (!inCockpit && !haveFootView) {
            log::Info("world ui: {} uses the destination's current camera (no saved view in this world; no render hold)", gS.screen);
        }
        log::Info("world ui: {} up (opening {}){}{}", gS.screen, gS.opening,
            inCockpit ? std::format(": in the Mako's cockpit, at its windshield toward ({:.2f}, {:.2f}, {:.2f}) m, {:.2f} m wide, {:.2f} m below "
                                    "the eyes (no view held)",
                            gS.anchor[0], gS.anchor[1], gS.anchor[2], state.widthM, state.belowM)
            : gS.anchored ? std::format(": it belongs to what stands at ({:.2f}, {:.2f}, {:.2f}) m in the room", gS.anchor[0], gS.anchor[1], gS.anchor[2])
                        : std::string(": nothing in the world to stand it by; straight ahead"),
            onWrist ? "; projected from the left wrist (in the room if the left controller isn't tracked)" : "");
    }
    std::uint64_t const now = GetTickCount64();
    if (!inCockpit && haveFootView && gHold.povLocation != nullptr && now - gHold.povMs < 500) {
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
    } else if (!inCockpit && haveFootView) {
        ++gUnheldStates;  // the next frame is drawn from the screen's own camera
        if (gUnheldLoggedOpening != gS.opening) {
            gUnheldLoggedOpening = gS.opening;
            log::Info("world ui: {} (opening {}) published without the held view: the player camera's POV not seen yet ({} so far)",
                gS.screen, gS.opening, gUnheldStates);
        }
    }
    ui_world_figure::Tick(settings, screen, state);  // the equipment and squad screens' squad member behind them (research/57 section 7)
    omni_tool::Tick(onWrist && settings.omniTool);
    state.omniOnMs = omni_tool::OnMs();
    link.setState(&state);
    if (matched) gOpening.Cancel(0);  // synchronous matching publication released the host reservation
    gEmitterOnly = false;  // the screen's state took its place
    gS.published = true;
    gScreenUp = true;
    ++gS.ticks;
    if (onWrist != gWristUp) {
        if (onWrist && !inCockpit && gHold.footFrame) {
            auto const& v = gHold.footView;
            auto const basis = engine::ToBasis(engine::UnrealRotation{gHold.rotation.pitch, gHold.rotation.yaw, gHold.rotation.roll});
            double const commonZ = basis.right.z * v.offsetRight + basis.up.z * v.offsetUp + basis.forward.z * v.offsetForward;
            log::Info("floor lock: world UI hold start mode {}: locked {}, raw Z {:.3f}, held base Z {:.3f}, common Z {:+.3f}, "
                      "render centre Z {:.3f}, IK head Z {:.3f}, delta {:+.3f}, floor Z {:.3f}, saved correction {:+.3f}",
                *mode, v.floorLocked, gHold.rawZ, gHold.location.z, commonZ, gHold.location.z + commonZ,
                gHold.footFrame->headWorld.z, gHold.footFrame->headWorld.z - (gHold.location.z + commonZ), v.pawnFloorZ, v.floorCorrection);
        }
        log::Info("world ui: {}", onWrist ? "the arms follow the controllers while the screen is on the wrist (Shepard's view on foot held)"
                                          : "the arms go back to the game");
    }
    gWristUp = onWrist;
    view_anchor::Hold(onWrist && !inCockpit ? gHold.footFrame : std::nullopt, gHold.footView);  // rail in the Mako uses its controller
    SetStatus(std::format("{{\"open\": true, \"screen\": \"{}\", \"stack\": \"{}\", \"opening\": {}, \"anchored\": {}, \"anchor\": [{:.3f}, {:.3f}, {:.3f}], "
                          "\"placement\": {}, \"inCockpit\": {}, \"heldView\": {}, \"unheldStates\": {}, \"uiWorldClearedAgain\": {}, "
                          "\"omniTool\": {}, \"figure\": {}}}",
        gS.screen, stack, gS.opening, gS.anchored ? "true" : "false", gS.anchor[0], gS.anchor[1], gS.anchor[2], state.placement,
        inCockpit ? "true" : "false", state.holdRender != 0 ? "true" : "false", gUnheldStates, UiWorldClearedAgain(),
        omni_tool::StatusJson(), ui_world_figure::StatusJson()));
}

bool WristScreenUp() {
    return gWristUp;
}

std::optional<engine::TrackingFrame> WristTrackingFrame() {
    auto const mode = player::GameMode(player::Controller());
    return gWristUp && view_anchor::Held() && gHold.haveView && mode && wu::ScreenMode(*mode) ? gHold.footFrame : std::nullopt;
}

std::string Console(std::string_view args) {
    std::istringstream words{std::string(args)};
    std::string sub, value;
    words >> sub >> value;
    if (sub == "firstheld") {
        if (value == "on" || value == "off") gPovAlways = value == "on";
        else if (!value.empty()) return "usage: mevr worldui firstheld on|off";
        return std::format("world ui: the first state of an opening {} (the POV's place {}); {} states published without the held view so far",
            gPovAlways ? "holds the view" : "holds no view (the old rule, an A/B)",
            gPovAlways ? "recorded at every camera update" : "recorded only once a screen is up", gUnheldStates);
    }
    return "usage: mevr worldui firstheld [on|off]; the status: " + StatusJson();
}

bool PuzzlePlaying() {
    return gPuzzleLogged;  // set each tick from the handler while the minigame's screen is up, cleared when it goes down
}

std::string StatusJson() {
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    return gStatusJson;
}

void Shutdown(Link const& link) {
    CancelOpening(link);
    Down(link, "the mod unloads");
    ui_world_figure::Shutdown();
}

} // namespace mevr::game::world_ui
