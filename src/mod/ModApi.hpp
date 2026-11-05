// SPDX-License-Identifier: MIT
#pragma once

// ABI between the host ASI (stable, loaded by LEBinkProxy) and the mod, which is either linked
// into the host (monolithic release build) or a separate DLL the host hot-reloads (dev build).

#include <cstdint>

struct IDXGISwapChain;

// Since research/34 stage 4 the engine and loader pointers here are our own types (ue3::Object and ue3::Function,
// loader::spi::Interface) instead of LExSDKv2's UObject, UFunction and ISharedProxyInterface: the same pointers,
// passed the same way, so the ABI (and kHostServicesVersion) didn't change.
namespace mevr::loader::spi {
class Interface;
}
namespace mevr::ue3 {
struct Object;
struct Function;
}

namespace mevr {

namespace vr::calibration {
struct HostState;
}
namespace vr::controllers {
struct State;
}
namespace vr::host_view {
struct State;
}
namespace vr::aim_ray {
struct Ray;
}
namespace vr::floor {
struct State;
}
namespace vr::sights {
struct Sight;
}
namespace vr::scope_capture {
struct Capture;
}
namespace vr::interact {
struct Label;
}
namespace vr::hud_pieces {
struct Pieces;
}
namespace vr::mako_controls {
struct View;
}
namespace vr::spectator {
struct Frame;
}
namespace vr::galaxy_map {
struct View;
}
namespace vr::galaxy_holo {
struct Scene;
}
namespace vr::world_ui {
struct State;
}
namespace vr::power_wheel {
struct View;
}
namespace vr::squad_menu {
struct View;
namespace wheel {
struct Panel;
}
}

// Fields are only ever appended: a mod reads the fields of every version up to the host's (and
// its own), so a newer mod still runs on an older host, without what the older host lacks.
//
// Version 3 is ambiguous: two branches each shipped a v3 host with a different layout after
// `controllers`. The controller-aim branch had {view, setHandAim}; the review-fix branch (commit
// f95d01d) had {u32 controllerStateSize, setProcessEventCallback}. Version 4 is their merge. So a
// mod reads nothing past `controllers` from a v3 host: `view` and `setHandAim` only from v4 on.
inline constexpr int kHostServicesVersion = 28;

// HostServices::mainMenu's bits (v16).
inline constexpr std::uint32_t kMainMenuOpen = 1u;   // our main menu is open
inline constexpr std::uint32_t kMainMenuPause = 2u;  // and the player wants the game paused while it is
// The omni-tool's rail is shown (research/56, 2026-11-04; no version change: an older mod ignores the bit, an older host never
// sets it). Set whether or not the main menu is open; the mod puts the game's omni-tool on Shepard's arm for it.
inline constexpr std::uint32_t kOmniRailOpen = 4u;

using ProcessEventCallback = void (*)(ue3::Object* context, ue3::Function* function, void* parms, void* result);

struct HostServices {
    int version = kHostServicesVersion;
    // Null in the monolithic build, where host and mod share one log.
    void (*log)(char const* level, char const* message) = nullptr;
    // Non-null when the host runs the VR renderer itself (LE1: Halcyon's): the mod then does no
    // VR output, head tracking or XInput hooking of its own. Returns the latest motion controller
    // sample, published by the host's frame loop.
    void (*controllers)(vr::controllers::State* out) = nullptr;

    // Version 4: from here through setProcessEventCallback. (The two v3 layouts each had some of
    // these, not all at these offsets: from a v2 or v3 host the mod reads none of them and runs
    // without them.)
    // The host renderer's latest view, relative to the game camera (core/vr/HostView.hpp): what
    // maps the controllers into the world. Non-null with `controllers`.
    void (*view)(vr::host_view::State* out) = nullptr;
    // The mod aims weapons with the right controller (game/HandAim) or has stopped. While it
    // does and the right controller is tracked, the host's renderer stops aiming with the head
    // (Halcyon's on-foot combat head aim). The host forgets it when it unloads the mod.
    void (*setHandAim)(bool on) = nullptr;

    // sizeof(vr::controllers::State) and sizeof(vr::host_view::State) as the host was built: a
    // mod rebuilt against a changed State must not read the host's (hot reload only replaces the
    // mod).
    std::uint32_t controllerStateSize = 0;
    std::uint32_t viewStateSize = 0;
    // The host's UObject::ProcessEvent hook (installed at attach, never removed) runs this after
    // the original. Clearing it waits (bounded) for in-flight calls; if one doesn't leave in time,
    // the host keeps the mod DLL loaded. No wait once the process is exiting.
    void (*setProcessEventCallback)(ProcessEventCallback callback) = nullptr;

