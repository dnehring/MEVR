// SPDX-License-Identifier: MIT
#include "plugin/ModLoader.hpp"
#include "plugin/Renderer.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "engine/GameAdapter.hpp"
#include "mod/ModApi.hpp"
#include "core/vr/CameraDiagnostic.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "plugin/PresentHook.hpp"
#include "plugin/ProcessEventHook.hpp"
#include "engine/SettingsFile.hpp"

#if defined(MEVR_WITH_HALCYON_LE3)
#include "plugin/HalcyonLe3.hpp"
#include "plugin/MotionBlurGuard.hpp"
#include "core/vr/PowerWheel.hpp"
#include "core/vr/Sights.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "plugin/Vignette.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/Floor.hpp"
#include "core/vr/HostView.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/PadMerge.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/XrHost.hpp"
#endif
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
#include "core/vr/AimRay.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/Floor.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/HudPieces.hpp"
#include "core/vr/InteractLabel.hpp"
#include "core/vr/PowerWheel.hpp"
#include "core/vr/SquadPlace.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/Sights.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/PadMerge.hpp"
#include "plugin/RenderLevers.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Vignette.hpp"
#include "plugin/XrHost.hpp"
#include "plugin/Voice.hpp"
#include "plugin/VoiceModel.hpp"
#endif
#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
#include "core/vr/Traversal.hpp"
#include "plugin/TraversalCue.hpp"
#endif
#if (defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)) && defined(MEVR_WITH_MAIN_MENU)
#include "core/vr/Calibration.hpp"
#include "plugin/Thespian.hpp"
#include "core/vr/WorldUi.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/GamePointer.hpp"
#include "plugin/MainMenu.hpp"
#include "plugin/WorldUi.hpp"
#endif
#if defined(MEVR_WITH_HALCYON)
#include "core/vr/Calibration.hpp"
#include "core/vr/Floor.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/HudPieces.hpp"
#include "core/vr/MakoControls.hpp"
#include "core/vr/InteractLabel.hpp"
#include "core/vr/Thespian.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/Thespian.hpp"
#endif
#include "core/vr/ScopeCapture.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/WorldUi.hpp"
#include "core/vr/Spectator.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/GamePointer.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/MotionBlurGuard.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Voice.hpp"
#include "plugin/WorldUi.hpp"
#include "plugin/VoiceModel.hpp"
#include "core/vr/Voice.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/MainMenu.hpp"
#endif
#endif

#include "core/vr/GalaxyHologram.hpp"

