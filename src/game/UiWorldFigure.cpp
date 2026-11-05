// SPDX-License-Identifier: MIT
// The squad member on the equipment, XMods and squad screens: the game's side (game/UiWorldFigure.hpp, research/57 section 7).
#include "game/UiWorldFigure.hpp"

#include "core/Exe.hpp"
#include "core/Log.hpp"
#include "engine/Addresses.hpp"
#include "engine/Engine.hpp"
#include "game/Player.hpp"
#include "game/SceneCaptureRig.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <string>

namespace mevr::game::ui_world_figure {

namespace {

namespace wu = vr::world_ui;

#if defined(MEVR_GAME_LE1)

// Their rows in engine/ReflectCheck.
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gLocation{"Engine.Actor", "Location"};
ue3::FieldRef gRotation{"Engine.Actor", "Rotation"};
ue3::FieldRef gUiWorld{"SFXGame.BioWorldInfo", "m_UIWorld"};
ue3::FieldRef gUiWorldPointer{"SFXGame.BioUIWorld", "m_pWorld"};        // FPointer: the UWorld
ue3::FieldRef gUiCamera{"SFXGame.BioUIWorld", "m_pCameraActor"};
ue3::FieldRef gUiChain{"SFXGame.BioUIWorld", "m_PostProcessChain"};
ue3::FieldRef gUiLoaded{"SFXGame.BioUIWorld", "m_bLoaded"};
ue3::FieldRef gUiEnabled{"SFXGame.BioUIWorld", "m_bEnabled"};
ue3::ClassRef gCameraActorClass{"Engine.CameraActor"};
ue3::FieldRef gCameraFov{"Engine.CameraActor", "FOVAngle"};
ue3::FieldRef gCameraAspect{"Engine.CameraActor", "AspectRatio"};
ue3::FieldRef gTimeSeconds{"Engine.WorldInfo", "TimeSeconds"};

// The UI world runs only while the game shows it (a full-screen panel up): its native command queue (the screen's SpawnPawn,
// TriggerEvent, UpdateAppearance... are queued, 0xB3B500), its Kismet (the camera's moves between the squad members' stands) and
// its actors' animation. With the panel no longer full screen the game stops ticking it, so the squad member was never spawned
// and the camera stayed where the last screen left it (2026-11-05). We tick it ourselves, with its own native Tick (the object's
// vtable slot 104, checked to hold 0xB3B810: the queue, then UWorld::Tick of the UI world with GWorld swapped), unless its clock
// moved without us (the game ticks it itself).
constexpr int kUiWorldTickSlot = 104;
using tUiWorldTick = void(__fastcall*)(void* self, float deltaSeconds);
struct UiTick {
    std::optional<std::chrono::steady_clock::time_point> last;
    std::optional<float> expected;  // the UI world's clock as we left it
    std::uint64_t ours = 0;         // ticks we gave it
    std::uint64_t games = 0;        // ticks the game gave it (its clock moved without us)
    bool checked = false;
    tUiWorldTick fn = nullptr;
};
UiTick gTick;

// The UI world's own Tick, if its vtable's slot is the function we know; else null (logged once).
tUiWorldTick UiWorldTickFn(ue3::Object* uiWorld) {
    void* const known = exe::Resolve(engine::addresses::kUiWorldTick);
    void* const* const vtable = uiWorld != nullptr ? *reinterpret_cast<void* const* const*>(uiWorld) : nullptr;
    void* const slot = vtable != nullptr ? vtable[kUiWorldTickSlot] : nullptr;
    if (!gTick.checked) {
        gTick.checked = true;
        if (known == nullptr || slot != known) {
            log::Warn("ui world figure: the UI world's vtable slot {} ({}) isn't its Tick ({}): it isn't ticked, the squad member may be "
                      "missing", kUiWorldTickSlot, slot, known);
        }
    }
    return known != nullptr && slot == known ? reinterpret_cast<tUiWorldTick>(known) : nullptr;
}

// One tick of the UI world while a screen of ours shows its squad member.
void TickUiWorld(ue3::Object* uiWorld, ue3::Object* camera) {
    auto const now = std::chrono::steady_clock::now();
    float const dt = gTick.last ? std::clamp(std::chrono::duration<float>(now - *gTick.last).count(), 1.0f / 240.0f, 0.1f) : 1.0f / 90.0f;
    gTick.last = now;
    ue3::Object* const info = gWorldInfo.Read<ue3::Object*>(camera).value_or(nullptr);  // the UI world's own WorldInfo
    std::optional<float> const clock = info != nullptr ? gTimeSeconds.Read<float>(info) : std::nullopt;
    if (gTick.expected && clock && *clock != *gTick.expected) {
        if (gTick.games++ == 0) {
            log::Info("ui world figure: the game ticks the UI world itself (its clock moved without us): ours stands down");
        }
        gTick.expected = clock;
        return;
    }
    tUiWorldTick const fn = UiWorldTickFn(uiWorld);
    if (fn == nullptr) {
        return;
    }
    fn(uiWorld, dt);
    if (gTick.ours++ == 0) {
        log::Info("ui world figure: we tick the UI world (its command queue, Kismet and animation) while our screen keeps the level");
    }
    gTick.expected = info != nullptr ? gTimeSeconds.Read<float>(info) : std::nullopt;
}

// The stage's shape (the UI world renders letterboxed to the movies' 16:9 stage, research/53 section 1), wide enough for the
// wrist screen's 1172-pixel panel image.
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr std::int32_t kPixelFormatA8R8G8B8 = 2;

#endif

struct Camera {
    ue3::Object* uiWorld = nullptr;
    std::uint64_t world = 0;      // the UI world's UWorld
    ue3::Object* actor = nullptr; // its camera actor
    ue3::Object* chain = nullptr; // its post-process chain
    bool loaded = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 0.0f;
    float aspect = 0.0f;
};

struct Status {
    bool wanted = false;
    std::string screen;
    std::string failure;       // why the figure isn't there (empty: it is)
    std::string loggedFailure; // the last one logged
    std::uint64_t ticks = 0;   // ticks the capture looked through the UI world's camera
    std::uint64_t uiTicks = 0;      // ticks we gave the UI world
    std::uint64_t gameUiTicks = 0;  // ticks the game gave it meanwhile (ours stood down)
    Camera camera;
    bool loggedUp = false;
};

#if defined(MEVR_GAME_LE1)
capture_rig::Rig gRig;
#endif
Status gS;
std::mutex gStatusMutex;
std::string gStatusJson = "{\"wanted\": false}";

void Publish() {
    std::string json = std::format(
        "{{\"wanted\": {}, \"screen\": \"{}\", \"failure\": \"{}\", \"ticks\": {}, \"uiWorld\": \"{:#x}\", \"camera\": [{:.1f}, {:.1f}, {:.1f}], "
        "\"rotation\": [{}, {}, {}], \"fov\": {:.2f}, \"aspect\": {:.3f}, \"loaded\": {}, \"chain\": {}, \"uiTicks\": {}, \"gameUiTicks\": {}}}",
        gS.wanted ? "true" : "false", gS.screen, gS.failure, gS.ticks, gS.camera.world, gS.camera.location.x, gS.camera.location.y,
        gS.camera.location.z, gS.camera.rotation.pitch, gS.camera.rotation.yaw, gS.camera.rotation.roll, gS.camera.fov, gS.camera.aspect,
        gS.camera.loaded ? "true" : "false", gS.camera.chain != nullptr ? "true" : "false", gS.uiTicks, gS.gameUiTicks);
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    gStatusJson = std::move(json);
}

void Fail(std::string why) {
    if (why != gS.loggedFailure) {
        log::Info("ui world figure: no squad member behind the {} screen: {}", gS.screen, why);
        gS.loggedFailure = why;
    }
    gS.failure = std::move(why);
}

#if defined(MEVR_GAME_LE1)

// The UI world and its camera as they are now; the reason in `why` if a piece is missing.
std::optional<Camera> ReadCamera(std::string& why) {
    Camera c;
    ue3::Object* const controller = player::Controller();
    ue3::Object* const worldInfo = controller != nullptr ? gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    if (worldInfo == nullptr) {
        why = "no player or WorldInfo";
        return std::nullopt;
    }
    c.uiWorld = gUiWorld.Read<ue3::Object*>(worldInfo).value_or(nullptr);
    if (c.uiWorld == nullptr || !ue3::runtime::IsAlive(c.uiWorld)) {
        why = gUiWorld.Get() == nullptr ? "BioWorldInfo.m_UIWorld not found" : "the WorldInfo has no UI world";
        return std::nullopt;
    }
    c.world = gUiWorldPointer.Read<std::uint64_t>(c.uiWorld).value_or(0);
    c.loaded = gUiLoaded.ReadBool(c.uiWorld).value_or(false);
    c.actor = gUiCamera.Read<ue3::Object*>(c.uiWorld).value_or(nullptr);
    c.chain = gUiChain.Read<ue3::Object*>(c.uiWorld).value_or(nullptr);
    if (c.world == 0) {
        why = c.loaded ? "the UI world has no world pointer" : "the UI world isn't loaded";
        return std::nullopt;
    }
    if (c.actor == nullptr || !ue3::runtime::IsAlive(c.actor) || !ue3::IsA(c.actor, gCameraActorClass)) {
        why = "the UI world has no camera actor";
        return std::nullopt;
    }
    auto const location = gLocation.Read<ue3::Vector>(c.actor);
    auto const rotation = gRotation.Read<ue3::Rotator>(c.actor);
    auto const fov = gCameraFov.Read<float>(c.actor);
    if (!location || !rotation || !fov || !(*fov > 1.0f && *fov < 170.0f)) {
        why = "the UI world's camera has no place or field of view";
        return std::nullopt;
    }
    c.location = *location;
    c.rotation = *rotation;
    c.fov = *fov;
    c.aspect = gCameraAspect.Read<float>(c.actor).value_or(0.0f);
    return c;
}

ue3::Vector SpawnPlace() {
    ue3::Object* const controller = player::Controller();
    return controller != nullptr ? gLocation.Read<ue3::Vector>(controller).value_or(ue3::Vector{}) : ue3::Vector{};
}

#endif

} // namespace

void Off(char const* why) {
#if defined(MEVR_GAME_LE1)
    if (gRig.Enabled()) {
        if (gRig.Alive()) {
            gRig.SetEnabled(false);
        }
        log::Info("ui world figure: the capture is off ({})", why);
    }
#else
    (void)why;
#endif
    if (gS.wanted) {
        gS.wanted = false;
        gS.loggedUp = false;
        gS.failure.clear();
        gS.loggedFailure.clear();
        Publish();
    }
}

void Tick(wu::Settings const& settings, wu::Screen screen, wu::State& state) {
    if (!settings.figure || !wu::ShowsFigure(screen)) {
        Off(!settings.figure ? "[WorldUi] figure = 0" : "a screen without a squad member");
        return;
    }
    gS.screen = wu::Name(screen);
    gS.wanted = true;
#if defined(MEVR_GAME_LE1)
    std::string why;
    std::optional<Camera> camera = ReadCamera(why);
    if (camera) {
        TickUiWorld(camera->uiWorld, camera->actor);  // the game doesn't while the screen isn't full screen
        gS.uiTicks = gTick.ours;
        gS.gameUiTicks = gTick.games;
        camera = ReadCamera(why);                     // its Kismet may have moved the camera
    }
    if (!camera) {
        Fail(why);
        if (gRig.Enabled() && gRig.Alive()) {
            gRig.SetEnabled(false);
        }
        Publish();
        return;
    }
    gS.camera = *camera;
    if (!gRig.Alive()) {  // not made yet, or a level change took it (Create forgets the old one)
        capture_rig::Options options;
        options.width = kWidth;
        options.height = kHeight;
        options.floatTarget = false;  // display-ready, post-processed (the UI world's own chain)
        options.fovDeg = camera->fov;
        options.nearUnits = 10.0f;
        options.postProcess = true;
        options.postProcessChain = camera->chain;
        if (!gRig.Create("ui world figure", options, SpawnPlace())) {
            Fail(std::format("the capture couldn't be made ({})", gRig.Failure()));
            Publish();
            return;
        }
    }
    if (!gRig.SetFov(camera->fov) || !gRig.Point(camera->location, camera->rotation) || (!gRig.Enabled() && !gRig.SetEnabled(true))) {
        Fail("the capture couldn't be pointed or enabled");
        Publish();
        return;
    }
    gRig.Tick();
    ++gS.ticks;
    gS.failure.clear();
    if (!gS.loggedUp) {
        gS.loggedUp = true;
        gS.loggedFailure.clear();
        log::Info("ui world figure: the {} screen's squad member: our capture looks through the UI world's camera {} at ({:.1f}, {:.1f}, "
                  "{:.1f}), rotation ({}, {}, {}), {:.1f} degrees across (aspect {:.3f}), its world {:#x}{}, post process {}",
            gS.screen, ue3::runtime::FullName(camera->actor), camera->location.x, camera->location.y, camera->location.z,
            camera->rotation.pitch, camera->rotation.yaw, camera->rotation.roll, camera->fov, camera->aspect, camera->world,
            camera->loaded ? "" : " (not marked loaded)",
            camera->chain != nullptr ? ue3::runtime::FullName(camera->chain) : std::string("the player's"));
    }
    // Which capture is ours from its first tick (the host keeps Umbra's camera query off it and renders it with the UI world's
    // scene); shown once it has been pointed and sent before a Draw at least once.
    state.figure = gRig.EnabledTicks() >= 2 ? 1 : 0;
    state.figureWidth = static_cast<std::uint16_t>(gRig.Width());
    state.figureHeight = static_cast<std::uint16_t>(gRig.Height());
    state.figureFormat = gRig.PixelFormat() != 0 ? gRig.PixelFormat() : kPixelFormatA8R8G8B8;
    state.figureOpacity = settings.figureOpacity;
    state.figureResource = gRig.Resource();
    state.figureTarget = gRig.Target();
    state.levelWorld = reinterpret_cast<std::uint64_t>(engine::World());
    state.uiWorld = camera->world;
    Publish();
#else
    (void)state;
    Fail("LE1 only");
#endif
}

void Shutdown() {
#if defined(MEVR_GAME_LE1)
    gRig.Destroy("the mod unloads");
#endif
    gS = Status{};
    Publish();
}

std::string Command(std::string_view args) {
    (void)args;
#if defined(MEVR_GAME_LE1)
    std::string why;
    std::optional<Camera> const camera = ReadCamera(why);
    if (!camera) {
        return std::format("ui world: {}", why);
    }
    // Diagnostics for the native side: the object, its vtable, the native command queue's size (TArray at +0x140) and m_bEnabled.
    auto const* const raw = reinterpret_cast<std::uint8_t const*>(camera->uiWorld);
    std::uint64_t vtable = 0;
    std::int32_t queued = 0;
    std::memcpy(&vtable, raw, sizeof vtable);
    std::memcpy(&queued, raw + 0x148, sizeof queued);
    return std::format("ui world {} at {:#x} (vtable {:#x}, {} queued native commands, enabled {}, loaded {}, world {:#x}): camera {} at "
                       "({:.1f}, {:.1f}, {:.1f}), rotation ({}, {}, {}), FOV {:.2f}, "
                       "aspect {:.3f}; post-process chain {}; our capture {} ({} ticks looking, {}x{}, target {:#x}, resource {:#x}){}",
        ue3::runtime::FullName(camera->uiWorld), reinterpret_cast<std::uint64_t>(camera->uiWorld), vtable, queued,
        gUiEnabled.ReadBool(camera->uiWorld).value_or(false) ? "yes" : "no", camera->loaded ? "yes" : "no", camera->world,
        ue3::runtime::FullName(camera->actor),
        camera->location.x, camera->location.y, camera->location.z, camera->rotation.pitch, camera->rotation.yaw, camera->rotation.roll,
        camera->fov, camera->aspect, camera->chain != nullptr ? ue3::runtime::FullName(camera->chain) : std::string("none"),
        gRig.Alive() ? (gRig.Enabled() ? "on" : "off") : "not made", gS.ticks, gRig.Width(), gRig.Height(), gRig.Target(), gRig.Resource(),
        gS.failure.empty() ? std::string() : std::format("; last failure: {}", gS.failure));
#else
    return "ui world: LE1 only";
#endif
}

std::string StatusJson() {
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    return gStatusJson;
}

} // namespace mevr::game::ui_world_figure