    // Version 5.
    // True once the process has begun exiting (the host's ExitProcess hook): shutdown must not
    // wait for other threads, which may never run again. The host won't unload the DLL then.
    bool (*processExiting)() = nullptr;
    // The mod's shutdown couldn't make unloading safe: a hook it couldn't remove, or a call still
    // inside one when it gave up waiting. The host then pins the DLL instead of freeing it.
    void (*keepLoaded)(char const* reason) = nullptr;

    // Version 6.
    // The weapon's aim ray (core/vr/AimRay.hpp), each tick: the host's renderer draws the reticle
    // along it. sizeof(vr::aim_ray::Ray) as the host was built; the mod only calls setAimRay when
    // it matches. Non-null with `controllers`.
    std::uint32_t aimRaySize = 0;
    void (*setAimRay)(vr::aim_ray::Ray const* ray) = nullptr;

    // Version 7.
    // The two-handed grip (game/support_grip), each tick: the left hand's distance from the held
    // gun's foregrip (meters; negative: no gun to hold), the acquire and release distances, and
    // gamepad::Options flags (kFlagLeftTrigger: the left trigger is the game's LT; kFlagSupportAims:
    // the grip holds LT). The host's pad mapping (gamepad::UpdateSupportGrip) takes hold on a new
    // left-grip squeeze within `acquire`: LT instead of LB, until the grip opens or the hand leaves
    // `release`. Forgotten (no grip, LT from the trigger) when not called for 250 ms. Non-null with
    // `controllers`.
    void (*setSupportGrip)(float distanceMeters, float acquireMeters, float releaseMeters, std::uint32_t flags) = nullptr;
    // Whether the pad mapping holds the support grip now.
    bool (*supportGripHeld)() = nullptr;

    // Version 8.
    // The test loop (plugin/TestLoop): while statusWanted() is true (a snapshot is being taken), the
    // mod passes a JSON object with its state (arm IK, hand aim, the weapon, the support grip) each
    // tick; the snapshot's JSON carries it as "mod". Non-null with `controllers`.
    bool (*statusWanted)() = nullptr;
    void (*setStatus)(char const* json) = nullptr;

    // Version 9: the body holsters (game/holsters, research/19).
    // A vibration on one controller (0 left, 1 right) for `seconds` at `amplitude` (0..1), applied
    // on the host's next frame (a newer request for the same hand replaces a pending one). Any
    // thread. Non-null with `controllers`.
    void (*hapticPulse)(int hand, float seconds, float amplitude) = nullptr;
    // The pad mapping's right grip (setSupportGrip's gamepad::kFlagRightGripFree): the number of
    // squeezes it kept from the game (no RB, because they began at a holster) shifted up one bit, and
    // in bit 0 whether the current squeeze is one (gamepad::RightGripReport). Squeezes while Halcyon's
    // menu is open never count. Non-null with `controllers`.
    std::uint32_t (*rightGripFree)() = nullptr;