#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace mevr::host {
namespace {

std::atomic<bool> gProcessExiting{false};
// Why the current mod generation must stay loaded when it's unloaded, if it must.
std::string gKeepReason;

void KeepModLoaded(char const* reason) {
    log::Error("the mod's code must stay loaded: {}", reason != nullptr ? reason : "(no reason given)");
    if (gKeepReason.empty()) {
        gKeepReason = reason != nullptr ? reason : "unknown";
    }
}

#if defined(MEVR_MONOLITHIC)
constexpr auto* kHostLog = static_cast<void (*)(char const*, char const*)>(nullptr);  // one log already
#else
void HostLog(char const* level, char const* message) {
    log::Write(level, message);
}
constexpr auto* kHostLog = &HostLog;
#endif

// The mod's ProcessEvent callback. Clearing it waits for calls still inside it, except once the
// process is exiting; a call that doesn't leave in time keeps the mod loaded.
void HostSetProcessEventCallback(ProcessEventCallback callback) {
    bool const exiting = gProcessExiting.load();
    if (!process_event::SetCallback(callback, exiting ? 0 : process_event::kDrainWaitMs) && !exiting) {
        KeepModLoaded("a thread was still inside the mod's ProcessEvent callback");
    }
}
bool HostProcessExiting() {
    return gProcessExiting.load();
}
void HostKeepLoaded(char const* reason) {
    KeepModLoaded(reason);
}
// v18, every game: the crash breadcrumbs (plugin/CrashBreadcrumbs).
HostServices WithCrashBreadcrumbs(HostServices services) {
    services.recordWrite = &crash::RecordWrite;
    services.beginScriptCall = &crash::BeginScriptCall;
    services.endScriptCall = &crash::EndScriptCall;
    return services;
}

// The base every host gives (every game): its version, its log, the ProcessEvent callback, the exit flag, staying loaded.
HostServices BaseServices() {
    HostServices services;
    services.version = kHostServicesVersion;
    services.log = kHostLog;
    services.setProcessEventCallback = &HostSetProcessEventCallback;
    services.processExiting = &HostProcessExiting;
    services.keepLoaded = &HostKeepLoaded;
    services.servicesSize = static_cast<std::uint32_t>(sizeof(HostServices));
    return services;
}

#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
// What a host with Halcyon's renderer gives (docs/le2-continuation-plan.md M2.5): the controllers, the view
// each frame (core/vr/HostView, published by the renderer's bridge), hand aim and the aim ray, the snapshots' status, the
// controllers' vibration and the floor (plugin/XrHost). The renderer owns VR: a mod given `controllers` stands its own down.
void HostControllers(vr::controllers::State* out) {
    *out = vr::controllers::Latest(UINT64_MAX);  // raw sample: the mod ages it itself
}
void HostView(vr::host_view::State* out) {
    *out = vr::host_view::Latest(UINT64_MAX);  // raw sample: the mod ages it itself
}
void HostSetHandAim(bool on) {
    renderer::SetModHandAim(on);
}
void HostSetAimRay(vr::aim_ray::Ray const* ray) {
    vr::aim_ray::Publish(ray != nullptr ? *ray : vr::aim_ray::Ray{});
}
bool HostStatusWanted() {
    return testloop::ModStatusWanted();
}
void HostSetStatus(char const* json) {
    testloop::SetModStatus(json);
}
void HostHapticPulse(int hand, float seconds, float amplitude) {
    xr_host::RequestHaptic(hand, seconds, amplitude);
}
void HostHapticPulseHz(int hand, float seconds, float amplitude, float frequencyHz) {
    xr_host::RequestHaptic(hand, seconds, amplitude, frequencyHz);
}
void HostHapticContinuous(int hand, float amplitude, float frequencyHz, float seconds) {
    xr_host::RequestContinuousHaptic(hand, amplitude, frequencyHz, seconds);
}
void HostFloor(vr::floor::State* out) {
    xr_host::Floor(out);
}
HostServices WithRenderer(HostServices services) {
    services.controllers = &HostControllers;
    services.view = &HostView;
    services.setHandAim = &HostSetHandAim;
    services.controllerStateSize = static_cast<std::uint32_t>(sizeof(vr::controllers::State));
    services.viewStateSize = static_cast<std::uint32_t>(sizeof(vr::host_view::State));
    services.aimRaySize = static_cast<std::uint32_t>(sizeof(vr::aim_ray::Ray));
    services.setAimRay = &HostSetAimRay;
    services.statusWanted = &HostStatusWanted;
    services.setStatus = &HostSetStatus;
    services.hapticPulse = &HostHapticPulse;
    services.hapticPulseHz = &HostHapticPulseHz;
    services.hapticContinuous = &HostHapticContinuous;
    services.floorStateSize = static_cast<std::uint32_t>(sizeof(vr::floor::State));
    services.floor = &HostFloor;
    // The game's crosshair hidden while the hand reticle stands in for it (game/ScreenReticle; both renderers' layers ask).
    if (renderer::Capabilities().screenReticleHide) {
        services.hideScreenReticle = &mevr::host::renderer::ScreenReticleHidden;
    }
    services.secondEyeIsReplay = &render_levers::SecondEyeIsReplay;  // v43: false where sharePass stands down
    return services;
}
// v17: the comfort vignette's darkness (plugin/Vignette, a quad of ours on either renderer).
void HostSetVignette(float amount) {
    vignette::SetTarget(amount);
}
HostServices WithVignette(HostServices services) {
    services.setVignette = &HostSetVignette;
    return services;
}
void HostSetSight(vr::sights::Sight const* sight) {
    if (sight != nullptr) {
        vr::sights::Publish(*sight);
    } else {
        vr::sights::Reset();
    }
}
std::uint32_t HostSightView() {
    return vr::sights::LatestView();
}
HostServices WithSights(HostServices services) {
    services.sightSize = static_cast<std::uint32_t>(sizeof(vr::sights::Sight));
    services.setSight = &HostSetSight;
    services.sightView = &HostSightView;
    return services;
}
HostServices WithPowerWheel(HostServices services) {
    services.powerWheelSize = static_cast<std::uint32_t>(sizeof(vr::power_wheel::View));
    services.setPowerWheel = [](vr::power_wheel::View const* view) {
        vr::power_wheel::Publish(view != nullptr ? *view : vr::power_wheel::View{});
    };
    services.powerLockSize = static_cast<std::uint32_t>(sizeof(vr::power_wheel::LockView));
    services.setPowerLock = [](vr::power_wheel::LockView const* view) {
        vr::power_wheel::PublishLock(view != nullptr ? *view : vr::power_wheel::LockView{});
    };
    return services;
}
// The pad mapping with the mod's flags (plugin/PadMerge, both hosts): v7 the support grip and the flags it carries (the
// holsters', the squad's, comfort's ...), v9 the right grip's report, v11 the left grip's.
void HostSetSupportGrip(float distanceMeters, float acquireMeters, float releaseMeters, std::uint32_t flags) {
    pad_merge::SetSupportGrip(distanceMeters, acquireMeters, releaseMeters, flags);
}
bool HostSupportGripHeld() {
    return pad_merge::SupportGripHeld();
}
std::uint32_t HostRightGripFree() {
    return pad_merge::RightGripFree();
}
std::uint32_t HostLeftGripAway() {
    return pad_merge::LeftGripAway();
}
// v22: HUD pieces on the body (game/HudPieces; plugin/HudPieceLayers, research/41), where the renderer's HUD panel takes
// the gameplay HUD (renderer::Capabilities().hudPieces: not ME3's yet).
void HostSetHudPieces(vr::hud_pieces::Pieces const* pieces) {
    if (pieces != nullptr) {
        vr::hud_pieces::Publish(*pieces);
    } else {
        vr::hud_pieces::Reset();
    }
}
HostServices WithHudPieces(HostServices services) {
    if (!renderer::Capabilities().hudPieces) {
        return services;
    }
    services.hudPiecesSize = static_cast<std::uint32_t>(sizeof(vr::hud_pieces::Pieces));
    services.setHudPieces = &HostSetHudPieces;
    return services;
}
// v36: the pawn's floor for the floor lock (game/Player's sample; plugin/FloorLock on both hosts, research/58).
HostServices WithFloorLock(HostServices services) {
    services.floorLockSampleSize = static_cast<std::uint32_t>(sizeof(vr::floor_lock::PawnSample));
    services.setFloorLockSample = &floor_lock::SetPawn;
    return services;
}
HostServices WithPadMerge(HostServices services) {
    services.setSupportGrip = &HostSetSupportGrip;
    services.supportGripHeld = &HostSupportGripHeld;
    services.rightGripFree = &HostRightGripFree;
    services.leftGripAway = &HostLeftGripAway;
    return services;
}
// Forgetting what the mod set in the renderer's services: hand aim off (the renderer's own aim back), no aim ray, no
// vibration, the vignette faded out, the pad mapping without the mod's flags (no two-handed grip, no grip kept).
void ForgetRendererModState() {
    pad_merge::ForgetSupportGrip();
    floor_lock::SetPawn(nullptr);
    vr::hud_pieces::Reset();  // the weapon panel back on the HUD (the wrist piece needs no mod)
    vr::interact::Reset();
#if defined(MEVR_WITH_HALCYON_LE3)
    halcyon_le3::SetTraversalPresentation(nullptr);
#endif
    vr::sights::Reset();
    vignette::Forget();
    renderer::SetModHandAim(false);
    vr::aim_ray::Reset();
    xr_host::ForgetHaptics();
#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
    traversal_cue::Forget();
    vr::scope_capture::Reset();  // the scope's picture goes back to the eye copy (LE1's: ForgetLe1ModState)
#endif
}
#endif

#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
// v40: the shared traversal cue and blink on the sequel hosts.
HostServices WithTraversal(HostServices services) {
    services.traversalCueSize = static_cast<std::uint32_t>(sizeof(vr::traversal::Cue));
    services.setTraversal = &traversal_cue::Set;
    return services;
}
#endif

#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || (defined(MEVR_WITH_HALCYON_LE3) && defined(MEVR_WITH_MAIN_MENU))
void HostSetVoiceListen(int on) {
    host::voice::SetListen(on);
}
bool HostTakeVoiceLine(vr::voice::Line* out) {
    return host::voice::TakeLine(out);
}
void HostVoiceState(vr::voice::State* out) {
    host::voice::GetState(out);
}
void HostSetVoiceKeyterms(char const* terms) {
    host::voice::SetKeyterms(terms);
}
void HostVoiceCommand(char const* line) {
    host::voice::Command(line);
}
std::uint32_t HostAskVoiceModel(char const* prefix, char const* rest, char const* gbnf) {
    return host::voice_model::Ask(prefix, rest, gbnf);
}
bool HostTakeVoiceModelAnswer(std::uint32_t* id, char* text, std::uint32_t capacity, std::uint32_t* ms) {
    return host::voice_model::TakeAnswer(id, text, capacity, ms);
}
HostServices WithVoice(HostServices services) {
    services.voiceLineSize = static_cast<std::uint32_t>(sizeof(vr::voice::Line));  // v31: voice orders (research/54)
    services.voiceStateSize = static_cast<std::uint32_t>(sizeof(vr::voice::State));
    services.setVoiceListen = &HostSetVoiceListen;
    services.takeVoiceLine = &HostTakeVoiceLine;
    services.voiceState = &HostVoiceState;
    services.setVoiceKeyterms = &HostSetVoiceKeyterms;
    services.voiceCommand = &HostVoiceCommand;
    services.askVoiceModel = &HostAskVoiceModel;
    services.takeVoiceModelAnswer = &HostTakeVoiceModelAnswer;
    return services;
}
#endif

#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
// v26: the galaxy hologram (core/vr/GalaxyHologram, plugin/GalaxyHologram, research/51 section 8).
void HostSetGalaxyHologram(vr::galaxy_holo::Scene const* scene) {
    if (scene != nullptr) {
        vr::galaxy_holo::Publish(*scene);
    } else {
        vr::galaxy_holo::Forget();
    }
}
bool HostTakeGalaxyPick(std::uint32_t* id) {
    auto const pick = vr::galaxy_holo::TakePick();
    if (pick && id != nullptr) {
        *id = *pick;
    }
    return pick.has_value();
}
HostServices WithGalaxyHologram(HostServices services) {
    services.galaxyHologramSize = static_cast<std::uint32_t>(sizeof(vr::galaxy_holo::Scene));
    services.setGalaxyHologram = &HostSetGalaxyHologram;
    services.takeGalaxyPick = &HostTakeGalaxyPick;
    return services;
}
#endif

#if defined(MEVR_WITH_HALCYON)
void HostSetSquadMenu(vr::squad_menu::View const* view) {
    vr::squad_menu::Publish(view != nullptr ? *view : vr::squad_menu::View{});
}
void HostSetWheelPanel(vr::squad_menu::wheel::Panel const* panel) {
    if (panel != nullptr) {
        vr::squad_menu::wheel::PublishPanel(*panel);
    } else {
        vr::squad_menu::wheel::ResetPanel();
    }
}
void HostCalibration(vr::calibration::HostState* out) {
    calibration::HostStateOut(out);
}
std::uint32_t HostMainMenu() {
#if defined(MEVR_WITH_MAIN_MENU)
    return main_menu::ModFlags();
#else
    return 0;
#endif
}
// v19: our main menu's Game menu and Holster buttons, done by the mod on the game thread (game/GameMenuRequest).
std::uint32_t HostTakeGameMenuRequest() {
#if defined(MEVR_WITH_MAIN_MENU)
    return static_cast<std::uint32_t>(main_menu::TakeGameRequest());
#else
    return 0;
#endif
}
HostServices WithGameMenuRequest(HostServices services) {
    services.takeGameMenuRequest = &HostTakeGameMenuRequest;
    return services;
}
#endif
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
// v20: the scope's real zoomed render (game/ScopeCapture, plugin/ScopeCaptureSource, research/39), every game's host.
void HostSetScopeCapture(vr::scope_capture::Capture const* capture) {
    if (capture != nullptr) {
        vr::scope_capture::Publish(*capture);
    } else {
        vr::scope_capture::Reset();
    }
}
HostServices WithScopeCapture(HostServices services) {
    services.scopeCaptureSize = static_cast<std::uint32_t>(sizeof(vr::scope_capture::Capture));
    services.setScopeCapture = &HostSetScopeCapture;
    return services;
}
#endif
#if defined(MEVR_WITH_HALCYON)
void HostSetInteractLabel(vr::interact::Label const* label) {
    if (label != nullptr) {
        vr::interact::Publish(*label);
    } else {
        vr::interact::Reset();
    }
}
HostServices WithInteractLabel(HostServices services) {
    services.interactLabelSize = static_cast<std::uint32_t>(sizeof(vr::interact::Label));
    services.setInteractLabel = &HostSetInteractLabel;
    return services;
}
// v23: physical Mako controls (game/MakoControls, plugin/HalcyonOverlays, research/46).
void HostSetMakoControls(vr::mako_controls::View const* view) {
    if (view != nullptr) {
        vr::mako_controls::Publish(*view);
    } else {
        vr::mako_controls::Reset();
    }
}
HostServices WithMakoControls(HostServices services) {
    services.makoControlsSize = static_cast<std::uint32_t>(sizeof(vr::mako_controls::View));
    services.setMakoControls = &HostSetMakoControls;
    return services;
}
// v24: the spectator camera (game/Spectator, plugin/Spectator, research/49).
void HostSetSpectator(vr::spectator::Frame const* frame) {
    if (frame != nullptr) {
        vr::spectator::Publish(*frame);
    } else {
        vr::spectator::Reset();
    }
}
HostServices WithSpectator(HostServices services) {
    services.spectatorSize = static_cast<std::uint32_t>(sizeof(vr::spectator::Frame));
    services.setSpectator = &HostSetSpectator;
    return services;
}
// v25: the galaxy map as a hologram (game/GalaxyMap, research/51).
void HostSetGalaxyMap(vr::galaxy_map::View const* view) {
    if (view != nullptr) {
        vr::galaxy_map::Publish(*view);
    } else {
        vr::galaxy_map::Reset();
    }
}
// v27: the game's screens in the room (core/vr/WorldUi, plugin/WorldUi, research/53).
void HostSetWorldUi(vr::world_ui::State const* state) {
    world_ui::Publish(state);
}
std::uint32_t HostTakeGameUiRequest() {
#if defined(MEVR_WITH_MAIN_MENU)
    return main_menu::TakeGameRequestValue();
#else
    return 0;
#endif
}
HostServices WithWorldUi(HostServices services) {
    services.worldUiSize = static_cast<std::uint32_t>(sizeof(vr::world_ui::State));
    services.setWorldUi = &HostSetWorldUi;
    services.takeGameUiRequest = &HostTakeGameUiRequest;
    services.reserveWorldUi = &world_ui::Reserve;
    services.logWorldUiStats = &world_ui::LogStats;
    return services;
}
HostServices WithGalaxyMap(HostServices services) {
    services.galaxyMapSize = static_cast<std::uint32_t>(sizeof(vr::galaxy_map::View));
    services.setGalaxyMap = &HostSetGalaxyMap;
    return services;
}
std::uint32_t HostTakeGuiEvents(std::uint8_t* events, std::uint32_t capacity) {
    return host::game_pointer::TakeGuiEvents(events, capacity);
}
void HostSetConversationWorld(vr::conversation_world::State const* state) {
    if (state != nullptr && state->size == sizeof(vr::conversation_world::State)) {
        vr::conversation_world::Publish(*state);
    } else {
        vr::conversation_world::Reset();
    }
}
HostServices WithLe1ExtraUi(HostServices services) {
    services.publishCameraOffset = [](double forward, double right, double up) {
        vr::camera_diagnostic::gCameraDiagnostic.Publish(forward, right, up, GetTickCount64());
    };
    services.takeGuiEvents = &HostTakeGuiEvents;  // v30: the game pointer's input as GuiKey events (research/57 section 6)
    services.conversationWorldSize = static_cast<std::uint32_t>(sizeof(vr::conversation_world::State));  // v33 (research/59)
    services.setConversationWorld = &HostSetConversationWorld;
    services.motionBlurProtected = &motion_blur_guard::ProtectsGameplay;
#if defined(MEVR_WITH_MAIN_MENU)
    services.thespianPanelSize = static_cast<std::uint32_t>(sizeof(vr::thespian::Panel));  // v39: Thespian mode's panel (research/70)
    services.setThespian = &thespian::SetPanel;
#endif
    return services;
}
// LE1's host modules' own services (the squad menu and the wheel panel, the calibration, our main menu): LE1's host only
// until the modules come over to LE2 (M3, M4).
HostServices WithLe1Modules(HostServices services) {
    services.squadMenuSize = static_cast<std::uint32_t>(sizeof(vr::squad_menu::View));
    services.setSquadMenu = &HostSetSquadMenu;
    services.wheelPanelSize = static_cast<std::uint32_t>(sizeof(vr::squad_menu::wheel::Panel));
    services.setWheelPanel = &HostSetWheelPanel;
    services.calibrationStateSize = static_cast<std::uint32_t>(sizeof(vr::calibration::HostState));
    services.calibration = &HostCalibration;
    services.mainMenu = &HostMainMenu;
    return services;
}
// A mod that's gone can't aim: the squad menu and the wheel panel settings go, the pad mapping forgets the two-handed grip
// (the grip holding the gun, the squad menu's controls), the scope, prompt, HUD pieces, Mako controls, spectator and the
// game's screens go back to the renderer's own (ForgetRendererModState: Halcyon's head aim back, no reticle, no vibration).
void ForgetLe1ModState() {
    vr::camera_diagnostic::gCameraDiagnostic.Reset();
    vr::squad_menu::Reset();
    vr::squad_menu::wheel::ResetPanel();
    vr::scope_capture::Reset();  // the scope's picture goes back to the eye copy
    vr::interact::Reset();  // no prompt at the object without the mod
    vr::mako_controls::Reset();  // no lever or stick without the mod
    vr::spectator::Reset();      // the desktop window gets Halcyon's mirror back
    world_ui::Forget();         // the game's screens and pending ownership go back to Halcyon's panel
}
#endif

#if (defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)) && defined(MEVR_WITH_MAIN_MENU)
// LE2 (M4, research/le2-menus.md): our main menu and the guided calibration, as LE1's WithLe1Modules and
// WithGameMenuRequest give them: v15 the calibration's state, v16 the menu's state, v19 its Game menu and Holster buttons.
void HostCalibration(vr::calibration::HostState* out) {
    calibration::HostStateOut(out);
}
std::uint32_t HostMainMenu() {
    return main_menu::ModFlags();
}
std::uint32_t HostTakeGameMenuRequest() {
    return static_cast<std::uint32_t>(main_menu::TakeGameRequest());
}
#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
// M4.3 (research/le2-menus.md "ME2's screens on the wrist"; ME3: research/le3-native-gui.md "The game's screens on ME3"): the
// game's screens on the wrist, as LE1's WithWorldUi gives them (v27 setWorldUi, v34 reserveWorldUi, the rail's requests with
// their origin).
void HostSetWorldUi(vr::world_ui::State const* state) {
    world_ui::Publish(state);
}
std::uint32_t HostTakeGameUiRequest() {
    return main_menu::TakeGameRequestValue();
}
#endif
// The game pointer's input as the game's own GUI events (v30, research/57 section 6), on both sequels: the mod's
// game/GuiCursor translates the shared queue into each game's BioGuiEvents (ME3: research/le3-native-gui.md) and polls it
// only while its native GuiKey contract is ready, so a missing consumer keeps GamePointer's SendInput fallback.
std::uint32_t HostTakeGuiEvents(std::uint8_t* events, std::uint32_t capacity) {
    return game_pointer::TakeGuiEvents(events, capacity);
}
HostServices WithMenus(HostServices services) {
    services.calibrationStateSize = static_cast<std::uint32_t>(sizeof(vr::calibration::HostState));
    services.calibration = &HostCalibration;
    services.mainMenu = &HostMainMenu;
    services.takeGameMenuRequest = &HostTakeGameMenuRequest;
#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
    services.worldUiSize = static_cast<std::uint32_t>(sizeof(vr::world_ui::State));
    services.setWorldUi = &HostSetWorldUi;
    services.takeGameUiRequest = &HostTakeGameUiRequest;
    services.reserveWorldUi = &world_ui::Reserve;
    services.logWorldUiStats = &world_ui::LogStats;
#endif
    services.takeGuiEvents = &HostTakeGuiEvents;
    services.setGameMenuOffers = &main_menu::NoteGameMenuOffers;  // v44: whether the top bar's Leave would act (research/28)
    return services;
}
#endif

// One builder for every game's host (docs/le2-continuation-plan.md M2.5): the base, the crash breadcrumbs, the renderer's
// services where Halcyon's renderer (either game's) is in the host, and LE1's host modules' where they are linked. A module
// that comes over to LE2 moves its With* out of the LE1-only list.
HostServices BuildServices() {
    HostServices services = WithCrashBreadcrumbs(BaseServices());
    if (engine::DevelopmentEngineProbe()) return services;
#if defined(MEVR_WITH_HALCYON_LE3)
    // Shared layers now consume these publications. Native/headset acceptance remains separate.
    services = WithGalaxyHologram(services);
    services.motionBlurProtected = &motion_blur_guard::ProtectsGameplay;
    services = WithTraversal(WithFloorLock(WithHudPieces(WithPadMerge(WithSights(WithVignette(WithRenderer(services)))))));
#if defined(MEVR_WITH_MAIN_MENU)
    services = WithVoice(services);
#endif
    services.traversalPresentationSize = static_cast<std::uint32_t>(sizeof(vr::traversal::Presentation));
    services.setTraversalPresentation = &halcyon_le3::SetTraversalPresentation;
    services.thespianInputBlocked = &halcyon_le3::ThespianInputBlocked;
    // The scope's zoomed render (game/ScopeCapture): the mod's own scene capture, sampled by SightLayers; else the eye tap.
    services = WithScopeCapture(services);
#endif
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2)
    services = WithVoice(WithFloorLock(WithHudPieces(WithPadMerge(WithSights(WithVignette(WithRenderer(services)))))));  // both renderers' modules
#endif
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
    services.squadMenuSize = static_cast<std::uint32_t>(sizeof(vr::squad_menu::View));
    services.setSquadMenu = [](vr::squad_menu::View const* view) {
        vr::squad_menu::Publish(view != nullptr && view->size == sizeof(vr::squad_menu::View) ? *view : vr::squad_menu::View{});
    };
    services = WithPowerWheel(services);
    services.squadPlaceSize = static_cast<std::uint32_t>(sizeof(vr::squad_place::View));
    services.setSquadPlace = [](vr::squad_place::View const* view) {
        vr::squad_place::Publish(view != nullptr && view->size == sizeof(vr::squad_place::View) ? *view : vr::squad_place::View{});
    };
#endif
#if defined(MEVR_WITH_HALCYON_LE2)
    services = WithTraversal(services);
    services = WithGalaxyHologram(services);  // the galaxy map over the CIC's projector (game/GalaxyProjectorFlow, the shared host)
    services = WithScopeCapture(services);    // the scope's zoomed render (game/ScopeCapture), as LE1's and LE3's
#endif
#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
    services.interactLabelSize = static_cast<std::uint32_t>(sizeof(vr::interact::Label));
    services.setInteractLabel = [](vr::interact::Label const* label) {
        if (label != nullptr && label->size == sizeof(vr::interact::Label)) vr::interact::Publish(*label);
        else vr::interact::Reset();
    };
#endif
#if defined(MEVR_WITH_HALCYON)
    services = WithLe1ExtraUi(WithWorldUi(WithGalaxyHologram(WithGalaxyMap(WithSpectator(WithMakoControls(
        WithInteractLabel(WithScopeCapture(WithGameMenuRequest(WithLe1Modules(services))))))))));
#endif
#if (defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)) && defined(MEVR_WITH_MAIN_MENU)
    services = WithMenus(services);  // our main menu and the calibration on LE2 (M4)
    services.thespianPanelSize = static_cast<std::uint32_t>(sizeof(vr::thespian::Panel));
    services.setThespian = &thespian::SetPanel;
#endif
    return services;
}
HostServices const gServices = BuildServices();

void ForgetModState() {
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || (defined(MEVR_WITH_HALCYON_LE3) && defined(MEVR_WITH_MAIN_MENU))
    host::voice::SetListen(0); // also release a crashed/unloaded mod's microphone claim
#endif
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
    ForgetRendererModState();
    vr::power_wheel::Publish({});
    vr::power_wheel::PublishLock({});
    vr::squad_place::Publish({});
    vr::squad_menu::Reset();
#endif
#if defined(MEVR_WITH_HALCYON)
    ForgetLe1ModState();
#endif
#if defined(MEVR_WITH_HALCYON_LE3)
    vr::galaxy_holo::Forget();
#endif
#if (defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)) && defined(MEVR_WITH_MAIN_MENU)
    world_ui::Forget();  // ME2's screens and pending ownership go back to Halcyon's flat panel (M4.3)
    thespian::SetPanel(nullptr);
#endif
}
loader::spi::Interface* gSpi = nullptr;

} // namespace

void SetProcessExiting() {
    gProcessExiting.store(true);
}

void StopWorkersForUnload() {
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || (defined(MEVR_WITH_HALCYON_LE3) && defined(MEVR_WITH_MAIN_MENU))
    host::voice::Shutdown();  // its capture and worker threads, before the DLLs unload
    host::voice_model::Shutdown();
#endif
}