    // Version 10: the grip holds the gun, contact haptics, the floor (research/19).
    // sizeof(HostServices) as the host was built: the mod reads a v10+ field only if the host's struct
    // reaches past it (HostHas in mod/Mod.cpp), so a later host that appends more stays readable and
    // an earlier v10 build can't be over-read.
    std::uint32_t servicesSize = 0;
    // setSupportGrip's flags also take gamepad::kFlagRightGripHolds and the RB source
    // (gamepad::kRbSourceMask): a host older than v10 ignores them, so the mod mustn't make the right
    // grip "hold the gun" there (RB would stay on it).
    // A pulse at a frequency (Hz; 0 = the runtime's choice), like hapticPulse.
    void (*hapticPulseHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr;
    // A continuous vibration at `amplitude` (0 stops it) and `frequencyHz`, for `seconds` from now
    // (at most 0.5): the mod asks again each tick to keep it going, so it stops by itself within that
    // if the mod stalls or unloads. Pulses interrupt it and it resumes after them (core/vr/Haptics).
    void (*hapticContinuous)(int hand, float amplitude, float frequencyHz, float seconds) = nullptr;
    // The head's height above the floor (core/vr/Floor.hpp), the latest frame's; the mod reads it only
    // if floorStateSize matches its own sizeof(vr::floor::State).
    std::uint32_t floorStateSize = 0;
    void (*floor)(vr::floor::State* out) = nullptr;

    // Version 11: the squad weapon menu (game/squad_weapons, docs/squad-weapons-design.md).
    // setSupportGrip's flags also take gamepad::kFlagSquadMenu (a v10 host ignores it): while set, the
    // sticks, A, B and the stick clicks are the menu's, and each stays kept from the game until released.
    // The menu to draw (core/vr/SquadMenu.hpp), each tick while it's open and once when it closes; the
    // host draws it at the left hand while it's fresh (250 ms). The mod calls it only if squadMenuSize
    // matches its own sizeof(vr::squad_menu::View).
    std::uint32_t squadMenuSize = 0;
    void (*setSquadMenu)(vr::squad_menu::View const* view) = nullptr;
    // The pad mapping's left grip (gamepad::LeftGripReport): the count of squeezes that began away from
    // the gun's foregrip (not the support grip) shifted up one bit, and in bit 0 whether the current
    // squeeze is one. Squeezes while Halcyon's menu is open aren't counted.
    std::uint32_t (*leftGripAway)() = nullptr;

    // Version 12: the game's own weapon wheel at the hand ([Squad] style = native,
    // docs/squad-weapons-design.md "Native wheel").
    // setSupportGrip's flags also take gamepad::kFlagNativeWheel, kFlagWheelPressA and the wheel stick
    // (kWheelStickMask): while the flag is set the left grip holds the game's wheel open, one stick steers
    // it, A queues (plus the mod's own A), the rest is kept from the game until released.
    // The wheel panel's settings (core/vr/SquadMenu.hpp, squad_menu::wheel::Panel), each tick: while the
    // game's weapon or power wheel is up, the host shows its part of the UI on a panel of its own at a hand
    // (cut out of the HUD panel). The mod calls it only if wheelPanelSize matches its sizeof(Panel).
    std::uint32_t wheelPanelSize = 0;
    void (*setWheelPanel)(vr::squad_menu::wheel::Panel const* panel) = nullptr;

    // Version 13: gun sights (core/vr/Sights.hpp, game/sights, research/21).
    // The sight on the held gun, each tick (kind None: no sight): the host draws a red dot or holographic
    // reticle to the eye looking through it, or a scope's magnified picture at its eyepiece. The mod calls
    // it only if sightSize matches its own sizeof(vr::sights::Sight).
    std::uint32_t sightSize = 0;
    void (*setSight)(vr::sights::Sight const* sight) = nullptr;
    // What the host showed in its latest frame (vr::sights::View bits: which eye looks through the sight,
    // whether a scope's picture is up); 0 when nothing or stale. The mod's aimed fire for scoped shots.
    std::uint32_t (*sightView)() = nullptr;

    // Version 14: physical melee and grenades (game/throwables, research/20). No new fields:
    // setSupportGrip's flags also take gamepad::kFlagNoMeleeButton and kFlagNoGrenadeButton (B and X
    // kept from the game on foot); a v13 or older host ignores them, so the buttons stay.

    // Version 15: the guided calibration (plugin/Calibration, game/body_calibration, research/25).
    // The host's state (core/vr/Calibration.hpp, vr::calibration::HostState), each tick: kCalibrating while the
    // sequence runs (the mod stands down: no holsters, grenades, melee, squad trigger, support grip or physical
    // crouch; the host also publishes the controllers hidden then, so an older mod stands down too), the current
    // controller type's [Calibration.<key>] section key, and a count of saves (re-read [Calibration] when it
    // changes). The mod calls it only if calibrationStateSize matches its own sizeof(HostState).
    std::uint32_t calibrationStateSize = 0;
    void (*calibration)(vr::calibration::HostState* out) = nullptr;

    // Version 16: our VR main menu (plugin/MainMenu, research/28). kMainMenuOpen while it's open (the host also
    // publishes the controllers hidden then, as during the calibration, so an older mod stands down too), plus
    // kMainMenuPause when the player wants the game paused meanwhile ([Menu] pause): the mod pauses it the way the
    // game's own weapon wheel does (WorldInfo.PauseGame), on foot or in the Mako, and unpauses it when the menu closes.
    // Any thread.
    std::uint32_t (*mainMenu)() = nullptr;

    // Version 17: comfort (game/comfort, plugin/Vignette, research/31).
    // setSupportGrip's flags also take gamepad::kFlagModTurns (the game sees no right-stick X from the motion
    // controllers: the mod turns the player itself, snap or smooth) and gamepad::kFlagHandWalk (the motion controllers'
    // left stick is turned so its forward is the left controller's yaw). A v16 or older host ignores them, so the mod
    // leaves turning to the game there.
    // The comfort vignette, each tick: how dark the view's edges should be now (0 none .. 1 black); the host fades
    // toward it over 0.15 s and draws it as a head-locked layer. Forgotten (faded out) when not called for 250 ms.
    // Any thread.
    void (*setVignette)(float amount) = nullptr;

    // Version 18: crash breadcrumbs (plugin/CrashBreadcrumbs, research/36). Any thread, a few stores each; the host
    // writes them out (with its last ProcessEvent dispatches) if the process crashes.
    // After the mod writes engine memory: a tag (a string literal: its text is copied, its pointer only compared), the
    // address, how many bytes, and the first 8 of them (core/WriteJournal.hpp). Repeats of a recent tag and address fold
    // into one entry.
    void (*recordWrite)(char const* tag, void const* address, std::uint32_t size, std::uint64_t value) = nullptr;
    // Around each of the mod's CallFunction post-handlers (engine/CallFunction): begin returns the token for end.
    std::uint64_t (*beginScriptCall)(ue3::Function* function, ue3::Object* object) = nullptr;
    void (*endScriptCall)(std::uint64_t token) = nullptr;

    // Version 19: our main menu's Game menu and Holster buttons (research/28 "The game's menus", game/GameMenuRequest).
    // The request the menu's last button press left (vr::game_menu::Action: 0 none, 1 the game's pause menu, 2 holster),
    // cleared by the call; the mod polls it each game tick and calls the game's own function on the game thread (the
    // active game mode's ShowMenuGui, SFXGameModeDefault.TryHolster) once the menu's pause is lifted. Any thread. A v18
    // or older host has no such buttons' request to give (its menu pressed Start or Back on the pad itself).
    std::uint32_t (*takeGameMenuRequest)() = nullptr;

    // Version 20: the scope's real zoomed render (game/ScopeCapture, plugin/ScopeCaptureSource, research/39).
    // The mod's scene capture, each tick (core/vr/ScopeCapture.hpp, vr::scope_capture::Capture: its render target's
    // FTextureResource, size, format and the pose it renders from): while it's kCapturing and fresh, the host's scope
    // picture samples it instead of the other eye's copy. The mod calls it only if scopeCaptureSize matches its own
    // sizeof(Capture); with a v19 or older host it never spawns a capture. Any thread.
    std::uint32_t scopeCaptureSize = 0;
    void (*setScopeCapture)(vr::scope_capture::Capture const* capture) = nullptr;

    // Version 21: the interaction prompt at the object (game/Interact, plugin/HalcyonOverlays, research/40).
    // The mod's label each tick (core/vr/InteractLabel.hpp, vr::interact::Label: whether to show it, where the selected
    // object is in tracking space, its size, the HUD band by hand): while it's visible and fresh the host cuts the
    // game's target status widget out of the HUD panel and shows it at the object. The mod calls it only if
    // interactLabelSize matches its own sizeof(Label). Any thread.
    std::uint32_t interactLabelSize = 0;
    void (*setInteractLabel)(vr::interact::Label const* label) = nullptr;

    // Version 22: HUD pieces on the body (game/HudPieces, plugin/HalcyonOverlays, research/41). The mod's pieces each
    // tick (core/vr/HudPieces.hpp, vr::hud_pieces::Pieces: whether a gun is drawn on foot and where its weapon panel
    // goes in tracking space): while fresh and visible the host cuts the game's weapon panel out of the HUD panel and
    // shows it on the gun. The wrist piece (the player's status at the left grip) needs nothing from the mod. The mod
    // calls it only if hudPiecesSize matches its own sizeof(Pieces). Any thread.
    std::uint32_t hudPiecesSize = 0;
    void (*setHudPieces)(vr::hud_pieces::Pieces const* pieces) = nullptr;

    // Version 23: physical Mako controls (game/MakoControls, plugin/HalcyonOverlays, research/46). The lever and the stick
    // to draw, each tick (core/vr/MakoControls.hpp, vr::mako_controls::View: where they sit in tracking space, the lever's
    // position, the steering, a hand in reach or holding); null forgets them. setSupportGrip's flags also take
    // gamepad::kFlagMakoDrive, kFlagMakoLever and kFlagMakoStick; a v22 or older host ignores them, so the mod stands the
    // controls down there. The mod publishes only if makoControlsSize matches its own sizeof(View). Any thread.
    std::uint32_t makoControlsSize = 0;
    void (*setMakoControls)(vr::mako_controls::View const* view) = nullptr;

    // Version 24: the spectator camera (game/Spectator, plugin/Spectator, research/49). The mod's scene capture each tick
    // (core/vr/Spectator.hpp, vr::spectator::Frame: its render target's FTextureResource, size, format, the camera's pose and
    // field): while it's kCapturing and fresh, the host composites the frame's quad layers over it and shows it in the
    // desktop window instead of Halcyon's mirror. Null forgets it. The mod publishes only if spectatorSize matches its own
    // sizeof(Frame); with a v23 or older host it never spawns the capture. Any thread.
    std::uint32_t spectatorSize = 0;
    void (*setSpectator)(vr::spectator::Frame const* frame) = nullptr;

    // Version 25: the galaxy map as a hologram (game/GalaxyMap, research/51). While the game is in its galaxy mode the mod
    // publishes the units per metre that show the map's centre `distanceM` ahead (core/vr/GalaxyMap.hpp, vr::galaxy_map::View);
    // while it's active and fresh, Halcyon's galaxy branch renders the map in stereo at that scale instead of on its flat
    // panel, and the game pointer points the game's mouse at it. Null forgets it. The mod publishes only if galaxyMapSize
    // matches its sizeof(View). Any thread.
    std::uint32_t galaxyMapSize = 0;
    void (*setGalaxyMap)(vr::galaxy_map::View const* view) = nullptr;

    // Version 26: the galaxy map as a hologram we draw (research/51 section 8; core/vr/GalaxyHologram). The mod sends the open
    // map's level each tick (its selectable objects, the selection, the game camera, the table's settings); the host draws it on
    // a table in front of the player and posts a pick (an object's id) when a controller points at one and its trigger goes
    // down; the mod takes it (true and the id) and makes the game select and open that object. Null forgets the scene. The mod
    // sends only if galaxyHologramSize matches its sizeof(Scene). Any thread.
    std::uint32_t galaxyHologramSize = 0;
    void (*setGalaxyHologram)(vr::galaxy_holo::Scene const* scene) = nullptr;
    bool (*takeGalaxyPick)(std::uint32_t* id) = nullptr;

    // Version 27: the game's full-screen screens in the room (game/WorldUi, research/53; core/vr/WorldUi). While one of ours
    // is up in the game's GUI mode (the shop, the inventory...), the mod sends which one, what it belongs to (the vendor's
    // shop actor, in tracking space) and the [WorldUi] settings each tick; while it's active and fresh, Halcyon renders the
    // world in stereo as on foot and the host shows the game's UI on a panel standing in the room, the game pointer on it.
    // Null forgets it. The mod sends only if worldUiSize matches its sizeof(State). Any thread.
    std::uint32_t worldUiSize = 0;
    void (*setWorldUi)(vr::world_ui::State const* state) = nullptr;

    // Version 28: the power wheel at the left hand (game/PowerWheel, plugin/PowerWheel, research/55; core/vr/PowerWheel). While
    // it's open the mod sends what to draw each tick (the player's powers, the pick, where it stands in tracking space, the
    // hand's offset) and once with open = 0 when it closes; the host draws it while it's open and fresh (250 ms). Null forgets
    // it. setSupportGrip's flags also take gamepad::kFlagPowerWheel (the left hand's controls kept from the game while it's
    // open); a v27 or older host ignores it, and the mod then keeps the wheel closed. The mod sends only if powerWheelSize
    // matches its sizeof(View). Any thread.
    std::uint32_t powerWheelSize = 0;
    void (*setPowerWheel)(vr::power_wheel::View const* view) = nullptr;
};

} // namespace mevr

#if defined(MEVR_MOD_DLL)
#define MEVR_MOD_API extern "C" __declspec(dllexport)
#else
#define MEVR_MOD_API extern "C"
#endif

// Installs the mod's hooks, through the loader's SPI interface (null: the mod's own MinHook; core/Hook). Returning
// false means the mod stays inactive.
MEVR_MOD_API bool MevrModStartup(mevr::loader::spi::Interface* spi, mevr::HostServices const* host);
// Called by the host every UGameEngine::Tick, after the engine's own tick.
MEVR_MOD_API void MevrModTick(float deltaSeconds);
// Render thread, before each IDXGISwapChain::Present. The host guarantees this isn't running
// when MevrModShutdown is called.
MEVR_MOD_API void MevrModOnPresent(IDXGISwapChain* swapChain);
// Removes every hook the mod installed and releases its XR/D3D resources. The host calls this
// before unloading the DLL; whatever couldn't be made safe to unload is reported through
// HostServices::keepLoaded, and the host then keeps the DLL loaded.
MEVR_MOD_API void MevrModShutdown();

using MevrModStartupFn = decltype(&MevrModStartup);
using MevrModTickFn = decltype(&MevrModTick);
using MevrModOnPresentFn = decltype(&MevrModOnPresent);
using MevrModShutdownFn = decltype(&MevrModShutdown);