bool ProcessExiting() {
    return gProcessExiting.load();
}

#if defined(MEVR_MONOLITHIC)

namespace {
bool gModActive = false;
}

void Start(loader::spi::Interface* spi) {
    gSpi = spi;
    gModActive = MevrModStartup(gSpi, &gServices);
    if (gModActive) {
        present::SetCallback(&MevrModOnPresent);
    }
}

void Tick(float deltaSeconds) {
    if (gModActive) {
        MevrModTick(deltaSeconds);
    }
}

void Stop() {
    if (gModActive) {
        present::SetCallback(nullptr);
        MevrModShutdown();
        gModActive = false;
    }
    ForgetModState();
    gKeepReason.clear();  // nothing to unload: host and mod are one module
}

#else // hot reload

namespace {

namespace fs = std::filesystem;

constexpr double kPollSeconds = 0.5;

struct LoadedMod {
    HMODULE module = nullptr;
    fs::path shadowPath;
    MevrModTickFn tick = nullptr;
    MevrModShutdownFn shutdown = nullptr;
    MevrModPrepareUnloadFn prepareUnload = nullptr; // optional: old generations have no preflight
};

LoadedMod gMod;
fs::path gSourcePath;
fs::path gShadowDir;
FILETIME gLoadedStamp{};
FILETIME gPendingStamp{};
bool gHavePending = false;
double gPollTimer = 0.0;
unsigned gGeneration = 0;
bool gUnloadDeferred = false;

fs::path HostDirectory() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&HostDirectory), &self);
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(self, buffer, MAX_PATH);
    return fs::path(buffer).parent_path();
}

fs::path ResolveSourcePath() {
    fs::path const dir = HostDirectory();
    fs::path const pointer = dir / (MEVR_GAME_NAME_W L"MEVR.modpath");
    std::ifstream in(pointer);
    std::string line;
    if (in && std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            return fs::path(std::u8string(line.begin(), line.end()));
        }
    }
    return dir / (MEVR_GAME_NAME_W L"MEVR_mod.dll");
}

bool ReadStamp(FILETIME& out) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(gSourcePath.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    out = data.ftLastWriteTime;
    return true;
}

bool SameStamp(FILETIME const& a, FILETIME const& b) {
    return CompareFileTime(&a, &b) == 0;
}

// Keeps a module mapped until the process ends, whatever FreeLibrary calls follow.
void Pin(HMODULE module) {
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(module), &pinned)) {
        log::Warn("pinning the old mod DLL failed (error {}); it stays loaded anyway (never freed)", GetLastError());
    }
}

bool Unload() {
    if (gMod.module == nullptr) {
        return true;
    }
    // A refusal leaves all callbacks, state and ticks intact. Pinning after shutdown is
    // different: it keeps code mapped but cannot retry an owned game-state restoration.
    if (!gProcessExiting.load() && gMod.prepareUnload != nullptr && !gMod.prepareUnload()) {
        if (!gUnloadDeferred) log::Warn("mod reload deferred: current generation has pending native restoration");
        gUnloadDeferred = true;
        return false;
    }
    gUnloadDeferred = false;
    // Waits for the render thread to leave the mod's present callback.
    present::SetCallback(nullptr);
    if (gMod.shutdown != nullptr) {
        gMod.shutdown();
    }
    ForgetModState();
    // The mod clears its ProcessEvent callback in its shutdown; make sure (an older or half-started
    // mod), since nothing may call into the DLL once it's freed.
    HostSetProcessEventCallback(nullptr);
    if (gProcessExiting.load()) {
        // Windows unloads it with the process. Freeing it now could pull code out from under a
        // thread that's still in a hook and never gets to leave.
        log::Info("mod shut down for exit; its DLL stays mapped");
    } else if (!gKeepReason.empty()) {
        Pin(gMod.module);
        log::Error("mod generation {} STAYS LOADED (pinned, not unloaded): {}. Its hooks may still run its code; "
                   "restart the game when convenient", gGeneration, gKeepReason);
        // The shadow copy is locked while mapped; the next session's cleanup removes it.
    } else {
        FreeLibrary(gMod.module);
        std::error_code ec;
        fs::remove(gMod.shadowPath, ec);
    }
    gKeepReason.clear();
    gMod = {};
    return true;
}

enum class LoadResult { Loaded, CopyFailed, Failed };

// Copies the DLL so the original stays writable for the next build, then loads the copy.
LoadResult Load() {
    fs::path const shadow = gShadowDir /
        std::format(L"{}MEVR_mod.{}.dll", MEVR_GAME_NAME_W, ++gGeneration);
    if (!CopyFileW(gSourcePath.c_str(), shadow.c_str(), FALSE)) {
        return LoadResult::CopyFailed;  // usually the linker still writing; retry next poll
    }

    HMODULE const module = LoadLibraryW(shadow.c_str());
    if (module == nullptr) {
        log::Error("LoadLibrary failed for {} (error {})", ini::Utf8(shadow), GetLastError());
        return LoadResult::Failed;
    }
    auto const startup = reinterpret_cast<MevrModStartupFn>(GetProcAddress(module, "MevrModStartup"));
    auto const tick = reinterpret_cast<MevrModTickFn>(GetProcAddress(module, "MevrModTick"));
    auto const shutdown = reinterpret_cast<MevrModShutdownFn>(GetProcAddress(module, "MevrModShutdown"));
    auto const onPresent = reinterpret_cast<MevrModOnPresentFn>(GetProcAddress(module, "MevrModOnPresent"));
    auto const prepareUnload = reinterpret_cast<MevrModPrepareUnloadFn>(GetProcAddress(module, "MevrModPrepareUnload"));
    if (startup == nullptr || tick == nullptr || shutdown == nullptr || onPresent == nullptr) {
        log::Error("mod DLL is missing its entry points");
        FreeLibrary(module);
        return LoadResult::Failed;
    }
    if (!startup(gSpi, &gServices)) {
        log::Error("mod startup failed; waiting for a new build");
        FreeLibrary(module);
        return LoadResult::Failed;
    }
    gMod = {module, shadow, tick, shutdown, prepareUnload};
    present::SetCallback(onPresent);
    log::Info("mod generation {} loaded from {}", gGeneration, ini::Utf8(gSourcePath));
    return LoadResult::Loaded;
}

void CleanShadowDirectory() {
    std::error_code ec;
    fs::create_directories(gShadowDir, ec);
    for (auto const& entry : fs::directory_iterator(gShadowDir, ec)) {
        fs::remove(entry.path(), ec);  // leftovers from earlier sessions; locked ones are skipped
    }
}

void CheckForNewBuild() {
    FILETIME stamp{};
    if (!ReadStamp(stamp) || SameStamp(stamp, gLoadedStamp)) {
        gHavePending = false;
        return;
    }
    // Wait until the file has been unchanged for one poll so we don't copy a half-written DLL.
    if (!gHavePending || !SameStamp(stamp, gPendingStamp)) {
        gPendingStamp = stamp;
        gHavePending = true;
        return;
    }
    if (!Unload()) return; // keep the source stamp pending; the existing poll retries
    log::Info("new mod build detected; reloading");
    LoadResult const result = Load();
    if (result == LoadResult::CopyFailed) {
        return;  // stamp not recorded, so the next poll retries
    }
    gLoadedStamp = stamp;
    gHavePending = false;
}

} // namespace

void Start(loader::spi::Interface* spi) {
    gSpi = spi;
    gSourcePath = ResolveSourcePath();

    wchar_t localAppData[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    gShadowDir = fs::path(localAppData) / L"MEVR" / L"shadow";
    CleanShadowDirectory();

    log::Info("hot reload: watching {}", ini::Utf8(gSourcePath));
    FILETIME stamp{};
    if (!ReadStamp(stamp)) {
        log::Warn("mod DLL not found yet; it will load when it appears");
        return;
    }
    if (Load() != LoadResult::CopyFailed) {
        gLoadedStamp = stamp;
    }
}

void Tick(float deltaSeconds) {
    gPollTimer += deltaSeconds;
    if (gPollTimer >= kPollSeconds) {
        gPollTimer = 0.0;
        CheckForNewBuild();
    }
    if (gMod.tick != nullptr) {
        gMod.tick(deltaSeconds);
    }
}

void Stop() {
    Unload();
}

#endif

} // namespace mevr::host
